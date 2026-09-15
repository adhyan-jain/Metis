// RESEARCH-GRADE BENCHMARK: unified latency + memory comparison
// Conventional vs Interned vs BudgetSym V1 vs SymTabV2
// (CLAUDE_RESEARCH.md section 9 -- Benchmark Infrastructure)
//
// ============================================================================
// DESIGN NOTE -- SCOPE METHODOLOGY (important for scientific validity)
// ============================================================================
// Earlier draft used buildScopedTrace() which exits all inner scopes before
// lookup. This caused all inserted names to be reclaimed before the lookup
// pass, making every lookup a guaranteed-absent miss. Those results were
// scientifically invalid and are discarded.
//
// Correct methodology (matching bench_metrics.hpp and memory_audit_v2_main):
//  1. PRIMARY MEASUREMENT: insert all names into the GLOBAL scope (never exit
//     it). This keeps all names live for the lookup pass. Memory is measured
//     after insert with all symbols live.
//  2. SCOPE EXIT TIMING: separately, enter a fresh inner scope, insert N
//     fixed names, then time the exitScope() call. This measures scope-exit
//     overhead without poisoning the primary measurement state.
//
// This is the same design that bench_metrics.hpp's runOne() uses (it inserts
// into the global scope, then tacks a fresh scope on at the end to time exit).
//
// ============================================================================
// MEMORY MEASUREMENT
//   MODELED:  tracker_.current()/peak() -- per-entry cost constants (see each
//             table's header). Deterministic and cross-platform.
//   MEASURED: actual heap bytes via heap_counter.hpp (operator new accounting;
//             includes allocator overhead). Machine-specific.
//   Never mixed. Separate columns. Clearly labelled.
//
// LATENCY MEASUREMENT
//   cold lookup:   first resolve() of each name on this table instance
//   hot lookup:    5th resolve() of each name (crosses V2 hotAccessThreshold=3;
//                  V2-promoted names will use INTERNED path by then)
//   absent lookup: guaranteed-absent names ("ZZZ_ABSENT_N_XYZ")
//   insertion:     per-call timed via global-scope insert
//   scope_exit:    one timed exitScope() on a scope with seN symbols
//
//   Warmup: one full discarded run before each dataset.
//   Per-call timing: each call individually timed for p50/p95/p99.
//   DCE protection: volatile sink after each call.
//
// DATASETS (CLAUDE_RESEARCH.md section 11 taxonomy)
//   Category A: real-world corpora (FreeRTOS, Arduino, Zephyr) if present
//   Category B: high-prefix-similarity, hot-cold-access, nested-scopes,
//               small/medium/large regression baselines
//   Category C: random-long, high-churn, memory-stress
//
// Output: results/v2_latency_memory.csv
#define BUDGETSYM_HEAP_COUNTER_IMPL
#include "../include/heap_counter.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#include "../include/budget_sym.hpp"
#include "../include/conventional_symbol_table.hpp"
#include "../include/dataset_generators.hpp"
#include "../include/hires_timer.hpp"
#include "../include/interned_symbol_table.hpp"
#include "../include/symtab_v2.hpp"

using namespace budgetsym;
using namespace budgetsym::v2;

// ---- Statistics helpers ------------------------------------------------------
static double pctile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t idx = static_cast<size_t>(p * static_cast<double>(v.size() - 1));
    return v[idx];
}
static double vmean(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    double s = 0.0; for (double x : v) s += x;
    return s / static_cast<double>(v.size());
}
static double vstddev(const std::vector<double>& v) {
    if (v.size() < 2) return 0.0;
    double m = vmean(v), sq = 0.0;
    for (double x : v) sq += (x - m) * (x - m);
    return std::sqrt(sq / static_cast<double>(v.size() - 1));
}

// ---- Build lookup/absent sample sets -----------------------------------------
// deduplicated in first-occurrence order, capped at maxSample
static std::vector<std::string> buildLookupSample(const std::vector<std::string>& ids,
                                                    size_t maxSample) {
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    for (auto& n : ids) {
        if (seen.find(n) == seen.end()) {
            seen.insert(n);
            out.push_back(n);
            if (out.size() >= maxSample) break;
        }
    }
    return out;
}
static std::vector<std::string> buildAbsentSample(size_t n) {
    std::vector<std::string> out;
    for (size_t i = 0; i < n; i++) out.push_back("ZZZ_ABSENT_" + std::to_string(i) + "_XYZ");
    return out;
}

