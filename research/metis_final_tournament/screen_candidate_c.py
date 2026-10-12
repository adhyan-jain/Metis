#!/usr/bin/env python3
"""Candidate C offline screening:
Exact overlap, fallback analysis, base table cost, loader overhead,
and symbol-binding / deliverable savings across all LLEXT bundles.
"""
import os, sys, json, subprocess
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection

ROOT = "/home/adhyan/Desktop/Compiler"
LLEXT_DIR = f"{ROOT}/research/metis_llext"
NM = "/home/adhyan/local-toolchain/bin/arm-none-eabi-nm"

def inspect_base(build_name):
    elf_path = f"{LLEXT_DIR}/work/{build_name}/zephyr/zephyr.elf"
    bin_path = f"{LLEXT_DIR}/work/{build_name}/zephyr/zephyr.bin"
    with open(elf_path, "rb") as f:
        elf = ELFFile(f)
        sec = {s.name: s for s in elf.iter_sections()}
        sym_area = sec["llext_const_symbol_area"]
        strtab = sec["llext_exports_strtab"]
        
        # Read exports
        sym_data = sym_area.data()
        n_exports = len(sym_data) // 8
        str_data = strtab.data()
        
        # Extract symbols from ELF symtab to know types (FUNC vs OBJECT)
        symtab = elf.get_section_by_name(".symtab")
        all_syms = {}
        if symtab:
            for s in symtab.iter_symbols():
                if s.name:
                    all_syms[s.name] = (s.entry["st_value"], s.entry["st_info"]["type"])
        
        # Extract nm function addresses
        # In Thumb, function addresses have bit 0 set.
        # Let's inspect exports
        # struct llext_const_symbol { uintptr_t slid; const void *addr; }
        exports = []
        name_pos = 0
        for i in range(n_exports):
            slid = int.from_bytes(sym_data[i*8 : i*8 + 4], "little")
            addr = int.from_bytes(sym_data[i*8 + 4 : i*8 + 8], "little")
            # find name in str_data at name_pos
            end_pos = str_data.find(b"\0", name_pos)
            name = str_data[name_pos:end_pos].decode("ascii", errors="replace") if end_pos != -1 else ""
            name_pos = end_pos + 1
            st_type = all_syms.get(name, (addr, "UNKNOWN"))[1]
            exports.append(dict(slid=slid, addr=addr, name=name, type=st_type))
            
    # Load function symbol manifests from base image
    # We can inspect which addresses are in the .text / functions
    # Let's get all function addresses from nm
    nm_out = subprocess.run([NM, "-n", elf_path], capture_output=True, text=True).stdout
    funcs = {}
    for line in nm_out.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            v = int(parts[0], 16)
            t = parts[1]
            nm = parts[2]
            if t in ("T", "t", "W", "w"):
                funcs[v] = nm
                funcs[v | 1] = nm # thumb
    
    # Check overlap of exports against functions
    func_matches = 0
    fallbacks = 0
    fallback_reasons = {"not_func": 0, "not_in_func_table": 0}
    fallback_list = []
    
    for exp in exports:
        addr = exp["addr"]
        # In thumb mode, function pointers have bit 0 set
        is_func = (exp["type"] == "STT_FUNC") or (addr & 1) or (addr in funcs)
        if is_func and ((addr in funcs) or ((addr | 1) in funcs)):
            func_matches += 1
        else:
            fallbacks += 1
            if exp["type"] == "STT_OBJECT":
                fallback_reasons["not_func"] += 1
            else:
                fallback_reasons["not_in_func_table"] += 1
            fallback_list.append(exp)
            
    return dict(
        build=build_name,
        image_bin=os.path.getsize(bin_path),
        n_exports=n_exports,
        func_matches=func_matches,
        fallbacks=fallbacks,
        fallback_reasons=fallback_reasons,
        fallback_samples=[f["name"] for f in fallback_list[:10]],
        exports=exports
    )

def ext_info(path):
    with open(path, "rb") as f:
        e = ELFFile(f)
        st = e.get_section_by_name(".symtab")
        syms = list(st.iter_symbols())
        imp, exp = {}, {}
        for i, s in enumerate(syms):
            b = s.entry["st_info"]["bind"]
            shn = s.entry["st_shndx"]
            if b == "STB_GLOBAL" and shn == "SHN_UNDEF" and s.name:
                imp[i] = s.name
            elif b == "STB_GLOBAL" and shn != "SHN_UNDEF":
                exp[i] = s.name
        nrel_imp = 0
        relsz = 8
        for s in e.iter_sections():
            if s["sh_type"] in ("SHT_REL", "SHT_RELA"):
                ent = s["sh_entsize"]
                for r in s.iter_relocations():
                    if r["r_info_sym"] in imp:
                        nrel_imp += 1
                        relsz = ent
        es = e.get_section_by_name(".exported_sym")
        n_exp_tab = es["sh_size"] // 8 if es else 0
        exp_rel = 0
        if es:
            for s in e.iter_sections():
                if s["sh_type"] in ("SHT_REL", "SHT_RELA") and s.name.startswith(".rel") and s.name.endswith(".exported_sym"):
                    exp_rel = s["sh_size"]
        nb = lambda d: sum(len(v) + 1 for v in d.values())
        return dict(
            file_bytes=os.path.getsize(path),
            imports=len(imp),
            import_name_bytes=nb(imp),
            import_symtab_bytes=16 * len(imp),
            import_reloc_bytes=nrel_imp * relsz,
            exports=len(exp),
            export_bytes=nb(exp) + 16 * len(exp) + 8 * n_exp_tab + exp_rel
        )

