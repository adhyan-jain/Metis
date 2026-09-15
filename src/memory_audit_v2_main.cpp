// Measured-heap audit for SymTabV2 against the same baselines
// memory_audit_main.cpp already established for V1 -- see that file's header
// for the measurement methodology (heap_counter.hpp).
//
// P0-1 of the V2 research-rewrite plan: prior versions of this tool measured
// heap at exactly one point (right after the initial insert pass). That is
// exactly the measurement an insert-only pass cannot catch an accounting bug
// in -- the pool-refcount under-charge bug found this session (a string
// interned, fully released, then interned again silently under-counting)
// was invisible to an insert-only snapshot and was only found via a
// lookup/release/reinsert cycle. This tool now measures THREE points per
// table and adds an automated repeatability invariant instead of relying on
// a one-off manual repro:
//   1. measuredAfterInsert   -- unchanged from before (insert every dataset
//                                identifier into the global scope once).
//   2. measuredAfterLookup   -- after resolve()-ing every DISTINCT
//                                identifier once (exercises memoization/
//                                promotion-adjacent bookkeeping without
//                                changing what's live).
//   3. measuredAfterLookup is followed by a per-dataset scope-cycle probe
//      (enterScope/insert-40-mixed-names/exitScope, twice) reported in the
//      CSV as diagnostic columns -- NOT gated on for pass/fail, because on
//      a table already holding the full dataset the probe's compressed
//      block entries can share an open block with still-live dataset
//      entries, and SymTabV2's whole-block-reclaim design (documented in
//      symtab_v2.hpp: "batched to whenever the LAST member of a block
//      happens to die -- a real, honestly-reported trade-off") means that
//      residual growth is then EXPECTED and workload-dependent, not a bug.
//      Confirmed by observation before finalizing this tool: gating on
//      strict repeatability here produced failures on every dataset that
//      were traced to exactly this pre-existing, already-documented
//      trade-off, not to any accounting error.
//
// The actual automated PASS/FAIL invariant (runRepeatabilityInvariant(),
// below) runs separately, once per table type, on a FRESH, otherwise-empty
// instance -- so the probe's compressed entries never share a block with
// anything else, isolating the check to pure accounting correctness (the
// class of bug this exists to catch, e.g. the pool-refcount under-charge
// found this session) rather than the block-sharing trade-off above. Same
// two-cycle repeatability technique already validated in
// tests/symtab_v2_compressed_test.cpp's test_whole_block_reclaim_to_zero.
//
// Usage: ./memory_audit_v2.exe [corpus_ids_file corpus_name]...
// Writes results/memory_audit_v2.csv. Exits 1 if the invariant check fails.
#define BUDGETSYM_HEAP_COUNTER_IMPL
#include "../include/heap_counter.hpp"

#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>
#include "../include/conventional_symbol_table.hpp"
#include "../include/interned_symbol_table.hpp"
#include "../include/robinhood_symbol_table.hpp"
#include "../include/symtab_v2.hpp"
#include "../include/dataset_generators.hpp"

using namespace budgetsym;

static bool g_anyInvariantFailed = false;

// Fixed, deterministic 40-name probe set for the scope-cycle invariant:
// 20 short names (< inlineMaxLen, INLINE-eligible on every table that has
// the concept) and 20 long, prefix-similar names (>= compressMinLen on
// SymTabV2's default policy, so the COMPRESSED and then, on the repeat
// cycle, INTERNED paths both actually get exercised -- not just INLINE).
static std::vector<std::string> probeNames() {
    std::vector<std::string> names;
    std::mt19937 rng(9001);
    for (int i = 0; i < 20; i++) names.push_back(randomIdentifier(rng, 4, 10));
    static const char* prefixes[] = {"temperatureSensorCalibration", "networkInterfaceBufferPool"};
    for (int p = 0; p < 2; p++) {
        for (int i = 0; i < 10; i++) names.push_back(std::string(prefixes[p]) + std::to_string(i));
    }
    return names;
}

struct AuditRow {
    std::string dataset, impl;
    size_t declarations = 0, uniqueNames = 0, reportedSymbols = 0, peakSlots = 0;
    long long modeledAfterInsert = 0, measuredAfterInsert = 0, measuredPeakInsert = 0;
    long long measuredAfterLookup = 0;
    long long firstCycleResidual = 0, secondCycleResidual = 0; // diagnostic only, see file header
};

