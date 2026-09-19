#!/usr/bin/env python3
"""
Step 13: Generate Canonical Phase-II Dataset.

Combines validation and instrumentation outputs into results/METIS_X_CANONICAL_DATASET.csv.
"""

import csv
import os

def main():
    val_file = "results/metis_x_validation.csv"
    instr_file = "results/metis_x_instrumentation.csv"
    out_file = "results/METIS_X_CANONICAL_DATASET.csv"

    instr_map = {}
    if os.path.exists(instr_file):
        with open(instr_file, "r") as f:
            reader = csv.DictReader(f)
            for r in reader:
                instr_map[r["corpus"]] = r

    with open(val_file, "r") as f_in, open(out_file, "w", newline="") as f_out:
        reader = csv.DictReader(f_in)
        writer = csv.writer(f_out)

        writer.writerow([
            "workload",
            "implementation",
            "repetition",
            "physical_final_heap_bytes",
            "physical_peak_heap_bytes",
            "lookup_p50_us",
            "lookup_p95_us",
            "lookup_p99_us",
            "lookup_mean_us",
            "insert_p95_us",
            "probe_mean",
            "inline_fraction",
            "fallback_fraction",
            "configuration_id",
            "seed",
            "measurement_method"
        ])

        for r in reader:
            corpus = r["corpus"]
            impl = r["implementation"]
            instr = instr_map.get(corpus, {})

            probe_mean = r.get("avg_probe_dist", instr.get("avg_probe_dist", "0.0"))
            inline_slots = float(r.get("inline_slots", 0))
            heap_slots = float(r.get("heap_slots", 0))
            total_slots = inline_slots + heap_slots
            inline_frac = f"{inline_slots / total_slots:.4f}" if total_slots > 0 else "0.0000"
            fallback_frac = f"{heap_slots / total_slots:.4f}" if total_slots > 0 else "0.0000"

            writer.writerow([
                corpus,
                impl,
                r.get("timing_reps", "7"),
                r["measured_final_heap_bytes"],
                r["measured_peak_heap_bytes"],
                r["lookup_p50_us"],
                r["lookup_p95_us"],
                r["lookup_p99_us"],
                r["lookup_mean_us"],
                r["insert_p95_us"],
                probe_mean,
                inline_frac,
                fallback_frac,
                "METIS_X_v0.1_kInlineCap12",
                "42",
                "malloc_usable_size"
            ])

    print(f"Wrote {out_file}")

if __name__ == "__main__":
    main()
