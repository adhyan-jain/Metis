"""Exact symbol-binding byte accounting for Zephyr LLEXT (SLID design, as built) + offline cost model for dense-ID variants.
Run with /usr/bin/python (pyelftools). Reads real artifacts built by work/*_slid. Output: results/slid_cost.json"""
import json, os, sys, subprocess, itertools
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection
R = "/home/adhyan/Desktop/Compiler/research/metis_llext"; NM = "/home/adhyan/local-toolchain/bin/arm-none-eabi-nm"

def base_info(build):
    elf = f"{R}/work/{build}/zephyr/zephyr.elf"; e = ELFFile(open(elf, "rb"))
    sec = {s.name: s for s in e.iter_sections()}
    t = sec["llext_const_symbol_area"]; n = t["sh_size"] // 8
    bin_sz = os.path.getsize(f"{R}/work/{build}/zephyr/zephyr.bin")
    nm = subprocess.run([NM, "-S", "--size-sort", elf], capture_output=True, text=True).stdout
    code = {l.split()[3]: int(l.split()[1], 16) for l in nm.splitlines() if len(l.split()) == 4 and l.split()[3] in ("llext_find_sym", "llext_bsearch_sym", "llext_find_extension_sym")}
    return dict(elf=elf, exports=n, table_bytes=t["sh_size"], names_strtab_not_in_flash=sec["llext_exports_strtab"]["sh_size"], image_bin=bin_sz, loader_syms=code)

def ext_info(path):
    f = open(path, "rb"); e = ELFFile(f); st = e.get_section_by_name(".symtab"); strt = e.get_section_by_name(".strtab")
    syms = list(st.iter_symbols()); imp = {}; exp = {}
    for i, s in enumerate(syms):
        b = s.entry["st_info"]["bind"]; shn = s.entry["st_shndx"]
        if b == "STB_GLOBAL" and shn == "SHN_UNDEF" and s.name: imp[i] = s.name
        elif b == "STB_GLOBAL" and shn != "SHN_UNDEF": exp[i] = s.name
    # relocations that reference imports
    nrel_imp = 0; nrel_all = 0; relsz = 0
    for s in e.iter_sections():
        if s["sh_type"] in ("SHT_REL", "SHT_RELA"):
            ent = s["sh_entsize"]; nrel_all += s["sh_size"] // ent
            for r in s.iter_relocations():
                if r["r_info_sym"] in imp: nrel_imp += 1; relsz = ent
    es = e.get_section_by_name(".exported_sym"); n_exp_tab = es["sh_size"] // 8 if es else 0
    exp_rel = 0
    if es:
        for s in e.iter_sections():
            if s["sh_type"] in ("SHT_REL", "SHT_RELA") and s.name.startswith(".rel") and s.name.endswith(".exported_sym"):
                exp_rel = s["sh_size"]
    nb = lambda d: sum(len(v) + 1 for v in d.values())
    d = dict(file_bytes=os.path.getsize(path), imports=len(imp), import_names=sorted(imp.values()), import_name_bytes=nb(imp),
             import_symtab_bytes=16 * len(imp), import_reloc_entries=nrel_imp, import_reloc_bytes=nrel_imp * (relsz or 8),
             exports_global_syms=len(exp), export_name_bytes_strtab=nb(exp), export_symtab_bytes=16 * len(exp),
             exported_sym_entries=n_exp_tab, exported_sym_bytes=8 * n_exp_tab, exported_sym_reloc_bytes=exp_rel,
             all_reloc_entries=nrel_all)
    return d

