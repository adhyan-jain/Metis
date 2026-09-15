// LATENCY-CONSTRAINED PARETO OPTIMIZATION BENCHMARK
// (CLAUDE_RESEARCH.md Sections 11 & 12)
//
// Evaluates the full memory-vs-latency Pareto frontier across:
//   - Category A (Representative): FreeRTOS, Arduino, Zephyr (real semantic event traces)
//   - Category B (Redundancy-rich): small, medium, large, high-prefix-similarity, hot-cold-access, nested-scopes
//   - Category C (Adversarial): random-long, high-churn, memory-stress
//
// Latency Constraints Evaluated:
//   L = 1.10 (10% overhead constraint)
//   L = 1.25 (25% overhead constraint)
//   L = 1.50 (50% overhead constraint)
//   L = 2.00 (100% overhead constraint)
//
// Outputs:
//   results/pareto_results.csv  (full grid of all evaluated configurations)
//   results/pareto_frontier.csv (non-dominated Pareto frontier & latency-constrained optimal configs)

#define BUDGETSYM_HEAP_COUNTER_IMPL
#include "../include/heap_counter.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
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

// Build event stream from synthetic identifier list
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
            // For hot-cold-access, add repeated USE events for hot symbols
            if (name == "hot-cold-access" && i < n / 10) {
                for (int r = 0; r < 4; r++) {
                    events.push_back({Event::USE, ids[i], 0});
                }
            } else {
                events.push_back({Event::USE, ids[i], 0});
            }
        }
        events.push_back({Event::EXIT_SCOPE, "", 0});
    }
    return events;
}

