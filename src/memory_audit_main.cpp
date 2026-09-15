// Memory-accounting audit (experiment E1): measured heap vs modeled
// MemoryTracker bytes for every symbol-table implementation, on the same 8
// synthetic datasets as benchmark_main.cpp plus any extracted corpus identifier
// streams passed on the command line.
//
// This tool deliberately runs the tables exactly as the existing benchmarks do
// (insert every identifier into the global scope, in order) so the numbers
// quantify how far the *existing* reported memory figures are from what the
// allocator actually holds. It does not change any table.
//
// Two measurement points per (dataset, implementation):
//   after_insert : right after inserting every identifier
//   after_lookup : after one successful lookup() of every distinct identifier
//                  (BudgetSym memoizes COMPRESSED reconstructions on lookup,
//                  so its real footprint can grow here while the model does not)
//
// Usage: ./memory_audit.exe [corpus_ids_file corpus_name]...
// Writes results/memory_audit.csv.
#define BUDGETSYM_HEAP_COUNTER_IMPL
#include "../include/heap_counter.hpp"

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
#include "../include/conventional_symbol_table.hpp"
#include "../include/interned_symbol_table.hpp"
#include "../include/budget_sym.hpp"
#include "../include/robinhood_symbol_table.hpp"
#include "../include/trie_symbol_table.hpp"
#include "../include/dataset_generators.hpp"

using namespace budgetsym;

struct AuditRow {
    std::string dataset, impl;
    size_t declarations = 0, uniqueNames = 0, reportedSymbols = 0;
    long long modeledAfterInsert = 0, measuredAfterInsert = 0, measuredPeakInsert = 0;
    long long modeledAfterLookup = 0, measuredAfterLookup = 0;
};

template <typename Table, typename Factory>
static AuditRow audit(const std::string& implName, const Dataset& ds,
                      const std::vector<std::string>& distinct, Factory make) {
    AuditRow r;
    r.dataset = ds.name;
    r.impl = implName;
    r.declarations = ds.identifiers.size();
    r.uniqueNames = distinct.size();

    heap::Scope scope; // baseline taken after the dataset + distinct list exist
    {
        std::unique_ptr<Table> t(make());
        for (auto& id : ds.identifiers) t->insert(id);
        r.measuredAfterInsert = scope.bytes();
        r.measuredPeakInsert = scope.peakBytes();
        r.modeledAfterInsert = t->tracker().current();
        r.reportedSymbols = t->size();

        volatile bool sink = false;
        for (auto& id : distinct) { bool hit = t->lookup(id); sink = sink || hit; }
        (void)sink;
        r.measuredAfterLookup = scope.bytes();
        r.modeledAfterLookup = t->tracker().current();
    }
    long long leaked = scope.bytes();
    if (leaked != 0) {
        std::cerr << "WARNING: " << implName << " on " << ds.name << " left " << leaked
                  << " heap bytes after destruction\n";
    }
    return r;
}

static void auditDataset(const Dataset& ds, std::vector<AuditRow>& rows) {
    std::vector<std::string> distinct;
    {
        std::unordered_set<std::string> seen;
        for (auto& id : ds.identifiers) if (seen.insert(id).second) distinct.push_back(id);
    }
    size_t budget = ds.budgetBytes;
    rows.push_back(audit<ConventionalSymbolTable>("Conventional", ds, distinct,
        [budget] { return new ConventionalSymbolTable(budget); }));
    rows.push_back(audit<InternedSymbolTable>("Interned", ds, distinct,
        [budget] { return new InternedSymbolTable(budget); }));
    rows.push_back(audit<BudgetSym>("BudgetSym(default-onlineML)", ds, distinct,
        [budget] { return new BudgetSym(budget); }));
    rows.push_back(audit<BudgetSym>("BudgetSym(fixed-handpicked)", ds, distinct,
        [budget] { PolicyConfig c; c.disableMLThresholdPrediction = true; return new BudgetSym(budget, c); }));
    rows.push_back(audit<RobinHoodSymbolTable>("RobinHood", ds, distinct,
        [budget] { return new RobinHoodSymbolTable(budget); }));
    rows.push_back(audit<TrieSymbolTable>("Trie", ds, distinct,
        [budget] { return new TrieSymbolTable(budget); }));
}

int main(int argc, char** argv) {
    const size_t seed = 42;
    const size_t defaultBudget = 64ull * 1024 * 1024;
    const size_t tinyBudget = 8192;

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
            auditDataset(ds, rows);
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
        auditDataset(ds, rows);
    }

    std::ofstream out("results/memory_audit.csv");
    if (!out) { std::cerr << "ERROR: cannot write results/memory_audit.csv (run from repo root)\n"; return 1; }
    out << "dataset,implementation,declarations,unique_names,reported_symbols,"
           "modeled_bytes_after_insert,measured_heap_bytes_after_insert,measured_peak_heap_bytes_insert,"
           "measured_over_modeled_insert,modeled_bytes_after_lookup,measured_heap_bytes_after_lookup,"
           "measured_bytes_per_unique_name\n";
    for (auto& r : rows) {
        double ratio = r.modeledAfterInsert > 0
            ? static_cast<double>(r.measuredAfterInsert) / static_cast<double>(r.modeledAfterInsert) : 0.0;
        double perName = r.uniqueNames > 0
            ? static_cast<double>(r.measuredAfterInsert) / static_cast<double>(r.uniqueNames) : 0.0;
        out << r.dataset << "," << r.impl << "," << r.declarations << "," << r.uniqueNames << ","
            << r.reportedSymbols << "," << r.modeledAfterInsert << "," << r.measuredAfterInsert << ","
            << r.measuredPeakInsert << "," << ratio << "," << r.modeledAfterLookup << ","
            << r.measuredAfterLookup << "," << perName << "\n";
    }
    std::cout << "Wrote results/memory_audit.csv (" << rows.size() << " rows)\n";
    return 0;
}