// Runs the shared post-insert measurement sequence (lookup pass + scope-
// cycle probe) against an already-populated table, filling in the
// remaining AuditRow fields. Works identically for all four table types via
// the same four methods every one of them exposes: resolve(), enterScope(),
// exitScope(), and heap::Scope for the byte accounting. The scope-cycle
// numbers here are DIAGNOSTIC (see file header) -- the pass/fail invariant
// runs separately, in runRepeatabilityInvariant() below, on a fresh table.
template <typename Table>
static void measureRemainder(Table& t, heap::Scope& scope, AuditRow& r,
                              const std::vector<std::string>& distinctNames,
                              const std::vector<std::string>& probe) {
    volatile bool sink = false;
    for (auto& n : distinctNames) { int id = t.resolve(n); sink = sink || (id >= 0); }
    (void)sink;
    r.measuredAfterLookup = scope.bytes();

    long long baseline = scope.bytes();
    t.enterScope();
    for (auto& n : probe) t.insert(n);
    t.exitScope();
    r.firstCycleResidual = scope.bytes() - baseline;

    t.enterScope();
    for (auto& n : probe) t.insert(n);
    t.exitScope();
    r.secondCycleResidual = scope.bytes() - baseline;
}

// The actual automated PASS/FAIL accounting-invariant check. Builds a FRESH,
// otherwise-empty table (so the probe's compressed block entries never share
// a block with anything else -- see file header for why that matters) and
// runs the enter/insert-probe/exit cycle THREE times, checking that cycle 2
// and cycle 3's residual growth over the pre-cycle baseline are IDENTICAL.
//
// Why three cycles, comparing 2 vs 3, not 1 vs 2: found empirically while
// building this check (not assumed) -- cycle 1's 20 long probe names are
// new, so decide() routes them to COMPRESSED (growing compPool_'s
// container infrastructure for the first time); cycle 2's SAME names are
// now repeats (per the exact-repeat rule), so decide() routes them to
// INTERNED instead (growing pool_/poolLookup_'s container infrastructure
// for the first time -- a container that cycle 1 never touched at all).
// std::vector/std::unordered_map capacity growth is asymmetric with
// logical reclaim (capacity never shrinks below its high-water mark), so
// cycle 1 and cycle 2 legitimately allocate different amounts even though
// both are "the same 40 names" -- confirmed by extending the repro to a
// cycle 3 and observing it match cycle 2 exactly (both tracker() and
// measured heap), i.e. steady state is reached after the one-time
// COMPRESSED-then-INTERNED representation-switch cost. Comparing cycle 2 vs
// cycle 3 (both steady-state INTERNED, both past any first-touch container
// growth) isolates the check to genuine accounting drift -- the class of
// bug this exists to catch (e.g. the pool-refcount under-charge bug found
// this session) -- rather than this one-time, expected allocator artifact.
template <typename Table, typename Factory>
static void runRepeatabilityInvariant(const std::string& implName, const std::vector<std::string>& probe,
                                       Factory make) {
    heap::Scope scope;
    long long secondResidual, thirdResidual;
    {
        std::unique_ptr<Table> t(make());
        long long baseline = scope.bytes();
        t->enterScope(); for (auto& n : probe) t->insert(n); t->exitScope(); // cycle 1: warm-up, discarded
        t->enterScope(); for (auto& n : probe) t->insert(n); t->exitScope();
        secondResidual = scope.bytes() - baseline;
        t->enterScope(); for (auto& n : probe) t->insert(n); t->exitScope();
        thirdResidual = scope.bytes() - baseline;
    }
    if (secondResidual != thirdResidual) {
        g_anyInvariantFailed = true;
        std::cerr << "INVARIANT FAIL: " << implName << " on a fresh table -- steady-state cycle-2 residual="
                  << secondResidual << " cycle-3 residual=" << thirdResidual
                  << " (must be equal once past the one-time representation-switch warm-up: a"
                     " release/reinsert cycle must not silently accumulate or lose tracked bytes)\n";
    } else {
        std::cout << "  " << implName << ": repeatability OK (steady-state residual=" << secondResidual << ")\n";
    }
}

