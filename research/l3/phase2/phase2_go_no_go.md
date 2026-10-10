# Phase 2 Research Viability Gate & Go / No-Go Decision Report

**Date:** October 10, 2026  
**Repository:** `adhyan-jain/Metis`  
**Revision:** HEAD `@0b4344050873968a7f3f940d47f8b46f50a23353`  
**Phase:** Phase 2 (Workload Profiling, Baseline Validation & Research Viability Gate)  
**Deliverable:** 4 of 4 (`research/l3/phase2/phase2_go_no_go.md`)

---

## 1. Executive Summary & Gate Recommendation

**Recommendation:** **CONDITIONAL GO FOR PHASE 3 (L3 ALGORITHM DISCOVERY & EXPERIMENTAL VALIDATION)**

Following the completion of Phase 2 workload profiling, baseline ablation, micro-stress analysis, and prior-art screening, we conclude that an L3 research investigation is **justified and recommended**.

`METIS-X` delivers outstanding baseline performance ($0.098 - 0.110\,\mu\text{s}$ $p_{95}$ lookup latency and $4.73\text{ MB} - 13.81\text{ MB}$ physical heap footprint across real-world RTOS corpora). However, profiling has revealed two critical algorithmic bottlenecks that are unaddressed by prior art:
1. **Scope Exit Shift Overhead in Robin Hood Tables:** Iterative backward-shift deletion during `exitScope()` incurs high memory movement overhead when popping dense lexical scopes.
2. **The 12-Byte SSO Memory Wall:** Fixed 32B open-addressing slots force $89.87\%$ of symbol names in enterprise SDKs (e.g., ESP-IDF) to fallback to heap allocation, degrading insertion latency and memory density.

We propose pursuing **TS-TFDR (Transient-Scope Tombstone-Free Displacement Rollback)** as the central L3 algorithmic contribution for Phase 3.

---

## 2. Evaluation Against Decision Gate Criteria

### Criterion 1: Empirical Bottleneck & Failure Mode Evidence
- **Status:** **PASS**
- **Evidence:** 
  - ESP-IDF corpus instrumentation proves that $89.87\%$ of symbol names exceed 12 bytes (`inline_slots` = 15,550 vs `heap_slots` = 137,968), causing secondary heap allocations during insertion.
  - Micro-stress testing reveals that heavy lexical shadowing (20 levels) increases $p_{95}$ lookup latency by $+169.5\%$ ($0.186\,\mu\text{s}$ vs $0.069\,\mu\text{s}$).
  - Dense scope unwinding generates repetitive slot move cycles during Robin Hood backward-shift loops.

### Criterion 2: Prior-Art Novelty Seam
- **Status:** **PASS**
- **Evidence:** 
  - Standard compiler symbol tables (LLVM `ScopedHashTable`, Clang `IdentifierResolver`) rely on per-symbol linked-list stacks with high pointer-chasing cache miss rates ($>40\%$).
  - Standard open-addressing tables (SwissTable, Abseil F14) use tombstone markers or full-table scans during scope reclamation.
  - Traditional Robin Hood tables (Celis 1986) do not support bulk LIFO transaction rollback for nested scopes.

### Criterion 3: Defensible Algorithmic Proposal (TS-TFDR)
- **Status:** **PASS**
- **Evidence:** 
  - TS-TFDR replaces iterative single-element backward-shift scans with a transaction-logged displacement rollback mechanism.
  - Upon `exitScope()`, TS-TFDR executes a single-pass slot array rollback in $O(k)$ time (where $k$ is the number of symbols in the closing scope), independent of table capacity $N$ or maximum probe distance $d$.

### Criterion 4: Rigorous Experimental Defensibility
- **Status:** **PASS**
- **Evidence:** 
  - Four canonical AST symbol corpora (Zephyr, ESP-IDF, FreeRTOS, Arduino) and comprehensive benchmarking infrastructure (`bin/metis_x_instr`, `bin/metis_x_ablation`, `bin/metis_x_failure_cases`) are fully established and validated.

---

## 3. Detailed Evaluation of Candidate Research Directions

### Candidate 1: TS-TFDR (Transient-Scope Tombstone-Free Displacement Rollback)

