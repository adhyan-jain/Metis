#pragma once
// BUDGET-SYM V3 -- representation-aware metadata layout.
//
// V2's fixed per-slot cost (44B PackedEntry) is sized for its LARGEST
// payload variant (20-byte inline buffer) and charged to every symbol
// regardless of representation, and V2 additionally maintains a
// std::string-keyed liveSeenRep_ map purely for repeat/refcount tracking
// that duplicates the name a second (or third, for interned symbols) time
// in a live unordered_map<std::string,uint32_t> node.
//
// V3 keeps V2's algorithm (three representations, scope-lifetime
// reclamation, front-coded compression blocks) unchanged and applies two
// structural cuts, each independently verifiable from the struct layout:
//
// 1. PackedEntryV3 tightens scopeId/accessCount to uint16_t (max observed
//    scope depth across all 16 corpora is 15; access counts wrapping past
//    65535 is a documented, accepted approximation) and shrinks the inline
//    cap from 20B to 12B (matching PolicyConfigV2's own default
//    inlineMaxLen, i.e. no symbol that would ever be routed to
//    Rep::INLINE_REP needs more than 12 bytes of inline storage). This
//    drops sizeof(PackedEntryV3) from 44B to 32B -- every symbol, in every
//    representation, pays this smaller floor.
//
// 2. liveSeenRep_ is keyed on a 64-bit FnvHash fingerprint instead of a
//    copy of the name. This removes the second (interned symbols: third)
//    physical copy of the string payload and shrinks the map node from
//    sizeof(std::string)+cap+1+68 down to sizeof(uint64_t)+68. This is an
//    approximation: two different live symbol names that collide on the
//    64-bit fingerprint would be treated as the same entry for
//    repeat/refcount purposes. At realistic corpus sizes (10^5-10^6 live
//    unique symbols) the birthday-bound collision probability is
//    astronomically small (~n^2/2^65), but it is a real, disclosed
//    correctness relaxation, not a free lunch.
//
// Nothing else changes: same representation-decision policy, same
// interning pool, same front-coded block compression, same scope-exit
// physical reclamation. This isolates "does representation-aware/tighter
// fixed metadata move the Pareto frontier" from "did we change the
// algorithm."

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "common.hpp"
#include "memory_tracker.hpp"
#include "hash_functions.hpp"

namespace budgetsym {
namespace v3 {

static const size_t kInlineCap = 12;

enum class Rep : uint8_t { INLINE_REP = 0, INTERNED_REP = 1, COMPRESSED_REP = 2 };

struct CompressedRef {
    uint32_t blockIndex = UINT32_MAX;
    uint16_t slotInBlock = 0;
};

// ---- representation-aware packed entry: 32B vs V2's 44B --------------------
struct PackedEntry {
    int32_t declId = -1;
    uint16_t scopeId = 0;
    uint16_t accessCount = 0;
    uint32_t lastAccessEpoch = 0;
    uint16_t nameLen = 0;
    uint8_t typeId = 0;
    Rep representation = Rep::INLINE_REP;
    uint8_t flags = 0;  // bit0 = live, bit1 = wasPromoted

    union Payload {
        char inlineBytes[kInlineCap];
        uint32_t poolIndex;
        CompressedRef compressedRef;

        Payload() { std::memset(inlineBytes, 0, sizeof(inlineBytes)); }
    } payload;

    bool live() const { return flags & 0x1; }
    void setLive(bool v) { if (v) flags |= 0x1; else flags &= ~0x1; }
    bool wasPromoted() const { return flags & 0x2; }
    void setWasPromoted(bool v) { if (v) flags |= 0x2; else flags &= ~0x2; }
};

// ---- per-scope open-addressing index over slot ids (unchanged from V2) ----
template <typename HashFn>
class ScopeIndex {
public:
    explicit ScopeIndex(size_t initialCapacity = 16) : slots_(initialCapacity), mask_(initialCapacity - 1) {
        assert(initialCapacity > 0 && (initialCapacity & (initialCapacity - 1)) == 0 &&
               "ScopeIndex capacity must be a power of two");
    }

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

