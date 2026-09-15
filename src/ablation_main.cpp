// COMPREHENSIVE V2 ABLATION STUDY
// (CLAUDE_RESEARCH.md Section 14)
//
// Evaluates the quantitative contribution of each core V2 mechanism by systematically
// disabling one component at a time against identical semantic event streams.
//
// Evaluated Variants:
//   1. Conventional                         : Baseline unordered_map per scope
//   2. Interned                             : Global refcounted interning pool
//   3. BudgetSymV1                          : Append-only vector of Entry objects
//   4. FullV2                               : Default V2 policy & all active mechanisms
//   5. V2-NoBlockCompression                : compressMinLen=999 (compression off)
//   6. V2-NoFingerprints                    : disableFingerprints=true (fingerprint rejection off)
//   7. V2-NoHotColdPromotion                : hotAccessThreshold=999 (promotion off)
//   8. V2-NoAdaptiveRepresentation          : inlineMaxLen=0 (INLINE short-string storage off)
//   9. V2-NoScopeReclamation                : disableScopeReclamation=true (scope slot reuse off)
//
// Outputs: results/ablation.csv

#define BUDGETSYM_HEAP_COUNTER_IMPL
#include "../include/heap_counter.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
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

// Statistics helpers
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

struct Event {
    enum Kind { ENTER_SCOPE, EXIT_SCOPE, DECLARE, USE } kind;
    std::string symbol;
    int depth = 0;
};

// Build synthetic events
static std::vector<Event> buildSyntheticEvents(const std::string& name,
                                                const std::vector<std::string>& ids,
                                                size_t scopeChunk) {
    std::vector<Event> events;
    size_t n = ids.size(), i = 0;
    while (i < n) {
        events.push_back({Event::ENTER_SCOPE, "", 0});
        size_t end = std::min(i + scopeChunk, n);
        for (; i < end; i++) {
            events.push_back({Event::DECLARE, ids[i], 0});
            if (name == "hot-cold-access" && i < n / 10) {
                for (int r = 0; r < 4; r++) events.push_back({Event::USE, ids[i], 0});
            } else {
                events.push_back({Event::USE, ids[i], 0});
            }
        }
        events.push_back({Event::EXIT_SCOPE, "", 0});
    }
    return events;
}

// Load real corpus event stream
static std::vector<Event> loadCorpusEvents(const std::string& filepath, size_t maxEvents = 25000) {
    std::vector<Event> events;
    std::ifstream in(filepath);
    if (!in) return events;

    std::string line;
    size_t count = 0;
    while (std::getline(in, line) && count < maxEvents) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string tag;
        iss >> tag;
        Event ev;
        if (tag == "ENTER_SCOPE") {
            ev.kind = Event::ENTER_SCOPE;
            iss >> ev.depth;
        } else if (tag == "EXIT_SCOPE") {
            ev.kind = Event::EXIT_SCOPE;
            iss >> ev.depth;
        } else if (tag == "DECLARE") {
            ev.kind = Event::DECLARE;
            iss >> ev.symbol >> ev.depth;
        } else if (tag == "USE") {
            ev.kind = Event::USE;
            iss >> ev.symbol >> ev.depth;
        }
        events.push_back(ev);
        count++;
    }
    return events;
}

struct AblationRow {
    std::string workload;
    std::string category;
    std::string variant;

    size_t declarations = 0;
    size_t uses = 0;
    size_t uniqueNames = 0;

    long long modeledPeakBytes = 0;
    long long modeledFinalBytes = 0;
    long long measuredPeakHeapBytes = 0;
    long long measuredFinalHeapBytes = 0;

    double coldLookupP50Us = 0.0;
    double coldLookupP95Us = 0.0;
    double coldLookupP99Us = 0.0;
    double coldLookupMeanUs = 0.0;

    double hotLookupP50Us = 0.0;
    double hotLookupP95Us = 0.0;
    double hotLookupP99Us = 0.0;
    double hotLookupMeanUs = 0.0;

    double insertP50Us = 0.0;
    double insertMeanUs = 0.0;
    double scopeExitUs = 0.0;

