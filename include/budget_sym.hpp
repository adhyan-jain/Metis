#pragma once
// BUDGET-SYM -- the proposed adaptive scope-aware symbol table.
//
// Unlike the two baselines, BUDGET-SYM cannot key its lookup structure by the
// identifier string itself (that would force every symbol to pay for a full
// string copy regardless of representation, defeating the entire point). So
// each scope instead maps a fast 64-bit hash of the name to candidate entry
// ids; on lookup, each candidate's identifier is reconstructed from whatever
// representation it actually uses and compared to the query string. This is
// the direct cause of COMPRESSED's slower lookup relative to INLINE/INTERNED
// (see docs/faculty_questions.md, "Does compression hurt lookup?").
//
// Three representations, chosen per-symbol by decide():
//   INLINE     -- short string stored directly on the entry (cheapest lookup, no sharing)
//   INTERNED   -- index into a shared, refcounted string pool (used for exact repeats)
//   COMPRESSED -- front-coded (prefix-shared) against the previous COMPRESSED
//                 identifier in insertion order, re-anchored every
//                 cfg.reanchorInterval inserts to bound decode chain depth
//
// The policy also *adapts after insertion*: recordAccess()/lookup() promote a
// COMPRESSED entry to INTERNED once its access count crosses hotAccessThreshold,
// trading its memory saving for O(1)-ish lookup. This is the part of the
// mechanism that is genuinely adaptive over time, not just a one-shot decision
// made with only insert-time information.
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include "common.hpp"
#include "memory_tracker.hpp"
#include "lookup_cache.hpp"
#include "workload_profiler.hpp"
#include "predicted_thresholds.hpp"
#include "hash_functions.hpp"

namespace budgetsym {

// Templated on the hash function used for the index's first-stage lookup key
// (see hash_functions.hpp) -- purely additive on top of the Review-2 caching/
// ML-prediction work above: every existing caller keeps working unchanged
// via the `BudgetSym` alias at the bottom of this file (BudgetSymT<FnvHash>,
// i.e. today's original behavior). Orthogonal to LRULookupCache/
// WorkloadProfiler/ThresholdPredictor -- those operate on whatever hash this
// parameter produces, so nothing else in this class needs to change.
template <typename HashFn = FnvHash>
class BudgetSymT {
public:
    explicit BudgetSymT(size_t budgetBytes, PolicyConfig cfg = PolicyConfig())
        : tracker_(budgetBytes), cfg_(cfg) {
        scopes_.emplace_back();
    }

    int enterScope() {
        scopes_.emplace_back();
        return static_cast<int>(scopes_.size()) - 1;
    }

    struct ScopeExitReport { size_t symbolsReleased = 0; long long bytesReclaimed = 0; };

    ScopeExitReport exitScope() {
        ScopeExitReport rep;
        if (scopes_.size() <= 1) return rep;
        for (auto& kv : scopes_.back().hashIndex) {
            int id = kv.second;
            Entry& e = entries_[id];
            if (e.tombstoned) continue;
            long long freed = releaseEntry(e);
            e.tombstoned = true;
            lookupCache_.invalidate(kv.first);
            rep.bytesReclaimed += freed;
            rep.symbolsReleased++;
        }
        scopes_.pop_back();
        return rep;
    }