// V2-specific overload: v2::SymTabV2's factory already returns a
// std::unique_ptr (matching auditV2() above's convention), so it cannot
// share runRepeatabilityInvariant()'s `std::unique_ptr<Table> t(make())`
// wrapping, which assumes a raw-pointer-returning factory. See that
// function's comment for why cycle 2 vs cycle 3, not cycle 1 vs cycle 2.
template <typename Factory>
static void runRepeatabilityInvariantV2(const std::string& implName, const std::vector<std::string>& probe,
                                         Factory make) {
    heap::Scope scope;
    long long secondResidual, thirdResidual;
    {
        auto t = make();
        long long baseline = scope.bytes();
        t->enterScope(); for (auto& n : probe) t->insert(n); t->exitScope(); // cycle 1: warm-up, discarded
        t->enterScope(); for (auto& n : probe) t->insert(n); t->exitScope();
        secondResidual = scope.bytes() - baseline;
        t->enterScope(); for (auto& n : probe) t->insert(n); t->exitScope();
        thirdResidual = scope.bytes() - baseline;
    }
    if (secondResidual != thirdResidual) {
        g_anyInvariantFailed = true;
        std::cerr << "INVARIANT FAIL: " << implName << " on a fresh table -- steady-state cycle-2 residual="
                  << secondResidual << " cycle-3 residual=" << thirdResidual
                  << " (must be equal once past the one-time representation-switch warm-up: a"
                     " release/reinsert cycle must not silently accumulate or lose tracked bytes)\n";
    } else {
        std::cout << "  " << implName << ": repeatability OK (steady-state residual=" << secondResidual << ")\n";
    }
}

template <typename Table, typename Factory>
static AuditRow audit(const std::string& implName, const Dataset& ds,
                       const std::vector<std::string>& distinctNames,
                       const std::vector<std::string>& probe, Factory make) {
    AuditRow r;
    r.dataset = ds.name;
    r.impl = implName;
    r.declarations = ds.identifiers.size();
    r.uniqueNames = distinctNames.size();

    heap::Scope scope;
    {
        std::unique_ptr<Table> t(make());
        for (auto& id : ds.identifiers) t->insert(id);
        r.measuredAfterInsert = scope.bytes();
        r.measuredPeakInsert = scope.peakBytes();
        r.modeledAfterInsert = t->tracker().current();
        r.reportedSymbols = t->size();
        measureRemainder(*t, scope, r, distinctNames, probe);
    }
    long long leaked = scope.bytes();
    if (leaked != 0) {
        std::cerr << "WARNING: " << implName << " on " << ds.name << " left " << leaked
                  << " heap bytes after destruction\n";
    }
    return r;
}

template <typename Factory>
static AuditRow auditV2(const std::string& implName, const Dataset& ds,
                         const std::vector<std::string>& distinctNames,
                         const std::vector<std::string>& probe, Factory make) {
    AuditRow r;
    r.dataset = ds.name;
    r.impl = implName;
    r.declarations = ds.identifiers.size();
    r.uniqueNames = distinctNames.size();

    heap::Scope scope;
    {
        auto t = make();
        for (auto& id : ds.identifiers) t->insert(id);
        r.measuredAfterInsert = scope.bytes();
        r.measuredPeakInsert = scope.peakBytes();
        r.modeledAfterInsert = t->tracker().current();
        r.reportedSymbols = t->size();
        r.peakSlots = t->peakSlotCount();
        measureRemainder(*t, scope, r, distinctNames, probe);
    }
    long long leaked = scope.bytes();
    if (leaked != 0) {
        std::cerr << "WARNING: " << implName << " on " << ds.name << " left " << leaked
                  << " heap bytes after destruction\n";
    }
    return r;
}

static void auditDataset(const Dataset& ds, const std::vector<std::string>& probe, std::vector<AuditRow>& rows) {
    size_t budget = ds.budgetBytes;
    std::vector<std::string> distinctNames;
    {
        std::unordered_set<std::string> seen;
        for (auto& id : ds.identifiers) if (seen.insert(id).second) distinctNames.push_back(id);
    }

    rows.push_back(audit<ConventionalSymbolTable>("Conventional", ds, distinctNames, probe,
        [budget] { return new ConventionalSymbolTable(budget); }));
    rows.push_back(audit<InternedSymbolTable>("Interned", ds, distinctNames, probe,
        [budget] { return new InternedSymbolTable(budget); }));
    rows.push_back(audit<RobinHoodSymbolTable>("RobinHood", ds, distinctNames, probe,
        [budget] { return new RobinHoodSymbolTable(budget); }));
    rows.push_back(auditV2("SymTabV2", ds, distinctNames, probe,
        [budget] { return std::unique_ptr<v2::SymTabV2<>>(new v2::SymTabV2<>(budget)); }));
}

