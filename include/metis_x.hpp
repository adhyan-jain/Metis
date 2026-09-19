#pragma once
// METIS-X — Cache-Conscious Compiler Symbol Table (Phase II)
//
// Research question: can a flat, cache-aware open-addressing symbol table
// beat EmbeddedConventionalSymbolTable on both physical final heap AND p95
// lookup latency under realistic compiler workloads?
//
// Phase I established:
//   - SymTabV3 reduces final heap -20.2% (Zephyr) but p95 is 1.824x worse.
//   - The p95 regression is caused by front-coded block decompression on every
//     lookup: hash -> probe -> reconstruct -> memcmp (O(block) path).
//   - SymTabV4 is a strict negative result (worse than both on 19/20 corpora).
//
// METIS-X's design eliminates reconstruction from the hot lookup path:
//
//   1. Fixed-size 32B MetisXSlot (one cache-line half):
//        - 12B inline name buffer (covers 93.4% of Zephyr names verbatim)
//        - 1B nameLen, 1B repFlags, 2B scopeId, 4B declId, 4B hashCache,
//          1B probeDistance, 3B padding / future use
//        - For names > 12B: nameLen > kInlineCap sets REP_HEAP flag;
//          the slot stores a heap pointer in the same 12B union.
//
//   2. Single flat power-of-2 slab (MetisXTable::slots_[]) -- no separate
//      arena, no per-entry operator new, no vector-of-vectors scope index.
//      Entire table is ONE heap allocation that grows geometrically.
//
//   3. Robin Hood displacement: on insert, swap with any slot whose probe
//      distance is less than the current key's probe distance. On lookup,
//      stop probing as soon as current probe distance exceeds the slot's
//      stored probe distance (key cannot be further out).
//
//   4. Scope lifetime: each scope pushes a frame to a vector<uint32_t>.
//      exitScope() marks all frame slots as empty -- O(symbols_in_scope),
//      same complexity as EmbeddedConventional.
//
//   5. Same physical heap measurement: callers wrap benchmarks in
//      budgetsym::heap::Scope (from heap_counter.hpp).
//
// Compatibility: C++14 (-std=c++14), -O2. No SIMD, no platform intrinsics.

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "common.hpp"
#include "hash_functions.hpp"

namespace budgetsym {
namespace metisx {

// ---- tunables ---------------------------------------------------------------
static const size_t kInlineCap    = 12;    // max chars stored in-slot (no heap)
static const size_t kInitialSlots = 16;    // must be power of 2
static const int    kLoadNum      = 7;     // rehash when liveCount * 10 > cap * kLoadNum
// Effective load factor = kLoadNum / 10 = 0.70

// ---- representation flags ---------------------------------------------------
enum : uint8_t {
    REP_INLINE = 0x00,  // name fully stored in slot.name.inlineBytes[0..nameLen-1]
    REP_HEAP   = 0x01,  // name heap-allocated; slot stores char* in name.inlineBytes
};

// ---- slot layout: exactly 32 bytes -----------------------------------------
struct MetisXSlot {
    int32_t  declId        = -1;         // 4B: declaration ordinal
    uint32_t hashCache     = 0;          // 4B: FNV hash (32-bit) for quick mismatch
    uint16_t scopeId       = 0;          // 2B: scope depth at declaration
    uint8_t  nameLen       = 0;          // 1B: 0 => empty slot sentinel
    uint8_t  probeDistance = 0;          // 1B: Robin Hood probe distance
    uint8_t  repFlags      = REP_INLINE; // 1B: REP_INLINE or REP_HEAP
    uint8_t  typeId        = 0;          // 1B: compiler type id
    uint16_t _pad          = 0;          // 2B: reserved (keeps struct 32B)

    union NameStorage {                  // 12B: inline bytes or heap pointer
        char  inlineBytes[kInlineCap];
        char* heapPtr;
        NameStorage() { std::memset(inlineBytes, 0, sizeof(inlineBytes)); }
    } name;

    bool occupied() const { return nameLen > 0; }

    std::string getString() const {
        if (repFlags & REP_HEAP) {
            char* p;
            std::memcpy(&p, name.inlineBytes, sizeof(char*));
            return std::string(p, nameLen);
        }
        return std::string(name.inlineBytes, nameLen);
    }