    // accessFreqHint: an optional caller-supplied hint (e.g. "this identifier
    // is a loop counter, expect heavy reuse") used only at decision time.
    // Real compilers rarely know true future access counts at declaration
    // time either -- this mirrors that constraint. Actual runtime access
    // (via lookup()/recordAccess()) is what drives the promotion adaptivity.
    int insert(const std::string& name, int typeId = 0, size_t accessFreqHint = 0) {
        int id = static_cast<int>(entries_.size());
        bool isRepeat = seen_.find(name) != seen_.end();
        seen_.insert(name);

        // Similarity signal for the *policy decision* is "does this identifier
        // share a long prefix with whatever was inserted immediately before
        // it" (e.g. a run of temperatureSensorX/Y/Z declarations) -- deliberately
        // independent of the *physical* front-coding chain (insertCompressed()
        // below always front-codes against the true previous COMPRESSED entry,
        // whatever that was). Comparing only against the last COMPRESSED entry
        // here would mean nothing could ever become the chain's first entry.
        size_t prefixShared = lastInsertedFull_.empty() ? 0 : commonPrefixLen(lastInsertedFull_, name);
        Representation rep = decide(name, isRepeat, tracker_.pressure(), accessFreqHint, prefixShared);
        lastInsertedFull_ = name;

        Entry e;
        e.meta.id = id;
        e.meta.scopeId = static_cast<int>(scopes_.size()) - 1;
        e.meta.typeId = typeId;
        e.meta.representation = rep;
        e.meta.accessCount = accessFreqHint;

        long long cost = materialize(e, name, rep);
        tracker_.add(cost);

        uint64_t h = HashFn::hash(name);
        // Same-scope redeclaration must replace the existing live binding,
        // not add a second one alongside it -- otherwise this scope's
        // hashIndex holds two live entries for one name (an internal
        // shadow-of-self), lookup()'s equal_range() walk finds whichever one
        // the multimap happens to iterate first (undefined which), and the
        // discarded entry's cost is never reclaimed. This mirrors
        // ConventionalSymbolTable/InternedSymbolTable's operator[] overwrite
        // semantics: this scope's *previous* binding for `name` (if live) is
        // tombstoned and its cost reclaimed before the new entry is charged.
        {
            auto& idx = scopes_.back().hashIndex;
            auto range = idx.equal_range(h);
            for (auto it = range.first; it != range.second; ++it) {
                Entry& old = entries_[it->second];
                if (!old.tombstoned && reconstructName(old) == name) {
                    releaseEntry(old);
                    old.tombstoned = true;
                    // Actually ERASE the multimap node here, not just mark
                    // tombstoned=true. exitScope() can afford to leave
                    // tombstoned nodes in place because the whole scope's
                    // hashIndex is destroyed a moment later anyway -- but a
                    // same-scope redeclaration happens while this scope stays
                    // open and this exact code path runs again on every
                    // future redeclaration of the same name. Leaving the node
                    // in place made a name redeclared k times in one scope
                    // (e.g. a loop counter reused across many statements, or
                    // a token appearing thousands of times in a flattened
                    // corpus stream) accumulate k dead nodes in this bucket,
                    // so equal_range() on a hot name became O(k) instead of
                    // O(1) -- confirmed as a real, severe slowdown on the
                    // Zephyr corpus benchmark (3.7M tokens; some tokens like
                    // "the"/"define" repeat tens of thousands of times in one
                    // scope) before this fix.
                    idx.erase(it);
                    break; // at most one live same-scope binding can exist per name
                }
            }
        }

        entries_.push_back(e);
        // Correctness fix: a new declaration sharing this hash -- most often
        // the exact same name shadowing an outer-scope symbol, or the
        // same-scope redeclaration just tombstoned above -- must not leave a
        // stale (hash, name) -> old-id mapping in the lookup cache. Without
        // this, lookup()'s cache fast path resolves to the WRONG (stale)
        // entry: it checks (hash, name) equality, both of which are
        // identical here, and has no scope-awareness at all. Confirmed
        // empirically before this fix -- a nested-scope shadow of "x" caused
        // the OUTER "x" to receive accessCount increments and even get
        // incorrectly promoted while the inner "x" was the live, correct
        // match. exitScope()'s own invalidate() call does not cover this
        // case: it only fires when a scope closes, not when a new
        // declaration opens one.
        lookupCache_.invalidate(h);
        scopes_.back().hashIndex.insert(std::make_pair(h, id));

        if (!profiler_.isComplete()) {
            profiler_.observe(name, e.meta.scopeId, isRepeat);
            // Fires exactly once, on the insert() call that pushes
            // sampleCount past sampleTarget (default 100). Skipped under the
            // ablation switch -- overwriting cfg_ mid-run would silently
            // defeat disableAdaptiveSelection's fixed-representation intent.
            if (profiler_.isComplete() && !cfg_.disableAdaptiveSelection && !cfg_.disableMLThresholdPrediction) {
                WorkloadFeatures features = profiler_.extract();
                cfg_ = ThresholdPredictor::predict(features);
            }
        }
        return id;
    }

    const WorkloadProfiler& profiler() const { return profiler_; }

    // See LRULookupCache::setEnabled() -- Review-2 benchmark/paper hook for
    // isolating the lookup cache's latency effect (Section VIII-E).
    void setLookupCacheEnabled(bool e) { lookupCache_.setEnabled(e); }