// ---- Result row --------------------------------------------------------------
struct BenchRow {
    std::string dataset, impl, category;
    size_t declarations = 0, uniqueNames = 0;

    // Memory (clearly distinguished)
    long long modeledPeakBytes = 0, modeledFinalBytes = 0;
    long long measuredPeakHeapBytes = 0, measuredFinalHeapBytes = 0;
    long long measuredBytesPerUniqueSymbol = 0;

    // Cold lookup: first resolve() of each sample name (all live in global scope)
    double coldP50 = 0, coldP95 = 0, coldP99 = 0, coldMean = 0, coldStddev = 0;
    // Hot lookup: 5th resolve() of each sample name
    double hotP50 = 0, hotP95 = 0, hotP99 = 0, hotMean = 0, hotStddev = 0;
    // Absent lookup: guaranteed-absent names
    double absP50 = 0, absP95 = 0, absP99 = 0, absMean = 0;
    // Insertion: per-call timed flat insert into global scope
    double insP50 = 0, insP95 = 0, insP99 = 0, insMean = 0;
    // Scope exit: one controlled exitScope() on a populated scope
    double scopeExitUs = 0;
    size_t scopeExitSymbols = 0;

    // V2-specific internal counters (0 for other impls)
    size_t promotions = 0, demotions = 0;
    size_t reconCount = 0, reconSteps = 0;
    double reconDepth = 0.0;
    // Representation distribution: counted over LOOKUP SAMPLE after all lookup passes
    // (post-measurement pass so reconstruction cost does not contaminate measured latencies)
    size_t cntInline = 0, cntInterned = 0, cntCompressed = 0;

    // V1 lookup-cache stats (0 for others)
    int v1CacheHits = 0, v1CacheMisses = 0;
    double v1CacheHitRate = 0.0;
};

// ---- Core per-call timed operations ------------------------------------------
template<typename Table>
static double timeOneInsert(HiResTimer& timer, Table& t, const std::string& name) {
    auto a = timer.now(); volatile int id = t.insert(name); auto b = timer.now();
    (void)id;
    return timer.microsecondsBetween(a, b);
}

template<typename Table>
static double timeOneResolve(HiResTimer& timer, Table& t, const std::string& name) {
    auto a = timer.now(); volatile int id = t.resolve(name); auto b = timer.now();
    (void)id;
    return timer.microsecondsBetween(a, b);
}

// ---- Insert all names into global scope, time per call ----------------------
template<typename Table>
static std::vector<double> insertFlat(HiResTimer& timer, Table& t,
                                       const std::vector<std::string>& ids) {
    std::vector<double> ins;
    ins.reserve(ids.size());
    for (auto& n : ids) ins.push_back(timeOneInsert(timer, t, n));
    return ins;
}

// ---- Collect cold + hot + absent lookup latencies ---------------------------
// All sample names are live in global scope (inserted by insertFlat above).
// cold: first resolve() of each name on this instance
// hot:  4 additional resolves → 5th total crosses V2's default hotAccessThreshold=3
template<typename Table>
static void collectLookup(HiResTimer& timer, Table& t,
                           const std::vector<std::string>& sample,
                           const std::vector<std::string>& absent,
                           std::vector<double>& coldS,
                           std::vector<double>& hotS,
                           std::vector<double>& absS) {
    coldS.clear(); hotS.clear(); absS.clear();
    // Cold
    for (auto& n : sample) coldS.push_back(timeOneResolve(timer, t, n));
    // 4 more passes; only record the last (pass 5, after promotion for V2)
    for (int p = 0; p < 4; p++) {
        for (size_t i = 0; i < sample.size(); i++) {
            double us = timeOneResolve(timer, t, sample[i]);
            if (p == 3) hotS.push_back(us);
        }
    }
    // Absent
    for (auto& n : absent) absS.push_back(timeOneResolve(timer, t, n));
}

// ---- Scope exit timing: SEPARATE scope, AFTER primary measurement -----------
// Enters a fresh inner scope, inserts seN random names, times the exit.
// Does NOT affect primary measurement state (global scope names remain live).
template<typename Table>
static double timeScopeExit(HiResTimer& timer, Table& t, size_t seN) {
    t.enterScope();
    std::mt19937 rng(99999);
    for (size_t i = 0; i < seN; i++) t.insert(randomIdentifier(rng, 6, 20));
    auto a = timer.now(); t.exitScope(); auto b = timer.now();
    return timer.microsecondsBetween(a, b);
}

