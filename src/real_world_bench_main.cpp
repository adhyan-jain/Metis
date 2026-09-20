// Real-World Corpus Benchmark Engine (Phase 4 / Phase 2 Hardening)
//
// Replays the extracted semantic event streams (DECLARE, USE, ENTER_SCOPE, EXIT_SCOPE)
// from representative real-world software workloads with repeated timing passes
// and variance quantification.
//
// Primary Scientific Comparison: Conventional vs Interned vs SymTabV2 vs SymTabV3 vs SymTabV4.
// Outputs: data/real_world_benchmark.csv and results/real_world_benchmark.csv

#define BUDGETSYM_HEAP_COUNTER_IMPL
#include "heap_counter.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "historical/budget_sym.hpp"
#include "historical/conventional_symbol_table.hpp"
#include "historical/conventional_heap_string_symbol_table.hpp"
#include "hires_timer.hpp"
#include "historical/interned_symbol_table.hpp"
#include "historical/symtab_v2.hpp"
#include "historical/symtab_v3.hpp"
#include "historical/symtab_v4.hpp"

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

static double vstddev(const std::vector<double>& v, double meanVal) {
    if (v.size() <= 1) return 0.0;
    double sumSq = 0.0;
    for (double x : v) sumSq += (x - meanVal) * (x - meanVal);
    return std::sqrt(sumSq / static_cast<double>(v.size() - 1));
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

    size_t lookupInlineCount = 0;
    size_t lookupInternedCount = 0;
    size_t lookupCompressedCount = 0;

    int timingReps = 1;
    double lookupP95StddevUs = 0.0;
    double lookupMeanStddevUs = 0.0;
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
                               const std::vector<Event>& events,
                               int numReps = 3) {
    CorpusBenchRow r;
    r.corpus = corpusName;
    r.impl = implName;
    r.timingReps = numReps;

    // Pass 1: Physical Heap Tracking (Isolated)
    {
        heap::Scope hs;
        Table t(0);
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
                t.insert(ev.symbol);
            } else if (ev.kind == Event::USE) {
                uses++;
                uniqueSymbols.insert(ev.symbol);
                t.resolve(ev.symbol);
                t.recordAccess(ev.symbol);
            }
        }
        r.declarations = decls;
        r.uses = uses;
        r.uniqueNames = uniqueSymbols.size();
        r.measuredFinalHeapBytes = hs.bytes();
        r.measuredPeakHeapBytes  = hs.peakBytes();
        r.modeledFinalBytes = t.tracker().current();
        r.modeledPeakBytes  = t.tracker().peak();
        r.measuredBytesPerUniqueSymbol = r.uniqueNames > 0
            ? r.measuredFinalHeapBytes / static_cast<long long>(r.uniqueNames) : 0;
    }

    // Pass 2: Multi-repetition latency sampling
    std::vector<double> repLookupP50, repLookupP95, repLookupP99, repLookupMean;
    std::vector<double> repInsP50, repInsP95, repInsP99, repInsMean;

    for (int rep = 0; rep < numReps; ++rep) {
        Table t(0);
        std::vector<double> insSamples;
        std::vector<double> lookupSamples;

        for (auto& ev : events) {
            if (ev.kind == Event::ENTER_SCOPE) {
                t.enterScope();
            } else if (ev.kind == Event::EXIT_SCOPE) {
                t.exitScope();
            } else if (ev.kind == Event::DECLARE) {
                auto a = timer.now();
                volatile int id = t.insert(ev.symbol);
                auto b = timer.now();
                (void)id;
                insSamples.push_back(timer.microsecondsBetween(a, b));
            } else if (ev.kind == Event::USE) {
                auto a = timer.now();
                volatile int id = t.resolve(ev.symbol);
                auto b = timer.now();
                (void)id;
                lookupSamples.push_back(timer.microsecondsBetween(a, b));
                t.recordAccess(ev.symbol);
            }
        }

        repLookupP50.push_back(pctile(lookupSamples, 0.50));
        repLookupP95.push_back(pctile(lookupSamples, 0.95));
        repLookupP99.push_back(pctile(lookupSamples, 0.99));
        repLookupMean.push_back(vmean(lookupSamples));

        repInsP50.push_back(pctile(insSamples, 0.50));
        repInsP95.push_back(pctile(insSamples, 0.95));
        repInsP99.push_back(pctile(insSamples, 0.99));
        repInsMean.push_back(vmean(insSamples));
    }

    r.lookupP50Us  = pctile(repLookupP50, 0.50);
    r.lookupP95Us  = pctile(repLookupP95, 0.50);
    r.lookupP99Us  = pctile(repLookupP99, 0.50);
    r.lookupMeanUs = vmean(repLookupMean);

    r.insertP50Us  = pctile(repInsP50, 0.50);
    r.insertP95Us  = pctile(repInsP95, 0.50);
    r.insertP99Us  = pctile(repInsP99, 0.50);
    r.insertMeanUs = vmean(repInsMean);

    r.lookupP95StddevUs  = vstddev(repLookupP95, r.lookupP95Us);
    r.lookupMeanStddevUs = vstddev(repLookupMean, r.lookupMeanUs);

    return r;
}

