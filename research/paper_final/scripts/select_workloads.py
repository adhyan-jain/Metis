#!/usr/bin/env python3
"""Deterministic workload selection (frozen rule, see GAP_FILL_PROTOCOL.md section 3). Inputs: feasibility sweep only
(build success + symbol count N). No size/encoding result is used.
ARM  (mps2/an385, Cortex-M3):  pilot samples excluded; 6 samples at N-rank quantiles k/5, k=0..5 of the successfully built set.
RV32 (qemu_riscv32):           hello_world + 3 samples at N-rank quantiles {0,1/2,1} of the RISC-V-built set after excluding
                               pilot samples, hello_world and the six ARM picks (so >= 9 distinct non-pilot samples overall)."""
import glob, json, os, sys
sys.path.insert(0, os.path.dirname(__file__))
import manifest as MF
PF = "/home/adhyan/Desktop/Compiler/research/paper_final"
PILOT = {"samples/hello_world", "samples/subsys/shell/shell_module", "samples/net/sockets/echo_server"}
R = [json.loads(l) for l in open(f"{PF}/results/sweep_results.jsonl")]
arm = sorted({(r["N"], r["sample"]) for r in R if r["board"] == "mps2/an385" and r["ok"] and r["sample"] not in PILOT})
pick = [arm[round(k * (len(arm) - 1) / 5)] for k in range(6)]
armsamples = {s for _, s in pick}
rv = []
for d in glob.glob(f"{PF}/work/sweep/*@qemu_riscv32"):
    e = d + "/zephyr/zephyr.elf"
    if os.path.exists(e):
        tag = os.path.basename(d).split("@")[0]
        s = "samples/" + tag.replace("__", "/")
        try: rv.append((len(MF.load_elf(e)["names"]), s))
        except Exception: pass
rv = sorted(rv)
rvc = [x for x in rv if x[1] not in PILOT and x[1] != "samples/hello_world" and x[1] not in armsamples]
rvpick = [rvc[0], rvc[len(rvc) // 2], rvc[-1]]
W = []
def wid(s, b): return s.replace("samples/", "").replace("/", "_") + "_" + ("mps2" if "mps2" in b else "rv32")
for n, s in pick: W.append(dict(id=wid(s, "mps2"), sample=s, board="mps2/an385", toolchain="gcc", arch="cortex-m3", N_sweep=n))
for n, s in [x for x in rv if x[1] == "samples/hello_world"] + rvpick: W.append(dict(id=wid(s, "rv32"), sample=s, board="qemu_riscv32", toolchain="gcc", arch="rv32imac", N_sweep=n))
json.dump(dict(rule=__doc__, workloads=W), open(f"{PF}/results/workloads_frozen.json", "w"), indent=1)
for w in W: print(w["id"], w["board"], w["N_sweep"])
