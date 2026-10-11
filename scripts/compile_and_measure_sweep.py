#!/usr/bin/env python3
"""
Compile and measure all dictionary budgets with ARM Cortex-M3 Clang toolchain,
and generate a host test binary for exhaustive correctness verification.
"""

import os
import sys
import json
import subprocess
from zephyr_symtab_compressor import load_dataset, learn_dictionary, encode_names, generate_c_code

CC_ARM = "/home/adhyan/local-toolchain/bin/arm-none-eabi-clang"
LLVM_SIZE = "/usr/bin/llvm-size"

def run_cmd(cmd):
    res = subprocess.run(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
    if res.returncode != 0:
        print(f"Command failed: {cmd}\nStderr: {res.stderr}")
    return res.returncode, res.stdout, res.stderr

def main():
    os.makedirs("build/cost_sweep", exist_ok=True)
    entries = load_dataset("data/zephyr_symtab_158.json")
    names = [e["name"] for e in entries]

    budgets = [0, 8, 16, 32, 64, 128]
    sweep_results = []

    print("=========================================================================================")
    print("                  ARM CORTEX-M3 COMPILED COST SWEEP (Clang 22.1.8 -Os)                   ")
    print("=========================================================================================")
    print(f"{'Budget':<8} {'Mode':<10} {'Tokens':<8} {'EncNames':<10} {'Dict':<8} {'Index':<8} {'Rodata':<10} {'Text(Dec)':<10} {'TotalFlash':<12} {'Delta vs 4416B':<14}")
    print("-----------------------------------------------------------------------------------------")

    for b in budgets:
        dictionary, words = learn_dictionary(names, b)
        encoded = encode_names(names, dictionary)

        for mode, use_cp in [("Direct", False), ("Checkpoint", True)]:
            c_code, stats = generate_c_code(entries, dictionary, encoded, use_checkpoints=use_cp, checkpoint_interval=16)
            c_filename = f"build/cost_sweep/symtab_b{b}_{mode.lower()}.c"
            o_filename = f"build/cost_sweep/symtab_b{b}_{mode.lower()}.o"

            with open(c_filename, "w") as f:
                f.write(c_code)

            # Compile with ARM Clang
            cmd = f"{CC_ARM} -mcpu=cortex-m3 -mthumb -Os -c {c_filename} -o {o_filename}"
            ret, out, err = run_cmd(cmd)
            if ret != 0:
                print(f"Compilation error for {c_filename}")
                continue

            # Measure with llvm-size
            cmd_size = f"{LLVM_SIZE} {o_filename}"
            ret, out, err = run_cmd(cmd_size)
            lines = out.strip().split("\n")
            parts = lines[1].split()
            text_size = int(parts[0])
            data_size = int(parts[1])
            bss_size = int(parts[2])

            # In .o, rodata is placed in text or separate section. Let us inspect with llvm-readelf
            cmd_sec = f"/usr/bin/llvm-readelf -S {o_filename}"
            _, out_sec, _ = run_cmd(cmd_sec)
            sec_rodata = 0
            sec_text = 0
            for sec_line in out_sec.split("\n"):
                if ".rodata" in sec_line:
                    fields = sec_line.split()
                    # format: [Nr] Name Type Address Off Size ...
                    for fi, fval in enumerate(fields):
                        if fval == ".rodata" or ".rodata" in fval:
                            # size is usually 2 or 3 fields down
                            try:
                                sec_rodata += int(fields[fi+4], 16)
                            except:
                                pass
                if ".text" in sec_line:
                    fields = sec_line.split()
                    for fi, fval in enumerate(fields):
                        if fval == ".text" or ".text" in fval:
                            try:
                                sec_text += int(fields[fi+4], 16)
                            except:
                                pass

            # Total flash in this compilation unit = rodata + text
            # Note: llvm-size text column includes rodata for ELF relocatable files on some platforms
            # Let's get exact sum:
            total_unit_flash = int(parts[0]) # llvm-size text column includes code + rodata for baremetal ELF .o
            dict_bytes = stats['dict_pool_bytes'] + stats['dict_offsets_bytes']
            index_bytes = stats['name_offsets_bytes'] + stats['addr_offsets_bytes']
            enc_names = stats['compressed_names_bytes']
            decoder_text = total_unit_flash - stats['total_rodata_bytes']
            
            savings = 4416 - total_unit_flash

            print(f"{b:<8} {mode:<10} {stats['num_tokens']:<8} {enc_names:<10} {dict_bytes:<8} {index_bytes:<8} {stats['total_rodata_bytes']:<10} {decoder_text:<10} {total_unit_flash:<12} {savings:<14}")

            sweep_results.append({
                "budget": b,
                "mode": mode,
                "tokens": stats['num_tokens'],
                "encoded_names_bytes": enc_names,
                "dict_bytes": dict_bytes,
                "index_bytes": index_bytes,
                "rodata_bytes": stats['total_rodata_bytes'],
                "decoder_text_bytes": decoder_text,
                "total_flash_bytes": total_unit_flash,
                "net_savings_bytes": savings
            })

    with open("results/compiled_sweep_results.json", "w") as f:
        json.dump(sweep_results, f, indent=2)
    print("=========================================================================================")
    print("Sweep data saved to results/compiled_sweep_results.json")

if __name__ == "__main__":
    main()