static CorpusBenchRow runV1(HiResTimer& timer, const std::string& corpusName,
                             const std::vector<Event>& events,
                             int numReps = 3) {
    CorpusBenchRow r;
    r.corpus = corpusName;
    r.impl = "BudgetSymV1";
    r.timingReps = numReps;

    {
        heap::Scope hs;
        BudgetSym t(0);
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
                t.insert(ev.symbol);
            } else if (ev.kind == Event::USE) {
                uses++;
                uniqueSymbols.insert(ev.symbol);
                t.resolve(ev.symbol);
                t.recordAccess(ev.symbol);
            }
        }
        r.declarations = decls;
        r.uses = uses;
        r.uniqueNames = uniqueSymbols.size();
        r.measuredFinalHeapBytes = hs.bytes();
        r.measuredPeakHeapBytes  = hs.peakBytes();
        r.modeledFinalBytes = t.tracker().current();
        r.modeledPeakBytes  = t.tracker().peak();
        r.measuredBytesPerUniqueSymbol = r.uniqueNames > 0
            ? r.measuredFinalHeapBytes / static_cast<long long>(r.uniqueNames) : 0;
        r.promotions = t.promotions();
    }

    std::vector<double> repLookupP50, repLookupP95, repLookupP99, repLookupMean;
    std::vector<double> repInsP50, repInsP95, repInsP99, repInsMean;

    for (int rep = 0; rep < numReps; ++rep) {
        BudgetSym t(0);
        std::vector<double> insSamples;
        std::vector<double> lookupSamples;

        for (auto& ev : events) {
            if (ev.kind == Event::ENTER_SCOPE) {
                t.enterScope();
            } else if (ev.kind == Event::EXIT_SCOPE) {
                t.exitScope();
            } else if (ev.kind == Event::DECLARE) {
                auto a = timer.now();
                volatile int id = t.insert(ev.symbol);
                auto b = timer.now();
                (void)id;
                insSamples.push_back(timer.microsecondsBetween(a, b));
            } else if (ev.kind == Event::USE) {
                auto a = timer.now();
                volatile int id = t.resolve(ev.symbol);
                auto b = timer.now();
                (void)id;
                lookupSamples.push_back(timer.microsecondsBetween(a, b));
                t.recordAccess(ev.symbol);
            }
        }

        repLookupP50.push_back(pctile(lookupSamples, 0.50));
        repLookupP95.push_back(pctile(lookupSamples, 0.95));
        repLookupP99.push_back(pctile(lookupSamples, 0.99));
        repLookupMean.push_back(vmean(lookupSamples));

        repInsP50.push_back(pctile(insSamples, 0.50));
        repInsP95.push_back(pctile(insSamples, 0.95));
        repInsP99.push_back(pctile(insSamples, 0.99));
        repInsMean.push_back(vmean(insSamples));
    }

    r.lookupP50Us  = pctile(repLookupP50, 0.50);
    r.lookupP95Us  = pctile(repLookupP95, 0.50);
    r.lookupP99Us  = pctile(repLookupP99, 0.50);
    r.lookupMeanUs = vmean(repLookupMean);

    r.insertP50Us  = pctile(repInsP50, 0.50);
    r.insertP95Us  = pctile(repInsP95, 0.50);
    r.insertP99Us  = pctile(repInsP99, 0.50);
    r.insertMeanUs = vmean(repInsMean);

    r.lookupP95StddevUs  = vstddev(repLookupP95, r.lookupP95Us);
    r.lookupMeanStddevUs = vstddev(repLookupMean, r.lookupMeanUs);

    return r;
}

