#!/usr/bin/env python3
"""Cross-Consumer String Sharing Audit.
Inspects authoritative Zephyr ELFs, linker maps, and loadable sections.
Measures exact overlap between METIS function names and other flash-resident strings.
Calculates upper bounds and evaluates sharing mechanisms A and B.
"""
import os, sys, json, csv, collections
from elftools.elf.elffile import ELFFile

ROOT = "/home/adhyan/Desktop/Compiler"
WORKLOADS = {
    "hello_world_mps2": {
        "metis_elf": f"{ROOT}/research/metis_final_tournament/work/img/hello_world_mps2_BLPR_B32.elf",
        "metis_map": f"{ROOT}/research/metis_final_tournament/work/img/hello_world_mps2_BLPR_B32.map",
        "no_symtab_elf": "/tmp/zephyr_build_symtab_n/zephyr/zephyr.elf",
        "stock_elf": "/tmp/zephyr_build_symtab_y/zephyr/zephyr.elf",
        "symtab_json": f"{ROOT}/data/zephyr_symtab_hello_world_158.json",
        "bin_size_blpr": 17568,
        "bin_size_stock": 19792
    },
    "shell_module_mps2": {
        "metis_elf": f"{ROOT}/research/metis_final_tournament/work/img/shell_module_mps2_BLPR_B32.elf",
        "metis_map": f"{ROOT}/research/metis_final_tournament/work/img/shell_module_mps2_BLPR_B32.map",
        "no_symtab_elf": "/tmp/zephyr_build_shell_symtab_n/zephyr/zephyr.elf",
        "stock_elf": "/tmp/zephyr_build_shell_symtab_y/zephyr/zephyr.elf",
        "symtab_json": f"{ROOT}/data/zephyr_symtab_shell_module_557.json",
        "bin_size_blpr": 80624,
        "bin_size_stock": 89112
    },
    "echo_server_mps2": {
        "metis_elf": f"{ROOT}/research/metis_final_tournament/work/img/echo_server_mps2_BLPR_B32.elf",
        "metis_map": f"{ROOT}/research/metis_final_tournament/work/img/echo_server_mps2_BLPR_B32.map",
        "no_symtab_elf": "/tmp/zephyr_build_echo_symtab_n/zephyr/zephyr.elf",
        "stock_elf": "/tmp/zephyr_build_echo_symtab_y/zephyr/zephyr.elf",
        "symtab_json": f"{ROOT}/data/zephyr_symtab_echo_server_1381.json",
        "bin_size_blpr": 269448,
        "bin_size_stock": 291936
    },
    "hello_world_nrf52840": {
        "metis_elf": f"{ROOT}/research/metis_final_tournament/work/img/hello_world_nrf52840_BLPR_B32.elf",
        "metis_map": f"{ROOT}/research/metis_final_tournament/work/img/hello_world_nrf52840_BLPR_B32.map",
        "no_symtab_elf": "/tmp/test_build_nrf_symtab_n/zephyr/zephyr.elf",
        "stock_elf": "/tmp/test_build_nrf/zephyr/zephyr.elf",
        "symtab_json": f"{ROOT}/data/zephyr_symtab_nrf52840_hello_226.json",
        "bin_size_blpr": 27120,
        "bin_size_stock": 30200
    }
}

def extract_strings_from_bytes(data, base_addr):
    """Extract all NUL-terminated ASCII strings (length >= 2) with their virtual address."""
    strings = []
    start = 0
    while start < len(data):
        end = data.find(b'\0', start)
        if end == -1:
            break
        chunk = data[start:end]
        if len(chunk) >= 2:
            try:
                s_str = chunk.decode('ascii')
                if all(32 <= ord(c) <= 126 for c in s_str):
                    strings.append((base_addr + start, s_str, len(chunk) + 1))
            except UnicodeDecodeError:
                pass
        start = end + 1
    return strings

def get_symbol_map(elf_path):
    """Extract symbols from ELF symtab."""
    syms = {}
    with open(elf_path, "rb") as f:
        elf = ELFFile(f)
        symtab = elf.get_section_by_name(".symtab")
        if symtab:
            for s in symtab.iter_symbols():
                if s.name:
                    syms[s.name] = (s.entry["st_value"], s.entry["st_size"])
    return syms

