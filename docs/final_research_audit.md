# Final Research Audit & Scientific Synthesis

**Date**: September 17, 2026  
**Repository**: `shubhisingh1510/compiler`  
**Status**: Comprehensive Final Evaluation & Claims Audit (Architecture Frozen)

---

## Executive Summary & Core Research Question

This document provides the canonical, frozen research audit for the `SymTab` compiler symbol table investigation. The primary research question addressed by this evaluation is:

> **"Under what workload conditions can adaptive symbol-table name representations overcome the structural memory efficiency of an embedded conventional hash-table baseline while satisfying a latency constraint?"**

The evaluation synthesizes physical allocator-measured heap memory (`heap_counter.hpp`), $p_{50}$/$p_{95}$/$p_{99}$ cold lookup latencies across 4 real embedded workloads (\textsc{FreeRTOS}, \textsc{Arduino}, \textsc{Zephyr}, \textsc{ESP-IDF}), 20 host software corpora, controlled synthetic microbenchmarks A–F, and theoretical break-even derivations.

---

## 1. Key Empirical & Theoretical Findings

1. **Physical Allocator Memory Victory on Zephyr**:
   On the large \textsc{Zephyr} workload (2.6M events, 228,739 unique symbols), `SymTabV3` achieves a **20.2% (13.52 MB)** physical final heap reduction over `EmbeddedConventional` (53.39 MB vs 66.91 MB final heap) and **34.3% (27.83 MB)** over `Interned` (53.39 MB vs 81.22 MB final heap). Peak heap memory is reduced by 24.2% (57.89 MB vs 76.37 MB).

2. **Quantified Memory/Latency Trade-Off**:
   Zero-allocation compressed-path optimization reduced \textsc{Zephyr} $p_{95}$ latency from $0.547\,\mu\text{s}$ down to $0.228\,\mu\text{s}$ without altering memory footprint. Median lookup latency is sub-microsecond ($p_{50} = 0.066\,\mu\text{s}$).
   However, comparing `SymTabV3` $p_{95}$ ($0.228\,\mu\text{s}$) against `EmbeddedConventional` $p_{95}$ ($0.125\,\mu\text{s}$) yields a ratio of **$1.824\times$**, failing the $1.25\times$ $p_{95}$ latency gate. `SymTabV3` trades $0.103\,\mu\text{s}$ of added $p_{95}$ latency for 13.52 MB of physical final heap saved ($131.26\text{ MB}$ saved per $\mu\text{s}$ of added $p_{95}$ latency).

3. **Source of Latency Trade-Off**:
   During execution, 8.81% of lookups on \textsc{Zephyr} hit front-coded \textsc{Compressed} entries. Reconstructing front-coded members requires copying multiple prefix and suffix segments across block members (mean depth $3.31$ steps to anchor). This introduces measured reconstruction/copy overhead that prevents satisfying the $1.25\times$ $p_{95}$ latency gate.

4. **Analytical Break-Even Boundary Verified by Synthetic Experiment D2**:
   The theoretical inequality:
   $$\text{For } L > 15\text{B}, \quad k_{\text{breakeven}} = \frac{2L + 86}{L + 13}$$
   is empirically confirmed by controlled synthetic experiment D2 ($L = 32$B, break-even at $k \approx 3.33$).

5. **Disclosed Negative Result (\texttt{SymTabV4})**:
   `SymTabV4` (representation-conditional metadata) lost to `SymTabV3` on 19/20 real software corpora and lost to Conventional on 20/20. Sparse side-table container allocations ($T_{\text{table}}$) plus marginal entry costs ($\sim 34\text{--}57$\,B) exceed core-struct scalar savings.

6. **Host-Side 20-Corpus Characterization**:
   On 64-bit host systems, `ConventionalHost` (\texttt{std::unordered\_map}) achieves the lowest physical heap footprint on 20/20 real software corpora due to short SSO identifier dominance ($59\%$--$76\%$) and low live duplication ($k < 1.5$).

---

## 2. Canonical Multi-Workload Matrix

| Workload | Architecture / Model | Peak Heap | Final Heap | $p_{50}$ Latency | $p_{95}$ Latency | $p_{99}$ Latency | Final RAM vs EmbConv | $p_{95}$ Gate |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **FreeRTOS** | `EmbeddedConventional` | 4.23 MB | 3.89 MB | $0.051\,\mu\text{s}$ | $0.094\,\mu\text{s}$ | $0.149\,\mu\text{s}$ | Baseline | Baseline |
| | `Interned` | 5.17 MB | 5.09 MB | $0.072\,\mu\text{s}$ | $0.143\,\mu\text{s}$ | $0.203\,\mu\text{s}$ | +30.8% | FAIL |
| | **`SymTabV3` (Optimized)** | 4.78 MB | 4.72 MB | $0.071\,\mu\text{s}$ | $0.277\,\mu\text{s}$ | $0.445\,\mu\text{s}$ | +21.2% | FAIL |
| **Arduino** | `EmbeddedConventional` | 2.31 MB | 2.27 MB | $0.046\,\mu\text{s}$ | $0.089\,\mu\text{s}$ | $0.130\,\mu\text{s}$ | Baseline | Baseline |
| | `Interned` | 3.70 MB | 3.70 MB | $0.070\,\mu\text{s}$ | $0.135\,\mu\text{s}$ | $0.195\,\mu\text{s}$ | +63.0% | FAIL |
| | **`SymTabV3` (Optimized)** | 2.99 MB | 2.99 MB | $0.061\,\mu\text{s}$ | $0.171\,\mu\text{s}$ | $0.356\,\mu\text{s}$ | +31.7% | FAIL |
| **Zephyr** | `EmbeddedConventional` | 76.37 MB | 66.91 MB | $0.050\,\mu\text{s}$ | $0.125\,\mu\text{s}$ | $0.225\,\mu\text{s}$ | Baseline | Baseline |
| | `Interned` | 81.52 MB | 81.22 MB | $0.082\,\mu\text{s}$ | $0.233\,\mu\text{s}$ | $0.499\,\mu\text{s}$ | +21.4% | FAIL |
| | **`SymTabV3` (Optimized)** | **57.89 MB** | **53.39 MB** | **$0.066\,\mu\text{s}$** | **$0.228\,\mu\text{s}$** | **$0.481\,\mu\text{s}$** | **-20.2%** | **FAIL** |
| **ESP-IDF** | `EmbeddedConventional` | 75.77 MB | 67.82 MB | $0.061\,\mu\text{s}$ | $0.236\,\mu\text{s}$ | $0.374\,\mu\text{s}$ | Baseline | Baseline |
| | `Interned` | 100.21 MB | 92.95 MB | $0.090\,\mu\text{s}$ | $0.216\,\mu\text{s}$ | $0.573\,\mu\text{s}$ | +37.1% | FAIL |
| | **`SymTabV3` (Optimized)** | 89.36 MB | 81.91 MB | $0.079\,\mu\text{s}$ | $0.297\,\mu\text{s}$ | $0.712\,\mu\text{s}$ | +20.8% | FAIL |

---

## 3. Final Conclusion Text

> "On the large Zephyr workload, SymTabV3 reduced physically measured final heap by 20.2% (13.52 MB) relative to the embedded conventional baseline. This reduction was achieved with sub-microsecond median lookup latency, while the p95 latency increased from 0.125 us to 0.228 us. Thus the architecture demonstrates a measurable RAM advantage in the large-scale embedded regime, but does not satisfy the 1.25x p95 latency constraint."
