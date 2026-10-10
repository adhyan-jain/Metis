# Phase 5: Research Space Inventory

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`

## A. Scope-Aware Deletion and Restoration
- **Concept:** Faster scope unwinding via transactional rollback.
- **Status:** `REJECTED_BY_EXPERIMENT` (Phase 2.2).
- **Reason:** `METIS-X`'s LIFO frame stack and Robin Hood backward-shift natively perfectly restore physical layout. Unwinding consumes $<8\%$ of compilation time. Transactional logging bloats the $O(1)$ hot insertion path to solve a non-existent bottleneck.

## B. Probe and Displacement Representation
- **Concept:** Fixing Robin Hood primary clustering via grouped probing (e.g., SwissTable) or Hopscotch hashing.
- **Status:** `REJECTED_BY_PRIOR_ART`.
- **Reason:** Standard techniques. Abseil SwissTable and Folly F14 have already exhausted the grouped-probing design space for memory-constrained hash tables. Implementing them here is an engineering port, not an algorithmic novelty.

## C. Identifier Resolution & Shadowing
- **Concept:** Accelerating lookup of deeply shadowed variables (avoiding the $O(K)$ scan through the probe cluster to find the highest `scopeId`).
- **Status:** `LITERATURE_CHECKED` / `UNEXPLORED`.
- **Reason:** While `heavy_shadowing_20_levels` degrades $p_{95}$ latency by $169\%$, AST canonical profiling shows shadowing depth rarely exceeds 2 in actual embedded C code. The engineering complexity of maintaining a shadow-linked list (like LLVM `ScopedHashTable`) breaks the flat 32-byte cache alignment. Optimization here has minimal real-world impact.

## D. Slot Layout and Metadata Organization
- **Concept:** Stripping the 4-byte `frameIndex` from the slot to gain 4 bytes of SSO storage.
- **Status:** `REJECTED_BY_ANALYSIS`.
- **Reason:** `frameIndex` provides the critical $O(1)$ bidirectional link allowing Robin Hood swaps to update the `scopeFrames_` stack. Removing it would require $O(S)$ linear scans of scope frames on every displacement swap, destroying insertion throughput.

## E. String Lifetime, Ownership, and Storage
- **Concept 1 (Monotonic Arena):** 32-bit arena offset into a contiguous `std::vector`.
- **Status:** `REJECTED_BY_EXPERIMENT` (Phase 4, Candidate D). Increased peak memory by $35\%$ due to transient strings accumulating monotonically without reclamation.

- **Concept 2 (LIFO Scope-Tied Arena with 12B SSO):** Exploiting compiler lexical scoping by storing $>12$-byte strings in a global arena, and reclaiming them instantly via `arena.resize()` upon `exitScope()`, entirely eliminating `operator new`/`delete`.
- **Status:** `PROMISING_NEEDS_TEST`.
- **Reason:** Completely untested. Promises to eliminate the $89\%$ heap-fallback allocation overhead of ESP-IDF and solve Candidate D's monotonic bloat problem by physically marrying arena truncation to AST scope unwinding. 

## F. Allocation and Memory-Reclamation Policy
- **Concept:** Custom slab allocators or memory pools for `new char[len]`.
- **Status:** `REJECTED_BY_PRIOR_ART`.
- **Reason:** Standard C++ engineering practice. Not a novel algorithmic mechanism.

## Conclusion
The algorithmic space for displacement, metadata, and probing is heavily exhausted by established prior art (SwissTable, F14, Hopscotch) or bounded by `METIS-X`'s already pareto-optimal backward-shift unwinding. The **sole remaining high-value, unexhausted vector** is **E.2: LIFO Scope-Tied Arenas**, which leverages domain-specific compiler invariants (strict lexical scoping) to solve the memory/latency trade-offs that defeated Candidate D.
