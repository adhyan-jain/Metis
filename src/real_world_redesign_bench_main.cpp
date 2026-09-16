// Real-World Corpus Redesigned V2 Benchmark Engine
//
// Replays the extracted semantic event streams across:
//   1. FreeRTOS
//   2. Arduino
//   3. Zephyr
//   4. CPython
//   5. Lua
//   6. ESP-IDF
//
// Compares: Conventional vs Interned vs Redesigned SymTabV2 (Headline & Parameter Sweep)
// Outputs: data/real_world_redesign_benchmark.csv

#define BUDGETSYM_HEAP_COUNTER_IMPL
#include "../include/heap_counter.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "../include/conventional_symbol_table.hpp"
#include "../include/interned_symbol_table.hpp"
#include "../include/hires_timer.hpp"
#include "../include/symtab_v2.hpp"

using namespace budgetsym;
using namespace budgetsym::v2;

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

struct CorpusBenchRow {
    std::string corpus;
    std::string impl;
    size_t declarations = 0;
    size_t uses = 0;
    size_t uniqueNames = 0;

    long long modeledPeakBytes = 0;
    long long modeledFinalBytes = 0;
    long long measuredPeakHeapBytes = 0;
    long long measuredFinalHeapBytes = 0;
    long long measuredBytesPerUniqueSymbol = 0;

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
    size_t reconCount = 0;
    size_t reconSteps = 0;
    double reconDepth = 0.0;

    size_t countInline = 0;
    size_t countInterned = 0;
    size_t countCompressed = 0;
};

static std::vector<Event> loadEvents(const std::string& filepath) {
    std::vector<Event> events;
    std::ifstream in(filepath);
    if (!in) return events;

    std::string line;
    while (std::getline(in, line)) {
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
    }
    return events;
}