    // resolve() is the single implementation of the cache-check + hash-bucket
    // walk + promote + cache-populate path; lookup() below is a thin wrapper.
    // (Previously these were two independently maintained copies of the same
    // logic -- a code-review finding: a future change to one, e.g. a
    // promotion-policy tweak or another cache-invalidation fix, could
    // silently diverge from the other.)
    bool lookup(const std::string& name) { return resolve(name) >= 0; }

    void recordAccess(const std::string& name) { lookup(name); } // lookup already counts + promotes

    Representation representationOf(const std::string& name) {
        uint64_t h = HashFn::hash(name);
        for (auto sIt = scopes_.rbegin(); sIt != scopes_.rend(); ++sIt) {
            auto range = sIt->hashIndex.equal_range(h);
            for (auto it = range.first; it != range.second; ++it) {
                Entry& e = entries_[it->second];
                if (!e.tombstoned && reconstructName(e) == name) return e.meta.representation;
            }
        }
        return Representation::INLINE_REP;
    }

    // Declaration ordinal of the innermost live binding of `name`, or -1.
    // See ConventionalSymbolTable::resolve() -- ids are comparable across
    // implementations because every insert() call numbers declarations
    // identically (0, 1, 2, ...) regardless of the table type.
    int resolve(const std::string& name) {
        uint64_t h = HashFn::hash(name);

        void* cachedPtr = nullptr;
        if (lookupCache_.get(h, name, cachedPtr)) {
            int id = idFromCachePtr(cachedPtr);
            Entry& e = entries_[id];
            if (!e.tombstoned) {
                e.meta.accessCount++;
                maybePromote(e, name);
                return e.meta.id;
            }
        }

        for (auto sIt = scopes_.rbegin(); sIt != scopes_.rend(); ++sIt) {
            auto range = sIt->hashIndex.equal_range(h);
            for (auto it = range.first; it != range.second; ++it) {
                Entry& e = entries_[it->second];
                if (e.tombstoned) continue;
                if (reconstructName(e) == name) {
                    e.meta.accessCount++;
                    maybePromote(e, name);
                    lookupCache_.put(h, name, cachePtrFromId(it->second));
                    return e.meta.id;
                }
            }
        }
        return -1;
    }

    size_t size() const {
        size_t n = 0;
        for (auto& e : entries_) if (!e.tombstoned) n++;
        return n;
    }

    const MemoryTracker& tracker() const { return tracker_; }
    const PolicyConfig& config() const { return cfg_; }
    size_t promotions() const { return promotions_; }

    // Aggregate counters surfaced for the benchmark/analysis tooling -- see
    // include/lookup_cache.hpp's CacheStats for the lookup-cache half of this.
    struct Statistics {
        int memoHits = 0;         // reconstructFull() calls served from reconstructedCache
        int memoColdLookups = 0;  // reconstructFull() calls that walked the chain
        CacheStats lookupCache;   // LRU lookup-cache hit/miss/hitRate (see lookup_cache.hpp)
    };

    Statistics statistics() const {
        Statistics s;
        s.memoHits = static_cast<int>(memoHits_);
        s.memoColdLookups = static_cast<int>(memoColdLookups_);
        s.lookupCache = lookupCache_.stats();
        return s;
    }

    // Human-readable reason for the representation decide() made on the most
    // recent insert() call. Additive, non-breaking: existing callers that
    // never look at this are unaffected. Used by the web dashboard's Symbol
    // Table Explorer to show real reasoning instead of a guess -- see
    // src/analyze_main.cpp, which reads this immediately after each insert().
    const std::string& lastDecisionReason() const { return lastDecisionReason_; }

private:
    // entries_ is a std::vector<Entry>; push_back() can reallocate and
    // invalidate any raw Entry* taken earlier. So the LRU cache's opaque
    // void* payload holds the stable integer entry id (packed via intptr_t),
    // never a pointer into entries_ itself -- lookup() re-derives a live
    // reference with entries_[id] on every cache hit.
    static void* cachePtrFromId(int id) { return reinterpret_cast<void*>(static_cast<intptr_t>(id)); }
    static int idFromCachePtr(void* p) { return static_cast<int>(reinterpret_cast<intptr_t>(p)); }

