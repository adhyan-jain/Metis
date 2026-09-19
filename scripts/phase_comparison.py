#!/usr/bin/env python3
"""
Step 11: Phase-I vs Phase-II Direct Comparison Script.

Generates a direct side-by-side metric comparison between:
- ConventionalHost (Host Baseline)
- EmbeddedConventional (Embedded Baseline)
- SymTabV3 (Phase-I Adaptive Result)
- MetisX (Phase-II Cache-Conscious Architecture)

Output: results/phase_comparison.csv
"""

import csv
import os

def main():
    bench_file = "results/metis_x_validation.csv"
    if not os.path.exists(bench_file):
        bench_file = "results/metis_x_benchmark.csv"

    out_file = "results/phase_comparison.csv"

    with open(bench_file, "r") as f_in, open(out_file, "w", newline="") as f_out:
        reader = csv.DictReader(f_in)
        writer = csv.writer(f_out)

        writer.writerow([
            "corpus",
            "implementation",
            "declarations",
            "uses",
            "unique_names",
            "measured_final_heap_bytes",
            "measured_peak_heap_bytes",
            "lookup_p50_us",
            "lookup_p95_us",
            "lookup_p99_us",
            "lookup_mean_us",
            "insert_p95_us",
            "vs_embconv_heap_ratio",
            "vs_embconv_p95_ratio"
        ])

        # Group by corpus
        rows_by_corpus = {}
        for r in reader:
            c = r["corpus"]
            if c not in rows_by_corpus:
                rows_by_corpus[c] = {}
            rows_by_corpus[c][r["implementation"]] = r

        print("\n=== METIS-X Step 11 Phase-I vs Phase-II Comparison ===")

        for c, impls in rows_by_corpus.items():
            if "EmbeddedConventional" not in impls:
                continue
            emb_heap = float(impls["EmbeddedConventional"]["measured_final_heap_bytes"])
            emb_p95 = float(impls["EmbeddedConventional"]["lookup_p95_us"])

            for impl_name in ["ConventionalHost", "EmbeddedConventional", "SymTabV3", "MetisX"]:
                if impl_name not in impls:
                    continue
                r = impls[impl_name]
                h = float(r["measured_final_heap_bytes"])
                p95 = float(r["lookup_p95_us"])

                h_ratio = h / emb_heap if emb_heap > 0 else 1.0
                p95_ratio = p95 / emb_p95 if emb_p95 > 0 else 1.0

                writer.writerow([
                    c,
                    impl_name,
                    r["declarations"],
                    r["uses"],
                    r["unique_names"],
                    r["measured_final_heap_bytes"],
                    r["measured_peak_heap_bytes"],
                    r["lookup_p50_us"],
                    r["lookup_p95_us"],
                    r["lookup_p99_us"],
                    r["lookup_mean_us"],
                    r["insert_p95_us"],
                    f"{h_ratio:.4f}",
                    f"{p95_ratio:.4f}"
                ])

    print(f"Wrote {out_file}")

if __name__ == "__main__":
    main()
