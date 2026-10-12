#!/usr/bin/env python3
"""Optional execution validation (outside the gates). For a workload and a list of arms, build a SEPARATE validation image that
also contains a SYS_INIT self-test: it calls the arm's real lookup_symbol() on target for a fixed address list (every symbol start,
start+1 and midpoint, plus out-of-range probes), hashes (name, offset) with FNV-1a and prints the hash; the host computes the expected
hash from the manifest oracle. The image is relinked to a fixed point with the self-test included, boots in QEMU and the console
output is captured.  What this proves: the decoder, compiled for the target, returns the oracle's answers for those addresses when
executed in QEMU. It says nothing about timing or about size of the (larger) validation image.
Usage: gapfill_exec.py <workload-id> [arm,arm,...]"""
import json, os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(__file__))
import gapfill_run as R
from gapfill_run import *      # noqa

QEMU = {"cortex-m3": ["qemu-system-arm", "-cpu", "cortex-m3", "-machine", "mps2-an385", "-nographic", "-monitor", "none"],
        "rv32imac": ["qemu-system-riscv32", "-machine", "virt", "-bios", "none", "-nographic", "-m", "256", "-monitor", "none"]}


def fnv(name, off):
    h = 0x811C9DC5
    for b in name.encode() + off.to_bytes(4, "little"):
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def selftest_src(d, arm):
    f = d["first"]; addrs = []
    for i, o in enumerate(d["offs"]):
        nxt = d["offs"][i + 1] if i + 1 < len(d["offs"]) else d["end"]
        addrs += [f + 2 * o, f + 2 * o + 1, f + 2 * ((o + nxt) // 2)]
    addrs += [f - 1, f + 2 * d["end"], f + 2 * d["end"] + 100]
    h = 0x811C9DC5
    for a in addrs:
        o = A.T.oracle(d, a)
        nm, off = o if o else ("?", 0)
        hh = fnv(nm, off)
        h = ((h ^ hh) * 0x01000193) & 0xFFFFFFFF
    arr = ",".join(f"0x{a & 0xFFFFFFFF:x}u" for a in addrs)
    return f"""#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <stdint.h>
#include <stddef.h>
int lookup_symbol(uintptr_t, char *, size_t, uint32_t *);
static const uint32_t st_addrs[{len(addrs)}] = {{{arr}}};
static uint32_t st_fnv(uint32_t h, const char *s, uint32_t off) {{
	for (; *s; s++) h = (h ^ (uint8_t)*s) * 0x01000193u;
	for (int i = 0; i < 4; i++) h = (h ^ ((off >> (8 * i)) & 0xff)) * 0x01000193u;
	return h;
}}
static int selftest_init(void) {{
	char buf[256]; uint32_t agg = 0x811C9DC5u;
	for (unsigned i = 0; i < {len(addrs)}u; i++) {{
		uint32_t off = 0; lookup_symbol(st_addrs[i], buf, sizeof buf, &off);
		if (buf[0] == '?' && buf[1] == 0) off = 0;
		agg = (agg ^ st_fnv(0x811C9DC5u, buf, off)) * 0x01000193u;
	}}
	printk("SELFTEST arm={arm} n={len(addrs)} hash=%08x expect={h:08x} %s\\n", agg, agg == 0x{h:08x}u ? "PASS" : "FAIL");
	return 0;
}}
SYS_INIT(selftest_init, APPLICATION, 99);
""", h


def run_exec(wl, arm, lk, d0, odir):
    os.makedirs(odir, exist_ok=True); d = d0; prev = None
    for it in range(1, MAXIT + 1):
        made = A.make(arm, d)
        cp, op = f"{odir}/a{it}.c", f"{odir}/a{it}.o"; open(cp, "w").write(made["c"]); lk.compile(cp, op)
        sc, h = selftest_src(d, arm); sp, so = f"{odir}/s{it}.c", f"{odir}/s{it}.o"; open(sp, "w").write(sc); lk.compile(sp, so)
        elf = f"{odir}/v{it}.elf"; lk.do_link([op, so], elf, f"{odir}/v{it}.map")
        dn = MF.load_elf(elf)
        if R.same(dn, d):
            break
        d = dn
    else:
        return dict(arm=arm, error="no fixed point")
    log = f"{odir}/qemu.log"
    if os.path.exists(log): os.remove(log)
    cmd = QEMU[wl["arch"]] + ["-serial", f"file:{log}", "-kernel", elf]
    try:
        subprocess.run(cmd, env=dict(os.environ, PATH=R.HOST + ":" + os.environ["PATH"]), timeout=25, capture_output=True)
    except subprocess.TimeoutExpired:
        pass
    out = open(log, errors="replace").read() if os.path.exists(log) else ""
    line = next((l for l in out.splitlines() if l.startswith("SELFTEST")), None)
    return dict(arm=arm, iterations=it, selftest_line=line, passed=bool(line and line.rstrip().endswith("PASS")),
                boot_banner=next((l for l in out.splitlines() if "Booting Zephyr" in l), None), qemu_cmd=" ".join(cmd), console_tail=out[-600:])


def main():
    wid = sys.argv[1]; arms_ = sys.argv[2].split(",") if len(sys.argv) > 2 else ["B1_B16_FC16", "B1_B16_FSST", "B1_B16_TOK", "KALL256"]
    wls = {w["id"]: w for w in json.load(open(f"{PF}/results/workloads_frozen.json"))["workloads"]}
    wl = wls[wid]; env = tc_env(wl["toolchain"])
    sbd, selfpath = west_build(wl, "stock", env)
    lk = Linker(sbd, env); d0 = MF.load_elf(selfpath, wid)
    out = dict(workload=wid, results=[])
    for a in arms_:
        r = run_exec(wl, a, lk, d0, f"{PF}/work/exec/{wid}/{a}")
        print(wid, a, r.get("passed"), r.get("selftest_line"), flush=True); out["results"].append(r)
    os.makedirs(f"{PF}/results/exec", exist_ok=True)
    json.dump(out, open(f"{PF}/results/exec/{wid}.json", "w"), indent=1)


if __name__ == "__main__":
    main()