static CorpusBenchRow runV2(HiResTimer& timer, const std::string& corpusName,
                             const std::vector<Event>& events,
                             int numReps = 3) {
    CorpusBenchRow r;
    r.corpus = corpusName;
    r.impl = "SymTabV2";
    r.timingReps = numReps;

    {
        heap::Scope hs;
        SymTabV2<> t(0);
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
                t.insert(ev.symbol);
            } else if (ev.kind == Event::USE) {
                uses++;
                uniqueSymbols.insert(ev.symbol);
                t.resolve(ev.symbol);
                t.recordAccess(ev.symbol);
            }
        }
        r.declarations = decls;
        r.uses = uses;
        r.uniqueNames = uniqueSymbols.size();
        r.measuredFinalHeapBytes = hs.bytes();
        r.measuredPeakHeapBytes  = hs.peakBytes();
        r.modeledFinalBytes = t.tracker().current();
        r.modeledPeakBytes  = t.tracker().peak();
        r.measuredBytesPerUniqueSymbol = r.uniqueNames > 0
            ? r.measuredFinalHeapBytes / static_cast<long long>(r.uniqueNames) : 0;
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
    }

    std::vector<double> repLookupP50, repLookupP95, repLookupP99, repLookupMean;
    std::vector<double> repInsP50, repInsP95, repInsP99, repInsMean;

    for (int rep = 0; rep < numReps; ++rep) {
        SymTabV2<> t(0);
        std::vector<double> insSamples;
        std::vector<double> lookupSamples;

        for (auto& ev : events) {
            if (ev.kind == Event::ENTER_SCOPE) {
                t.enterScope();
            } else if (ev.kind == Event::EXIT_SCOPE) {
                t.exitScope();
            } else if (ev.kind == Event::DECLARE) {
                auto a = timer.now();
                volatile int id = t.insert(ev.symbol);
                auto b = timer.now();
                (void)id;
                insSamples.push_back(timer.microsecondsBetween(a, b));
            } else if (ev.kind == Event::USE) {
                auto a = timer.now();
                volatile int id = t.resolve(ev.symbol);
                auto b = timer.now();
                (void)id;
                lookupSamples.push_back(timer.microsecondsBetween(a, b));
                t.recordAccess(ev.symbol);
            }
        }

        repLookupP50.push_back(pctile(lookupSamples, 0.50));
        repLookupP95.push_back(pctile(lookupSamples, 0.95));
        repLookupP99.push_back(pctile(lookupSamples, 0.99));
        repLookupMean.push_back(vmean(lookupSamples));

        repInsP50.push_back(pctile(insSamples, 0.50));
        repInsP95.push_back(pctile(insSamples, 0.95));
        repInsP99.push_back(pctile(insSamples, 0.99));
        repInsMean.push_back(vmean(insSamples));
    }

    r.lookupP50Us  = pctile(repLookupP50, 0.50);
    r.lookupP95Us  = pctile(repLookupP95, 0.50);
    r.lookupP99Us  = pctile(repLookupP99, 0.50);
    r.lookupMeanUs = vmean(repLookupMean);

    r.insertP50Us  = pctile(repInsP50, 0.50);
    r.insertP95Us  = pctile(repInsP95, 0.50);
    r.insertP99Us  = pctile(repInsP99, 0.50);
    r.insertMeanUs = vmean(repInsMean);

    r.lookupP95StddevUs  = vstddev(repLookupP95, r.lookupP95Us);
    r.lookupMeanStddevUs = vstddev(repLookupMean, r.lookupMeanUs);

    return r;
}

