# Reproducibility Specification & Execution Guide

## 1. Executive Summary & Pipeline Overview

This document provides complete instructions for reproducing all experimental results, statistical tests, ML evaluations, and publication figures for the **`SymTabV2`** research project.

A single-entry-point script is provided at the root of the repository:

```bash
./run_research_experiments.sh
```

Running `./run_research_experiments.sh` automatically compiles all code, executes unit and differential tests, extracts real-world semantic traces, runs the complete research benchmark suite, performs statistical hypothesis testing ($N=30$ seeds), evaluates latency-constrained ML policies, and renders publication-quality figures.

---

## 2. Hardware, OS, & System Toolchain Specifications

### Target Environment Assumptions:
- **Operating System**: Linux (x86_64 architecture, kernel version 5.x/6.x). Tested on Linux 6.x.
- **CPU Requirements**: Any modern x86_64 processor with high-resolution CPU timer support (`std::chrono::high_resolution_clock`).
- **Memory Requirements**: Minimum 2 GB physical RAM.
- **Disk Requirements**: Minimum 100 MB available disk space.

### Compiler & Linker Toolchain:
- **C++ Compiler**: `g++` (GCC) version 6.3.0 or higher (Tested on GCC 16.2.1 / GCC 11.4.0).
- **C++ Standard**: `-std=c++14` (Compatible with GCC 6.3+ / MinGW toolchains).
- **Optimization Flags**: `-O2 -Wall -Wextra`
- **Sanitizer Flags (Verification)**: `-fsanitize=address,undefined -g`

### Python Runtime & Dependencies:
- **Python Version**: `Python 3.10` or higher (Tested on Python 3.14.7 / 3.10+).
- **Python Package Dependencies**:
  - `numpy` (Version 1.20+)
  - `scikit-learn` (Version 1.0+, optional; fallback numpy classifier included)
  - `matplotlib` (Version 3.4+, optional; figure scripts handle missing matplotlib gracefully)
- **Virtual Environment**: Pre-configured virtual environment in `venv/` is automatically used by `./run_research_experiments.sh` if present.

---

## 3. Dataset & Seed Specifications

### Synthetic Workloads:
- **Deterministic Baseline Seed**: Seed `1337` (used for single-run benchmarks, pareto sweeps, parameter study, and ablations).
- **Multiseed Independent Seeds**: Seeds $1000 \dots 1729$ ($N=30$ independent random seeds per workload family) for Section 15 statistical validation.
- **Workload Families (8)**: `small`, `medium`, `large`, `nested-scopes`, `high-prefix-similarity`, `hot-cold-access`, `memory-stress`, `random-long`.

### Real-World Corpora:
- **Source Code Repositories**:
  - `FreeRTOS`: Amazon FreeRTOS kernel (`corpora/freertos`)
  - `Arduino Core`: Arduino AVR Core library (`corpora/arduino-core`)
  - `Zephyr`: Zephyr RTOS codebase (`corpora/zephyr`)
- **Extraction Command**: `python scripts/extract_corpus_events.py`
- **Pre-extracted Event Files**: Pre-generated event traces are committed in `results/corpus_events_FreeRTOS.txt`, `results/corpus_events_Arduino.txt`, and `results/corpus_events_Zephyr.txt` so reproduction does not strictly require re-downloading external source trees.

---

## 4. End-to-End Pipeline Execution Sequence

When `./run_research_experiments.sh` is executed, it runs the following 11-step pipeline:

```
[Step 1] Environment & Toolchain Audit
   ↓
[Step 2] Compilation of Executables (g++ -std=c++14 -O2)
   ↓
[Step 3] Unit & Differential Test Suite Verification
   ↓
[Step 4] Real-World Corpus Semantic Event Trace Extraction & Evaluation
   ↓
[Step 5] Core V2 Latency & Memory Benchmark (Section 9)
   ↓
[Step 6] Latency-Constrained Pareto Optimization Sweep (Section 12)
   ↓
[Step 7] Systematic Parameter Sensitivity Study (Section 13)
   ↓
[Step 8] Comprehensive V2 Mechanism Ablation Study (Section 14)
   ↓
[Step 9] Multiseed Statistical Validation (N=30 Seeds, Section 15)
   ↓
[Step 10] Latency-Constrained ML Policy Evaluation & Audit (Section 16)
   ↓
[Step 11] Publication Figure & Table Generation
```

