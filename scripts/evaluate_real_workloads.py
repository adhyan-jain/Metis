#!/usr/bin/env python3
"""
Full Real-Workload Evaluation Driver for Zephyr Symbol Table Compression.
Evaluates:
  1. samples/hello_world (mps2/an385 - ARM Cortex-M3)
  2. samples/subsys/shell/shell_module (mps2/an385 - ARM Cortex-M3)
  3. samples/net/sockets/echo_server (mps2/an385 - ARM Cortex-M3)
  4. samples/hello_world (nrf52840dk/nrf52840 - ARM Cortex-M4 constrained target)

Strict non-negotiable rule: 100% real symbols extracted from real builds.
No synthetic datasets, no extrapolated counts.
"""

import os
import re
import sys
import json
import subprocess

CC_ARM = "/home/adhyan/local-toolchain/bin/arm-none-eabi-clang"
OBJCOPY_ARM = "/home/adhyan/local-toolchain/bin/arm-none-eabi-objcopy"
READELF_ARM = "/home/adhyan/local-toolchain/bin/arm-none-eabi-readelf"
GCC_ARM = "/home/adhyan/local-toolchain/bin/arm-none-eabi-gcc"

WORKLOADS = [
    {
        "id": "hello_world_mps2",
        "name": "hello_world",
        "board": "mps2/an385",
        "arch": "Cortex-M3",
        "cflags": "-mcpu=cortex-m3 -mthumb -Os",
        "build_y": "/tmp/zephyr_build_symtab_y",
        "build_n": "/tmp/zephyr_build_symtab_n",
        "data_file": "data/zephyr_symtab_hello_world_158.json",
        "optimal_budget": 64
    },
    {
        "id": "shell_module_mps2",
        "name": "shell_module",
        "board": "mps2/an385",
        "arch": "Cortex-M3",
        "cflags": "-mcpu=cortex-m3 -mthumb -Os",
        "build_y": "/tmp/zephyr_build_shell_symtab_y",
        "build_n": "/tmp/zephyr_build_shell_symtab_n",
        "data_file": "data/zephyr_symtab_shell_module_557.json",
        "optimal_budget": 128
    },
    {
        "id": "echo_server_mps2",
        "name": "echo_server",
        "board": "mps2/an385",
        "arch": "Cortex-M3",
        "cflags": "-mcpu=cortex-m3 -mthumb -Os",
        "build_y": "/tmp/zephyr_build_echo_symtab_y",
        "build_n": "/tmp/zephyr_build_echo_symtab_n",
        "data_file": "data/zephyr_symtab_echo_server_1381.json",
        "optimal_budget": 128
    },
    {
        "id": "hello_world_nrf52840",
        "name": "hello_world (constrained)",
        "board": "nrf52840dk/nrf52840",
        "arch": "Cortex-M4",
        "cflags": "-mcpu=cortex-m4 -mthumb -Os",
        "build_y": "/tmp/test_build_nrf",
        "build_n": "/tmp/test_build_nrf_symtab_n",
        "data_file": "data/zephyr_symtab_nrf52840_hello_226.json",
        "optimal_budget": 64
    }
]

def run_cmd(cmd, cwd=None):
    res = subprocess.run(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True, cwd=cwd)
    if res.returncode != 0:
        print(f"Error executing: {cmd}\nStderr: {res.stderr[:500]}")
    return res.returncode, res.stdout, res.stderr

def parse_symtab_c(symtab_c_path):
    with open(symtab_c_path, "r") as f:
        content = f.read()

    first_addr_match = re.search(r'\.first_addr\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)', content)
    first_addr = int(first_addr_match.group(1), 0) if first_addr_match else 0

    entry_pattern = re.compile(
        r'(?:/\*\s*ADDR:\s*(0x[0-9a-fA-F]+)\s*SIZE:\s*(0x[0-9a-fA-F]+)\s*\*/\s*)?'
        r'\[(\d+)\]\s*=\s*\{\s*\.offset\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*,\s*\.name\s*=\s*"([^"]+)"\s*\}'
    )

    entries = []
    for match in entry_pattern.finditer(content):
        addr_str, size_str, idx_str, offset_str, name = match.groups()
        idx = int(idx_str)
        offset = int(offset_str, 0)
        addr = int(addr_str, 0) if addr_str else (first_addr + offset)
        size = int(size_str, 0) if size_str else 0
        entries.append({
            "index": idx,
            "addr": addr,
            "offset": offset,
            "size": size,
            "name": name
        })

    return first_addr, entries

