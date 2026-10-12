#!/usr/bin/env python3
"""Stage 2: Build real firmware images for Candidates A (BLPR) and B (ALOB).
Compiles with arm-none-eabi-clang, links using identical Zephyr link harness,
measures flat .bin sizes, section breakdowns, linker maps, and worst-case work.
"""
import json, os, sys, math
sys.path.insert(0, "/home/adhyan/Desktop/Compiler/research/metis_arch")
sys.path.insert(0, "/home/adhyan/Desktop/Compiler/research/metis_final_tournament")

import link_image as L
import offline_screen_ab as AB
import gen_c_blpr, gen_c_alob

ROOT = "/home/adhyan/Desktop/Compiler"
WORK = f"{ROOT}/research/metis_final_tournament/work"
OUT = f"{ROOT}/research/metis_final_tournament/results"
os.makedirs(WORK, exist_ok=True)
os.makedirs(OUT, exist_ok=True)
os.makedirs(f"{WORK}/img", exist_ok=True)

def sections(sz_stdout):
    r = {}
    for line in sz_stdout.splitlines():
        p = line.split()
        if len(p) >= 2 and p[0].startswith(".") and p[1].isdigit():
            r[p[0]] = int(p[1])
    return r

def obj_split(path):
    out = L.sh(f"/usr/bin/llvm-size -A {path}").stdout
    s = sections(out)
    code = sum(v for k, v in s.items() if k.startswith(".text"))
    data = sum(v for k, v in s.items() if k.startswith(".rodata") or k.startswith(".data") or k.startswith(".bss"))
    return code, data, s

def worst_work_blpr(d, F):
    n = F["n"]
    B = F["B"]
    rbits = F["rbits"]
    nblk = F["nblk"]
    max_asteps = math.ceil(math.log2(nblk)) + (B - 1)
    max_nrecs = 0
    for idx in range(n):
        bit = idx * rbits
        chunk = int.from_bytes(F["rank_data"][bit >> 3 : (bit >> 3) + 4], "little")
        r = (chunk >> (bit & 7)) & ((1 << rbits) - 1)
        if r > max_nrecs: max_nrecs = r
    return dict(max_addr_steps=max_asteps, max_name_recs=max_nrecs)

def worst_work_alob(d, F):
    n = F["n"]
    B = F["B"]
    rbits = F["rbits"]
    nblk = F["nblk"]
    max_asteps = math.ceil(math.log2(nblk)) + (B - 1)
    max_nrecs = 0
    for idx in range(n):
        b = idx // B
        kk = idx % B
        bd = F["block_decisions"][b]
        if bd["mode"] == 0:
            r = kk
        else:
            bit = kk * rbits
            # in rank map
            r = bd["rank_map"][kk]
        if r > max_nrecs: max_nrecs = r
    return dict(max_addr_steps=max_asteps, max_name_recs=max_nrecs)

