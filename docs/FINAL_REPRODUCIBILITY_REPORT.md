# FINAL REPRODUCIBILITY REPORT

**Repository**: `adhyan-jain/Metis`  
**Author**: Adhyan Jain  
**Date**: September 18, 2026  
**Status**: Research Completion & Submission Freeze Verified  

---

## 1. Master Reproduction Pipeline

The entire research verification and compilation pipeline is automated and reproducible with a single command:

```bash
./reproduce_all.sh
```

### Reproducible Procedure vs. Machine-Sensitive Numeric Timings

> [!IMPORTANT]
> **Procedural Determinism**: The build steps, test suites, dataset reconciliation, figure generation scripts, and LaTeX document compilation are fully automated and deterministically reproducible.
>
> **Numeric Timing Sensitivity**: Microsecond-level execution times ($p_{50}, p_{95}, p_{99}$) and memory profiling outputs are inherently sensitive to hardware architecture, operating system scheduling, CPU cache hierarchy, and host memory allocator implementations. Multi-repetition execution ($R \ge 3$) and physical allocator inspection (`malloc_usable_size`) are utilized to minimize jitter, but exact numeric timing figures will naturally vary across differing host environments.

---

## 2. Reproduction Pipeline Structure

The script `./reproduce_all.sh` executes the following 6-step pipeline:

1. **Correctness & Differential Verification**:
   - Compiles `tests/smoke_test.cpp` and `tests/differential_test.cpp` (`-std=c++14 -O2`).
   - Executes 200 fuzzing trace cycles verifying semantic symbol-resolution agreement between all implementations and detecting memory leaks.
2. **Benchmark Harness Compilation**:
   - Compiles `src/embedded_bench_main.cpp`, `src/real_world_bench_main.cpp`, `src/synthetic_experiments_main.cpp`, and `src/multiseed_v4_main.cpp`.
3. **Embedded Benchmark Execution**:
   - Executes multi-repetition benchmark runs ($R=3$) with isolated physical heap profiling (`malloc_usable_size`) across \textsc{FreeRTOS}, \textsc{Arduino}, \textsc{Zephyr}, and \textsc{ESP-IDF}.
   - Generates `results/embedded_benchmark.csv`.
4. **Canonical Dataset Reconciliation**:
   - Runs `scripts/reconcile_canonical_dataset.py` unifying embedded benchmarks, 20+ software corpora, synthetic suites A–F, and multiseed V4 data into `results/CANONICAL_FINAL_DATASET.csv` (433 rows).
   - Generates `results/CANONICAL_AUDIT_LOG.md`.
5. **Publication Figure Regeneration**:
   - Executes `scripts/plot_results.py`, `scripts/plot_pareto.py`, and `scripts/generate_v4_evaluation_plots.py` regenerating all 16 figures in `figures/`.
6. **Manuscript Compilation**:
   - Compiles `metis_v2.tex` using `pdflatex` (2 passes) to produce `metis_v2.pdf` and `Metis_v2_IEEE.pdf`.

---

## 3. Environment and Toolchain Specification

- **C++ Compiler**: GCC 6.3.0+ / GCC 14+ / GCC 16+ supporting `-std=c++14 -O2 -Wall -Wextra`.
- **Python Environment**: Python 3.10+ with `matplotlib`, `numpy`, `pandas`, `scipy`, `scikit-learn` (`requirements.txt`).
- **LaTeX Distribution**: `pdflatex` with standard IEEEtran class and packages (`amsmath`, `booktabs`, `cite`, `graphicx`, `hyperref`).
- **Operating System**: Linux x86_64 / POSIX.

---

## 4. Authoritative Artifact Verification

| Component | Target Output | Ground Truth File | Status |
|---|---|---|---|
| **Embedded Suite** | FreeRTOS, Arduino, Zephyr, ESP-IDF | `results/embedded_benchmark.csv` | **REPRODUCED** |
| **Host Software Corpora** | 20+ Real-World Corpora | `data/real_world_benchmark.csv` | **REPRODUCED** |
| **Synthetic Suite** | Workloads A–F ($N=30$ seeds) | `results/synthetic_experiments_A_F.csv` | **REPRODUCED** |
| **Multiseed V4 Suite** | Side-Table Negative Result | `results/multiseed_v4_summary.csv` | **REPRODUCED** |
| **Canonical Master Dataset** | 433 Harmonized Data Rows | `results/CANONICAL_FINAL_DATASET.csv` | **REPRODUCED** |
| **Manuscript Figures** | 16 Publication PNGs | `figures/*.png` | **REPRODUCED** |
| **IEEE Conference Paper** | 5-Page Paper | `Metis_v2_IEEE.pdf` | **COMPILED CLEANLY** |
