"""Reference-decoder round trip vs a direct oracle built from the frozen manifests."""
import bisect, random, sys
sys.path.insert(0, "research/metis_arch")
import metis_arch as M

def queries(d):
    f = d["first"]; o = d["offs"]; e = d["end"]; q = {f - 1, f - 2, f, f + 2 * e - 1, f + 2 * e - 2, f + 2 * e, f + 2 * e + 100}
    for i, x in enumerate(o):
        nxt = o[i + 1] if i + 1 < len(o) else e
        for r in (0, 1, 2, (nxt - x) - 1 if nxt - x > 1 else 0):
            q.add(f + 2 * x + r); q.add(f + 2 * x + 2 * r)
    random.seed(1); q |= {f + random.randrange(0, 2 * e) for _ in range(2000)}
    return sorted(q)

def oracle(d, a):
    rel = a - d["first"]
    if rel < 0 or rel >= 2 * d["end"]: return None
    i = bisect.bisect_right([2 * x for x in d["offs"]], rel) - 1
    return d["names"][i], rel - 2 * d["offs"][i]

def check(d, F):
    mx = dict(asteps=0, nrec=0); bad = 0
    for a in queries(d):
        st = {}
        got = M.lookup(F, a, st)
        if got != oracle(d, a): bad += 1
        for k in mx: mx[k] = max(mx[k], st.get(k, 0))
    return bad, mx

if __name__ == "__main__":
    cfgs = [("RAW32", 16, "TOK"), ("A", 8, "TOK"), ("A", 16, "TOK"), ("B1", 8, "TOK"), ("B1", 16, "TOK"), ("B2", 8, "TOK"),
            ("B2", 16, "TOK"), ("D", 16, "TOK"), ("A", 16, "FC"), ("B1", 16, "FC"), ("B2", 8, "FC")]
    tot = 0
    for wl in M.FILES:
        d = M.load(wl)
        for kind, B, nk in cfgs:
            F = M.build(d, kind, B, nk)
            bad, mx = check(d, F); tot += bad
            print(wl, kind, B, nk, "mismatches", bad, mx, M.breakdown(F)["payload"])
    print("TOTAL MISMATCHES", tot); sys.exit(1 if tot else 0)
