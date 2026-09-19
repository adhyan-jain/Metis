#!/usr/bin/env python3
"""
Generate publication-quality figures for METIS-X Phase II.

Reads from:
  - results/METIS_X_CANONICAL_DATASET.csv
  - results/metis_x_ablation.csv
  - results/phase_comparison.csv

Generates:
  - figures/metis_x_memory_comparison.png
  - figures/metis_x_p95_comparison.png
  - figures/metis_x_ablation.png
  - figures/metis_x_v3_vs_metisx.png
"""

import csv
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESULTS = os.path.join(ROOT, "results")
FIGURES = os.path.join(ROOT, "figures")
os.makedirs(FIGURES, exist_ok=True)

# Styles
plt.style.use("seaborn-v0_8-whitegrid" if "seaborn-v0_8-whitegrid" in plt.style.available else "default")
plt.rcParams["font.family"] = "sans-serif"
plt.rcParams["font.size"] = 10

IMPL_COLORS = {
    "ConventionalHost": "#94a3b8",      # Slate
    "EmbeddedConventional": "#3b82f6",  # Blue
    "SymTabV3": "#f59e0b",             # Amber
    "MetisX": "#10b981",               # Emerald
}

IMPL_ORDER = ["ConventionalHost", "EmbeddedConventional", "SymTabV3", "MetisX"]


def load_csv(filename):
    path = os.path.join(RESULTS, filename)
    if not os.path.exists(path):
        print(f"Warning: {path} not found.", file=sys.stderr)
        return []
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def plot_memory_comparison():
    rows = load_csv("METIS_X_CANONICAL_DATASET.csv")
    if not rows:
        return

    corpora = ["FreeRTOS", "Arduino", "Zephyr", "ESP-IDF"]
    fig, ax = plt.subplots(figsize=(10, 5))
    x = range(len(corpora))
    width = 0.20

    for i, impl in enumerate(IMPL_ORDER):
        vals = []
        for c in corpora:
            match = [r for r in rows if r["workload"] == c and r["implementation"] == impl]
            if match:
                vals.append(float(match[0]["physical_final_heap_bytes"]) / (1024 * 1024))
            else:
                vals.append(0.0)

        pos = [xi + (i - 1.5) * width for xi in x]
        bars = ax.bar(pos, vals, width, label=impl, color=IMPL_COLORS[impl], edgecolor="white")

        for b, v in zip(bars, vals):
            if v > 0:
                ax.annotate(f"{v:.2f}M", (b.get_x() + b.get_width() / 2, b.get_height()),
                            ha="center", va="bottom", fontsize=8, rotation=45, xytext=(0, 2),
                            textcoords="offset points")

    ax.set_xticks(list(x))
    ax.set_xticklabels(corpora, fontweight="bold")
    ax.set_ylabel("Physical Final Heap (MB)", fontweight="bold")
    ax.set_title("METIS-X Physical Final Heap Memory Comparison", fontweight="bold", fontsize=12)
    ax.legend(frameon=True, facecolor="white", framealpha=0.9)
    ax.grid(axis="y", linestyle="--", alpha=0.7)

    plt.tight_layout()
    path = os.path.join(FIGURES, "metis_x_memory_comparison.png")
    plt.savefig(path, dpi=300)
    plt.close()
    print(f"Saved {path}")


def plot_p95_comparison():
    rows = load_csv("METIS_X_CANONICAL_DATASET.csv")
    if not rows:
        return

    corpora = ["FreeRTOS", "Arduino", "Zephyr", "ESP-IDF"]
    fig, ax = plt.subplots(figsize=(10, 5))
    x = range(len(corpora))
    width = 0.20

    for i, impl in enumerate(IMPL_ORDER):
        vals = []
        for c in corpora:
            match = [r for r in rows if r["workload"] == c and r["implementation"] == impl]
            if match:
                vals.append(float(match[0]["lookup_p95_us"]))
            else:
                vals.append(0.0)

        pos = [xi + (i - 1.5) * width for xi in x]
        bars = ax.bar(pos, vals, width, label=impl, color=IMPL_COLORS[impl], edgecolor="white")

        for b, v in zip(bars, vals):
            if v > 0:
                ax.annotate(f"{v:.3f}µs", (b.get_x() + b.get_width() / 2, b.get_height()),
                            ha="center", va="bottom", fontsize=8, rotation=45, xytext=(0, 2),
                            textcoords="offset points")

    ax.set_xticks(list(x))
    ax.set_xticklabels(corpora, fontweight="bold")
    ax.set_ylabel("p95 Lookup Latency (µs)", fontweight="bold")
    ax.set_title("METIS-X p95 Lookup Latency Comparison", fontweight="bold", fontsize=12)
    ax.legend(frameon=True, facecolor="white", framealpha=0.9)
    ax.grid(axis="y", linestyle="--", alpha=0.7)

    plt.tight_layout()
    path = os.path.join(FIGURES, "metis_x_p95_comparison.png")
    plt.savefig(path, dpi=300)
    plt.close()
    print(f"Saved {path}")