static CorpusBenchRow runV3(HiResTimer& timer, const std::string& corpusName,
                             const std::vector<Event>& events,
                             int numReps = 3) {
    CorpusBenchRow r;
    r.corpus = corpusName;
    r.impl = "SymTabV3";
    r.timingReps = numReps;

    {
        heap::Scope hs;
        budgetsym::v3::SymTabV3<> t(0);
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
                t.insert(ev.symbol);
            } else if (ev.kind == Event::USE) {
                uses++;
                uniqueSymbols.insert(ev.symbol);
                t.resolve(ev.symbol);
                t.recordAccess(ev.symbol);
            }
        }
        r.declarations = decls;
        r.uses = uses;
        r.uniqueNames = uniqueSymbols.size();
        r.measuredFinalHeapBytes = hs.bytes();
        r.measuredPeakHeapBytes  = hs.peakBytes();
        r.modeledFinalBytes = t.tracker().current();
        r.modeledPeakBytes  = t.tracker().peak();
        r.measuredBytesPerUniqueSymbol = r.uniqueNames > 0
            ? r.measuredFinalHeapBytes / static_cast<long long>(r.uniqueNames) : 0;
        r.promotions = t.promotions();
        r.demotions  = t.demotions();
        r.reconCount = t.reconstructionCount();
        r.reconSteps = t.reconstructionStepsTotal();
        r.reconDepth = r.reconCount > 0
            ? static_cast<double>(r.reconSteps) / static_cast<double>(r.reconCount) : 0.0;

        r.lookupInlineCount     = t.inlineLookups();
        r.lookupInternedCount   = t.internedLookups();
        r.lookupCompressedCount = t.compressedLookups();

        for (auto& sym : uniqueSymbols) {
            switch (t.representationOf(sym)) {
                case budgetsym::v3::Rep::INLINE_REP:     r.countInline++;    break;
                case budgetsym::v3::Rep::INTERNED_REP:   r.countInterned++;  break;
                case budgetsym::v3::Rep::COMPRESSED_REP: r.countCompressed++; break;
            }
        }
    }

    std::vector<double> repLookupP50, repLookupP95, repLookupP99, repLookupMean;
    std::vector<double> repInsP50, repInsP95, repInsP99, repInsMean;

    for (int rep = 0; rep < numReps; ++rep) {
        budgetsym::v3::SymTabV3<> t(0);
        std::vector<double> insSamples;
        std::vector<double> lookupSamples;

        for (auto& ev : events) {
            if (ev.kind == Event::ENTER_SCOPE) {
                t.enterScope();
            } else if (ev.kind == Event::EXIT_SCOPE) {
                t.exitScope();
            } else if (ev.kind == Event::DECLARE) {
                auto a = timer.now();
                volatile int id = t.insert(ev.symbol);
                auto b = timer.now();
                (void)id;
                insSamples.push_back(timer.microsecondsBetween(a, b));
            } else if (ev.kind == Event::USE) {
                auto a = timer.now();
                volatile int id = t.resolve(ev.symbol);
                auto b = timer.now();
                (void)id;
                lookupSamples.push_back(timer.microsecondsBetween(a, b));
                t.recordAccess(ev.symbol);
            }
        }

        repLookupP50.push_back(pctile(lookupSamples, 0.50));
        repLookupP95.push_back(pctile(lookupSamples, 0.95));
        repLookupP99.push_back(pctile(lookupSamples, 0.99));
        repLookupMean.push_back(vmean(lookupSamples));

        repInsP50.push_back(pctile(insSamples, 0.50));
        repInsP95.push_back(pctile(insSamples, 0.95));
        repInsP99.push_back(pctile(insSamples, 0.99));
        repInsMean.push_back(vmean(insSamples));
    }

    r.lookupP50Us  = pctile(repLookupP50, 0.50);
    r.lookupP95Us  = pctile(repLookupP95, 0.50);
    r.lookupP99Us  = pctile(repLookupP99, 0.50);
    r.lookupMeanUs = vmean(repLookupMean);

    r.insertP50Us  = pctile(repInsP50, 0.50);
    r.insertP95Us  = pctile(repInsP95, 0.50);
    r.insertP99Us  = pctile(repInsP99, 0.50);
    r.insertMeanUs = vmean(repInsMean);

    r.lookupP95StddevUs  = vstddev(repLookupP95, r.lookupP95Us);
    r.lookupMeanStddevUs = vstddev(repLookupMean, r.lookupMeanUs);

    return r;
}

