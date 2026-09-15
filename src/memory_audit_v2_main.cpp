// Measured-heap audit for SymTabV2 (P0-1/P0-2 slice) against the same
// baselines memory_audit_main.cpp already established for V1 -- see that
// file's header for the measurement methodology (heap_counter.hpp).
//
// Purpose: verify, with real numbers, whether the free-list slot allocator +
// open-addressing index actually fixes the V1 finding (append-only Entry
// vector = 73% of measured heap on the Zephyr corpus). Uses the identical 8
// synthetic datasets plus any corpus token files passed on the command line,
// so rows are directly comparable to results/memory_audit.csv.
//
// Usage: ./memory_audit_v2.exe [corpus_ids_file corpus_name]...
// Writes results/memory_audit_v2.csv.
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
#include "../include/robinhood_symbol_table.hpp"
#include "../include/symtab_v2.hpp"
#include "../include/dataset_generators.hpp"

using namespace budgetsym;

struct AuditRow {
    std::string dataset, impl;
    size_t declarations = 0, uniqueNames = 0, reportedSymbols = 0, peakSlots = 0;
    long long modeledAfterInsert = 0, measuredAfterInsert = 0, measuredPeakInsert = 0;
};

template <typename Table, typename Factory>
static AuditRow audit(const std::string& implName, const Dataset& ds, Factory make) {
    AuditRow r;
    r.dataset = ds.name;
    r.impl = implName;
    r.declarations = ds.identifiers.size();
    {
        std::unordered_set<std::string> seen;
        for (auto& id : ds.identifiers) if (seen.insert(id).second) r.uniqueNames++;
    }

    heap::Scope scope;
    {
        std::unique_ptr<Table> t(make());
        for (auto& id : ds.identifiers) t->insert(id);
        r.measuredAfterInsert = scope.bytes();
        r.measuredPeakInsert = scope.peakBytes();
        r.modeledAfterInsert = t->tracker().current();
        r.reportedSymbols = t->size();
    }
    long long leaked = scope.bytes();
    if (leaked != 0) {
        std::cerr << "WARNING: " << implName << " on " << ds.name << " left " << leaked
                  << " heap bytes after destruction\n";
    }
    return r;
}

template <typename Factory>
static AuditRow auditV2(const std::string& implName, const Dataset& ds, Factory make) {
    AuditRow r;
    r.dataset = ds.name;
    r.impl = implName;
    r.declarations = ds.identifiers.size();
    {
        std::unordered_set<std::string> seen;
        for (auto& id : ds.identifiers) if (seen.insert(id).second) r.uniqueNames++;
    }
    heap::Scope scope;
    {
        auto t = make();
        for (auto& id : ds.identifiers) t->insert(id);
        r.measuredAfterInsert = scope.bytes();
        r.measuredPeakInsert = scope.peakBytes();
        r.modeledAfterInsert = t->tracker().current();
        r.reportedSymbols = t->size();
        r.peakSlots = t->peakSlotCount();
    }
    long long leaked = scope.bytes();
    if (leaked != 0) {
        std::cerr << "WARNING: " << implName << " on " << ds.name << " left " << leaked
                  << " heap bytes after destruction\n";
    }
    return r;
}

static void auditDataset(const Dataset& ds, std::vector<AuditRow>& rows) {
    size_t budget = ds.budgetBytes;
    rows.push_back(audit<ConventionalSymbolTable>("Conventional", ds,
        [budget] { return new ConventionalSymbolTable(budget); }));
    rows.push_back(audit<InternedSymbolTable>("Interned", ds,
        [budget] { return new InternedSymbolTable(budget); }));
    rows.push_back(audit<RobinHoodSymbolTable>("RobinHood", ds,
        [budget] { return new RobinHoodSymbolTable(budget); }));
    rows.push_back(auditV2("SymTabV2", ds,
        [budget] { return std::unique_ptr<v2::SymTabV2<>>(new v2::SymTabV2<>(budget)); }));
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

    std::ofstream out("results/memory_audit_v2.csv");
    if (!out) { std::cerr << "ERROR: cannot write results/memory_audit_v2.csv (run from repo root)\n"; return 1; }
    out << "dataset,implementation,declarations,unique_names,reported_symbols,peak_slots,"
           "modeled_bytes_after_insert,measured_heap_bytes_after_insert,measured_peak_heap_bytes_insert,"
           "measured_over_modeled_insert,measured_bytes_per_unique_name\n";
    for (auto& r : rows) {
        double ratio = r.modeledAfterInsert > 0
            ? static_cast<double>(r.measuredAfterInsert) / static_cast<double>(r.modeledAfterInsert) : 0.0;
        double perName = r.uniqueNames > 0
            ? static_cast<double>(r.measuredAfterInsert) / static_cast<double>(r.uniqueNames) : 0.0;
        out << r.dataset << "," << r.impl << "," << r.declarations << "," << r.uniqueNames << ","
            << r.reportedSymbols << "," << r.peakSlots << "," << r.modeledAfterInsert << ","
            << r.measuredAfterInsert << "," << r.measuredPeakInsert << "," << ratio << "," << perName << "\n";
    }
    std::cout << "Wrote results/memory_audit_v2.csv (" << rows.size() << " rows)\n";
    return 0;
}