    size_t promotions = 0;
    size_t demotions = 0;
    size_t reconCount = 0;
    size_t reconSteps = 0;
    double reconDepth = 0.0;

    size_t countInline = 0;
    size_t countInterned = 0;
    size_t countCompressed = 0;

    double memoryDeltaVsFullV2Pct = 0.0;
    double coldLatencyDeltaVsFullV2Pct = 0.0;
};

template<typename Table>
static double timeScopeExit(HiResTimer& timer, Table& t, size_t seN) {
    t.enterScope();
    std::mt19937 rng(99999);
    for (size_t i = 0; i < seN; i++) t.insert(randomIdentifier(rng, 6, 20));
    auto a = timer.now(); t.exitScope(); auto b = timer.now();
    return timer.microsecondsBetween(a, b);
}

// Evaluate Baseline
template<typename Table>
static AblationRow evalBaseline(HiResTimer& timer, const std::string& workload,
                                const std::string& cat, const std::string& variantName,
                                const std::vector<Event>& events) {
    heap::Scope hs;
    Table t(0);

    std::vector<double> insSamples;
    std::vector<double> lookupSamples;
    std::unordered_set<std::string> uniqueSymbols;
    size_t decls = 0, uses = 0;

    for (auto& ev : events) {
        if (ev.kind == Event::ENTER_SCOPE) {
            t.enterScope();
        } else if (ev.kind == Event::EXIT_SCOPE) {
            t.exitScope();
        } else if (ev.kind == Event::DECLARE) {
            decls++;
            uniqueSymbols.insert(ev.symbol);
            auto a = timer.now(); volatile int id = t.insert(ev.symbol); auto b = timer.now();
            (void)id;
            insSamples.push_back(timer.microsecondsBetween(a, b));
        } else if (ev.kind == Event::USE) {
            uses++;
            uniqueSymbols.insert(ev.symbol);
            auto a = timer.now(); volatile int id = t.resolve(ev.symbol); auto b = timer.now();
            (void)id;
            lookupSamples.push_back(timer.microsecondsBetween(a, b));
            t.recordAccess(ev.symbol);
        }
    }

    AblationRow r;
    r.workload = workload; r.category = cat; r.variant = variantName;
    r.declarations = decls; r.uses = uses; r.uniqueNames = uniqueSymbols.size();

    r.measuredFinalHeapBytes = hs.bytes();
    r.measuredPeakHeapBytes  = hs.peakBytes();
    r.modeledFinalBytes = t.tracker().current();
    r.modeledPeakBytes  = t.tracker().peak();

    r.insertP50Us  = pctile(insSamples, 0.50); r.insertMeanUs = vmean(insSamples);
    r.coldLookupP50Us = pctile(lookupSamples, 0.50); r.coldLookupP95Us = pctile(lookupSamples, 0.95);
    r.coldLookupP99Us = pctile(lookupSamples, 0.99); r.coldLookupMeanUs = vmean(lookupSamples);
    r.hotLookupP50Us  = r.coldLookupP50Us; r.hotLookupP95Us = r.coldLookupP95Us;
    r.scopeExitUs = timeScopeExit(timer, t, 100);

    return r;
}

