// SYSTEMATIC PARAMETER SENSITIVITY & INTERACTION STUDY
// (CLAUDE_RESEARCH.md Section 13)
//
// Evaluates parameter sensitivity across development/training workloads and held-out evaluation corpora:
//   1. Block Size x Anchor Interval Sweep (reconstruction depth & reclaim granularity)
//   2. Representation Threshold Sweep (inlineMaxLen x compressMinLen)
//   3. Tiering Policy Sweep (hotAccessThreshold x coldIdleEpochs)
//
// Output: results/parameter_sweep.csv

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

struct ParameterEvalRow {
    std::string workload;
    std::string workloadType; // "training" vs "held_out"
    std::string sweepCategory;
    std::string impl;
    size_t configId = 0;

    size_t inlineMaxLen = 12;
    size_t compressMinLen = 14;
    size_t blockSize = 32;
    size_t anchorInterval = 8;
    size_t hotAccessThreshold = 3;
    size_t coldIdleEpochs = 0;

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
};

// Timed helper operations
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
static ParameterEvalRow evalBaseline(HiResTimer& timer, const std::string& workload,
                                     const std::string& wlType, const std::string& implName,
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

    ParameterEvalRow r;
    r.workload = workload; r.workloadType = wlType; r.sweepCategory = "baseline"; r.impl = implName;
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

// Evaluate SymTabV2 Config
static ParameterEvalRow evalV2Config(HiResTimer& timer, const std::string& workload,
                                     const std::string& wlType, const std::string& category,
                                     size_t cfgId, const PolicyConfigV2& cfg,
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
            if (cfg.coldIdleEpochs > 0 && uses % 500 == 0) {
                t.runMaintenance(); // Trigger demotion sweep if demotion is enabled
            }
        }
    }

    ParameterEvalRow r;
    r.workload = workload; r.workloadType = wlType; r.sweepCategory = category; r.impl = "SymTabV2";
    r.configId = cfgId;
    r.inlineMaxLen = cfg.inlineMaxLen;
    r.compressMinLen = cfg.compressMinLen;
    r.blockSize = cfg.blockSize;
    r.anchorInterval = cfg.anchorInterval;
    r.hotAccessThreshold = cfg.hotAccessThreshold;
    r.coldIdleEpochs = cfg.coldIdleEpochs;

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
    out << "workload,workload_type,sweep_category,implementation,config_id,"
           "inline_max_len,compress_min_len,block_size,anchor_interval,hot_access_threshold,cold_idle_epochs,"
           "declarations,uses,unique_names,modeled_peak_bytes,modeled_final_bytes,"
           "measured_peak_heap_bytes,measured_final_heap_bytes,"
           "cold_lookup_p50_us,cold_lookup_p95_us,cold_lookup_p99_us,cold_lookup_mean_us,"
           "hot_lookup_p50_us,hot_lookup_p95_us,hot_lookup_p99_us,hot_lookup_mean_us,"
           "insert_p50_us,insert_mean_us,scope_exit_us,"
           "promotions,demotions,reconstruction_count,reconstruction_steps_total,mean_reconstruction_depth,"
           "count_inline,count_interned,count_compressed\n";
}

static void writeRow(std::ofstream& out, const ParameterEvalRow& r) {
    out << r.workload << "," << r.workloadType << "," << r.sweepCategory << "," << r.impl << "," << r.configId << ","
        << r.inlineMaxLen << "," << r.compressMinLen << "," << r.blockSize << ","
        << r.anchorInterval << "," << r.hotAccessThreshold << "," << r.coldIdleEpochs << ","
        << r.declarations << "," << r.uses << "," << r.uniqueNames << ","
        << r.modeledPeakBytes << "," << r.modeledFinalBytes << ","
        << r.measuredPeakHeapBytes << "," << r.measuredFinalHeapBytes << ","
        << r.coldLookupP50Us << "," << r.coldLookupP95Us << "," << r.coldLookupP99Us << "," << r.coldLookupMeanUs << ","
        << r.hotLookupP50Us << "," << r.hotLookupP95Us << "," << r.hotLookupP99Us << "," << r.hotLookupMeanUs << ","
        << r.insertP50Us << "," << r.insertMeanUs << "," << r.scopeExitUs << ","
        << r.promotions << "," << r.demotions << ","
        << r.reconCount << "," << r.reconSteps << "," << r.reconDepth << ","
        << r.countInline << "," << r.countInterned << "," << r.countCompressed << "\n";
}

struct WorkloadSpec {
    std::string name;
    std::string workloadType; // "training" vs "held_out"
    std::vector<Event> events;
};

