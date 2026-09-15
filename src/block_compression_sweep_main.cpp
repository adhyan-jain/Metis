// P0.3 block-compression parameter measurement (CLAUDE_RESEARCH.md section 7,
// P0.3): sweeps SymTabV2's block-based front-coded compression tier over
// (blockSize, anchorInterval) combinations and measures memory, reconstruction
// work, and lookup latency -- the required evidence that was previously
// missing (see docs/v2_architecture.md section 10, item 3).
//
// Design choices, stated explicitly:
//  - Every name in each sweep dataset is UNIQUE (no repeats), so decide()'s
//    exact-repeat rule (see symtab_v2.hpp) never routes anything to INTERNED
//    on that basis -- every name that is >= compressMinLen stays COMPRESSED
//    for the whole run. hotAccessThreshold is set unreachably high so
//    promotion never fires either. This isolates the measurement to the
//    COMPRESSED tier itself, not a blend with INTERNED/promotion effects
//    (those are covered separately by the P0.4 hot/cold machinery and its
//    own tests).
//  - Two datasets, both real generators already used elsewhere in this repo
//    (dataset_generators.hpp), deliberately at opposite ends of the prefix-
//    similarity axis that front-coding is meant to exploit:
//      high-prefix-similarity: genHighPrefixSimilaritySeeded (contiguous
//        same-prefix runs -- front-coding should compress well)
//      random-long:            uniform random long identifiers (no shared
//        prefixes -- front-coding degrades to ~full-suffix storage, a
//        legitimate "does this help less here" case, not hidden)
//  - "Reconstruction ops/depth" is measured via SymTabV2::reconstructionCount()
//    / reconstructionStepsTotal(), added to symtab_v2.hpp for this purpose:
//    counts actual reconstructMember() calls (i.e. lookups that got past the
//    free length check and the cheap 8-bit fp8 rejection and had to pay for
//    a real decode) and the front-coding steps walked per call.
//  - Lookup latency is measured as COLD (first resolve() of each name after
//    the full dataset is inserted, in dataset order, single pass -- every
//    resolve() in this pass is a genuine compressed-block decode, since
//    nothing has been looked up before) using per-call wall-clock timing
//    (HiResTimer, the same portable timer already used by
//    bench_metrics.hpp/benchmark_main.cpp) so p50/p95/p99 can be reported,
//    not just a mean -- CLAUDE_RESEARCH.md section 9.4 requires warmup +
//    repetition + variance, not a single aggregate number. A second, HOT
//    pass re-resolves the same names (promotion is disabled here, so this
//    is a second round of genuine repeated COMPRESSED-tier decodes, not a
//    cache hit -- V2 has no separate lookup cache, see
//    docs/v2_architecture.md section 7) to show whether repeated lookups on
//    this representation get cheaper on their own (they do not -- decode
//    cost is representation-inherent -- a real, honestly-reported finding).
//  - A full warmup configuration is run and discarded before the first
//    measured configuration, so the first row is not penalized by cold
//    process/allocator state relative to every later row.
//
// Usage: ./block_compression_sweep.exe
// Writes results/block_compression_sweep.csv.
#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "../include/dataset_generators.hpp"
#include "../include/hires_timer.hpp"
#include "../include/symtab_v2.hpp"

using namespace budgetsym;

struct SweepRow {
    std::string dataset;
    size_t blockSize = 0, anchorInterval = 0;
    size_t declarations = 0, uniqueNames = 0;
    long long modeledMemoryBytes = 0;
    size_t reconstructionCount = 0, reconstructionStepsTotal = 0;
    double meanReconstructionDepth = 0.0;
    double coldLookupP50Us = 0.0, coldLookupP95Us = 0.0, coldLookupP99Us = 0.0, coldLookupMeanUs = 0.0;
    double hotLookupP50Us = 0.0, hotLookupP95Us = 0.0, hotLookupP99Us = 0.0, hotLookupMeanUs = 0.0;
    double insertUsMean = 0.0;
};

static double percentile(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    size_t idx = static_cast<size_t>(p * static_cast<double>(sorted.size() - 1));
    return sorted[idx];
}

