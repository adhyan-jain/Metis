// Controlled Synthetic Experiments Harness (Phase 5 / Evaluation)
//
// Evaluates workloads A-F to test theoretical break-even predictions:
//   A: 100% INLINE population (varying N live symbols)
//   B: 90/10 INLINE/INTERNED+COMPRESSED mix
//   C: 70/30 INLINE/INTERNED+COMPRESSED mix
//   D: Sweep of identifier length L and live duplication ratio k
//   E: Compressed-Heavy with high scope depth & scope churn
//   F: Hot/Cold access skew (80/20 Pareto distribution)
//
// Baseline comparisons:
//   - Conventional
//   - Interned
//   - SymTabV3
//   - SymTabV4
//   - Conventional-HeapString
//
// Measured metrics:
//   - Physical allocator heap bytes (via heap_counter.hpp)
//   - Lookup & Insert latency (p50, p95, p99, mean)
//   - Representation counts & promotion/demotion events
//
// Output: results/synthetic_experiments_A_F.csv

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

#include "../include/conventional_symbol_table.hpp"
#include "../include/conventional_heap_string_symbol_table.hpp"
#include "../include/interned_symbol_table.hpp"
#include "../include/symtab_v3.hpp"
#include "../include/symtab_v4.hpp"
#include "../include/hires_timer.hpp"

using namespace budgetsym;

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

struct SyntheticRow {
    std::string experiment;
    std::string paramName;
    double paramValue = 0.0;
    std::string impl;

    size_t declarations = 0;
    size_t uses = 0;
    size_t uniqueNames = 0;

    long long measuredPeakHeapBytes = 0;
    long long measuredFinalHeapBytes = 0;
    long long modeledFinalBytes = 0;

    double lookupP50Us = 0.0;
    double lookupP95Us = 0.0;
    double lookupP99Us = 0.0;
    double lookupMeanUs = 0.0;

    double insertP50Us = 0.0;
    double insertP95Us = 0.0;
    double insertP99Us = 0.0;
    double insertMeanUs = 0.0;

    size_t promotions = 0;
    size_t demotions = 0;
    size_t countInline = 0;
    size_t countInterned = 0;
    size_t countCompressed = 0;
};

// Workload Generators

// Random string helper
static std::string makeIdentifier(std::mt19937& rng, size_t len, size_t idNum) {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
    std::string prefix = "sym_" + std::to_string(idNum) + "_";
    if (prefix.size() >= len) return prefix.substr(0, len);
    std::string res = prefix;
    std::uniform_int_distribution<size_t> dist(0, sizeof(chars) - 2);
    while (res.size() < len) {
        res += chars[dist(rng)];
    }
    return res;
}

// Workload A: 100% INLINE (len = 8)
static std::vector<Event> genWorkloadA(size_t numSymbols, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<Event> events;
    events.push_back({Event::ENTER_SCOPE, "", 0});
    for (size_t i = 0; i < numSymbols; i++) {
        std::string name = makeIdentifier(rng, 8, i);
        events.push_back({Event::DECLARE, name, 1});
        events.push_back({Event::USE, name, 1});
    }
    events.push_back({Event::EXIT_SCOPE, "", 0});
    return events;
}

// Workload B: 90/10 Mix (90% len=8, 10% len=24)
static std::vector<Event> genWorkloadB(size_t numSymbols, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<Event> events;
    events.push_back({Event::ENTER_SCOPE, "", 0});
    for (size_t i = 0; i < numSymbols; i++) {
        size_t len = (i % 10 == 0) ? 24 : 8;
        std::string name = makeIdentifier(rng, len, i);
        events.push_back({Event::DECLARE, name, 1});
        events.push_back({Event::USE, name, 1});
    }
    events.push_back({Event::EXIT_SCOPE, "", 0});
    return events;
}

