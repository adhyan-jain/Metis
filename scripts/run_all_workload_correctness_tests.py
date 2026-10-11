#!/usr/bin/env python3
"""
Exhaustive correctness test runner for all real Zephyr workloads.
Runs 100% byte-for-byte symbol reconstruction and address lookup tests.
"""

import os
import sys
import json
import subprocess

TEST_C_TEMPLATE = """
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

struct golden_entry {
    uint32_t addr;
    uint32_t offset;
    uint32_t size;
    const char *name;
};

static const struct golden_entry golden_entries[] = {
%GOLDEN_ENTRIES%
};
#define NUM_GOLDEN_ENTRIES %NUM_ENTRIES%
#define GOLDEN_FIRST_ADDR  %FIRST_ADDR%

extern int symtab_find_symbol_name_r(uintptr_t addr, uint32_t *offset, char *buf, size_t buflen);

int main(void) {
    int passed = 0;
    int failed = 0;
    char buf[256];
    uint32_t offset = 0;
    
    /* 1. Exact lookups */
    for (size_t i = 0; i < NUM_GOLDEN_ENTRIES; i++) {
        memset(buf, 0xAA, sizeof(buf));
        offset = 0xDEADBEEF;
        int len = symtab_find_symbol_name_r(golden_entries[i].addr, &offset, buf, sizeof(buf));
        if (len < 0 || offset != 0 || strcmp(buf, golden_entries[i].name) != 0) {
            printf("FAIL exact [%zu]: addr=0x%x expected='%s' got='%s' off=%u len=%d\\n",
                   i, golden_entries[i].addr, golden_entries[i].name, buf, offset, len);
            failed++;
        } else {
            passed++;
        }
    }
    
    /* 2. Interior lookups */
    for (size_t i = 0; i < NUM_GOLDEN_ENTRIES; i++) {
        if (golden_entries[i].size > 2) {
            memset(buf, 0xAA, sizeof(buf));
            offset = 0;
            int len = symtab_find_symbol_name_r(golden_entries[i].addr + 2, &offset, buf, sizeof(buf));
            if (len < 0 || offset != 2 || strcmp(buf, golden_entries[i].name) != 0) {
                printf("FAIL interior [%zu]: addr=0x%x+2 expected='%s' got='%s' off=%u\\n",
                       i, golden_entries[i].addr, golden_entries[i].name, buf, offset);
                failed++;
            } else {
                passed++;
            }
        }
    }
    
    /* 3. Underflow */
    memset(buf, 0xAA, sizeof(buf));
    offset = 0xDEADBEEF;
    int ret_under = symtab_find_symbol_name_r(GOLDEN_FIRST_ADDR > 0x10 ? GOLDEN_FIRST_ADDR - 0x10 : 0, &offset, buf, sizeof(buf));
    if (ret_under != 1 || offset != 0 || strcmp(buf, "?") != 0) {
        printf("FAIL underflow: got='%s' off=%u ret=%d\\n", buf, offset, ret_under);
        failed++;
    } else {
        passed++;
    }
    
    /* 4. Overflow */
    uint32_t last_addr = golden_entries[NUM_GOLDEN_ENTRIES - 1].addr + 0x1000;
    memset(buf, 0xAA, sizeof(buf));
    offset = 0xDEADBEEF;
    int ret_over = symtab_find_symbol_name_r(last_addr, &offset, buf, sizeof(buf));
    if (ret_over != 1 || strcmp(buf, "?") != 0) {
        printf("FAIL overflow: got='%s' ret=%d\\n", buf, ret_over);
        failed++;
    } else {
        passed++;
    }
    
    /* 5. Truncation */
    for (size_t i = 0; i < (NUM_GOLDEN_ENTRIES > 10 ? 10 : NUM_GOLDEN_ENTRIES); i++) {
        size_t expected_len = strlen(golden_entries[i].name);
        for (size_t lim = 0; lim <= expected_len + 3; lim++) {
            memset(buf, 0x55, sizeof(buf));
            offset = 0;
            int ret = symtab_find_symbol_name_r(golden_entries[i].addr, &offset, buf, lim);
            if (lim == 0) {
                if (ret != -1) failed++; else passed++;
            } else if (lim <= expected_len) {
                if (buf[lim - 1] != '\\0' || strncmp(buf, golden_entries[i].name, lim - 1) != 0) {
                    failed++;
                } else {
                    passed++;
                }
            } else {
                if (buf[expected_len] != '\\0' || strcmp(buf, golden_entries[i].name) != 0) {
                    failed++;
                } else {
                    passed++;
                }
            }
        }
    }
    
    printf("RESULTS: passed=%d, failed=%d\\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
"""

def main():
    workloads = [
        ("hello_world_mps2", "data/zephyr_symtab_hello_world_158.json"),
        ("shell_module_mps2", "data/zephyr_symtab_shell_module_557.json"),
        ("echo_server_mps2", "data/zephyr_symtab_echo_server_1381.json"),
        ("hello_world_nrf52840", "data/zephyr_symtab_nrf52840_hello_226.json")
    ]
    
    all_passed = True
    test_reports = {}
    
    for wid, jpath in workloads:
        with open(jpath, "r") as f:
            entries = json.load(f)
            
        g_lines = []
        for e in entries:
            # escape name
            esc_name = e["name"].replace('\\', '\\\\').replace('"', '\\"')
            g_lines.append(f'\t{{ {hex(e["addr"])}, {hex(e["offset"])}, {hex(e["size"])}, "{esc_name}" }},')
            
        code = TEST_C_TEMPLATE
        code = code.replace("%GOLDEN_ENTRIES%", "\n".join(g_lines))
        code = code.replace("%NUM_ENTRIES%", str(len(entries)))
        code = code.replace("%FIRST_ADDR%", hex(entries[0]["addr"]))
        
        main_c_path = f"build/real_workloads/test_{wid}_main.c"
        with open(main_c_path, "w") as f:
            f.write(code)
            
        test_reports[wid] = {}
        for mode in ["direct", "checkpoint"]:
            c_cand = f"build/real_workloads/{wid}_{mode}.c"
            bin_path = f"build/real_workloads/test_{wid}_{mode}"
            
            # Compile with host gcc
            cmd = f"gcc -O2 -Iinclude {main_c_path} {c_cand} -o {bin_path}"
            res = subprocess.run(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
            if res.returncode != 0:
                print(f"Compilation error for {bin_path}: {res.stderr}")
                all_passed = False
                continue
                
            run_res = subprocess.run(bin_path, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
            out = run_res.stdout.strip()
            print(f"[{wid}] [{mode.upper()}] -> {out}")
            test_reports[wid][mode] = out
            if run_res.returncode != 0 or "failed=0" not in out:
                all_passed = False

    with open("results/all_workload_correctness_report.json", "w") as f:
        json.dump(test_reports, f, indent=2)
        
    if all_passed:
        print("\nALL WORKLOAD CORRECTNESS TESTS PASSED 100%!")
    else:
        print("\nSOME CORRECTNESS TESTS FAILED!")

if __name__ == "__main__":
    main()