def learn_dictionary(names, budget):
    import collections
    words = [[c for c in w] for w in names]
    dictionary = []
    
    for step in range(budget):
        cand_counts = collections.defaultdict(int)
        for w in words:
            n = len(w)
            i = 0
            while i < n:
                if isinstance(w[i], str):
                    j = i
                    while j < n and isinstance(w[j], str):
                        j += 1
                    s_run = "".join(w[i:j])
                    seen_in_run = set()
                    for l in range(2, min(len(s_run) + 1, 16)):
                        for k in range(len(s_run) - l + 1):
                            seen_in_run.add(s_run[k:k+l])
                    for sub in seen_in_run:
                        cand_counts[sub] += 1
                    i = j
                else:
                    i += 1
                    
        best_cand = None
        best_save = 0
        for cand, count in cand_counts.items():
            save = count * (len(cand) - 1) - (len(cand) + 2)
            if save > best_save:
                best_save = save
                best_cand = cand
                
        if not best_cand or best_save <= 0:
            break
            
        token_id = 0x80 + len(dictionary)
        dictionary.append(best_cand)
        cand_len = len(best_cand)
        for w_idx in range(len(words)):
            w = words[w_idx]
            new_w = []
            idx = 0
            while idx < len(w):
                if all(idx + k < len(w) and w[idx+k] == best_cand[k] for k in range(cand_len)):
                    new_w.append(token_id)
                    idx += cand_len
                else:
                    new_w.append(w[idx])
                    idx += 1
            words[w_idx] = new_w

    return dictionary

def encode_names(names, dictionary):
    encoded = []
    for name in names:
        w = [c for c in name]
        for token_idx, tok_str in enumerate(dictionary):
            token_id = 0x80 + token_idx
            cand_len = len(tok_str)
            new_w = []
            idx = 0
            while idx < len(w):
                if all(idx + k < len(w) and w[idx+k] == tok_str[k] for k in range(cand_len)):
                    new_w.append(token_id)
                    idx += cand_len
                else:
                    new_w.append(w[idx])
                    idx += 1
            w = new_w
        b = bytearray()
        for atom in w:
            if isinstance(atom, str):
                b.append(ord(atom))
            else:
                b.append(atom)
        b.append(0) # null terminator
        encoded.append(b)
    return encoded