// Evaluate V1
static AblationRow evalV1(HiResTimer& timer, const std::string& workload,
                          const std::string& cat,
                          const std::vector<Event>& events) {
    heap::Scope hs;
    BudgetSym t(0);

    std::vector<double> insSamples;
    std::vector<double> lookupSamples;
    std::unordered_set<std::string> uniqueSymbols;
    size_t decls = 0, uses = 0;

    for (auto& ev : events) {
        if (ev.kind == Event::ENTER_SCOPE) {
            t.enterScope();
        } else if (ev.kind == Event::EXIT_SCOPE) {
            t.exitScope();
        } else if (ev.kind == Event::DECLARE) {
            decls++;
            uniqueSymbols.insert(ev.symbol);
            auto a = timer.now(); volatile int id = t.insert(ev.symbol); auto b = timer.now();
            (void)id;
            insSamples.push_back(timer.microsecondsBetween(a, b));
        } else if (ev.kind == Event::USE) {
            uses++;
            uniqueSymbols.insert(ev.symbol);
            auto a = timer.now(); volatile int id = t.resolve(ev.symbol); auto b = timer.now();
            (void)id;
            lookupSamples.push_back(timer.microsecondsBetween(a, b));
            t.recordAccess(ev.symbol);
        }
    }

    AblationRow r;
    r.workload = workload; r.category = cat; r.variant = "BudgetSymV1";
    r.declarations = decls; r.uses = uses; r.uniqueNames = uniqueSymbols.size();

    r.measuredFinalHeapBytes = hs.bytes();
    r.measuredPeakHeapBytes  = hs.peakBytes();
    r.modeledFinalBytes = t.tracker().current();
    r.modeledPeakBytes  = t.tracker().peak();

    r.insertP50Us  = pctile(insSamples, 0.50); r.insertMeanUs = vmean(insSamples);
    r.coldLookupP50Us = pctile(lookupSamples, 0.50); r.coldLookupP95Us = pctile(lookupSamples, 0.95);
    r.coldLookupP99Us = pctile(lookupSamples, 0.99); r.coldLookupMeanUs = vmean(lookupSamples);
    r.hotLookupP50Us  = r.coldLookupP50Us; r.hotLookupP95Us = r.coldLookupP95Us;
    r.promotions = t.promotions();
    r.scopeExitUs = timeScopeExit(timer, t, 100);

    return r;
}

// Evaluate V2 Variant
static AblationRow evalV2Variant(HiResTimer& timer, const std::string& workload,
                                 const std::string& cat, const std::string& variantName,
                                 const PolicyConfigV2& cfg,
                                 const std::vector<Event>& events) {
    heap::Scope hs;
    SymTabV2<> t(0, cfg);

    std::vector<double> insSamples;
    std::vector<double> lookupSamples;
    std::unordered_set<std::string> uniqueSymbols;
    size_t decls = 0, uses = 0;

    for (auto& ev : events) {
        if (ev.kind == Event::ENTER_SCOPE) {
            t.enterScope();
        } else if (ev.kind == Event::EXIT_SCOPE) {
            t.exitScope();
        } else if (ev.kind == Event::DECLARE) {
            decls++;
            uniqueSymbols.insert(ev.symbol);
            auto a = timer.now(); volatile int id = t.insert(ev.symbol); auto b = timer.now();
            (void)id;
            insSamples.push_back(timer.microsecondsBetween(a, b));
        } else if (ev.kind == Event::USE) {
            uses++;
            uniqueSymbols.insert(ev.symbol);
            auto a = timer.now(); volatile int id = t.resolve(ev.symbol); auto b = timer.now();
            (void)id;
            lookupSamples.push_back(timer.microsecondsBetween(a, b));
            t.recordAccess(ev.symbol);
        }
    }

    AblationRow r;
    r.workload = workload; r.category = cat; r.variant = variantName;
    r.declarations = decls; r.uses = uses; r.uniqueNames = uniqueSymbols.size();
    r.measuredFinalHeapBytes = hs.bytes();
    r.measuredPeakHeapBytes  = hs.peakBytes();
    r.modeledFinalBytes = t.tracker().current();
    r.modeledPeakBytes  = t.tracker().peak();

    r.insertP50Us  = pctile(insSamples, 0.50); r.insertMeanUs = vmean(insSamples);
    r.coldLookupP50Us = pctile(lookupSamples, 0.50); r.coldLookupP95Us = pctile(lookupSamples, 0.95);
    r.coldLookupP99Us = pctile(lookupSamples, 0.99); r.coldLookupMeanUs = vmean(lookupSamples);
    r.hotLookupP50Us  = r.coldLookupP50Us; r.hotLookupP95Us = r.coldLookupP95Us;

    r.reconCount = t.reconstructionCount();
    r.reconSteps = t.reconstructionStepsTotal();
    r.reconDepth = r.reconCount > 0
        ? static_cast<double>(r.reconSteps) / static_cast<double>(r.reconCount) : 0.0;

    r.promotions = t.promotions();
    r.demotions  = t.demotions();
    r.scopeExitUs = timeScopeExit(timer, t, 100);

    for (auto& sym : uniqueSymbols) {
        switch (t.representationOf(sym)) {
            case Rep::INLINE_REP:     r.countInline++;    break;
            case Rep::INTERNED_REP:   r.countInterned++;  break;
            case Rep::COMPRESSED_REP: r.countCompressed++; break;
        }
    }

    return r;
}

