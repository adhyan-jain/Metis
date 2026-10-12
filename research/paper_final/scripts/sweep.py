#!/usr/bin/env python3
"""Feasibility sweep (pre-freeze): which existing Zephyr samples build with CONFIG_SYMTAB=y on each target board with the
Zephyr SDK GCC, and what is their symbol count N?  Records build success, N and flat image size ONLY.
No encoder/decoder or arm-vs-arm size comparison is computed here, so this step cannot be influenced by the gate outcome.

Usage: sweep.py <board> [<board> ...]   (results appended to results/sweep_results.jsonl; logs in logs/sweep/)"""
import json, os, subprocess, sys, time, concurrent.futures as cf, shutil

ROOT = "/home/adhyan/Desktop/Compiler"; PF = f"{ROOT}/research/paper_final"; ZB = f"{ROOT}/corpora/zephyr"
sys.path.insert(0, f"{PF}/scripts")
import manifest as MF

ENV = dict(os.environ, ZEPHYR_SDK_INSTALL_DIR=os.path.expanduser("~/zephyr-sdk-1.0.1"), ZEPHYR_TOOLCHAIN_VARIANT="zephyr",
           ZEPHYR_BASE=ZB, PATH=os.path.expanduser("~/zephyr-sdk-1.0.1/hosttools_inst/sysroots/x86_64-pokysdk-linux/usr/bin") + ":" + os.environ["PATH"])
OUT = f"{PF}/results/sweep_results.jsonl"


def samples():
    r = []
    for d, _, fs in os.walk(f"{ZB}/samples"):
        if "CMakeLists.txt" in fs:
            r.append(os.path.relpath(d, ZB))
    r = sorted(r)
    if os.environ.get("SAMPLES_FILE"):
        keep = set(json.load(open(os.environ["SAMPLES_FILE"])))
        r = [x for x in r if x in keep]
    return r


def build(args):
    sample, board = args
    tag = sample.replace("samples/", "").replace("/", "__") + "@" + board.replace("/", "_")
    bd = f"{PF}/work/sweep/{tag}"; lg = f"{PF}/logs/sweep/{tag}.log"
    os.makedirs(os.path.dirname(lg), exist_ok=True); t0 = time.time()
    cmd = ["west", "build", "-b", board, "-d", bd, f"{ZB}/{sample}", "--", "-DCONFIG_SYMTAB=y"]
    try:
        p = subprocess.run(cmd, env=ENV, capture_output=True, text=True, timeout=900, cwd=ZB)
        open(lg, "w").write(p.stdout + "\n--- STDERR ---\n" + p.stderr)
        rc = p.returncode
    except subprocess.TimeoutExpired:
        rc = -9; open(lg, "w").write("TIMEOUT")
    rec = dict(sample=sample, board=board, tag=tag, rc=rc, seconds=round(time.time() - t0, 1))
    elf = f"{bd}/zephyr/zephyr.elf"
    if rc == 0 and os.path.exists(elf):
        try:
            d = MF.load_elf(elf)
            rec.update(N=len(d["names"]), bin=os.path.getsize(f"{bd}/zephyr/zephyr.bin"), ok=True)
        except Exception as e:
            rec.update(ok=False, err=str(e))
    else:
        rec.update(ok=False)
        shutil.rmtree(bd, ignore_errors=True)       # only deletes this script's own failed build dir
    with open(OUT, "a") as f:
        f.write(json.dumps(rec) + "\n")
    return rec


if __name__ == "__main__":
    boards = sys.argv[1:]
    jobs = [(s, b) for b in boards for s in samples()]
    with cf.ThreadPoolExecutor(3) as ex:
        for r in ex.map(build, jobs):
            print(r["tag"], r["rc"], r.get("N"), r["seconds"], flush=True)
