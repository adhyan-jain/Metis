#!/usr/bin/env python3
"""
Step 6: Statistical Analysis Script for METIS-X Validation.

Reads results/metis_x_validation.csv and calculates:
- heap_ratio (MetisX / EmbeddedConventional)
- p95_ratio (MetisX_p95 / EmbeddedConventional_p95)
- per-workload ratio summaries and classifications (JOINT WIN / PARTIAL / LOSS)

Output: results/metis_x_statistical_summary.csv
"""

import csv
import sys
import os

def main():
    val_file = "results/metis_x_validation.csv"
    if not os.path.exists(val_file):
        print(f"ERROR: {val_file} not found.", file=sys.stderr)
        sys.exit(1)

    rows_by_corpus = {}
    with open(val_file, "r") as f:
        reader = csv.DictReader(f)
        for r in reader:
            corpus = r["corpus"]
            impl = r["implementation"]
            if corpus not in rows_by_corpus:
                rows_by_corpus[corpus] = {}
            rows_by_corpus[corpus][impl] = r

    out_file = "results/metis_x_statistical_summary.csv"
    with open(out_file, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow([
            "corpus",
            "embconv_final_heap_bytes",
            "metisx_final_heap_bytes",
            "heap_ratio",
            "embconv_p95_us",
            "metisx_p95_us",
            "p95_ratio",
            "p95_cv_pct",
            "classification"
        ])

        print("\n=== METIS-X Step 6 Statistical Summary ===")
        print(f"{'Corpus':<12} {'Heap Ratio':<12} {'p95 Ratio':<12} {'p95 CV%':<10} {'Classification'}")
        print("-" * 65)

        for corpus, impls in rows_by_corpus.items():
            if "EmbeddedConventional" not in impls or "MetisX" not in impls:
                continue

            emb = impls["EmbeddedConventional"]
            mx = impls["MetisX"]

            emb_heap = float(emb["measured_final_heap_bytes"])
            mx_heap = float(mx["measured_final_heap_bytes"])
            heap_ratio = mx_heap / emb_heap if emb_heap > 0 else 1.0

            emb_p95 = float(emb["lookup_p95_us"])
            mx_p95 = float(mx["lookup_p95_us"])
            p95_ratio = mx_p95 / emb_p95 if emb_p95 > 0 else 1.0

            mx_cv = float(mx.get("lookup_p95_cv_pct", 0.0))

            # Classification
            heap_win = heap_ratio <= 1.0
            p95_win = p95_ratio <= 1.0
            ten_pct = (heap_ratio <= 0.90 or p95_ratio <= 0.90)

            if heap_win and p95_win and ten_pct:
                classification = "JOINT WIN (>=10%)"
            elif heap_win and p95_win:
                classification = "JOINT WIN (<10%)"
            elif heap_win or p95_win:
                classification = "PARTIAL / TRADEOFF"
            else:
                classification = "LOSS"

            writer.writerow([
                corpus,
                int(emb_heap),
                int(mx_heap),
                f"{heap_ratio:.4f}",
                f"{emb_p95:.4f}",
                f"{mx_p95:.4f}",
                f"{p95_ratio:.4f}",
                f"{mx_cv:.2f}",
                classification
            ])

            print(f"{corpus:<12} {heap_ratio:<12.4f} {p95_ratio:<12.4f} {mx_cv:<10.2f} {classification}")

    print(f"\nWrote {out_file}")

if __name__ == "__main__":
    main()
