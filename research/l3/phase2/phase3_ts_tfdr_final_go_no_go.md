# Phase 2.2 — TS-TFDR Kill Test & Final Go / No-Go Decision

**Date:** October 10, 2026  
**Repository:** `adhyan-jain/Metis`  
**Phase:** Phase 2.2 (TS-TFDR Kill Test)  
**Deliverable:** `research/l3/phase2/phase3_ts_tfdr_final_go_no_go.md`

## 1. Prior-Art Corrections & Novelty Accuracy (Task 4)

Following primary-source verification, the following factual corrections are applied to the prior-art claims originally presented in `phase3_ts_tfdr_novelty_screen.md`:

1. **LLVM `ScopedHashTable`:** 
   - *Correction:* It does not use a naive per-scope singly linked list node stack for *all* elements. It actually wraps `llvm::DenseMap` (an open-addressing table using quadratic probing). The linked list is only used to chain *shadowed* declarations of the same identifier across nested scopes, maintaining active binding pointers.
2. **Clang `IdentifierResolver`:**
   - *Correction:* Clang maintains a `Decl*` chain directly inside the `IdentifierInfo` object (the interned string representation). When an identifier is looked up, Clang traverses this custom chain. It does not use a standard hash table for scope resolution but rather leverages the AST's string pool identity.
3. **Abseil SwissTable (`absl::flat_hash_map`):**
   - *Correction:* SwissTable does not use `0xFE` for tombstones. It uses a 7-bit metadata control array where `0xFF` represents `kEmpty`, `0x80` represents `kDeleted` (tombstone), and `0x00`-`0x7F` represent hash remnants. Furthermore, standard element deletion is an $O(1)$ control-byte update (`kDeleted`), not a mandatory $O(N)$ full table scan, though tombstones degrade probe chains until table rehash/compaction.
4. **Folly F14:**
   - *Correction:* F14 does not use a "fragmented secondary array container." It utilizes chunked probing, processing 14-element chunks aligned to 16 bytes for SIMD operations, blending open-addressing with tight spatial chunking.

These corrections demonstrate that the compiler data structure landscape is heavily optimized. TS-TFDR's theoretical novelty over these structures is rendered moot by the findings in Tasks 1 and 2.

---

## 2. Evidence Summary from the Kill Test

1. **Logical Redundancy (Task 1):** The existing Robin Hood backward-shift implementation in `METIS-X` already perfectly rolls back insertion history. Outer-scope slots displaced by inner-scope insertions have their probe distances precisely restored when the inner-scope slot is deleted. TS-TFDR provides no new layout restoration capability; it merely attempts to log what the algorithm already natively deduces.
2. **Cost Model Falsification (Task 2 & 3):** Profiling the wall-clock times of the AST workloads reveals that `exitScope()` accounts for less than **8%** of total execution time across Zephyr and ESP-IDF traces. Introducing a 32-byte transaction log append operation into `insert()` (which accounts for $\sim40\%$ of execution time) would impose a massive memory bandwidth penalty that vastly outweighs any theoretical optimization of `exitScope()`.

---

## 3. Final Recommendation

**Decision:** **`ABANDON TS-TFDR`**

### Strongest Supporting Evidence (Why we abandon):
The core problem TS-TFDR aimed to solve—fast, tombstone-free layout restoration upon scope exit—is mathematically already solved by standard Robin Hood backward-shift deletion. The independent profiling audit proved that `exitScope()` consumes less than $8\%$ of compilation time on canonical codebases. TS-TFDR's required mutation logging overhead would strictly degrade `insert()` performance (the actual hot path) to fix a non-existent bottleneck.

### Strongest Counterargument (Why we might have kept it):
One could argue that TS-TFDR allows $O(1)$ scope closure in dense synthetic blocks (e.g., automated code-generation files with 10,000 flat declarations per scope). However, even in extreme synthetic tests, `exitScope` only accounted for $11.4\%$ of mutative work. There is no credible material speedup to be gained that justifies the architectural complexity and log memory overhead.

### Next Steps:
We must `REVISE RESEARCH QUESTION`. The measurements from Phase 2.1 identified a genuine remaining bottleneck: the **12-Byte SSO Memory Wall** (where $89.87\%$ of ESP-IDF identifiers fall back to dynamic heap allocations). Future L3 ideation should abandon scope-exit rollback mechanics and focus exclusively on resolving variable-length identifier storage without pointer indirection in open-addressing tables.
