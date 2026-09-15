#pragma once
// BUDGET-SYM V2 -- scope-aware, locality-aware adaptive symbol table.
//
// Root-cause fix for the V1 finding (see the session's audit report):
// V1's `entries_` was an APPEND-ONLY std::vector<Entry> -- every insert()
// call pushed a new 80-byte Entry, even a same-scope redeclaration of an
// already-live name, and nothing was ever physically removed (only
// logically "tombstoned"). On the Zephyr corpus (3.7M insert() calls, 268K
// live unique names) that vector alone measured ~297MB of real heap -- 73%
// of BudgetSym's total measured footprint -- while only 268K entries were
// ever actually live at once.
//
// V2's storage is a FREE-LIST-BACKED SLOT ALLOCATOR instead:
//   - entries_ still lives in one std::vector<PackedEntry>, but a freed slot
//     (same-scope redeclaration, or scope exit) is pushed onto freeSlots_ and
//     REUSED by the next insert() before the vector ever grows. Vector size
//     therefore tracks the historical PEAK of live symbols, not the count of
//     insert() calls -- the actual bug this fixes.
//   - each scope keeps the list of slot ids it created; exitScope() frees
//     every one of them in one pass (O(live-in-scope), same complexity as
//     V1, but the freed slots are genuinely reusable afterward instead of
//     leaving dead nodes behind).
//
// The lookup index is a per-scope OPEN-ADDRESSING (robin-hood) table keyed
// by a 32-bit fingerprint, storing only a 4-byte slot id per occupied slot
// (not a full string, not a heap node per entry the way
// std::unordered_multimap chains its buckets). Unlike V1's
// RobinHoodSymbolTableT (which explicitly does not implement deletion --
// see robinhood_symbol_table.hpp -- because V1 only ever discarded whole
// scopes), V2's index DOES support single-key deletion (backward-shift, the
// standard robin-hood deletion algorithm), because a same-scope
// redeclaration must free exactly one key while the rest of that scope's
// table stays live and in active use for a potentially very long time (a
// flattened corpus stream never exits its one global scope).
//
// PRODUCTION-DEPLOYABLE representations so far in this file: INLINE (no
// allocation at all, packed inline byte array) and INTERNED (a pool bounded
// by UNIQUE name count, same idea as V1/InternedSymbolTable -- this was
// never the source of V1's measured bloat). The COMPRESSED (block front-
// coded) tier is the next slice of this redesign and is NOT implemented in
// this file yet -- decide() falls back to INTERNED for anything V1 would
// have marked COMPRESSED, clearly commented below, so this class already
// type-checks against the same PolicyConfig and can be benchmarked/compared
// today while the block-compression tier is built on top of it
// incrementally.
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "common.hpp"
#include "memory_tracker.hpp"

namespace budgetsym {
namespace v2 {

// Names longer than this cannot use the INLINE tier (no arena, no heap --
// the bytes live directly in the packed entry). decide() below only offers
// INLINE when name.size() < min(cfg_.inlineMaxLen, kInlineCap), so this cap
// is never silently violated.
static const size_t kInlineCap = 20;

enum class Rep : uint8_t { INLINE_REP = 0, INTERNED_REP = 1, COMPRESSED_REP = 2 };

// ---- packed, slot-indexed entry -------------------------------------------
// One per live-or-freed slot. sizeof ~= 40B (vs V1's Entry at a measured 80B,
// and V1 never reused a slot at all). No std::string member -- INLINE bytes
// are a fixed char array; INTERNED/COMPRESSED store a 4-byte reference into a
// parallel vector (poolIndexOf_ / compressedRefOf_) indexed by the same slot
// id, so the always-present PackedEntry stays small regardless of which
// representation a given slot uses.
struct PackedEntry {
    uint32_t scopeId = 0;
    uint32_t typeId = 0;
    uint32_t accessCount = 0;
    uint16_t nameLen = 0;
    Rep representation = Rep::INLINE_REP;
    bool live = false;
    char inlineBytes[kInlineCap]; // valid iff representation == INLINE_REP
};

// ---- per-scope open-addressing index over slot ids ------------------------
// Robin-hood probing (same algorithm as robinhood_symbol_table.hpp) but the
// stored value is a 4-byte slot id, not a full string + SymbolMeta, and --
// the key addition V1's RobinHood table explicitly omits -- single-key
// DELETION via standard backward-shift: on removing a slot, subsequent
// elements in its probe run are shifted back one position until an empty
// slot or a slot with probe distance 0 is reached, which is what lets a
// same-scope redeclaration free exactly the old key without disturbing any
// other live key's probe-early-exit invariant.
template <typename HashFn>
class ScopeIndex {
public:
    explicit ScopeIndex(size_t initialCapacity = 16) : slots_(initialCapacity), mask_(initialCapacity - 1) {
        // mask_ = capacity - 1 only wraps `pos = fp & mask_` correctly within
        // [0, slots_.size()) for a power-of-two capacity; a non-power-of-two
        // caller would silently index out of bounds. Always default-
        // constructed via Scope::index today, but landmine-proofed per
        // code review since this constructor is a public entry point.
        assert(initialCapacity > 0 && (initialCapacity & (initialCapacity - 1)) == 0 &&
               "ScopeIndex capacity must be a power of two");
    }

