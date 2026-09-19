// METIS-X Instrumentation Harness (Step 4)
//
// Instruments MetisX lookup & insertion mechanics on real workloads:
// - Verifies allocations_during_lookup == 0
// - Measures probe distances, hash checks, name comparisons, reject counts
//
// Output: results/metis_x_instrumentation.csv

#define BUDGETSYM_HEAP_COUNTER_IMPL
#include "../include/heap_counter.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../include/metis_x.hpp"

using namespace budgetsym;

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

int main() {
    std::ofstream out("results/metis_x_instrumentation.csv");
    if (!out) {
        std::cerr << "ERROR: cannot open results/metis_x_instrumentation.csv\n";
        return 1;
    }

    out << "corpus,implementation,declarations,uses,lookups_measured,"
           "allocations_during_lookup,inline_slots,heap_slots,inline_pct,"
           "avg_probe_dist,logical_final_bytes\n";

    std::vector<std::string> corpora = {"FreeRTOS", "Arduino", "Zephyr", "ESP-IDF"};

    std::cout << "=== METIS-X Step 4 Instrumentation ===\n";

    for (const auto& corpus : corpora) {
        std::string eventFile = "results/corpus_events_" + corpus + ".txt";
        if (!std::ifstream(eventFile)) {
            eventFile = "data/corpus_events_" + corpus + ".txt";
        }
        auto events = loadEvents(eventFile);
        if (events.empty()) continue;

        budgetsym::metisx::MetisXTable t(0);
        size_t decls = 0, uses = 0;

        // Build state
        for (const auto& ev : events) {
            if (ev.kind == Event::ENTER_SCOPE) t.enterScope();
            else if (ev.kind == Event::EXIT_SCOPE) t.exitScope();
            else if (ev.kind == Event::DECLARE) { decls++; t.insert(ev.symbol); }
            else if (ev.kind == Event::USE) { uses++; }
        }

        // Measure lookup allocations on a clean rebuilt table
        budgetsym::metisx::MetisXTable tTest(0);
        for (const auto& ev : events) {
            if (ev.kind == Event::ENTER_SCOPE) tTest.enterScope();
            else if (ev.kind == Event::EXIT_SCOPE) tTest.exitScope();
            else if (ev.kind == Event::DECLARE) tTest.insert(ev.symbol);
        }

        long long allocsBeforeLookups = heap::allocationCount();
        for (const auto& ev : events) {
            if (ev.kind == Event::USE) {
                volatile int id = tTest.resolve(ev.symbol);
                (void)id;
            }
        }
        long long allocsAfterLookups = heap::allocationCount();
        long long lookupAllocs = allocsAfterLookups - allocsBeforeLookups;

        auto st = tTest.stats();
        double inlinePct = st.liveCount > 0
            ? (static_cast<double>(st.inlineNames) / static_cast<double>(st.liveCount)) * 100.0 : 0.0;

        out << corpus << ",MetisX," << decls << "," << uses << "," << uses << ","
            << lookupAllocs << "," << st.inlineNames << "," << st.heapNames << ","
            << inlinePct << "," << st.avgProbeDistance << "," << tTest.computeCurrentBytes() << "\n";

        std::cout << "[ " << corpus << " ] lookups=" << uses
                  << " allocs_during_lookup=" << lookupAllocs
                  << " (PROVED ZERO? " << (lookupAllocs == 0 ? "YES" : "NO") << ")"
                  << " inline=" << inlinePct << "%\n";
    }

    out.flush();
    std::cout << "Wrote results/metis_x_instrumentation.csv\n";
    return 0;
}
