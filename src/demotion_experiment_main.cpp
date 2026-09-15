// H3 fix (results/ecc_review.md finding H3): a dedicated demotion ON-vs-OFF
// comparison. Before this tool existed, P0.4's demotion mechanism
// (PolicyConfigV2::coldIdleEpochs / SymTabV2::runMaintenance()) had zero
// experimental evidence it was net-beneficial, and decide()'s own comment
// (symtab_v2.hpp) documents a finding that pushes the other way (repeat-
// heavy names belong in INTERNED, not COMPRESSED -- and every demotion
// candidate is, by construction, a name that was accessed >= hotAccessThreshold
// times before it qualified). This tool measures the actual effect rather
// than assuming one.
//
// Two workloads, deliberately at opposite ends of the axis demotion is
// supposed to help on:
//   hot-then-cold: a "hot" subset is accessed heavily EARLY (crossing
//     hotAccessThreshold -> promoted), then NEVER accessed again for the
//     rest of the run. This is the case demotion is explicitly designed
//     for -- if it doesn't help HERE, it doesn't help anywhere.
//   sustained-hot: the SAME hot subset stays hot for the ENTIRE run
//     (re-accessed periodically throughout, interleaved with filler
//     accesses that advance the epoch clock). This is the adversarial case:
//     decide()'s comment predicts demotion should HURT here, since a
//     genuinely repeat-heavy name gets bounced back into COMPRESSED only to
//     immediately need re-promotion.
//
// Both workloads also include real scope churn (short-lived nested scopes
// with repeated short names) interleaved throughout, both to advance the
// epoch clock realistically (not just via filler resolve() calls on the
// same population) and to exercise free-list slot reuse UNDER demotion at
// experiment scale -- the exact combination the ECC review's C1 fix
// (allocSlot()/insert() resetting wasPromoted/lastAccessEpoch on reuse) is
// meant to make safe.
//
// For every (workload, demotion on/off) cell, the SAME exact operation
// sequence (same seed) is replayed on a fresh table -- only coldIdleEpochs
// differs -- so the comparison is apples-to-apples.
//
// Usage: ./demotion_experiment.exe
// Writes results/demotion_experiment.csv.
#include <algorithm>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include "../include/dataset_generators.hpp"
#include "../include/hires_timer.hpp"
#include "../include/symtab_v2.hpp"

using namespace budgetsym;
using budgetsym::v2::PolicyConfigV2;
using budgetsym::v2::Rep;
using budgetsym::v2::SymTabV2;