    // Inserts (fingerprint -> slotId). Caller guarantees no existing live key
    // has this exact (fingerprint, name) pair -- redeclaration handling
    // erases the old key first (see SymTabV2::insert()).
    void insert(uint32_t fp, uint32_t slotId) {
        if (static_cast<double>(count_ + 1) / static_cast<double>(slots_.size()) > kMaxLoad) grow();
        Slot incoming{fp, slotId, 0, true};
        size_t pos = fp & mask_;
        for (;;) {
            Slot& s = slots_[pos];
            if (!s.occupied) { s = incoming; count_++; return; }
            if (s.dist < incoming.dist) std::swap(s, incoming);
            incoming.dist++;
            pos = (pos + 1) & mask_;
        }
    }

    // Finds the slot id for (fp), verified by `eq(slotId)` (caller compares
    // the actual name via the arena/pool, since two different names can
    // share a fingerprint). Returns UINT32_MAX if absent.
    template <typename Eq>
    uint32_t find(uint32_t fp, Eq eq) const {
        size_t pos = fp & mask_;
        uint32_t dist = 0;
        for (;;) {
            const Slot& s = slots_[pos];
            if (!s.occupied || dist > s.dist) return UINT32_MAX;
            if (s.fp == fp && eq(s.slotId)) return s.slotId;
            pos = (pos + 1) & mask_;
            dist++;
        }
    }

    // Backward-shift deletion: removes the occupied slot whose (fp, slotId)
    // matches exactly (the caller already resolved which live entry to
    // remove). O(1) amortized, same as insert.
    void erase(uint32_t fp, uint32_t slotId) {
        size_t pos = fp & mask_;
        uint32_t dist = 0;
        for (;;) {
            Slot& s = slots_[pos];
            if (!s.occupied || dist > s.dist) return; // not present
            if (s.fp == fp && s.slotId == slotId) {
                size_t cur = pos;
                for (;;) {
                    size_t next = (cur + 1) & mask_;
                    Slot& nxt = slots_[next];
                    if (!nxt.occupied || nxt.dist == 0) { slots_[cur] = Slot{}; break; }
                    slots_[cur] = nxt;
                    slots_[cur].dist--;
                    cur = next;
                }
                count_--;
                return;
            }
            pos = (pos + 1) & mask_;
            dist++;
        }
    }

    size_t liveCount() const { return count_; }
    // Real bytes this index occupies (every slot, occupied or not, is a
    // fixed-size POD -- reported honestly rather than folded silently into
    // "tracked memory", same discipline V1's RobinHood table documents via
    // slackBytes()).
    long long byteFootprint() const { return static_cast<long long>(slots_.size() * sizeof(Slot)); }

private:
    static constexpr double kMaxLoad = 0.70;
    struct Slot {
        uint32_t fp = 0;
        uint32_t slotId = 0;
        uint32_t dist = 0;
        bool occupied = false;
    };
    std::vector<Slot> slots_;
    size_t mask_;
    size_t count_ = 0;

