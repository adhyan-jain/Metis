# METIS Project Closure Record

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`

## 1. Project Summary and Final Disposition
The METIS project investigated optimizations for compiler symbol tables, culminating in the **METIS-X** architecture: a flat, open-addressing Robin Hood hash table integrating a 12-byte Small-String Optimization (SSO), tombstone-free backward-shift scope unwinding, and a bidirectional `frameIndex` for LIFO scope management. 

Canonical trace-replay benchmarks (`results/metis_x_benchmark.csv`) demonstrated substantial memory and latency improvements on synthetic and extracted AST workloads (e.g., a ~49% reduction in measured peak heap for the Zephyr trace compared to a conventional chained embedded baseline). However, attempts to integrate METIS-X into a real-world compiler (Tiny C Compiler) revealed a fundamental architectural mismatch: production static compilers heavily decouple identifier interning (lexing) from lexical scope resolution (parsing), rendering a raw-string-keyed scoped hash table counterproductive in that specific domain.

**Decision:** The current compiler-optimization research direction is STOPPED. No further algorithmic hunts or speculative domain pivots will be conducted.

## 2. Distinction of Claims and Evidence

### Facts established by source inspection
- **TCC Architecture:** The Tiny C Compiler uses integer tokens for AST name resolution, decoupling string hashing from scope shadowing.
- **Complexity:** METIS-X scope unwinding uses a backward shift that operates in $O(P)$ time (proportional to the probe cluster length), not $O(1)$.
- **Layout:** The `MetisXSlot` is 32 bytes but lacks an explicit `alignas(32)` directive, meaning it is not strictly bounded to cache-line alignments in standard vector allocations.

### Results measured by experiments
- **Trace Replay Baseline:** On the Zephyr corpus (`metis_x_benchmark.csv`), METIS-X reduced `measured_peak_heap_bytes` to 25.9 MB compared to 51.2 MB for the Embedded Conventional baseline.
- **Trace Replay Latency:** METIS-X achieved highly competitive lookup latencies ($0.098\mu\text{s}$ $p_{95}$ on ESP-IDF).

### Hypotheses that were rejected
- **Transactional Rollback (TS-TFDR):** Rejected because the overhead of tracking insertions outweighs the benefit of accelerating the rare scope-exit phase (Amdahl's Law).
- **String Arenas (Candidate D & F):** Rejected for this specific LIFO workload. Both monotonic and scope-tied `std::vector` string arenas caused severe peak memory bloat (+20% to +35%) due to geometric capacity scaling and fragmentation, demonstrating that standard allocator (`ptmalloc`) heap-fallback was optimal *for this specific benchmark*.

### Claims that remain unverified
- **L1 Cache Residency:** Inferences that $>90\%$ of lookups are served entirely within L1 cache remain unverified due to the absence of explicit hardware performance counters (e.g., `perf L1-dcache-load-misses`).

### Broad conclusions the evidence does NOT support
- The evidence does **not** support the claim that *every* static compiler architecture is fundamentally incompatible with scope-aware string data structures. We only verified specific implementations (TCC, Clang).
- The evidence does **not** support the claim that `ptmalloc` is universally optimal or that METIS-X is universally Pareto-optimal. It was optimal *only* under the specific trace-replay harness tested.
- The evidence does **not** establish that all conceivable application domains for METIS-X have been exhausted.

## 3. Status of the Phase 7 Manuscript
The Phase 7 manuscript (`research/l3/phase7/metis_x_paper.tex`) is **NOT SUBMISSION-READY** under its current compiler-optimization framing.
To be reconsidered for publication, the manuscript would require new evidence: specifically, end-to-end integration into a target domain that natively utilizes raw-string keys for scoped lookup, demonstrating wall-clock runtime speedups or whole-process peak memory reductions on a real workload.

## 4. Known Engineering Limitations
The canonical implementation of `MetisXTable` contains an unresolved exception-safety defect. As identified in Phase 6, if `slots_.resize(newCap)` throws `std::bad_alloc` during `rehash()`, heap-backed strings in the old array are permanently leaked, and the table state is left corrupted. The canonical code has deliberately not been patched. This project closure represents the end of the research phase, not a declaration of production readiness.

## 5. Unresolved Historical Inconsistencies
- **Memory Footprint Reporting:** Earlier Phase 2-3 reports cited a ~49% peak heap reduction derived from `results/metis_x_benchmark.csv`. The Phase 7 manuscript generated a table claiming a 4.51 MB footprint derived from `results/metis_x_ablation.csv` (which logged `final_heap_bytes` on a truncated/ablated run). The canonical benchmark CSV was identified as authoritative in Phase 8, but the historical markdown reports from Phases 3-7 containing the mismatched figures were preserved unmodified for archival integrity. To resolve this inconsistency in any future work, the `generate_tables.py` script must be rewritten to exclusively parse `metis_x_benchmark.csv`.

## 6. Conditions for Reopening the Project
The METIS project will remain closed unless a strictly defined opportunity arises. Reopening the project is justified **only if**:
1. A concrete, existing production system is identified.
2. Source-level inspection verifies that the system has a materially costly, scoped string-lookup path (e.g., a specific stream parser or dynamic interpreter).
3. The system's architecture natively operates on raw strings for scope resolution (no prior string-interning step bypasses the problem).
4. A feasible, semantically equivalent, controlled end-to-end comparison can be implemented to measure whole-process improvements.
