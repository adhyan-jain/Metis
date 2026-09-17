#!/usr/bin/env python3
import csv
import math
from collections import defaultdict

def mean_std(vals):
    if not vals:
        return 0.0, 0.0
    m = sum(vals) / len(vals)
    var = sum((x - m) ** 2 for x in vals) / (len(vals) - 1) if len(vals) > 1 else 0.0
    return m, math.sqrt(var)

def paired_t_test(x, y):
    # Returns t-stat and approximate p-value for paired samples
    n = len(x)
    if n < 2:
        return 0.0, 1.0
    diffs = [x[i] - y[i] for i in range(n)]
    m_diff, std_diff = mean_std(diffs)
    if std_diff == 0:
        return 0.0, 1.0
    se = std_diff / math.sqrt(n)
    t_stat = m_diff / se
    # Approximate normal approximation for df=29
    p_val = math.erfc(abs(t_stat) / math.sqrt(2))
    return t_stat, p_val

def main():
    raw_file = "results/multiseed_v4_raw.csv"
    out_file = "results/multiseed_v4_summary.csv"

    data = defaultdict(lambda: defaultdict(list))
    # data[workload][impl] -> list of (heap_bytes, p95_us)

    with open(raw_file, "r") as f:
        reader = csv.DictReader(f)
        for row in reader:
            wl = row["workload"]
            impl = row["implementation"]
            heap_b = float(row["measured_final_heap_bytes"])
            p95 = float(row["cold_lookup_p95_us"])
            data[wl][impl].append((heap_b, p95))

    with open(out_file, "w") as f:
        f.write("workload,implementation,mean_heap_bytes,std_heap_bytes,heap_ratio_vs_conv,mean_p95_us,p95_ratio_vs_conv,p_value_heap_vs_conv\n")

        for wl in sorted(data.keys()):
            conv_heap = [x[0] for x in data[wl]["Conventional"]]
            conv_p95  = [x[1] for x in data[wl]["Conventional"]]
            conv_m_heap, _ = mean_std(conv_heap)
            conv_m_p95, _  = mean_std(conv_p95)

            for impl in ["Conventional", "Interned", "Conventional-HeapString", "SymTabV3", "SymTabV4"]:
                if impl not in data[wl]:
                    continue
                heaps = [x[0] for x in data[wl][impl]]
                p95s  = [x[1] for x in data[wl][impl]]
                m_h, s_h = mean_std(heaps)
                m_p, s_p = mean_std(p95s)

                ratio_h = m_h / conv_m_heap if conv_m_heap > 0 else 1.0
                ratio_p = m_p / conv_m_p95 if conv_m_p95 > 0 else 1.0

                t_stat, p_val = paired_t_test(heaps, conv_heap)

                f.write(f"{wl},{impl},{m_h:.2f},{s_h:.2f},{ratio_h:.4f},{m_p:.4f},{ratio_p:.4f},{p_val:.4e}\n")

    print(f"Wrote {out_file}")

if __name__ == "__main__":
    main()