    void erase(uint32_t fp, uint32_t targetSlotId) {
        size_t pos = fp & mask_;
        uint32_t dist = 0;
        for (;;) {
            Slot& s = slots_[pos];
            if (!s.occupied || dist > s.dist) return;
            if (s.fp == fp && s.slotId == targetSlotId) {
                size_t nextPos = (pos + 1) & mask_;
                while (slots_[nextPos].occupied && slots_[nextPos].dist > 0) {
                    slots_[pos] = slots_[nextPos];
                    slots_[pos].dist--;
                    pos = nextPos;
                    nextPos = (pos + 1) & mask_;
                }
                slots_[pos] = Slot();
                count_--;
                return;
            }
            pos = (pos + 1) & mask_;
            dist++;
        }
    }

    size_t liveCount() const { return count_; }
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

// ---- block-based front-coded compression (unchanged from V2) --------------
struct BlockMember {
    std::string suffix;
    uint8_t sharedPrefixLen = 0;
    uint8_t fp8 = 0;
    bool isAnchor = false;
};

static const long long kCompressedMemberOverhead = sizeof(BlockMember);

struct Block {
    std::vector<BlockMember> members;
    uint32_t liveCount = 0;
    long long trackedBytes = 0;
};

struct PolicyConfigV3 {
    size_t inlineMaxLen = 12;
    size_t compressMinLen = 14;
    size_t blockSize = 32;
    size_t anchorInterval = 8;
    size_t hotAccessThreshold = 3;
    uint32_t coldIdleEpochs = 0;
    bool disableFingerprints = false;
    bool disableScopeReclamation = false;
};

// ---- main scope-aware adaptive symbol table (representation-aware V3) -----
template <typename HashFn = FnvHash>
class SymTabV3 {
public:
    explicit SymTabV3(size_t budgetBytes, PolicyConfigV3 cfg = PolicyConfigV3())
        : tracker_(budgetBytes), cfg_(cfg) {
        assert(cfg_.blockSize <= 65535 && "SymTabV3: blockSize must fit in CompressedRef::slotInBlock (uint16_t)");
        assert(cfg_.inlineMaxLen <= kInlineCap && "SymTabV3: inlineMaxLen must fit in the 12B inline buffer");
        scopes_.emplace_back();
        tracker_.add(scopes_.back().index.byteFootprint());
    }

    int enterScope() {
        assert(scopes_.size() < UINT16_MAX && "SymTabV3: scope nesting exceeds uint16_t scopeId range");
        scopes_.emplace_back();
        tracker_.add(scopes_.back().index.byteFootprint());
        return static_cast<int>(scopes_.size()) - 1;
    }

    struct ScopeExitReport { size_t symbolsReleased = 0; long long bytesReclaimed = 0; };

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
            if (!e.live() || e.scopeId != exitingScopeId) continue;
            long long freed = costOf(e);
            releaseSlot(slotId, e);
            rep.bytesReclaimed += freed;
            rep.symbolsReleased++;
        }
        long long indexFootprint = s.index.byteFootprint();
        tracker_.reclaim(indexFootprint);
        rep.bytesReclaimed += indexFootprint;
        scopes_.pop_back();

        if (freeSlots_.size() == entries_.size()) {
            entries_.clear();
            freeSlots_.clear();
            entries_.shrink_to_fit();
        }

        return rep;
    }

