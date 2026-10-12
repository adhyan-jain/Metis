import json, math, csv, os
R = "research/metis_arch/results"
base = json.load(open(f"{R}/baselines_relinked.json"))
tour = {wl: json.load(open(f"{R}/tournament_{wl}.json")) + json.load(open(f"{R}/tournament_kall_{wl}.json")) for wl in base}
EXIST = ["kallsyms_m256", "kallsyms_m16", "cand_direct", "cand_cp16"]
def ram(secs): return sum(v for k, v in secs.items() if k in (".data", ".bss", ".noinit", "datas") or k.endswith("bss") or k.endswith("data"))
rows = []; best_ex = {}
for wl, b in base.items():
    nosym = b["A_no_symtab"]
    be = min(EXIST, key=lambda m: b[m]["whole_image_bin"]); best_ex[wl] = be
    print(f"{wl}: best existing = {be} {b[be]['whole_image_bin']}  feature={b[be]['whole_image_bin']-nosym}  (stock {b['B_stock_zephyr']}, nosym {nosym}; ex: " + ", ".join(f"{m}={b[m]['whole_image_bin']}" for m in EXIST) + ")")
    bx = b[be]["whole_image_bin"]
    for r in tour[wl]:
        img = r["whole_image_bin"]
        rows.append(dict(workload=wl, tag=r["tag"], family=r["family"], B=r["B"], names=r["names"], Bn=r["Bn"], tokens=r["tokens"],
                         payload=r["bytes"]["payload"], addr_index=r["bytes"]["address_index"], name_ckpt=r["bytes"]["name_checkpoints"],
                         dictionary=r["bytes"]["dictionary"], name_pool=r["bytes"]["name_pool"], ids=r["bytes"]["ids"],
                         decoder_code=r["obj_code_bytes"], whole_image=img, feature=img - nosym,
                         best_existing=be, best_existing_image=bx, imp_bytes=bx - img, imp_pct=100.0 * (bx - img) / bx,
                         feat_red_pct=100.0 * ((bx - nosym) - (img - nosym)) / (bx - nosym),
                         max_name_recs=r["worst_work"]["nrec"], max_addr_steps=r["worst_work"]["asteps"], mismatches=r["ref_mismatches"],
                         ram_delta_vs_best=ram(r["sections_elf"]) - ram(b[be]["sections"])))
json.dump(rows, open(f"{R}/normalized_results.json", "w"), indent=1)
with open(f"{R}/normalized_results.csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
# per-config cross-workload table
tags = sorted({r["tag"] for r in rows}); wls = list(base)
print("\n%-16s" % "config" + "".join(f"{w[:12]:>14}" for w in wls) + "   geomean%  min%   feat>=5% wins")
sumry = []
for t in tags:
    rs = {r["workload"]: r for r in rows if r["tag"] == t}
    imps = [rs[w]["imp_pct"] for w in wls]
    gm = 100 * (math.exp(sum(math.log(1 + i / 100) for i in imps) / 4) - 1)
    fw = sum(rs[w]["feat_red_pct"] >= 5 for w in wls)
    wins = sum(i > 0 for i in imps)
    sumry.append(dict(tag=t, geomean_imp_pct=gm, min_imp_pct=min(imps), feat_ge5_workloads=fw, whole_image_wins=wins, echo_imp_pct=rs["echo_server_mps2"]["imp_pct"]))
    print("%-16s" % t + "".join(f"{rs[w]['imp_pct']:>13.2f}%" for w in wls) + f"   {gm:6.2f}  {min(imps):6.2f}   {fw}   wins={wins}")
json.dump(sumry, open(f"{R}/config_summary.json", "w"), indent=1)
