#pragma once
// Baseline #2 -- classic string interning.
//
// Every distinct identifier string is stored exactly once in a shared,
// refcounted pool; each scope's lookup map is keyed by the small integer pool
// index instead of the string itself, so a repeated identifier (e.g. "i" used
// in fifty different functions) costs one shared string + fifty 4-byte
// indices, not fifty independent string copies. This is the standard
// "quick win" alternative to the conventional table -- BUDGET-SYM has to beat
// *this*, not just the naive baseline, for the comparison to mean anything.
#include <string>
#include <unordered_map>
#include <vector>
#include "common.hpp"
#include "memory_tracker.hpp"

namespace budgetsym {

class InternedSymbolTable {
public:
    explicit InternedSymbolTable(size_t budgetBytes = 0) : tracker_(budgetBytes) {
        scopeMaps_.emplace_back();
    }

    int enterScope() {
        scopeMaps_.emplace_back();
        return static_cast<int>(scopeMaps_.size()) - 1;
    }

    struct ScopeExitReport { size_t symbolsReleased = 0; long long bytesReclaimed = 0; };

    ScopeExitReport exitScope() {
        ScopeExitReport rep;
        if (scopeMaps_.size() <= 1) return rep;
        for (auto& kv : scopeMaps_.back()) {
            long long freed = releasePoolRef(kv.first) + kIndexEntryOverhead;
            tracker_.reclaim(freed);
            rep.bytesReclaimed += freed;
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
        meta.representation = Representation::INTERNED_REP;
        // Same-scope redeclaration replaces the binding in place: no second
        // scope-map slot exists, so no extra charge and no extra pool
        // reference. (Earlier versions bumped the refcount and charged
        // kIndexEntryOverhead on every insert but released only once on
        // scope exit -- a refcount leak plus double-charging.)
        auto p = poolLookup_.find(name);
        if (p != poolLookup_.end()) {
            auto existing = scopeMaps_.back().find(p->second);
            if (existing != scopeMaps_.back().end()) {
                existing->second = meta;
                return id;
            }
        }
        int poolIdx = internName(name);
        scopeMaps_.back().emplace(poolIdx, meta);
        tracker_.add(kIndexEntryOverhead);
        return id;
    }

    // See ConventionalSymbolTable::resolve().
    int resolve(const std::string& name) const {
        auto p = poolLookup_.find(name);
        if (p == poolLookup_.end()) return -1; // never interned -> definitely not present
        for (auto it = scopeMaps_.rbegin(); it != scopeMaps_.rend(); ++it) {
            auto f = it->find(p->second);
            if (f != it->end()) return f->second.id;
        }
        return -1;
    }

    bool lookup(const std::string& name) const { return resolve(name) >= 0; }

    void recordAccess(const std::string& name) {
        auto p = poolLookup_.find(name);
        if (p == poolLookup_.end()) return;
        for (auto it = scopeMaps_.rbegin(); it != scopeMaps_.rend(); ++it) {
            auto f = it->find(p->second);
            if (f != it->end()) { f->second.accessCount++; return; }
        }
    }

    size_t size() const {
        size_t n = 0;
        for (auto& m : scopeMaps_) n += m.size();
        return n;
    }

    const MemoryTracker& tracker() const { return tracker_; }

    static const long long kIndexEntryOverhead = 28; // SymbolMeta (~24B) + 4-byte pool index, per scope-map slot

private:
    // Second bug (found via a symtab_v2.hpp repro, same pattern here):
    // pool_[idx] is never physically erased when the last reference drops
    // (releasePoolRef() below only reclaims its bytes from the tracker; the
    // slot itself stays allocated -- indices must stay stable while other
    // refs may exist). So if this exact name is interned again later
    // (refcount 0 -> 1), reusing the existing pool slot must re-charge those
    // bytes, or the tracker silently under-counts relative to what's
    // actually still allocated.
    int internName(const std::string& name) {
        auto it = poolLookup_.find(name);
        if (it != poolLookup_.end()) {
            int idx = it->second;
            if (poolRefCount_[idx] == 0) {
                tracker_.add(static_cast<long long>(sizeof(std::string) + pool_[idx].size() + 1));
            }
            poolRefCount_[idx]++;
            return idx;
        }
        int idx = static_cast<int>(pool_.size());
        pool_.push_back(name);
        poolRefCount_.push_back(1);
        poolLookup_[name] = idx;
        tracker_.add(static_cast<long long>(sizeof(std::string) + name.size() + 1));
        return idx;
    }

    // Decrements the pool refcount for whatever name maps to poolIndex-in-scope;
    // frees the pooled string's bytes only when the last reference drops.
    // Note: the pool vector slot itself is not compacted (indices must stay
    // stable while other refs may still exist) -- this is the standard
    // fixed-cost trade-off real interning pools make; see docs/future_work.md.
    long long releasePoolRef(int poolIdx) {
        poolRefCount_[poolIdx]--;
        if (poolRefCount_[poolIdx] == 0) {
            return static_cast<long long>(sizeof(std::string) + pool_[poolIdx].size() + 1);
        }
        return 0;
    }

    std::vector<std::unordered_map<int, SymbolMeta>> scopeMaps_;
    std::vector<std::string> pool_;
    std::vector<int> poolRefCount_;
    std::unordered_map<std::string, int> poolLookup_;
    int nextId_ = 0;
    MemoryTracker tracker_;
};

} // namespace budgetsym