    int insert(const std::string& name, int typeId = 0) {
        uint32_t fp = fingerprint(name);
        Scope& s = scopes_.back();

        uint32_t existing = s.index.find(fp, [&](uint32_t id) { return nameEquals(id, name); });
        if (existing != UINT32_MAX) {
            PackedEntry& old = entries_[existing];
            s.index.erase(fp, existing);
            releaseSlot(existing, old);
        }

        uint64_t liveKey = fingerprint64(name);
        bool isRepeat = false;
        auto lit = liveSeenRep_.find(liveKey);
        if (lit != liveSeenRep_.end()) {
            isRepeat = true;
            lit->second++;
        } else {
            liveSeenRep_[liveKey] = 1;
            tracker_.add(static_cast<long long>(sizeof(uint64_t) + kMapNodeOverhead));
        }

        Rep rep = decide(name, isRepeat);
        uint32_t slotId = allocSlot();
        PackedEntry& e = entries_[slotId];

        int declId = nextDeclId_++;
        e.declId = declId;
        e.scopeId = static_cast<uint16_t>(scopes_.size() - 1);
        e.typeId = static_cast<uint8_t>(typeId);
        e.accessCount = 0;
        e.lastAccessEpoch = 0;
        assert(name.size() <= UINT16_MAX && "SymTabV3: identifier too long for uint16_t nameLen");
        e.nameLen = static_cast<uint16_t>(name.size());
        e.representation = rep;
        e.setLive(true);
        e.setWasPromoted(false);

        long long cost = materialize(e, name, rep);
        tracker_.add(cost);

        long long footprintBefore = s.index.byteFootprint();
        s.index.insert(fp, slotId);
        long long footprintAfter = s.index.byteFootprint();
        if (footprintAfter != footprintBefore) tracker_.add(footprintAfter - footprintBefore);
        s.liveSlots.push_back(slotId);

        return declId;
    }

    int resolve(const std::string& name) {
        epoch_++;
        uint32_t fp = fingerprint(name);
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            uint32_t slotId = it->index.find(fp, [&](uint32_t id) { return nameEquals(id, name); }, cfg_.disableFingerprints);
            if (slotId != UINT32_MAX) {
                if (entries_[slotId].representation == Rep::INLINE_REP) inlineLookups_++;
                else if (entries_[slotId].representation == Rep::INTERNED_REP) internedLookups_++;
                else if (entries_[slotId].representation == Rep::COMPRESSED_REP) compressedLookups_++;

                if (entries_[slotId].accessCount < UINT16_MAX) entries_[slotId].accessCount++;
                entries_[slotId].lastAccessEpoch = epoch_;
                maybePromote(slotId, name);
                return entries_[slotId].declId;
            }
        }
        return -1;
    }

    size_t runMaintenance() {
        if (cfg_.coldIdleEpochs == 0) return 0;
        size_t demoted = 0;
        for (uint32_t slotId = 0; slotId < entries_.size(); slotId++) {
            PackedEntry& e = entries_[slotId];
            if (!e.live() || !e.wasPromoted() || e.representation != Rep::INTERNED_REP) continue;
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
        for (auto& e : entries_) if (e.live()) n++;
        return n;
    }

    size_t promotions() const { return promotions_; }
    size_t demotions() const { return demotions_; }
    size_t reconstructionCount() const { return reconstructions_; }
    size_t reconstructionStepsTotal() const { return reconstructionSteps_; }
    size_t inlineLookups() const { return inlineLookups_; }
    size_t internedLookups() const { return internedLookups_; }
    size_t compressedLookups() const { return compressedLookups_; }

    Rep representationOf(const std::string& name) const {
        uint32_t fp = fingerprint(name);
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            uint32_t slotId = it->index.find(fp, [&](uint32_t id) { return nameEquals(id, name); });
            if (slotId != UINT32_MAX) return entries_[slotId].representation;
        }
        return Rep::INLINE_REP;
    }

    const MemoryTracker& tracker() const { return tracker_; }

    size_t peakSlotCount() const { return entries_.size(); }
    int declarationCount() const { return nextDeclId_; }

