// METIS-X Step 12: Failure Cases Harness
//
// Evaluates MetisX on specific synthetic stress tests to identify boundary conditions:
//   - very_short_names (1-3B)
//   - very_long_names (20-50B)
//   - high_uniqueness (10,000 unique names, 1 decl each)
//   - high_reuse (10 unique names, 10,000 uses each)
//   - high_miss_rate (50% lookups miss)
//   - high_scope_depth (depth=50)
//   - heavy_shadowing (same names in all scopes)
//   - high_scope_churn (10,000 enter/exit scope cycles)
//
// Output: results/metis_x_failure_cases.csv

#define BUDGETSYM_HEAP_COUNTER_IMPL
#include "../include/heap_counter.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "../include/metis_x.hpp"
#include "../include/hires_timer.hpp"

using namespace budgetsym;

static std::string randStr(std::mt19937& rng, size_t minL, size_t maxL) {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
    std::uniform_int_distribution<size_t> lenDist(minL, maxL);
    std::uniform_int_distribution<size_t> charDist(0, sizeof(chars) - 2);
    size_t len = lenDist(rng);
    std::string s; s.reserve(len);
    for (size_t i = 0; i < len; ++i) s += chars[charDist(rng)];
    return s;
}

struct FailureRow {
    std::string testCase;
    size_t decls;
    size_t uses;
    long long finalHeapBytes;
    double lookupP95Us;
    size_t inlineSlots;
    size_t heapSlots;
    double inlinePct;
};

void runFailureCase(std::ofstream& out, const std::string& caseName,
                    const std::vector<std::pair<std::string, int>>& ops, HiResTimer& timer) {
    // ops: 0=enterScope, 1=exitScope, 2=insert, 3=lookup
    heap::resetPeak();
    heap::Scope hs;
    metisx::MetisXTable t(0);
    size_t decls = 0, uses = 0;

    for (const auto& op : ops) {
        if (op.second == 0) t.enterScope();
        else if (op.second == 1) t.exitScope();
        else if (op.second == 2) { decls++; t.insert(op.first); }
        else if (op.second == 3) { uses++; t.resolve(op.first); }
    }
    long long finalHeap = hs.bytes();

    // Timing rep (3 reps)
    std::vector<double> p95s;
    for (int rep = 0; rep < 3; ++rep) {
        metisx::MetisXTable tT(0);
        std::vector<double> samples;
        for (const auto& op : ops) {
            if (op.second == 0) tT.enterScope();
            else if (op.second == 1) tT.exitScope();
            else if (op.second == 2) tT.insert(op.first);
            else if (op.second == 3) {
                auto a = timer.now();
                volatile int id = tT.resolve(op.first);
                auto b = timer.now(); (void)id;
                samples.push_back(timer.microsecondsBetween(a, b));
            }
        }
        if (!samples.empty()) {
            std::sort(samples.begin(), samples.end());
            p95s.push_back(samples[static_cast<size_t>(0.95 * (samples.size() - 1))]);
        } else {
            p95s.push_back(0.0);
        }
    }
    std::sort(p95s.begin(), p95s.end());
    double p95Us = p95s.empty() ? 0.0 : p95s[p95s.size() / 2];

    auto st = t.stats();
    double inlinePct = st.liveCount > 0
        ? (static_cast<double>(st.inlineNames) / static_cast<double>(st.liveCount)) * 100.0 : 0.0;

    out << caseName << "," << decls << "," << uses << "," << finalHeap << ","
        << p95Us << "," << st.inlineNames << "," << st.heapNames << "," << inlinePct << "\n";

    std::cout << "  " << caseName << ": heap=" << finalHeap << "B  p95=" << p95Us
              << "us  inline=" << inlinePct << "%\n";
}

