#!/usr/bin/env python3
"""
Generate golden entries array, compile host test runners against all budget variants,
and execute exhaustive correctness tests.
"""

import json
import os
import subprocess

def generate_golden_c(json_path, out_c_path):
    with open(json_path, "r") as f:
        entries = json.load(f)
        
    lines = []
    lines.append('#include <stdint.h>')
    lines.append('#include <stddef.h>')
    lines.append('typedef struct { uint32_t index; uintptr_t addr; uint32_t size; uint32_t offset; const char *name; } sym_test_entry_t;')
    lines.append(f'const sym_test_entry_t golden_entries[{len(entries)}] = {{')
    for e in entries:
        lines.append(f'\t{{ {e["index"]}, {hex(e["addr"])}, {hex(e["size"])}, {hex(e["offset"])}, "{e["name"]}" }},')
    lines.append('};')
    lines.append(f'const size_t golden_num_entries = {len(entries)};')
    
    with open(out_c_path, "w") as f:
        f.write("\n".join(lines))

def main():
    os.makedirs("build/cost_sweep", exist_ok=True)
    golden_c = "build/cost_sweep/golden_data.c"
    generate_golden_c("data/zephyr_symtab_158.json", golden_c)
    
    budgets = [0, 8, 16, 32, 64, 128]
    modes = ["direct", "checkpoint"]
    
    all_passed = True
    test_logs = []
    
    print("=========================================================================================")
    print("                    EXHAUSTIVE CORRECTNESS & RECONSTRUCTION TESTS                        ")
    print("=========================================================================================")

    for b in budgets:
        for mode in modes:
            src_c = f"build/cost_sweep/symtab_b{b}_{mode}.c"
            test_bin = f"build/cost_sweep/test_b{b}_{mode}"
            
            # Compile with host GCC
            cmd_build = f"gcc -O2 tests/test_symtab_compressed_correctness.c {src_c} {golden_c} -o {test_bin}"
            res = subprocess.run(cmd_build, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
            if res.returncode != 0:
                print(f"Build error for {b}_{mode}: {res.stderr}")
                all_passed = False
                continue
                
            # Run test binary
            res_run = subprocess.run(f"./{test_bin} budget_{b}_{mode}", shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
            output = res_run.stdout.strip()
            status = "PASS" if res_run.returncode == 0 else "FAIL"
            print(f"Variant Budget={b:3d} Mode={mode:10s} : {status}")
            test_logs.append({
                "variant": f"budget_{b}_{mode}",
                "status": status,
                "output": output
            })
            if status == "FAIL":
                all_passed = False
                print(output)

    print("=========================================================================================")
    if all_passed:
        print("ALL VARIANTS PASSED 100% OF CORRECTNESS & RECONSTRUCTION TESTS!")
    else:
        print("SOME TESTS FAILED!")
        
    with open("results/correctness_test_report.json", "w") as f:
        json.dump(test_logs, f, indent=2)

if __name__ == "__main__":
    main()