    void grow() {
        std::vector<Slot> old = std::move(slots_);
        slots_.assign(old.size() * 2, Slot());
        mask_ = slots_.size() - 1;
        count_ = 0;
        for (auto& s : old) if (s.occupied) insert(s.fp, s.slotId);
    }
};

// ---- block-based front-coded compression (P0-3) ---------------------------
// A block holds up to cfg.blockSize members in insertion order. Every
// cfg.anchorInterval-th member (always including member 0) is an ANCHOR --
// its `suffix` field holds the full string, not a front-coded fragment.
// Every other member front-codes against the member immediately before it
// IN THE SAME BLOCK. Because anchors recur at a fixed, known stride,
// reconstructing any member is DIRECT: walk back to
// `slot - (slot % anchorInterval)` (never more than anchorInterval-1 steps
// back), no linked prevIndex pointer chasing (V1's design) and no need to
// track "how far is the chain tail" at all.
//
// Reclaim is whole-block: individual members are never removed from a
// block's `members` vector (that would corrupt every later member's
// front-coding, since each one's suffix is only meaningful relative to its
// predecessor's full string). Instead each release decrements `liveCount`;
// only when liveCount reaches 0 (every member this block ever held is now
// dead) is the block's real string storage actually freed and its tracked
// bytes reclaimed in one step -- avoiding V1's documented "only the chain
// TAIL can be physically reclaimed, interior nodes leak until the next
// re-anchor" limitation entirely, at the cost of batching reclaim to
// whenever the LAST member of a block happens to die (which may be later
// than when most of its members die -- a real, honestly-reported trade-off,
// not hidden).
struct BlockMember {
    uint8_t sharedPrefixLen = 0; // shared with the previous member in this block; 0 for an anchor
    bool isAnchor = false;
    std::string suffix; // full string if isAnchor, else the non-shared remainder
};

struct Block {
    std::vector<BlockMember> members;
    uint32_t liveCount = 0;
    long long trackedBytes = 0; // sum of every member's charged cost, reclaimed in one step at liveCount==0
};

struct CompressedRef {
    uint32_t blockIndex = UINT32_MAX;
    uint16_t slotInBlock = 0;
};

static const long long kCompressedMemberOverhead = 2; // sharedPrefixLen byte + isAnchor flag (packed estimate)

} // namespace v2
} // namespace budgetsym

#include "hash_functions.hpp"

namespace budgetsym {
namespace v2 {

// Reduced policy for this first slice: only INLINE/INTERNED are actually
// implemented (see file header). Full thresholds (compressMinLen,
// pressure/prefix-similarity gates, hotAccessThreshold-driven promotion)
// return once the COMPRESSED block tier lands on top of this storage layer.
struct PolicyConfigV2 {
    size_t inlineMaxLen = 12;     // clamped to kInlineCap internally
    // P0-3: block-based front-coded compression. A name at or above this
    // length is compressed instead of interned. Names below inlineMaxLen
    // still go INLINE (checked first in decide()) regardless of this value.
    size_t compressMinLen = 14;
    // Entries per block -- the RECLAIM/whole-block-addressing granularity.
    // A block is only physically freed once every member in it is dead.
    size_t blockSize = 32;
    // Re-anchor (store a full string instead of front-coding) every N
    // members WITHIN a block. This decouples the reconstruction-cost bound
    // (anchorInterval -- reconstructMember() never walks more than this many
    // steps) from the reclaim granularity (blockSize), unlike V1's single
    // reanchorInterval knob which conflated both. Must be >= 1.
    size_t anchorInterval = 8;
};

// sizeof(PackedEntry): the REAL struct size (not a hand-picked guess like
// V1's 28/28/29-byte constants) -- this is what V2's memory model charges,
// so "modeled" and "measured" should track much more closely than V1's did.
// + sizeof(uint32_t) for the always-present poolIndexOf_ parallel-vector
// slot (allocated for every slot regardless of representation, so its cost
// is charged uniformly rather than only when a slot happens to be INTERNED).
template <typename HashFn = FnvHash>
class SymTabV2 {
public:
    explicit SymTabV2(size_t budgetBytes, PolicyConfigV2 cfg = PolicyConfigV2())
        : tracker_(budgetBytes), cfg_(cfg) {
        scopes_.emplace_back();
    }

    int enterScope() {
        scopes_.emplace_back();
        return static_cast<int>(scopes_.size()) - 1;
    }

    struct ScopeExitReport { size_t symbolsReleased = 0; long long bytesReclaimed = 0; };

