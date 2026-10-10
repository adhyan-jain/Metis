# Phase 5: Implementation Ground Truth

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`

## 1. Verified Implementation Facts (METIS-X)

1. **Slot Layout & Alignment:** `MetisXSlot` is precisely 32 bytes (enforced via `static_assert`). It fits exactly into half a standard 64B cache line. It utilizes a 12-byte SSO (using 13 bytes physically with a null-terminator trick, but effectively 12 bytes of payload).
2. **String Ownership & Heap Fallback:** Identifiers $\le 12\text{B}$ are stored inline. Identifiers $>12\text{B}$ trigger `new char[len]`. The pointer is stored inside the `inlineBytes` array.
3. **Robin Hood & Displacement:** Standard Robin Hood probing based on `probeDistance`. Displaced elements are continuously reinserted (`reinsertDisplaced`).
4. **Scope Frame Bookkeeping:** `scopeFrames_` is a `std::vector<std::vector<uint32_t>>`. It maintains a strict LIFO stack of vectors. Each vector holds the physical slot indices of declarations made in that scope. Crucially, `MetisXSlot` contains `frameIndex` (4B) to enable $O(1)$ updates to `scopeFrames_` when Robin Hood swaps move a slot.
5. **Deletion (Backward Shift):** `exitScope()` iterates the top `scopeFrames_` vector, clears the slot (invoking `delete[]` if heap-backed), and triggers `backwardShift()`. The shift flawlessly restores displaced outer-scope variables to their exact original probe locations.
6. **Lookup Resolution (Shadowing):** `resolve()` probes the cluster. It continues probing even after a match is found until `dist > probeDistance` (Robin Hood break) to ensure it finds the shadow with the highest `scopeId`. This incurs an $O(K)$ scan penalty on deeply shadowed variables.
7. **Exception Safety (Verified):** `insert()` is strongly exception-safe. `setName` (which can throw `std::bad_alloc`) is called *before* any table state (slots or scope frames) is mutated. If a probe-distance overflow occurs (dry-run fails), `clear()` is called to free the isolated heap string safely.

## 2. Uncovered Correctness Hazard (Safety Fix Needed)

**Rehash Leak & Corruption:** 
In `rehash(size_t newCap)`, the table executes `old.swap(slots_); slots_.resize(newCap);`. If `resize()` throws `std::bad_alloc`, the function exits via exception. The local vector `old` (containing all live heap strings) is destroyed. Because `MetisXSlot` is a POD struct without a destructor (the table relies on `~MetisXTable()` for cleanup), all heap strings in `old` are permanently **leaked**. Furthermore, `slots_` is left completely empty while `capacity_` remains at the old size, guaranteeing an out-of-bounds segfault on the next `insert()` or `resolve()`.

*This is a severe bug, but it is an engineering oversight, not an algorithmic novelty. It must be patched prior to production integration, but is excluded from the novelty hunt.*
