#pragma once
// Baseline #1 -- the textbook approach.
//
// One scope = one std::unordered_map<std::string, SymbolMeta>. Every symbol's
// identifier is stored as a full, independent std::string copy (the map key);
// nothing is shared or compressed. Nested scopes are a stack of these maps so
// shadowing and scope-exit reclamation behave like a real compiler's symbol
// table, but the per-symbol storage strategy itself never adapts to anything.
// This is what most course/hobby compilers ship, and is the floor BUDGET-SYM
// has to beat.
#include <string>
#include <unordered_map>
#include <vector>
#include "common.hpp"
#include "memory_tracker.hpp"

namespace budgetsym {

class ConventionalSymbolTable {
public:
    explicit ConventionalSymbolTable(size_t budgetBytes = 0) : tracker_(budgetBytes) {
        scopeMaps_.emplace_back();
    }

    int enterScope() {
        scopeMaps_.emplace_back();
        return static_cast<int>(scopeMaps_.size()) - 1;
    }

    struct ScopeExitReport { size_t symbolsReleased = 0; long long bytesReclaimed = 0; };

    ScopeExitReport exitScope() {
        ScopeExitReport rep;
        if (scopeMaps_.size() <= 1) return rep; // never pop the global scope
        for (auto& kv : scopeMaps_.back()) {
            long long cost = entryCost(kv.first);
            tracker_.reclaim(cost);
            rep.bytesReclaimed += cost;
            rep.symbolsReleased++;
        }
        scopeMaps_.pop_back();
        return rep;
    }

    int insert(const std::string& name, int typeId = 0) {
        int id = nextId_++;
        SymbolMeta meta;
        meta.id = id;
        meta.scopeId = static_cast<int>(scopeMaps_.size()) - 1;
        meta.typeId = typeId;
        meta.representation = Representation::INLINE_REP; // conventional = always "inline", no adaptivity
        // Same-scope redeclaration replaces the binding in place. It must not
        // be charged again: std::unordered_map::operator[] reuses the existing
        // node, so no new string copy exists. (Earlier versions charged
        // entryCost() on every insert, which inflated this baseline's modeled
        // memory by one full string per duplicate occurrence -- e.g. 80 MB
        // modeled vs 1.8 MB measured on the FreeRTOS token stream.)
        auto& scope = scopeMaps_.back();
        auto existing = scope.find(name);
        if (existing != scope.end()) {
            existing->second = meta;
            return id;
        }
        scope.emplace(name, meta);
        tracker_.add(entryCost(name));
        return id;
    }

    // Declaration ordinal (the value insert() returned) of the innermost live
    // binding of `name`, or -1. All symbol tables in this project number
    // declarations identically (0, 1, 2, ... per insert() call), so resolve()
    // results are directly comparable across implementations -- this is what
    // tests/differential_test.cpp checks.
    int resolve(const std::string& name) const {
        for (auto it = scopeMaps_.rbegin(); it != scopeMaps_.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) return f->second.id;
        }
        return -1;
    }

    bool lookup(const std::string& name) const { return resolve(name) >= 0; }

    void recordAccess(const std::string& name) {
        for (auto it = scopeMaps_.rbegin(); it != scopeMaps_.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) { f->second.accessCount++; return; }
        }
    }

    size_t size() const {
        size_t n = 0;
        for (auto& m : scopeMaps_) n += m.size();
        return n;
    }

    const MemoryTracker& tracker() const { return tracker_; }

    // Cost model: sizeof(std::string) control block + heap bytes for the
    // characters (+1 NUL) as the unordered_map key, plus SymbolMeta and a
    // fixed hash-node bookkeeping estimate. See docs/methodology.md.
    static long long entryCost(const std::string& name) {
        return static_cast<long long>(sizeof(std::string) + name.size() + 1 + kMetaOverhead);
    }

    static const long long kMetaOverhead = 32; // SymbolMeta (~24B) + hash-node next-ptr estimate

private:
    std::vector<std::unordered_map<std::string, SymbolMeta>> scopeMaps_;
    int nextId_ = 0;
    MemoryTracker tracker_;
};

} // namespace budgetsym