def run():
    print("=" * 80)
    print("STAGE 2: FIRMWARE BUILD & LINK TOURNAMENT")
    print("=" * 80)
    
    # Load baselines
    base_data = json.load(open(f"{ROOT}/research/metis_arch/results/baselines_relinked.json"))
    norm_data = json.load(open(f"{ROOT}/research/metis_arch/results/normalized_results.json"))
    norm_idx = {(r["workload"], r["tag"]): r for r in norm_data}
    
    all_results = []
    
    for wl in AB.FILES:
        d = AB.load(wl)
        bd, cflags = L.WL[wl]
        no_symtab_bin = base_data[wl]["A_no_symtab"]
        stock_bin = base_data[wl]["B_stock_zephyr"]
        
        # Determine best existing baseline for wl
        EX = ["kallsyms_m256", "kallsyms_m16", "cand_direct", "cand_cp16"]
        best_exist_tag = min(EX, key=lambda m: base_data[wl][m]["whole_image_bin"])
        best_exist_bin = base_data[wl][best_exist_tag]["whole_image_bin"]
        best_exist_feat = best_exist_bin - no_symtab_bin
        
        # Hardened baseline: min of best existing and B1/B2 variants with TOK or KALL
        hardened_candidates = [best_exist_bin]
        for t in ("B1_B16_TOK", "B1_B16_KALL", "B2_B16_TOK", "B2_B16_KALL"):
            if (wl, t) in norm_idx:
                hardened_candidates.append(norm_idx[(wl, t)]["whole_image"])
        hardened_baseline_bin = min(hardened_candidates)
        
        print(f"\n>>> Workload: {wl}")
        print(f"  No-symtab: {no_symtab_bin} B | Stock: {stock_bin} B")
        print(f"  Best existing: {best_exist_tag} = {best_exist_bin} B (feature={best_exist_feat} B)")
        print(f"  Hardened baseline: {hardened_baseline_bin} B")
        print(f"  B1_B16_FC16: {norm_idx[(wl, 'B1_B16_FC16')]['whole_image']} B (feature={norm_idx[(wl, 'B1_B16_FC16')]['feature']} B)")
        print(f"  B1_B16_TOK : {norm_idx[(wl, 'B1_B16_TOK')]['whole_image']} B (feature={norm_idx[(wl, 'B1_B16_TOK')]['feature']} B)")
        
        configs = []
        for B in (8, 16, 32):
            configs.append(("BLPR", B))
        for B in (8, 16, 32):
            configs.append(("ALOB", B))
            
        for cand_type, B in configs:
            tag = f"{cand_type}_B{B}"
            cf = f"{WORK}/{wl}_{tag}.c"
            of = f"{WORK}/{wl}_{tag}.o"
            
            if cand_type == "BLPR":
                F = AB.build_blpr(d, B)
                code_str = gen_c_blpr.gen_blpr(F)
                ww = worst_work_blpr(d, F)
            else:
                F = AB.build_alob(d, B)
                code_str = gen_c_alob.gen_alob(F)
                ww = worst_work_alob(d, F)
                
            with open(cf, "w") as f: f.write(code_str)
            L.compile_obj(cf, of, cflags)
            code_bytes, data_bytes, obj_sec = obj_split(of)
            
            bin_sz, elf, mp, sz = L.link(wl, of, tag, f"{WORK}/img")
            elf_sec = sections(sz)
            
            feature_sz = bin_sz - no_symtab_bin
            
            # Static RAM delta vs best existing
            # ELF data + bss
            ram = sum(v for k, v in elf_sec.items() if k in (".data", ".bss", ".noinit"))
            base_ram = sum(v for k, v in base_data[wl][best_exist_tag]["sections"].items() if k in (".data", ".bss", ".noinit"))
            ram_delta = ram - base_ram
            
            # Metrics vs best existing
            imp_bytes_exist = best_exist_bin - bin_sz
            imp_pct_exist = 100.0 * imp_bytes_exist / best_exist_bin
            feat_red_pct_exist = 100.0 * (best_exist_feat - feature_sz) / best_exist_feat
            
            # Metrics vs B1_B16_FC16
            fc16_bin = norm_idx[(wl, "B1_B16_FC16")]["whole_image"]
            fc16_feat = norm_idx[(wl, "B1_B16_FC16")]["feature"]
            diff_fc16_bin = fc16_bin - bin_sz
            diff_fc16_pct = 100.0 * diff_fc16_bin / fc16_bin
            
            # Metrics vs hardened baseline
            imp_bytes_hard = hardened_baseline_bin - bin_sz
            imp_pct_hard = 100.0 * imp_bytes_hard / hardened_baseline_bin
            
            res_entry = dict(
                workload=wl,
                tag=tag,
                candidate=cand_type,
                B=B,
                payload=F["breakdown"]["payload"],
                decoder_code=code_bytes,
                obj_data=data_bytes,
                whole_image=bin_sz,
                feature=feature_sz,
                best_exist_tag=best_exist_tag,
                best_exist_bin=best_exist_bin,
                imp_bytes_exist=imp_bytes_exist,
                imp_pct_exist=round(imp_pct_exist, 3),
                feat_red_pct_exist=round(feat_red_pct_exist, 2),
                diff_fc16_bin=diff_fc16_bin,
                diff_fc16_pct=round(diff_fc16_pct, 3),
                hardened_baseline_bin=hardened_baseline_bin,
                imp_bytes_hard=imp_bytes_hard,
                imp_pct_hard=round(imp_pct_hard, 3),
                ram_delta=ram_delta,
                max_addr_steps=ww["max_addr_steps"],
                max_name_recs=ww["max_name_recs"],
                elf=elf,
                map=mp,
                source=cf,
                breakdown=F["breakdown"]
            )
            all_results.append(res_entry)
            
            print(f"  {tag:10s} | payload={F['breakdown']['payload']:5d} B | code={code_bytes:3d} B | bin={bin_sz:6d} B | feat={feature_sz:5d} B | vs best exist: {imp_bytes_exist:+5d} B ({imp_pct_exist:+.2f}%) | feat red: {feat_red_pct_exist:.1f}% | vs FC16: {diff_fc16_bin:+5d} B ({diff_fc16_pct:+.2f}%) | vs hard: {imp_bytes_hard:+5d} B ({imp_pct_hard:+.2f}%) | RAM d={ram_delta}")
            
    with open(f"{OUT}/tournament_firmware_results.json", "w") as f:
        json.dump(all_results, f, indent=2)
    return all_results

if __name__ == "__main__":
    run()
