// Multi-Seed Repetition Harness for V4 Final Evaluation
//
// Evaluates N=30 seeds (1000..1029) across synthetic workloads A-F and real corpora
// comparing:
//   - Conventional
//   - Interned
//   - SymTabV3
//   - SymTabV4
//   - Conventional-HeapString
//
// Output: results/multiseed_v4_raw.csv

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

struct Event {
    enum Kind { ENTER_SCOPE, EXIT_SCOPE, DECLARE, USE } kind;
    std::string symbol;
    int depth = 0;
};

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

static std::vector<Event> buildSyntheticWorkload(const std::string& type, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<Event> events;
    events.push_back({Event::ENTER_SCOPE, "", 0});

    if (type == "ExpA_100pct_Inline") {
        for (size_t i = 0; i < 5000; i++) {
            std::string name = makeIdentifier(rng, 8, i);
            events.push_back({Event::DECLARE, name, 1});
            events.push_back({Event::USE, name, 1});
        }
    } else if (type == "ExpB_90_10_Mix") {
        for (size_t i = 0; i < 5000; i++) {
            size_t len = (i % 10 == 0) ? 24 : 8;
            std::string name = makeIdentifier(rng, len, i);
            events.push_back({Event::DECLARE, name, 1});
            events.push_back({Event::USE, name, 1});
        }
    } else if (type == "ExpC_70_30_Mix") {
        for (size_t i = 0; i < 5000; i++) {
            size_t len = (i % 10 < 3) ? 24 : 8;
            std::string name = makeIdentifier(rng, len, i);
            events.push_back({Event::DECLARE, name, 1});
            events.push_back({Event::USE, name, 1});
        }
    } else if (type == "ExpD_Length32_k4") {
        std::vector<std::string> names;
        for (size_t i = 0; i < 1000; i++) names.push_back(makeIdentifier(rng, 32, i));
        for (size_t dup = 0; dup < 4; dup++) {
            events.push_back({Event::ENTER_SCOPE, "", static_cast<int>(dup + 1)});
            for (auto& name : names) {
                events.push_back({Event::DECLARE, name, static_cast<int>(dup + 1)});
                events.push_back({Event::USE, name, static_cast<int>(dup + 1)});
            }
        }
        for (size_t dup = 0; dup < 4; dup++) events.push_back({Event::EXIT_SCOPE, "", 0});
    } else if (type == "ExpE_ScopeChurn") {
        size_t idNum = 0;
        for (size_t s = 0; s < 30; s++) {
            events.push_back({Event::ENTER_SCOPE, "", 1});
            for (size_t i = 0; i < 100; i++) {
                std::string name = makeIdentifier(rng, 36, idNum++);
                events.push_back({Event::DECLARE, name, 1});
                events.push_back({Event::USE, name, 1});
            }
            if (s % 2 == 1) events.push_back({Event::EXIT_SCOPE, "", 0});
        }
    }
    events.push_back({Event::EXIT_SCOPE, "", 0});
    return events;
}

template<typename Table>
static void evalEvents(HiResTimer& timer, Table& t, const std::vector<Event>& events,
                       long long& heapBytes, double& coldLookupP50Us, double& coldLookupP95Us) {
    heap::Scope hs;
    std::vector<double> lookups;

    for (auto& ev : events) {
        if (ev.kind == Event::ENTER_SCOPE) {
            t.enterScope();
        } else if (ev.kind == Event::EXIT_SCOPE) {
            t.exitScope();
        } else if (ev.kind == Event::DECLARE) {
            t.insert(ev.symbol);
        } else if (ev.kind == Event::USE) {
            auto a = timer.now();
            volatile int id = t.resolve(ev.symbol);
            auto b = timer.now();
            (void)id;
            lookups.push_back(timer.microsecondsBetween(a, b));
            t.recordAccess(ev.symbol);
        }
    }

    heapBytes = hs.bytes();
    std::sort(lookups.begin(), lookups.end());
    if (!lookups.empty()) {
        coldLookupP50Us = lookups[lookups.size() * 50 / 100];
        coldLookupP95Us = lookups[lookups.size() * 95 / 100];
    } else {
        coldLookupP50Us = 0.0;
        coldLookupP95Us = 0.0;
    }
}

int main() {
    HiResTimer timer;
    const int kSeeds = 30;
    const size_t seedBase = 1000;

    std::ofstream out("results/multiseed_v4_raw.csv");
    if (!out) {
        std::cerr << "ERROR: cannot write results/multiseed_v4_raw.csv\n";
        return 1;
    }

    out << "workload,seed,implementation,measured_final_heap_bytes,cold_lookup_p50_us,cold_lookup_p95_us\n";

    std::vector<std::string> syntheticWorkloads = {
        "ExpA_100pct_Inline", "ExpB_90_10_Mix", "ExpC_70_30_Mix", "ExpD_Length32_k4", "ExpE_ScopeChurn"
    };

    std::cout << "=== Running Multi-Seed Evaluation (N=30 seeds x 5 synthetic workloads) ===\n";

    for (auto& wlName : syntheticWorkloads) {
        std::cout << "Running workload " << wlName << " across 30 seeds ... " << std::flush;
        for (int s = 0; s < kSeeds; s++) {
            unsigned seed = seedBase + s;
            auto events = buildSyntheticWorkload(wlName, seed);

            long long heapB = 0;
            double p50 = 0, p95 = 0;

            // 1. Conventional
            { ConventionalSymbolTable t(0); evalEvents(timer, t, events, heapB, p50, p95); }
            out << wlName << "," << seed << ",Conventional," << heapB << "," << p50 << "," << p95 << "\n";

            // 2. Interned
            { InternedSymbolTable t(0); evalEvents(timer, t, events, heapB, p50, p95); }
            out << wlName << "," << seed << ",Interned," << heapB << "," << p50 << "," << p95 << "\n";

            // 3. Conventional-HeapString
            { ConventionalHeapStringSymbolTable t(0); evalEvents(timer, t, events, heapB, p50, p95); }
            out << wlName << "," << seed << ",Conventional-HeapString," << heapB << "," << p50 << "," << p95 << "\n";

            // 4. SymTabV3
            { budgetsym::v3::SymTabV3<> t(0); evalEvents(timer, t, events, heapB, p50, p95); }
            out << wlName << "," << seed << ",SymTabV3," << heapB << "," << p50 << "," << p95 << "\n";

            // 5. SymTabV4
            { budgetsym::v4::SymTabV4<> t(0); evalEvents(timer, t, events, heapB, p50, p95); }
            out << wlName << "," << seed << ",SymTabV4," << heapB << "," << p50 << "," << p95 << "\n";
        }
        std::cout << "done\n";
    }

    out.flush();
    std::cout << "\nWrote results/multiseed_v4_raw.csv\n";
    return 0;
}
