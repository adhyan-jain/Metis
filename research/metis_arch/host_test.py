"""Compile the generated C for the host and run it against the manifest oracle (incl. truncation, bounds, interior addresses)."""
import subprocess, sys, os, tempfile
sys.path.insert(0, "research/metis_arch")
import metis_arch as M, gen_c, test_roundtrip as T

DRV = r'''
#include <stdio.h>
#include <stdlib.h>
int lookup_symbol(uintptr_t, char *, size_t, uint32_t *);
int main(int argc, char **argv) { unsigned long a; unsigned cap = atoi(argv[1]); char buf[256];
  while (scanf("%lu", &a) == 1) { uint32_t off = 0xdeadbeef; int rc = lookup_symbol((uintptr_t)a, buf, cap, &off); printf("%d|%s|%u\n", rc, buf, off); } return 0; }
'''

def run(F, d, tmp, cap, qs):
    src = os.path.join(tmp, "g.c"); open(src, "w").write(gen_c.gen(F))
    open(os.path.join(tmp, "d.c"), "w").write("#include <stdint.h>\n#include <stddef.h>\n" + DRV)
    exe = os.path.join(tmp, "t")
    r = subprocess.run(["gcc", "-O1", "-Wall", "-Wno-unused-function", src, os.path.join(tmp, "d.c"), "-o", exe], capture_output=True, text=True)
    if r.returncode: print(r.stderr[:2000]); return None
    out = subprocess.run([exe, str(cap)], input="\n".join(map(str, qs)), capture_output=True, text=True).stdout.splitlines()
    return out

def expect(d, a, cap):
    o = T.oracle(d, a)
    if o is None: return ("1|?|0" if cap >= 2 else "1||0")
    nm, off = o
    if len(nm) + 1 <= cap: return f"0|{nm}|{off}"
    return f"2|{nm[:cap - 1]}|{off}"

def main(cfgs, wls=None):
    bad_total = 0
    for wl in (wls or M.FILES):
        d = M.load(wl); qs = T.queries(d)
        for kind, B, nk in cfgs:
            F = M.build(d, kind, B, nk)
            with tempfile.TemporaryDirectory() as tmp:
                for cap in (64, 12):
                    out = run(F, d, tmp, cap, qs)
                    if out is None: bad_total += 1; break
                    bad = sum(1 for a, o in zip(qs, out) if o != expect(d, a, cap))
                    bad_total += bad
                    print(wl, kind, B, nk, "cap", cap, "queries", len(qs), "C-mismatches", bad)
    print("TOTAL C MISMATCHES", bad_total); return bad_total

if __name__ == "__main__":
    sys.exit(1 if main([("RAW32", 16, "TOK"), ("A", 8, "TOK"), ("A", 16, "TOK"), ("B1", 8, "TOK"), ("B1", 16, "TOK"), ("B2", 8, "TOK"),
                        ("B2", 16, "TOK"), ("D", 16, "TOK"), ("A", 16, "FC"), ("B1", 16, "FC"), ("B2", 8, "FC")]) else 0)
