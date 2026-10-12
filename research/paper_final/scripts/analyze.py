#!/usr/bin/env python3
"""Gate computation, tables and figures for the gap-fill (definitions: GAP_FILL_PROTOCOL.md sections 2 and 6).
Usage: analyze.py [results-subdir=workloads] [out-subdir=analysis]
Writes: results/<out>/gate_results.json, per_workload.csv, per_workload_table.tex, arms_table.tex, decomposition.pdf, ladder.pdf"""
import csv, glob, json, math, os, statistics, sys

PF = "/home/adhyan/Desktop/Compiler/research/paper_final"
EXIST = ["KALL256", "KALL16", "CAND_DIRECT", "CAND_CP16"]
HARD = ["B1_B16_TOK", "B1_B16_KALL", "B2_B16_TOK", "B2_B16_KALL"]
P, F = "B1_B16_FC16", "B1_B16_FSST"
ALL = ["STOCK"] + EXIST + HARD + ["RAW32_B16_TOK", "RAW32_B16_KALL", P, "B2_B16_FC16", F]


def load(sub):
    return [json.load(open(p)) for p in sorted(glob.glob(f"{PF}/results/{sub}/*.json"))]


def img(w, a):
    r = w["arms"].get(a, {})
    return r.get("bin") if r.get("converged") else None


def mism(w):
    tot, bad = 0, []
    for a, r in w["arms"].items():
        hc = r.get("host_check")
        if hc is None:
            continue
        if "error" in hc:
            tot += 10 ** 6; bad.append((a, "compile error")); continue
        for cap, v in hc.items():
            tot += v["mismatches"]
            if v["mismatches"]: bad.append((a, cap, v["mismatches"]))
        if r.get("py_ref_mismatches"):
            tot += r["py_ref_mismatches"]; bad.append((a, "py", r["py_ref_mismatches"]))
    return tot, bad


def per_workload(w):
    wl = w["workload"]; row = dict(id=wl["id"], board=wl["board"], arch=wl["arch"], toolchain=wl["toolchain"], N=w["N_vanilla"])
    row["all_converged"] = all(img(w, a) is not None for a in ALL)
    row["nosym"] = w["nosym_bin"]
    for a in ALL:
        row[a] = img(w, a)
    need = EXIST + HARD + [P, F]
    row["required_converged"] = all(img(w, a) is not None for a in need)
    if row["required_converged"]:
        E = min(img(w, a) for a in EXIST); H = min([E] + [img(w, a) for a in HARD]); Pw, Fw = img(w, P), img(w, F)
        row.update(E=E, E_arm=min(EXIST, key=lambda a: img(w, a)), H=H, H_arm=min(HARD, key=lambda a: img(w, a)),
                   S_total=E - Pw, S_addr=E - H, g_pct=100 * (H - Pw) / H,
                   g_vs_B1TOK_pct=100 * (img(w, "B1_B16_TOK") - Pw) / img(w, "B1_B16_TOK"),
                   share=(E - H) / (E - Pw) if E - Pw > 0 else None,
                   fsst_vs_P_pct=100 * (Fw - Pw) / Pw, S_total_pct=100 * (E - Pw) / E)
    row["stock_ok"] = bool(w["arms"].get("STOCK", {}).get("check_baseline_present"))
    row["stub_ok"] = all(w["arms"].get(a, {}).get("check_stub_only") for a in ALL[1:])
    row["N_final_P"] = w["arms"].get(P, {}).get("n_final")
    row["mismatches"], row["mismatch_detail"] = mism(w)
    row["max_iterations"] = max((r.get("iterations", 0) for r in w["arms"].values()), default=0)
    return row


