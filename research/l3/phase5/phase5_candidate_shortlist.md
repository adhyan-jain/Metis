# Phase 5: Ranked Candidate Shortlist

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`

Based on the research space inventory and prior-art analysis, we have isolated exactly one high-value, falsifiable algorithmic candidate. All other algorithmic directions are exhausted, known prior art, or theoretically bankrupt.

## Candidate F: Hybrid LIFO Scope-Tied Arena (12B SSO + LIFO Overflow)

### A. Technical Definition
- **Data Structure Changes:** 
  1. Add a `std::vector<char> arena_` to `MetisXTable`. 
  2. Add `std::vector<size_t> scopeArenaBounds_`.
- **Operation Changes:**
  - `enterScope()`: Appends `arena_.size()` to `scopeArenaBounds_`.
  - `insert(name)`: If `name <= 12B`, store entirely in `inlineBytes` (L1 hit). If `name > 12B`, store the first 8 bytes as a prefix and a 4-byte `arenaOffset` in `inlineBytes`, appending the *tail* of the string to `arena_`.
  - `exitScope()`: Along with backward-shifting the slots, execute `arena_.resize(scopeArenaBounds_.back()); scopeArenaBounds_.pop_back();`.

### B. Novelty Argument
It uniquely solves the structural trade-off demonstrated by Candidate D's failure. By physically mapping the lexical LIFO invariants of the compiler AST to the string arena, it eliminates 100% of the `operator new` overhead of `METIS-X` (saving glibc chunk headers) *without* suffering the monotonic memory bloat of Candidate D. The 12-byte SSO + 8-byte prefix ensures $>99\%$ of collisions are rejected in L1 cache without arena indirection.

### C. Correctness Obligations
1. **LIFO Invariant:** Strings from an inner scope must *never* be appended out-of-order behind strings from an outer scope. (Guaranteed by single-threaded AST parsing).
2. **Rehash Invariant:** `rehash()` must accurately copy the 4-byte `arenaOffset` and 8-byte prefix without mutating or copying the `arena_` itself.
3. **Shadowing Invariant:** Same-scope redeclarations must update in place without leaking a second string tail into the arena.

### D. Performance Hypothesis
- **Peak Physical Heap:** Will drop below `METIS-X` (Control) because the 32-byte glibc chunk header overhead per long string (137,968 allocations in ESP-IDF) is eliminated, while transient strings are perfectly reclaimed.
- **$p_{95}$ Lookup Latency:** Will remain equivalent to or slightly faster than `METIS-X` ($< 5\%$ variance) because the working set of the LIFO arena remains compact (fitting in L3 cache) and the 8-byte inline prefix handles mismatch rejections inside the 32-byte slot.

### E. Counter-hypothesis
**The Fragmentation/Dead-String Leak:** If an identifier is `insert`ed but the table rejects it *after* appending to the arena (e.g., due to `kMaxProbe` overflow), the appended bytes are leaked into the arena until the scope exits. If a workload has massive churn of *same-scope* redeclarations or failures, the arena could bloat before `exitScope()` has a chance to truncate it.

### F. Minimum Decisive Test
Implement Candidate F as an isolated header (`include/metis_x_hybrid_arena.hpp`). Modify `src/phase4_bench.cpp` to run the ESP-IDF and Zephyr traces against it.
**Falsification:** If Candidate F fails to reduce Peak Physical Heap below `METIS-X`, or if it degrades $p_{95}$ latency by $> 5\%$, the LIFO arena hypothesis is false, and the algorithmic space is conclusively exhausted.

### G. Paper Value
**Systems & Algorithmic Paper.** Success justifies a paper presenting METIS-X as a novel hybrid open-addressing architecture explicitly optimized for scoped symbol tables. It transitions the narrative from a standard engineering ablation study to a genuine domain-specific algorithmic design.