int main() {
    HiResTimer timer;
    const size_t BUDGET = 4 * 1024 * 1024;
    const unsigned SEED = 42;

    std::ofstream out("results/parameter_sweep.csv");
    if (!out) {
        std::cerr << "ERROR: cannot open results/parameter_sweep.csv for writing\n";
        return 1;
    }
    writeHeader(out);

    std::cout << "=== Systematic Parameter Sensitivity & Interaction Study ===\n\n";

    std::vector<WorkloadSpec> workloads;
    // Training workloads
    workloads.push_back({"medium", "training", buildSyntheticEvents("medium", genUniformRandom("medium", 2000, 4, 20, SEED, BUDGET).identifiers, 50)});
    workloads.push_back({"high-prefix-similarity", "training", buildSyntheticEvents("high-prefix-similarity", genHighPrefixSimilarity(2000, SEED, BUDGET).identifiers, 50)});
    workloads.push_back({"hot-cold-access", "training", buildSyntheticEvents("hot-cold-access", genHotColdAccess(2000, SEED, BUDGET).identifiers, 50)});
    workloads.push_back({"nested-scopes", "training", buildSyntheticEvents("nested-scopes", genNestedScopes(40, 50, SEED, BUDGET).identifiers, 50)});
    workloads.push_back({"random-long", "training", buildSyntheticEvents("random-long", genUniformRandom("random-long", 2000, 20, 40, SEED, BUDGET).identifiers, 50)});

    // Held-out evaluation workloads
    for (auto& p : std::vector<std::pair<std::string, std::string>>{
        {"results/corpus_events_FreeRTOS.txt", "FreeRTOS"},
        {"results/corpus_events_Arduino.txt", "Arduino"},
        {"results/corpus_events_Zephyr.txt", "Zephyr"}}) {
        if (std::ifstream(p.first)) {
            auto evs = loadCorpusEvents(p.first, 25000);
            if (!evs.empty()) workloads.push_back({p.second, "held_out", evs});
        }
    }

    size_t globalCfgId = 0;

    for (auto& wl : workloads) {
        std::cout << "Evaluating Workload [" << wl.workloadType << "] " << wl.name
                  << " (" << wl.events.size() << " events) ...\n" << std::flush;

        // Baselines
        auto conv = evalBaseline<ConventionalSymbolTable>(timer, wl.name, wl.workloadType, "Conventional", wl.events);
        writeRow(out, conv);

        auto interned = evalBaseline<InternedSymbolTable>(timer, wl.name, wl.workloadType, "Interned", wl.events);
        writeRow(out, interned);

        // 1. Block Size x Anchor Interval Sweep (Reconstruction & Reclaim Granularity)
        std::cout << "  - Sweep 1: Block Size x Anchor Interval ... " << std::flush;
        std::vector<size_t> blockSizes = {4, 8, 16, 32, 64, 128};
        std::vector<size_t> anchorIntervals = {2, 4, 8, 16, 32};
        for (size_t bs : blockSizes) {
            for (size_t ai : anchorIntervals) {
                if (ai > bs) continue;
                PolicyConfigV2 cfg;
                cfg.inlineMaxLen = 12;
                cfg.compressMinLen = 14;
                cfg.blockSize = bs;
                cfg.anchorInterval = ai;
                cfg.hotAccessThreshold = 3;
                cfg.coldIdleEpochs = 0;
                globalCfgId++;
                auto r = evalV2Config(timer, wl.name, wl.workloadType, "block_x_anchor", globalCfgId, cfg, wl.events);
                writeRow(out, r);
            }
        }
        std::cout << "done\n" << std::flush;

        // 2. Representation Thresholds Sweep (inlineMaxLen x compressMinLen)
        std::cout << "  - Sweep 2: Representation Thresholds ... " << std::flush;
        std::vector<size_t> inlineLens = {4, 8, 12, 16, 20};
        std::vector<size_t> compressLens = {8, 12, 16, 24, 32, 999};
        for (size_t iml : inlineLens) {
            for (size_t cml : compressLens) {
                PolicyConfigV2 cfg;
                cfg.inlineMaxLen = iml;
                cfg.compressMinLen = cml;
                cfg.blockSize = 32;
                cfg.anchorInterval = 8;
                cfg.hotAccessThreshold = 3;
                cfg.coldIdleEpochs = 0;
                globalCfgId++;
                auto r = evalV2Config(timer, wl.name, wl.workloadType, "representation_thresholds", globalCfgId, cfg, wl.events);
                writeRow(out, r);
            }
        }
        std::cout << "done\n" << std::flush;

        // 3. Tiering Policy Sweep (hotAccessThreshold x coldIdleEpochs)
        std::cout << "  - Sweep 3: Hot/Cold Tiering Policy ... " << std::flush;
        std::vector<size_t> hotThresholds = {1, 2, 3, 5, 10, 999};
        std::vector<size_t> coldIdleEpochsList = {0, 20, 100};
        for (size_t ht : hotThresholds) {
            for (size_t cie : coldIdleEpochsList) {
                PolicyConfigV2 cfg;
                cfg.inlineMaxLen = 12;
                cfg.compressMinLen = 14;
                cfg.blockSize = 32;
                cfg.anchorInterval = 8;
                cfg.hotAccessThreshold = ht;
                cfg.coldIdleEpochs = cie;
                globalCfgId++;
                auto r = evalV2Config(timer, wl.name, wl.workloadType, "tiering_policy", globalCfgId, cfg, wl.events);
                writeRow(out, r);
            }
        }
        std::cout << "done\n" << std::flush;
    }

    out.flush();
    std::cout << "\nWrote results/parameter_sweep.csv (" << globalCfgId << " configurations evaluated)\n";
    return 0;
}