def main():
    sub = sys.argv[1] if len(sys.argv) > 1 else "workloads"; out = sys.argv[2] if len(sys.argv) > 2 else "analysis"
    ws = load(sub); od = f"{PF}/results/{out}"; os.makedirs(od, exist_ok=True)
    rows = [per_workload(w) for w in ws]; n = len(rows)
    valid = [r for r in rows if r["required_converged"]]
    need75 = math.ceil(0.75 * n)
    g1 = sum(1 for r in rows if r.get("share") is not None and r["share"] >= 0.70)
    g2 = sum(1 for r in rows if r["required_converged"] and r["g_pct"] <= 1.0)
    big = [r for r in rows if r["required_converged"] and (r["N_final_P"] or 0) > 1000]
    g3_ok = all(abs(r["fsst_vs_P_pct"]) <= 1.0 for r in big) and len(big) > 0
    mism_total = sum(r["mismatches"] for r in rows)
    res = dict(n_workloads=n, n_valid=len(valid),
               G1=dict(count=g1, need=need75, passed=g1 >= need75),
               G2=dict(count=g2, need=need75, passed=g2 >= need75),
               G3=dict(workloads_N_gt_1000=len(big), within_1pct=sum(abs(r["fsst_vs_P_pct"]) <= 1.0 for r in big), passed=bool(g3_ok)),
               G4=dict(mismatches=mism_total, passed=mism_total == 0),
               G5=dict(all_converged=all(r["all_converged"] for r in rows), stock_present=all(r["stock_ok"] for r in rows),
                       stubs_ok=all(r["stub_ok"] for r in rows)))
    res["G5"]["passed"] = all(res["G5"].values())
    gt1 = sum(1 for r in rows if r["required_converged"] and r["g_pct"] > 1.0)
    st = [r for r in valid if r["S_total"] > 0]
    pooled = (sum(r["S_addr"] for r in st) / sum(r["S_total"] for r in st)) if st else None
    shares = [r["share"] for r in rows if r.get("share") is not None]
    res["T1"] = dict(count_g_gt_1pct=gt1, fraction=gt1 / n, triggered=gt1 / n >= 0.5)
    res["T2"] = dict(pooled_share=pooled, triggered=(pooled is not None and pooled < 0.5))
    res["T3"] = dict(not_converged=[r["id"] for r in rows if not r["required_converged"]], triggered=any(not r["required_converged"] for r in rows))
    res["sensitivity"] = dict(share_median=statistics.median(shares) if shares else None,
                              share_geomean=math.exp(sum(math.log(max(s, 1e-9)) for s in shares) / len(shares)) if shares else None,
                              g_median_pct=statistics.median(r["g_pct"] for r in valid) if valid else None,
                              g_vs_B1TOK_median_pct=statistics.median(r["g_vs_B1TOK_pct"] for r in valid) if valid else None,
                              S_total_pct_median=statistics.median(r["S_total_pct"] for r in valid) if valid else None)
    passed = all(res[k]["passed"] for k in ("G1", "G2", "G3", "G4", "G5"))
    term = res["T1"]["triggered"] or res["T2"]["triggered"] or res["T3"]["triggered"]
    res["decision"] = "TERMINATE" if term else ("PASS" if passed else "INCONCLUSIVE")
    json.dump(dict(gates=res, rows=rows), open(f"{od}/gate_results.json", "w"), indent=1, default=str)
    cols = ["id", "board", "arch", "N", "N_final_P", "nosym"] + ALL + ["E", "E_arm", "H", "H_arm", "S_total", "S_addr", "share", "g_pct", "fsst_vs_P_pct", "mismatches", "max_iterations"]
    with open(f"{od}/per_workload.csv", "w", newline="") as f:
        wr = csv.writer(f); wr.writerow(cols)
        for r in rows: wr.writerow([r.get(c) for c in cols])

    def tex(s): return str(s).replace("_", r"\_")
    with open(f"{od}/per_workload_table.tex", "w") as f:
        f.write("\\begin{tabular}{llrrrrrrrr}\n\\toprule\nworkload & arch & $N$ & $E$ & $H$ & $P$ & $S_\\mathrm{tot}$ & $S_\\mathrm{addr}$ & share & $g$ (\\%) \\\\\n\\midrule\n")
        for r in rows:
            if r["required_converged"]:
                sh = "--" if r["share"] is None else f"{100*r['share']:.0f}\\%"
                f.write(f"{tex(r['id'])} & {r['arch'].replace('cortex-m3','M3').replace('rv32imac','RV32')} & {r['N']} & {r['E']} & {r['H']} & {r[P]} & {r['S_total']} & {r['S_addr']} & {sh} & {r['g_pct']:.2f} \\\\\n")
        f.write("\\bottomrule\n\\end{tabular}\n")
    with open(f"{od}/arms_table.tex", "w") as f:
        f.write("\\begin{tabular}{l" + "r" * len(rows) + "}\n\\toprule\narm & " + " & ".join(tex(r["id"].replace("net_", "").replace("_mps2", "").replace("_rv32", "'"))[:14] for r in rows) + " \\\\\n\\midrule\n")
        f.write("NOSYM & " + " & ".join(str(r["nosym"]) for r in rows) + " \\\\\n")
        for a in ALL:
            f.write(f"{tex(a)} & " + " & ".join(str(r[a]) if r[a] is not None else "--" for r in rows) + " \\\\\n")
        f.write("\\bottomrule\n\\end{tabular}\n")
    try:
        import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
        vr = list(valid)
        if vr:
            fig, ax = plt.subplots(figsize=(7, 3.2)); x = range(len(vr))
            ax.bar(x, [100 * r["S_addr"] / r["E"] for r in vr], label="address index (E to H)")
            ax.bar(x, [r["g_pct"] * r["H"] / r["E"] for r in vr], bottom=[100 * r["S_addr"] / r["E"] for r in vr], label="name coding (H to P)")
            ax.set_xticks(list(x)); ax.set_xticklabels([f"{r['id'].replace('net_','').replace('_mps2','').replace('_rv32',' (rv)')}\nN={r['N']}" for r in vr], rotation=60, ha="right", fontsize=6)
            ax.set_ylabel("saving vs best existing (% of image)"); ax.legend(fontsize=7); fig.tight_layout(); fig.savefig(f"{od}/decomposition.pdf")
            fig, ax = plt.subplots(figsize=(7, 3.2))
            for a, lab in [("B1_B16_TOK", "B1+TOK (hardened)"), (P, "B1+FC16 (proposed)"), (F, "B1+FSST")]:
                pts = sorted((r["N"], 100 * (r["E"] - r[a]) / r["E"]) for r in vr)
                ax.plot([p[0] for p in pts], [p[1] for p in pts], "o-", label=lab)
            ax.set_xlabel("symbols N"); ax.set_ylabel("saving vs best existing (% of image)"); ax.legend(fontsize=7); fig.tight_layout(); fig.savefig(f"{od}/ladder.pdf")
    except Exception as ex:
        print("figure error", ex)
    print(json.dumps(res, indent=1, default=str))


if __name__ == "__main__":
    main()