// ---- Fill BenchRow from insertion + lookup sample vectors -------------------
static void fillTimings(BenchRow& r,
                         const std::vector<double>& ins,
                         const std::vector<double>& cs,
                         const std::vector<double>& hs,
                         const std::vector<double>& as_) {
    r.insP50=pctile(ins,.50); r.insP95=pctile(ins,.95);
    r.insP99=pctile(ins,.99); r.insMean=vmean(ins);
    r.coldP50=pctile(cs,.50); r.coldP95=pctile(cs,.95); r.coldP99=pctile(cs,.99);
    r.coldMean=vmean(cs); r.coldStddev=vstddev(cs);
    r.hotP50=pctile(hs,.50); r.hotP95=pctile(hs,.95); r.hotP99=pctile(hs,.99);
    r.hotMean=vmean(hs); r.hotStddev=vstddev(hs);
    r.absP50=pctile(as_,.50); r.absP95=pctile(as_,.95); r.absP99=pctile(as_,.99);
    r.absMean=vmean(as_);
}

// ---- Generic runner (Conventional / Interned) --------------------------------
template<typename Table>
static BenchRow runGeneric(HiResTimer& timer,
                            const std::string& dsName, const std::string& implName,
                            const std::string& cat,
                            const std::vector<std::string>& ids,
                            const std::vector<std::string>& sample,
                            const std::vector<std::string>& absent,
                            size_t seN) {
    // Warmup (full discarded run)
    { Table w(0);
      auto wi = insertFlat(timer, w, ids);
      std::vector<double> wc, wh, wa;
      collectLookup(timer, w, sample, absent, wc, wh, wa);
      timeScopeExit(timer, w, seN); (void)wi; }

    // Measured run -- heap scope begins just before table construction
    heap::Scope hs;
    Table t(0);
    auto ins = insertFlat(timer, t, ids);

    BenchRow r;
    r.dataset = dsName; r.impl = implName; r.category = cat;
    r.declarations = ids.size();
    { std::unordered_set<std::string> u(ids.begin(), ids.end()); r.uniqueNames = u.size(); }
    r.measuredFinalHeapBytes = hs.bytes();
    r.measuredPeakHeapBytes  = hs.peakBytes();
    r.modeledFinalBytes = t.tracker().current();
    r.modeledPeakBytes  = t.tracker().peak();
    r.measuredBytesPerUniqueSymbol = r.uniqueNames > 0
        ? r.measuredFinalHeapBytes / static_cast<long long>(r.uniqueNames) : 0;
    r.scopeExitSymbols = seN;

    std::vector<double> cs, hsvec, as_;
    collectLookup(timer, t, sample, absent, cs, hsvec, as_);
    r.scopeExitUs = timeScopeExit(timer, t, seN);
    fillTimings(r, ins, cs, hsvec, as_);
    return r;
}

// ---- V1 runner ---------------------------------------------------------------
static BenchRow runV1(HiResTimer& timer,
                       const std::string& dsName, const std::string& cat,
                       const std::vector<std::string>& ids,
                       const std::vector<std::string>& sample,
                       const std::vector<std::string>& absent, size_t seN) {
    // Warmup
    { BudgetSym w(0);
      auto wi = insertFlat(timer, w, ids);
      std::vector<double> wc, wh, wa;
      collectLookup(timer, w, sample, absent, wc, wh, wa);
      timeScopeExit(timer, w, seN); (void)wi; }

    heap::Scope hs;
    BudgetSym t(0);
    auto ins = insertFlat(timer, t, ids);

    BenchRow r;
    r.dataset = dsName; r.impl = "BudgetSymV1"; r.category = cat;
    r.declarations = ids.size();
    { std::unordered_set<std::string> u(ids.begin(), ids.end()); r.uniqueNames = u.size(); }
    r.measuredFinalHeapBytes = hs.bytes();
    r.measuredPeakHeapBytes  = hs.peakBytes();
    r.modeledFinalBytes = t.tracker().current();
    r.modeledPeakBytes  = t.tracker().peak();
    r.measuredBytesPerUniqueSymbol = r.uniqueNames > 0
        ? r.measuredFinalHeapBytes / static_cast<long long>(r.uniqueNames) : 0;
    r.scopeExitSymbols = seN;

    std::vector<double> cs, hsvec, as_;
    collectLookup(timer, t, sample, absent, cs, hsvec, as_);
    r.scopeExitUs = timeScopeExit(timer, t, seN);
    fillTimings(r, ins, cs, hsvec, as_);

    r.promotions = t.promotions();
    auto st = t.statistics();
    r.v1CacheHits = st.lookupCache.hits;
    r.v1CacheMisses = st.lookupCache.misses;
    r.v1CacheHitRate = st.lookupCache.hitRate;
    return r;
}