    struct Entry {
        SymbolMeta meta;
        std::string inlineStr;   // valid when representation == INLINE_REP
        int poolIndex = -1;      // valid when representation == INTERNED_REP
        int compIndex = -1;      // valid when representation == COMPRESSED_REP
        bool tombstoned = false;
    };

    struct CompEntry {
        uint8_t sharedPrefixLen = 0;
        std::string suffix;
        int prevIndex = -1;
        std::string reconstructedCache; // empty = not yet memoized
    };

    struct Scope {
        std::unordered_multimap<uint64_t, int> hashIndex;
    };

    // ---- policy ----------------------------------------------------------
    Representation decide(const std::string& name, bool isRepeat, double pressure,
                           size_t accessFreqHint, size_t prefixShared) const {
        if (cfg_.disableAdaptiveSelection) {
            lastDecisionReason_ = "Adaptive selection disabled (ablation config) -- forced to a fixed representation.";
            return cfg_.fixedRepresentation;
        }

        bool hot = accessFreqHint >= cfg_.hotAccessThreshold;
        bool longId = name.size() >= cfg_.compressMinLen;
        bool prefixSimilar = prefixShared >= cfg_.prefixSimilarityMinShared;
        bool highPressure = pressure >= cfg_.highPressureThreshold;
        bool lowPressure = pressure < cfg_.lowPressureThreshold;

        // Exact repeats: interning is essentially free memory-wise (shared,
        // refcounted) and keeps O(1)-ish lookup, so it always wins over
        // paying for a second full or compressed copy of the same string.
        if (isRepeat) {
            lastDecisionReason_ = "Exact repeat of an identifier already seen -- interning shares "
                "the existing pool entry for free instead of paying for a second copy.";
            return Representation::INTERNED_REP;
        }

        // Under real memory pressure, prefer the compact representation for
        // anything long enough to be worth compressing -- unless it's hot,
        // in which case lookup speed matters more than the bytes saved.
        if (highPressure && longId && !hot) {
            lastDecisionReason_ = "Memory pressure is elevated (>= " +
                std::to_string(static_cast<int>(cfg_.highPressureThreshold * 100)) +
                "% of budget) and this identifier is long enough (>= " +
                std::to_string(cfg_.compressMinLen) + " chars) to be worth compressing.";
            return Representation::COMPRESSED_REP;
        }

        // Not under pressure, but this identifier front-codes well against
        // its predecessor and isn't hot: still worth compressing opportunistically.
        if (longId && prefixSimilar && !hot) {
            lastDecisionReason_ = "Shares a long prefix (>= " +
                std::to_string(cfg_.prefixSimilarityMinShared) +
                " chars) with the previously inserted identifier, and is long enough to "
                "front-code well -- compressing it opportunistically.";
            return Representation::COMPRESSED_REP;
        }

        // Short identifier, plenty of budget left: cheapest possible path,
        // no pool indirection needed.
        if (name.size() < cfg_.inlineMaxLen && lowPressure) {
            lastDecisionReason_ = "Short identifier (< " + std::to_string(cfg_.inlineMaxLen) +
                " chars) under low memory pressure -- cheapest path, no pool indirection needed.";
            return Representation::INLINE_REP;
        }

        // Default / fallback: balanced cost and lookup speed.
        lastDecisionReason_ = "No other rule applied -- default fallback, balanced cost and lookup speed.";
        return Representation::INTERNED_REP;
    }

    // Promote a COMPRESSED entry to INTERNED once it's been accessed enough
    // to count as "hot" -- trading its memory saving for faster lookup.
    // Caveat (documented, not hidden): if this entry's compPool_ slot is a
    // chain interior node with descendants, that slot's bytes cannot be
    // physically freed without breaking the decode chain for those
    // descendants, so the byte credit here only reflects the entry's own
    // logical accounting -- same fragmentation trade-off as scope-exit
    // tombstoning. See docs/future_work.md.
    void maybePromote(Entry& e, const std::string& name) {
        if (e.meta.representation != Representation::COMPRESSED_REP) return;
        if (e.meta.accessCount < cfg_.hotAccessThreshold) return;
        long long oldCost = costOf(e);
        int idx = internName(name);
        tracker_.reclaim(oldCost);
        e.poolIndex = idx;
        e.compIndex = -1;
        e.meta.representation = Representation::INTERNED_REP;
        long long newCost = costOf(e);
        tracker_.add(newCost);
        promotions_++;
    }