def audit_workload(wl_name, cfg):
    print(f"\n=======================================================")
    print(f"Auditing Workload: {wl_name}")
    print(f"=======================================================")
    
    # 1. Load METIS function names
    symtab_data = json.load(open(cfg["symtab_json"]))
    func_names = [x["name"] for x in symtab_data if x["name"] != "?"]
    func_set = set(func_names)
    total_funcs = len(func_names)
    unique_funcs = len(func_set)
    print(f"METIS symbol inventory: {total_funcs} functions ({unique_funcs} unique)")
    
    # 2. Inspect METIS-linked ELF
    metis_elf_path = cfg["metis_elf"]
    metis_syms = get_symbol_map(metis_elf_path)
    
    # Determine the boundaries of METIS's own symbol tables:
    # name_pool, local_ranks, dir_base, dir_am, dir_noff, addr_stream, tok_tab
    metis_own_ranges = []
    for s_name in ("name_pool", "local_ranks", "dir_base", "dir_am", "dir_noff", "addr_stream", "tok_tab", "sym_ids"):
        if s_name in metis_syms:
            addr, size = metis_syms[s_name]
            metis_own_ranges.append((addr, addr + size, s_name))
            
    # Load all flash sections from METIS ELF
    with open(metis_elf_path, "rb") as f:
        elf = ELFFile(f)
        flash_sections = {}
        for sec in elf.iter_sections():
            flags = sec['sh_flags']
            addr = sec['sh_addr']
            size = sec['sh_size']
            name = sec.name
            if (flags & 0x2) and addr < 0x20000000 and size > 0:
                if not name.startswith(".debug") and name not in (".comment", ".ARM.attributes"):
                    flash_sections[name] = (addr, size, sec.data())
                    
    # Find all strings in non-METIS flash regions
    external_flash_strings = {}
    total_external_string_bytes = 0
    
    for sec_name, (sec_addr, sec_size, sec_data) in flash_sections.items():
        all_s = extract_strings_from_bytes(sec_data, sec_addr)
        for s_addr, s_text, s_len in all_s:
            # Check if this string lies inside METIS's own data tables
            is_metis_own = any(start <= s_addr < end for start, end, _ in metis_own_ranges)
            if not is_metis_own:
                if s_text not in external_flash_strings:
                    external_flash_strings[s_text] = []
                external_flash_strings[s_text].append((sec_name, s_addr, s_len))
                total_external_string_bytes += s_len
                
    print(f"Flash sections in METIS firmware: {list(flash_sections.keys())}")
    print(f"METIS internal table ranges excluded: {[(name, hex(st), sz) for st, sz, name in metis_own_ranges]}")
    print(f"Non-METIS external flash strings found: {len(external_flash_strings)} distinct strings")
    
    # 3. Measure exact matches between METIS function names and external flash strings
    matched_functions = {}
    raw_duplicate_bytes = 0
    unique_matched_string_bytes = 0
    
    for fn in func_set:
        if fn in external_flash_strings:
            occs = external_flash_strings[fn]
            matched_functions[fn] = occs
            s_len = len(fn) + 1
            raw_duplicate_bytes += s_len * len(occs)
            unique_matched_string_bytes += s_len
            
    K = len(matched_functions)
    match_pct = 100.0 * K / total_funcs
    print(f"\nExact matches found: {K} of {total_funcs} functions ({match_pct:.2f}%)")
    print(f"Raw duplicate bytes across external occurrences: {raw_duplicate_bytes} B")
    print(f"Unique matched string bytes: {unique_matched_string_bytes} B")
    
    # Detailed match inventory
    match_inventory = []
    consumer_counts = collections.Counter()
    
    for fn, occs in sorted(matched_functions.items(), key=lambda x: -len(x[1])):
        sections_present = list(set(sec for sec, _, _ in occs))
        for sec, addr, _ in occs:
            # Classify consumer
            if "rodata" in sec:
                consumer = "rodata_const"
            elif "text" in sec:
                consumer = "text_inline"
            elif "shell" in sec:
                consumer = "shell_cmd"
            else:
                consumer = sec
            consumer_counts[consumer] += 1
            
        match_inventory.append({
            "function_name": fn,
            "length_with_nul": len(fn) + 1,
            "occurrences": len(occs),
            "sections": sections_present,
            "addresses": [hex(a) for _, a, _ in occs]
        })
        
    print(f"Consumers containing matches: {dict(consumer_counts)}")
    print(f"Matching functions list: {[m['function_name'] for m in match_inventory]}")
    
    # 4. Model Mechanism A: Reuse existing external full string
    # Current METIS cost for these K functions:
    # In BLPR/FC16, average name cost is ~8 bytes per symbol.
    metis_compressed_pool_bytes = {
        "hello_world_mps2": 1165, # BLPR B32 pool
        "shell_module_mps2": 4488,
        "echo_server_mps2": 11112,
        "hello_world_nrf52840": 1764,
    }[wl_name]
    avg_compressed_bytes = metis_compressed_pool_bytes / total_funcs
    gross_saved_metis = K * avg_compressed_bytes
    
    # Metadata required to reference external string:
    # 1. External pointer / relative offset: 2 bytes (optimistic 16-bit offset into .rodata) or 4 bytes pointer
    ext_ref_cost = K * 2
    # 2. Per-symbol external bit flag (bitmap in directory):
    flag_bitmap_cost = (total_funcs + 7) // 8
    # 3. Decoder code delta:
    # Needs a branch: if (is_external(s)) { return external_strings[ext_idx]; }
    decoder_code_delta = 36 # bytes of ARM Thumb-2 code
    # 4. Front-coding prefix disruption:
    # Removing a symbol from a front-coded block breaks LCP sharing for the next symbol (+1.0 B avg)
    prefix_disruption = K * 1.0
    
    mech_a_net_saving = gross_saved_metis - (ext_ref_cost + flag_bitmap_cost + decoder_code_delta + prefix_disruption)
    mech_a_pct = 100.0 * mech_a_net_saving / cfg["bin_size_blpr"]
    
    print(f"\n--- Mechanism A: External String Reference ---")
    print(f"  Gross compressed bytes removed: {gross_saved_metis:.1f} B")
    print(f"  External reference cost (2 B/entry): {ext_ref_cost} B")
    print(f"  Tag bitmap: {flag_bitmap_cost} B")
    print(f"  Decoder code delta: {decoder_code_delta} B")
    print(f"  Prefix disruption loss: {prefix_disruption:.1f} B")
    print(f"  Net saving: {mech_a_net_saving:.1f} B ({mech_a_pct:.3f}% of whole image)")
    print(f"  Passes 1.0% gate? {mech_a_pct >= 1.0}")
    
    # 5. Model Mechanism B: Shared Canonical Pool
    # In Mechanism B, external consumers and METIS share a single canonical pool.
    # Because standard C consumers (printk, struct device::name, shell_cmd) require
    # standard 'const char *' pointers, strings MUST be stored uncompressed in flash.
    # Therefore, external consumers already have their uncompressed string in flash.
    # If METIS points to those uncompressed strings, the net saving is bounded by Mechanism A.
    # If, conversely, external consumers migrated to a compressed pool, every C call site
    # in the kernel would require an adapter/unpacker and a RAM buffer, which violates
    # Zephyr's driver contract and adds hundreds of bytes of caller code.
    # Thus, Mechanism B net saving <= Mechanism A net saving.
    mech_b_net_saving = mech_a_net_saving
    mech_b_pct = mech_a_pct
    
    # 6. Strict Theoretical Upper Bound
    # Absolute theoretical bound: What if ALL matching strings completely vanished from the image with 0 overhead?
    # Upper bound = unique_matched_string_bytes
    abs_bound_bytes = unique_matched_string_bytes
    abs_bound_pct = 100.0 * abs_bound_bytes / cfg["bin_size_blpr"]
    print(f"\n--- Theoretical Upper Bound ---")
    print(f"  Absolute upper bound (all matched strings vanish with 0 overhead): {abs_bound_bytes} B")
    print(f"  Upper bound % of whole image: {abs_bound_pct:.3f}%")
    print(f"  Reaches 1.0%? {abs_bound_pct >= 1.0}")
    
    return {
        "workload": wl_name,
        "whole_image_bytes": cfg["bin_size_blpr"],
        "total_functions": total_funcs,
        "matched_functions": K,
        "match_percentage": round(match_pct, 2),
        "raw_duplicate_bytes": raw_duplicate_bytes,
        "unique_matched_string_bytes": unique_matched_string_bytes,
        "gross_saved_metis": round(gross_saved_metis, 1),
        "mech_a_ref_cost": ext_ref_cost,
        "mech_a_tag_cost": flag_bitmap_cost,
        "mech_a_code_delta": decoder_code_delta,
        "mech_a_prefix_loss": round(prefix_disruption, 1),
        "mech_a_net_saving_bytes": round(mech_a_net_saving, 1),
        "mech_a_pct_image": round(mech_a_pct, 3),
        "mech_a_pass": mech_a_pct >= 1.0,
        "mech_b_net_saving_bytes": round(mech_b_net_saving, 1),
        "mech_b_pct_image": round(mech_b_pct, 3),
        "mech_b_pass": mech_b_pct >= 1.0,
        "abs_upper_bound_bytes": abs_bound_bytes,
        "abs_upper_bound_pct": round(abs_bound_pct, 3),
        "abs_bound_pass": abs_bound_pct >= 1.0,
        "consumer_distribution": dict(consumer_counts),
        "match_inventory": match_inventory
    }