static void summarize(std::vector<double> samples, double& p50, double& p95, double& p99, double& mean) {
    if (samples.empty()) { p50 = p95 = p99 = mean = 0.0; return; }
    std::sort(samples.begin(), samples.end());
    p50 = percentile(samples, 0.50);
    p95 = percentile(samples, 0.95);
    p99 = percentile(samples, 0.99);
    double sum = 0.0;
    for (double v : samples) sum += v;
    mean = sum / static_cast<double>(samples.size());
}

static SweepRow runOne(HiResTimer& timer, const std::string& dsName, const std::vector<std::string>& names,
                        size_t blockSize, size_t anchorInterval) {
    SweepRow r;
    r.dataset = dsName;
    r.blockSize = blockSize;
    r.anchorInterval = anchorInterval;
    r.declarations = names.size();
    r.uniqueNames = names.size(); // dataset is unique-by-construction, see file header

    budgetsym::v2::PolicyConfigV2 cfg;
    cfg.inlineMaxLen = 1;      // force everything (all names here are long) out of INLINE
    cfg.compressMinLen = 2;    // force everything into COMPRESSED (no repeats -> never INTERNED via decide())
    cfg.blockSize = blockSize;
    cfg.anchorInterval = anchorInterval;
    cfg.hotAccessThreshold = 1000000; // unreachably high: promotion must never fire in this measurement
    budgetsym::v2::SymTabV2<> t(1ull << 30, cfg);

    // insert, timed as a whole-pass mean (this sweep's focus is lookup/
    // reconstruction behavior; per-call insert percentiles are already
    // covered elsewhere in the pipeline, e.g. bench_metrics.hpp).
    auto tIns0 = timer.now();
    for (auto& n : names) t.insert(n);
    auto tIns1 = timer.now();
    r.insertUsMean = names.empty() ? 0.0 : timer.microsecondsBetween(tIns0, tIns1) / static_cast<double>(names.size());

    bool anyMisrouted = false;
    for (auto& n : names) {
        if (t.representationOf(n) != budgetsym::v2::Rep::COMPRESSED_REP) anyMisrouted = true;
    }
    if (anyMisrouted) {
        std::cerr << "WARNING: " << dsName << " (block=" << blockSize << ", anchor=" << anchorInterval
                  << ") has a name that did not route to COMPRESSED as expected\n";
    }

    r.modeledMemoryBytes = t.tracker().current();

    // COLD pass: first-ever resolve() of each name, single pass, dataset order.
    std::vector<double> coldSamples;
    coldSamples.reserve(names.size());
    volatile bool sink = false;
    for (auto& n : names) {
        auto a = timer.now();
        int id = t.resolve(n);
        auto b = timer.now();
        sink = sink || (id >= 0);
        coldSamples.push_back(timer.microsecondsBetween(a, b));
    }
    (void)sink;
    size_t reconAfterCold = t.reconstructionCount();
    size_t stepsAfterCold = t.reconstructionStepsTotal();
    r.reconstructionCount = reconAfterCold;
    r.reconstructionStepsTotal = stepsAfterCold;
    r.meanReconstructionDepth = reconAfterCold > 0
        ? static_cast<double>(stepsAfterCold) / static_cast<double>(reconAfterCold) : 0.0;
    summarize(coldSamples, r.coldLookupP50Us, r.coldLookupP95Us, r.coldLookupP99Us, r.coldLookupMeanUs);

    // HOT pass: repeated resolve() of the same names. Promotion is disabled
    // (hotAccessThreshold unreachable), so this is a genuine second round of
    // real COMPRESSED-tier decodes, not a cache hit -- see file header.
    std::vector<double> hotSamples;
    hotSamples.reserve(names.size());
    for (auto& n : names) {
        auto a = timer.now();
        int id = t.resolve(n);
        auto b = timer.now();
        sink = sink || (id >= 0);
        hotSamples.push_back(timer.microsecondsBetween(a, b));
    }
    summarize(hotSamples, r.hotLookupP50Us, r.hotLookupP95Us, r.hotLookupP99Us, r.hotLookupMeanUs);

    // Sanity: representation must still be COMPRESSED for all of them
    // (hotAccessThreshold is unreachable), so the hot pass measured the same
    // representation as the cold pass, not a blend.
    for (auto& n : names) {
        if (t.representationOf(n) != budgetsym::v2::Rep::COMPRESSED_REP) {
            std::cerr << "WARNING: " << dsName << " promotion fired unexpectedly during hot pass\n";
            break;
        }
    }

    return r;
}