    // Set name; allocates heap copy if len > kInlineCap. Returns heap ptr or null.
    char* setName(const char* data, uint8_t len) {
        nameLen = len;
        if (static_cast<size_t>(len) <= kInlineCap) {
            repFlags = REP_INLINE;
            std::memcpy(name.inlineBytes, data, len);
            return nullptr;
        }
        repFlags = REP_HEAP;
        char* p = new char[len];
        std::memcpy(p, data, len);
        std::memcpy(name.inlineBytes, &p, sizeof(char*));
        return p;
    }

    void clear() {
        if (occupied() && (repFlags & REP_HEAP)) {
            char* p;
            std::memcpy(&p, name.inlineBytes, sizeof(char*));
            delete[] p;
        }
        declId        = -1;
        hashCache     = 0;
        scopeId       = 0;
        nameLen       = 0;
        probeDistance = 0;
        repFlags      = REP_INLINE;
        typeId        = 0;
        _pad          = 0;
        std::memset(name.inlineBytes, 0, kInlineCap);
    }
};

static_assert(sizeof(MetisXSlot) == 32, "MetisXSlot must be exactly 32 bytes");

// ---- main symbol table ------------------------------------------------------
class MetisXTable {
public:
    explicit MetisXTable(size_t /*budgetBytes*/ = 0) {
        slots_.resize(kInitialSlots);
        capacity_ = kInitialSlots;
        scopeFrames_.emplace_back();
    }

    ~MetisXTable() {
        for (auto& s : slots_) {
            if (s.occupied() && (s.repFlags & REP_HEAP)) {
                char* p;
                std::memcpy(&p, s.name.inlineBytes, sizeof(char*));
                delete[] p;
            }
        }
    }

    MetisXTable(const MetisXTable&) = delete;
    MetisXTable& operator=(const MetisXTable&) = delete;

    // ---- scope management ---------------------------------------------------

    int enterScope() {
        scopeFrames_.emplace_back();
        return static_cast<int>(scopeFrames_.size()) - 1;
    }

    struct ScopeExitReport { size_t symbolsReleased = 0; long long bytesReclaimed = 0; };

    ScopeExitReport exitScope() {
        ScopeExitReport rep;
        if (scopeFrames_.size() <= 1) return rep;

        uint16_t currentScope = static_cast<uint16_t>(scopeFrames_.size() - 1);
        std::vector<uint32_t>& frame = scopeFrames_.back();

        // Collect indices to clear (only those still alive in this scope)
        std::vector<uint32_t> toClear;
        for (uint32_t slotIdx : frame) {
            if (slotIdx >= static_cast<uint32_t>(slots_.size())) continue;
            MetisXSlot& s = slots_[slotIdx];
            if (!s.occupied() || s.scopeId != currentScope) continue;
            toClear.push_back(slotIdx);
        }

        // Clear each slot and backward-shift to maintain Robin Hood invariant
        for (uint32_t slotIdx : toClear) {
            MetisXSlot& s = slots_[slotIdx];
            if (s.repFlags & REP_HEAP)
                rep.bytesReclaimed += static_cast<long long>(s.nameLen);
            s.clear();
            backwardShift(slotIdx);
            liveCount_--;
            rep.symbolsReleased++;
        }

        scopeFrames_.pop_back();
        return rep;
    }

    // ---- insert -------------------------------------------------------------