def main():
    results = []
    all_inventories = {}
    
    for wl_name, cfg in WORKLOADS.items():
        res = audit_workload(wl_name, cfg)
        results.append(res)
        all_inventories[wl_name] = res["match_inventory"]
        
    out_dir = f"{ROOT}/research/metis_shared_pool"
    os.makedirs(out_dir, exist_ok=True)
    
    # Export results JSON
    with open(f"{out_dir}/audit_results.json", "w") as f:
        json.dump(results, f, indent=2)
        
    # Export inventory JSON
    with open(f"{out_dir}/string_inventory.json", "w") as f:
        json.dump(all_inventories, f, indent=2)
        
    # Export summary CSV
    csv_rows = []
    for r in results:
        csv_rows.append({
            "workload": r["workload"],
            "whole_image_bytes": r["whole_image_bytes"],
            "total_functions": r["total_functions"],
            "matched_functions": r["matched_functions"],
            "match_pct": r["match_percentage"],
            "raw_duplicate_bytes": r["raw_duplicate_bytes"],
            "unique_matched_bytes": r["unique_matched_string_bytes"],
            "mech_a_net_bytes": r["mech_a_net_saving_bytes"],
            "mech_a_pct_image": r["mech_a_pct_image"],
            "mech_a_pass": r["mech_a_pass"],
            "mech_b_net_bytes": r["mech_b_net_saving_bytes"],
            "mech_b_pct_image": r["mech_b_pct_image"],
            "mech_b_pass": r["mech_b_pass"],
            "abs_upper_bound_bytes": r["abs_upper_bound_bytes"],
            "abs_upper_bound_pct": r["abs_upper_bound_pct"],
            "abs_bound_pass": r["abs_bound_pass"]
        })
        
    with open(f"{out_dir}/audit_summary.csv", "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(csv_rows[0].keys()))
        writer.writeheader()
        writer.writerows(csv_rows)
        
    # Export flat inventory CSV
    flat_inv_rows = []
    for wl_name, inv in all_inventories.items():
        for item in inv:
            flat_inv_rows.append({
                "workload": wl_name,
                "function_name": item["function_name"],
                "length_with_nul": item["length_with_nul"],
                "occurrences": item["occurrences"],
                "sections": ";".join(item["sections"]),
                "addresses": ";".join(item["addresses"])
            })
    with open(f"{out_dir}/string_inventory.csv", "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["workload", "function_name", "length_with_nul", "occurrences", "sections", "addresses"])
        writer.writeheader()
        writer.writerows(flat_inv_rows)
        
    print("\n" + "=" * 80)
    print("AUDIT SUMMARY TABLE")
    print("=" * 80)
    for r in results:
        print(f"{r['workload']:22s} | N={r['total_functions']:4d} | Matches={r['matched_functions']:2d} ({r['match_percentage']:4.1f}%) | "
              f"RawDup={r['raw_duplicate_bytes']:4d} B | NetSave={r['mech_a_net_saving_bytes']:+5.1f} B ({r['mech_a_pct_image']:+.3f}%) | "
              f"AbsBound={r['abs_upper_bound_pct']:.3f}% | Pass={r['mech_a_pass']}")

if __name__ == "__main__":
    main()