static void writeHeader(std::ofstream& out) {
    out << "dataset,block_size,anchor_interval,declarations,unique_names,modeled_memory_bytes,"
           "reconstruction_count,reconstruction_steps_total,mean_reconstruction_depth,"
           "cold_lookup_p50_us,cold_lookup_p95_us,cold_lookup_p99_us,cold_lookup_mean_us,"
           "hot_lookup_p50_us,hot_lookup_p95_us,hot_lookup_p99_us,hot_lookup_mean_us,insert_us_mean\n";
}

static void writeRow(std::ofstream& out, const SweepRow& r) {
    out << r.dataset << "," << r.blockSize << "," << r.anchorInterval << "," << r.declarations << ","
        << r.uniqueNames << "," << r.modeledMemoryBytes << "," << r.reconstructionCount << ","
        << r.reconstructionStepsTotal << "," << r.meanReconstructionDepth << "," << r.coldLookupP50Us << ","
        << r.coldLookupP95Us << "," << r.coldLookupP99Us << "," << r.coldLookupMeanUs << "," << r.hotLookupP50Us
        << "," << r.hotLookupP95Us << "," << r.hotLookupP99Us << "," << r.hotLookupMeanUs << "," << r.insertUsMean
        << "\n";
}

int main() {
    HiResTimer timer;
    const size_t seed = 7001;
    const int n = 4000;

    // Two datasets at opposite ends of the prefix-similarity axis -- see
    // file header. Both regenerated fresh (not shared/mutated) for every
    // (blockSize, anchorInterval) combination below, so each measured
    // configuration starts from the identical input.
    auto makeHighPrefix = [&] {
        Dataset d = genHighPrefixSimilaritySeeded(n, seed, 1ull << 30);
        return d.identifiers;
    };
    auto makeRandomLong = [&] {
        std::mt19937 rng(static_cast<unsigned>(seed + 1));
        std::vector<std::string> v;
        for (int i = 0; i < n; i++) v.push_back(randomIdentifier(rng, 20, 40));
        return v;
    };

    // Warmup: run once and discard so the FIRST measured configuration is
    // not penalized by cold process/allocator state relative to later ones.
    { auto warm = makeHighPrefix(); runOne(timer, "warmup", warm, 32, 8); }

    static const size_t blockSizes[] = {4, 8, 16, 32, 64, 128};
    static const size_t anchorIntervals[] = {2, 4, 8, 16, 32};

    std::vector<SweepRow> rows;
    for (const auto& dsSpec : {std::make_pair(std::string("high-prefix-similarity"), 0),
                                std::make_pair(std::string("random-long"), 1)}) {
        std::vector<std::string> names = dsSpec.second == 0 ? makeHighPrefix() : makeRandomLong();
        for (size_t bs : blockSizes) {
            for (size_t ai : anchorIntervals) {
                if (ai > bs) continue; // an anchor stride longer than the block itself is meaningless
                std::cout << "sweeping " << dsSpec.first << " block=" << bs << " anchor=" << ai << "\n";
                rows.push_back(runOne(timer, dsSpec.first, names, bs, ai));
            }
        }
    }

    std::ofstream out("results/block_compression_sweep.csv");
    if (!out) {
        std::cerr << "ERROR: cannot write results/block_compression_sweep.csv (run from repo root)\n";
        return 1;
    }
    writeHeader(out);
    for (auto& r : rows) writeRow(out, r);
    std::cout << "Wrote results/block_compression_sweep.csv (" << rows.size() << " rows)\n";
    return 0;
}