def generate_c_code(entries, dictionary, encoded_names, use_checkpoints=False, checkpoint_interval=16):
    num_symbols = len(entries)
    first_addr = entries[0]["addr"]
    
    dict_pool = bytearray()
    dict_offsets = []
    for tok in dictionary:
        dict_offsets.append(len(dict_pool))
        dict_pool.extend(tok.encode("ascii") + b"\0")
        
    packed_names = bytearray()
    name_offsets = []
    checkpoints = []
    
    for i, enc in enumerate(encoded_names):
        if use_checkpoints:
            if i % checkpoint_interval == 0:
                checkpoints.append(len(packed_names))
        else:
            name_offsets.append(len(packed_names))
        packed_names.extend(enc)
        
    code = []
    code.append("/* AUTO-GENERATED - DO NOT EDIT */")
    code.append("#include <stdint.h>")
    code.append("#include <stddef.h>")
    code.append("#include <stdbool.h>")
    code.append("")
    code.append(f"#define Z_SYMTAB_NUM_SYMBOLS {num_symbols}")
    code.append(f"#define Z_SYMTAB_FIRST_ADDR  {hex(first_addr)}")
    code.append(f"#define Z_SYMTAB_NUM_TOKENS   {len(dictionary)}")
    code.append("")
    code.append(f"static const uint32_t z_symtab_addr_offsets[{num_symbols}] = {{")
    for i, e in enumerate(entries):
        code.append(f"\t{hex(e['offset'])},")
    code.append("};")
    code.append("")
    
    if len(dictionary) > 0:
        code.append(f"static const char z_symtab_dict_pool[{len(dict_pool)}] = {{")
        chunks = [f"0x{b:02x}" for b in dict_pool]
        for i in range(0, len(chunks), 16):
            code.append("\t" + ", ".join(chunks[i:i+16]) + ",")
        code.append("};")
        code.append("")
        off_type = "uint8_t" if len(dict_pool) <= 256 else "uint16_t"
        code.append(f"static const {off_type} z_symtab_dict_offsets[{len(dict_offsets)}] = {{")
        for off in dict_offsets:
            code.append(f"\t{off},")
        code.append("};")
        code.append("")
        
    if use_checkpoints:
        code.append(f"#define Z_SYMTAB_CHECKPOINT_INTERVAL {checkpoint_interval}")
        code.append(f"static const uint16_t z_symtab_checkpoints[{len(checkpoints)}] = {{")
        for cp in checkpoints:
            code.append(f"\t{cp},")
        code.append("};")
        code.append("")
    else:
        off_t = "uint16_t" if len(packed_names) <= 65535 else "uint32_t"
        code.append(f"static const {off_t} z_symtab_name_offsets[{len(name_offsets)}] = {{")
        for noff in name_offsets:
            code.append(f"\t{noff},")
        code.append("};")
        code.append("")
        
    code.append(f"static const uint8_t z_symtab_names_compressed[{len(packed_names)}] = {{")
    chunks = [f"0x{b:02x}" for b in packed_names]
    for i in range(0, len(chunks), 16):
        code.append("\t" + ", ".join(chunks[i:i+16]) + ",")
    code.append("};")
    code.append("")
    
    code.append("static int z_symtab_decode_symbol(uint32_t sym_idx, char *buf, size_t buflen) {")
    code.append("\tif (sym_idx >= Z_SYMTAB_NUM_SYMBOLS || buflen == 0) return -1;")
    if use_checkpoints:
        code.append("\tuint32_t cp_idx = sym_idx / Z_SYMTAB_CHECKPOINT_INTERVAL;")
        code.append("\tuint32_t rem = sym_idx % Z_SYMTAB_CHECKPOINT_INTERVAL;")
        code.append("\tconst uint8_t *p = &z_symtab_names_compressed[z_symtab_checkpoints[cp_idx]];")
        code.append("\twhile (rem > 0) { while (*p++ != 0) { } rem--; }")
    else:
        code.append("\tconst uint8_t *p = &z_symtab_names_compressed[z_symtab_name_offsets[sym_idx]];")
    code.append("\tsize_t out = 0;")
    code.append("\twhile (*p != 0) {")
    code.append("\t\tuint8_t byte = *p++;")
    if len(dictionary) > 0:
        code.append("\t\tif (byte >= 0x80 && (byte - 0x80) < Z_SYMTAB_NUM_TOKENS) {")
        code.append("\t\t\tuint32_t tok_idx = byte - 0x80;")
        code.append("\t\t\tconst char *tok = &z_symtab_dict_pool[z_symtab_dict_offsets[tok_idx]];")
        code.append("\t\t\twhile (*tok) { if (out + 1 < buflen) buf[out++] = *tok; tok++; }")
        code.append("\t\t} else { if (out + 1 < buflen) buf[out++] = (char)byte; }")
    else:
        code.append("\t\tif (out + 1 < buflen) buf[out++] = (char)byte;")
    code.append("\t}")
    code.append("\tbuf[out < buflen ? out : buflen - 1] = '\\0';")
    code.append("\treturn (int)out;")
    code.append("}")
    code.append("")
    code.append("int symtab_find_symbol_name_r(uintptr_t addr, uint32_t *offset, char *buf, size_t buflen) {")
    code.append("\tif (buflen == 0) return -1;")
    code.append("\tconst uint32_t first_addr = Z_SYMTAB_FIRST_ADDR;")
    code.append("\tuint32_t ret_offset = 0; int ret = 0;")
    code.append("\tif (addr < first_addr) { if (offset) *offset = 0; buf[0] = '?'; buf[1] = '\\0'; return 1; }")
    code.append("\tconst uint32_t symbol_offset = addr - first_addr;")
    code.append("\tif (symbol_offset < z_symtab_addr_offsets[Z_SYMTAB_NUM_SYMBOLS - 1]) {")
    code.append("\t\tuint32_t left = 0, right = Z_SYMTAB_NUM_SYMBOLS - 2;")
    code.append("\t\twhile (left <= right) {")
    code.append("\t\t\tuint32_t mid = left + (right - left) / 2;")
    code.append("\t\t\tif (symbol_offset >= z_symtab_addr_offsets[mid] && symbol_offset < z_symtab_addr_offsets[mid + 1]) {")
    code.append("\t\t\t\tret_offset = symbol_offset - z_symtab_addr_offsets[mid];")
    code.append("\t\t\t\tret = z_symtab_decode_symbol(mid, buf, buflen); break;")
    code.append("\t\t\t} else if (symbol_offset < z_symtab_addr_offsets[mid]) {")
    code.append("\t\t\t\tif (mid == 0) break; right = mid - 1;")
    code.append("\t\t\t} else { left = mid + 1; }")
    code.append("\t\t}")
    code.append("\t} else { buf[0] = '?'; buf[1] = '\\0'; ret = 1; }")
    code.append("\tif (offset != NULL) *offset = ret_offset;")
    code.append("\treturn ret;")
    code.append("}")
    
    dict_b = len(dict_pool) + len(dict_offsets) * (1 if len(dict_pool) <= 256 else 2)
    idx_b = (len(checkpoints) * 2 if use_checkpoints else len(name_offsets) * (2 if len(packed_names) <= 65535 else 4)) + num_symbols * 4
    return "\n".join(code), {
        "num_symbols": num_symbols,
        "dict_bytes": dict_b,
        "index_bytes": idx_b,
        "encoded_names_bytes": len(packed_names),
        "total_rodata": dict_b + idx_b + len(packed_names)
    }

