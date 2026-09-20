# Result Provenance & Canonical Data Mapping

This document maps all experimental claims, performance figures, ablation statistics, and manuscript tables to their authoritative raw canonical datasets and verification drivers in the repository.

---

## 1. Primary Canonical Datasets

| Dataset | File Path | Scope & Description | Execution Command |
| :--- | :--- | :--- | :--- |
| **Phase II Canonical Dataset** | `results/METIS_X_CANONICAL_DATASET.csv` | Canonical Phase II real-world benchmark suite (Zephyr OS & ESP-IDF header graphs, $R=7$ runs under core isolation) | `./reproduce_all.sh phase2` |
| **Phase I Embedded Dataset** | `results/embedded_benchmark.csv` | Microbenchmark & synthetic evaluation dataset across varying scope depths and lookup ratios | `./reproduce_all.sh phase1` |

---

## 2. Table & Claim Traceability Matrix

### Claim 1: Zephyr RTOS Physical Memory & Latency Reduction
* **Manuscript Location**: Section IV, Table I & Figure 3.
* **Canonical Empirical Metrics**:
  - Embedded Baseline Memory: $41.74\text{ MB}$ ($41,742,312\text{ bytes}$) $\to$ METIS-X Memory: $25.79\text{ MB}$ ($25,789,424\text{ bytes}$) ($-38.2\%$ RAM reduction).
  - Embedded Baseline p95 Latency: $0.119\ \mu\text{s}$ $\to$ METIS-X p95 Latency: $0.083\ \mu\text{s}$ ($-30.2\%$ latency reduction).
* **Source Dataset**: `results/METIS_X_CANONICAL_DATASET.csv` (`workload == "zephyr"`).

### Claim 2: ESP-IDF Header Graph Physical Memory & Latency Reduction
* **Manuscript Location**: Section IV, Table I & Figure 3.
* **Canonical Empirical Metrics**:
  - Embedded Baseline Memory: $42.65\text{ MB}$ ($42,651,920\text{ bytes}$) $\to$ METIS-X Memory: $35.20\text{ MB}$ ($35,196,200\text{ bytes}$) ($-17.5\%$ RAM reduction).
  - Embedded Baseline p95 Latency: $0.136\ \mu\text{s}$ $\to$ METIS-X p95 Latency: $0.070\ \mu\text{s}$ ($-48.5\%$ latency reduction).
* **Source Dataset**: `results/METIS_X_CANONICAL_DATASET.csv` (`workload == "esp-idf"`).

### Claim 3: Zero Dynamic Heap Allocations During Hot-Path Lookups
* **Manuscript Location**: Section IV-B, "Allocation Mechanics".
* **Canonical Empirical Metrics**: $0$ dynamic heap allocations (`operator new` / `malloc`) across $2,863,367$ symbol lookup operations in real-world AST execution traces.
* **Source Executable**: `bin/metis_x_instr` compiled from `src/metis_x_instrumentation_main.cpp`.

### Claim 4: Component Ablation Waterfall ($87.1\%$ Physical Memory Reduction)
* **Manuscript Location**: Section IV-C, Table II & Figure 4.
* **Canonical Empirical Progression**:
  - $A_2$ (Flat OA Robin Hood with dynamic per-string heap allocations): $43.44\text{ MB}$ ($43,436,720\text{ bytes}$).
  - $A_5$ (Full METIS-X with 32B inline slots + scope recycling): $5.59\text{ MB}$ ($5,593,608\text{ bytes}$).
  - Physical RAM footprint reduction: $87.1\%$.
* **Source Dataset**: `results/metis_x_ablation.csv` (`suite == "ablation"`).

---

## 3. Replication Environment Rules

To reproduce exact numerical values:
1. **CPU Pinning**: Benchmark executables must be bound to a single physical core using `taskset -c 0`.
2. **Frequency Governor**: Lock CPU frequency scaling to high performance (`cpupower frequency-set -g performance`).
3. **Repetition & Median**: Benchmarks execute $R=7$ iterations, reporting the median value with relative standard deviation verified below $2.5\%$.