def model(base, exts, label):
    n = base["exports"]; slid_base = base["table_bytes"]
    imp_names = sum(x["import_name_bytes"] for x in exts); imp_sym = sum(x["import_symtab_bytes"] for x in exts)
    imp_rel = sum(x["import_reloc_bytes"] for x in exts)
    exp_b = sum(x["export_name_bytes_strtab"] + x["export_symtab_bytes"] + x["exported_sym_bytes"] + x["exported_sym_reloc_bytes"] for x in exts)
    ext_bind = imp_names + imp_sym + imp_rel + exp_b           # all symbol-binding bytes shipped in extensions (SLID as built; SLID itself rides in st_value, 0 extra)
    D = slid_base + ext_bind                                     # DENOMINATOR (frozen definition)
    V = {}
    # V2: dense 32-bit word per import in st_value = 16-bit build fingerprint | 16-bit index ; base = n*4 addr array + 4 B fingerprint const
    V["V2_dense_fp_in_id"] = dict(base=4 * n + 4, ext=ext_bind, loader=0, note="+4B base const; loader: bsearch removed, index lookup added (assumed net 0)")
    # V1: separate 8B fingerprint per extension in a new ELF note section: header 40 + shstrtab name 12 + note hdr 12 + name 4 + desc 8 = ~76B per ext, base const 8
    V["V1_dense_fp_note"] = dict(base=4 * n + 8, ext=ext_bind + 76 * len(exts), loader=0, note="per-ext note section (~76B incl. section header)")
    # control S: SLID + strip import names from extension strtab (valid only if no ext->ext name lookups; breaks llext_find_extension_sym for those imports)
    V["S_slid_strip_import_names"] = dict(base=slid_base, ext=ext_bind - imp_names, loader=0, note="no ABI change; control")
    # control M: SLID-compatible MPH: addr 4B + 1B check + ~2.6 bits/key MPH seeds (analytic, NOT built)
    V["M_slid_compatible_MPH_analytic"] = dict(base=int(n * (4 + 1) + n * 2.6 / 8 + 8), ext=ext_bind, loader=0, note="analytic, no ABI change, extensions unchanged")
    out = dict(label=label, n_exports_base=n, n_ext=len(exts), slid_base_bytes=slid_base, ext_binding_bytes=ext_bind,
               ext_parts=dict(import_names=imp_names, import_symtab=imp_sym, import_relocs=imp_rel, exports=exp_b),
               denominator_D=D, ext_file_bytes=sum(x["file_bytes"] for x in exts), base_image_bin=base["image_bin"], variants={})
    for k, v in V.items():
        tot = v["base"] + v["ext"] + v["loader"]
        out["variants"][k] = dict(total=tot, saving_bytes=D - tot, saving_pct_of_D=round(100 * (D - tot) / D, 2),
                                  saving_pct_of_deliverable=round(100 * (D - tot) / (base["image_bin"] + out["ext_file_bytes"]), 3), note=v["note"])
    return out

def main():
    res = {}
    bm = base_info("mod_slid"); em = [ext_info(f"{R}/work/mod_slid/llext/hello_world.llext")]
    bb = base_info("bundle_slid")
    names = ["hello_world", "logging", "syscalls", "object", "relative_jump", "inspect", "threads_kernel_objects", "export_dependent", "export_dependency", "align", "movwmovt", "init_fini"]
    ex = {n: ext_info(f"{R}/work/bundle_slid/llext/{n}.llext") for n in names if os.path.exists(f"{R}/work/bundle_slid/llext/{n}.llext")}
    res["base_sample"] = bm; res["base_bundle"] = bb; res["ext_details"] = dict(sample_hello=em[0], **ex)
    res["models"] = [model(bm, em, "W1 sample/modules: 1 ext (toy) on sample base")]
    keys = list(ex)
    for lab, sel in [("W2 test base + 3 ext", keys[:3]), ("W3 test base + 6 ext", keys[:6]), ("W4 test base + all %d ext" % len(keys), keys),
                     ("W5 test base + 1 ext (hello_world)", keys[:1])]:
        res["models"].append(model(bb, [ex[k] for k in sel], lab))
    json.dump(res, open(f"{R}/results/slid_cost.json", "w"), indent=1)
    for m in res["models"]:
        print(m["label"], "| exports", m["n_exports_base"], "| SLID base", m["slid_base_bytes"], "| ext binding", m["ext_binding_bytes"], "| D", m["denominator_D"])
        for k, v in m["variants"].items(): print("    ", k, v["total"], "save", v["saving_bytes"], f'{v["saving_pct_of_D"]}% of D', f'{v["saving_pct_of_deliverable"]}% of deliverable')
main()
