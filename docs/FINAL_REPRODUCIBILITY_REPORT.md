# FINAL REPRODUCIBILITY REPORT

**Repository**: `adhyan-jain/Metis`  
**Author**: Adhyan Jain  
**Date**: September 18, 2026  
**Status**: Research Completion & Submission Freeze Verified  

---

## 1. Master Reproduction Command

The entire research pipeline is automated and verified with a single script:

```bash
./reproduce_all.sh
```

This master script deterministically executes:
1. **Sanitizer & Differential Correctness**: Compiles and runs `tests/smoke_test.cpp` and `tests/differential_test.cpp` under GCC 14/16 verifying zero memory leaks and complete symbol-resolution agreement across implementations.
2. **Benchmark Harness Compilation & Multi-Repetition Execution**: Compiles `src/embedded_bench_main.cpp`, `src/real_world_bench_main.cpp`, `src/synthetic_experiments_main.cpp`, and `src/multiseed_v4_main.cpp`, executing multi-repetition benchmark runs with isolated physical allocator profiling (`malloc_usable_size`).
3. **Canonical Dataset Reconciliation**: Runs `scripts/reconcile_canonical_dataset.py` generating `results/CANONICAL_FINAL_DATASET.csv` (433 rows) and `results/CANONICAL_AUDIT_LOG.md`.
4. **Publication Figure Regeneration**: Runs `scripts/plot_results.py`, `scripts/plot_pareto.py`, and `scripts/generate_v4_evaluation_plots.py` generating all publication charts in `figures/`.
5. **LaTeX Manuscript Build**: Builds `metis_v2.tex` to `metis_v2.pdf` and copies to `Metis_v2_IEEE.pdf`.

---

## 2. Verified Empirical Findings & Data Provenance

| Component | Target Output | Ground Truth File | Status |
|---|---|---|---|
| **Embedded Suite** | FreeRTOS, Arduino, Zephyr, ESP-IDF | `results/embedded_benchmark.csv` | **REPRODUCED** |
| **Host Software Corpora** | 20+ Real-World Corpora | `data/real_world_benchmark.csv` | **REPRODUCED** |
| **Synthetic Suite** | Workloads A–F ($N=30$ seeds) | `results/synthetic_experiments_A_F.csv` | **REPRODUCED** |
| **Multiseed V4 Suite** | Side-Table Negative Result | `results/multiseed_v4_summary.csv` | **REPRODUCED** |
| **Canonical Dataset** | 433 Harmonized Data Rows | `results/CANONICAL_FINAL_DATASET.csv` | **REPRODUCED** |
| **Manuscript Figures** | 16 Publication PNGs | `figures/*.png` | **REPRODUCED** |
| **IEEE Conference Paper** | 5-Page Paper | `Metis_v2_IEEE.pdf` | **COMPILED CLEANLY** |

---

## 3. Scientific Verification Checklist

- [x] All benchmark measurements derived from physical allocator heap profiling (`malloc_usable_size`).
- [x] Multi-repetition timing ($R \ge 3$) verified with latency variance metrics ($\sigma / \mu < 0.05$).
- [x] No artificial performance wins forced for SymTabV3 over Conventional.
- [x] Failed latency gate on Zephyr ($1.824\times$ $p_{95}$ vs $1.25\times$ target) explicitly documented with front-coded decode analysis.
- [x] Analytical break-even derivation ($k_{\text{breakeven}} = (2L+86)/(L+13)$) confirmed by synthetic experiments ($k \ge 3.33$ at $L=32$B).
- [x] SymTabV4 side-table overhead negative result fully preserved and explained.
- [x] All 433 rows in `CANONICAL_FINAL_DATASET.csv` match paper tables and text values.
