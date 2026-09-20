#pragma once
// BUDGET-SYM Review-2 extension: a small, hash-keyed LRU cache sitting in
// front of BudgetSym's normal hash-then-reconstruct lookup path (see
// budget_sym.hpp's header comment and docs/faculty_questions.md, "Does
// compression hurt lookup?"). A hit here skips the reconstruct-and-compare
// step entirely for whatever symbols were looked up most recently -- the
// direct latency-reduction mechanism for Review-2 claim (Section VIII-E of
// the paper).
//
// Deliberately name-keyed, not just hash-keyed: fnv1a is a 64-bit non-
// cryptographic hash with no collision resistance guarantee, and a false
// cache hit would return the wrong symbol's entry pointer silently. Storing
// the name alongside the hash lets get() confirm an exact match before
// declaring a hit, at the cost of one string comparison per hit (still far
// cheaper than a COMPRESSED chain walk).
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>

namespace budgetsym {

struct CacheStats {
    long long hits = 0;
    long long misses = 0;
    double hitRate = 0.0;
};

template <size_t N = 64>
class LRULookupCache {
public:
    // Not part of the Phase 2 spec's minimal API, but needed for the Review-2
    // benchmark/paper's "with cache vs without cache" latency comparison
    // (Section VIII-E) -- lets benchmark_main.cpp instantiate a BudgetSym
    // whose cache is present but inert, isolating the cache's latency effect
    // without adding a second BudgetSym class or a compile-time toggle.
    void setEnabled(bool e) { enabled_ = e; }

    bool get(uint64_t hash, const std::string& name, void*& outPtr) {
        if (!enabled_) { misses_++; return false; }
        auto idxIt = index_.find(hash);
        if (idxIt != index_.end() && idxIt->second->name == name) {
            lruList_.splice(lruList_.begin(), lruList_, idxIt->second);
            outPtr = idxIt->second->entryPtr;
            hits_++;
            return true;
        }
        misses_++;
        return false;
    }

    void put(uint64_t hash, const std::string& name, void* entryPtr) {
        if (!enabled_) return;
        auto idxIt = index_.find(hash);
        if (idxIt != index_.end() && idxIt->second->name == name) {
            idxIt->second->entryPtr = entryPtr;
            lruList_.splice(lruList_.begin(), lruList_, idxIt->second);
            return;
        }
        lruList_.push_front(CacheEntry{hash, name, entryPtr});
        index_[hash] = lruList_.begin();
        if (lruList_.size() > N) {
            const CacheEntry& tail = lruList_.back();
            index_.erase(tail.hash);
            lruList_.pop_back();
        }
    }

    // Drops a cached entry (used on scope exit, when the underlying symbol is
    // reclaimed and the cached entryPtr would otherwise dangle).
    void invalidate(uint64_t hash) {
        auto idxIt = index_.find(hash);
        if (idxIt == index_.end()) return;
        lruList_.erase(idxIt->second);
        index_.erase(idxIt);
    }

    CacheStats stats() const {
        CacheStats s;
        s.hits = hits_;
        s.misses = misses_;
        long long total = hits_ + misses_;
        s.hitRate = total > 0 ? static_cast<double>(hits_) / static_cast<double>(total) : 0.0;
        return s;
    }

    size_t size() const { return lruList_.size(); }

private:
    struct CacheEntry {
        uint64_t hash;
        std::string name;
        void* entryPtr;
    };

    std::list<CacheEntry> lruList_;
    std::unordered_map<uint64_t, typename std::list<CacheEntry>::iterator> index_;
    long long hits_ = 0;
    long long misses_ = 0;
    bool enabled_ = true;
};

} // namespace budgetsym