static void writeHeader(std::ofstream& out) {
    out << "workload,category,variant,"
           "declarations,uses,unique_names,modeled_peak_bytes,modeled_final_bytes,"
           "measured_peak_heap_bytes,measured_final_heap_bytes,"
           "cold_lookup_p50_us,cold_lookup_p95_us,cold_lookup_p99_us,cold_lookup_mean_us,"
           "hot_lookup_p50_us,hot_lookup_p95_us,hot_lookup_p99_us,hot_lookup_mean_us,"
           "insert_p50_us,insert_mean_us,scope_exit_us,"
           "promotions,demotions,reconstruction_count,reconstruction_steps_total,mean_reconstruction_depth,"
           "count_inline,count_interned,count_compressed,"
           "memory_delta_vs_full_v2_pct,cold_latency_delta_vs_full_v2_pct\n";
}

static void writeRow(std::ofstream& out, const AblationRow& r) {
    out << r.workload << "," << r.category << "," << r.variant << ","
        << r.declarations << "," << r.uses << "," << r.uniqueNames << ","
        << r.modeledPeakBytes << "," << r.modeledFinalBytes << ","
        << r.measuredPeakHeapBytes << "," << r.measuredFinalHeapBytes << ","
        << r.coldLookupP50Us << "," << r.coldLookupP95Us << "," << r.coldLookupP99Us << "," << r.coldLookupMeanUs << ","
        << r.hotLookupP50Us << "," << r.hotLookupP95Us << "," << r.hotLookupP99Us << "," << r.hotLookupMeanUs << ","
        << r.insertP50Us << "," << r.insertMeanUs << "," << r.scopeExitUs << ","
        << r.promotions << "," << r.demotions << ","
        << r.reconCount << "," << r.reconSteps << "," << r.reconDepth << ","
        << r.countInline << "," << r.countInterned << "," << r.countCompressed << ","
        << r.memoryDeltaVsFullV2Pct << "," << r.coldLatencyDeltaVsFullV2Pct << "\n";
}

struct WorkloadSpec {
    std::string name, category;
    std::vector<Event> events;
};

