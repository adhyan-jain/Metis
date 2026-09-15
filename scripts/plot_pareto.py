#!/usr/bin/env python3
"""Plotting script for Latency-Constrained Pareto Optimization
(CLAUDE_RESEARCH.md Section 12.2)

Generates:
  - figures/pareto_memory_vs_latency.png (Scatter plot of memory vs cold lookup latency for all configs)
  - figures/pareto_frontier_by_workload.png (Pareto curves per workload category)
"""

import csv
import os
import sys

try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    MATPLOTLIB_AVAILABLE = True
except ImportError:
    MATPLOTLIB_AVAILABLE = False


def main():
    if not MATPLOTLIB_AVAILABLE:
        print("matplotlib not available; skipping figure generation.")
        return 0

    results_file = "results/pareto_results.csv"
    if not os.path.exists(results_file):
        print(f"File {results_file} not found; run pareto.exe first.")
        return 1

    os.makedirs("figures", exist_ok=True)

    with open(results_file, "r", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))

    # Group rows by workload
    workloads = {}
    for r in rows:
        wl = r["workload"]
        if wl not in workloads:
            workloads[wl] = []
        workloads[wl].append(r)

    # 1. Figure 1: Memory vs Cold Lookup Latency Scatter for Representative Workloads
    plt.figure(figsize=(10, 6))
    colors = {"Conventional": "red", "Interned": "orange", "BudgetSymV1": "purple", "SymTabV2": "blue"}
    markers = {"Conventional": "s", "Interned": "^", "BudgetSymV1": "x", "SymTabV2": "o"}

    sample_workload = "FreeRTOS" if "FreeRTOS" in workloads else list(workloads.keys())[0]
    wl_rows = workloads[sample_workload]

    for impl in ["Conventional", "Interned", "BudgetSymV1", "SymTabV2"]:
        impl_rows = [r for r in wl_rows if r["implementation"] == impl]
        if not impl_rows:
            continue
        xs = [float(r["cold_lookup_p50_us"]) for r in impl_rows]
        ys = [int(r["measured_final_heap_bytes"]) / 1024.0 for r in impl_rows] # KB
        plt.scatter(xs, ys, label=impl, color=colors.get(impl, "gray"),
                    marker=markers.get(impl, "o"), alpha=0.6, s=30 if impl == "SymTabV2" else 80)

    plt.xlabel("Cold Lookup Latency p50 (microseconds)")
    plt.ylabel("Measured Final Heap Memory (KB)")
    plt.title(f"Memory vs. Lookup Latency Frontier ({sample_workload})")
    plt.grid(True, linestyle="--", alpha=0.5)
    plt.legend()
    plt.tight_layout()
    plt.savefig("figures/pareto_memory_vs_latency.png", dpi=150)
    plt.close()

    # 2. Figure 2: Frontier curves across Workload Categories
    fig, axes = plt.subplots(1, 3, figsize=(15, 5))
    cats = [("A", "Category A: Corpora"), ("B", "Category B: Redundancy-Rich"), ("C", "Category C: Adversarial")]

    for idx, (cat_code, cat_title) in enumerate(cats):
        ax = axes[idx]
        cat_wls = [wl for wl, rlist in workloads.items() if rlist[0]["category"] == cat_code]
        for wl in cat_wls:
            rlist = workloads[wl]
            v2_rows = [r for r in rlist if r["implementation"] == "SymTabV2"]
            if not v2_rows:
                continue
            xs = [float(r["cold_lookup_p50_us"]) for r in v2_rows]
            ys = [int(r["measured_final_heap_bytes"]) / 1024.0 for r in v2_rows]
            ax.scatter(xs, ys, label=wl, alpha=0.5, s=20)

        ax.set_xlabel("Cold Lookup p50 (us)")
        ax.set_ylabel("Measured Heap (KB)")
        ax.set_title(cat_title)
        ax.grid(True, linestyle="--", alpha=0.5)
        ax.legend(fontsize=8)

    plt.tight_layout()
    plt.savefig("figures/pareto_frontier_by_workload.png", dpi=150)
    plt.close()

    print("Generated figures/pareto_memory_vs_latency.png and figures/pareto_frontier_by_workload.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