    // Frees every slot this scope created in one pass. Unlike V1, the freed
    // slots are pushed onto freeSlots_ and become available for reuse by the
    // NEXT insert() anywhere in the table, instead of being left as
    // permanently-allocated dead vector entries.
    ScopeExitReport exitScope() {
        ScopeExitReport rep;
        if (scopes_.size() <= 1) return rep;
        uint32_t exitingScopeId = static_cast<uint32_t>(scopes_.size()) - 1;
        Scope& s = scopes_.back();
        for (uint32_t slotId : s.liveSlots) {
            PackedEntry& e = entries_[slotId];
            // A slot in this scope's liveSlots_ list can be in one of two
            // "not mine anymore" states, and both must be checked, not just
            // e.live:
            //  1. It was redeclared (and thus released+freed) earlier in
            //     this same still-open scope -- see insert() below, which
            //     does not remove the old slot id from liveSlots_ (that
            //     would require an O(n) scan). Here e.live is false.
            //  2. Defense-in-depth for a related hazard: if a freed slot
            //     could ever be handed by allocSlot() to an UNRELATED
            //     insert() in a different, still-open scope before this
            //     scope exits, e.live would be true again but e.scopeId
            //     would no longer belong to this scope, and checking e.live
            //     alone would incorrectly free the other scope's live
            //     symbol. Reviewed (ecc:cpp-reviewer) and empirically probed:
            //     with the CURRENT insert() ordering this cannot actually
            //     happen -- a redeclaration's releaseSlot() is always
            //     immediately followed, in the same insert() call with no
            //     intervening allocSlot(), by the allocSlot() that reclaims
            //     it, so freeSlots_ (a LIFO stack) never exposes that slot
            //     to any other scope's insert() in between. This check is
            //     therefore currently unreachable in practice, kept anyway
            //     because it is a one-line, zero-cost guard against the
            //     hazard being reintroduced by a future refactor (e.g. the
            //     upcoming block-compression tier batching releases, or
            //     reordering release-before-alloc) -- see
            //     tests/differential_test.cpp's
            //     test_exit_scope_does_not_free_slot_reused_by_inner_scope,
            //     whose docstring documents the same "currently unreachable,
            //     kept as a regression guard" status honestly.
            if (!e.live || e.scopeId != exitingScopeId) continue;
            long long freed = costOf(e);
            releaseSlot(slotId, e);
            rep.bytesReclaimed += freed;
            rep.symbolsReleased++;
        }
        scopes_.pop_back();
        return rep;
    }

    int insert(const std::string& name, int typeId = 0) {
        uint32_t fp = fingerprint(name);
        Scope& s = scopes_.back();

        // Same-scope redeclaration: erase+release the old slot FIRST (both
        // from the index and from live accounting) so the replacement is a
        // clean second insert, not a leaked duplicate -- this is the exact
        // bug class the V1 audit found across all five V1 tables.
        uint32_t existing = s.index.find(fp, [&](uint32_t id) { return nameEquals(id, name); });
        if (existing != UINT32_MAX) {
            PackedEntry& old = entries_[existing];
            s.index.erase(fp, existing);
            releaseSlot(existing, old);
        }

        Rep rep = decide(name);
        uint32_t slotId = allocSlot();
        PackedEntry& e = entries_[slotId];
        e.scopeId = static_cast<uint32_t>(scopes_.size()) - 1;
        e.typeId = static_cast<uint32_t>(typeId);
        e.accessCount = 0;
        assert(name.size() <= UINT16_MAX && "SymTabV2: identifier too long for uint16_t nameLen");
        e.nameLen = static_cast<uint16_t>(name.size());
        e.representation = rep;
        e.live = true;

        if (rep != Rep::INLINE_REP) {
            auto insertResult = everSeenRep_.insert(name);
            if (insertResult.second) { // actually new -- charge its real cost, don't hide it
                tracker_.add(static_cast<long long>(sizeof(std::string) + name.size() + 1 + kSetNodeOverhead));
            }
        }

        long long cost = materialize(slotId, e, name, rep);
        tracker_.add(cost);

        s.index.insert(fp, slotId);
        s.liveSlots.push_back(slotId);

        int declId = nextDeclId_++;
        declIdOf_[slotId] = declId;
        return declId;
    }

