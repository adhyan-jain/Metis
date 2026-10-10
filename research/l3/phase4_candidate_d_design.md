# Phase 4 Candidate D Design Specification

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`
**Phase:** Phase 4 (Candidate D Controlled Experiment)
**Deliverable:** `research/l3/phase4_candidate_d_design.md`

## 1. Slot Representation (`MetisXArenaSlot`)

```cpp
struct MetisXArenaSlot {
    int32_t  declId        = -1; // 4B
    uint32_t hashCache     = 0;  // 4B
    uint32_t frameIndex    = 0;  // 4B
    uint16_t scopeId       = 0;  // 2B
    uint16_t nameLen       = 0;  // 2B
    uint8_t  probeDistance = 0;  // 1B
    uint8_t  typeId        = 0;  // 1B
    
    char     prefix[4]     = {0};// 4B (First 4 chars of the string)
    uint32_t arenaOffset   = 0;  // 4B (Offset in the global character arena)
    
    uint32_t reserved      = 0;  // 4B padding to maintain 32B alignment
    uint16_t reserved2     = 0;  // 2B padding
};
static_assert(sizeof(MetisXArenaSlot) == 32, "Slot must be exactly 32 bytes");
```

## 2. Design Explicit Answers

1. **Does the slot remain 32 bytes?**
   Yes. Removing the 13-byte `inlineBytes` array allows us to fit a 4-byte `prefix` and a 4-byte `arenaOffset` while retaining 6 bytes of padding to keep the slot exactly 32 bytes (cache-aligned).
2. **What is the precise meaning of the 32-bit offset?**
   It is the absolute byte index into `std::vector<char> arena_`. The invalid-offset sentinel is `0xFFFFFFFF`. The maximum representable arena size is 4GB (sufficient for a single compilation unit).
3. **How are string length, termination, and full-string equality determined?**
   `nameLen` stores the exact length. Strings in the arena are NOT null-terminated to save space. Equality is determined by:
   `if (slot.nameLen == targetLen && memcmp(slot.prefix, targetPrefix, min(4, targetLen)) == 0) {`
       `if (targetLen <= 4) return true;`
       `return memcmp(&arena_[slot.arenaOffset], target + 4, targetLen - 4) == 0;`
   `}`
4. **How are hash computation and hash caching preserved?**
   Unchanged. FNV-1a 32-bit hashes are cached in `hashCache`.
5. **How are strings appended during insertion and handled during rehash?**
   During `insert()`, if length $> 4$, the characters from index 4 to end are appended to `arena_` (`arena_.insert(arena_.end(), str + 4, str + len)`). During table rehash, slots are moved to new buckets, but their `arenaOffset` remains strictly valid. Strings are never moved.
6. **What happens when the arena reaches its limit?**
   If `arena_.size() + len > 0xFFFFFFFF`, the insertion returns `-1` (failure).
7. **Can a failed insertion leave the table or arena partially mutated?**
   If `insert()` fails due to maximum probe distance (250) *after* appending to the arena, the appended characters become dead space (leak), but memory safety is maintained.
8. **Can arena growth invalidate pointers, references, or views?**
   Yes, `arena_.push_back()` can cause reallocation. Thus, we *only* store `uint32_t arenaOffset`, never raw pointers. The offset is immune to vector reallocation.
9. **Are strings reclaimed when scopes exit?**
   **No.** This is the critical trade-off of Candidate D. Because strings are interleaved sequentially across scopes, `exitScope()` cannot free the memory of transient long identifiers without shifting the entire arena (which would invalidate all offsets) or introducing a complex freelist. Transient strings will be retained until table destruction.
10. **How are repeated strings handled?**
    No interning is performed. Every declaration appends a new copy to the arena. This isolates the experiment to *storage layout only*.
11. **How do collisions, shadowing, backward-shift deletion, and `updateSlotLocation()` interact with offsets?**
    Unchanged. Swapping slots simply swaps the 32-byte `MetisXArenaSlot` structure, moving the `arenaOffset` seamlessly.
12. **What are the behavior and resource limits for identifiers exceeding 65,535 bytes and scope depths exceeding 65,535?**
    Unchanged from METIS-X. `nameLen` limits identifiers to 65,535 bytes; `insert()` rejects larger strings before mutation. Scope depth is limited by `kMaxScopeDepth = 65535`.