// Workload C: 70/30 Mix (70% len=8, 30% len=24)
static std::vector<Event> genWorkloadC(size_t numSymbols, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<Event> events;
    events.push_back({Event::ENTER_SCOPE, "", 0});
    for (size_t i = 0; i < numSymbols; i++) {
        size_t len = (i % 10 < 3) ? 24 : 8;
        std::string name = makeIdentifier(rng, len, i);
        events.push_back({Event::DECLARE, name, 1});
        events.push_back({Event::USE, name, 1});
    }
    events.push_back({Event::EXIT_SCOPE, "", 0});
    return events;
}

// Workload D1: Varying identifier length L (fixed k live duplicates)
static std::vector<Event> genWorkloadD_Length(size_t L, size_t k, size_t uniqueSymbols, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<Event> events;
    std::vector<std::string> names;
    for (size_t i = 0; i < uniqueSymbols; i++) {
        names.push_back(makeIdentifier(rng, L, i));
    }
    for (size_t dup = 0; dup < k; dup++) {
        events.push_back({Event::ENTER_SCOPE, "", static_cast<int>(dup + 1)});
        for (auto& name : names) {
            events.push_back({Event::DECLARE, name, static_cast<int>(dup + 1)});
            events.push_back({Event::USE, name, static_cast<int>(dup + 1)});
        }
    }
    for (size_t dup = 0; dup < k; dup++) {
        events.push_back({Event::EXIT_SCOPE, "", 0});
    }
    return events;
}

// Workload D2: Varying live duplication k (fixed length L = 32)
static std::vector<Event> genWorkloadD_Duplication(size_t k, size_t L, size_t uniqueSymbols, unsigned seed) {
    return genWorkloadD_Length(L, k, uniqueSymbols, seed);
}

// Workload E: Compressed-Heavy & Scope Churn (len = 36, high scope depth)
static std::vector<Event> genWorkloadE(size_t numScopes, size_t symbolsPerScope, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<Event> events;
    size_t idCounter = 0;
    for (size_t s = 0; s < numScopes; s++) {
        events.push_back({Event::ENTER_SCOPE, "", 1});
        for (size_t i = 0; i < symbolsPerScope; i++) {
            std::string name = makeIdentifier(rng, 36, idCounter++);
            events.push_back({Event::DECLARE, name, 1});
            events.push_back({Event::USE, name, 1});
        }
        if (s % 2 == 1) {
            events.push_back({Event::EXIT_SCOPE, "", 0});
        }
    }
    while (events.back().kind != Event::EXIT_SCOPE) {
        events.push_back({Event::EXIT_SCOPE, "", 0});
    }
    return events;
}

// Workload F: Hot/Cold Skew (Pareto 80/20 lookup access distribution)
static std::vector<Event> genWorkloadF(size_t numSymbols, size_t totalLookups, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<Event> events;
    events.push_back({Event::ENTER_SCOPE, "", 0});

    std::vector<std::string> names;
    for (size_t i = 0; i < numSymbols; i++) {
        size_t len = (i % 2 == 0) ? 28 : 10;
        std::string name = makeIdentifier(rng, len, i);
        names.push_back(name);
        events.push_back({Event::DECLARE, name, 1});
    }

    // Pareto 80/20 distribution
    size_t hotCount = numSymbols / 5;
    std::uniform_int_distribution<size_t> hotDist(0, hotCount > 0 ? hotCount - 1 : 0);
    std::uniform_int_distribution<size_t> coldDist(hotCount, numSymbols - 1);
    std::uniform_real_distribution<double> coin(0.0, 1.0);

    for (size_t l = 0; l < totalLookups; l++) {
        size_t idx = (coin(rng) < 0.8) ? hotDist(rng) : coldDist(rng);
        events.push_back({Event::USE, names[idx], 1});
    }
    events.push_back({Event::EXIT_SCOPE, "", 0});
    return events;
}

// Table Runner

