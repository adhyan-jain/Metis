# Phase 3 Evidence Audit & Reproducibility Assessment

**Date:** October 10, 2026  
**Repository:** `adhyan-jain/Metis`  
**Revision:** HEAD `@0b4344050873968a7f3f940d47f8b46f50a23353`  
**Phase:** Phase 3 (Paper-First Pivot)  
**Deliverable:** 1 of 3 (`research/l3/phase3_evidence_audit.md`)

---

## 1. Repository State & Phase 2.2 Verification

### Current Environment
- **Branch:** `main` (Verified clean working tree with only untracked Phase 2/3 artifacts and Phase 1 boundary fixes in `include/metis_x.hpp` and `.gitignore`).
- **Safety Guarantee:** No canonical dataset overrides, no git commits, no pushes.

### Audit of Phase 2.2 Conclusions
We independently audited the conclusions of Phase 2.2 (the TS-TFDR Kill Test):
1. **Layout Restoration Proof (`phase2_2_layout_restoration_test.md`):** Verified. Robin Hood backward-shift natively restores the exact physical slot layout because `insert()` is stable (it only swaps when incoming probe distance is strictly greater, preserving sequential order for ties). `backwardShift()` strictly pulls the contiguous sequence back, meaning exact reverse-deletion flawlessly undoes displacement.
2. **`exitScope()` Cost Profiling (`phase2_2_exit_cost_measurements.md`):** Verified. The measurements correctly captured cumulative wall-clock time in a batched execution harness. `exitScope()` accounts for 4.5%–7.8% of the total event loop. Amdahl's Law dictates that eliminating `exitScope()` entirely yields at most a $<8\%$ speedup.
3. **89.87% Long-Identifier Claim:** 
   - **Finding:** The figure is mathematically reproducible from `metis_x_instrumentation.csv`, but its phrasing is dangerously ambiguous. 
   - **Correction:** The 89.87% figure represents the percentage of **Final Live Entries (global namespace scope)** in the ESP-IDF trace that are heap-allocated ($137,968$ heap slots / $153,518$ total live slots). It does **not** mean 89.87% of all *cumulative insert operations* hit the heap, as transient local variables are statistically shorter than global SDK macros.

---

## 2. Claim-by-Claim Evidence Table

This table maps the core performance claims of METIS-X to their exact reproducible evidence base in the repository.

| Claim | Measurement Source | Baseline & Comparator | Sample / Workload | Verification Status | Safe Paper Wording |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Physical Memory Reduction** | `metis_x_bench_main.cpp` output in `METIS_X_CANONICAL_DATASET.csv` (lines 10-17). | vs `EmbeddedConventional` (flat linear probing arena). | Zephyr ($2.65\text{M}$ events), ESP-IDF ($2.42\text{M}$ events), 5 repetitions. | **VERIFIED.** ($25.0\text{ MB}$ vs $51.2\text{ MB}$ peak heap on Zephyr). | "METIS-X reduces peak physical heap footprint by $51\%$ compared to flat linear-probing arena tables on the Zephyr AST trace." |
| **Lookup Latency Superiority** | `METIS_X_CANONICAL_DATASET.csv` (lines 11, 15). | vs `EmbeddedConventional` and `ConventionalHost`. | Zephyr / ESP-IDF, $p_{95}$ latency on core-pinned Linux. | **VERIFIED.** ($0.081\mu\text{s}$ vs $0.187\mu\text{s}$ on ESP-IDF). | "METIS-X achieves a $p_{95}$ lookup latency of $0.081\mu\text{s}$ on ESP-IDF, outperforming conventional linear probing baselines." |
| **Zero Lookup Allocations** | `src/metis_x_instrumentation_main.cpp` ($\to$ `metis_x_instrumentation.csv`). | N/A (Absolute invariant). | $2.81\text{M}$ total trace lookups. | **VERIFIED.** $0$ calls to `operator new` on hot lookup path. | "The METIS-X lookup path executes zero dynamic memory allocations, eliminating allocator jitter during symbol resolution." |
| **Scope Exit Overhead** | `src/phase2_2_exit_profiler.cpp` output. | Cumulative time share of execution loop. | All 4 canonical workloads. | **VERIFIED.** $4.5\% - 7.8\%$ of total time. | "Empirical profiling demonstrates that backward-shift LIFO scope unwinding consumes less than $8\%$ of total symbol table execution time." |
| **Shadowing Degradation** | `src/metis_x_failure_case_main.cpp` ($\to$ `metis_x_failure_cases.csv`, line 8). | Unshadowed lookups. | Synthetic 20-level shadow nesting. | **VERIFIED.** Latency spikes to $0.186\mu\text{s}$. | "Extreme lexical shadowing (20 levels) introduces multi-layer probing overhead, degrading $p_{95}$ latency by up to $169\%$." |

---

## 3. Fair Baseline Assessment

The current benchmarks compare METIS-X against three core baselines:
1. `EmbeddedConventional`: A flat, linear-probing array with contiguous string arena. This is a highly realistic baseline representing typical embedded C compiler architectures (e.g., LCC).
2. `ConventionalHost`: Standard C++ `std::vector<std::unordered_map<std::string, Meta>>`. This correctly represents the baseline naive implementation most users default to.
3. `SymTabV3`: The complex 3-tier compressed front-coded implementation.

**Conclusion:** The benchmark harness is fair and valid. `METIS-X` is not a strawman winner; it beats a highly optimized C-style flat arena (`EmbeddedConventional`) on both memory (by avoiding arena compaction fragmentation) and latency (via 32B cache alignment and Robin Hood load factor density).