    // Declaration ordinal of the innermost live binding, or -1. Numbered
    // identically to every V1 table (0, 1, 2, ... per insert() call) so
    // results are directly comparable -- see tests/differential_test.cpp.
    // Non-const: resolve() mutates accessCount on every hit (the runtime
    // access-observation signal the future hot/cold tiering policy will read
    // from) -- intentional, matches the same design choice in budget_sym.hpp.
    int resolve(const std::string& name) {
        uint32_t fp = fingerprint(name);
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            uint32_t slotId = it->index.find(fp, [&](uint32_t id) { return nameEquals(id, name); });
            if (slotId != UINT32_MAX) {
                entries_[slotId].accessCount++;
                return declIdOf_[slotId];
            }
        }
        return -1;
    }

    bool lookup(const std::string& name) { return resolve(name) >= 0; }
    void recordAccess(const std::string& name) { resolve(name); }

    size_t size() const {
        size_t n = 0;
        for (auto& e : entries_) if (e.live) n++;
        return n;
    }

    const MemoryTracker& tracker() const { return tracker_; }

    // Peak slot-vector size ever reached (entries_.size(), which only grows
    // when the free list is empty) -- the direct counter-metric to V1's
    // append-only growth. Compare against declarations (nextDeclId_) to see
    // the compaction ratio the free list achieved.
    size_t peakSlotCount() const { return entries_.size(); }
    int declarationCount() const { return nextDeclId_; }

    // Real, honest structural cost per slot: sizeof(PackedEntry) (the actual
    // POD, not a hand-picked guess) + sizeof(uint32_t) for the always-
    // present poolIndexOf_ parallel-vector slot. Public (matches
    // ConventionalSymbolTable::kMetaOverhead's style) so tests/benchmarks
    // can assert exact tracked-byte conservation.
    static const long long kSlotOverhead = static_cast<long long>(sizeof(PackedEntry) + sizeof(uint32_t));
    // Estimated per-node overhead of std::unordered_set<std::string> (bucket
    // pointer + hash cache), same order of magnitude as V1's identical,
    // never-charged seen_ set -- charged here so it is not hidden.
    static const long long kSetNodeOverhead = 32;

private:
    struct Scope {
        ScopeIndex<HashFn> index;
        std::vector<uint32_t> liveSlots;
    };

    uint32_t allocSlot() {
        if (!freeSlots_.empty()) {
            uint32_t id = freeSlots_.back();
            freeSlots_.pop_back();
            return id;
        }
        entries_.emplace_back();
        declIdOf_.emplace_back();
        poolIndexOf_.emplace_back(UINT32_MAX);
        compressedRefOf_.emplace_back();
        return static_cast<uint32_t>(entries_.size()) - 1;
    }

    void releaseSlot(uint32_t slotId, PackedEntry& e) {
        long long freed = costOf(e);
        tracker_.reclaim(freed);
        if (e.representation == Rep::INTERNED_REP) {
            releasePoolRef(poolIndexOf_[slotId]);
        } else if (e.representation == Rep::COMPRESSED_REP) {
            releaseBlockMember(compressedRefOf_[slotId]);
        }
        e.live = false;
        freeSlots_.push_back(slotId);
    }

    long long costOf(const PackedEntry&) const { return kSlotOverhead; }

    long long materialize(uint32_t slotId, PackedEntry& e, const std::string& name, Rep rep) {
        if (rep == Rep::INLINE_REP) {
            std::memcpy(e.inlineBytes, name.data(), name.size());
            return kSlotOverhead;
        }
        if (rep == Rep::COMPRESSED_REP) {
            compressedRefOf_[slotId] = insertCompressed(name);
            return kSlotOverhead; // the block's own bytes are tracked/charged separately, see insertCompressed()
        }
        uint32_t idx = internName(name);
        poolIndexOf_[slotId] = idx;
        return kSlotOverhead;
    }

    uint32_t internName(const std::string& name) {
        auto it = poolLookup_.find(name);
        if (it != poolLookup_.end()) {
            uint32_t idx = it->second;
            // Bug found and fixed during P0-3 measurement (root-caused via a
            // targeted repro, not guessed): pool_[idx] is never physically
            // erased when the last reference drops (see the "not compacted"
            // comment on releasePoolRef() below -- indices must stay stable
            // while other refs may exist). But releasePoolRef() DOES reclaim
            // its bytes from the tracker at that point. So if the SAME
            // string is interned again later (refcount 0 -> 1), the pool
            // slot is reused for free-in-reality but the tracker was never
            // re-charged for it -- a real under-count, not merely a
            // theoretical one: confirmed via a repro derived from the
            // differential fuzz test (a name compressed, then released, then
              // re-interned via decide()'s repeat-detection rule) where
            // tracker() dropped BELOW size()*kSlotOverhead, an impossible
            // floor if every live entry's cost were correctly charged. Same
            // bug pattern exists in V1's InternedSymbolTable::internName()
            // and BudgetSym::internName() -- fixed there too in this same
            // session, see their comments.
            if (poolRefCount_[idx] == 0) {
                tracker_.add(static_cast<long long>(sizeof(std::string) + pool_[idx].size() + 1));
            }
            poolRefCount_[idx]++;
            return idx;
        }
        uint32_t idx = static_cast<uint32_t>(pool_.size());
        pool_.push_back(name);
        poolRefCount_.push_back(1);
        poolLookup_.emplace(name, idx);
        tracker_.add(static_cast<long long>(sizeof(std::string) + name.size() + 1));
        return idx;
    }