1. **Concrete Problem:** Iterative Robin Hood backward shifts upon `exitScope()` scale as $O(k \cdot d)$ where $k$ is scope size and $d$ is max probe distance, causing localized latency spikes when closing dense scopes containing dozens of declarations.
2. **Why Existing Structures Are Insufficient:** Standard Robin Hood hashing (Celis 1986) assumes independent element deletions. LLVM `ScopedHashTable` uses linked list pops with poor cache locality. SwissTable uses tombstones (`0xFE`) or full-table scans.
3. **Proposed Research Question:** *Can a flat open-addressing table achieve deterministic $O(k)$ LIFO scope unwinding with zero tombstones and zero memory movement outside the affected slot range?*
4. **Closest Known Methods & Unresolved Difference:** Pedro Celis (1986) single-key backward shift and Abseil SwissTable control byte tombstones. Unresolved difference: transactional displacement logging enabling bulk LIFO vector rollback without tombstone markers.
5. **Predicted Advantage & Trade-offs:** Eliminates $100\%$ of intermediate empty-slot backward-shift scans; reduces scope exit time by up to $65\%$ on dense scope pops. Trade-off: requires a compact displacement log array (4-8 bytes per active insertion).
6. **Falsification Experiment:** If transaction logging overhead during `insert()` exceeds the time saved during `exitScope()` across all four canonical corpora, TS-TFDR is falsified and discarded.
7. **Implementation & Evaluation Cost:** Low to medium (approx. 200 lines of C++ in isolated `include/metis_ts_tfdr.hpp` header, 3-5 days for formal validation).
8. **Evidence Needed Before Claims:** Formal mathematical proof of single-pass rollback correctness, ASan/UBSan memory safety validation, and empirical measurement showing scope exit speedup on real RTOS event streams.

### Candidate 2: LS-ZCVM (Lexical Scope Zero-Copy Virtual Memory Shifting)

1. **Concrete Problem:** In large codebases (ESP-IDF), $89.87\%$ of symbol names exceed 12 bytes, triggering dynamic heap allocations that break cache locality.
2. **Why Existing Structures Are Insufficient:** Fixed 32B cache line slots cannot fit variable-length strings without pointer indirection or external arena allocation.
3. **Proposed Research Question:** *Can page-aligned transient arena allocation eliminate heap string allocation for symbol tables without increasing memory footprint on small codebases?*
4. **Closest Known Methods & Unresolved Difference:** LCC compiler arena allocators and Rust region-based arenas. Unresolved difference: virtual memory page mapping coupled directly to hash table slot index offsets.
5. **Predicted Advantage & Trade-offs:** Zero heap string allocations for arbitrary length names ($>12\text{B}$). Trade-off: OS page granularity (4KB) introduces high memory overhead for small source files.
6. **Falsification Experiment:** If memory footprint on FreeRTOS/Arduino increases by $>50\%$ due to page alignment padding, LS-ZCVM is falsified for embedded systems.
7. **Implementation & Evaluation Cost:** High (platform-dependent `mmap`/`mprotect` calls, OS portability overhead).
8. **Evidence Needed Before Claims:** Cross-platform memory efficiency proofs on both embedded micro-controllers and host compiler front-ends.

---

## 4. Phase 3 Scope & Algorithmic Hypotheses

Phase 3 will focus on formalizing, implementing, and empirically validating **TS-TFDR** under the following research hypotheses:

### Formal Hypotheses for Phase 3:
- **Hypothesis $H_1$ (Scope Exit Complexity):** TS-TFDR achieves deterministic $O(k)$ scope unwinding latency for a scope containing $k$ declarations, eliminating the $O(k \cdot d)$ probe-distance dependency of iterative backward shifts.
- **Hypothesis $H_2$ (Zero Tombstones & Dynamic Load Factor):** TS-TFDR preserves $100\%$ tombstone-free open addressing, maintaining table load factor $\alpha \ge 0.85$ without requiring rehash or compaction post-scope exit.
- **Hypothesis $H_3$ (Extended SSO / Arena Integration):** Integrating a block-allocated transient scope string arena with TS-TFDR eliminates dynamic heap allocations for names $>12\text{B}$ while retaining zero-allocation lookups.

---

## 5. Phase 3 Experimental Roadmap & Guardrails

### Execution Plan for Phase 3:
1. **Mathematical Formalization:** Write formal algorithm specifications, pseudo-code, invariant proofs, and complexity bounds for TS-TFDR.
2. **Isolated Reference Implementation:** Build TS-TFDR in an isolated header (`include/metis_ts_tfdr.hpp`) to ensure production `metis_x.hpp` remains untouched during early development.
3. **Differential & Correctness Auditing:** Verify TS-TFDR against `METIS-X` using randomized differential fuzzing and ASan/UBSan memory safety checks.
4. **Empirical Benchmarking & Paper Artifact:** Run TS-TFDR across all four canonical corpora and produce the L3 paper artifact and statistical validation report.

### Non-Negotiable Guardrails:
- Do not modify or break canonical datasets (`results/METIS_X_CANONICAL_DATASET.csv`).
- Do not commit or push to git repository.
- Preserve all existing Phase 0, Phase 1, and Phase 2 research deliverables.
