#pragma once
// Synthetic identifier-workload generators shared by benchmark_main.cpp,
// grid_search_main.cpp and multiseed_main.cpp. Extracted from the original
// benchmark_main.cpp (Review-1) so all three tools build datasets identically.
#include <random>
#include <string>
#include <vector>

namespace budgetsym {

inline std::string randomIdentifier(std::mt19937& rng, int minLen, int maxLen) {
    static const char* alpha = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::uniform_int_distribution<int> lenDist(minLen, maxLen);
    std::uniform_int_distribution<int> chDist(0, 51);
    int len = lenDist(rng);
    std::string s;
    s.reserve(static_cast<size_t>(len));
    for (int i = 0; i < len; i++) s += alpha[chDist(rng)];
    return s;
}

struct Dataset {
    std::string name;
    std::vector<std::string> identifiers; // in insertion order, duplicates allowed
    size_t budgetBytes;
};

// ---- dataset generators (all deterministic given a fixed seed) -----------

inline Dataset genUniformRandom(const std::string& name, int n, int minLen, int maxLen, size_t seed, size_t budget) {
    Dataset d; d.name = name; d.budgetBytes = budget;
    std::mt19937 rng(static_cast<unsigned>(seed));
    for (int i = 0; i < n; i++) d.identifiers.push_back(randomIdentifier(rng, minLen, maxLen));
    return d;
}

// BudgetSym's similarity signal compares each identifier only against the one
// inserted immediately before it (see budget_sym.hpp, decide()) -- that is
// the realistic case (a compiler processing declarations in source order),
// not a global "does this prefix exist anywhere" search. So to actually
// exercise the COMPRESSED path, this dataset groups same-prefix identifiers
// into contiguous runs (like a block of related config-parameter or
// struct-field declarations in real source), rather than interleaving five
// unrelated prefixes item-by-item.
//
// NOTE: `seed` is intentionally unused here -- kept byte-identical to the
// original Review-1 benchmark_main.cpp generator so refactoring this file
// out of benchmark_main.cpp does not change results/benchmark_results.csv.
// For seed-varying behavior (needed by multiseed_main.cpp, since 30 identical
// "seeds" would collapse to zero variance), see genHighPrefixSimilaritySeeded
// below.
inline Dataset genHighPrefixSimilarity(int n, size_t seed, size_t budget) {
    Dataset d; d.name = "high-prefix-similarity"; d.budgetBytes = budget;
    static const char* prefixes[] = {
        "moduleConfigParameter", "temperatureSensorCalibration", "networkInterfaceBuffer",
        "userAuthenticationToken", "compilerSymbolTableEntry"
    };
    (void)seed;
    int groupSize = n / 5;
    for (int p = 0; p < 5; p++) {
        for (int i = 0; i < groupSize; i++) {
            d.identifiers.push_back(std::string(prefixes[p]) + std::to_string(p * groupSize + i));
        }
    }
    return d;
}

// Seed-varying sibling of genHighPrefixSimilarity: shuffles the per-group
// numeric suffix start so different seeds produce different (but still
// contiguous-run, still prefix-similar) identifier sequences. Only used by
// multiseed_main.cpp -- kept separate from genHighPrefixSimilarity above so
// benchmark_main.cpp's output is untouched by this addition.
inline Dataset genHighPrefixSimilaritySeeded(int n, size_t seed, size_t budget) {
    Dataset d; d.name = "high-prefix-similarity"; d.budgetBytes = budget;
    static const char* prefixes[] = {
        "moduleConfigParameter", "temperatureSensorCalibration", "networkInterfaceBuffer",
        "userAuthenticationToken", "compilerSymbolTableEntry"
    };
    std::mt19937 rng(static_cast<unsigned>(seed));
    std::uniform_int_distribution<int> offsetDist(0, 999);
    int groupSize = n / 5;
    for (int p = 0; p < 5; p++) {
        int offset = offsetDist(rng);
        for (int i = 0; i < groupSize; i++) {
            d.identifiers.push_back(std::string(prefixes[p]) + std::to_string(offset + p * groupSize + i));
        }
    }
    return d;
}

inline Dataset genNestedScopes(int scopesCount, int symbolsPerScope, size_t seed, size_t budget) {
    Dataset d; d.name = "nested-scopes"; d.budgetBytes = budget;
    std::mt19937 rng(static_cast<unsigned>(seed));
    for (int s = 0; s < scopesCount; s++) {
        for (int i = 0; i < symbolsPerScope; i++) {
            d.identifiers.push_back("scope" + std::to_string(s) + "_" + randomIdentifier(rng, 4, 10));
        }
    }
    return d;
}

// The "hot" fraction is deliberately long + prefix-similar (so BudgetSym starts
// them as COMPRESSED under the default policy); the rest are short/random
// "cold" identifiers that are accessed once and never revisited. The benchmark
// then repeatedly re-looks-up the hot fraction (see runOne), which is what
// should trigger COMPRESSED -> INTERNED promotion for the identifiers that
// actually got compressed initially.
inline Dataset genHotColdAccess(int n, size_t seed, size_t budget) {
    Dataset d; d.name = "hot-cold-access"; d.budgetBytes = budget;
    std::mt19937 rng(static_cast<unsigned>(seed));
    int hotN = n / 10;
    for (int i = 0; i < hotN; i++) {
        d.identifiers.push_back("hotPathVariableAccessedFrequently" + std::to_string(i));
    }
    for (int i = hotN; i < n; i++) d.identifiers.push_back(randomIdentifier(rng, 6, 18));
    return d;
}

inline Dataset genMemoryStress(int n, size_t seed, size_t tinyBudget) {
    Dataset d; d.name = "memory-stress"; d.budgetBytes = tinyBudget;
    std::mt19937 rng(static_cast<unsigned>(seed));
    for (int i = 0; i < n; i++) d.identifiers.push_back(randomIdentifier(rng, 15, 30));
    return d;
}

} // namespace budgetsym
