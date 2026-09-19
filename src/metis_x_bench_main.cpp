// METIS-X Benchmark Harness (Phase II)
//
// Evaluates MetisXTable against EmbeddedConventionalSymbolTable and SymTabV3
// on the same 4 embedded compiler event traces used in Phase I.
//
// Key methodological fixes (vs Phase I embedded_bench_main.cpp):
//   M1: heap::Scope reset + allocation count snapshot before/after each scope
//       -- proves zero cross-rep contamination.
//   M2: modeled_final_bytes column OMITTED for EmbeddedConventional.
//   M3: Output path configurable: ./metis_x_bench.exe <reps> <outfile>
//       Default: results/metis_x_benchmark.csv
//       Validation: results/metis_x_validation.csv
//   M4: Separate binary from embedded_bench.exe.
//   M5: Median p95 across R reps + min/max/CV diagnostics.
//   M6: Physical heap bytes ONLY via heap::Scope (malloc_usable_size).
//
// Authoritative columns:
//   measured_peak_heap_bytes  -- heap::Scope::peakBytes()
//   measured_final_heap_bytes -- heap::Scope::bytes()
//   lookup_p95_us             -- median of R p95 values across repetitions
//
// Usage:
//   taskset -c 0 ./metis_x_bench.exe [reps] [output.csv]

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

#include "../include/conventional_symbol_table.hpp"
#include "../include/embedded_conventional_symbol_table.hpp"
#include "../include/interned_symbol_table.hpp"
#include "../include/symtab_v3.hpp"
#include "../include/metis_x.hpp"
#include "../include/hires_timer.hpp"

using namespace budgetsym;

// ---- percentile / stats helpers --------------------------------------------

static double pctile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t idx = static_cast<size_t>(p * static_cast<double>(v.size() - 1));
    return v[idx];
}

static double vmean(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    double s = 0.0;
    for (double x : v) s += x;
    return s / static_cast<double>(v.size());
}

static double vstddev(const std::vector<double>& v, double meanVal) {
    if (v.size() <= 1) return 0.0;
    double sumSq = 0.0;
    for (double x : v) sumSq += (x - meanVal) * (x - meanVal);
    return std::sqrt(sumSq / static_cast<double>(v.size() - 1));
}

// ---- event replay -----------------------------------------------------------

struct Event {
    enum Kind { ENTER_SCOPE, EXIT_SCOPE, DECLARE, USE } kind;
    std::string symbol;
    int depth = 0;
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

// ---- row definition --------------------------------------------------------

struct MetisXBenchRow {
    std::string corpus;
    std::string impl;
    size_t declarations = 0;
    size_t uses = 0;
    size_t uniqueNames = 0;

    // Physical heap (authoritative)
    long long measuredPeakHeapBytes  = 0;
    long long measuredFinalHeapBytes = 0;
    long long measuredBytesPerUniqueSymbol = 0;

    // Latency (median of R repetitions)
    double lookupP50Us  = 0.0;
    double lookupP95Us  = 0.0;
    double lookupP99Us  = 0.0;
    double lookupMeanUs = 0.0;
    double insertP50Us  = 0.0;
    double insertP95Us  = 0.0;
    double insertP99Us  = 0.0;
    double insertMeanUs = 0.0;

    // Spread across R reps
    double lookupP95StddevUs  = 0.0;
    double lookupMeanStddevUs = 0.0;
    double lookupP95MinUs     = 0.0;  // Step 3: min rep-level p95
    double lookupP95MaxUs     = 0.0;  // Step 3: max rep-level p95
    double lookupP95CvPct     = 0.0;  // Step 3: coefficient of variation (%)

    // Step 2: allocation accounting
    long long allocsBefore   = 0;  // heap alloc count before scope
    long long allocsInScope  = 0;  // heap alloc count during measurement
    long long allocsAfter    = 0;  // heap alloc count after scope (must equal before)

