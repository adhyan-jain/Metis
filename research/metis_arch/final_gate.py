import json, math
R = "research/metis_arch/results"
rows = json.load(open(f"{R}/normalized_results.json")); base = json.load(open(f"{R}/baselines_relinked.json"))
WLS = list(base); idx = {(r["workload"], r["tag"]): r for r in rows}
EX = ["kallsyms_m256", "kallsyms_m16", "cand_direct", "cand_cp16"]
def bx(wl): return min(base[wl][m]["whole_image_bin"] for m in EX)
def gate(tags, baseline, name):
    out = dict(candidate=name, per_workload={})
    imps, feats = [], []
    for wl in WLS:
        r = idx[(wl, tags[wl])]; b = baseline(wl); nos = base[wl]["A_no_symtab"]
        imp = 100 * (b - r["whole_image"]) / b; fr = 100 * ((b - nos) - r["feature"]) / (b - nos)
        imps.append(imp); feats.append(fr)
        out["per_workload"][wl] = dict(config=tags[wl], whole_image=r["whole_image"], baseline_image=b, imp_bytes=b - r["whole_image"], imp_pct=round(imp, 3),
                                       feature_bytes=r["feature"], baseline_feature=b - nos, feat_red_pct=round(fr, 2), max_name_recs=r["max_name_recs"],
                                       max_addr_steps=r["max_addr_steps"], mismatches=r["mismatches"], ram_delta=r["ram_delta_vs_best"])
    gm = 100 * (math.exp(sum(math.log(1 + i / 100) for i in imps) / 4) - 1)
    c = {"4_beats_3of4_incl_echo": sum(i > 0 for i in imps) >= 3 and imps[WLS.index("echo_server_mps2")] > 0,
         "4_geomean>=0.5": gm >= 0.5, "5_feature>=5%_on_3of4": sum(f >= 5 for f in feats) >= 3, "6_no_regress>0.25%": min(imps) >= -0.25,
         "1_zero_mismatches": all(out["per_workload"][w]["mismatches"] == 0 for w in WLS), "2_zero_static_ram": all(out["per_workload"][w]["ram_delta"] == 0 for w in WLS),
         "3_recs<=B-1": all(out["per_workload"][w]["max_name_recs"] <= 15 for w in WLS)}
    out.update(geomean_imp_pct=round(gm, 3), criteria=c, all_numeric_criteria_pass=all(c.values()))
    return out
fixed = lambda t: {w: t for w in WLS}
def auto(w):   # build-time cost procedure: min exact payload over the FC/TOK configs (decoder size is not part of the generator's model)
    cs = [r for r in rows if r["workload"] == w and r["family"] != "RAW32" and r["names"] in ("FC", "TOK")]
    return min(cs, key=lambda r: r["payload"])["tag"]
res = []
exist = lambda wl: bx(wl)
# stronger "address-hardened" baseline: best of existing and the conventional name schemes given the same compact address index
hard = lambda wl: min(bx(wl), *[idx[(wl, t)]["whole_image"] for t in ("B1_B16_TOK", "B1_B16_KALL", "B2_B16_TOK", "B2_B16_KALL")])
for name, tags in [("B1_B16_FC16", fixed("B1_B16_FC16")), ("B2_B16_FC16", fixed("B2_B16_FC16")), ("B2_B8_FC16", fixed("B2_B8_FC16")),
                   ("A_B8_FC16 (family A + FC)", fixed("A_B8_FC16")), ("D_B16_FC16", fixed("D_B16_FC16")), ("B1_B16_TOK (names unchanged-style)", fixed("B1_B16_TOK")),
                   ("auto-by-payload", {w: auto(w) for w in WLS}), ("control RAW32_B16_TOK", fixed("RAW32_B16_TOK"))]:
    res.append(dict(vs="pre-declared best existing", **gate(tags, exist, name)))
for name, tags in [("B1_B16_FC16", fixed("B1_B16_FC16")), ("B2_B16_FC16", fixed("B2_B16_FC16")), ("auto-by-payload", {w: auto(w) for w in WLS})]:
    res.append(dict(vs="address-hardened baseline (best of existing and B1/B2 + TOK/Kallsyms-names)", **gate(tags, hard, name)))
json.dump(res, open(f"{R}/final_gate.json", "w"), indent=1)
for r in res:
    print(f"{r['vs'][:22]:22s} {r['candidate']:34s} gm={r['geomean_imp_pct']:6.2f}% pass={r['all_numeric_criteria_pass']} ", " ".join(f"{w[:5]}:{p['imp_pct']:+.2f}/{p['feat_red_pct']:.0f}" for w, p in r["per_workload"].items()), [k for k, v in r["criteria"].items() if not v])
print("\nhardened baselines:", {w: (hard(w), min(EX, key=lambda m: base[w][m]["whole_image_bin"])) for w in WLS})
print("auto picks:", {w: auto(w) for w in WLS})
# ablation ladder
print("\nLADDER (whole-image bytes)")
for wl in WLS:
    g = lambda t: idx[(wl, t)]["whole_image"]
    print(wl, "best_existing", bx(wl), "| RAW32+KALL", g("RAW32_B16_KALL"), "| RAW32+TOK", g("RAW32_B16_TOK"), "| B1+KALL", g("B1_B16_KALL"), "| B1+TOK", g("B1_B16_TOK"), "| B1+FC16", g("B1_B16_FC16"), "| B2+FC16", g("B2_B16_FC16"))
# predicted-vs-actual
print("\nPREDICTED (payload+obj code) vs ACTUAL feature (image - no-symtab image)")
for t in ("B1_B16_FC16", "B2_B16_FC16", "D_B16_FC16"):
    for wl in WLS:
        r = idx[(wl, t)]; pred = r["payload"] + r["decoder_code"]; print(t, wl[:14], "pred", pred, "actual", r["feature"], "diff", r["feature"] - pred)
