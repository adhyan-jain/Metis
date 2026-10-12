#!/usr/bin/env python3
"""Gap-fill driver: for one workload (sample x board x toolchain) build the stock + no-symtab references with west, then for
every arm run a FIXED-POINT relink and record exact image accounting and decoder checks.

Usage:  gapfill_run.py <workload-id> [--arms A,B] [--out-suffix X]
Workload definitions come from results/workloads_frozen.json (frozen before the first measurement).

Method (see GAP_FILL_PROTOCOL.md, section 6):
  * the final image is relinked with the build's OWN recorded link command (ninja -t commands), with symtab.c.obj replaced by
    the arm's object; a consumer `zephyr_fault_symbol_logger` (forced with -Wl,-u) keeps the lookup path reachable in every arm;
  * iteration k encodes the symbol manifest of ELF(k-1) into the arm's table, links ELF(k) and re-extracts its manifest;
    converged <=> manifest(ELF(k)) == manifest(ELF(k-1)) exactly (names, addresses, end). Cap: 8 iterations.
  * image size = objcopy flat binary, recipe identical to Zephyr's zephyr.bin step.
"""
import hashlib, json, os, shlex, shutil, subprocess, sys, tempfile, time

ROOT = "/home/adhyan/Desktop/Compiler"; PF = f"{ROOT}/research/paper_final"; ZB = f"{ROOT}/corpora/zephyr"
sys.path.insert(0, f"{PF}/scripts")
os.chdir(ROOT)                                    # historical generators expect the repo root as cwd
import manifest as MF, arms as A
from elftools.elf.elffile import ELFFile
from elftools.elf.descriptions import describe_symbol_type

SDK = os.path.expanduser("~/zephyr-sdk-1.0.1")
HOST = f"{SDK}/hosttools_inst/sysroots/x86_64-pokysdk-linux/usr/bin"
MAXIT = 8


def tc_env(tc):
    e = dict(os.environ, ZEPHYR_BASE=ZB, PATH=HOST + ":" + os.environ["PATH"])
    if tc == "gcc":
        e.update(ZEPHYR_SDK_INSTALL_DIR=SDK, ZEPHYR_TOOLCHAIN_VARIANT="zephyr")
    elif tc == "clang":
        e.update(ZEPHYR_TOOLCHAIN_VARIANT="cross-compile", CROSS_COMPILE="/home/adhyan/local-toolchain/bin/arm-none-eabi-")
    else:
        raise ValueError(tc)
    return e