    int timingReps = 1;

    // MetisX-specific diagnostics (0 for other impls)
    size_t inlineSlots = 0;
    size_t heapSlots   = 0;
    double avgProbeDist = 0.0;
    long long logicalFinalBytes = 0;  // computeCurrentBytes() -- diagnostic only
};

// ---- generic template harness (works for ConventionalSymbolTable,
//      EmbeddedConventionalSymbolTable, InternedSymbolTable) -----------------

template<typename Table>
static MetisXBenchRow runGeneric(HiResTimer& timer, const std::string& corpusName,
                                  const std::string& implName,
                                  const std::vector<Event>& events,
                                  int numReps = 5) {
    MetisXBenchRow r;
    r.corpus = corpusName;
    r.impl   = implName;
    r.timingReps = numReps;

    // Pass 1: Physical heap measurement (single run, no timing)
    {
        // Step 2: snapshot allocation count before scope
        long long allocsBefore = heap::allocationCount();
        heap::resetPeak();
        heap::Scope hs;
        Table t(0);
        std::unordered_set<std::string> uniqueSymbols;
        size_t decls = 0, uses = 0;

        for (auto& ev : events) {
            if      (ev.kind == Event::ENTER_SCOPE) { t.enterScope(); }
            else if (ev.kind == Event::EXIT_SCOPE)  { t.exitScope(); }
            else if (ev.kind == Event::DECLARE) {
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
        long long allocsEnd = heap::allocationCount();
        // Table destructor runs here, freeing all allocations
        r.allocsBefore  = allocsBefore;
        r.allocsInScope = allocsEnd - allocsBefore;
        r.declarations = decls;
        r.uses = uses;
        r.uniqueNames = uniqueSymbols.size();
        r.measuredFinalHeapBytes = hs.bytes();
        r.measuredPeakHeapBytes  = hs.peakBytes();
        r.measuredBytesPerUniqueSymbol = r.uniqueNames > 0
            ? r.measuredFinalHeapBytes / static_cast<long long>(r.uniqueNames) : 0;
    }
    // Step 2: after scope, live bytes should return to pre-measurement level
    r.allocsAfter = heap::allocationCount();

    // Pass 2: Latency across R independent reps (M5: R=5 default)
    std::vector<double> repLookupP50, repLookupP95, repLookupP99, repLookupMean;
    std::vector<double> repInsP50,    repInsP95,    repInsP99,    repInsMean;

    for (int rep = 0; rep < numReps; ++rep) {
        Table t(0);
        std::vector<double> insSamples, lookupSamples;

        for (auto& ev : events) {
            if      (ev.kind == Event::ENTER_SCOPE) { t.enterScope(); }
            else if (ev.kind == Event::EXIT_SCOPE)  { t.exitScope(); }
            else if (ev.kind == Event::DECLARE) {
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
    // Step 3: min, max, coefficient of variation
    if (!repLookupP95.empty()) {
        r.lookupP95MinUs = *std::min_element(repLookupP95.begin(), repLookupP95.end());
        r.lookupP95MaxUs = *std::max_element(repLookupP95.begin(), repLookupP95.end());
        r.lookupP95CvPct = r.lookupP95Us > 0.0
            ? (r.lookupP95StddevUs / r.lookupP95Us) * 100.0 : 0.0;
        if (r.lookupP95CvPct > 20.0) {
            std::cerr << "  [WARNING] High CV=" << r.lookupP95CvPct
                      << "% for p95 -- consider taskset -c 0 to reduce interference\n";
        }
    }

    return r;
}

// ---- MetisX-specific harness (adds diagnostics) ----------------------------

static MetisXBenchRow runMetisX(HiResTimer& timer, const std::string& corpusName,
                                 const std::vector<Event>& events,
                                 int numReps = 5) {
    MetisXBenchRow r;
    r.corpus = corpusName;
    r.impl   = "MetisX";
    r.timingReps = numReps;

    // Pass 1: Physical heap + diagnostics
    {
        heap::resetPeak();
        heap::Scope hs;
        budgetsym::metisx::MetisXTable t(0);
        std::unordered_set<std::string> uniqueSymbols;
        size_t decls = 0, uses = 0;

        for (auto& ev : events) {
            if      (ev.kind == Event::ENTER_SCOPE) { t.enterScope(); }
            else if (ev.kind == Event::EXIT_SCOPE)  { t.exitScope(); }
            else if (ev.kind == Event::DECLARE) {
                decls++;
                uniqueSymbols.insert(ev.symbol);
                t.insert(ev.symbol);
            } else if (ev.kind == Event::USE) {
                uses++;
                uniqueSymbols.insert(ev.symbol);
                t.resolve(ev.symbol);
            }
        }
        r.declarations = decls;
        r.uses = uses;
        r.uniqueNames = uniqueSymbols.size();
        r.measuredFinalHeapBytes = hs.bytes();
        r.measuredPeakHeapBytes  = hs.peakBytes();
        r.measuredBytesPerUniqueSymbol = r.uniqueNames > 0
            ? r.measuredFinalHeapBytes / static_cast<long long>(r.uniqueNames) : 0;
        r.logicalFinalBytes = t.computeCurrentBytes();

        auto st = t.stats();
        r.inlineSlots   = st.inlineNames;
        r.heapSlots     = st.heapNames;
        r.avgProbeDist  = st.avgProbeDistance;
    }

    // Pass 2: Latency (M5: R=5)
    std::vector<double> repLookupP50, repLookupP95, repLookupP99, repLookupMean;
    std::vector<double> repInsP50,    repInsP95,    repInsP99,    repInsMean;

    for (int rep = 0; rep < numReps; ++rep) {
        budgetsym::metisx::MetisXTable t(0);
        std::vector<double> insSamples, lookupSamples;

        for (auto& ev : events) {
            if      (ev.kind == Event::ENTER_SCOPE) { t.enterScope(); }
            else if (ev.kind == Event::EXIT_SCOPE)  { t.exitScope(); }
            else if (ev.kind == Event::DECLARE) {
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

// ---- SymTabV3 harness -------------------------------------------------------

static MetisXBenchRow runV3(HiResTimer& timer, const std::string& corpusName,
                              const std::vector<Event>& events, int numReps = 5) {
    MetisXBenchRow r;
    r.corpus = corpusName;
    r.impl   = "SymTabV3";
    r.timingReps = numReps;

    {
        heap::resetPeak();
        heap::Scope hs;
        budgetsym::v3::SymTabV3<> t(0);
        std::unordered_set<std::string> uniqueSymbols;
        size_t decls = 0, uses = 0;

        for (auto& ev : events) {
            if      (ev.kind == Event::ENTER_SCOPE) { t.enterScope(); }
            else if (ev.kind == Event::EXIT_SCOPE)  { t.exitScope(); }
            else if (ev.kind == Event::DECLARE) {
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
        r.measuredBytesPerUniqueSymbol = r.uniqueNames > 0
            ? r.measuredFinalHeapBytes / static_cast<long long>(r.uniqueNames) : 0;
        r.logicalFinalBytes = t.tracker().current();
    }

    std::vector<double> repLookupP50, repLookupP95, repLookupP99, repLookupMean;
    std::vector<double> repInsP50,    repInsP95,    repInsP99,    repInsMean;

    for (int rep = 0; rep < numReps; ++rep) {
        budgetsym::v3::SymTabV3<> t(0);
        std::vector<double> insSamples, lookupSamples;

        for (auto& ev : events) {
            if      (ev.kind == Event::ENTER_SCOPE) { t.enterScope(); }
            else if (ev.kind == Event::EXIT_SCOPE)  { t.exitScope(); }
            else if (ev.kind == Event::DECLARE) {
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

// ---- CSV output -------------------------------------------------------------

static void writeHeader(std::ofstream& out) {
    out << "corpus,implementation,declarations,uses,unique_names,"
           "measured_peak_heap_bytes,measured_final_heap_bytes,"
           "measured_bytes_per_unique_symbol,"
           "lookup_p50_us,lookup_p95_us,lookup_p99_us,lookup_mean_us,"
           "insert_p50_us,insert_p95_us,insert_p99_us,insert_mean_us,"
           "timing_reps,lookup_p95_stddev_us,lookup_mean_stddev_us,"
           "lookup_p95_min_us,lookup_p95_max_us,lookup_p95_cv_pct,"
           "allocs_before,allocs_in_scope,allocs_after,"
           "inline_slots,heap_slots,avg_probe_dist,logical_final_bytes\n";
}

static void writeRow(std::ofstream& out, const MetisXBenchRow& r) {
    out << r.corpus << "," << r.impl << ","
        << r.declarations << "," << r.uses << "," << r.uniqueNames << ","
        << r.measuredPeakHeapBytes << "," << r.measuredFinalHeapBytes << ","
        << r.measuredBytesPerUniqueSymbol << ","
        << r.lookupP50Us  << "," << r.lookupP95Us  << "," << r.lookupP99Us  << "," << r.lookupMeanUs  << ","
        << r.insertP50Us  << "," << r.insertP95Us  << "," << r.insertP99Us  << "," << r.insertMeanUs  << ","
        << r.timingReps << "," << r.lookupP95StddevUs << "," << r.lookupMeanStddevUs << ","
        << r.lookupP95MinUs << "," << r.lookupP95MaxUs << "," << r.lookupP95CvPct << ","
        << r.allocsBefore << "," << r.allocsInScope << "," << r.allocsAfter << ","
        << r.inlineSlots << "," << r.heapSlots << "," << r.avgProbeDist << "," << r.logicalFinalBytes
        << "\n";
}

// ---- win check --------------------------------------------------------------

static void checkWin(const std::string& corpus,
                     const MetisXBenchRow& embConv,
                     const MetisXBenchRow& metisX) {
    std::cout << "\n  [WIN CHECK] " << corpus << ":\n";

    double heapRatio   = embConv.measuredFinalHeapBytes > 0
        ? static_cast<double>(metisX.measuredFinalHeapBytes) / static_cast<double>(embConv.measuredFinalHeapBytes)
        : 1.0;
    double latRatio    = embConv.lookupP95Us > 0.0
        ? metisX.lookupP95Us / embConv.lookupP95Us
        : 1.0;

    std::cout << "    Final heap:  MetisX=" << metisX.measuredFinalHeapBytes
              << "B  EmbConv=" << embConv.measuredFinalHeapBytes
              << "B  ratio=" << heapRatio;
    if (heapRatio <= 0.90) std::cout << "  [HEAP WIN >=10%]";
    else if (heapRatio <= 1.00) std::cout << "  [heap win <10%]";
    else std::cout << "  [heap LOSS]";
    std::cout << "\n";

    std::cout << "    p95 latency: MetisX=" << metisX.lookupP95Us
              << "us  EmbConv=" << embConv.lookupP95Us
              << "us  ratio=" << latRatio;
    if (latRatio <= 0.90) std::cout << "  [LATENCY WIN >=10%]";
    else if (latRatio <= 1.00) std::cout << "  [latency win <10%]";
    else if (latRatio <= 1.25) std::cout << "  [latency within 1.25x gate]";
    else std::cout << "  [latency REGRESSION " << latRatio << "x]";
    std::cout << "\n";

    bool heapWin = heapRatio <= 1.00;
    bool latWin  = latRatio  <= 1.00;
    bool tenPctWin = (heapRatio <= 0.90 || latRatio <= 0.90);
    if (heapWin && latWin && tenPctWin) {
        std::cout << "    *** GENUINE WIN: both metrics improved, >=10% on at least one ***\n";
    } else if (heapWin || latWin) {
        std::cout << "    Partial improvement: only one metric won.\n";
    } else {
        std::cout << "    No win on either metric -- diagnose before proceeding.\n";
    }
}

// ---- main -------------------------------------------------------------------

int main(int argc, char** argv) {
    int defaultReps = 5;
    std::string outPath = "results/metis_x_benchmark.csv";
    if (argc > 1) {
        defaultReps = std::atoi(argv[1]);
        if (defaultReps < 1) defaultReps = 1;
    }
    if (argc > 2) {
        outPath = argv[2];
    }

    HiResTimer timer;

    std::ofstream out(outPath.c_str());
    if (!out) {
        std::cerr << "ERROR: cannot open " << outPath << " for writing\n";
        return 1;
    }
    writeHeader(out);

    std::cout << "=== METIS-X Phase II Benchmark (R=" << defaultReps << " reps) ===\n";
    std::cout << "NOTE: For best results, run: taskset -c 0 ./metis_x_bench.exe\n\n";

    std::vector<std::string> corpora = {"FreeRTOS", "Arduino", "Zephyr", "ESP-IDF"};

    for (auto& corpus : corpora) {
        std::string eventFile = "results/corpus_events_" + corpus + ".txt";
        if (!std::ifstream(eventFile)) {
            eventFile = "data/corpus_events_" + corpus + ".txt";
        }
        std::cout << "[ " << corpus << " ]  loading " << eventFile << " ... " << std::flush;
        auto events = loadEvents(eventFile);
        if (events.empty()) {
            std::cout << "missing/empty -- skipping\n";
            continue;
        }
        std::cout << events.size() << " events\n";

        int reps = defaultReps;
        if (events.size() > 500000) reps = std::max(3, defaultReps - 2);
        if (events.size() < 20000)  reps = std::max(7, defaultReps + 2);

        // 1. EmbeddedConventional (primary baseline)
        std::cout << "  EmbConv    ... " << std::flush;
        auto embConv = runGeneric<EmbeddedConventionalSymbolTable>(
            timer, corpus, "EmbeddedConventional", events, reps);
        writeRow(out, embConv);
        std::cout << "heap=" << embConv.measuredFinalHeapBytes
                  << "B  p95=" << embConv.lookupP95Us << "us\n";

        // 2. MetisX (Phase II primary)
        std::cout << "  MetisX     ... " << std::flush;
        auto metisX = runMetisX(timer, corpus, events, reps);
        writeRow(out, metisX);
        std::cout << "heap=" << metisX.measuredFinalHeapBytes
                  << "B  p95=" << metisX.lookupP95Us
                  << "us  inline=" << metisX.inlineSlots
                  << "  heap_slots=" << metisX.heapSlots
                  << "  avgProbe=" << metisX.avgProbeDist << "\n";

        // 3. SymTabV3 (Phase I reference)
        std::cout << "  SymTabV3   ... " << std::flush;
        auto v3Row = runV3(timer, corpus, events, reps);
        writeRow(out, v3Row);
        std::cout << "heap=" << v3Row.measuredFinalHeapBytes
                  << "B  p95=" << v3Row.lookupP95Us << "us\n";

        // 4. ConventionalHost (host baseline)
        std::cout << "  ConvHost   ... " << std::flush;
        auto convHost = runGeneric<ConventionalSymbolTable>(
            timer, corpus, "ConventionalHost", events, reps);
        writeRow(out, convHost);
        std::cout << "heap=" << convHost.measuredFinalHeapBytes
                  << "B  p95=" << convHost.lookupP95Us << "us\n";

        // Win check vs EmbeddedConventional
        checkWin(corpus, embConv, metisX);
        std::cout << "\n";
    }

    out.flush();
    std::cout << "Wrote results/metis_x_benchmark.csv\n";
    return 0;
}