template<typename Table>
static SyntheticRow runTable(HiResTimer& timer, const std::string& expName,
                             const std::string& paramName, double paramVal,
                             const std::string& implName,
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
            auto a = timer.now();
            volatile int id = t.insert(ev.symbol);
            auto b = timer.now();
            (void)id;
            insSamples.push_back(timer.microsecondsBetween(a, b));
        } else if (ev.kind == Event::USE) {
            uses++;
            uniqueSymbols.insert(ev.symbol);
            auto a = timer.now();
            volatile int id = t.resolve(ev.symbol);
            auto b = timer.now();
            (void)id;
            lookupSamples.push_back(timer.microsecondsBetween(a, b));
            t.recordAccess(ev.symbol);
        }
    }

    SyntheticRow r;
    r.experiment = expName;
    r.paramName  = paramName;
    r.paramValue = paramVal;
    r.impl       = implName;

    r.declarations = decls;
    r.uses         = uses;
    r.uniqueNames  = uniqueSymbols.size();

    r.measuredFinalHeapBytes = hs.bytes();
    r.measuredPeakHeapBytes  = hs.peakBytes();
    r.modeledFinalBytes      = t.tracker().current();

    r.insertP50Us  = pctile(insSamples, 0.50);
    r.insertP95Us  = pctile(insSamples, 0.95);
    r.insertP99Us  = pctile(insSamples, 0.99);
    r.insertMeanUs = vmean(insSamples);

    r.lookupP50Us  = pctile(lookupSamples, 0.50);
    r.lookupP95Us  = pctile(lookupSamples, 0.95);
    r.lookupP99Us  = pctile(lookupSamples, 0.99);
    r.lookupMeanUs = vmean(lookupSamples);

    return r;
}

static SyntheticRow runV3(HiResTimer& timer, const std::string& expName,
                           const std::string& paramName, double paramVal,
                           const std::vector<Event>& events) {
    heap::Scope hs;
    budgetsym::v3::SymTabV3<> t(0);

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
            auto a = timer.now();
            volatile int id = t.insert(ev.symbol);
            auto b = timer.now();
            (void)id;
            insSamples.push_back(timer.microsecondsBetween(a, b));
        } else if (ev.kind == Event::USE) {
            uses++;
            uniqueSymbols.insert(ev.symbol);
            auto a = timer.now();
            volatile int id = t.resolve(ev.symbol);
            auto b = timer.now();
            (void)id;
            lookupSamples.push_back(timer.microsecondsBetween(a, b));
            t.recordAccess(ev.symbol);
        }
    }

    SyntheticRow r;
    r.experiment = expName;
    r.paramName  = paramName;
    r.paramValue = paramVal;
    r.impl       = "SymTabV3";

    r.declarations = decls;
    r.uses         = uses;
    r.uniqueNames  = uniqueSymbols.size();

    r.measuredFinalHeapBytes = hs.bytes();
    r.measuredPeakHeapBytes  = hs.peakBytes();
    r.modeledFinalBytes      = t.tracker().current();

    r.insertP50Us  = pctile(insSamples, 0.50);
    r.insertP95Us  = pctile(insSamples, 0.95);
    r.insertP99Us  = pctile(insSamples, 0.99);
    r.insertMeanUs = vmean(insSamples);

    r.lookupP50Us  = pctile(lookupSamples, 0.50);
    r.lookupP95Us  = pctile(lookupSamples, 0.95);
    r.lookupP99Us  = pctile(lookupSamples, 0.99);
    r.lookupMeanUs = vmean(lookupSamples);

    r.promotions = t.promotions();
    r.demotions  = t.demotions();

    for (auto& sym : uniqueSymbols) {
        switch (t.representationOf(sym)) {
            case budgetsym::v3::Rep::INLINE_REP:     r.countInline++;    break;
            case budgetsym::v3::Rep::INTERNED_REP:   r.countInterned++;  break;
            case budgetsym::v3::Rep::COMPRESSED_REP: r.countCompressed++; break;
        }
    }
    return r;
}