    long long materialize(Entry& e, const std::string& name, Representation rep) {
        switch (rep) {
            case Representation::INLINE_REP:
                e.inlineStr = name;
                return static_cast<long long>(name.size() + 1 + kInlineOverhead);
            case Representation::INTERNED_REP: {
                e.poolIndex = internName(name);
                return kInternedOverhead;
            }
            case Representation::COMPRESSED_REP: {
                e.compIndex = insertCompressed(name);
                const CompEntry& ce = compPool_[e.compIndex];
                return static_cast<long long>(1 + ce.suffix.size() + 1 + kCompressedLinkOverhead);
            }
        }
        return 0;
    }

    long long costOf(const Entry& e) const {
        switch (e.meta.representation) {
            case Representation::INLINE_REP:
                return static_cast<long long>(e.inlineStr.size() + 1 + kInlineOverhead);
            case Representation::INTERNED_REP:
                return kInternedOverhead;
            case Representation::COMPRESSED_REP: {
                const CompEntry& ce = compPool_[e.compIndex];
                return static_cast<long long>(1 + ce.suffix.size() + 1 + kCompressedLinkOverhead);
            }
        }
        return 0;
    }

    long long releaseEntry(Entry& e) {
        switch (e.meta.representation) {
            case Representation::INLINE_REP: {
                long long freed = static_cast<long long>(e.inlineStr.size() + 1 + kInlineOverhead);
                tracker_.reclaim(freed);
                return freed;
            }
            case Representation::INTERNED_REP: {
                long long freed = releasePoolRef(e.poolIndex) + kInternedOverhead;
                tracker_.reclaim(freed);
                return freed;
            }
            case Representation::COMPRESSED_REP: {
                // Only the chain *tail* (no descendants yet) can be physically
                // reclaimed; interior nodes are logically dropped (excluded
                // from lookup via tombstoned=true) but keep their bytes
                // allocated until the next re-anchor walks past them. This is
                // an explicit, documented limitation -- see docs/future_work.md.
                long long own = costOf(e);
                if (chainTail_ == e.compIndex) {
                    tracker_.reclaim(own);
                    chainTail_ = compPool_[e.compIndex].prevIndex;
                    return own;
                }
                return 0;
            }
        }
        return 0;
    }

    // Charges the tracker for the pool string's bytes exactly once, at the
    // moment a name is first interned (mirrors InternedSymbolTable's own
    // accounting) -- every subsequent internName() call for the same name
    // only bumps the refcount and costs nothing extra. Bug this fixes: an
    // earlier version left this uncharged entirely, silently making every
    // INTERNED_REP entry look cheaper than it actually is.
    //
    // Second bug, found and fixed via a symtab_v2.hpp repro (same pattern
    // exists here): pool_[idx] is never physically erased when the last
    // reference drops (releasePoolRef() only reclaims its bytes from the
    // tracker; the slot itself stays allocated -- "indices must stay stable
    // while other refs may exist"). So if this exact name is interned AGAIN
    // later (refcount 0 -> 1), reusing the existing pool slot must re-charge
    // those bytes, or the tracker silently under-counts relative to what's
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

    long long releasePoolRef(int idx) {
        poolRefCount_[idx]--;
        if (poolRefCount_[idx] == 0) {
            return static_cast<long long>(sizeof(std::string) + pool_[idx].size() + 1);
        }
        return 0;
    }

    // Internal recursive chain walk, memoizing every node it visits along the
    // way. Deliberately not the thing that increments memoHits_/memoColdLookups_
    // -- those track top-level reconstruction requests (see reconstructFull()
    // below), not the internal bookkeeping calls insertCompressed() makes to
    // compute a new entry's shared-prefix length against the current chain tail.
    std::string reconstructChain(int idx) const {
        CompEntry& ce = compPool_[idx];
        if (!ce.reconstructedCache.empty()) return ce.reconstructedCache;
        std::string result;
        if (ce.prevIndex == -1) {
            result = ce.suffix;
        } else {
            std::string prev = reconstructChain(ce.prevIndex);
            result = prev.substr(0, ce.sharedPrefixLen) + ce.suffix;
        }
        ce.reconstructedCache = result;
        return result;
    }

