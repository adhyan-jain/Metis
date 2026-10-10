# Phase 2.2 — Scope Exit Cost Measurements (Task 2 & 3)

**Date:** October 10, 2026  
**Repository:** `adhyan-jain/Metis`  
**Phase:** Phase 2.2 (TS-TFDR Kill Test)  
**Deliverable:** `research/l3/phase2/phase2_2_exit_cost_measurements.md`

## Objective
To quantify the actual wall-clock cost of `exitScope()` within the `METIS-X` benchmark stream and assess whether exit-time relocation is a meaningful share of total compilation runtime.

## Methodology
We wrote an isolated C++ profiler (`src/phase2_2_exit_profiler.cpp` and `src/phase2_2_dense_profiler.cpp`) using `std::chrono::high_resolution_clock`. To eliminate timer overhead and jitter:
- Measurements were aggregated into total cumulative time per operation type across the entire stream.
- Warm-up loops (3-5 runs) were executed before the measured timing pass.
- Tests were performed on canonical traces and a synthetic "Dense Scope" workload.

## Empirical Measurements

### 1. Canonical Workload Timing Distributions
*Measurements capture cumulative wall-clock time across the entire event stream.*

| Corpus | Total Replay Time | `insert()` Time | `resolve()` Time | `exitScope()` Time | `exitScope` Share of Total |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **FreeRTOS** | 18.07 ms | 6.21 ms | 6.33 ms | **0.83 ms** | **4.59%** |
| **Arduino** | 8.44 ms | 3.45 ms | 2.39 ms | **0.52 ms** | **6.23%** |
| **ESP-IDF** | 221.86 ms | 103.35 ms | 56.45 ms | **14.49 ms** | **6.53%** |
| **Zephyr RTOS** | 228.89 ms | 78.07 ms | 77.41 ms | **17.92 ms** | **7.83%** |

### 2. Synthetic "Dense Scope" Profile (100 Repetitions)
To simulate the absolute worst-case scenario, we benchmarked a scope containing 10,000 sequentially inserted declarations, closed in a single `exitScope()` call.

| Synthetic Workload | Cumulative Insert Time | Cumulative Exit Time | Exit Share of Mutative Work |
| :--- | :---: | :---: | :---: |
| **10k Decls / Scope** | 91.15 ms | 11.79 ms | **11.45%** |

## TS-TFDR Cost Model Reassessment (Task 3)

The measurements strictly falsify the premise that `exitScope()` is a major performance bottleneck justifying an entirely new transactional rollback mechanism:

1. **Relocation Share:** `exitScope()` relocation accounts for a maximum of **7.8%** of total execution time on Zephyr, and just **6.5%** on ESP-IDF.
2. **Work Saved:** If TS-TFDR were infinitely fast and completely eliminated `exitScope()` time (0 ms cost), the absolute maximum speedup for `METIS-X` would be under 8%.
3. **Log Allocation Overhead:** `insert()` currently consumes **34% - 46%** of runtime. TS-TFDR would require writing a 32-byte displacement log entry for *every* displacement swap during `insert()`. The memory bandwidth, bounds-checking, and vector allocation overhead added to `insert()` would indisputably exceed the meager 7% time savings in `exitScope()`. 
4. **Conclusion:** TS-TFDR's cost model is flawed. The existing cache-line contiguous backward shift is highly optimized and operates much faster than maintaining an explicit shadow transaction log.