static SyntheticRow runV4(HiResTimer& timer, const std::string& expName,
                           const std::string& paramName, double paramVal,
                           const std::vector<Event>& events) {
    heap::Scope hs;
    budgetsym::v4::SymTabV4<> t(0);

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
            auto a = timer.now();
            volatile int id = t.insert(ev.symbol);
            auto b = timer.now();
            (void)id;
            insSamples.push_back(timer.microsecondsBetween(a, b));
        } else if (ev.kind == Event::USE) {
            uses++;
            uniqueSymbols.insert(ev.symbol);
            auto a = timer.now();
            volatile int id = t.resolve(ev.symbol);
            auto b = timer.now();
            (void)id;
            lookupSamples.push_back(timer.microsecondsBetween(a, b));
            t.recordAccess(ev.symbol);
        }
    }

    SyntheticRow r;
    r.experiment = expName;
    r.paramName  = paramName;
    r.paramValue = paramVal;
    r.impl       = "SymTabV4";

    r.declarations = decls;
    r.uses         = uses;
    r.uniqueNames  = uniqueSymbols.size();

    r.measuredFinalHeapBytes = hs.bytes();
    r.measuredPeakHeapBytes  = hs.peakBytes();
    r.modeledFinalBytes      = t.tracker().current();

    r.insertP50Us  = pctile(insSamples, 0.50);
    r.insertP95Us  = pctile(insSamples, 0.95);
    r.insertP99Us  = pctile(insSamples, 0.99);
    r.insertMeanUs = vmean(insSamples);

    r.lookupP50Us  = pctile(lookupSamples, 0.50);
    r.lookupP95Us  = pctile(lookupSamples, 0.95);
    r.lookupP99Us  = pctile(lookupSamples, 0.99);
    r.lookupMeanUs = vmean(lookupSamples);

    r.promotions = t.promotions();
    r.demotions  = t.demotions();

    for (auto& sym : uniqueSymbols) {
        switch (t.representationOf(sym)) {
            case budgetsym::v4::Rep::INLINE_REP:     r.countInline++;    break;
            case budgetsym::v4::Rep::INTERNED_REP:   r.countInterned++;  break;
            case budgetsym::v4::Rep::COMPRESSED_REP: r.countCompressed++; break;
        }
    }
    return r;
}

static void writeHeader(std::ofstream& out) {
    out << "experiment,param_name,param_value,implementation,"
           "declarations,uses,unique_names,"
           "measured_peak_heap_bytes,measured_final_heap_bytes,modeled_final_bytes,"
           "lookup_p50_us,lookup_p95_us,lookup_p99_us,lookup_mean_us,"
           "insert_p50_us,insert_p95_us,insert_p99_us,insert_mean_us,"
           "promotions,demotions,count_inline,count_interned,count_compressed\n";
}

static void writeRow(std::ofstream& out, const SyntheticRow& r) {
    out << r.experiment << "," << r.paramName << "," << r.paramValue << "," << r.impl << ","
        << r.declarations << "," << r.uses << "," << r.uniqueNames << ","
        << r.measuredPeakHeapBytes << "," << r.measuredFinalHeapBytes << "," << r.modeledFinalBytes << ","
        << r.lookupP50Us << "," << r.lookupP95Us << "," << r.lookupP99Us << "," << r.lookupMeanUs << ","
        << r.insertP50Us << "," << r.insertP95Us << "," << r.insertP99Us << "," << r.insertMeanUs << ","
        << r.promotions << "," << r.demotions << ","
        << r.countInline << "," << r.countInterned << "," << r.countCompressed << "\n";
}

static void runAllImplementations(std::ofstream& out, HiResTimer& timer,
                                   const std::string& expName, const std::string& paramName,
                                   double paramVal, const std::vector<Event>& events) {
    auto conv = runTable<ConventionalSymbolTable>(timer, expName, paramName, paramVal, "Conventional", events);
    writeRow(out, conv);

    auto intn = runTable<InternedSymbolTable>(timer, expName, paramName, paramVal, "Interned", events);
    writeRow(out, intn);

    auto heapStr = runTable<ConventionalHeapStringSymbolTable>(timer, expName, paramName, paramVal, "Conventional-HeapString", events);
    writeRow(out, heapStr);

    auto v3 = runV3(timer, expName, paramName, paramVal, events);
    writeRow(out, v3);

    auto v4 = runV4(timer, expName, paramName, paramVal, events);
    writeRow(out, v4);
}

