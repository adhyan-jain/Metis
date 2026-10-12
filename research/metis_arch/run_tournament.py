"""Stage 1+2 tournament: encode, verify (Python reference, exhaustive worst-case work), generate C, compile for ARM, link whole image.
Usage: run_tournament.py [workload ...]   Outputs: results/tournament_<wl>.json"""
import json, os, sys, subprocess, re
sys.path.insert(0, os.path.dirname(__file__))
import metis_arch as M, gen_c, link_image as L, test_roundtrip as T

OUT = "research/metis_arch/results"; WORK = "research/metis_arch/work"

def cfgs():
    if os.environ.get('KALL'):
        return [('RAW32', 16, 'KALL', 16)] + [(k, B, 'KALL', 16) for B in (8, 16) for k in ('A', 'B1', 'B2')]
    c = [("RAW32", 16, "TOK", 16)]                               # control: raw u32 offsets + same token names
    for B in (8, 16):
        for kind in ("A", "B1", "B2"):
            c.append((kind, B, "TOK", 16))
    c.append(("D", 16, "TOK", 16))
    for Bn in (8, 16):
        for B in (8, 16):
            for kind in ("A", "B1", "B2"): c.append((kind, B, "FC", Bn))
        c.append(("D", 16, "FC", Bn))
    return c

def sections(sz):
    r = {}
    for line in sz.splitlines():
        p = line.split()
        if len(p) >= 2 and p[0].startswith(".") and p[1].isdigit(): r[p[0]] = int(p[1])
    return r

def obj_split(path):
    out = L.sh(f"/usr/bin/llvm-size -A {path}").stdout; s = sections(out)
    code = sum(v for k, v in s.items() if k.startswith(".text")); data = sum(v for k, v in s.items() if k.startswith(".rodata") or k.startswith(".data") or k.startswith(".bss"))
    return code, data, s

def worst_work(d, F):
    mx = dict(asteps=0, nrec=0); bytes_scanned = 0
    f = d["first"]
    for o in d["offs"]:
        st = {}; M.lookup(F, f + 2 * o, st)
        for k in mx: mx[k] = max(mx[k], st.get(k, 0))
    return mx

def stack_of(cfile, cflags):
    r = L.sh(f"{L.CLANG} {cflags} -fstack-usage -c {cfile} -o /dev/null")
    su = cfile.replace(".c", ".su")
    return None

def run(wl):
    d = M.load(wl); bd, cflags = L.WL[wl]; res = []
    for kind, B, nk, Bn in cfgs():
        F = M.build(d, kind, B, nk, Bn=Bn)
        tag = f"{kind}_B{B}_{nk}" + (f"{Bn}" if nk == "FC" else "")
        bad, mx = T.check(d, F)
        cf = f"{WORK}/{wl}_{tag}.c"; of = f"{WORK}/{wl}_{tag}.o"
        open(cf, "w").write(gen_c.gen(F)); L.compile_obj(cf, of, cflags)
        code, data, secs = obj_split(of)
        bin_sz, elf, mp, sz = L.link(wl, of, tag, f"{WORK}/img")
        r = dict(workload=wl, tag=tag, family=kind, B=B, names=nk, Bn=Bn if nk == "FC" else None, tokens=F["T"], ref_mismatches=bad,
                 worst_work=mx, bytes=M.breakdown(F), obj_code_bytes=code, obj_data_bytes=data, obj_sections=secs,
                 whole_image_bin=bin_sz, elf=elf, map=mp, tok_stack_bytes=F["sd"], source=cf,
                 sections_elf=sections(sz))
        res.append(r)
        print(wl, tag, "payload", r["bytes"]["payload"], "code", code, "bin", bin_sz, "mism", bad, flush=True)
    json.dump(res, open(f"{OUT}/tournament_{'kall_' if os.environ.get('KALL') else ''}{wl}.json", "w"), indent=1)

if __name__ == "__main__":
    for wl in (sys.argv[1:] or M.FILES): run(wl)
