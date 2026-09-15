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
    // P0-4: last resolve()-epoch (see SymTabV2::epoch_) this slot was hit on.
    // Only meaningful for demotion eligibility (see maybeDemote()); a slot
    // can only become demotion-eligible via wasPromoted=true, and promotion
    // itself only happens from inside resolve(), which always sets
    // lastAccessEpoch in that same call -- so a promoted-but-unstamped epoch
    // cannot occur.
    uint32_t lastAccessEpoch = 0;
    uint16_t nameLen = 0;
    Rep representation = Rep::INLINE_REP;
    bool live = false;
    // P0-4: true iff this slot's CURRENT representation is INTERNED because
    // maybePromote() promoted it from COMPRESSED (not because decide() chose
    // INTERNED at insert time). Demotion eligibility is gated on this flag
    // so a name decide() legitimately routed to INTERNED (below
    // inlineMaxLen/compressMinLen, or an exact repeat) is never force-
    // demoted into COMPRESSED against the policy that put it there.
    bool wasPromoted = false;
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
    uint32_t find(uint32_t fp, Eq eq, bool disableFp = false) const {
        size_t pos = fp & mask_;
        uint32_t dist = 0;
        for (;;) {
            const Slot& s = slots_[pos];
            if (!s.occupied || dist > s.dist) return UINT32_MAX;
            if ((disableFp || s.fp == fp) && eq(s.slotId)) return s.slotId;
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
    // P0-2: an 8-bit fingerprint of the FULL name this member represents,
    // derived from different hash bits than ScopeIndex's own 32-bit
    // fingerprint (see fingerprint8() below) so the two checks are not
    // fully redundant. Checked in nameEquals() BEFORE reconstructMember()
    // is called, so a same-ScopeIndex-fingerprint, different-name candidate
    // can often be rejected without walking/decoding this block member at
    // all. With ~268K unique names (Zephyr-scale), the 32-bit ScopeIndex
    // fingerprint alone has an expected ~8.4 real collisions (birthday
    // bound: n^2/(2*2^32)); this second, independent 8-bit check catches
    // the ones that would otherwise force a full decode to reject.
    uint8_t fp8 = 0;
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

// sharedPrefixLen byte + isAnchor flag (packed estimate) + fp8 byte (P0-2,
// the intra-block fingerprint -- charged, not hidden, per its own byte cost).
static const long long kCompressedMemberOverhead = 3;

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
    // P0-3 (V2 research-rewrite plan): a COMPRESSED entry whose accessCount
    // (incremented on every resolve() hit -- see resolve() below) reaches
    // this threshold is promoted to INTERNED in place, trading its
    // compression saving for O(1)-ish lookup (no block decode). Matches
    // V1's identical mechanism and default (budget_sym.hpp's
    // hotAccessThreshold). accessCount is ONLY ever incremented by past
    // resolve() calls -- this is legitimate online information, never a
    // future-access oracle (see maybePromote()'s comment for the oracle/
    // online distinction).
    size_t hotAccessThreshold = 3;
    // P0-4: an INTERNED entry that WAS PROMOTED from COMPRESSED (never a
    // decide()-native INTERNED entry -- see PackedEntry::wasPromoted) is
    // eligible for demotion back to COMPRESSED once this many resolve()
    // epochs (SymTabV2::epoch_, incremented once per resolve() call --
    // online, observed-operation count, not wall-clock time) have passed
    // since its last hit. Demotion is NOT automatic on every resolve(); it
    // only runs when runMaintenance() is explicitly invoked (see that
    // method's comment for why an O(live-slot) scan is not folded into the
    // O(1) hot path). coldIdleEpochs == 0 disables demotion entirely
    // (matches V1/pre-P0-4 SymTabV2 behavior: promotion is one-directional).
    size_t coldIdleEpochs = 0;
    // Ablation study toggles (Section 14)
    bool disableFingerprints = false;
    bool disableScopeReclamation = false;
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
        // ECC review M4 fix: CompressedRef::slotInBlock is a uint16_t (see
        // its definition) -- insertCompressed() truncates b.members.size()
        // into it silently. A blockSize above 65535 would alias two
        // different members onto the same slotInBlock, corrupting
        // addressing. Not reachable by any configuration used anywhere in
        // this repository today, but nothing previously stopped a future
        // caller from doing it silently -- fail loudly instead.
        assert(cfg_.blockSize <= 65535 && "SymTabV2: blockSize must fit in CompressedRef::slotInBlock (uint16_t)");
        scopes_.emplace_back();
        // ECC review H1 fix: charge the initial (global) scope's ScopeIndex
        // allocation -- see the identical comment on enterScope() below.
        tracker_.add(scopes_.back().index.byteFootprint());
    }

    int enterScope() {
        scopes_.emplace_back();
        // ECC review H1 fix: ScopeIndex's constructor allocates a real,
        // fixed-size backing vector (default 16 slots) the moment a Scope is
        // default-constructed -- charge it immediately so every open scope's
        // index memory is represented in tracker_.current(), not just the
        // per-insert growth deltas charged in insert() below.
        tracker_.add(scopes_.back().index.byteFootprint());
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
        if (cfg_.disableScopeReclamation) {
            long long footprint = scopes_.back().index.byteFootprint();
            tracker_.reclaim(footprint);
            scopes_.pop_back();
            return rep;
        }
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
        // ECC review H1 fix: reclaim exactly what this scope's ScopeIndex
        // was charged (its current byteFootprint(), which already reflects
        // every growth doubling charged incrementally in insert() above) --
        // symmetric with the charge in enterScope()/the constructor.
        long long indexFootprint = s.index.byteFootprint();
        tracker_.reclaim(indexFootprint);
        rep.bytesReclaimed += indexFootprint;
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
        // ECC review C1 fix: a reused (free-listed) slotId's PackedEntry may
        // carry state left over from a PREVIOUS, entirely unrelated
        // occupant -- allocSlot() intentionally does not zero the struct
        // (that would defeat the point of O(1) slot reuse), so EVERY field
        // that could influence behavior for the NEW occupant must be reset
        // here explicitly, not just the ones a given representation happens
        // to write via materialize(). Before this fix, wasPromoted and
        // lastAccessEpoch were left stale, which let a decide()-native
        // INTERNED entry that recycled a promoted-then-released slot get
        // misclassified as demotion-eligible from the instant it was
        // created (see results/ecc_review.md finding C1, and
        // test_slot_reuse_does_not_inherit_promotion_state below).
        e.scopeId = static_cast<uint32_t>(scopes_.size()) - 1;
        e.typeId = static_cast<uint32_t>(typeId);
        e.accessCount = 0;
        e.lastAccessEpoch = 0; // C1: stale value from a prior occupant must not leak into demotion eligibility
        assert(name.size() <= UINT16_MAX && "SymTabV2: identifier too long for uint16_t nameLen");
        e.nameLen = static_cast<uint16_t>(name.size());
        e.representation = rep;
        e.live = true;
        e.wasPromoted = false; // C1: only maybePromote() may ever set this true, never a stale carry-over
        // C1 audit ("any other stale per-entry fields"): the parallel
        // representation-specific arrays (poolIndexOf_/compressedRefOf_)
        // are indexed by the same slotId and can likewise hold a prior
        // occupant's reference. materialize() below only overwrites the ONE
        // array matching the NEW representation, so the other one is reset
        // to its sentinel/default here -- otherwise a slot that changes
        // representation across reuse (e.g. was COMPRESSED, now INLINE)
        // would carry a stale, potentially-since-reclaimed-and-reused
        // CompressedRef/pool index that no live code path reads today (see
        // ecc_review.md M2) but that a future reader must not be able to
        // observe as "valid-looking" garbage.
        poolIndexOf_[slotId] = UINT32_MAX;
        compressedRefOf_[slotId] = CompressedRef{};

        if (rep != Rep::INLINE_REP) {
            auto insertResult = everSeenRep_.insert(name);
            if (insertResult.second) { // actually new -- charge its real cost, don't hide it
                tracker_.add(static_cast<long long>(sizeof(std::string) + name.size() + 1 + kSetNodeOverhead));
            }
        }

        long long cost = materialize(slotId, e, name, rep);
        tracker_.add(cost);

        // ECC review H1 fix: ScopeIndex allocates real, growable backing
        // storage (ScopeIndex::byteFootprint()) that was previously never
        // charged to tracker_ at all (see results/ecc_review.md finding
        // H1) -- charge exactly the DELTA this insert()'s index growth (if
        // any) actually costs, so a scope that never grows its index pays
        // nothing extra here (its initial allocation is already charged
        // once, at scope-creation time -- see enterScope()/the constructor)
        // and a scope that doubles its index pays exactly that doubling,
        // no more, no less.
        long long footprintBefore = s.index.byteFootprint();
        s.index.insert(fp, slotId);
        long long footprintAfter = s.index.byteFootprint();
        if (footprintAfter != footprintBefore) tracker_.add(footprintAfter - footprintBefore);
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
        epoch_++; // P0-4: one online "tick" per lookup operation -- see PolicyConfigV2::coldIdleEpochs
        uint32_t fp = fingerprint(name);
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            uint32_t slotId = it->index.find(fp, [&](uint32_t id) { return nameEquals(id, name); }, cfg_.disableFingerprints);
            if (slotId != UINT32_MAX) {
                entries_[slotId].accessCount++;
                entries_[slotId].lastAccessEpoch = epoch_;
                maybePromote(slotId, name);
                return declIdOf_[slotId];
            }
        }
        return -1;
    }

    // P0-4: explicit, externally-triggered cold-demotion sweep (INTERNED,
    // previously-promoted entries idle for >= cfg_.coldIdleEpochs resolve()
    // calls are demoted back to COMPRESSED). NOT invoked automatically from
    // resolve()/insert(): it is an O(peak-concurrent-slot-count) scan (see
    // ECC review M3 correction below), and running it on every O(1) lookup
    // would silently turn every resolve() call into an O(n) operation --
    // exactly the kind of hidden cost CLAUDE_RESEARCH.md's benchmark-
    // artifact concerns are about. A real compiler (or this benchmark
    // harness) calls it at natural checkpoints -- e.g. once per N
    // declarations, or once per translation unit -- matching how a real
    // system would schedule non-critical-path maintenance work. Returns the
    // number of entries demoted in this call. A no-op if
    // cfg_.coldIdleEpochs == 0 (demotion disabled).
    //
    // ECC review M3 correction: this was previously described as an
    // "O(live-slot)" scan. That is inaccurate -- the loop below walks every
    // index in entries_ (live or dead, filtered internally), so its true
    // cost is O(entries_.size()) == O(peakSlotCount()), the historical
    // high-water mark of CONCURRENTLY live slots (see peakSlotCount()'s doc
    // comment), not the number of slots live at the moment this is called.
    // For a workload with a high peak but a low current live count (e.g.
    // right after a large batch of scopes exits), this scan costs far more
    // than "O(live-slot)" would suggest. Not yet benchmarked directly (see
    // results/demotion_experiment.csv for demotion's end-to-end effect,
    // which does NOT isolate runMaintenance()'s own wall-clock cost).
    size_t runMaintenance() {
        if (cfg_.coldIdleEpochs == 0) return 0;
        size_t demoted = 0;
        for (uint32_t slotId = 0; slotId < entries_.size(); slotId++) {
            PackedEntry& e = entries_[slotId];
            if (!e.live || !e.wasPromoted || e.representation != Rep::INTERNED_REP) continue;
            if (epoch_ - e.lastAccessEpoch < cfg_.coldIdleEpochs) continue;
            demote(slotId, e);
            demoted++;
        }
        return demoted;
    }

    bool lookup(const std::string& name) { return resolve(name) >= 0; }
    void recordAccess(const std::string& name) { resolve(name); }

    size_t size() const {
        size_t n = 0;
        for (auto& e : entries_) if (e.live) n++;
        return n;
    }

    // Number of COMPRESSED->INTERNED promotions that have happened so far
    // (see maybePromote()). Surfaced for benchmark/ablation tooling, same
    // role as V1's BudgetSym::promotions().
    size_t promotions() const { return promotions_; }

    // P0-4: number of COMPRESSED->INTERNED->COMPRESSED demotions performed
    // so far by runMaintenance(). Always 0 if cfg_.coldIdleEpochs == 0.
    size_t demotions() const { return demotions_; }

    // P0-3: total number of COMPRESSED-member reconstructions actually
    // performed (nameEquals() reaching past the length+fp8 cheap-rejection
    // checks), and the total front-coding steps walked across all of them.
    // reconstructionStepsTotal()/reconstructionCount() (when count > 0) is
    // the mean reconstruction depth -- see reconstructMember()'s comment.
    size_t reconstructionCount() const { return reconstructions_; }
    size_t reconstructionStepsTotal() const { return reconstructionSteps_; }

    // Current representation of the innermost live binding of `name`, or
    // Rep::INLINE_REP if absent (matches V1's representationOf() default-
    // on-absence behavior). Does NOT count as an access (no accessCount
    // bump, no promotion side effect) -- used by tests and analysis tooling
    // to observe promotion without perturbing the hot/cold policy state.
    //
    // ECC review H2 correction: the "without perturbing it" claim above is
    // narrower than it may read -- this call IS a real lookup for indexing
    // purposes (ScopeIndex::find() + nameEquals()), and for a COMPRESSED
    // candidate, nameEquals() DOES perform a genuine reconstructMember()
    // decode, which DOES increment reconstructionCount()/
    // reconstructionStepsTotal() (see reconstructMember()'s comment) exactly
    // as any other lookup of that name would. Only accessCount/promotion are
    // guaranteed untouched. A caller measuring reconstruction work must NOT
    // call representationOf() (or lookup()/resolve()) on a name inside the
    // region being measured unless that call is meant to count -- see
    // src/block_compression_sweep_main.cpp's fix for exactly this mistake
    // (results/ecc_review.md finding H2).
    Rep representationOf(const std::string& name) const {
        uint32_t fp = fingerprint(name);
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            uint32_t slotId = it->index.find(fp, [&](uint32_t id) { return nameEquals(id, name); });
            if (slotId != UINT32_MAX) return entries_[slotId].representation;
        }
        return Rep::INLINE_REP;
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

    // P0-2: a SECOND fingerprint for compressed block members, built from
    // different bits of the same 64-bit hash than fingerprint() above uses
    // (bits 16-23 here vs the full low/high-32-bit XOR there).
    //
    // ECC review M1 correction: this is NOT a statistically independent
    // check in the strict sense -- both values are deterministic functions
    // of the SAME 64-bit HashFn::hash(name) output, so a genuine 64-bit
    // hash collision (h1 == h2 for two different names) collides both
    // fingerprint() and fingerprint8() together, always. What this bit-slice
    // choice actually buys is narrower: a FINGERPRINT-level collision
    // (fingerprint(a) == fingerprint(b) despite hash(a) != hash(b), i.e. the
    // 64-bit hashes differ but their low32^high32 XOR happens to coincide)
    // does not imply fingerprint8(a) == fingerprint8(b), since fp8's source
    // bits are folded into fp's XOR differently than they'd need to be to
    // force agreement. That is still a real, useful rejection-rate
    // improvement (see BlockMember::fp8's collision-count comment) but it is
    // a weaker property than "independent checks" -- the true rejection
    // rate for this specific hash function (FnvHash by default, whose
    // avalanche is not uniform across bit positions) is asserted here, not
    // measured. Regardless of how correlated the two checks turn out to be,
    // CORRECTNESS never depends on their independence: nameEquals()'s
    // COMPRESSED branch always falls through to a full decode-and-compare
    // as the final arbiter (see below), so fp8 can only ever cause a safe
    // early-reject (both checks already agree a real fingerprint collision
    // exists before fp8 is even consulted), never a false accept.
    uint8_t fingerprint8(const std::string& name) const {
        uint64_t h = HashFn::hash(name);
        return static_cast<uint8_t>((h >> 16) & 0xFF);
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
            case Rep::COMPRESSED_REP: {
                // P0-2: reject on the block member's own 8-bit fingerprint
                // BEFORE paying for reconstructMember()'s decode -- see
                // BlockMember::fp8's comment for why this is checked in
                // addition to (not instead of) ScopeIndex's 32-bit
                // fingerprint. Exact correctness is unaffected either way:
                // this is a rejection-only fast path, and any candidate that
                // passes both cheap checks still gets the full decode-and-
                // compare below as the final arbiter.
                const CompressedRef& ref = compressedRefOf_[slotId];
                const BlockMember& m = blocks_[ref.blockIndex].members[ref.slotInBlock];
                if (m.fp8 != fingerprint8(name)) return false;
                return reconstructMember(ref) == name;
            }
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
        m.fp8 = fingerprint8(name);
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
    //
    // P0-3 parameter-study instrumentation: counts every call (a real decode
    // was actually paid for -- nameEquals() only reaches this after the free
    // length check and the fp8 cheap-rejection check both already passed) and
    // the number of front-coding steps walked forward from the nearest
    // anchor (0 for an anchor itself; up to anchorInterval-1 otherwise) --
    // exactly the "reconstruction operations" / "reconstruction depth"
    // metrics CLAUDE_RESEARCH.md's P0.3 block-compression measurement
    // requires. Mutable because this is a read-path counter on an otherwise
    // logically-const query (matches accessCount's mutation-through-const-
    // resolve() precedent elsewhere in this class).
    std::string reconstructMember(CompressedRef ref) const {
        const Block& b = blocks_[ref.blockIndex];
        const BlockMember& m = b.members[ref.slotInBlock];
        reconstructions_++;
        if (!m.isAnchor) {
            uint16_t start = ref.slotInBlock;
            while (!b.members[start].isAnchor) start--;
            reconstructionSteps_ += static_cast<size_t>(ref.slotInBlock - start);
        }
        return decodeFrom(b, ref.slotInBlock, m);
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

    // P0-3 (V2 research-rewrite plan): promotes a COMPRESSED entry to
    // INTERNED once its accessCount crosses cfg_.hotAccessThreshold,
    // trading its compression saving for O(1)-ish lookup (no block decode
    // on future hits). Mirrors V1's BudgetSym::maybePromote() exactly in
    // spirit, adapted to V2's slot/index model.
    //
    // ONLINE vs ORACLE (explicit, not implied): accessCount is incremented
    // ONLY by past resolve() calls (see resolve() above) -- this function
    // never looks at, or is given, any information about FUTURE accesses.
    // It is therefore legitimate online information, safe to use in a
    // production/runtime policy. A separate, explicitly-labeled oracle
    // experiment (pre-scanning a trace for true future access counts and
    // passing that in via accessFreqHint at insert time) is a DIFFERENT,
    // opt-in code path -- not this one -- and any such experiment's output
    // must be labeled info_source=oracle, never blended with this function's
    // online promotions.
    //
    // Correctness: does NOT touch the ScopeIndex -- fingerprint() and
    // fingerprint8() are computed purely from the NAME string, independent
    // of representation, so the index's (fp -> slotId) mapping and the
    // block-member fp8 rejection path both stay valid across a promotion
    // with no re-insertion needed. declIdOf_[slotId] and the slot id itself
    // are also untouched, so resolve()'s return value is unaffected by
    // promotion happening mid-call.
    void maybePromote(uint32_t slotId, const std::string& name) {
        PackedEntry& e = entries_[slotId];
        if (e.representation != Rep::COMPRESSED_REP) return;
        if (e.accessCount < cfg_.hotAccessThreshold) return;
        CompressedRef oldRef = compressedRefOf_[slotId];
        releaseBlockMember(oldRef); // may or may not physically reclaim yet -- see releaseBlockMember()'s comment
        compressedRefOf_[slotId] = CompressedRef{}; // ECC review M2 fix: don't leave a released, dangling ref behind
        e.representation = Rep::INTERNED_REP;
        e.wasPromoted = true; // P0-4: marks this INTERNED slot demotion-eligible, see runMaintenance()
        poolIndexOf_[slotId] = internName(name);
        // kSlotOverhead itself was already charged at insert time and is
        // charged identically for every representation (see costOf()), so
        // it is not touched here -- only the representation-specific extra
        // cost changes (block-member bytes, now possibly reclaimed, for
        // pool-string bytes, now charged via internName() above).
        promotions_++;
    }

    // P0-4: the inverse of maybePromote() -- demotes an idle, previously-
    // promoted INTERNED entry back to COMPRESSED. Only called from
    // runMaintenance() (never from the resolve()/insert() hot path -- see
    // that method's comment). Correctness argument mirrors maybePromote()'s:
    // fingerprint()/fingerprint8() depend only on the NAME, so the
    // ScopeIndex mapping and slotId/declId are untouched by a representation
    // change; the only state that moves is where the string's bytes live.
    void demote(uint32_t slotId, PackedEntry& e) {
        // Reconstruct the name from the pool BEFORE releasing the pool ref
        // (releasePoolRef may erase the last reference's bytes once
        // refcount hits 0 -- see releasePoolRef()'s comment on why the slot
        // itself is not compacted; the string content stays valid until
        // this function's own copy below regardless, but capturing it first
        // keeps the ordering obviously correct rather than relying on that).
        std::string name = pool_[poolIndexOf_[slotId]];
        releasePoolRef(poolIndexOf_[slotId]);
        poolIndexOf_[slotId] = UINT32_MAX;
        e.representation = Rep::COMPRESSED_REP;
        e.wasPromoted = false; // freshly re-materialized as COMPRESSED, no longer promotion-derived
        e.accessCount = 0;     // P0-4: reset the hot/cold counter so a demoted entry must re-earn promotion
        compressedRefOf_[slotId] = insertCompressed(name);
        demotions_++;
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
    // P0-3 parameter-study counters -- see reconstructMember()'s comment.
    mutable size_t reconstructions_ = 0;
    mutable size_t reconstructionSteps_ = 0;

    std::vector<Scope> scopes_;
    int nextDeclId_ = 0;
    size_t promotions_ = 0;
    size_t demotions_ = 0;
    // P0-4: online lookup-operation clock, incremented once per resolve()
    // call. Never set from, or influenced by, any future-looking information
    // -- see PolicyConfigV2::coldIdleEpochs and maybeDemote()'s comment.
    uint32_t epoch_ = 0;

    MemoryTracker tracker_;
    PolicyConfigV2 cfg_;
};

} // namespace v2
} // namespace budgetsym
