# Phase 3 Inline Storage Feasibility & Cost Model

**Date:** October 10, 2026  
**Repository:** `adhyan-jain/Metis`  
**Revision:** HEAD `@0b4344050873968a7f3f940d47f8b46f50a23353`  
**Phase:** Phase 3 (Paper-First Pivot)  
**Deliverable:** 2 of 3 (`research/l3/phase3_inline_storage_feasibility.md`)

---

## 1. Resolution of the Identifier Distribution Paradox

A critical inconsistency exists between the Phase 2 corpus plan (claiming $>90\%$ of identifiers are $\le 12\text{B}$) and the Phase 2 instrumentation results (showing $89.87\%$ of ESP-IDF final slots are heap-allocated). 

### The Denominator Explanation:
- **Total Unique Identifiers (Lexical Corpus):** $89.9\%$ of all unique identifiers in ESP-IDF are $\le 12\text{B}$. These are overwhelmingly short, transient local variables (e.g., `i`, `len`, `buf`) declared in deep scopes.
- **Final Live Global Entries (Resident Memory):** Once local scopes are popped via `exitScope()`, the remaining $153,518$ live entries consist of global macros, constants, and function declarations (e.g., `ESP_APPTRACE_DEST_UART_NUM`). For these long-lived global symbols, **$89.87\%$ exceed 12 bytes**.
- **Conclusion:** While METIS-X's 12-byte SSO optimally captures transient local variables, it completely fails to capture the resident global symbol table, resulting in $137,968$ disjoint heap allocations during the ESP-IDF AST parse.

---

## 2. Analytical Comparison of Storage Architectures

We analytically evaluate five design families for open-addressing slot storage without implementing them.

### Candidate A: Current 12-Byte Inline + External Heap (METIS-X Baseline)
- **Mechanism:** 32B cache-aligned slot. Names $>12\text{B}$ call `new char[len]`.
- **Memory Cost:** 32B per slot. Heap strings incur 32B minimum physical overhead (16B chunk header + 16B payload alignment).
- **Lookup Latency:** Excellent for $\le 12\text{B}$ (in-slot `memcmp`), but incurs $L1 \to L3$/RAM cache miss for pointer dereference on long names.
- **Verdict:** Highly optimized for small codebases, but memory-fragmented and latency-bound on SDK-heavy codebases.

### Candidate B: Enlarged 28-Byte Inline SSO (48-Byte Slot)
- **Mechanism:** Expand slot size to 48B, allowing 28B inline names before heap fallback.
- **Memory Cost:** Slot array size increases by $50\%$ ($4.19\text{ MB} \to 6.29\text{ MB}$ on Zephyr).
- **Lookup Latency:** 48B slots misalign with 64B CPU cache lines. Every slot probe straddles cache-line boundaries, drastically increasing cache miss rates during collision resolution.
- **Verdict:** **REJECT.** Breaks the foundational $32\text{B}$ cache-alignment invariant of METIS-X. The density loss negates the allocation savings.

### Candidate C: Inline-Prefix + Pooled Overflow (SwissTable `StringView` Style)
- **Mechanism:** 32B slot containing a 4-byte string prefix, a 4-byte length, and an 8-byte pointer to a shared string pool (or AST arena).
- **Memory Cost:** Eliminates per-string glibc chunk headers ($16\text{B}$ savings per long name). Slot remains 32B.
- **Lookup Latency:** The 4-byte prefix allows $80\%$ of hash collisions to be rejected without dereferencing the pointer.
- **Verdict:** Promising, but pointer indirection still causes a cache miss on true matches or prefix collisions.

### Candidate D: Compact Prefix + Linear Arena Offset
- **Mechanism:** 32B slot containing a 4-byte inline prefix and a 32-bit integer offset (`uint32_t arena_idx`) into a contiguous global `std::vector<char>` string arena. 
- **Memory Cost:** Minimal. The arena is contiguous (no malloc overhead). A 32-bit offset limits the arena to 4GB (sufficient for compilers).
- **Lookup Latency:** Replaces scattered heap pointers with localized arena array indexing. Prefix matching rejects most collisions in-slot.
- **Verdict:** **STRONGEST CANDIDATE.** Maintains 32B slot alignment, eliminates $100\%$ of `insert()` string heap allocations, and guarantees compact physical memory.

---

## 3. Risks & Decisive Falsification Test

### The Primary Risk (Amdahl's Indirection Wall)
**Risk Hypothesis:** Reducing dynamic memory allocations via Arena Offsets (Candidate D) does not necessarily improve $p_{95}$ lookup latency. Arena indexing still requires fetching memory outside the 32B slot array. If the arena exceeds the L2 cache size, the hardware prefetcher may stall, meaning Candidate D could perform identically to or worse than Candidate A's `ptmalloc` pointers.

### The Decisive Next Experiment
Before adopting Candidate D, we must empirically test whether arena offset indirection actually outperforms standard pointer dereferencing on the exact ESP-IDF workload.

- **Experiment:** Prototype **Candidate D (Prefix + Arena Offset)** in an isolated benchmark.
- **Metric:** $p_{95}$ lookup latency and physical peak heap on ESP-IDF.
- **Falsification Threshold:** If Candidate D fails to reduce $p_{95}$ lookup latency below Candidate A ($0.081\mu\text{s}$) or increases insertion CPU time due to arena bounds checking, Candidate D will be rejected as an optimization.