static CorpusBenchRow runV4(HiResTimer& timer, const std::string& corpusName,
                             const std::vector<Event>& events,
                             int numReps = 3) {
    CorpusBenchRow r;
    r.corpus = corpusName;
    r.impl = "SymTabV4";
    r.timingReps = numReps;

    {
        heap::Scope hs;
        budgetsym::v4::SymTabV4<> t(0);
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
                t.insert(ev.symbol);
            } else if (ev.kind == Event::USE) {
                uses++;
                uniqueSymbols.insert(ev.symbol);
                t.resolve(ev.symbol);
                t.recordAccess(ev.symbol);
            }
        }
        r.declarations = decls;
        r.uses = uses;
        r.uniqueNames = uniqueSymbols.size();
        r.measuredFinalHeapBytes = hs.bytes();
        r.measuredPeakHeapBytes  = hs.peakBytes();
        r.modeledFinalBytes = t.tracker().current();
        r.modeledPeakBytes  = t.tracker().peak();
        r.measuredBytesPerUniqueSymbol = r.uniqueNames > 0
            ? r.measuredFinalHeapBytes / static_cast<long long>(r.uniqueNames) : 0;
        r.promotions = t.promotions();
        r.demotions  = t.demotions();
        r.reconCount = t.reconstructionCount();
        r.reconSteps = t.reconstructionStepsTotal();
        r.reconDepth = r.reconCount > 0
            ? static_cast<double>(r.reconSteps) / static_cast<double>(r.reconCount) : 0.0;

        for (auto& sym : uniqueSymbols) {
            switch (t.representationOf(sym)) {
                case budgetsym::v4::Rep::INLINE_REP:     r.countInline++;    break;
                case budgetsym::v4::Rep::INTERNED_REP:   r.countInterned++;  break;
                case budgetsym::v4::Rep::COMPRESSED_REP: r.countCompressed++; break;
            }
        }
    }

    std::vector<double> repLookupP50, repLookupP95, repLookupP99, repLookupMean;
    std::vector<double> repInsP50, repInsP95, repInsP99, repInsMean;

    for (int rep = 0; rep < numReps; ++rep) {
        budgetsym::v4::SymTabV4<> t(0);
        std::vector<double> insSamples;
        std::vector<double> lookupSamples;

        for (auto& ev : events) {
            if (ev.kind == Event::ENTER_SCOPE) {
                t.enterScope();
            } else if (ev.kind == Event::EXIT_SCOPE) {
                t.exitScope();
            } else if (ev.kind == Event::DECLARE) {
                auto a = timer.now();
                volatile int id = t.insert(ev.symbol);
                auto b = timer.now();
                (void)id;
                insSamples.push_back(timer.microsecondsBetween(a, b));
            } else if (ev.kind == Event::USE) {
                auto a = timer.now();
                volatile int id = t.resolve(ev.symbol);
                auto b = timer.now();
                (void)id;
                lookupSamples.push_back(timer.microsecondsBetween(a, b));
                t.recordAccess(ev.symbol);
            }
        }

        repLookupP50.push_back(pctile(lookupSamples, 0.50));
        repLookupP95.push_back(pctile(lookupSamples, 0.95));
        repLookupP99.push_back(pctile(lookupSamples, 0.99));
        repLookupMean.push_back(vmean(lookupSamples));

        repInsP50.push_back(pctile(insSamples, 0.50));
        repInsP95.push_back(pctile(insSamples, 0.95));
        repInsP99.push_back(pctile(insSamples, 0.99));
        repInsMean.push_back(vmean(insSamples));
    }

    r.lookupP50Us  = pctile(repLookupP50, 0.50);
    r.lookupP95Us  = pctile(repLookupP95, 0.50);
    r.lookupP99Us  = pctile(repLookupP99, 0.50);
    r.lookupMeanUs = vmean(repLookupMean);

    r.insertP50Us  = pctile(repInsP50, 0.50);
    r.insertP95Us  = pctile(repInsP95, 0.50);
    r.insertP99Us  = pctile(repInsP99, 0.50);
    r.insertMeanUs = vmean(repInsMean);

    r.lookupP95StddevUs  = vstddev(repLookupP95, r.lookupP95Us);
    r.lookupMeanStddevUs = vstddev(repLookupMean, r.lookupMeanUs);

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
           "mean_reconstruction_depth,count_inline,count_interned,count_compressed,"
           "lookup_inline_count,lookup_interned_count,lookup_compressed_count,"
           "timing_reps,lookup_p95_stddev_us,lookup_mean_stddev_us\n";
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
        << r.countInline << "," << r.countInterned << "," << r.countCompressed << ","
        << r.lookupInlineCount << "," << r.lookupInternedCount << "," << r.lookupCompressedCount << ","
        << r.timingReps << "," << r.lookupP95StddevUs << "," << r.lookupMeanStddevUs << "\n";
}

