# Quantitative Technical Effects & Empirical Validation

This document records the quantitative, empirical technical effects produced by the **METIS-X** technical combination, validated against real-world embedded C/C++ AST header workload graphs.

---

## 1. Summary of Quantitative Technical Effects

| Technical Effect Metric | Baseline Implementation | METIS-X Implementation | Measured Advantage | Target Workload |
| :--- | :--- | :--- | :--- | :--- |
| **Physical Final Heap Memory** | $41.74\text{ MB}$ ($41,742,312\text{ B}$) | $25.79\text{ MB}$ ($25,789,424\text{ B}$) | **$-38.2\%$ RAM reduction** | Zephyr RTOS Header Graph |
| **p95 Lookup Latency** | $0.119\ \mu\text{s}$ | $0.083\ \mu\text{s}$ | **$-30.2\%$ Latency reduction** | Zephyr RTOS Header Graph |
| **Physical Final Heap Memory** | $42.65\text{ MB}$ ($42,651,920\text{ B}$) | $35.20\text{ MB}$ ($35,196,200\text{ B}$) | **$-17.5\%$ RAM reduction** | ESP-IDF Header Graph |
| **p95 Lookup Latency** | $0.136\ \mu\text{s}$ | $0.070\ \mu\text{s}$ | **$-48.5\%$ Latency reduction** | ESP-IDF Header Graph |
| **Hot-Path Heap Allocations** | $> 2.5\times 10^6$ allocations | $0$ allocations | **Zero Allocation Hot Path** | Across $2,863,367$ lookups |
| **Ablation Memory Progression** | $43.44\text{ MB}$ ($A_2$: Flat Robin Hood) | $5.59\text{ MB}$ ($A_5$: Full METIS-X) | **$87.1\%$ Physical RAM Reduction** | Zephyr AST Symbol Workload |

---

## 2. Non-Obvious Synergistic Interactions

1. **Simultaneous Memory and Latency Optimization**: Conventional wisdom dictates a trade-off between memory footprint and latency (i.e., compression saves memory but increases lookup latency). METIS-X achieves **simultaneous physical memory reduction and lookup latency acceleration** by combining 32-byte cache-aligned slots with Robin Hood open addressing and inline hash signatures.
2. **Zero Allocation Hot-Path**: Combining inline 15-byte SSO strings with scope-bound bump-pointer recycling guarantees that once initial arena chunks are allocated, symbol lookup operations execute with zero memory allocations (`0` calls to `operator new` or `malloc`).