// Load real corpus event stream
static std::vector<Event> loadCorpusEvents(const std::string& filepath, size_t maxEvents = 50000) {
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

struct ConfigEvalRow {
    std::string workload;
    std::string category;
    std::string impl;
    size_t configId = 0;

    // Policy parameters (for SymTabV2)
    size_t inlineMaxLen = 0;
    size_t compressMinLen = 0;
    size_t blockSize = 0;
    size_t anchorInterval = 0;
    size_t hotAccessThreshold = 0;

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
    double insertP95Us = 0.0;
    double insertP99Us = 0.0;
    double insertMeanUs = 0.0;

    size_t promotions = 0;
    size_t demotions = 0;
    size_t reconCount = 0;
    size_t reconSteps = 0;
    double reconDepth = 0.0;

    size_t countInline = 0;
    size_t countInterned = 0;
    size_t countCompressed = 0;

    double coldLatencyRatioVsConv = 1.0;
    double hotLatencyRatioVsConv = 1.0;
    double memoryRatioVsConv = 1.0; // measured_final_heap / conv_measured_final_heap
    double memorySavingsPctVsConv = 0.0; // 100 * (1 - memoryRatioVsConv)
};

// Timed helper operations
template<typename Table>
static ConfigEvalRow evalBaselineEventStream(HiResTimer& timer, const std::string& workload,
                                             const std::string& cat, const std::string& implName,
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

    ConfigEvalRow r;
    r.workload = workload; r.category = cat; r.impl = implName;
    r.declarations = decls; r.uses = uses; r.uniqueNames = uniqueSymbols.size();

    r.measuredFinalHeapBytes = hs.bytes();
    r.measuredPeakHeapBytes  = hs.peakBytes();
    r.modeledFinalBytes = t.tracker().current();
    r.modeledPeakBytes  = t.tracker().peak();

    r.insertP50Us  = pctile(insSamples, 0.50); r.insertP95Us  = pctile(insSamples, 0.95);
    r.insertP99Us  = pctile(insSamples, 0.99); r.insertMeanUs = vmean(insSamples);

    r.coldLookupP50Us = pctile(lookupSamples, 0.50); r.coldLookupP95Us = pctile(lookupSamples, 0.95);
    r.coldLookupP99Us = pctile(lookupSamples, 0.99); r.coldLookupMeanUs = vmean(lookupSamples);
    r.hotLookupP50Us  = r.coldLookupP50Us; r.hotLookupP95Us = r.coldLookupP95Us;

    return r;
}

static ConfigEvalRow evalV1EventStream(HiResTimer& timer, const std::string& workload,
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

    ConfigEvalRow r;
    r.workload = workload; r.category = cat; r.impl = "BudgetSymV1";
    r.declarations = decls; r.uses = uses; r.uniqueNames = uniqueSymbols.size();

    r.measuredFinalHeapBytes = hs.bytes();
    r.measuredPeakHeapBytes  = hs.peakBytes();
    r.modeledFinalBytes = t.tracker().current();
    r.modeledPeakBytes  = t.tracker().peak();

    r.insertP50Us  = pctile(insSamples, 0.50); r.insertP95Us  = pctile(insSamples, 0.95);
    r.insertP99Us  = pctile(insSamples, 0.99); r.insertMeanUs = vmean(insSamples);

    r.coldLookupP50Us = pctile(lookupSamples, 0.50); r.coldLookupP95Us = pctile(lookupSamples, 0.95);
    r.coldLookupP99Us = pctile(lookupSamples, 0.99); r.coldLookupMeanUs = vmean(lookupSamples);
    r.hotLookupP50Us  = r.coldLookupP50Us; r.hotLookupP95Us = r.coldLookupP95Us;

    r.promotions = t.promotions();
    return r;
}

static ConfigEvalRow evalV2ConfigEventStream(HiResTimer& timer, const std::string& workload,
                                             const std::string& cat, size_t cfgId,
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

    ConfigEvalRow r;
    r.workload = workload; r.category = cat; r.impl = "SymTabV2";
    r.configId = cfgId;
    r.inlineMaxLen = cfg.inlineMaxLen;
    r.compressMinLen = cfg.compressMinLen;
    r.blockSize = cfg.blockSize;
    r.anchorInterval = cfg.anchorInterval;
    r.hotAccessThreshold = cfg.hotAccessThreshold;

    r.declarations = decls; r.uses = uses; r.uniqueNames = uniqueSymbols.size();
    r.measuredFinalHeapBytes = hs.bytes();
    r.measuredPeakHeapBytes  = hs.peakBytes();
    r.modeledFinalBytes = t.tracker().current();
    r.modeledPeakBytes  = t.tracker().peak();

    r.insertP50Us  = pctile(insSamples, 0.50); r.insertP95Us  = pctile(insSamples, 0.95);
    r.insertP99Us  = pctile(insSamples, 0.99); r.insertMeanUs = vmean(insSamples);

    r.coldLookupP50Us = pctile(lookupSamples, 0.50); r.coldLookupP95Us = pctile(lookupSamples, 0.95);
    r.coldLookupP99Us = pctile(lookupSamples, 0.99); r.coldLookupMeanUs = vmean(lookupSamples);
    r.hotLookupP50Us  = r.coldLookupP50Us; r.hotLookupP95Us = r.coldLookupP95Us;

    r.reconCount = t.reconstructionCount();
    r.reconSteps = t.reconstructionStepsTotal();
    r.reconDepth = r.reconCount > 0
        ? static_cast<double>(r.reconSteps) / static_cast<double>(r.reconCount) : 0.0;

    r.promotions = t.promotions();
    r.demotions  = t.demotions();

    for (auto& sym : uniqueSymbols) {
        switch (t.representationOf(sym)) {
            case Rep::INLINE_REP:     r.countInline++;    break;
            case Rep::INTERNED_REP:   r.countInterned++;  break;
            case Rep::COMPRESSED_REP: r.countCompressed++; break;
        }
    }

    return r;
}

// Generate representative grid of policy configurations
static std::vector<PolicyConfigV2> buildConfigGrid() {
    std::vector<PolicyConfigV2> configs;

    std::vector<size_t> inlineLens = {8, 12, 16, 20};
    std::vector<size_t> compressLens = {8, 12, 16, 24, 999}; // 999 = no compression
    std::vector<size_t> blockSizes = {4, 8, 16, 32, 64};
    std::vector<size_t> anchorIntervals = {2, 4, 8, 16};
    std::vector<size_t> hotThresholds = {1, 2, 3, 5, 999}; // 999 = no promotion

    for (size_t iml : inlineLens) {
        for (size_t cml : compressLens) {
            for (size_t bs : blockSizes) {
                for (size_t ai : anchorIntervals) {
                    if (ai > bs) continue;
                    for (size_t ht : hotThresholds) {
                        PolicyConfigV2 c;
                        c.inlineMaxLen = iml;
                        c.compressMinLen = cml;
                        c.blockSize = bs;
                        c.anchorInterval = ai;
                        c.hotAccessThreshold = ht;
                        c.coldIdleEpochs = 0;
                        configs.push_back(c);
                    }
                }
            }
        }
    }
    return configs;
}

static void writeResultHeader(std::ofstream& out) {
    out << "workload,category,implementation,config_id,"
           "inline_max_len,compress_min_len,block_size,anchor_interval,hot_access_threshold,"
           "declarations,uses,unique_names,modeled_peak_bytes,modeled_final_bytes,"
           "measured_peak_heap_bytes,measured_final_heap_bytes,"
           "cold_lookup_p50_us,cold_lookup_p95_us,cold_lookup_p99_us,cold_lookup_mean_us,"
           "hot_lookup_p50_us,hot_lookup_p95_us,hot_lookup_p99_us,hot_lookup_mean_us,"
           "insert_p50_us,insert_p95_us,insert_p99_us,insert_mean_us,"
           "promotions,demotions,reconstruction_count,reconstruction_steps_total,mean_reconstruction_depth,"
           "count_inline,count_interned,count_compressed,"
           "cold_latency_ratio_vs_conv,hot_latency_ratio_vs_conv,memory_ratio_vs_conv,memory_savings_pct_vs_conv\n";
}

static void writeResultRow(std::ofstream& out, const ConfigEvalRow& r) {
    out << r.workload << "," << r.category << "," << r.impl << "," << r.configId << ","
        << r.inlineMaxLen << "," << r.compressMinLen << "," << r.blockSize << ","
        << r.anchorInterval << "," << r.hotAccessThreshold << ","
        << r.declarations << "," << r.uses << "," << r.uniqueNames << ","
        << r.modeledPeakBytes << "," << r.modeledFinalBytes << ","
        << r.measuredPeakHeapBytes << "," << r.measuredFinalHeapBytes << ","
        << r.coldLookupP50Us << "," << r.coldLookupP95Us << "," << r.coldLookupP99Us << "," << r.coldLookupMeanUs << ","
        << r.hotLookupP50Us << "," << r.hotLookupP95Us << "," << r.hotLookupP99Us << "," << r.hotLookupMeanUs << ","
        << r.insertP50Us << "," << r.insertP95Us << "," << r.insertP99Us << "," << r.insertMeanUs << ","
        << r.promotions << "," << r.demotions << ","
        << r.reconCount << "," << r.reconSteps << "," << r.reconDepth << ","
        << r.countInline << "," << r.countInterned << "," << r.countCompressed << ","
        << r.coldLatencyRatioVsConv << "," << r.hotLatencyRatioVsConv << ","
        << r.memoryRatioVsConv << "," << r.memorySavingsPctVsConv << "\n";
}

static void writeFrontierHeader(std::ofstream& out) {
    out << "workload,category,latency_constraint,satisfied,"
           "optimal_impl,optimal_config_id,"
           "inline_max_len,compress_min_len,block_size,anchor_interval,hot_access_threshold,"
           "conv_cold_lookup_p50_us,conv_measured_final_heap_bytes,"
           "actual_cold_lookup_p50_us,actual_measured_final_heap_bytes,"
           "latency_ratio_vs_conv,memory_ratio_vs_conv,memory_savings_pct_vs_conv\n";
}

struct WorkloadSpec {
    std::string name, category;
    std::vector<Event> events;
};

int main() {
    HiResTimer timer;
    const size_t BUDGET = 4 * 1024 * 1024;
    const unsigned SEED = 42;

    std::ofstream outResults("results/pareto_results.csv");
    if (!outResults) {
        std::cerr << "ERROR: cannot write results/pareto_results.csv\n";
        return 1;
    }
    writeResultHeader(outResults);

    auto configGrid = buildConfigGrid();
    std::cout << "=== Semantic Latency-Constrained Pareto Optimization Benchmark ===\n";
    std::cout << "  Grid size: " << configGrid.size() << " SymTabV2 configurations per workload\n\n";

    // Workload Taxonomy Setup
    std::vector<WorkloadSpec> workloads;

    // Category B
    workloads.push_back({"small", "B", buildSyntheticEvents("small", genUniformRandom("small", 100, 4, 16, SEED, BUDGET).identifiers, 20)});
    workloads.push_back({"medium", "B", buildSyntheticEvents("medium", genUniformRandom("medium", 2000, 4, 20, SEED, BUDGET).identifiers, 50)});
    workloads.push_back({"large", "B", buildSyntheticEvents("large", genUniformRandom("large", 5000, 4, 20, SEED, BUDGET).identifiers, 100)});
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

    // Category A (Real-World Corpora)
    for (auto& p : std::vector<std::pair<std::string, std::string>>{
        {"results/corpus_events_FreeRTOS.txt", "FreeRTOS"},
        {"results/corpus_events_Arduino.txt", "Arduino"},
        {"results/corpus_events_Zephyr.txt", "Zephyr"}}) {
        if (std::ifstream(p.first)) {
            auto evs = loadCorpusEvents(p.first, 25000); // 25K sample for fast grid sweep
            if (!evs.empty()) workloads.push_back({p.second, "A", evs});
        }
    }

    std::vector<ConfigEvalRow> allResults;
    std::ofstream outFrontier("results/pareto_frontier.csv");
    if (!outFrontier) {
        std::cerr << "ERROR: cannot write results/pareto_frontier.csv\n";
        return 1;
    }
    writeFrontierHeader(outFrontier);

    std::vector<double> latencyConstraints = {1.10, 1.25, 1.50, 2.00};

    for (auto& wl : workloads) {
        std::cout << "Evaluating Workload [" << wl.category << "] " << wl.name
                  << " (" << wl.events.size() << " events) ...\n" << std::flush;

        // 1. Baselines
        auto conv = evalBaselineEventStream<ConventionalSymbolTable>(timer, wl.name, wl.category, "Conventional", wl.events);
        conv.coldLatencyRatioVsConv = 1.0;
        conv.hotLatencyRatioVsConv = 1.0;
        conv.memoryRatioVsConv = 1.0;
        conv.memorySavingsPctVsConv = 0.0;
        writeResultRow(outResults, conv);
        allResults.push_back(conv);

        auto interned = evalBaselineEventStream<InternedSymbolTable>(timer, wl.name, wl.category, "Interned", wl.events);
        interned.coldLatencyRatioVsConv = conv.coldLookupP50Us > 0 ? interned.coldLookupP50Us / conv.coldLookupP50Us : 1.0;
        interned.hotLatencyRatioVsConv = conv.hotLookupP50Us > 0 ? interned.hotLookupP50Us / conv.hotLookupP50Us : 1.0;
        interned.memoryRatioVsConv = conv.measuredFinalHeapBytes > 0 ? static_cast<double>(interned.measuredFinalHeapBytes) / static_cast<double>(conv.measuredFinalHeapBytes) : 1.0;
        interned.memorySavingsPctVsConv = 100.0 * (1.0 - interned.memoryRatioVsConv);
        writeResultRow(outResults, interned);
        allResults.push_back(interned);

        auto v1 = evalV1EventStream(timer, wl.name, wl.category, wl.events);
        v1.coldLatencyRatioVsConv = conv.coldLookupP50Us > 0 ? v1.coldLookupP50Us / conv.coldLookupP50Us : 1.0;
        v1.hotLatencyRatioVsConv = conv.hotLookupP50Us > 0 ? v1.hotLookupP50Us / conv.hotLookupP50Us : 1.0;
        v1.memoryRatioVsConv = conv.measuredFinalHeapBytes > 0 ? static_cast<double>(v1.measuredFinalHeapBytes) / static_cast<double>(conv.measuredFinalHeapBytes) : 1.0;
        v1.memorySavingsPctVsConv = 100.0 * (1.0 - v1.memoryRatioVsConv);
        writeResultRow(outResults, v1);
        allResults.push_back(v1);

        // 2. SymTabV2 Grid Search
        std::vector<ConfigEvalRow> wlV2Results;
        for (size_t cfgIdx = 0; cfgIdx < configGrid.size(); cfgIdx++) {
            auto r = evalV2ConfigEventStream(timer, wl.name, wl.category, cfgIdx + 1, configGrid[cfgIdx], wl.events);
            r.coldLatencyRatioVsConv = conv.coldLookupP50Us > 0 ? r.coldLookupP50Us / conv.coldLookupP50Us : 1.0;
            r.hotLatencyRatioVsConv = conv.hotLookupP50Us > 0 ? r.hotLookupP50Us / conv.hotLookupP50Us : 1.0;
            r.memoryRatioVsConv = conv.measuredFinalHeapBytes > 0 ? static_cast<double>(r.measuredFinalHeapBytes) / static_cast<double>(conv.measuredFinalHeapBytes) : 1.0;
            r.memorySavingsPctVsConv = 100.0 * (1.0 - r.memoryRatioVsConv);
            writeResultRow(outResults, r);
            wlV2Results.push_back(r);
            allResults.push_back(r);
        }

        // 3. Compute Latency-Constrained Optimal Configurations for each L threshold
        for (double L : latencyConstraints) {
            double maxAllowedColdUs = L * conv.coldLookupP50Us;

            ConfigEvalRow bestConfig;
            bool found = false;
            long long minMem = std::numeric_limits<long long>::max();

            for (auto& v2r : wlV2Results) {
                if (v2r.coldLookupP50Us <= maxAllowedColdUs) {
                    if (v2r.measuredFinalHeapBytes < minMem) {
                        minMem = v2r.measuredFinalHeapBytes;
                        bestConfig = v2r;
                        found = true;
                    }
                }
            }

            if (conv.coldLookupP50Us <= maxAllowedColdUs && conv.measuredFinalHeapBytes < minMem) {
                minMem = conv.measuredFinalHeapBytes;
                bestConfig = conv;
                found = true;
            }

            outFrontier << wl.name << "," << wl.category << "," << L << ","
                       << (found ? 1 : 0) << ","
                       << (found ? bestConfig.impl : "NONE") << ","
                       << (found ? bestConfig.configId : 0) << ","
                       << (found ? bestConfig.inlineMaxLen : 0) << ","
                       << (found ? bestConfig.compressMinLen : 0) << ","
                       << (found ? bestConfig.blockSize : 0) << ","
                       << (found ? bestConfig.anchorInterval : 0) << ","
                       << (found ? bestConfig.hotAccessThreshold : 0) << ","
                       << conv.coldLookupP50Us << "," << conv.measuredFinalHeapBytes << ","
                       << (found ? bestConfig.coldLookupP50Us : 0.0) << ","
                       << (found ? bestConfig.measuredFinalHeapBytes : 0) << ","
                       << (found ? bestConfig.coldLatencyRatioVsConv : 0.0) << ","
                       << (found ? bestConfig.memoryRatioVsConv : 0.0) << ","
                       << (found ? bestConfig.memorySavingsPctVsConv : 0.0) << "\n";
        }
        std::cout << "  Completed " << wl.name << " (" << wlV2Results.size() << " configs evaluated)\n";
    }

    outResults.flush();
    outFrontier.flush();

    std::cout << "\nWrote results/pareto_results.csv and results/pareto_frontier.csv\n";
    return 0;
}
