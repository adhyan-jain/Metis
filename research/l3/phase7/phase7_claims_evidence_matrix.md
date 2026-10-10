# Phase 7: Claims and Evidence Matrix

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`

This matrix freezes all quantitative and algorithmic claims, strictly delineating what the evidence actually supports.

| Exact Claim | Evidence Source | Verification Status | Allowed Wording | Forbidden Wording |
| :--- | :--- | :--- | :--- | :--- |
| **32-byte Slot Size** | `include/metis_x.hpp`, `sizeof(MetisXSlot) == 32` | `VERIFIED` | "The slot is strictly bounded to 32 bytes." | "Cache-aligned," "L1 boundary aligned." (Lacks `alignas(32)`). |
| **Scope Unwinding Complexity** | Source analysis (Phase 6) | `CONTRADICTED` | "Scope unwinding executes without tombstones via $O(P)$ backward-shifting." | "$O(1)$ scope unwinding." (It scales with probe distance). |
| **Peak Memory Reduction** | `results/metis_x_ablation.csv` | `VERIFIED` | "Reduces peak heap memory by 78% vs Embedded Conventional on Zephyr." | "Optimal memory," "Best possible layout." |
| **$p_{95}$ Lookup Latency** | `results/metis_x_ablation.csv` | `VERIFIED` | "Achieves $0.098 \mu\text{s}$ $p_{95}$ latency on ESP-IDF traces." | "Zero-latency," "Faster than all hash tables." |
| **SSO and Identifier Length** | Phase 1 dataset analysis | `VERIFIED` | ">90% of local variables fit within the 12-byte SSO buffer." | "90% of strings require no allocation." (Global macros are longer). |
| **L1 Cache Residency** | Phase 6 Hardware Audit | `UNVERIFIED` | "The 32-byte density increases L1 cache payload capacity." | ">90% of lookups are served entirely within L1 cache." (No perf counters). |
| **Exception Safety** | Source analysis (Phase 6) | `SUPPORTED_WITH_SCOPE_LIMITATION` | "Insertion isolates `bad_alloc` before state mutation." | "Completely exception safe." (Rehash contains a known resource leak). |
| **Zero Allocation Lookup** | `metis_x.hpp` `resolve()` | `VERIFIED` | "The hot lookup path performs zero dynamic allocations." | N/A |
| **Arena Defeat (Cand D/F)** | `research/l3/phase4` & `phase5` | `VERIFIED` | "Monotonic and capacity-scaled arenas increased peak heap by >20%." | "Arenas are always worse." (Specific to this workload/trace). |
| **TS-TFDR Rejection** | `research/l3/phase2` | `VERIFIED` | "Transactional logs degraded hot-path insertion to optimize a <8% unwinding phase." | "Undo logs never work." |
| **End-to-End Compiler Speedup** | N/A | `NOT_APPLICABLE` | "Evaluated via trace-replay of compiler event logs." | "Improves Clang compile times by X%." |
| **Algorithmic Novelty** | Phase 6 Audit | `CONTRADICTED` | "An architectural synthesis of Robin Hood hashing, SSO, and scope backlinks." | "Novel algorithm," "First-of-its-kind data structure." |