def run():
    base_mod = inspect_base("mod_slid")
    base_bundle = inspect_base("bundle_slid")
    
    print("=== Base Overlap and Fallbacks ===")
    print(f"mod_slid: {base_mod['n_exports']} exports, {base_mod['func_matches']} func matches, {base_mod['fallbacks']} fallbacks")
    print(f"  Fallback breakdown: {base_mod['fallback_reasons']}")
    print(f"  Fallback samples: {base_mod['fallback_samples']}")
    print(f"bundle_slid: {base_bundle['n_exports']} exports, {base_bundle['func_matches']} func matches, {base_bundle['fallbacks']} fallbacks")
    print(f"  Fallback breakdown: {base_bundle['fallback_reasons']}")
    print(f"  Fallback samples: {base_bundle['fallback_samples']}")
    
    # Load extensions
    em = [ext_info(f"{LLEXT_DIR}/work/mod_slid/llext/hello_world.llext")]
    names = ["hello_world", "logging", "syscalls", "object", "relative_jump", "inspect",
             "threads_kernel_objects", "export_dependent", "export_dependency", "align", "movwmovt", "init_fini"]
    ex = {n: ext_info(f"{LLEXT_DIR}/work/bundle_slid/llext/{n}.llext") for n in names if os.path.exists(f"{LLEXT_DIR}/work/bundle_slid/llext/{n}.llext")}
    
    bundles = [
        ("W1 sample/modules: 1 ext", base_mod, em),
        ("W5 test base + 1 ext (hello_world)", base_bundle, [ex["hello_world"]]),
        ("W2 test base + 3 ext", base_bundle, [ex[n] for n in names[:3]]),
        ("W3 test base + 6 ext", base_bundle, [ex[n] for n in names[:6]]),
        ("W4 test base + all 12 ext", base_bundle, list(ex.values())),
    ]
    
    results = []
    print("\n=== Candidate C Cost Analysis ===")
    for label, base, exts in bundles:
        N = base["n_exports"]
        F = base["fallbacks"]
        slid_base = N * 8
        ext_bind = sum(x["import_name_bytes"] + x["import_symtab_bytes"] + x["import_reloc_bytes"] + x["export_bytes"] for x in exts)
        D = slid_base + ext_bind
        
        # Candidate C base export table:
        # slid_table: N * 4 bytes
        # target_ref: N * 2 bytes (aligned)
        # fallback_table: F * 4 bytes
        # table total = 4N + 2N + 4F = 6N + 4F
        # Alignment padding: if 2N is odd multiple of 4, add 2 bytes padding before fallback_table:
        pad = 2 if (2 * N) % 4 != 0 else 0
        cand_c_base_table = 6 * N + pad + 4 * F
        
        # Loader code delta:
        # In stock Zephyr, llext_bsearch_sym is 134 B.
        # Candidate C requires binary search over slid array + ref unpacking + fallback or METIS address decode.
        # Address decode from METIS B1 block:
        #   b = rank / 16, kk = rank % 16, binary search or table lookup for block base, delta loop.
        # Realistic loader code delta: +60 bytes.
        loader_delta = 60
        
        cand_c_base_total = cand_c_base_table + loader_delta
        cand_c_ext_total = ext_bind # extensions unchanged (same SLID contract)
        cand_c_total = cand_c_base_total + cand_c_ext_total
        
        saving_bytes = D - cand_c_total
        saving_pct_D = 100.0 * saving_bytes / D
        
        total_deliverable = base["image_bin"] + sum(x["file_bytes"] for x in exts)
        deliverable_saving_pct = 100.0 * saving_bytes / total_deliverable
        
        print(f"\n{label}:")
        print(f"  N={N}, F={F}, D={D} B (SLID base={slid_base} B, ext bind={ext_bind} B)")
        print(f"  Candidate C table: 6N + pad + 4F = {cand_c_base_table} B (base table saving = {slid_base - cand_c_base_table} B)")
        print(f"  Candidate C with loader (+{loader_delta} B): total binding = {cand_c_total} B")
        print(f"  Net saving on D: {saving_bytes} B ({saving_pct_D:.2f}%)")
        print(f"  Net saving on deliverable: {saving_bytes} B ({deliverable_saving_pct:.3f}%)")
        
        results.append(dict(
            bundle=label, N=N, F=F, D=D,
            slid_base=slid_base, ext_bind=ext_bind,
            cand_c_base_table=cand_c_base_table,
            loader_delta=loader_delta,
            saving_bytes=saving_bytes,
            saving_pct_D=round(saving_pct_D, 2),
            deliverable_saving_pct=round(deliverable_saving_pct, 3)
        ))
        
    with open(f"{ROOT}/research/metis_final_tournament/candidate_c_screen.json", "w") as out_f:
        json.dump(results, out_f, indent=2)

if __name__ == "__main__":
    run()