def main():
    os.makedirs("data", exist_ok=True)
    os.makedirs("build/real_workloads", exist_ok=True)
    os.makedirs("results", exist_ok=True)
    
    summary = []
    
    for wl in WORKLOADS:
        print(f"\n================================================================================")
        print(f"  Evaluating Workload: {wl['name']} ({wl['board']})")
        print(f"================================================================================")
        
        symtab_c = os.path.join(wl["build_y"], "zephyr/symtab.c")
        if not os.path.exists(symtab_c):
            print(f"Skipping {wl['id']}: {symtab_c} not found")
            continue
            
        first_addr, entries = parse_symtab_c(symtab_c)
        num_symbols = len(entries)
        names = [e["name"] for e in entries]
        unique_names = len(set(names))
        raw_name_bytes = sum(len(n.encode("utf-8")) + 1 for n in names)
        entry_table_bytes = num_symbols * 8 + 12 # 8 bytes/entry + 12B metadata
        table_overhead_bytes = entry_table_bytes + raw_name_bytes
        
        # Save dataset
        with open(wl["data_file"], "w") as f:
            json.dump(entries, f, indent=2)
            
        print(f"Symbols: {num_symbols} (Unique: {unique_names})")
        print(f"Raw string bytes: {raw_name_bytes} B | Entry table: {entry_table_bytes} B | Total table: {table_overhead_bytes} B")
        
        # Get baseline images
        bin_y = os.path.join(wl["build_y"], "zephyr/zephyr.bin")
        bin_n = os.path.join(wl["build_n"], "zephyr/zephyr.bin")
        elf_y = os.path.join(wl["build_y"], "zephyr/zephyr.elf")
        elf_n = os.path.join(wl["build_n"], "zephyr/zephyr.elf")
        
        run_cmd(f"{OBJCOPY_ARM} --gap-fill 0xFF --output-target=binary --remove-section=.comment --remove-section=COMMON {elf_y} {bin_y}")
        run_cmd(f"{OBJCOPY_ARM} --gap-fill 0xFF --output-target=binary --remove-section=.comment --remove-section=COMMON {elf_n} {bin_n}")
        
        size_flash_y = os.path.getsize(bin_y) if os.path.exists(bin_y) else 0
        size_flash_n = os.path.getsize(bin_n) if os.path.exists(bin_n) else 0
        feature_delta = size_flash_y - size_flash_n
        
        # RAM from readelf
        _, out_r_y, _ = run_cmd(f"{READELF_ARM} -l {elf_y}")
        _, out_r_n, _ = run_cmd(f"{READELF_ARM} -l {elf_n}")
        ram_y = 0
        for l in out_r_y.splitlines():
            if "0x20000000" in l or "20000000" in l:
                p = l.split()
                if len(p) >= 6: ram_y += int(p[5], 16)
        
        print(f"Stock SYMTAB=y Flash: {size_flash_y:,} B | SYMTAB=n Flash: {size_flash_n:,} B | Delta: {feature_delta:,} B")
        
        # Train dictionary
        budget = wl["optimal_budget"]
        dictionary = learn_dictionary(names, budget)
        encoded = encode_names(names, dictionary)
        print(f"Trained dictionary with {len(dictionary)} tokens (budget {budget})")
        
        # Evaluate Checkpoint and Direct candidates
        wl_results = {
            "workload": wl["name"],
            "board": wl["board"],
            "arch": wl["arch"],
            "num_symbols": num_symbols,
            "unique_names": unique_names,
            "raw_name_bytes": raw_name_bytes,
            "entry_table_bytes": entry_table_bytes,
            "stock_table_overhead_bytes": table_overhead_bytes,
            "stock_flash_y": size_flash_y,
            "stock_flash_n": size_flash_n,
            "feature_delta": feature_delta,
            "static_ram_y": ram_y,
            "variants": {}
        }
        
        for mode, use_cp in [("Direct", False), ("Checkpoint", True)]:
            c_code, stats = generate_c_code(entries, dictionary, encoded, use_checkpoints=use_cp, checkpoint_interval=16)
            c_file = f"build/real_workloads/{wl['id']}_{mode.lower()}.c"
            o_file = f"build/real_workloads/{wl['id']}_{mode.lower()}.o"
            with open(c_file, "w") as f:
                f.write(c_code)
                
            # Compile with target CFLAGS
            run_cmd(f"{CC_ARM} {wl['cflags']} -c {c_file} -o {o_file}")
            
            # Measure CU flash
            _, out_sz, _ = run_cmd(f"/usr/bin/llvm-size {o_file}")
            cu_flash = int(out_sz.splitlines()[1].split()[0])
            decoder_text = cu_flash - stats["total_rodata"]
            
            # Whole image link
            # Extract link line from build.ninja
            ninja_file = os.path.join(wl["build_y"], "build.ninja")
            with open(ninja_file, "r") as nf:
                ninja_content = nf.read()
                
            match_link = re.search(r'build zephyr/zephyr\.elf[^\n]*: C_EXECUTABLE_LINKER__zephyr_final_[^\n]*\n(?:\s+[^\n]*\n)*', ninja_content)
            
            cand_elf = os.path.join(wl["build_y"], f"zephyr/zephyr_candidate_{mode.lower()}.elf")
            cand_bin = os.path.join(wl["build_y"], f"zephyr/zephyr_candidate_{mode.lower()}.bin")
            cand_map = os.path.join(wl["build_y"], f"zephyr/zephyr_candidate_{mode.lower()}.map")
            
            # Find all objects and link flags
            # Use GCC command directly
            # Replace symtab.c.obj with o_file and add -Wl,-u,symtab_find_symbol_name_r
            rules_ninja = os.path.join(wl["build_y"], "CMakeFiles/rules.ninja")
            
            # We can execute ninja command or invoke the link line
            link_sh = f"""
cd {wl['build_y']} && \\
{GCC_ARM} -Os \\
  zephyr/CMakeFiles/zephyr_final.dir/misc/empty_file.c.obj \\
  zephyr/CMakeFiles/zephyr_final.dir/isr_tables.c.obj \\
  {os.path.abspath(o_file)} \\
  /home/adhyan/Desktop/Compiler/build/cost_sweep/libaeabi_helpers.a \\
  -o {cand_elf} \\
  zephyr/CMakeFiles/offsets.dir/./arch/arm/core/offsets/offsets.c.obj \\
  -T zephyr/linker.cmd \\
  -Wl,-Map,{cand_map} \\
  -Wl,--whole-archive \\
  app/libapp.a \\
  zephyr/libzephyr.a \\
  zephyr/arch/common/libarch__common.a \\
  zephyr/arch/arch/arm/core/libarch__arm__core.a \\
  zephyr/arch/arch/arm/core/cortex_m/libarch__arm__core__cortex_m.a \\
  zephyr/arch/arch/arm/core/mpu/libarch__arm__core__mpu.a \\
  `[ -f zephyr/lib/libc/picolibc/liblib__libc__picolibc.a ] && echo zephyr/lib/libc/picolibc/liblib__libc__picolibc.a` \\
  `[ -f zephyr/lib/libc/minimal/liblib__libc__minimal.a ] && echo zephyr/lib/libc/minimal/liblib__libc__minimal.a` \\
  zephyr/lib/libc/common/liblib__libc__common.a \\
  zephyr/subsys/debug/symtab/libsubsys__debug__symtab.a \\
  zephyr/subsys/portability/posix/c_lib_ext/libsubsys__portability__posix__c_lib_ext.a \\
  `[ -f zephyr/subsys/timing/libsubsys__timing.a ] && echo zephyr/subsys/timing/libsubsys__timing.a` \\
  `[ -f zephyr/subsys/net/ip/libsubsys__net__ip.a ] && echo zephyr/subsys/net/ip/libsubsys__net__ip.a` \\
  `[ -f zephyr/subsys/net/l2/ethernet/libsubsys__net__l2__ethernet.a ] && echo zephyr/subsys/net/l2/ethernet/libsubsys__net__l2__ethernet.a` \\
  `[ -f zephyr/subsys/net/lib/config/libsubsys__net__lib__config.a ] && echo zephyr/subsys/net/lib/config/libsubsys__net__lib__config.a` \\
  `[ -f zephyr/subsys/net/conn_mgr/libsubsys__net__conn_mgr.a ] && echo zephyr/subsys/net/conn_mgr/libsubsys__net__conn_mgr.a` \\
  `[ -f zephyr/subsys/net/libsubsys__net.a ] && echo zephyr/subsys/net/libsubsys__net.a` \\
  `[ -f zephyr/lib/net_buf/liblib__net_buf.a ] && echo zephyr/lib/net_buf/liblib__net_buf.a` \\
  `[ -f zephyr/lib/os/zvfs/liblib__os__zvfs.a ] && echo zephyr/lib/os/zvfs/liblib__os__zvfs.a` \\
  `[ -f zephyr/subsys/fs/libsubsys__fs.a ] && echo zephyr/subsys/fs/libsubsys__fs.a` \\
  `[ -f zephyr/subsys/random/libsubsys__random.a ] && echo zephyr/subsys/random/libsubsys__random.a` \\
  `[ -f zephyr/subsys/portability/posix/eventfd/libsubsys__portability__posix__eventfd.a ] && echo zephyr/subsys/portability/posix/eventfd/libsubsys__portability__posix__eventfd.a` \\
  `[ -f zephyr/subsys/portability/posix/options/libsubsys__portability__posix__options.a ] && echo zephyr/subsys/portability/posix/options/libsubsys__portability__posix__options.a` \\
  `[ -f zephyr/drivers/hwinfo/libdrivers__hwinfo.a ] && echo zephyr/drivers/hwinfo/libdrivers__hwinfo.a` \\
  `[ -f zephyr/drivers/ethernet/libdrivers__ethernet.a ] && echo zephyr/drivers/ethernet/libdrivers__ethernet.a` \\
  `[ -f zephyr/soc/soc/nrf52840/libsoc__nordic.a ] && echo zephyr/soc/soc/nrf52840/libsoc__nordic.a` \\
  `[ -f modules/hal_nordic/nrfx/libnrfx.a ] && echo modules/hal_nordic/nrfx/libnrfx.a` \\
  `[ -f zephyr/drivers/clock_control/libdrivers__clock_control.a ] && echo zephyr/drivers/clock_control/libdrivers__clock_control.a` \\
  zephyr/drivers/console/libdrivers__console.a \\
  zephyr/drivers/gpio/libdrivers__gpio.a \\
  zephyr/drivers/pinctrl/libdrivers__pinctrl.a \\
  zephyr/drivers/serial/libdrivers__serial.a \\
  zephyr/drivers/timer/libdrivers__timer.a \\
  -Wl,--no-whole-archive \\
  zephyr/kernel/libkernel.a \\
  -L{wl['build_y']}/zephyr \\
  zephyr/arch/common/libisr_tables.a \\
  zephyr/subsys/debug/symtab/libsymtab.a \\
  -fuse-ld=bfd \\
  {wl['cflags']} \\
  -mabi=aapcs \\
  -mfp16-format=ieee \\
  -Wl,--gc-sections \\
  -Wl,--build-id=none \\
  -Wl,--sort-common=descending \\
  -Wl,--sort-section=alignment \\
  -Wl,-u,_OffsetAbsSyms \\
  -Wl,-u,_ConfigAbsSyms \\
  -nostdlib \\
  -static \\
  -Wl,-X \\
  -Wl,-N \\
  -Wl,--orphan-handling=warn \\
  -Wl,--undefined=_sw_isr_table \\
  -Wl,--undefined=_irq_vector_table \\
  -Wl,-u,symtab_find_symbol_name_r \\
  -L"/home/adhyan/local-toolchain/lib/arm-none-eabi" \\
  `[ -f {wl['build_y']}/modules/picolibc/libc.a ] && echo {wl['build_y']}/modules/picolibc/libc.a` \\
  -lclang_rt.builtins
"""
            ret, _, err = run_cmd(link_sh)
            if ret != 0:
                print(f"Link failed for {wl['id']} {mode}")
                continue
                
            run_cmd(f"{OBJCOPY_ARM} --gap-fill 0xFF --output-target=binary --remove-section=.comment --remove-section=COMMON {cand_elf} {cand_bin}")
            cand_flash = os.path.getsize(cand_bin) if os.path.exists(cand_bin) else 0
            
            # RAM
            _, out_cr, _ = run_cmd(f"{READELF_ARM} -l {cand_elf}")
            cand_ram = 0
            for l in out_cr.splitlines():
                if "0x20000000" in l or "20000000" in l:
                    p = l.split()
                    if len(p) >= 6: cand_ram += int(p[5], 16)
                    
            savings = size_flash_y - cand_flash
            pct = (savings / feature_delta * 100) if feature_delta > 0 else 0
            
            wl_results["variants"][mode] = {
                "encoded_names_bytes": stats["encoded_names_bytes"],
                "dict_bytes": stats["dict_bytes"],
                "index_bytes": stats["index_bytes"],
                "total_rodata": stats["total_rodata"],
                "decoder_text": decoder_text,
                "cu_flash": cu_flash,
                "whole_image_flash": cand_flash,
                "net_savings_bytes": savings,
                "reduction_percent": round(pct, 2),
                "whole_image_ram": cand_ram,
                "ram_delta": cand_ram - ram_y
            }
            print(f"[{mode:<10}] CU Flash: {cu_flash:,} B (Text: {decoder_text} B, Rodata: {stats['total_rodata']} B) | Image Flash: {cand_flash:,} B | Net Savings: {savings:,} B ({pct:.1f}%) | RAM Delta: {cand_ram - ram_y} B")

        summary.append(wl_results)

    with open("results/real_workload_results.json", "w") as f:
        json.dump(summary, f, indent=2)
    print("\nSaved comprehensive results to results/real_workload_results.json")

if __name__ == "__main__":
    main()
