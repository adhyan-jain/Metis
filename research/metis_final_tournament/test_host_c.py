#!/usr/bin/env python3
"""Host C verification for Candidate A (BLPR) and Candidate B (ALOB).
Verifies 0 mismatches against oracle across all symbols, offsets, and truncations.
"""
import subprocess, sys, os, tempfile
sys.path.insert(0, "/home/adhyan/Desktop/Compiler/research/metis_arch")
sys.path.insert(0, "/home/adhyan/Desktop/Compiler/research/metis_final_tournament")
import metis_arch as M, test_roundtrip as T
import offline_screen_ab as AB
import gen_c_blpr, gen_c_alob

DRV = r'''
#include <stdio.h>
#include <stdlib.h>
int lookup_symbol(uintptr_t, char *, size_t, uint32_t *);
int main(int argc, char **argv) {
    unsigned long a; unsigned cap = atoi(argv[1]); char buf[256];
    while (scanf("%lu", &a) == 1) {
        uint32_t off = 0xdeadbeef;
        int rc = lookup_symbol((uintptr_t)a, buf, cap, &off);
        printf("%d|%s|%u\n", rc, buf, off);
    }
    return 0;
}
'''

def run_test(c_code, d, cap, qs):
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "g.c")
        with open(src, "w") as f: f.write(c_code)
        drv = os.path.join(tmp, "d.c")
        with open(drv, "w") as f: f.write("#include <stdint.h>\n#include <stddef.h>\n" + DRV)
        exe = os.path.join(tmp, "t")
        r = subprocess.run(["gcc", "-O1", "-Wall", "-Wno-unused-function", src, drv, "-o", exe], capture_output=True, text=True)
        if r.returncode:
            print("Compile error:", r.stderr[:2000])
            return None
        out = subprocess.run([exe, str(cap)], input="\n".join(map(str, qs)), capture_output=True, text=True).stdout.splitlines()
        return out

def expect(d, a, cap):
    o = T.oracle(d, a)
    if o is None: return ("1|?|0" if cap >= 2 else "1||0")
    nm, off = o
    if len(nm) + 1 <= cap: return f"0|{nm}|{off}"
    return f"2|{nm[:cap - 1]}|{off}"

def main():
    total_mismatches = 0
    wls = list(AB.FILES.keys())
    
    print("=== Host C Correctness Testing ===")
    for wl in wls:
        d = AB.load(wl)
        qs = T.queries(d)
        print(f"\nWorkload: {wl} ({len(qs)} queries)")
        
        # Test BLPR B=8, 16, 32
        for B in (8, 16, 32):
            F = AB.build_blpr(d, B)
            c_code = gen_c_blpr.gen_blpr(F)
            for cap in (64, 12):
                out = run_test(c_code, d, cap, qs)
                if out is None:
                    print(f"  BLPR B={B} cap={cap}: COMPILE FAILED")
                    total_mismatches += 1000
                    continue
                bad = sum(1 for a, o in zip(qs, out) if o != expect(d, a, cap))
                total_mismatches += bad
                print(f"  BLPR B={B:2d} cap={cap:2d}: mismatches={bad}")
                if bad > 0:
                    for a, o in zip(qs, out):
                        exp = expect(d, a, cap)
                        if o != exp:
                            print(f"    Sample fail addr {a}: got '{o}', exp '{exp}'")
                            break
                            
        # Test ALOB B=8, 16, 32
        for B in (8, 16, 32):
            F = AB.build_alob(d, B)
            c_code = gen_c_alob.gen_alob(F)
            for cap in (64, 12):
                out = run_test(c_code, d, cap, qs)
                if out is None:
                    print(f"  ALOB B={B} cap={cap}: COMPILE FAILED")
                    total_mismatches += 1000
                    continue
                bad = sum(1 for a, o in zip(qs, out) if o != expect(d, a, cap))
                total_mismatches += bad
                print(f"  ALOB B={B:2d} cap={cap:2d}: mismatches={bad}")
                if bad > 0:
                    for a, o in zip(qs, out):
                        exp = expect(d, a, cap)
                        if o != exp:
                            print(f"    Sample fail addr {a}: got '{o}', exp '{exp}'")
                            break

    print(f"\nTOTAL HOST C MISMATCHES: {total_mismatches}")
    return total_mismatches

if __name__ == "__main__":
    sys.exit(main())
