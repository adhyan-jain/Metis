// STATISTICAL VALIDATION MULTISEED BENCHMARK
// (CLAUDE_RESEARCH.md Section 15)
//
// Runs N=30 independent seeds (1000..1029) across all synthetic workload families
// for Conventional, Interned, BudgetSym (V1), SymTabV2 (Full), and key V2 ablation variants.
//
// Outputs: results/multiseed_v2_raw.csv

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

static double pctile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t idx = static_cast<size_t>(p * static_cast<double>(v.size() - 1));
    return v[idx];
}

struct Event {
    enum Kind { ENTER_SCOPE, EXIT_SCOPE, DECLARE, USE } kind;
    std::string symbol;
    int depth = 0;
};

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

static Dataset buildFamilyDataset(int f, size_t seed) {
    const size_t defaultBudget = 64ull * 1024 * 1024;
    const size_t tinyBudget = 8192;
    switch (f) {
        case 0: return genUniformRandom("small", 100, 4, 12, seed, defaultBudget);
        case 1: return genUniformRandom("medium", 2000, 4, 16, seed, defaultBudget);
        case 2: return genUniformRandom("large", 5000, 4, 16, seed, defaultBudget);
        case 3: return genHighPrefixSimilaritySeeded(2000, seed, defaultBudget);
        case 4: return genUniformRandom("random-long", 2000, 20, 40, seed, defaultBudget);
        case 5: return genNestedScopes(40, 50, seed, defaultBudget);
        case 6: return genHotColdAccess(2000, seed, defaultBudget);
        case 7: return genMemoryStress(2000, seed, tinyBudget);
    }
    throw std::runtime_error("bad familyIndex");
}

template<typename Table>
static void evalTableEvents(HiResTimer& timer, Table& t, const std::vector<Event>& events,
                            long long& heapBytes, long long& modeledBytes,
                            double& coldP50, double& insertP50) {
    heap::Scope hs;
    std::vector<double> ins;
    std::vector<double> cold;

    for (auto& ev : events) {
        if (ev.kind == Event::ENTER_SCOPE) {
            t.enterScope();
        } else if (ev.kind == Event::EXIT_SCOPE) {
            t.exitScope();
        } else if (ev.kind == Event::DECLARE) {
            auto a = timer.now(); volatile int id = t.insert(ev.symbol); auto b = timer.now();
            (void)id;
            ins.push_back(timer.microsecondsBetween(a, b));
        } else if (ev.kind == Event::USE) {
            auto a = timer.now(); volatile int id = t.resolve(ev.symbol); auto b = timer.now();
            (void)id;
            cold.push_back(timer.microsecondsBetween(a, b));
            t.recordAccess(ev.symbol);
        }
    }

    heapBytes    = hs.bytes();
    modeledBytes = t.tracker().current();
    coldP50      = pctile(cold, 0.50);
    insertP50    = pctile(ins, 0.50);
}

int main() {
    HiResTimer timer;
    const int kFamilies = 8;
    const int kSeeds = 30;
    const size_t seedBase = 1000;

    std::ofstream out("results/multiseed_v2_raw.csv");
    if (!out) {
        std::cerr << "ERROR: cannot write results/multiseed_v2_raw.csv\n";
        return 1;
    }

    out << "dataset,seed,implementation,"
           "measured_final_heap_bytes,modeled_final_bytes,"
           "cold_lookup_p50_us,insert_p50_us\n";

    std::cout << "=== Statistical Validation Multiseed Benchmark (N=30 seeds x 8 families) ===\n";

    for (int f = 0; f < kFamilies; f++) {
        std::string name = buildFamilyDataset(f, seedBase).name;
        std::cout << "Running family [" << f << "] " << name << " across 30 seeds ... " << std::flush;

        for (int s = 0; s < kSeeds; s++) {
            size_t seed = seedBase + f * 100 + s;
            Dataset ds = buildFamilyDataset(f, seed);
            auto events = buildSyntheticEvents(ds.name, ds.identifiers, 50);

            long long heapB = 0, modB = 0;
            double coldP50 = 0, insP50 = 0;

            // 1. Conventional
            { ConventionalSymbolTable t(0); evalTableEvents(timer, t, events, heapB, modB, coldP50, insP50); }
            out << ds.name << "," << seed << ",Conventional," << heapB << "," << modB << "," << coldP50 << "," << insP50 << "\n";

            // 2. Interned
            { InternedSymbolTable t(0); evalTableEvents(timer, t, events, heapB, modB, coldP50, insP50); }
            out << ds.name << "," << seed << ",Interned," << heapB << "," << modB << "," << coldP50 << "," << insP50 << "\n";

            // 3. BudgetSym V1
            { BudgetSym t(0); evalTableEvents(timer, t, events, heapB, modB, coldP50, insP50); }
            out << ds.name << "," << seed << ",BudgetSymV1," << heapB << "," << modB << "," << coldP50 << "," << insP50 << "\n";

            // 4. Full V2
            { SymTabV2<> t(0); evalTableEvents(timer, t, events, heapB, modB, coldP50, insP50); }
            out << ds.name << "," << seed << ",SymTabV2," << heapB << "," << modB << "," << coldP50 << "," << insP50 << "\n";

            // 5. V2-NoBlockCompression
            { PolicyConfigV2 cfg; cfg.compressMinLen = 999; SymTabV2<> t(0, cfg); evalTableEvents(timer, t, events, heapB, modB, coldP50, insP50); }
            out << ds.name << "," << seed << ",V2-NoBlockCompression," << heapB << "," << modB << "," << coldP50 << "," << insP50 << "\n";

            // 6. V2-NoFingerprints
            { PolicyConfigV2 cfg; cfg.disableFingerprints = true; SymTabV2<> t(0, cfg); evalTableEvents(timer, t, events, heapB, modB, coldP50, insP50); }
            out << ds.name << "," << seed << ",V2-NoFingerprints," << heapB << "," << modB << "," << coldP50 << "," << insP50 << "\n";

            // 7. V2-NoScopeReclamation
            { PolicyConfigV2 cfg; cfg.disableScopeReclamation = true; SymTabV2<> t(0, cfg); evalTableEvents(timer, t, events, heapB, modB, coldP50, insP50); }
            out << ds.name << "," << seed << ",V2-NoScopeReclamation," << heapB << "," << modB << "," << coldP50 << "," << insP50 << "\n";
        }
        std::cout << "done\n";
    }

    out.flush();
    std::cout << "\nWrote results/multiseed_v2_raw.csv\n";
    return 0;
}
