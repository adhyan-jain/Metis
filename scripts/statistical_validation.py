#!/usr/bin/env python3
"""Statistical Validation & Inference Engine
(CLAUDE_RESEARCH.md Section 15)

Processes results/multiseed_v2_raw.csv (N=30 independent seeds x 8 families)
and computes:
  - Mean, Median, StdDev, 95% Confidence Intervals (Student's t, df=29)
  - Paired t-test p-values
  - Cohen's d Effect Sizes (d = (mean1 - mean2) / s_pooled)
  - Effect Magnitude Classifications ("large", "medium", "small", "negligible")

Outputs: results/statistical_summary.csv
"""

import csv
import math
import os
import sys
from collections import defaultdict

RAW_PATH = "results/multiseed_v2_raw.csv"
SUMMARY_PATH = "results/statistical_summary.csv"

N = 30 # seeds per group (df = 29)
T_CRIT_95 = 2.045 # two-tailed alpha=0.05 for df=29


def mean(xs):
    return sum(xs) / len(xs)


def median(xs):
    s = sorted(xs)
    n = len(s)
    if n % 2 == 1:
        return s[n // 2]
    return (s[n // 2 - 1] + s[n // 2]) / 2.0


def sample_std(xs, m=None):
    if m is None:
        m = mean(xs)
    n = len(xs)
    if n <= 1:
        return 0.0
    var = sum((x - m) ** 2 for x in xs) / (n - 1)
    return math.sqrt(var)


def ci95(xs, m=None, sd=None):
    if m is None:
        m = mean(xs)
    if sd is None:
        sd = sample_std(xs, m)
    half_width = T_CRIT_95 * sd / math.sqrt(len(xs))
    return m - half_width, m + half_width


def cohens_d(xs, ys):
    """Calculates Cohen's d effect size for two independent or paired groups."""
    m1, m2 = mean(xs), mean(ys)
    s1, s2 = sample_std(xs, m1), sample_std(ys, m2)
    n1, n2 = len(xs), len(ys)
    s_pooled = math.sqrt(((n1 - 1) * (s1 ** 2) + (n2 - 1) * (s2 ** 2)) / (n1 + n2 - 2))
    if s_pooled == 0:
        return 0.0
    return (m1 - m2) / s_pooled


def classify_effect(d):
    ad = abs(d)
    if ad >= 0.8:
        return "large"
    elif ad >= 0.5:
        return "medium"
    elif ad >= 0.2:
        return "small"
    return "negligible"


# Hand-rolled paired t-test
def _betacf(a, b, x, max_iter=200, eps=3e-12):
    qab = a + b
    qap = a + 1.0
    qam = a - 1.0
    c = 1.0
    d = 1.0 - qab * x / qap
    if abs(d) < 1e-30: d = 1e-30
    d = 1.0 / d
    h = d
    for m in range(1, max_iter + 1):
        m2 = 2 * m
        aa = m * (b - m) * x / ((qam + m2) * (a + m2))
        d = 1.0 + aa * d
        if abs(d) < 1e-30: d = 1e-30
        c = 1.0 + aa / c
        if abs(c) < 1e-30: c = 1e-30
        d = 1.0 / d
        h *= d * c
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2))
        d = 1.0 + aa * d
        if abs(d) < 1e-30: d = 1e-30
        c = 1.0 + aa / c
        if abs(c) < 1e-30: c = 1e-30
        d = 1.0 / d
        delta = d * c
        h *= delta
        if abs(delta - 1.0) < eps: break
    return h


def _betai(a, b, x):
    if x <= 0.0: return 0.0
    if x >= 1.0: return 1.0
    log_bt = (math.lgamma(a + b) - math.lgamma(a) - math.lgamma(b)
              + a * math.log(x) + b * math.log(1.0 - x))
    bt = math.exp(log_bt)
    if x < (a + 1.0) / (a + b + 2.0):
        return bt * _betacf(a, b, x) / a
    else:
        return 1.0 - bt * _betacf(b, a, 1.0 - x) / b


def paired_t_test(xs, ys):
    """Paired Student's t-test: returns (t_stat, p_val)."""
    diffs = [x - y for x, y in zip(xs, ys)]
    m_diff = mean(diffs)
    sd_diff = sample_std(diffs, m_diff)
    se = sd_diff / math.sqrt(len(diffs))
    if se == 0:
        return 0.0, 1.0
    t_stat = m_diff / se
    df = len(diffs) - 1
    x = df / (df + t_stat * t_stat)
    p = _betai(df / 2.0, 0.5, x)
    return t_stat, p


def main():
    if not os.path.exists(RAW_PATH):
        print(f"File {RAW_PATH} missing; run statistical_validation.exe first.")
        return 1

    # Load raw multiseed data
    # (dataset, seed, implementation) -> row
    data = defaultdict(dict)
    with open(RAW_PATH, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for r in reader:
            data[(r["dataset"], r["implementation"])][int(r["seed"])] = r

    datasets = sorted({k[0] for k in data.keys()})
    impls = sorted({k[1] for k in data.keys()})

    out_rows = []

    print("=== Statistical Validation & Hypothesis Testing Summary ===\n")

    for ds in datasets:
        # Get seeds for this dataset
        ds_seeds = sorted(data[(ds, "SymTabV2")].keys())

        # Extract seed values for key implementations
        v2_heaps = [int(data[(ds, "SymTabV2")][s]["measured_final_heap_bytes"]) for s in ds_seeds]
        v2_colds = [float(data[(ds, "SymTabV2")][s]["cold_lookup_p50_us"]) for s in ds_seeds]

        v1_heaps = [int(data[(ds, "BudgetSymV1")][s]["measured_final_heap_bytes"]) for s in ds_seeds]
        conv_heaps = [int(data[(ds, "Conventional")][s]["measured_final_heap_bytes"]) for s in ds_seeds]
        conv_colds = [float(data[(ds, "Conventional")][s]["cold_lookup_p50_us"]) for s in ds_seeds]

        no_reclaim_heaps = [int(data[(ds, "V2-NoScopeReclamation")][s]["measured_final_heap_bytes"]) for s in ds_seeds]
        no_fp_colds = [float(data[(ds, "V2-NoFingerprints")][s]["cold_lookup_p50_us"]) for s in ds_seeds]

        # H1: V2 vs V1 Heap Reduction
        d_v2_v1 = cohens_d(v2_heaps, v1_heaps)
        _, p_v2_v1 = paired_t_test(v2_heaps, v1_heaps)

        # H2: V2 vs Scope Reclamation (NoReclaim vs V2)
        d_reclaim = cohens_d(no_reclaim_heaps, v2_heaps)
        _, p_reclaim = paired_t_test(no_reclaim_heaps, v2_heaps)

        # H3: V2 vs Fingerprints Latency (NoFP vs V2)
        d_fp = cohens_d(no_fp_colds, v2_colds)
        _, p_fp = paired_t_test(no_fp_colds, v2_colds)

        for impl in impls:
            heaps = [int(data[(ds, impl)][s]["measured_final_heap_bytes"]) for s in ds_seeds]
            colds = [float(data[(ds, impl)][s]["cold_lookup_p50_us"]) for s in ds_seeds]

            m_h, med_h, sd_h = mean(heaps), median(heaps), sample_std(heaps)
            ci_l_h, ci_h_h = ci95(heaps, m_h, sd_h)

            m_c, med_c, sd_c = mean(colds), median(colds), sample_std(colds)
            ci_l_c, ci_h_c = ci95(colds, m_c, sd_c)

            out_rows.append({
                "dataset": ds,
                "implementation": impl,
                "sample_size": N,
                "heap_mean_bytes": round(m_h, 1),
                "heap_median_bytes": round(med_h, 1),
                "heap_std_bytes": round(sd_h, 1),
                "heap_ci95_low": round(ci_l_h, 1),
                "heap_ci95_high": round(ci_h_h, 1),
                "cold_p50_mean_us": round(m_c, 4),
                "cold_p50_median_us": round(med_c, 4),
                "cold_p50_std_us": round(sd_c, 4),
                "cold_p50_ci95_low": round(ci_l_c, 4),
                "cold_p50_ci95_high": round(ci_h_c, 4),
                "cohens_d_heap_vs_v1": round(d_v2_v1, 3) if impl == "SymTabV2" else "",
                "p_val_heap_vs_v1": f"{p_v2_v1:.6e}" if impl == "SymTabV2" else "",
                "effect_size_vs_v1": classify_effect(d_v2_v1) if impl == "SymTabV2" else ""
            })

        print(f"[{ds:<24}] V2 vs V1 Heap Reduc: Cohen's d = {d_v2_v1:>+6.2f} ({classify_effect(d_v2_v1):<10}), p = {p_v2_v1:.2e} | Scope Reclaim Win: d = {d_reclaim:>+6.2f} ({classify_effect(d_reclaim):<10})")

    # Write summary CSV
    fieldnames = [
        "dataset", "implementation", "sample_size",
        "heap_mean_bytes", "heap_median_bytes", "heap_std_bytes", "heap_ci95_low", "heap_ci95_high",
        "cold_p50_mean_us", "cold_p50_median_us", "cold_p50_std_us", "cold_p50_ci95_low", "cold_p50_ci95_high",
        "cohens_d_heap_vs_v1", "p_val_heap_vs_v1", "effect_size_vs_v1"
    ]

    with open(SUMMARY_PATH, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for r in out_rows:
            writer.writerow(r)

    print(f"\nWrote statistical summary to {SUMMARY_PATH}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