def plot_ablation():
    rows = load_csv("metis_x_ablation.csv")
    if not rows:
        return

    zephyr_rows = [r for r in rows if r["corpus"] == "Zephyr"]
    if not zephyr_rows:
        return

    fig, ax1 = plt.subplots(figsize=(9, 4.5))

    levels = [r["ablation_level"] for r in zephyr_rows]
    memory_mb = [float(r["final_heap_bytes"]) / (1024 * 1024) for r in zephyr_rows]
    p95_us = [float(r["lookup_p95_us"]) for r in zephyr_rows]

    # Clean level names
    clean_levels = [
        "A0: ConvHost",
        "A1: FlatOA (Linear)",
        "A2: FlatOA (Robin Hood)",
        "A3: EmbeddedConv",
        "A5: Full MetisX"
    ]

    colors = ["#94a3b8", "#ef4444", "#f97316", "#3b82f6", "#10b981"]

    bars = ax1.bar(clean_levels, memory_mb, color=colors, width=0.5, edgecolor="white")
    ax1.set_ylabel("Physical Final Heap (MB)", fontweight="bold", color="#1e293b")
    ax1.tick_params(axis="y", labelcolor="#1e293b")

    for b, v in zip(bars, memory_mb):
        ax1.annotate(f"{v:.2f} MB", (b.get_x() + b.get_width() / 2, b.get_height()),
                    ha="center", va="bottom", fontsize=9, fontweight="bold", xytext=(0, 3),
                    textcoords="offset points")

    ax2 = ax1.twinx()
    ax2.plot(clean_levels, p95_us, color="#6366f1", marker="o", linewidth=2.5, markersize=8, label="p95 Latency (µs)")
    ax2.set_ylabel("p95 Lookup Latency (µs)", fontweight="bold", color="#6366f1")
    ax2.tick_params(axis="y", labelcolor="#6366f1")
    ax2.grid(False)

    for i, txt in enumerate(p95_us):
        ax2.annotate(f"{txt:.3f}µs", (i, p95_us[i]), ha="center", va="bottom", fontsize=8,
                    xytext=(0, 8), textcoords="offset points", fontweight="bold", color="#4338ca")

    plt.title("METIS-X Component Ablation Waterfall (Zephyr RTOS)", fontweight="bold", fontsize=12)
    plt.tight_layout()
    path = os.path.join(FIGURES, "metis_x_ablation.png")
    plt.savefig(path, dpi=300)
    plt.close()
    print(f"Saved {path}")


def plot_v3_vs_metisx():
    rows = load_csv("phase_comparison.csv")
    if not rows:
        return

    corpora = ["Zephyr", "ESP-IDF"]
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10, 4.5))

    for ax, corpus in zip([ax1, ax2], corpora):
        c_rows = [r for r in rows if r["corpus"] == corpus]
        impls = ["EmbeddedConventional", "SymTabV3", "MetisX"]
        labels = ["EmbeddedConv\n(Baseline)", "SymTabV3\n(Phase I)", "MetisX\n(Phase II)"]
        colors = ["#3b82f6", "#f59e0b", "#10b981"]

        p95s = []
        heaps = []
        for imp in impls:
            match = [r for r in c_rows if r["implementation"] == imp]
            if match:
                p95s.append(float(match[0]["lookup_p95_us"]))
                heaps.append(float(match[0]["measured_final_heap_bytes"]) / (1024 * 1024))
            else:
                p95s.append(0.0)
                heaps.append(0.0)

        bars = ax.bar(labels, p95s, color=colors, width=0.5, edgecolor="white")
        ax.set_ylabel("p95 Lookup Latency (µs)", fontweight="bold")
        ax.set_title(f"{corpus}: Resolution of Tail Latency", fontweight="bold")
        ax.grid(axis="y", linestyle="--", alpha=0.7)

        for b, v, h in zip(bars, p95s, heaps):
            ax.annotate(f"{v:.3f} µs\n({h:.1f} MB)", (b.get_x() + b.get_width() / 2, b.get_height()),
                        ha="center", va="bottom", fontsize=9, fontweight="bold", xytext=(0, 3),
                        textcoords="offset points")

    plt.tight_layout()
    path = os.path.join(FIGURES, "metis_x_v3_vs_metisx.png")
    plt.savefig(path, dpi=300)
    plt.close()
    print(f"Saved {path}")


def main():
    plot_memory_comparison()
    plot_p95_comparison()
    plot_ablation()
    plot_v3_vs_metisx()


if __name__ == "__main__":
    main()