struct ExperimentRow {
    std::string workload;
    bool demotionEnabled = false;
    size_t coldIdleEpochs = 0, hotAccessThreshold = 0;
    size_t declarations = 0, uniqueNames = 0;
    size_t promotions = 0, demotions = 0;
    long long modeledMemoryBytes = 0;
    double finalLookupP50Us = 0.0, finalLookupP95Us = 0.0, finalLookupP99Us = 0.0, finalLookupMeanUs = 0.0;
    size_t reconstructionCount = 0, reconstructionStepsTotal = 0;
    double meanReconstructionDepth = 0.0;
    size_t countInline = 0, countInterned = 0, countCompressed = 0;
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

// Runs the full operation trace (insert -> heavy hot access -> filler/churn
// with periodic maintenance ticks -> final measurement pass) against a
// freshly-constructed table with the given coldIdleEpochs, and returns the
// measured row. `sustainedHot` selects which of the two workloads to run.
static ExperimentRow runOne(HiResTimer& timer, const std::string& workloadName, bool sustainedHot,
                             size_t coldIdleEpochs, unsigned seed) {
    ExperimentRow r;
    r.workload = workloadName;
    r.demotionEnabled = coldIdleEpochs > 0;
    r.coldIdleEpochs = coldIdleEpochs;

    PolicyConfigV2 cfg;
    cfg.inlineMaxLen = 1;       // force every declared name (all long, see below) out of INLINE
    cfg.compressMinLen = 2;     // every unique long name starts COMPRESSED
    cfg.hotAccessThreshold = 3;
    cfg.coldIdleEpochs = coldIdleEpochs;
    r.hotAccessThreshold = cfg.hotAccessThreshold;

    SymTabV2<> t(1ull << 30, cfg);

    const int nMain = 3000;      // main population, all unique, all initially COMPRESSED
    const int hotFraction = nMain / 5; // first 20% is the "hot" subset
    Dataset main = genHighPrefixSimilaritySeeded(nMain, seed, 1ull << 30);
    for (auto& n : main.identifiers) t.insert(n);
    r.declarations = main.identifiers.size();
    r.uniqueNames = main.identifiers.size();

    // Phase A: drive the hot subset past hotAccessThreshold -> promotion.
    for (int i = 0; i < hotFraction; i++) {
        for (size_t k = 0; k < cfg.hotAccessThreshold; k++) t.resolve(main.identifiers[i]);
    }

    // Phase B: churn + (workload-dependent) hot-subset access, interleaved
    // with periodic runMaintenance() ticks so demotion (when enabled) gets
    // real opportunities to fire, not just one sweep at the very end.
    const int churnRounds = 400;
    const int maintenanceEvery = 25; // roughly matches coldIdleEpochs's scale used below
    for (int round = 0; round < churnRounds; round++) {
        // Real scope churn: a short-lived nested scope with a handful of
        // SHORT, repeated names -- exercises free-list slot reuse (the C1
        // fix's target) under demotion at experiment scale, and advances
        // the epoch clock via genuine resolve() traffic, not synthetic
        // padding.
        t.enterScope();
        for (int k = 0; k < 5; k++) {
            std::string shortName = "v" + std::to_string(k); // short + repeated across rounds -> decide()-native INTERNED after round 0
            t.insert(shortName);
            t.resolve(shortName);
        }
        t.exitScope();

        if (sustainedHot) {
            // Keep the ENTIRE hot subset genuinely hot: touch every one of
            // them every round, so no hot entry's lastAccessEpoch is ever
            // more than one round's worth of epoch ticks stale relative to
            // "now" -- i.e. never idle long enough to become demotion-
            // eligible under any reasonable coldIdleEpochs. A single
            // rotating index (touching one hot name per round) was tried
            // first and rejected: it left most of the 600-name hot subset
            // idle for hundreds of rounds between touches, which is not
            // meaningfully different from hot-then-cold at the population
            // level (confirmed empirically: it produced the same demotion
            // count as hot-then-cold, defeating the point of a contrasting
            // workload).
            for (int i = 0; i < hotFraction; i++) t.resolve(main.identifiers[i]);
        } else {
            // hot-then-cold: never touch the hot subset again here. Advance
            // the epoch clock purely via COLD-population filler lookups
            // instead, so the hot subset's idle time grows every round.
            int idx = hotFraction + (round % (nMain - hotFraction));
            t.resolve(main.identifiers[idx]);
        }

        if ((round + 1) % maintenanceEvery == 0) t.runMaintenance();
    }
    t.runMaintenance(); // final sweep, so a just-barely-idle-enough entry isn't missed by round-off

    r.promotions = t.promotions();
    r.demotions = t.demotions();
    r.modeledMemoryBytes = t.tracker().current();

    // Final measurement pass: resolve every name in the main population
    // once, timed, with reconstruction counters snapshotted immediately
    // before/after (delta-isolated -- same discipline as the H2 fix in
    // block_compression_sweep_main.cpp, so nothing else in this function
    // can contaminate the reported reconstruction numbers).
    size_t reconBefore = t.reconstructionCount();
    size_t stepsBefore = t.reconstructionStepsTotal();
    std::vector<double> samples;
    samples.reserve(main.identifiers.size());
    volatile bool sink = false;
    for (auto& n : main.identifiers) {
        auto a = timer.now();
        int id = t.resolve(n);
        auto b = timer.now();
        sink = sink || (id >= 0);
        samples.push_back(timer.microsecondsBetween(a, b));
    }
    (void)sink;
    size_t reconAfter = t.reconstructionCount();
    size_t stepsAfter = t.reconstructionStepsTotal();
    r.reconstructionCount = reconAfter - reconBefore;
    r.reconstructionStepsTotal = stepsAfter - stepsBefore;
    r.meanReconstructionDepth = r.reconstructionCount > 0
        ? static_cast<double>(r.reconstructionStepsTotal) / static_cast<double>(r.reconstructionCount) : 0.0;
    summarize(samples, r.finalLookupP50Us, r.finalLookupP95Us, r.finalLookupP99Us, r.finalLookupMeanUs);

    // Representation distribution: a diagnostic-only pass, run AFTER every
    // timed/counted metric above has already been captured, so its own
    // reconstruction cost (representationOf() on a COMPRESSED candidate
    // does reconstruct -- see symtab_v2.hpp's corrected doc comment) cannot
    // contaminate anything reported.
    for (auto& n : main.identifiers) {
        switch (t.representationOf(n)) {
            case Rep::INLINE_REP: r.countInline++; break;
            case Rep::INTERNED_REP: r.countInterned++; break;
            case Rep::COMPRESSED_REP: r.countCompressed++; break;
        }
    }

    return r;
}

static void writeHeader(std::ofstream& out) {
    out << "workload,demotion_enabled,cold_idle_epochs,hot_access_threshold,declarations,unique_names,"
           "promotions,demotions,modeled_memory_bytes,final_lookup_p50_us,final_lookup_p95_us,"
           "final_lookup_p99_us,final_lookup_mean_us,reconstruction_count,reconstruction_steps_total,"
           "mean_reconstruction_depth,count_inline,count_interned,count_compressed\n";
}

static void writeRow(std::ofstream& out, const ExperimentRow& r) {
    out << r.workload << "," << (r.demotionEnabled ? 1 : 0) << "," << r.coldIdleEpochs << ","
        << r.hotAccessThreshold << "," << r.declarations << "," << r.uniqueNames << "," << r.promotions << ","
        << r.demotions << "," << r.modeledMemoryBytes << "," << r.finalLookupP50Us << "," << r.finalLookupP95Us
        << "," << r.finalLookupP99Us << "," << r.finalLookupMeanUs << "," << r.reconstructionCount << ","
        << r.reconstructionStepsTotal << "," << r.meanReconstructionDepth << "," << r.countInline << ","
        << r.countInterned << "," << r.countCompressed << "\n";
}

int main() {
    HiResTimer timer;
    const unsigned seed = 5501;
    const size_t coldIdleEpochsOn = 40; // small enough that the churn loop's ~400 rounds gives many maintenance windows

    // Warmup: run once, discarded, so the first measured cell isn't
    // penalized by cold process/allocator state relative to later ones.
    { auto warm = runOne(timer, "warmup", true, 0, seed); (void)warm; }

    std::vector<ExperimentRow> rows;
    for (bool sustainedHot : {false, true}) {
        std::string workload = sustainedHot ? "sustained-hot" : "hot-then-cold";
        std::cout << "running " << workload << " (demotion OFF)\n";
        rows.push_back(runOne(timer, workload, sustainedHot, 0, seed));
        std::cout << "running " << workload << " (demotion ON, coldIdleEpochs=" << coldIdleEpochsOn << ")\n";
        rows.push_back(runOne(timer, workload, sustainedHot, coldIdleEpochsOn, seed));
    }

    std::ofstream out("results/demotion_experiment.csv");
    if (!out) {
        std::cerr << "ERROR: cannot write results/demotion_experiment.csv (run from repo root)\n";
        return 1;
    }
    writeHeader(out);
    for (auto& r : rows) writeRow(out, r);
    std::cout << "Wrote results/demotion_experiment.csv (" << rows.size() << " rows)\n";

    // Print a plain-language summary of the comparison so the result isn't
    // buried in a CSV a reader has to parse by hand.
    std::cout << "\n== Demotion ON vs OFF summary ==\n";
    for (size_t i = 0; i + 1 < rows.size(); i += 2) {
        const ExperimentRow& off = rows[i];
        const ExperimentRow& on = rows[i + 1];
        double memDeltaPct = off.modeledMemoryBytes > 0
            ? 100.0 * (static_cast<double>(on.modeledMemoryBytes) - static_cast<double>(off.modeledMemoryBytes))
                  / static_cast<double>(off.modeledMemoryBytes)
            : 0.0;
        double latDeltaPct = off.finalLookupMeanUs > 0
            ? 100.0 * (on.finalLookupMeanUs - off.finalLookupMeanUs) / off.finalLookupMeanUs : 0.0;
        std::cout << off.workload << ": memory " << (memDeltaPct >= 0 ? "+" : "") << memDeltaPct
                  << "%, mean lookup latency " << (latDeltaPct >= 0 ? "+" : "") << latDeltaPct
                  << "%, demotions=" << on.demotions << ", promotions off/on=" << off.promotions << "/"
                  << on.promotions << "\n";
    }
    return 0;
}