// ---- V2 runner ---------------------------------------------------------------
static BenchRow runV2(HiResTimer& timer,
                       const std::string& dsName, const std::string& cat,
                       const std::vector<std::string>& ids,
                       const std::vector<std::string>& sample,
                       const std::vector<std::string>& absent, size_t seN) {
    // Warmup
    { SymTabV2<> w(0);
      auto wi = insertFlat(timer, w, ids);
      std::vector<double> wc, wh, wa;
      collectLookup(timer, w, sample, absent, wc, wh, wa);
      timeScopeExit(timer, w, seN); (void)wi; }

    heap::Scope hs;
    SymTabV2<> t(0);
    auto ins = insertFlat(timer, t, ids);

    // Snapshot reconstruction counters BEFORE lookup pass (delta-isolated;
    // same discipline as ECC fix H2 in block_compression_sweep_main.cpp)
    size_t rcBefore = t.reconstructionCount();
    size_t rsBefore = t.reconstructionStepsTotal();

    BenchRow r;
    r.dataset = dsName; r.impl = "SymTabV2"; r.category = cat;
    r.declarations = ids.size();
    { std::unordered_set<std::string> u(ids.begin(), ids.end()); r.uniqueNames = u.size(); }
    r.measuredFinalHeapBytes = hs.bytes();
    r.measuredPeakHeapBytes  = hs.peakBytes();
    r.modeledFinalBytes = t.tracker().current();
    r.modeledPeakBytes  = t.tracker().peak();
    r.measuredBytesPerUniqueSymbol = r.uniqueNames > 0
        ? r.measuredFinalHeapBytes / static_cast<long long>(r.uniqueNames) : 0;
    r.scopeExitSymbols = seN;

    std::vector<double> cs, hsvec, as_;
    collectLookup(timer, t, sample, absent, cs, hsvec, as_);

    // Reconstruction delta: cold pass only (rcBefore was snapshotted before cold)
    r.reconCount = t.reconstructionCount() - rcBefore;
    r.reconSteps = t.reconstructionStepsTotal() - rsBefore;
    r.reconDepth = r.reconCount > 0
        ? static_cast<double>(r.reconSteps)/static_cast<double>(r.reconCount) : 0.0;

    r.scopeExitUs = timeScopeExit(timer, t, seN);
    fillTimings(r, ins, cs, hsvec, as_);

    r.promotions = t.promotions();
    r.demotions  = t.demotions();

    // Representation distribution: SEPARATE POST-MEASUREMENT PASS over lookup
    // sample so representationOf()'s reconstruction cost cannot contaminate
    // any timed or counter-measured metric captured above.
    // VALIDITY CHECK: all names in sample must be present (count_inline should
    // be >0 only if names are actually short enough for INLINE tier).
    for (auto& n : sample) {
        switch (t.representationOf(n)) {
            case Rep::INLINE_REP:     r.cntInline++;    break;
            case Rep::INTERNED_REP:   r.cntInterned++;  break;
            case Rep::COMPRESSED_REP: r.cntCompressed++; break;
        }
    }
    // Sanity: warn if all sample names are INLINE (likely means absent-name bug)
    if (r.cntInline == sample.size() && !sample.empty() && r.uniqueNames > 0) {
        std::cerr << "WARNING [" << dsName << " SymTabV2]: all " << sample.size()
                  << " sample names report INLINE -- possible absent-name measurement bug\n";
    }
    return r;
}

// ---- CSV output --------------------------------------------------------------
static void writeHeader(std::ofstream& out) {
    out << "dataset,implementation,category,"
           "declarations,unique_names,"
           "modeled_peak_bytes,modeled_final_bytes,"
           "measured_peak_heap_bytes,measured_final_heap_bytes,"
           "measured_bytes_per_unique_symbol,"
           "cold_lookup_p50_us,cold_lookup_p95_us,cold_lookup_p99_us,"
           "cold_lookup_mean_us,cold_lookup_stddev_us,"
           "hot_lookup_p50_us,hot_lookup_p95_us,hot_lookup_p99_us,"
           "hot_lookup_mean_us,hot_lookup_stddev_us,"
           "absent_lookup_p50_us,absent_lookup_p95_us,absent_lookup_p99_us,"
           "absent_lookup_mean_us,"
           "insertion_p50_us,insertion_p95_us,insertion_p99_us,insertion_mean_us,"
           "scope_exit_us,scope_exit_symbols,"
           "promotions,demotions,"
           "reconstruction_count,reconstruction_steps_total,mean_reconstruction_depth,"
           "count_inline,count_interned,count_compressed,"
           "v1_cache_hits,v1_cache_misses,v1_cache_hit_rate\n";
}

