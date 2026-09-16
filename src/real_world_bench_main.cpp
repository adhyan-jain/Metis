// Real-World Corpus Benchmark Engine (Phase 4)
//
// Replays the extracted semantic event streams (DECLARE, USE, ENTER_SCOPE, EXIT_SCOPE)
// from representative real-world software workloads:
//   1. FreeRTOS (Embedded RTOS)
//   2. Arduino (Microcontroller HAL)
//   3. Zephyr (Scalable RTOS)
//   4. CPython (Compiler / Interpreter Core)
//   5. Lua (Lightweight Embedded C Interpreter - Adversarial)
//   6. ESP-IDF (Embedded IoT SDK)
//
// Primary Scientific Comparison: Conventional vs Interned vs SymTabV2.
// Outputs: data/real_world_benchmark.csv

#define BUDGETSYM_HEAP_COUNTER_IMPL
#include "../include/heap_counter.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "../include/budget_sym.hpp"
#include "../include/conventional_symbol_table.hpp"
#include "../include/hires_timer.hpp"
#include "../include/interned_symbol_table.hpp"
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

static CorpusBenchRow runV1(HiResTimer& timer, const std::string& corpusName,
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
    r.impl = "BudgetSymV1";
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
    return r;
}

static CorpusBenchRow runV2(HiResTimer& timer, const std::string& corpusName,
                             const std::vector<Event>& events) {
    heap::Scope hs;
    SymTabV2<> t(0);

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
    r.impl = "SymTabV2";
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

    std::ofstream out("data/real_world_benchmark.csv");
    if (!out) {
        std::cerr << "ERROR: cannot open data/real_world_benchmark.csv for writing\n";
        return 1;
    }
    writeHeader(out);

    std::vector<std::string> corpora = {"FreeRTOS", "Arduino", "Zephyr", "CPython", "Lua", "ESP-IDF"};

    std::cout << "=== Running Representative Real-World Corpus Benchmark ===\n";

    for (auto& corpus : corpora) {
        std::string eventFile = "data/corpus_events_" + corpus + ".txt";
        if (!std::ifstream(eventFile)) {
            eventFile = "results/corpus_events_" + corpus + ".txt";
        }
        std::cout << "Processing " << corpus << " from " << eventFile << " ... " << std::flush;
        auto events = loadEvents(eventFile);
        if (events.empty()) {
            std::cout << "missing/empty events file, skipping\n";
            continue;
        }
        std::cout << events.size() << " events loaded\n";

        auto convRow = runTable<ConventionalSymbolTable>(timer, corpus, "Conventional", events);
        writeRow(out, convRow);
        std::cout << "  Conventional: heap=" << convRow.measuredFinalHeapBytes << "B, lookup_p50=" << convRow.lookupP50Us << "us\n";

        auto intRow = runTable<InternedSymbolTable>(timer, corpus, "Interned", events);
        writeRow(out, intRow);
        std::cout << "  Interned    : heap=" << intRow.measuredFinalHeapBytes << "B, lookup_p50=" << intRow.lookupP50Us << "us\n";

        auto v1Row = runV1(timer, corpus, events);
        writeRow(out, v1Row);
        std::cout << "  BudgetSymV1 : heap=" << v1Row.measuredFinalHeapBytes << "B, lookup_p50=" << v1Row.lookupP50Us << "us\n";

        auto v2Row = runV2(timer, corpus, events);
        writeRow(out, v2Row);
        std::cout << "  SymTabV2    : heap=" << v2Row.measuredFinalHeapBytes << "B, lookup_p50=" << v2Row.lookupP50Us << "us\n";
    }

    out.flush();
    std::cout << "\nWrote data/real_world_benchmark.csv\n";
    return 0;
}
