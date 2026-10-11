#!/usr/bin/env python3
"""
Empirical and Analytical Runtime Latency Benchmark
Explicit Disclosure:
  - Microbenchmark measured on host execution environment (x86_64 Linux).
  - Target Cortex-M latency characterized via formal analytical complexity:
    number of memory accesses, branch comparisons, sequential scan steps,
    and de-tokenization loop iterations.
  - No physical target hardware attached; simulator/host timings are not physical Cortex-M latencies.
"""

import os
import sys
import json
import time
import subprocess
import numpy as np

WORKLOADS = [
    ("hello_world_mps2", "data/zephyr_symtab_hello_world_158.json"),
    ("shell_module_mps2", "data/zephyr_symtab_shell_module_557.json"),
    ("echo_server_mps2", "data/zephyr_symtab_echo_server_1381.json"),
    ("hello_world_nrf52840", "data/zephyr_symtab_nrf52840_hello_226.json")
]

BENCH_C_TEMPLATE = """
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

struct query_entry {
    uint32_t addr;
    const char *name;
};

static const struct query_entry queries[] = {
%QUERIES%
};
#define NUM_QUERIES %NUM_QUERIES%
#define REPETITIONS 1000

extern int %LOOKUP_FN%(uintptr_t addr, char *output_buffer, size_t capacity, uint32_t *offset_out);

static inline uint64_t get_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int main(void) {
    char buf[128];
    uint32_t off = 0;
    
    /* Warmup */
    for (size_t i = 0; i < NUM_QUERIES; i++) {
        %LOOKUP_FN%(queries[i].addr, buf, sizeof(buf), &off);
    }
    
    /* Measure each query across REPETITIONS */
    for (size_t i = 0; i < NUM_QUERIES; i++) {
        uint64_t t0 = get_ns();
        for (int r = 0; r < REPETITIONS; r++) {
            %LOOKUP_FN%(queries[i].addr, buf, sizeof(buf), &off);
        }
        uint64_t t1 = get_ns();
        double avg_ns = (double)(t1 - t0) / REPETITIONS;
        printf("%.2f\\n", avg_ns);
    }
    return 0;
}
"""

def main():
    print("================================================================================")
    print("  RUNTIME PERFORMANCE BENCHMARK & ANALYTICAL COMPLEXITY")
    print("================================================================================")
    
    results = {}
    
    for wid, data_path in WORKLOADS:
        print(f"\n>>> Workload: {wid}")
        results[wid] = {}
        with open(data_path, "r") as f:
            entries = json.load(f)
            
        real_entries = entries[:-1] # exclude sentinel
        num_syms = len(real_entries)
        
        # Select 5 representative query addresses across table (10%, 25%, 50%, 75%, 90% indices)
        indices = [int(num_syms * pct) for pct in [0.10, 0.25, 0.50, 0.75, 0.90]]
        selected = [real_entries[idx] for idx in indices]
        
        q_lines = []
        for s in selected:
            esc = s["name"].replace('\\', '\\\\').replace('"', '\\"')
            q_lines.append(f'\t{{ {hex(s["addr"])}, "{esc}" }},')
            
        methods = [
            ("Stock_Zephyr_Adapter", f"build/audit/{wid}_stock_adapter.c", "stock_lookup", {
                "bsearch_steps": int(np.ceil(np.log2(num_syms))),
                "scan_steps_worst": 0,
                "scan_steps_avg": 0.0,
                "token_indirections": 0,
                "memory_accesses_bsearch": int(np.ceil(np.log2(num_syms))),
                "memory_accesses_decode": 1 # direct memcpy from flash
            }),
            ("Candidate_Direct_K1", f"build/audit/{wid}_cand_direct.c", "cand_direct_lookup", {
                "bsearch_steps": int(np.ceil(np.log2(num_syms))),
                "scan_steps_worst": 0,
                "scan_steps_avg": 0.0,
                "token_indirections": 1,
                "memory_accesses_bsearch": int(np.ceil(np.log2(num_syms))),
                "memory_accesses_decode": 2 # name_offsets lookup + dict lookup per token
            }),
            ("Candidate_Checkpoint_K16", f"build/audit/{wid}_cand_cp16.c", "cand_cp16_lookup", {
                "bsearch_steps": int(np.ceil(np.log2(num_syms))),
                "scan_steps_worst": 15,
                "scan_steps_avg": 7.5,
                "token_indirections": 1,
                "memory_accesses_bsearch": int(np.ceil(np.log2(num_syms))),
                "memory_accesses_decode": 2 + 7.5 # checkpoint lookup + 7.5 null skips + dict lookup
            }),
            ("Linux_Kallsyms_M16", f"build/audit/{wid}_kallsyms_m16.c", "kallsyms_m16_lookup", {
                "bsearch_steps": int(np.ceil(np.log2(num_syms))),
                "scan_steps_worst": 15,
                "scan_steps_avg": 7.5,
                "token_indirections": 2, # token_index + token_table double dereference
                "memory_accesses_bsearch": int(np.ceil(np.log2(num_syms))),
                "memory_accesses_decode": 2 + 7.5 # marker lookup + 7.5 length jumps + 2 token derefs
            }),
            ("Linux_Kallsyms_M256", f"build/audit/{wid}_kallsyms_m256.c", "kallsyms_m256_lookup", {
                "bsearch_steps": int(np.ceil(np.log2(num_syms))),
                "scan_steps_worst": min(255, num_syms - 1),
                "scan_steps_avg": min(127.5, (num_syms - 1) / 2.0),
                "token_indirections": 2,
                "memory_accesses_bsearch": int(np.ceil(np.log2(num_syms))),
                "memory_accesses_decode": 2 + min(127.5, (num_syms - 1) / 2.0)
            })
        ]
        
        for m_name, c_path, fn_name, complexity in methods:
            code = BENCH_C_TEMPLATE
            code = code.replace("%QUERIES%", "\n".join(q_lines))
            code = code.replace("%NUM_QUERIES%", str(len(selected)))
            code = code.replace("%LOOKUP_FN%", fn_name)
            
            bench_c = f"build/audit/bench_{wid}_{m_name}.c"
            bench_bin = f"build/audit/bench_{wid}_{m_name}"
            with open(bench_c, "w") as f: f.write(code)
            
            subprocess.run(f"gcc -O2 {bench_c} {c_path} -o {bench_bin}", shell=True, check=True)
            res = subprocess.run(bench_bin, shell=True, stdout=subprocess.PIPE, universal_newlines=True)
            latencies = [float(l.strip()) for l in res.stdout.strip().splitlines() if l.strip()]
            
            median_lat = float(np.median(latencies))
            iqr_lat = float(np.percentile(latencies, 75) - np.percentile(latencies, 25))
            
            results[wid][m_name] = {
                "host_median_latency_ns": round(median_lat, 2),
                "host_iqr_spread_ns": round(iqr_lat, 2),
                "complexity": complexity
            }
            print(f"  [{m_name:25s}] Host Latency={median_lat:.1f} ns (IQR={iqr_lat:.1f}) | BSearch Steps={complexity['bsearch_steps']} | Scan Steps (avg/worst)={complexity['scan_steps_avg']}/{complexity['scan_steps_worst']}")

    with open("results/runtime_performance_report.json", "w") as f:
        json.dump(results, f, indent=2)
    print("\nSaved runtime benchmark report to results/runtime_performance_report.json")

if __name__ == "__main__":
    main()