int main() {
    std::ofstream out("results/metis_x_failure_cases.csv");
    if (!out) return 1;
    out << "test_case,declarations,uses,final_heap_bytes,lookup_p95_us,inline_slots,heap_slots,inline_pct\n";

    HiResTimer timer;
    std::mt19937 rng(42);

    std::cout << "=== METIS-X Step 12 Failure Cases ===\n";

    // 1. Very short names (1-3 chars)
    {
        std::vector<std::pair<std::string, int>> ops;
        std::vector<std::string> names;
        for (int i = 0; i < 5000; i++) names.push_back(randStr(rng, 1, 3));
        for (const auto& n : names) ops.push_back({n, 2});
        for (int r = 0; r < 5; r++) for (const auto& n : names) ops.push_back({n, 3});
        runFailureCase(out, "very_short_names_1_3B", ops, timer);
    }

    // 2. Very long names (20-50 chars)
    {
        std::vector<std::pair<std::string, int>> ops;
        std::vector<std::string> names;
        for (int i = 0; i < 5000; i++) names.push_back(randStr(rng, 20, 50));
        for (const auto& n : names) ops.push_back({n, 2});
        for (int r = 0; r < 5; r++) for (const auto& n : names) ops.push_back({n, 3});
        runFailureCase(out, "very_long_names_20_50B", ops, timer);
    }

    // 3. High uniqueness (10,000 unique names, 1 decl each)
    {
        std::vector<std::pair<std::string, int>> ops;
        for (int i = 0; i < 10000; i++) {
            std::string n = randStr(rng, 5, 15);
            ops.push_back({n, 2});
            ops.push_back({n, 3});
        }
        runFailureCase(out, "high_uniqueness_10k", ops, timer);
    }

    // 4. High reuse (10 unique names, 10,000 uses each)
    {
        std::vector<std::pair<std::string, int>> ops;
        std::vector<std::string> names;
        for (int i = 0; i < 10; i++) {
            std::string n = randStr(rng, 5, 15);
            names.push_back(n);
            ops.push_back({n, 2});
        }
        for (int i = 0; i < 100000; i++) {
            ops.push_back({names[i % 10], 3});
        }
        runFailureCase(out, "high_reuse_10_names_100k_uses", ops, timer);
    }

    // 5. High miss rate (50% lookups miss)
    {
        std::vector<std::pair<std::string, int>> ops;
        std::vector<std::string> hitNames, missNames;
        for (int i = 0; i < 2000; i++) hitNames.push_back("hit_" + std::to_string(i));
        for (int i = 0; i < 2000; i++) missNames.push_back("absent_" + std::to_string(i));
        for (const auto& n : hitNames) ops.push_back({n, 2});
        for (int i = 0; i < 5000; i++) {
            ops.push_back({hitNames[i % hitNames.size()], 3});
            ops.push_back({missNames[i % missNames.size()], 3});
        }
        runFailureCase(out, "high_miss_rate_50pct", ops, timer);
    }

    // 6. High scope depth (depth=50)
    {
        std::vector<std::pair<std::string, int>> ops;
        for (int d = 0; d < 50; d++) {
            ops.push_back({"", 0}); // enterScope
            for (int s = 0; s < 20; s++) {
                std::string n = "d" + std::to_string(d) + "_s" + std::to_string(s);
                ops.push_back({n, 2});
                ops.push_back({n, 3});
            }
        }
        for (int d = 0; d < 50; d++) ops.push_back({"", 1}); // exitScope
        runFailureCase(out, "high_scope_depth_50", ops, timer);
    }

    // 7. Heavy shadowing (same names in all scopes)
    {
        std::vector<std::pair<std::string, int>> ops;
        std::vector<std::string> names;
        for (int i = 0; i < 50; i++) names.push_back("shadow_var_" + std::to_string(i));
        for (int d = 0; d < 20; d++) {
            ops.push_back({"", 0});
            for (const auto& n : names) {
                ops.push_back({n, 2});
                ops.push_back({n, 3});
            }
        }
        for (int d = 0; d < 20; d++) ops.push_back({"", 1});
        runFailureCase(out, "heavy_shadowing_20_levels", ops, timer);
    }

    // 8. High scope churn (5,000 enter/exit cycles)
    {
        std::vector<std::pair<std::string, int>> ops;
        for (int cycle = 0; cycle < 5000; cycle++) {
            ops.push_back({"", 0});
            std::string n = "tmp_" + std::to_string(cycle);
            ops.push_back({n, 2});
            ops.push_back({n, 3});
            ops.push_back({"", 1});
        }
        runFailureCase(out, "high_scope_churn_5k_cycles", ops, timer);
    }

    out.flush();
    std::cout << "\nWrote results/metis_x_failure_cases.csv\n";
    return 0;
}
