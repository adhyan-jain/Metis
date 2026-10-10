# Phase 2 Prior-Art Matrix & Research Gap Analysis

**Date:** October 10, 2026  
**Repository:** `adhyan-jain/Metis`  
**Revision:** HEAD `@0b4344050873968a7f3f940d47f8b46f50a23353`  
**Phase:** Phase 2 (Workload Profiling, Baseline Validation & Research Viability Gate)  
**Deliverable:** 3 of 4 (`research/l3/phase2/phase2_research_gap.md`)

---

## 1. Executive Summary

This document performs a literature review, prior-art taxonomy, and gap analysis for symbol table data structures used in compiler front-ends and language runtimes. 

While `METIS-X` combines existing systems primitives (Robin Hood open-addressing hash tables, LIFO scope stack frame indices, 12B inline SSO, and backward shift deletion), it leaves several critical algorithmic challenges unaddressed in transient lexical scope management. This report evaluates whether a defensible L3 research contribution exists and defines candidate algorithmic directions.

---

## 2. Prior-Art Screening & Taxonomy

Compiler symbol tables historically divide into three architectural paradigms:

1. **Scoped Linked-List Chains (LLVM `ScopedHashTable` / Clang `IdentifierResolver`):**
   - *Mechanism:* Each scope allocation pushes nodes onto a per-identifier symbol definition stack. Scope exit traverses the current scope frame's linked list and pops elements.
   - *Tradeoffs:* $O(1)$ scope entry and exit. However, memory fragmentation is high due to node heap allocations, and pointer chasing causes cache miss rates above $40\%$ on modern architectures.

2. **Global Hash Table with Scope Tagging (GCC / standard production compilers):**
   - *Mechanism:* A global hash table maps symbol names to symbol metadata structures tagged with a `scope_id`. Lookups verify that `symbol.scope_id` is currently active.
   - *Tradeoffs:* High lookup performance when cache lines align. However, scope exit requires either (a) tombstone markers causing table load factor degradation, or (b) full table scans to purge out-of-scope entries.

3. **Open-Addressing Robin Hood with Backward Shift (Celis 1986, Pedro Celizic 2016):**
   - *Mechanism:* Open addressing with probe distance balancing during insertion and backward-shift slot relocation during deletion to eliminate tombstones.
   - *Tradeoffs:* Excellent cache locality and high load factors ($>90\%$). However, standard backward shift assumes individual arbitrary-key deletions rather than bulk LIFO scope unwinding.

---

## 3. Prior-Art Comparison Matrix

### Table 3.1: Comparative Analysis of Compiler Symbol Table Architectures

| Feature / Metric | LLVM `ScopedHashTable` | Clang `IdentifierResolver` | SwissTable / Abseil / F14 | Standard Robin Hood (Celis 1986) | **`METIS-X` Reference** |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Primary Data Structure** | Linked-list per identifier | Per-identifier shadow chain | SIMD 128-bit Control Byte Group | Flat Open-Addressing Array | Cache-aligned 32B Slot Array |
| **Lookup Hot Path Allocation** | 0 | 0 | 0 | 0 | **0** |
| **Scope Exit Complexity** | $O(k)$ linked list pops | $O(k)$ chain unlinks | $O(n)$ full table scan / tombstones | $O(k \cdot d)$ single-item backward shift | **$O(k \cdot d)$ LIFO shift scan** |
| **Tombstone Strategy** | None (node deletion) | None (node deletion) | 7-bit Control Tombstones (`0xFE`) | Tombstone-Free Backward Shift | **Tombstone-Free Backward Shift** |
| **Inline String Capacity** | 0 (Pointers) | 0 (Pointers to AST) | 0 (Key stored externally) | 0 (Key stored externally) | **12 Bytes (SSO)** |
| **Hardware Alignment** | Pointer aligned | Pointer aligned | 16-byte SSE2 / 32-byte AVX2 | Cache aligned | **32-byte slot cache aligned** |
| **Cache Line Efficiency** | Low (Pointer chasing) | Moderate | High (SIMD group match) | High (Contiguous probe) | **High ($p_{95} \le 0.11\,\mu\text{s}$)** |
| **Handling of $>12\text{B}$ Names** | External AST Alloc | External AST Alloc | External Alloc | External Alloc | **Heap string fallback (Alloc spike)** |

---

## 4. Identification of Unresolved Research Gaps

Empirical analysis in Phase 2 reveals three fundamental algorithmic gaps in transient-scope symbol management:

### Gap 1: The "12-Byte SSO Wall" in Fixed-Slot Open Addressing
- **Problem:** `METIS-X` fixes slot sizes at 32 bytes to guarantee L1 cache line fitting (2 slots per 64B cache line). This caps inline SSO strings at 12 bytes.
- **Empirical Reality:** Real-world enterprise C/C++ codebases (e.g., ESP-IDF) average $>12$ bytes per symbol name for $89.87\%$ of declarations. Consequently, $89.87\%$ of insertions require secondary heap allocations, eliminating the cache locality benefits of inline open addressing.
- **Unresolved Challenge:** How can open-addressing symbol tables maintain 32B cache-aligned slots while supporting variable-length symbol names without secondary dynamic heap allocation?

### Gap 2: Scope Exit Unwind Overhead under Dense Lexical Scopes
- **Problem:** Upon `exitScope()`, `METIS-X` iterates over all symbols declared in the target scope frame and executes individual Robin Hood backward-shift steps to fill vacated slots.
- **Empirical Reality:** When dense scopes containing dozens of symbols are closed (e.g., automated code generation or macro expansions), individual backward shifts cause redundant memory swaps and cache invalidation across the hash array.
- **Unresolved Challenge:** Can a Robin Hood table support bulk LIFO scope unwinding in guaranteed amortized $O(1)$ time without introducing tombstones?

### Gap 3: Shadowed Lookup Degradation under Deep Nesting
- **Problem:** When local variables shadow outer-scope declarations with identical names, `METIS-X` must probe displaced slots to resolve the highest matching `scope_id`.
- **Empirical Reality:** Under 20 levels of nested shadowing, lookup latency spikes from $0.069\,\mu\text{s}$ to $0.186\,\mu\text{s}$ ($+169.5\%$).
- **Unresolved Challenge:** How to achieve $O(1)$ scope-shadowed resolution in flat open addressing without linked shadow chains?

---

## 5. Candidate L3 Algorithmic Contribution Opportunities

To reach a defensible L3 contribution, two algorithmic formulations are identified for Phase 3 exploration:

### Candidate A: TS-TFDR (Transient-Scope Tombstone-Free Displacement Rollback)
- **Concept:** Replace iterative single-element backward shifts during `exitScope()` with a transaction-logged displacement rollback mechanism.
- **Mechanism:** Maintain a compact displacement transaction log during symbol insertions within a scope frame. Upon scope exit, execute a single-pass vector rollback that restores the table to pre-scope state in $O(k)$ time without scanning intermediate empty slots.
- **Novelty Claim:** First Robin Hood open-addressing variant providing bulk LIFO scope rollback with zero tombstones and deterministic single-pass slot restoration.

### Candidate B: LS-ZCVM (Lexical Scope Zero-Copy Virtual Memory Shifting)
- **Concept:** Couple the hash table layout with a scope-partitioned virtual memory arena.
- **Mechanism:** Allocate symbol names and slot extensions in page-aligned scope blocks. Scope exit performs a pointer swap / page unmap operation, invalidating entire scope memory regions in $O(1)$ hardware instructions.
- **Novelty Claim:** Hardware-accelerated transient scope reclamation eliminating heap overhead for variable-length strings.
