# METIS: Memory-Constrained Adaptive Symbol-Table Architecture

[![Build & Reproduce](https://img.shields.io/badge/reproducibility-automated-teal.svg)](./reproduce_all.sh)
[![Paper](https://img.shields.io/badge/paper-IEEE%20PDF-blue.svg)](./Metis_v2_IEEE.pdf)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](./LICENSE)

**METIS** is a research compiler symbol-table architecture that investigates the memory and latency boundaries of adaptive name representations under embedded resource constraints. Rather than storing all symbol names uniformly, METIS dynamically assigns identifiers to **INLINE**, **INTERNED**, or front-coded **COMPRESSED** representations based on identifier length, duplication, scope lifetime, and access frequency.

---

## Central Research Question

> *"Under what workload conditions can adaptive symbol-table name representations overcome the structural memory efficiency of an embedded conventional hash-table baseline while satisfying a latency constraint?"*

---

## Architectural Mechanisms

METIS (`SymTabV3`) integrates five core mechanisms to compact symbol storage while preserving sub-microsecond lookup throughput:

1. **3-Tier Representation Routing**:
   - **\textsc{Inline}**: Short identifiers ($\le 12$\,B) reside entirely within the 32-byte slot union (`inlineBytes[12]`), incurring zero secondary heap allocations.
   - **\textsc{Interned}**: Duplicated or hot symbols reference a global string pool via a 4-byte pool index.
   - **\textsc{Compressed}**: Long, cold identifiers are stored using front-coded block compression ($B=32$, anchor interval $A=8$).
2. **Scope-Lifetime Slot Recycling**: Immediate LIFO recycling of slots on scope exit (`exitScope()`), preventing heap fragmentation during lexical tree traversal.
3. **1-Byte Hash Fingerprints (`fp8`)**: Open-addressing slot indexing filters non-matching lookups in $O(1)$ time before attempting name comparison or decompression.
4. **Zero-Allocation Stack Buffer Decoding**: Compressed string reconstruction unpacks prefix/suffix slices directly into a stack-allocated buffer (`char stackBuf[512]`) via fast `memcpy`, eliminating dynamic heap allocations on the lookup path.
5. **Direct Anchor Index Calculation**: Anchors are computed in $O(1)$ time via arithmetic indexing ($\text{start} = \text{slot} - (\text{slot} \pmod A)$), removing backward scan loops.

---

## Canonical Empirical Results

The authoritative dataset is frozen in [`results/CANONICAL_FINAL_DATASET.csv`](./results/CANONICAL_FINAL_DATASET.csv) (433 configurations), measured using physical allocator profiling (`malloc_usable_size`).

### Embedded Evaluation Benchmark

| Workload | Unique Symbols | Metric | EmbeddedConventional | SymTabV3 (Proposed) | Difference / Outcome |
| :--- | :---: | :--- | :---: | :---: | :---: |
| **FreeRTOS** | 10,386 | Final Heap<br>Peak Heap<br>Lookup $p_{50}$<br>Lookup $p_{95}$ | 3.89 MB<br>4.23 MB<br>0.051 $\mu$s<br>0.094 $\mu$s | 4.72 MB<br>4.78 MB<br>0.071 $\mu$s<br>0.277 $\mu$s | +21.2% (Conv wins)<br>+13.0%<br>0.071 $\mu$s<br>Gate FAIL ($2.95\times$) |
| **Arduino** | 11,000 | Final Heap<br>Peak Heap<br>Lookup $p_{50}$<br>Lookup $p_{95}$ | 2.27 MB<br>2.31 MB<br>0.046 $\mu$s<br>0.089 $\mu$s | 2.99 MB<br>2.99 MB<br>0.061 $\mu$s<br>0.171 $\mu$s | +31.7% (Conv wins)<br>+29.4%<br>0.061 $\mu$s<br>Gate FAIL ($1.92\times$) |
| **Zephyr** | **228,739** | **Final Heap**<br>**Peak Heap**<br>Lookup $p_{50}$<br>**Lookup $p_{95}$** | **66.91 MB**<br>**76.37 MB**<br>0.050 $\mu$s<br>**0.125 $\mu$s** | **53.39 MB**<br>**57.89 MB**<br>0.066 $\mu$s<br>**0.228 $\mu$s** | **-20.2% (-13.52 MB)**<br>**-24.2% (-18.48 MB)**<br>0.066 $\mu$s<br>**Gate FAIL ($1.824\times$)** |
| **ESP-IDF** | 231,075 | Final Heap<br>Peak Heap<br>Lookup $p_{50}$<br>Lookup $p_{95}$ | 67.82 MB<br>75.77 MB<br>0.061 $\mu$s<br>0.236 $\mu$s | 81.91 MB<br>89.36 MB<br>0.079 $\mu$s<br>0.297 $\mu$s | +20.8% (Conv wins)<br>+17.9%<br>0.079 $\mu$s<br>Gate FAIL ($1.26\times$) |

### Summary of Key Findings

1. **Physical Memory Victory on Zephyr**: On large-scale embedded codebases ($N=2.6\text{M}$ events, 228k unique names), METIS achieves a **20.2% final heap reduction (13.52 MB saved)** and **24.2% peak heap reduction** relative to `EmbeddedConventional`.
2. **Tail-Latency Trade-off & Gate Failure**: On Zephyr, median lookup remains sub-microsecond ($p_{50} = 0.066\,\mu\text{s}$), but $p_{95}$ latency is $0.228\,\mu\text{s}$ vs $0.125\,\mu\text{s}$ for `EmbeddedConventional` ($1.824\times$). METIS **fails the $1.25\times$ $p_{95}$ latency constraint** due to front-coded prefix/suffix reconstruction copy work.
3. **SSO Boundary for Short Names ($L \le 15$\,B)**: Conventional hash tables storing names with Short String Optimization (SSO) incur 0 secondary heap bytes. The analytical break-even duplication ratio $k_{\text{breakeven}} < 0$, proving Conventional is structurally unbeatable on short names.
4. **Long-Identifier Break-Even Model ($L > 15$\,B)**: Interning and adaptive compression beat Conventional when:
   $$k > k_{\text{breakeven}}(L) = \frac{2L + 86}{L + 13}$$
   At $L=32$\,B, break-even occurs at $k \approx 3.33$, confirmed empirically by Synthetic Experiment D2.
5. **Disclosed Negative Result (`SymTabV4`)**: Moving metadata fields out of core entries into sparse side tables saves 8\,B on inline entries, but side-table container overheads ($T_{\text{table}}$) plus marginal entry costs ($\sim 34\text{--}57$\,B) exceed the savings across real identifier mixes (V4 loses to V3 on 19/20 real software corpora).

---

## End-to-End Reproduction

The complete test suite, benchmark harnesses, dataset reconciliation, figure generation, and LaTeX manuscript build run via:

```bash
./reproduce_all.sh
```

### Script Execution Steps
1. Compiles and executes correctness smoke tests and differential fuzzing tests (`tests/smoke_test.cpp`, `tests/differential_test.cpp`).
2. Builds all C++ benchmark binaries (`bin/embedded_bench`, `bin/real_world_bench`, `bin/synthetic_experiments`, `bin/multiseed_v4`).
3. Executes multi-repetition embedded benchmarks ($R=3$) with physical allocator heap profiling.
4. Reconciles the master dataset (`scripts/reconcile_canonical_dataset.py` &rarr; `results/CANONICAL_FINAL_DATASET.csv`).
5. Generates all 16 publication figures in `figures/`.
6. Compiles the IEEE conference paper (`metis_v2.tex` &rarr; `Metis_v2_IEEE.pdf`).

---

## Interactive Visualization Dashboard

A Next.js 16 + React 19 + Tailwind CSS research dashboard is provided in `frontend/`:

```bash
cd frontend
npm install
npm run dev
```

Open `http://localhost:3000` to inspect interactive memory diagnostics, break-even models, 26-corpus workload explorers, and latency distributions.

---

## Repository Structure

```
├── include/                     # Header-only C++ implementations
│   ├── symtab_v3.hpp            # Primary proposed architecture (SymTabV3)
│   ├── embedded_conventional_symbol_table.hpp  # Embedded baseline (EmbeddedConventional)
│   ├── conventional_symbol_table.hpp           # Host SSO baseline (ConventionalHost)
│   ├── symtab_v4.hpp            # Disclosed negative result (side-table metadata)
│   ├── heap_counter.hpp         # Physical allocator tracker (malloc_usable_size)
│   └── common.hpp               # Shared types, hash fingerprints, and tokens
├── src/                         # Benchmark and evaluation drivers
│   ├── embedded_bench_main.cpp  # Multi-repetition embedded benchmark suite
│   ├── real_world_bench_main.cpp# 20+ software corpora semantic event harness
│   ├── synthetic_experiments_main.cpp # Controlled experiments A–F (30 seeds)
│   └── multiseed_v4_main.cpp    # 30-seed V4 vs V3 statistical validation
├── tests/                       # Correctness & fuzzing test suites
│   ├── smoke_test.cpp           # Assert-based unit correctness test
│   └── differential_test.cpp    # Cross-implementation differential fuzzing test
├── scripts/                     # Python analysis and plotting scripts
│   ├── reconcile_canonical_dataset.py # Reconciles CANONICAL_FINAL_DATASET.csv
│   ├── plot_results.py          # Generates publication bar & latency charts
│   ├── plot_pareto.py           # Generates memory vs latency Pareto frontiers
│   └── generate_v4_evaluation_plots.py # Generates break-even curves & tables
├── results/                     # Authoritative data and claim audit records
│   ├── CANONICAL_FINAL_DATASET.csv # Master canonical dataset (433 rows)
│   ├── CANONICAL_AUDIT_LOG.md   # Reconciliation log & provenance audit
│   └── FINAL_CLAIMS_AUDIT.md    # 4-axis scientific claim classification
├── figures/                     # 16 regenerated publication figures
├── frontend/                    # Next.js research dashboard and simulator
├── metis_v2.tex                 # IEEE conference manuscript source
├── Metis_v2_IEEE.pdf            # Compiled publication paper
└── reproduce_all.sh             # Master single-command reproduction script
```

---

## Limitations and Boundary Conditions

1. **Host Workloads**: Conventional SSO hash tables remain optimal for host compilation workloads dominated by short identifiers ($L \le 15$\,B) and low duplication ($k < 1.5$).
2. **Tail Latency Penalty**: Front-coded compressed representations require memory copying during decompression, leading to higher $p_{95}$ tail latencies that fail tight $1.25\times$ latency gates on embedded targets.
3. **Global String Pool Persistence**: Interned strings remain in the global string pool for the duration of the compilation unit.
4. **Empirical Cost Model Specificity**: The break-even equation $k_{\text{breakeven}} = (2L+86)/(L+13)$ is specific to the 64-bit C++ container layout and physical allocator alignment overheads.