static void writeRow(std::ofstream& out, const BenchRow& r) {
    out << r.dataset << "," << r.impl << "," << r.category << ","
        << r.declarations << "," << r.uniqueNames << ","
        << r.modeledPeakBytes << "," << r.modeledFinalBytes << ","
        << r.measuredPeakHeapBytes << "," << r.measuredFinalHeapBytes << ","
        << r.measuredBytesPerUniqueSymbol << ","
        << r.coldP50 << "," << r.coldP95 << "," << r.coldP99 << ","
        << r.coldMean << "," << r.coldStddev << ","
        << r.hotP50 << "," << r.hotP95 << "," << r.hotP99 << ","
        << r.hotMean << "," << r.hotStddev << ","
        << r.absP50 << "," << r.absP95 << "," << r.absP99 << ","
        << r.absMean << ","
        << r.insP50 << "," << r.insP95 << "," << r.insP99 << "," << r.insMean << ","
        << r.scopeExitUs << "," << r.scopeExitSymbols << ","
        << r.promotions << "," << r.demotions << ","
        << r.reconCount << "," << r.reconSteps << "," << r.reconDepth << ","
        << r.cntInline << "," << r.cntInterned << "," << r.cntCompressed << ","
        << r.v1CacheHits << "," << r.v1CacheMisses << "," << r.v1CacheHitRate << "\n";
}

// ---- Dataset driver ----------------------------------------------------------
struct DatasetSpec {
    std::string name, category;
    std::vector<std::string> identifiers;
    size_t lookupSampleSize = 200;
    size_t scopeExitN = 100;
};

static void runDataset(HiResTimer& timer, const DatasetSpec& ds, std::ofstream& out) {
    std::cout << "  [" << ds.category << "] " << ds.name
              << " (" << ds.identifiers.size() << " decls)\n" << std::flush;

    auto sample = buildLookupSample(ds.identifiers, ds.lookupSampleSize);
    auto absent = buildAbsentSample(sample.size());

    auto printDone = [](const char* nm, const BenchRow& r) {
        std::cout << "    " << nm
                  << ": heap=" << r.measuredFinalHeapBytes
                  << "B  cold_p50=" << r.coldP50
                  << "us  hot_p50=" << r.hotP50
                  << "us  inline=" << r.cntInline
                  << " interned=" << r.cntInterned
                  << " comp=" << r.cntCompressed << "\n" << std::flush;
    };

    auto cr = runGeneric<ConventionalSymbolTable>(timer, ds.name, "Conventional",
                  ds.category, ds.identifiers, sample, absent, ds.scopeExitN);
    writeRow(out, cr); printDone("Conventional", cr);

    auto ir = runGeneric<InternedSymbolTable>(timer, ds.name, "Interned",
                  ds.category, ds.identifiers, sample, absent, ds.scopeExitN);
    writeRow(out, ir); printDone("Interned    ", ir);

    auto v1r = runV1(timer, ds.name, ds.category, ds.identifiers, sample, absent, ds.scopeExitN);
    writeRow(out, v1r); printDone("BudgetSymV1 ", v1r);

    auto v2r = runV2(timer, ds.name, ds.category, ds.identifiers, sample, absent, ds.scopeExitN);
    writeRow(out, v2r); printDone("SymTabV2    ", v2r);
}

// ---- Load corpus ids file ----------------------------------------------------
static std::vector<std::string> loadCorpusIds(const std::string& path, size_t maxLines = 50000) {
    std::vector<std::string> ids;
    std::ifstream f(path);
    if (!f) return ids;
    std::string line;
    size_t count = 0;
    while (std::getline(f, line) && count < maxLines) {
        if (!line.empty()) { ids.push_back(line); count++; }
    }
    return ids;
}