def sh(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def west_build(wl, kind, env):
    bd = f"{PF}/work/builds/{wl['id']}/{kind}"
    elf = f"{bd}/zephyr/zephyr.elf"
    lg = f"{PF}/logs/builds/{wl['id']}_{kind}.log"
    os.makedirs(os.path.dirname(lg), exist_ok=True)
    if not os.path.exists(elf):
        flag = "-DCONFIG_SYMTAB=y" if kind == "stock" else "-DCONFIG_SYMTAB=n"
        p = sh(["west", "build", "-b", wl["board"], "-d", bd, f"{ZB}/{wl['sample']}", "--", flag] + wl.get("extra_cmake", []), env=env, cwd=ZB)
        open(lg, "w").write(p.stdout + "\n--- STDERR ---\n" + p.stderr)
        if p.returncode:
            raise RuntimeError(f"west build {kind} failed, see {lg}")
    return bd, elf


def split_amp(tokens):
    parts = [[]]
    for x in tokens:
        if x == "&&":
            parts.append([])
        else:
            parts[-1].append(x)
    return parts


def ninja_cmds(bd, target, env):
    out = sh(["ninja", "-t", "commands", target], cwd=bd, env=env).stdout.strip().splitlines()
    return out[-1]


class Linker:
    def __init__(self, bd, env):
        self.bd, self.env = bd, env
        parts = split_amp(shlex.split(ninja_cmds(bd, "zephyr/zephyr.elf", env)))
        self.link = next(p for p in parts if "zephyr/CMakeFiles/zephyr_final.dir/symtab.c.obj" in p and "-o" in p)
        comp = split_amp(shlex.split(ninja_cmds(bd, "zephyr/CMakeFiles/zephyr_final.dir/symtab.c.obj", env)))[0]
        self.cc = self._strip_compile(comp)
        gcc = self.link[0]
        base = os.path.basename(gcc)
        assert base.endswith("gcc"), base
        self.objcopy = os.path.join(os.path.dirname(gcc), base[:-3] + "objcopy")

    @staticmethod
    def _strip_compile(tok):
        out, skip = [], 0
        for i, x in enumerate(tok):
            if skip:
                skip -= 1; continue
            if x in ("-MD",):
                continue
            if x in ("-MT", "-MF", "-o", "-c"):
                skip = 1; continue
            out.append(x)
        return out

    def compile(self, src, obj):
        p = sh(self.cc + ["-c", src, "-o", obj], cwd=self.bd, env=self.env)
        if p.returncode:
            raise RuntimeError("compile failed: " + p.stderr[:2000])

    def do_link(self, objs, elf, mp):
        t = []
        i = 0
        L = self.link
        while i < len(L):
            x = L[i]
            if x == "zephyr/CMakeFiles/zephyr_final.dir/symtab.c.obj":
                t += objs
            elif x == "-o":
                t += ["-o", elf]; i += 1
            elif x.startswith("-Wl,-Map,"):
                t.append(f"-Wl,-Map,{mp}")
            else:
                t.append(x)
            i += 1
        t.append("-Wl,-u,zephyr_fault_symbol_logger")
        p = sh(t, cwd=self.bd, env=self.env)
        if p.returncode:
            raise RuntimeError("link failed: " + p.stderr[-2500:])
        return p.stderr

    def flat_bin(self, elf, out):
        p = sh([self.objcopy, "--gap-fill", "0xFF", "--output-target=binary", "--remove-section=.comment", "--remove-section=COMMON", elf, out])
        if p.returncode:
            raise RuntimeError(p.stderr)
        return os.path.getsize(out)


def same(a, b):
    return a["names"] == b["names"] and a["addrs"] == b["addrs"] and a["end"] == b["end"]


def elf_facts(elf):
    """allocated-section sizes + selected symbols (used for explicit baseline / stub inclusion checks)"""
    with open(elf, "rb") as f:
        e = ELFFile(f)
        secs = {}
        for s in e.iter_sections():
            if s["sh_flags"] & 2 and s["sh_size"]:
                secs[s.name] = dict(size=s["sh_size"], nobits=s["sh_type"] == "SHT_NOBITS", addr=s["sh_addr"])
        syms = {}
        for s in e.get_section_by_name(".symtab").iter_symbols():
            n = s.name
            if n in ("z_symtab", "z_symtab_entries", "symtab_find_symbol_name", "lookup_symbol", "zephyr_fault_symbol_logger", "symtab_get"):
                syms[n] = dict(size=s["st_size"], addr=s["st_value"], type=describe_symbol_type(s["st_info"]["type"]))
    flash = sum(v["size"] for v in secs.values() if not v["nobits"])
    return dict(alloc_progbits_bytes=flash, sections=secs, symbols=syms)


def obj_bytes(obj):
    with open(obj, "rb") as f:
        e = ELFFile(f)
        tot = dict(text=0, ro=0, data=0, bss=0)
        for s in e.iter_sections():
            if not s["sh_flags"] & 2:
                continue
            n = s.name
            if s["sh_type"] == "SHT_NOBITS":
                tot["bss"] += s["sh_size"]
            elif n.startswith(".text"):
                tot["text"] += s["sh_size"]
            elif n.startswith(".rodata") or "symtab" in n:
                tot["ro"] += s["sh_size"]
            else:
                tot["data"] += s["sh_size"]
        return tot


def read_stock_table(elf, d):
    """explicitly read z_symtab_entries out of the linked ELF and compare with the manifest (proves the stock table is present)"""
    with open(elf, "rb") as f:
        e = ELFFile(f)
        ptr = e.elfclass // 8
        sym = next(s for s in e.get_section_by_name(".symtab").iter_symbols() if s.name == "z_symtab_entries")
        base, size = sym["st_value"], sym["st_size"]

        def mem(addr, n):
            for s in e.iter_sections():
                if s["sh_flags"] & 2 and s["sh_type"] != "SHT_NOBITS" and s["sh_addr"] <= addr and addr + n <= s["sh_addr"] + s["sh_size"]:
                    o = addr - s["sh_addr"]; return s.data()[o:o + n]
            raise KeyError(hex(addr))

        esz = 2 * ptr
        n = size // esz
        res = []
        raw = mem(base, size)
        for i in range(n):
            off = int.from_bytes(raw[i * esz:i * esz + 4], "little")
            nameptr = int.from_bytes(raw[i * esz + ptr:(i + 1) * esz], "little")
            nm = bytearray()
            k = 0
            while True:
                c = mem(nameptr + k, 1)[0]
                if c == 0: break
                nm.append(c); k += 1
            res.append((off, nm.decode()))
    ok = (n == len(d["names"]) + 1 and
          all(res[i][0] == d["addrs"][i] - d["first"] and res[i][1] == d["names"][i] for i in range(len(d["names"]))) and res[-1][1] == "?")
    return dict(entries=n, entry_bytes=size, matches_manifest=bool(ok))


def run_arm(arm, wl, lk, d0, odir, nosym_bin, env):
    os.makedirs(odir, exist_ok=True)
    rec = dict(arm=arm, family=A.family(arm), hist=[])
    d = d0
    converged = False
    prev_elf = None
    for it in range(1, MAXIT + 1):
        t0 = time.time()
        if arm == "STOCK":
            cpath, opath = f"{odir}/symtab_it{it}.c", f"{odir}/symtab_it{it}.o"
            src_elf = prev_elf or wl["_stock_elf"]
            p = sh([sys.executable, f"{ZB}/scripts/build/gen_symtab.py", "-k", src_elf, "-o", cpath])
            if p.returncode:
                raise RuntimeError(p.stderr)
            lk.compile(cpath, opath)
            ccons, ocons = f"{odir}/consumer.c", f"{odir}/consumer.o"
            if not os.path.exists(ccons):
                shutil.copy(f"{PF}/scripts/stock_consumer.c", ccons)
            lk.compile(ccons, ocons)
            objs = [opath, ocons]
            made = dict(c=open(cpath).read(), F=None, payload=None, meta={})
        else:
            made = A.make(arm, d)
            cpath, opath = f"{odir}/arm_it{it}.c", f"{odir}/arm_it{it}.o"
            open(cpath, "w").write(made["c"])
            lk.compile(cpath, opath)
            objs = [opath]
        elf, mp = f"{odir}/arm_it{it}.elf", f"{odir}/arm_it{it}.map"
        lk.do_link(objs, elf, mp)
        dn = MF.load_elf(elf)
        binsz = lk.flat_bin(elf, f"{odir}/arm_it{it}.bin")
        os.remove(f"{odir}/arm_it{it}.bin")
        ok = same(dn, d) if arm != "STOCK" else (prev_elf is not None and same(dn, MF.load_elf(prev_elf)))
        rec["hist"].append(dict(it=it, n=len(dn["names"]), manifest_equal_to_input=bool(ok), bin=binsz, seconds=round(time.time() - t0, 1)))
        prev_elf = elf
        if ok:
            converged = True
            break
        d = dn
        # remove intermediates (keep only the last iteration's artefacts)
        if it > 1:
            for ext in ("c", "o", "elf", "map"):
                for pre in ("arm_it", "symtab_it"):
                    pth = f"{odir}/{pre}{it - 1}.{ext}"
                    if os.path.exists(pth): os.remove(pth)
    rec["converged"] = converged; rec["iterations"] = len(rec["hist"])
    rec["n_final"] = len(dn["names"])
    rec["bin"] = binsz; rec["feature_cost"] = binsz - nosym_bin
    # preserve evidence: debug-stripped ELF + map + C source + link command
    final_elf = elf
    ev = f"{odir}/final"
    os.makedirs(ev, exist_ok=True)
    sh([lk.objcopy, "--strip-debug", final_elf, f"{ev}/{arm}.elf"])
    shutil.copy(mp, f"{ev}/{arm}.map")
    if arm == "STOCK":
        shutil.copy(cpath, f"{ev}/{arm}_symtab.c")
    else:
        shutil.copy(cpath, f"{ev}/{arm}.c")
    rec["c_sha256"] = hashlib.sha256(open(cpath, "rb").read()).hexdigest()
    rec["elf_sha256"] = hashlib.sha256(open(f"{ev}/{arm}.elf", "rb").read()).hexdigest()
    rec["facts"] = elf_facts(final_elf)
    rec["obj"] = [obj_bytes(o) for o in objs]
    rec["mechanism_obj_bytes"] = sum(sum(x.values()) - x["bss"] for x in rec["obj"])
    # explicit baseline-inclusion / stub checks
    sy = rec["facts"]["symbols"]
    if arm == "STOCK":
        rec["stock_table"] = read_stock_table(final_elf, dn)
        rec["check_baseline_present"] = bool(rec["stock_table"]["matches_manifest"] and "symtab_find_symbol_name" in sy)
    else:
        rec["check_stub_only"] = sy.get("z_symtab_entries", {}).get("size", 0) <= 8 and "symtab_find_symbol_name" not in sy and "lookup_symbol" in sy
        rec["payload"] = made["payload"]; rec["meta"] = made["meta"]
        with tempfile.TemporaryDirectory() as tmp:
            rec["host_check"] = A.host_check(made, dn, tmp)
        if made["F"] is not None and arm not in A.FSSTARMS:
            bad, mx = A.T.check(dn, made["F"])
            rec["py_ref_mismatches"] = bad; rec["worst_work"] = mx
        else:
            rec["py_ref_mismatches"] = None
    rec["manifest_n"] = len(dn["names"])
    return rec, dn


def main():
    wid = sys.argv[1]
    sel = None; suffix = ""
    if "--arms" in sys.argv:
        sel = sys.argv[sys.argv.index("--arms") + 1].split(",")
    if "--out-suffix" in sys.argv:
        suffix = sys.argv[sys.argv.index("--out-suffix") + 1]
    wls = {w["id"]: w for w in json.load(open(os.environ.get("WORKLOADS_FILE", f"{PF}/results/workloads_frozen.json")))["workloads"]}
    wl = wls[wid]
    env = tc_env(wl["toolchain"])
    sbd, selfpath = west_build(wl, "stock", env)
    nbd, nelf = west_build(wl, "nosym", env)
    wl["_stock_elf"] = selfpath
    lk = Linker(sbd, env)
    d0 = MF.load_elf(selfpath, wid)
    nosym_bin = lk.flat_bin(nelf, f"{PF}/work/builds/{wid}/nosym.bin")
    stock_bin = lk.flat_bin(selfpath, f"{PF}/work/builds/{wid}/stock.bin")
    out = dict(workload=wl, N_vanilla=len(d0["names"]), nosym_bin=nosym_bin, vanilla_stock_bin=stock_bin, arms={}, started=time.strftime("%Y-%m-%d %H:%M:%S"))
    out["tool_versions"] = dict(cc=sh([lk.link[0], "--version"]).stdout.splitlines()[0], python=sys.version.split()[0])
    for arm in (sel or A.ALL_ARMS):
        t0 = time.time()
        try:
            rec, dn = run_arm(arm, wl, lk, d0, f"{PF}/work/gapfill{suffix}/{wid}/{arm}", nosym_bin, env)
            rec["seconds"] = round(time.time() - t0, 1)
        except Exception as ex:
            rec = dict(arm=arm, error=str(ex)[:3000], converged=False)
        out["arms"][arm] = rec
        print(wid, arm, rec.get("bin"), "conv" if rec.get("converged") else "NOCONV/ERR", rec.get("iterations"), (rec.get("error") or "")[:120], flush=True)
        os.makedirs(f"{PF}/results/workloads{suffix}", exist_ok=True)
        json.dump(out, open(f"{PF}/results/workloads{suffix}/{wid}.json", "w"), indent=1)
    out["finished"] = time.strftime("%Y-%m-%d %H:%M:%S")
    json.dump(out, open(f"{PF}/results/workloads{suffix}/{wid}.json", "w"), indent=1)


if __name__ == "__main__":
    main()