template<typename Table>
static CorpusBenchRow runTable(HiResTimer& timer, const std::string& corpusName,
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

    CorpusBenchRow r;
    r.corpus = corpusName;
    r.impl = implName;
    r.declarations = decls;
    r.uses = uses;
    r.uniqueNames = uniqueSymbols.size();

    r.measuredFinalHeapBytes = hs.bytes();
    r.measuredPeakHeapBytes  = hs.peakBytes();
    r.modeledFinalBytes = t.tracker().current();
    r.modeledPeakBytes  = t.tracker().peak();
    r.measuredBytesPerUniqueSymbol = r.uniqueNames > 0
        ? r.measuredFinalHeapBytes / static_cast<long long>(r.uniqueNames) : 0;

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

static CorpusBenchRow runV2Config(HiResTimer& timer, const std::string& corpusName,
                                  const std::string& implName,
                                  PolicyConfigV2 cfg,
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

    CorpusBenchRow r;
    r.corpus = corpusName;
    r.impl = implName;
    r.declarations = decls;
    r.uses = uses;
    r.uniqueNames = uniqueSymbols.size();

    r.measuredFinalHeapBytes = hs.bytes();
    r.measuredPeakHeapBytes  = hs.peakBytes();
    r.modeledFinalBytes = t.tracker().current();
    r.modeledPeakBytes  = t.tracker().peak();
    r.measuredBytesPerUniqueSymbol = r.uniqueNames > 0
        ? r.measuredFinalHeapBytes / static_cast<long long>(r.uniqueNames) : 0;

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
    r.reconCount = t.reconstructionCount();
    r.reconSteps = t.reconstructionStepsTotal();
    r.reconDepth = r.reconCount > 0
        ? static_cast<double>(r.reconSteps) / static_cast<double>(r.reconCount) : 0.0;

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
    out << "corpus,implementation,declarations,uses,unique_names,"
           "modeled_peak_bytes,modeled_final_bytes,"
           "measured_peak_heap_bytes,measured_final_heap_bytes,"
           "measured_bytes_per_unique_symbol,"
           "lookup_p50_us,lookup_p95_us,lookup_p99_us,lookup_mean_us,"
           "insert_p50_us,insert_p95_us,insert_p99_us,insert_mean_us,"
           "promotions,demotions,reconstruction_count,reconstruction_steps_total,"
           "mean_reconstruction_depth,count_inline,count_interned,count_compressed\n";
}

static void writeRow(std::ofstream& out, const CorpusBenchRow& r) {
    out << r.corpus << "," << r.impl << "," << r.declarations << "," << r.uses << "," << r.uniqueNames << ","
        << r.modeledPeakBytes << "," << r.modeledFinalBytes << ","
        << r.measuredPeakHeapBytes << "," << r.measuredFinalHeapBytes << ","
        << r.measuredBytesPerUniqueSymbol << ","
        << r.lookupP50Us << "," << r.lookupP95Us << "," << r.lookupP99Us << "," << r.lookupMeanUs << ","
        << r.insertP50Us << "," << r.insertP95Us << "," << r.insertP99Us << "," << r.insertMeanUs << ","
        << r.promotions << "," << r.demotions << ","
        << r.reconCount << "," << r.reconSteps << "," << r.reconDepth << ","
        << r.countInline << "," << r.countInterned << "," << r.countCompressed << "\n";
}

int main() {
    HiResTimer timer;

    std::ofstream out("data/real_world_redesign_benchmark.csv");
    if (!out) {
        std::cerr << "ERROR: cannot open data/real_world_redesign_benchmark.csv for writing\n";
        return 1;
    }
    writeHeader(out);

    std::vector<std::string> corpora = {"FreeRTOS", "Arduino", "Zephyr", "CPython", "Lua", "ESP-IDF"};

    std::cout << "=== Running Redesigned V2 Real-World Corpus Benchmark ===\n";

    for (auto& corpus : corpora) {
        std::string eventFile = "data/corpus_events_" + corpus + ".txt";
        if (!std::ifstream(eventFile)) {
            eventFile = "results/corpus_events_" + corpus + ".txt";
        }
        std::cout << "\n--------------------------------------------------------------------------\n";
        std::cout << "Processing " << corpus << " from " << eventFile << " ... " << std::flush;
        auto events = loadEvents(eventFile);
        if (events.empty()) {
            std::cout << "missing/empty events file, skipping\n";
            continue;
        }
        std::cout << events.size() << " events loaded\n";

        // 1. Conventional
        auto convRow = runTable<ConventionalSymbolTable>(timer, corpus, "Conventional", events);
        writeRow(out, convRow);
        std::cout << "  Conventional       : Peak Heap=" << std::setw(10) << convRow.measuredPeakHeapBytes << " B, p95=" 
                  << std::fixed << std::setprecision(4) << convRow.lookupP95Us << " us\n";

        // 2. Interned
        auto intRow = runTable<InternedSymbolTable>(timer, corpus, "Interned", events);
        writeRow(out, intRow);
        std::cout << "  Interned           : Peak Heap=" << std::setw(10) << intRow.measuredPeakHeapBytes << " B, p95=" 
                  << std::fixed << std::setprecision(4) << intRow.lookupP95Us << " us\n";

        // 3. Redesigned V2 Default Policy
        PolicyConfigV2 defaultCfg;
        auto v2DefaultRow = runV2Config(timer, corpus, "Redesigned_V2_Default", defaultCfg, events);
        writeRow(out, v2DefaultRow);
        double v2RatioConv = static_cast<double>(v2DefaultRow.measuredPeakHeapBytes) / convRow.measuredPeakHeapBytes;
        double v2LatencyRatio = convRow.lookupP95Us > 0 ? (v2DefaultRow.lookupP95Us / convRow.lookupP95Us) : 1.0;
        std::cout << "  Redesigned_V2_Def  : Peak Heap=" << std::setw(10) << v2DefaultRow.measuredPeakHeapBytes << " B ("
                  << std::fixed << std::setprecision(2) << v2RatioConv << "x Conv), p95=" 
                  << std::fixed << std::setprecision(4) << v2DefaultRow.lookupP95Us << " us ("
                  << std::fixed << std::setprecision(2) << v2LatencyRatio << "x Conv p95)\n";

        // 4. Redesigned V2 Memory-Optimized Policy (inlineMaxLen=16, compressMinLen=20)
        PolicyConfigV2 memCfg;
        memCfg.inlineMaxLen = 16;
        memCfg.compressMinLen = 20;
        auto v2MemRow = runV2Config(timer, corpus, "Redesigned_V2_MemOpt", memCfg, events);
        writeRow(out, v2MemRow);
        double v2MemRatioConv = static_cast<double>(v2MemRow.measuredPeakHeapBytes) / convRow.measuredPeakHeapBytes;
        double v2MemLatencyRatio = convRow.lookupP95Us > 0 ? (v2MemRow.lookupP95Us / convRow.lookupP95Us) : 1.0;
        std::cout << "  Redesigned_V2_Mem  : Peak Heap=" << std::setw(10) << v2MemRow.measuredPeakHeapBytes << " B ("
                  << std::fixed << std::setprecision(2) << v2MemRatioConv << "x Conv), p95=" 
                  << std::fixed << std::setprecision(4) << v2MemRow.lookupP95Us << " us ("
                  << std::fixed << std::setprecision(2) << v2MemLatencyRatio << "x Conv p95)\n";

        // 5. Redesigned V2 Inline-Heavy Policy (inlineMaxLen=20, compressMinLen=25)
        PolicyConfigV2 inlineCfg;
        inlineCfg.inlineMaxLen = 20;
        inlineCfg.compressMinLen = 25;
        auto v2InlineRow = runV2Config(timer, corpus, "Redesigned_V2_InlineHeavy", inlineCfg, events);
        writeRow(out, v2InlineRow);
        double v2InlineRatioConv = static_cast<double>(v2InlineRow.measuredPeakHeapBytes) / convRow.measuredPeakHeapBytes;
        double v2InlineLatencyRatio = convRow.lookupP95Us > 0 ? (v2InlineRow.lookupP95Us / convRow.lookupP95Us) : 1.0;
        std::cout << "  Redesigned_V2_Inl  : Peak Heap=" << std::setw(10) << v2InlineRow.measuredPeakHeapBytes << " B ("
                  << std::fixed << std::setprecision(2) << v2InlineRatioConv << "x Conv), p95=" 
                  << std::fixed << std::setprecision(4) << v2InlineRow.lookupP95Us << " us ("
                  << std::fixed << std::setprecision(2) << v2InlineLatencyRatio << "x Conv p95)\n";
    }

    out.flush();
    std::cout << "\n==========================================================================\n";
    std::cout << "Wrote data/real_world_redesign_benchmark.csv\n";
    std::cout << "==========================================================================\n";
    return 0;
}