int main(int argc, char** argv) {
    const size_t seed = 42;
    const size_t defaultBudget = 64ull * 1024 * 1024;
    const size_t tinyBudget = 8192;
    const std::vector<std::string> probe = probeNames();

    std::cout << "== Accounting-invariant check (fresh tables, isolated from any dataset) ==\n";
    runRepeatabilityInvariant<ConventionalSymbolTable>("Conventional", probe,
        [defaultBudget] { return new ConventionalSymbolTable(defaultBudget); });
    runRepeatabilityInvariant<InternedSymbolTable>("Interned", probe,
        [defaultBudget] { return new InternedSymbolTable(defaultBudget); });
    runRepeatabilityInvariant<RobinHoodSymbolTable>("RobinHood", probe,
        [defaultBudget] { return new RobinHoodSymbolTable(defaultBudget); });
    runRepeatabilityInvariantV2("SymTabV2", probe,
        [defaultBudget] { return std::unique_ptr<v2::SymTabV2<>>(new v2::SymTabV2<>(defaultBudget)); });
    std::cout << "\n";

    std::vector<AuditRow> rows;
    {
        std::vector<Dataset> datasets;
        datasets.push_back(genUniformRandom("small", 100, 4, 12, seed, defaultBudget));
        datasets.push_back(genUniformRandom("medium", 2000, 4, 16, seed + 1, defaultBudget));
        datasets.push_back(genUniformRandom("large", 20000, 4, 16, seed + 2, defaultBudget));
        datasets.push_back(genHighPrefixSimilarity(2000, seed + 3, defaultBudget));
        datasets.push_back(genUniformRandom("random-identifiers", 2000, 3, 24, seed + 4, defaultBudget));
        datasets.push_back(genNestedScopes(40, 25, seed + 5, defaultBudget));
        datasets.push_back(genHotColdAccess(1500, seed + 6, defaultBudget));
        datasets.push_back(genMemoryStress(1500, seed + 7, tinyBudget));
        for (auto& ds : datasets) {
            std::cout << "auditing " << ds.name << "\n";
            auditDataset(ds, probe, rows);
        }
    }

    for (int i = 1; i + 1 < argc; i += 2) {
        Dataset ds;
        ds.name = argv[i + 1];
        ds.budgetBytes = defaultBudget;
        std::ifstream in(argv[i]);
        if (!in) { std::cerr << "skipping unreadable corpus file " << argv[i] << "\n"; continue; }
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) ds.identifiers.push_back(line);
        }
        std::cout << "auditing corpus " << ds.name << " (" << ds.identifiers.size() << " tokens)\n";
        auditDataset(ds, probe, rows);
    }

    std::ofstream out("results/memory_audit_v2.csv");
    if (!out) { std::cerr << "ERROR: cannot write results/memory_audit_v2.csv (run from repo root)\n"; return 1; }
    out << "dataset,implementation,declarations,unique_names,reported_symbols,peak_slots,"
           "modeled_bytes_after_insert,measured_heap_bytes_after_insert,measured_peak_heap_bytes_insert,"
           "measured_over_modeled_insert,measured_bytes_per_unique_name,measured_heap_bytes_after_lookup,"
           "first_cycle_residual,second_cycle_residual\n";
    for (auto& r : rows) {
        double ratio = r.modeledAfterInsert > 0
            ? static_cast<double>(r.measuredAfterInsert) / static_cast<double>(r.modeledAfterInsert) : 0.0;
        double perName = r.uniqueNames > 0
            ? static_cast<double>(r.measuredAfterInsert) / static_cast<double>(r.uniqueNames) : 0.0;
        out << r.dataset << "," << r.impl << "," << r.declarations << "," << r.uniqueNames << ","
            << r.reportedSymbols << "," << r.peakSlots << "," << r.modeledAfterInsert << ","
            << r.measuredAfterInsert << "," << r.measuredPeakInsert << "," << ratio << "," << perName << ","
            << r.measuredAfterLookup << "," << r.firstCycleResidual << "," << r.secondCycleResidual << "\n";
    }
    std::cout << "Wrote results/memory_audit_v2.csv (" << rows.size() << " rows)\n";
    if (g_anyInvariantFailed) {
        std::cerr << "\nFAILED: one or more repeatability invariant checks failed (see above)\n";
        return 1;
    }
    std::cout << "All repeatability invariant checks passed.\n";
    return 0;
}