int main() {
    HiResTimer timer;
    std::ofstream out("results/synthetic_experiments_A_F.csv");
    if (!out) {
        std::cerr << "ERROR: cannot open results/synthetic_experiments_A_F.csv for writing\n";
        return 1;
    }
    writeHeader(out);

    std::cout << "=== Running Controlled Synthetic Experiments A-F ===\n\n";

    // Experiment A: 100% INLINE vs N symbols
    std::cout << "Running Experiment A (100% INLINE, N live symbols) ...\n";
    for (size_t N : {1000, 5000, 10000, 20000, 50000}) {
        auto events = genWorkloadA(N, 42);
        runAllImplementations(out, timer, "A_100pct_Inline", "live_symbols", static_cast<double>(N), events);
    }

    // Experiment B: 90/10 Mix vs N symbols
    std::cout << "Running Experiment B (90/10 Mix, N live symbols) ...\n";
    for (size_t N : {1000, 5000, 10000, 20000, 50000}) {
        auto events = genWorkloadB(N, 42);
        runAllImplementations(out, timer, "B_90_10_Mix", "live_symbols", static_cast<double>(N), events);
    }

    // Experiment C: 70/30 Mix vs N symbols
    std::cout << "Running Experiment C (70/30 Mix, N live symbols) ...\n";
    for (size_t N : {1000, 5000, 10000, 20000, 50000}) {
        auto events = genWorkloadC(N, 42);
        runAllImplementations(out, timer, "C_70_30_Mix", "live_symbols", static_cast<double>(N), events);
    }

    // Experiment D1: Identifier Length L (fixed k = 4 live duplicates)
    std::cout << "Running Experiment D1 (Identifier Length L, fixed k=4) ...\n";
    for (size_t L : {4, 8, 12, 16, 20, 24, 32, 48, 64}) {
        auto events = genWorkloadD_Length(L, 4, 2000, 42);
        runAllImplementations(out, timer, "D1_Identifier_Length", "identifier_length", static_cast<double>(L), events);
    }

    // Experiment D2: Live Duplication Ratio k (fixed L = 32)
    std::cout << "Running Experiment D2 (Live Duplication Ratio k, fixed L=32) ...\n";
    for (size_t k : {1, 2, 3, 4, 5, 6, 8, 10, 15, 20}) {
        auto events = genWorkloadD_Duplication(k, 32, 2000, 42);
        runAllImplementations(out, timer, "D2_Live_Duplication", "live_duplication_k", static_cast<double>(k), events);
    }

    // Experiment E: Compressed-Heavy & Scope Churn (Scope depth sweep)
    std::cout << "Running Experiment E (Compressed-Heavy & Scope Churn) ...\n";
    for (size_t scopes : {10, 20, 50, 100}) {
        auto events = genWorkloadE(scopes, 100, 42);
        runAllImplementations(out, timer, "E_Scope_Churn", "num_scopes", static_cast<double>(scopes), events);
    }

    // Experiment F: Hot/Cold Skew (Total lookups sweep)
    std::cout << "Running Experiment F (Hot/Cold Skew, Pareto 80/20) ...\n";
    for (size_t lookups : {5000, 20000, 50000, 100000}) {
        auto events = genWorkloadF(2000, lookups, 42);
        runAllImplementations(out, timer, "F_Hot_Cold_Skew", "total_lookups", static_cast<double>(lookups), events);
    }

    out.flush();
    std::cout << "\nWrote results/synthetic_experiments_A_F.csv\n";
    return 0;
}