    static const long long kSlotOverhead = static_cast<long long>(sizeof(PackedEntry));
    static const long long kPoolNodeOverhead = 68;
    static const long long kMapNodeOverhead = 68;

protected:
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
        return static_cast<uint32_t>(entries_.size()) - 1;
    }

    void releaseSlot(uint32_t slotId, PackedEntry& e) {
        long long freed = costOf(e);
        tracker_.reclaim(freed);

        uint64_t liveKey = fingerprint64(nameOf(slotId));
        auto lit = liveSeenRep_.find(liveKey);
        if (lit != liveSeenRep_.end()) {
            if (lit->second > 0) lit->second--;
            if (lit->second == 0) {
                tracker_.reclaim(static_cast<long long>(sizeof(uint64_t) + kMapNodeOverhead));
                liveSeenRep_.erase(lit);
            }
        }

        if (e.representation == Rep::INTERNED_REP) {
            releasePoolRef(e.payload.poolIndex);
            e.payload.poolIndex = UINT32_MAX;
        } else if (e.representation == Rep::COMPRESSED_REP) {
            releaseBlockMember(e.payload.compressedRef);
            e.payload.compressedRef = CompressedRef{};
        }
        e.setLive(false);
        freeSlots_.push_back(slotId);
    }

    long long costOf(const PackedEntry&) const { return kSlotOverhead; }

    long long materialize(PackedEntry& e, const std::string& name, Rep rep) {
        if (rep == Rep::INLINE_REP) {
            std::memcpy(e.payload.inlineBytes, name.data(), name.size());
            return kSlotOverhead;
        }
        if (rep == Rep::COMPRESSED_REP) {
            e.payload.compressedRef = insertCompressed(name);
            return kSlotOverhead;
        }
        uint32_t idx = internName(name);
        e.payload.poolIndex = idx;
        return kSlotOverhead;
    }

    uint32_t internName(const std::string& name) {
        auto it = poolLookup_.find(name);
        if (it != poolLookup_.end()) {
            uint32_t idx = it->second;
            if (poolRefCount_[idx] == 0) {
                tracker_.add(static_cast<long long>(sizeof(std::string) + pool_[idx].capacity() + 1 + kPoolNodeOverhead));
            }
            poolRefCount_[idx]++;
            return idx;
        }
        uint32_t idx;
        if (!poolFreeList_.empty()) {
            idx = poolFreeList_.back();
            poolFreeList_.pop_back();
            pool_[idx] = name;
            poolRefCount_[idx] = 1;
        } else {
            idx = static_cast<uint32_t>(pool_.size());
            pool_.push_back(name);
            poolRefCount_.push_back(1);
        }
        poolLookup_.emplace(pool_[idx], idx);
        tracker_.add(static_cast<long long>(sizeof(std::string) + name.capacity() + 1 + kPoolNodeOverhead));
        return idx;
    }

    void releasePoolRef(uint32_t idx) {
        if (idx == UINT32_MAX || idx >= poolRefCount_.size()) return;
        if (poolRefCount_[idx] == 0) return;
        poolRefCount_[idx]--;
        if (poolRefCount_[idx] == 0) {
            const std::string& str = pool_[idx];
            tracker_.reclaim(static_cast<long long>(sizeof(std::string) + str.capacity() + 1 + kPoolNodeOverhead));
            auto it = poolLookup_.find(str);
            if (it != poolLookup_.end()) {
                poolLookup_.erase(it);
            }
            std::string().swap(pool_[idx]);
            poolFreeList_.push_back(idx);
        }
    }

    Rep decide(const std::string& name, bool isRepeat) const {
        size_t cap = cfg_.inlineMaxLen < kInlineCap ? cfg_.inlineMaxLen : kInlineCap;
        if (name.size() < cap) return Rep::INLINE_REP;
        if (isRepeat) return Rep::INTERNED_REP;
        if (name.size() >= cfg_.compressMinLen) return Rep::COMPRESSED_REP;
        return Rep::INTERNED_REP;
    }

    uint32_t fingerprint(const std::string& name) const {
        uint64_t h = HashFn::hash(name);
        return static_cast<uint32_t>(h ^ (h >> 32));
    }

    uint64_t fingerprint64(const std::string& name) const {
        return HashFn::hash(name);
    }

    uint8_t fingerprint8(const std::string& name) const {
        uint64_t h = HashFn::hash(name);
        return static_cast<uint8_t>((h >> 16) & 0xFF);
    }

    std::string nameOf(uint32_t slotId) const {
        const PackedEntry& e = entries_[slotId];
        if (e.representation == Rep::INLINE_REP) {
            return std::string(e.payload.inlineBytes, e.nameLen);
        }
        if (e.representation == Rep::INTERNED_REP) {
            return pool_[e.payload.poolIndex];
        }
        if (e.representation == Rep::COMPRESSED_REP) {
            return reconstructMember(e.payload.compressedRef);
        }
        return "";
    }

    static void decodeToBuffer(const Block& b, uint16_t slot, size_t anchorInterval, char* outBuf, size_t& outLen) {
        const BlockMember& m = b.members[slot];
        if (m.isAnchor) {
            std::memcpy(outBuf, m.suffix.data(), m.suffix.size());
            outLen = m.suffix.size();
            return;
        }
        size_t anchorEvery = anchorInterval == 0 ? 1 : anchorInterval;
        uint16_t start = static_cast<uint16_t>(slot - (slot % anchorEvery));
        const std::string& anchorStr = b.members[start].suffix;
        std::memcpy(outBuf, anchorStr.data(), anchorStr.size());
        outLen = anchorStr.size();
        for (uint16_t i = start + 1; i <= slot; i++) {
            const BlockMember& mi = b.members[i];
            size_t prefixLen = mi.sharedPrefixLen < outLen ? mi.sharedPrefixLen : outLen;
            std::memcpy(outBuf + prefixLen, mi.suffix.data(), mi.suffix.size());
            outLen = prefixLen + mi.suffix.size();
        }
    }

    bool nameEquals(uint32_t slotId, const std::string& name) const {
        const PackedEntry& e = entries_[slotId];
        if (e.nameLen != name.size()) return false;
        switch (e.representation) {
            case Rep::INLINE_REP:
                return std::memcmp(e.payload.inlineBytes, name.data(), name.size()) == 0;
            case Rep::INTERNED_REP:
                return pool_[e.payload.poolIndex] == name;
            case Rep::COMPRESSED_REP: {
                const CompressedRef& ref = e.payload.compressedRef;
                const BlockMember& m = blocks_[ref.blockIndex].members[ref.slotInBlock];
                if (m.fp8 != fingerprint8(name)) return false;

                reconstructions_++;
                const Block& b = blocks_[ref.blockIndex];
                size_t anchorEvery = cfg_.anchorInterval == 0 ? 1 : cfg_.anchorInterval;
                uint16_t start = static_cast<uint16_t>(ref.slotInBlock - (ref.slotInBlock % anchorEvery));
                reconstructionSteps_ += static_cast<size_t>(ref.slotInBlock - start);

                char stackBuf[512];
                if (e.nameLen < sizeof(stackBuf)) {
                    size_t decodedLen = 0;
                    decodeToBuffer(b, ref.slotInBlock, cfg_.anchorInterval, stackBuf, decodedLen);
                    if (decodedLen != name.size()) return false;
                    return std::memcmp(stackBuf, name.data(), name.size()) == 0;
                } else {
                    std::vector<char> heapBuf(e.nameLen + 1);
                    size_t decodedLen = 0;
                    decodeToBuffer(b, ref.slotInBlock, cfg_.anchorInterval, heapBuf.data(), decodedLen);
                    if (decodedLen != name.size()) return false;
                    return std::memcmp(heapBuf.data(), name.data(), name.size()) == 0;
                }
            }
        }
        return false;
    }

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
            std::string prevFull = decodeFrom(b, slot - 1, prev, cfg_.anchorInterval);
            size_t shared = commonPrefixLen(prevFull, name);
            if (shared > 255) shared = 255;
            m.isAnchor = false;
            m.sharedPrefixLen = static_cast<uint8_t>(shared);
            m.suffix = name.substr(shared);
        }
        long long cost = static_cast<long long>(m.suffix.capacity()) + kCompressedMemberOverhead;
        b.members.push_back(std::move(m));
        b.liveCount++;
        b.trackedBytes += cost;
        tracker_.add(cost);
        return CompressedRef{openBlock_, slot};
    }

    std::string reconstructMember(CompressedRef ref) const {
        const Block& b = blocks_[ref.blockIndex];
        const BlockMember& m = b.members[ref.slotInBlock];
        reconstructions_++;
        size_t anchorEvery = cfg_.anchorInterval == 0 ? 1 : cfg_.anchorInterval;
        uint16_t start = static_cast<uint16_t>(ref.slotInBlock - (ref.slotInBlock % anchorEvery));
        reconstructionSteps_ += static_cast<size_t>(ref.slotInBlock - start);

        if (m.isAnchor) return m.suffix;

        char stackBuf[512];
        size_t decodedLen = 0;
        decodeToBuffer(b, ref.slotInBlock, cfg_.anchorInterval, stackBuf, decodedLen);
        if (decodedLen < sizeof(stackBuf)) {
            return std::string(stackBuf, decodedLen);
        } else {
            std::vector<char> heapBuf(decodedLen + 1);
            decodeToBuffer(b, ref.slotInBlock, cfg_.anchorInterval, heapBuf.data(), decodedLen);
            return std::string(heapBuf.data(), decodedLen);
        }
    }

    static std::string decodeFrom(const Block& b, uint16_t slot, const BlockMember& m, size_t anchorInterval = 8) {
        if (m.isAnchor) return m.suffix;
        char stackBuf[512];
        size_t decodedLen = 0;
        decodeToBuffer(b, slot, anchorInterval, stackBuf, decodedLen);
        return std::string(stackBuf, decodedLen);
    }

    void releaseBlockMember(CompressedRef ref) {
        if (ref.blockIndex == UINT32_MAX) return;
        Block& b = blocks_[ref.blockIndex];
        if (b.liveCount > 0) b.liveCount--;
        if (b.liveCount == 0) {
            tracker_.reclaim(b.trackedBytes);
            b.members.clear();
            b.members.shrink_to_fit();
            b.trackedBytes = 0;
            if (openBlock_ == ref.blockIndex) openBlock_ = UINT32_MAX;
            blockFreeList_.push_back(ref.blockIndex);
        }
    }

    void maybePromote(uint32_t slotId, const std::string& name) {
        PackedEntry& e = entries_[slotId];
        if (e.representation != Rep::COMPRESSED_REP) return;
        if (e.accessCount < cfg_.hotAccessThreshold) return;
        CompressedRef oldRef = e.payload.compressedRef;
        releaseBlockMember(oldRef);
        e.representation = Rep::INTERNED_REP;
        e.setWasPromoted(true);
        e.payload.poolIndex = internName(name);
        promotions_++;
    }

    void demote(uint32_t slotId, PackedEntry& e) {
        std::string name = pool_[e.payload.poolIndex];
        releasePoolRef(e.payload.poolIndex);
        e.representation = Rep::COMPRESSED_REP;
        e.setWasPromoted(false);
        e.accessCount = 0;
        e.payload.compressedRef = insertCompressed(name);
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

    std::unordered_map<uint64_t, uint32_t> liveSeenRep_;

    std::vector<std::string> pool_;
    std::vector<uint32_t> poolRefCount_;
    std::unordered_map<std::string, uint32_t> poolLookup_;
    std::vector<uint32_t> poolFreeList_;

    std::vector<Block> blocks_;
    std::vector<uint32_t> blockFreeList_;
    uint32_t openBlock_ = UINT32_MAX;
    mutable size_t reconstructions_ = 0;
    mutable size_t reconstructionSteps_ = 0;
    mutable size_t inlineLookups_ = 0;
    mutable size_t internedLookups_ = 0;
    mutable size_t compressedLookups_ = 0;

    std::vector<Scope> scopes_;
    int nextDeclId_ = 0;
    size_t promotions_ = 0;
    size_t demotions_ = 0;
    uint32_t epoch_ = 0;

    MemoryTracker tracker_;
    PolicyConfigV3 cfg_;
};

} // namespace v3
} // namespace budgetsym