int main() {
    HiResTimer timer;
    const size_t BUDGET = 4 * 1024 * 1024;
    const unsigned SEED = 42;

    std::ofstream out("results/ablation.csv");
    if (!out) {
        std::cerr << "ERROR: cannot open results/ablation.csv for writing\n";
        return 1;
    }
    writeHeader(out);

    std::cout << "=== Comprehensive V2 Ablation Study ===\n\n";

    std::vector<WorkloadSpec> workloads;
    // Category B
    workloads.push_back({"medium", "B", buildSyntheticEvents("medium", genUniformRandom("medium", 2000, 4, 20, SEED, BUDGET).identifiers, 50)});
    workloads.push_back({"high-prefix-similarity", "B", buildSyntheticEvents("high-prefix-similarity", genHighPrefixSimilarity(2000, SEED, BUDGET).identifiers, 50)});
    workloads.push_back({"hot-cold-access", "B", buildSyntheticEvents("hot-cold-access", genHotColdAccess(2000, SEED, BUDGET).identifiers, 50)});
    workloads.push_back({"nested-scopes", "B", buildSyntheticEvents("nested-scopes", genNestedScopes(40, 50, SEED, BUDGET).identifiers, 50)});

    // Category C
    workloads.push_back({"random-long", "C", buildSyntheticEvents("random-long", genUniformRandom("random-long", 2000, 20, 40, SEED, BUDGET).identifiers, 50)});
    {
        std::mt19937 rng(SEED + 1);
        std::vector<std::string> churnIds;
        for (int i = 0; i < 2000; i++) churnIds.push_back(randomIdentifier(rng, 3, 8));
        workloads.push_back({"high-churn", "C", buildSyntheticEvents("high-churn", churnIds, 5)});
    }
    workloads.push_back({"memory-stress", "C", buildSyntheticEvents("memory-stress", genMemoryStress(2000, SEED, BUDGET).identifiers, 50)});

    // Category A (Held-out Corpora)
    for (auto& p : std::vector<std::pair<std::string, std::string>>{
        {"results/corpus_events_FreeRTOS.txt", "FreeRTOS"},
        {"results/corpus_events_Arduino.txt", "Arduino"},
        {"results/corpus_events_Zephyr.txt", "Zephyr"}}) {
        if (std::ifstream(p.first)) {
            auto evs = loadCorpusEvents(p.first, 25000);
            if (!evs.empty()) workloads.push_back({p.second, "A", evs});
        }
    }

    for (auto& wl : workloads) {
        std::cout << "Evaluating Workload [" << wl.category << "] " << wl.name
                  << " (" << wl.events.size() << " events) ...\n" << std::flush;

        std::vector<AblationRow> wlResults;

        // 1. Conventional
        wlResults.push_back(evalBaseline<ConventionalSymbolTable>(timer, wl.name, wl.category, "Conventional", wl.events));

        // 2. Interned
        wlResults.push_back(evalBaseline<InternedSymbolTable>(timer, wl.name, wl.category, "Interned", wl.events));

        // 3. BudgetSym V1
        wlResults.push_back(evalV1(timer, wl.name, wl.category, wl.events));

        // 4. Full V2
        PolicyConfigV2 defaultV2;
        auto fullV2 = evalV2Variant(timer, wl.name, wl.category, "FullV2", defaultV2, wl.events);
        wlResults.push_back(fullV2);

        // 5. V2 without block compression
        PolicyConfigV2 noComp = defaultV2;
        noComp.compressMinLen = 999;
        wlResults.push_back(evalV2Variant(timer, wl.name, wl.category, "V2-NoBlockCompression", noComp, wl.events));

        // 6. V2 without fingerprints
        PolicyConfigV2 noFp = defaultV2;
        noFp.disableFingerprints = true;
        wlResults.push_back(evalV2Variant(timer, wl.name, wl.category, "V2-NoFingerprints", noFp, wl.events));

        // 7. V2 without hot/cold tiering promotion
        PolicyConfigV2 noProm = defaultV2;
        noProm.hotAccessThreshold = 999;
        wlResults.push_back(evalV2Variant(timer, wl.name, wl.category, "V2-NoHotColdPromotion", noProm, wl.events));

        // 8. V2 without adaptive representation (INLINE short-string storage disabled)
        PolicyConfigV2 noInline = defaultV2;
        noInline.inlineMaxLen = 0;
        wlResults.push_back(evalV2Variant(timer, wl.name, wl.category, "V2-NoAdaptiveRepresentation", noInline, wl.events));

        // 9. V2 without scope-aware reclamation (scope slot reuse disabled)
        PolicyConfigV2 noReclaim = defaultV2;
        noReclaim.disableScopeReclamation = true;
        wlResults.push_back(evalV2Variant(timer, wl.name, wl.category, "V2-NoScopeReclamation", noReclaim, wl.events));

        // Compute deltas relative to FullV2
        double baseHeap = fullV2.measuredFinalHeapBytes > 0 ? static_cast<double>(fullV2.measuredFinalHeapBytes) : 1.0;
        double baseCold = fullV2.coldLookupP50Us > 0 ? fullV2.coldLookupP50Us : 1.0;

        for (auto& r : wlResults) {
            r.memoryDeltaVsFullV2Pct = 100.0 * (static_cast<double>(r.measuredFinalHeapBytes) - baseHeap) / baseHeap;
            r.coldLatencyDeltaVsFullV2Pct = 100.0 * (r.coldLookupP50Us - baseCold) / baseCold;
            writeRow(out, r);
        }

        std::cout << "  - FullV2 Heap=" << fullV2.measuredFinalHeapBytes << "B, Cold P50=" << fullV2.coldLookupP50Us << "us\n";
    }

    out.flush();
    std::cout << "\nWrote results/ablation.csv\n";
    return 0;
}