// ---- main -------------------------------------------------------------------
int main(int argc, char* argv[]) {
    HiResTimer timer;
    const size_t BUDGET = 4 * 1024 * 1024;
    const unsigned SEED = 42;

    std::ofstream out("results/v2_latency_memory.csv");
    if (!out) {
        std::cerr << "ERROR: cannot write results/v2_latency_memory.csv -- run from repo root\n";
        return 1;
    }
    writeHeader(out);

    std::cout << "=== Benchmark Infrastructure: unified latency + memory ===\n"
              << "    Methodology: flat global-scope insert; names live during lookup\n\n";

    // ---- Category B: baseline scale ------------------------------------------
    std::cout << "-- Category B (baseline scale) --\n";
    for (auto& spec : std::vector<DatasetSpec>{
        {"small",  "B", genUniformRandom("small",   100, 4, 16, SEED, BUDGET).identifiers,  50,  30},
        {"medium", "B", genUniformRandom("medium", 2000, 4, 20, SEED, BUDGET).identifiers, 200, 100},
        {"large",  "B", genUniformRandom("large", 20000, 4, 20, SEED, BUDGET).identifiers, 500, 200},
    }) runDataset(timer, spec, out);

    // ---- Category B: redundancy-rich -----------------------------------------
    std::cout << "\n-- Category B (redundancy-rich) --\n";
    { DatasetSpec ds;
      ds.name="high-prefix-similarity"; ds.category="B";
      ds.identifiers = genHighPrefixSimilarity(2000, SEED, BUDGET).identifiers;
      ds.lookupSampleSize=200; ds.scopeExitN=100;
      runDataset(timer, ds, out); }
    { DatasetSpec ds;
      ds.name="hot-cold-access"; ds.category="B";
      ds.identifiers = genHotColdAccess(2000, SEED, BUDGET).identifiers;
      ds.lookupSampleSize=200; ds.scopeExitN=100;
      runDataset(timer, ds, out); }
    { DatasetSpec ds;
      ds.name="nested-scopes"; ds.category="B";
      ds.identifiers = genNestedScopes(40, 50, SEED, BUDGET).identifiers;
      ds.lookupSampleSize=200; ds.scopeExitN=100;
      runDataset(timer, ds, out); }

    // ---- Category C: adversarial ---------------------------------------------
    std::cout << "\n-- Category C (adversarial) --\n";
    { DatasetSpec ds;
      ds.name="random-long"; ds.category="C";
      ds.identifiers = genUniformRandom("random-long", 2000, 20, 40, SEED, BUDGET).identifiers;
      ds.lookupSampleSize=200; ds.scopeExitN=100;
      runDataset(timer, ds, out); }
    { DatasetSpec ds;
      ds.name="high-churn"; ds.category="C";
      std::mt19937 rng(SEED + 1);
      for (int i = 0; i < 2000; i++) ds.identifiers.push_back(randomIdentifier(rng, 3, 8));
      ds.lookupSampleSize=200; ds.scopeExitN=50;
      runDataset(timer, ds, out); }
    { DatasetSpec ds;
      ds.name="memory-stress"; ds.category="C";
      ds.identifiers = genMemoryStress(2000, SEED, BUDGET).identifiers;
      ds.lookupSampleSize=200; ds.scopeExitN=100;
      runDataset(timer, ds, out); }

    // ---- Category A: real-world corpora --------------------------------------
    std::cout << "\n-- Category A (real-world corpora) --\n";
    struct CA { std::string path, name; };
    std::vector<CA> corpusArgs;
    for (int i = 1; i + 1 < argc; i += 2) corpusArgs.push_back({argv[i], argv[i+1]});
    if (corpusArgs.empty()) {
        for (auto& p : std::vector<CA>{
            {"results/corpus_ids_freertos.txt",    "FreeRTOS"},
            {"results/corpus_ids_arduino-core.txt","Arduino"},
            {"results/corpus_ids_zephyr.txt",      "Zephyr"}}) {
            if (std::ifstream(p.path)) corpusArgs.push_back(p);
        }
    }
    bool anyCorpus = false;
    for (auto& ca : corpusArgs) {
        std::cout << "  Loading " << ca.name << " ... " << std::flush;
        auto ids = loadCorpusIds(ca.path, 50000);
        if (ids.empty()) { std::cout << "empty/missing, skipping\n"; continue; }
        std::cout << ids.size() << " ids\n" << std::flush;
        DatasetSpec ds;
        ds.name=ca.name; ds.category="A"; ds.identifiers=ids;
        ds.lookupSampleSize=500; ds.scopeExitN=100;
        runDataset(timer, ds, out);
        anyCorpus = true;
    }
    if (!anyCorpus)
        std::cout << "  (no corpus files found -- skipping Category A)\n";

    out.flush();
    std::cout << "\nWrote results/v2_latency_memory.csv\n";
    return 0;
}