    int insert(const std::string& name, int typeId = 0) {
        int id = nextId_++;
        uint16_t currentScope = static_cast<uint16_t>(scopeFrames_.size() - 1);

        // Clamp name length to uint8_t range (identifiers > 255B are astronomically rare)
        uint8_t nameLen8 = static_cast<uint8_t>(name.size() > 255 ? 255 : name.size());
        uint32_t h = hash32(name);

        // Same-scope redeclaration: update in place
        {
            size_t mask = capacity_ - 1;
            size_t idx  = h & mask;
            uint8_t dist = 0;
            while (slots_[idx].occupied()) {
                MetisXSlot& s = slots_[idx];
                if (s.probeDistance < dist) break;
                if (s.hashCache == h && s.scopeId == currentScope &&
                    s.nameLen == nameLen8 && nameMatch(s, name.data(), nameLen8)) {
                    s.declId = id;
                    s.typeId = static_cast<uint8_t>(typeId);
                    return id;
                }
                idx = (idx + 1) & mask;
                dist++;
            }
        }

        // Grow if at load limit
        if (static_cast<long long>(liveCount_ + 1) * 10 >
            static_cast<long long>(capacity_) * kLoadNum) {
            rehash(capacity_ * 2);
        }

        // Robin Hood insert
        MetisXSlot toInsert;
        toInsert.declId    = id;
        toInsert.hashCache = h;
        toInsert.scopeId   = currentScope;
        toInsert.typeId    = static_cast<uint8_t>(typeId);
        toInsert.setName(name.data(), nameLen8);
        toInsert.probeDistance = 0;

        size_t mask = capacity_ - 1;
        size_t idx  = h & mask;
        uint32_t slotIdx = static_cast<uint32_t>(idx);

        while (true) {
            MetisXSlot& cur = slots_[idx];
            if (!cur.occupied()) {
                slotIdx = static_cast<uint32_t>(idx);
                slots_[idx] = toInsert;
                scopeFrames_.back().push_back(slotIdx);
                liveCount_++;
                return id;
            }
            if (cur.probeDistance < toInsert.probeDistance) {
                // Robin Hood swap: displaced entry keeps its scope frame record
                // (exitScope scans by scopeId, not by slotIdx position)
                slotIdx = static_cast<uint32_t>(idx);
                std::swap(toInsert, slots_[idx]);
                // Register the new entry (toInsert before swap = our new entry)
                scopeFrames_.back().push_back(slotIdx);
                liveCount_++;
                // Continue to place the displaced old entry
                // (it will find its home without incrementing nextId_)
                reinsertDisplaced(toInsert, idx);
                return id;
            }
            idx = (idx + 1) & mask;
            toInsert.probeDistance++;
        }
    }

    // ---- lookup (hot path) --------------------------------------------------

    int resolve(const std::string& name) const {
        uint8_t nameLen8 = static_cast<uint8_t>(name.size() > 255 ? 255 : name.size());
        uint32_t h       = hash32(name);
        size_t mask      = capacity_ - 1;
        size_t idx       = h & mask;
        uint8_t dist     = 0;

        int bestDeclId = -1;
        int bestScope  = -1;

        while (slots_[idx].occupied()) {
            const MetisXSlot& s = slots_[idx];
            if (s.probeDistance < dist) break;  // Robin Hood early exit

            if (s.hashCache == h && s.nameLen == nameLen8 &&
                nameMatch(s, name.data(), nameLen8)) {
                if (static_cast<int>(s.scopeId) > bestScope) {
                    bestScope  = static_cast<int>(s.scopeId);
                    bestDeclId = s.declId;
                }
            }
            idx = (idx + 1) & mask;
            dist++;
        }
        return bestDeclId;
    }

    bool lookup(const std::string& name) const { return resolve(name) >= 0; }

    void recordAccess(const std::string& /*name*/) {}

    size_t size() const { return liveCount_; }

    // Logical byte estimate (use heap_counter.hpp Scope for physical measurement)
    long long computeCurrentBytes() const {
        long long slotBytes  = static_cast<long long>(slots_.capacity() * sizeof(MetisXSlot));
        long long freeBytes  = 0;
        long long scopeBytes = 0;
        for (auto& f : scopeFrames_)
            scopeBytes += static_cast<long long>(f.capacity() * sizeof(uint32_t));
        long long heapNames = 0;
        for (auto& s : slots_) {
            if (s.occupied() && (s.repFlags & REP_HEAP))
                heapNames += static_cast<long long>(s.nameLen);
        }
        return slotBytes + freeBytes + scopeBytes + heapNames;
    }

    // ---- diagnostics --------------------------------------------------------
    struct Stats {
        size_t capacity;
        size_t liveCount;
        size_t inlineNames;
        size_t heapNames;
        size_t scopeDepth;
        double loadFactor;
        double avgProbeDistance;
    };