---

## 5. Output File Registry & Traceability Map

All benchmark outputs, raw observations, statistical summaries, and figures are written to `results/` and `figures/`:

| Output File Path | Primary Generating Script / Tool | Relevant Research Section | Contents & Purpose |
| :--- | :--- | :--- | :--- |
| `results/v2_latency_memory.csv` | `./v2_benchmark.exe` | Section 9 | Core V2 latency and memory measurements across synthetic baselines. |
| `results/corpus_characterization.csv` | `scripts/extract_corpus_events.py` | Section 10 | 15 characterization metrics for real-world corpora. |
| `results/corpus_benchmark.csv` | `./corpus_event_bench.exe` | Section 10 | Real-world corpus symbol table performance evaluation. |
| `results/pareto_results.csv` | `./pareto.exe` | Section 12 | Full 1,700-configuration Pareto evaluation grid across 12 workloads. |
| `results/pareto_frontier.csv` | `./pareto.exe` | Section 12 | Non-dominated Pareto frontier and latency-constrained optimal configs. |
| `results/parameter_sweep.csv` | `./parameter_study.exe` | Section 13 | Systematic 592-run parameter study across 5 threshold axes. |
| `results/ablation.csv` | `./ablation.exe` | Section 14 | 90-run ablation matrix isolating major V2 architectural components. |
| `results/multiseed_v2_raw.csv` | `./statistical_validation.exe` | Section 15 | 1,680 raw observations across $N=30$ independent random seeds. |
| `results/statistical_summary.csv` | `scripts/statistical_validation.py` | Section 15 | Aggregated means, medians, 95% CIs, paired $t$-tests, and Cohen's $d$. |
| `results/ml_comparison.csv` | `scripts/train_ml_oracle.py` | Section 16 | Policy selection evaluation across 5 models and 4 latency bounds. |
| `results/ml_validation.md` | `scripts/train_ml_oracle.py` | Section 16 | Detailed ML audit report, LOWO CV, and held-out evaluation metrics. |
| `results/adversarial_review.md` | Hostile Review Audit | Section 18 | Hostile peer review, issue classification, and claim revisions. |
| `figures/pareto_memory_vs_latency.png` | `scripts/plot_pareto.py` | Section 22 | Memory vs Cold $p_{50}$ Latency Pareto frontier visualization. |
| `figures/memory_usage.png` | `scripts/plot_results.py` | Section 22 | Physical heap memory footprint comparison across workloads. |
| `figures/ablation_memory.png` | `scripts/plot_results.py` | Section 22 | Ablation impact on physical heap memory across V2 variants. |

---

## 6. Documented Limitations & Reproduction Notes

1. **Timing Variance across Hardware**: Lookup latencies ($p_{50}$, $p_{95}$ in microseconds) depend on CPU clock frequency, L1/L2 cache size, and operating system scheduling jitter. While relative latency ratios ($\frac{L_{\text{V2}}}{L_{\text{Conv}}}$) remain highly stable across environments, absolute microsecond timing values will reflect the local hardware host.
2. **Corpora Extraction Requirements**: If external source code directories under `corpora/` are missing, `./run_research_experiments.sh` automatically uses the committed pre-extracted event files (`results/corpus_events_*.txt`), preserving full benchmark reproducibility without external network downloads.
3. **No Proprietary Dependencies**: The pipeline relies strictly on standard C++14 headers and standard Python 3 libraries (`csv`, `math`, `numpy`). No proprietary cloud APIs or licenses are required.