    void releasePoolRef(uint32_t idx) {
        if (idx == UINT32_MAX) return;
        poolRefCount_[idx]--;
        if (poolRefCount_[idx] == 0) {
            tracker_.reclaim(static_cast<long long>(sizeof(std::string) + pool_[idx].size() + 1));
            // pool_[idx] slot itself is not compacted -- matches V1/
            // InternedSymbolTable's documented trade-off (indices must stay
            // stable while other refs may exist). Bounded by UNIQUE name
            // count, never the source of V1's measured bloat.
        }
    }

    // isRepeat: has THIS EXACT STRING ever been assigned a representation
    // before (in any scope, at any point in this table's lifetime)? Mirrors
    // V1's identical rule (budget_sym.hpp's decide(), "Exact repeats:
    // interning is essentially free memory-wise... so it always wins over
    // paying for a second full or compressed copy of the same string") --
    // and for a good, measured reason, not just because V1 did it: without
    // this check, a long name that recurs many times (e.g. a corpus token
    // stream, where names repeat ~14x on average) would pay a FULL new
    // COMPRESSED-block cost on every single occurrence, since block
    // compression has no built-in deduplication the way the interned pool
    // does. Confirmed empirically: enabling length-only COMPRESSED routing
    // (no repeat check) made SymTabV2 WORSE than Conventional's measured
    // heap on every corpus (FreeRTOS went from 6.4x to 7.2x); adding this
    // check is what makes compression a net win. everSeenRep_ is bounded by
    // UNIQUE name count (same bound as pool_/poolLookup_, not the
    // append-only-vector class of bug this whole redesign targets).
    Rep decide(const std::string& name) const {
        size_t cap = cfg_.inlineMaxLen < kInlineCap ? cfg_.inlineMaxLen : kInlineCap;
        if (name.size() < cap) return Rep::INLINE_REP;
        if (everSeenRep_.find(name) != everSeenRep_.end()) return Rep::INTERNED_REP;
        if (name.size() >= cfg_.compressMinLen) return Rep::COMPRESSED_REP;
        return Rep::INTERNED_REP;
    }

    uint32_t fingerprint(const std::string& name) const {
        uint64_t h = HashFn::hash(name);
        return static_cast<uint32_t>(h ^ (h >> 32));
    }

    bool nameEquals(uint32_t slotId, const std::string& name) const {
        const PackedEntry& e = entries_[slotId];
        // Length check first, for EVERY representation -- a free rejection
        // that needs no reconstruction at all (part of P0-4's "reject
        // candidates without full reconstruction where possible": ScopeIndex
        // already rejects on fingerprint mismatch before this is even
        // called; this rejects same-fingerprint, different-length
        // candidates before paying for a COMPRESSED block walk).
        if (e.nameLen != name.size()) return false;
        switch (e.representation) {
            case Rep::INLINE_REP:
                return std::memcmp(e.inlineBytes, name.data(), name.size()) == 0;
            case Rep::INTERNED_REP:
                return pool_[poolIndexOf_[slotId]] == name;
            case Rep::COMPRESSED_REP:
                return reconstructMember(compressedRefOf_[slotId]) == name;
        }
        return false;
    }