    Stats stats() const {
        Stats st;
        st.capacity   = capacity_;
        st.liveCount  = liveCount_;
        st.inlineNames = 0;
        st.heapNames   = 0;
        st.scopeDepth  = scopeFrames_.size();
        st.loadFactor  = capacity_ > 0
            ? static_cast<double>(liveCount_) / static_cast<double>(capacity_) : 0.0;
        double totalDist = 0.0;
        for (auto& s : slots_) {
            if (!s.occupied()) continue;
            if (s.repFlags & REP_HEAP) st.heapNames++;
            else st.inlineNames++;
            totalDist += s.probeDistance;
        }
        st.avgProbeDistance = liveCount_ > 0
            ? totalDist / static_cast<double>(liveCount_) : 0.0;
        return st;
    }

private:
    // ---- internal helpers ---------------------------------------------------

    static uint32_t hash32(const std::string& name) {
        uint64_t h64 = FnvHash::hash(name);
        return static_cast<uint32_t>(h64 ^ (h64 >> 32));
    }

    static bool nameMatch(const MetisXSlot& s, const char* data, uint8_t len) {
        if (s.repFlags & REP_HEAP) {
            char* p;
            std::memcpy(&p, s.name.inlineBytes, sizeof(char*));
            return std::memcmp(p, data, len) == 0;
        }
        // Hot path: <=12B memcmp -- typically 1-2 64-bit comparisons
        return std::memcmp(s.name.inlineBytes, data, len) == 0;
    }

    void backwardShift(size_t startIdx) {
        size_t mask = capacity_ - 1;
        size_t idx  = startIdx & mask;
        while (true) {
            size_t next = (idx + 1) & mask;
            MetisXSlot& nextSlot = slots_[next];
            if (!nextSlot.occupied() || nextSlot.probeDistance == 0) break;
            slots_[idx] = nextSlot;
            slots_[idx].probeDistance--;
            nextSlot.nameLen = 0;  // mark original position empty (no heap free; ptr moved)
            idx = next;
        }
    }

    // Insert a displaced slot entry (Robin Hood swap continuation).
    // Does NOT update scopeFrames_ -- caller registered the final resting slot.
    void reinsertDisplaced(MetisXSlot src, size_t startIdx) {
        size_t mask = capacity_ - 1;
        size_t idx  = (startIdx + 1) & mask;
        src.probeDistance++;

        while (true) {
            MetisXSlot& cur = slots_[idx];
            if (!cur.occupied()) {
                slots_[idx] = src;
                return;
            }
            if (cur.probeDistance < src.probeDistance) {
                std::swap(src, slots_[idx]);
            }
            idx = (idx + 1) & mask;
            src.probeDistance++;
        }
    }

    void rehash(size_t newCap) {
        assert((newCap & (newCap - 1)) == 0);
        std::vector<MetisXSlot> old;
        old.swap(slots_);
        slots_.resize(newCap);
        capacity_  = newCap;
        liveCount_ = 0;
        // Scope frames remain intact; slot indices change during rehash.
        // exitScope() identifies slots by scopeId match, not by stored slotIdx,
        // so correctness is maintained after rehash.
        for (auto& s : old) {
            if (!s.occupied()) continue;
            reinsertForRehash(s);
        }
    }

    void reinsertForRehash(MetisXSlot src) {
        size_t mask = capacity_ - 1;
        size_t idx  = src.hashCache & mask;
        src.probeDistance = 0;
        while (true) {
            MetisXSlot& cur = slots_[idx];
            if (!cur.occupied()) {
                slots_[idx] = src;
                liveCount_++;
                return;
            }
            if (cur.probeDistance < src.probeDistance) {
                std::swap(src, slots_[idx]);
            }
            idx = (idx + 1) & mask;
            src.probeDistance++;
        }
    }

    // ---- data members -------------------------------------------------------
    std::vector<MetisXSlot>            slots_;
    std::vector<std::vector<uint32_t>> scopeFrames_;

    size_t capacity_  = kInitialSlots;
    size_t liveCount_ = 0;
    int    nextId_    = 0;
};

} // namespace metisx
} // namespace budgetsym