    // Top-level entry point used by the lookup path (reconstructName()). If
    // compPool_[idx].reconstructedCache is already populated, returns it
    // directly (memoHits_); otherwise performs the chain walk once and caches
    // the result (memoColdLookups_).
    std::string reconstructFull(int idx) const {
        CompEntry& ce = compPool_[idx];
        if (!ce.reconstructedCache.empty()) {
            memoHits_++;
            return ce.reconstructedCache;
        }
        memoColdLookups_++;
        return reconstructChain(idx);
    }

    std::string reconstructName(const Entry& e) const {
        switch (e.meta.representation) {
            case Representation::INLINE_REP:   return e.inlineStr;
            case Representation::INTERNED_REP: return pool_[e.poolIndex];
            case Representation::COMPRESSED_REP: return reconstructFull(e.compIndex);
        }
        return "";
    }

    int insertCompressed(const std::string& name) {
        bool reanchor = compPool_.empty() || (compressedInsertCount_ % cfg_.reanchorInterval == 0);
        compressedInsertCount_++;
        CompEntry ce;
        if (reanchor || chainTail_ == -1) {
            ce.sharedPrefixLen = 0;
            ce.suffix = name;
            ce.prevIndex = -1;
        } else {
            std::string prevFull = reconstructChain(chainTail_);
            size_t shared = commonPrefixLen(prevFull, name);
            if (shared > 255) shared = 255;
            ce.sharedPrefixLen = static_cast<uint8_t>(shared);
            ce.suffix = name.substr(shared);
            ce.prevIndex = chainTail_;
        }
        compPool_.push_back(ce);
        int idx = static_cast<int>(compPool_.size()) - 1;
        chainTail_ = idx;
        return idx;
    }

    static const long long kInlineOverhead = 28;          // SymbolMeta + std::string control block
    static const long long kInternedOverhead = 28;         // SymbolMeta + 4-byte pool index
    static const long long kCompressedLinkOverhead = 29;   // SymbolMeta + prevIndex(4) + misc chain bookkeeping

    std::vector<Entry> entries_;
    std::vector<Scope> scopes_;
    std::unordered_set<std::string> seen_;

    std::vector<std::string> pool_;
    std::vector<int> poolRefCount_;
    std::unordered_map<std::string, int> poolLookup_;

    mutable std::vector<CompEntry> compPool_;
    mutable size_t memoHits_ = 0;        // reconstructFull() calls served from reconstructedCache
    mutable size_t memoColdLookups_ = 0; // reconstructFull() calls that walked the chain
    int chainTail_ = -1;
    size_t compressedInsertCount_ = 0;
    std::string lastInsertedFull_; // most recent insert()'d name, any representation

    size_t promotions_ = 0;
    // Capacity was 64. Measured directly (see docs/latency.md): under a
    // cyclic/round-robin access pattern -- e.g. a loop that touches each of
    // a function's locals in turn every iteration, a realistic compiler
    // workload -- LRU thrashing is a hard cliff, not a gradual falloff. A
    // 150-symbol hot set got a *0% hit rate* at both 64 and 128 capacity
    // (every access evicts something needed again before it's reused) and a
    // 95% hit rate at 256 (matches the theoretical ceiling: one unavoidable
    // cold miss per symbol, then all hits). A 32-symbol hot set -- well
    // within either capacity -- showed no regression at 256 (same 95% hit
    // rate, latency same or marginally better). 256 is not a fix for
    // arbitrarily large hot sets -- the same cliff exists above 256 -- it is
    // an evidence-based increase to a size realistic compiler workloads
    // (a large function's locals) are less likely to exceed, at a memory
    // cost of roughly (256-64) cache entries per BudgetSym instance
    // (a few KB, not charged against the tracked-memory model -- see
    // Threats to Validity item 2).
    LRULookupCache<256> lookupCache_;
    WorkloadProfiler profiler_;
    MemoryTracker tracker_;
    PolicyConfig cfg_;
    mutable std::string lastDecisionReason_; // set by decide() (const); see lastDecisionReason()
};

// Default instantiation: identical behavior to the original, untemplated
// BudgetSym class (FNV-1a index hash). Every existing caller uses this alias
// and needed zero changes to keep compiling.
using BudgetSym = BudgetSymT<FnvHash>;
using BudgetSymMurmur3 = BudgetSymT<Murmur3Hash>;
using BudgetSymDjb2 = BudgetSymT<Djb2Hash>;

} // namespace budgetsym