    // ---- block-based compression (P0-3) -----------------------------------
    CompressedRef insertCompressed(const std::string& name) {
        if (openBlock_ == UINT32_MAX || blocks_[openBlock_].members.size() >= cfg_.blockSize) {
            openBlock_ = allocBlock();
        }
        Block& b = blocks_[openBlock_];
        uint16_t slot = static_cast<uint16_t>(b.members.size());
        BlockMember m;
        size_t anchorEvery = cfg_.anchorInterval == 0 ? 1 : cfg_.anchorInterval;
        if (slot % anchorEvery == 0) {
            m.isAnchor = true;
            m.suffix = name;
        } else {
            const BlockMember& prev = b.members[slot - 1];
            std::string prevFull = decodeFrom(b, slot - 1, prev);
            size_t shared = commonPrefixLen(prevFull, name);
            if (shared > 255) shared = 255;
            m.isAnchor = false;
            m.sharedPrefixLen = static_cast<uint8_t>(shared);
            m.suffix = name.substr(shared);
        }
        long long cost = static_cast<long long>(m.suffix.size()) + kCompressedMemberOverhead;
        b.members.push_back(std::move(m));
        b.liveCount++;
        b.trackedBytes += cost;
        tracker_.add(cost);
        return CompressedRef{openBlock_, slot};
    }

    // Bounded reconstruction: walks back at most (slotInBlock % anchorInterval)
    // steps to the nearest anchor within this block, then decodes forward --
    // never a chain of unbounded depth the way V1's prevIndex-linked
    // COMPRESSED chain could be.
    std::string reconstructMember(CompressedRef ref) const {
        const Block& b = blocks_[ref.blockIndex];
        return decodeFrom(b, ref.slotInBlock, b.members[ref.slotInBlock]);
    }

    // Decodes member `slot` of block `b`, given its own record `m` (caller
    // already has it, avoids a redundant vector index in insertCompressed()'s
    // hot path). Not memoized in this slice -- see docs on reconstruction
    // count as a measured metric; a bounded per-block memo is a natural P1
    // follow-up once the latency evaluation (P0-7) identifies whether it's
    // actually needed.
    static std::string decodeFrom(const Block& b, uint16_t slot, const BlockMember& m) {
        if (m.isAnchor) return m.suffix;
        // Walk back to the nearest preceding anchor (guaranteed to exist at
        // or before slot 0 of this block, since member 0 is always an
        // anchor), then decode forward.
        uint16_t start = slot;
        while (!b.members[start].isAnchor) start--;
        std::string cur = b.members[start].suffix;
        for (uint16_t i = start + 1; i <= slot; i++) {
            const BlockMember& mi = b.members[i];
            cur = cur.substr(0, mi.sharedPrefixLen) + mi.suffix;
        }
        return cur;
    }

    void releaseBlockMember(CompressedRef ref) {
        if (ref.blockIndex == UINT32_MAX) return;
        Block& b = blocks_[ref.blockIndex];
        b.liveCount--;
        if (b.liveCount == 0) {
            tracker_.reclaim(b.trackedBytes);
            b.members.clear();
            b.members.shrink_to_fit(); // actually release the strings' heap storage now
            b.trackedBytes = 0;
            if (openBlock_ == ref.blockIndex) openBlock_ = UINT32_MAX; // don't keep appending to a freed block
            blockFreeList_.push_back(ref.blockIndex);
        }
    }

    uint32_t allocBlock() {
        if (!blockFreeList_.empty()) {
            uint32_t id = blockFreeList_.back();
            blockFreeList_.pop_back();
            return id;
        }
        blocks_.emplace_back();
        return static_cast<uint32_t>(blocks_.size()) - 1;
    }

    std::vector<PackedEntry> entries_;
    std::vector<uint32_t> freeSlots_;
    std::vector<int> declIdOf_;
    std::vector<uint32_t> poolIndexOf_;
    std::vector<CompressedRef> compressedRefOf_;

    std::vector<std::string> pool_;
    std::vector<uint32_t> poolRefCount_;
    std::unordered_map<std::string, uint32_t> poolLookup_;

    // Repeat-detection registry for decide() -- see its comment above. A
    // real, bounded (unique-name-count) cost, charged so it is not hidden
    // from the memory model the way V1's equivalent `seen_` set was never
    // charged at all.
    std::unordered_set<std::string> everSeenRep_;

    std::vector<Block> blocks_;
    std::vector<uint32_t> blockFreeList_;
    uint32_t openBlock_ = UINT32_MAX;

    std::vector<Scope> scopes_;
    int nextDeclId_ = 0;

    MemoryTracker tracker_;
    PolicyConfigV2 cfg_;
};

} // namespace v2
} // namespace budgetsym