int main(int argc, char** argv) {
    int defaultReps = 3;
    if (argc > 1) {
        defaultReps = std::atoi(argv[1]);
        if (defaultReps < 1) defaultReps = 1;
    }

    HiResTimer timer;

    std::ofstream out("data/real_world_benchmark.csv");
    if (!out) {
        std::cerr << "ERROR: cannot open data/real_world_benchmark.csv for writing\n";
        return 1;
    }
    writeHeader(out);

    std::vector<std::string> corpora = {"FreeRTOS", "Arduino", "Zephyr", "CPython", "Lua", "ESP-IDF", "protobuf-generated-cpp", "Clang", "Qt6", "Eigen",
                                         "cJSON", "curl", "FFmpeg", "LLVM", "mbedTLS", "Nginx", "protobuf-c", "QEMU", "Redis", "SQLite",
                                         "nanopb", "TinyUSB", "LVGL", "OpenThread", "MbedTLS2", "CMSIS"};

    std::cout << "=== Running Representative Real-World Corpus Benchmark (Reps: " << defaultReps << ") ===\n";

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

        // Adaptive reps for very large workloads vs micro workloads
        int reps = defaultReps;
        if (events.size() > 500000) reps = std::max(2, defaultReps - 1);
        if (events.size() < 20000)  reps = std::max(5, defaultReps + 2);

        auto convRow = runTable<ConventionalSymbolTable>(timer, corpus, "Conventional", events, reps);
        writeRow(out, convRow);

        auto intRow = runTable<InternedSymbolTable>(timer, corpus, "Interned", events, reps);
        writeRow(out, intRow);

        auto heapStrRow = runTable<ConventionalHeapStringSymbolTable>(timer, corpus, "Conventional-HeapString", events, reps);
        writeRow(out, heapStrRow);

        auto v1Row = runV1(timer, corpus, events, reps);
        writeRow(out, v1Row);

        auto v2Row = runV2(timer, corpus, events, reps);
        writeRow(out, v2Row);

        auto v3Row = runV3(timer, corpus, events, reps);
        writeRow(out, v3Row);

        auto v4Row = runV4(timer, corpus, events, reps);
        writeRow(out, v4Row);

        std::cout << "  Conventional: heap=" << convRow.measuredFinalHeapBytes << "B, lookup_p50=" << convRow.lookupP50Us << "us (p95=" << convRow.lookupP95Us << "us)\n";
        std::cout << "  SymTabV3    : heap=" << v3Row.measuredFinalHeapBytes << "B, lookup_p50=" << v3Row.lookupP50Us << "us (p95=" << v3Row.lookupP95Us << "us, recon=" << v3Row.reconCount << ")\n";
    }

    out.flush();
    std::cout << "\nWrote data/real_world_benchmark.csv\n";

    // Mirror to results/real_world_benchmark.csv
    std::ifstream src("data/real_world_benchmark.csv");
    std::ofstream dst("results/real_world_benchmark.csv");
    if (src && dst) dst << src.rdbuf();

    return 0;
}
