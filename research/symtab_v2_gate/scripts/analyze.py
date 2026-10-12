#!/usr/bin/env python3
"""Aggregate raw harness output into tables (memory, latency, paired differences)."""
import csv, collections, math, random, statistics as st, sys, os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
CORP = ["lua", "cjson", "protobufc", "freertos", "redisdeps", "redis"]
MAIN = ["Conventional", "Interned", "AbslFlat", "ScopedId", "V2"]
ABL = ["V2", "V2_noFingerprint", "V2_noReclaim", "V2_noSlotRecycle", "V2_fixedInterned", "V2_noCompress", "V2_compressFirst",
       "V2_noSeenMap_diag", "V2_noSeenMap_noCompress_diag", "V2_noSeenMap_fixedInterned_diag"]
T95 = {2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571, 6: 2.447, 7: 2.365, 8: 2.306, 9: 2.262, 10: 2.228, 11: 2.201, 12: 2.179, 13: 2.160, 14: 2.145, 15: 2.131}
def ci(v):
    n = len(v); m = st.mean(v)
    if n < 2: return m, 0.0
    return m, T95.get(n, 1.96) * st.stdev(v) / math.sqrt(n)

def mem(c):
    d = collections.defaultdict(dict)
    for r in csv.DictReader(open(f"raw/mem_{c}.csv")):
        d[r['impl']][int(r['tu'])] = (int(r['peak_heap']), int(r['final_heap']))
    return d
def boot(ratios, B=2000):
    rnd = random.Random(1); n = len(ratios); out = []
    for _ in range(B): out.append(st.mean(ratios[rnd.randrange(n)] for _ in range(n)))
    out.sort(); return out[int(.025 * B)], out[int(.975 * B)]

def timing(c):
    d = collections.defaultdict(lambda: collections.defaultdict(dict))  # impl->metric->rep->val
    for r in csv.DictReader(open(f"raw/time_{c}.csv")):
        if r['mode'] == 'time': d[r['impl']][r['metric']][int(r['rep'])] = float(r['value'])
    return d

out = []
P = out.append
P("## Physical heap (allocator-measured, fresh table per TU; ratio = paired per-TU peak ratio, bootstrap 95% CI)\n")
P("| corpus | TUs | impl | peak KiB (mean) | peak KiB (max TU) | final KiB (mean) | peak ratio vs Conv [95% CI] | peak ratio vs ScopedId |")
P("|---|---|---|---|---|---|---|---|")
for c in CORP:
    m = mem(c); tus = sorted(m['Conventional'])
    for impl in MAIN + ABL[1:]:
        if impl not in m: continue
        pk = [m[impl][t][0] for t in tus]; fn = [m[impl][t][1] for t in tus]
        rc = [m[impl][t][0] / m['Conventional'][t][0] for t in tus]
        rs = [m[impl][t][0] / m['ScopedId'][t][0] for t in tus]
        lo, hi = boot(rc) if len(tus) > 1 else (rc[0], rc[0])
        P(f"| {c} | {len(tus)} | {impl} | {st.mean(pk)/1024:.1f} | {max(pk)/1024:.1f} | {st.mean(fn)/1024:.1f} | {st.mean(rc):.3f} [{lo:.3f},{hi:.3f}] | {st.mean(rs):.3f} |")

P("\n## Latency (host x86-64 i7-13620H P-core, powersave governor; ns after timer-overhead subtraction; mean over reps ± 95% CI over reps)\n")
P("| corpus | impl | agg ns/event | hit p50 | hit p95 | hit p99 | hit p99.9 | hit max | miss p50 | miss p99 | decl p50 | decl p99 | exit p50 | exit p99 |")
P("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
for c in CORP:
    if not os.path.exists(f"raw/time_{c}.csv"): continue
    t = timing(c)
    for impl in MAIN + ABL[1:7]:
        if impl not in t: continue
        cells = []
        for mt in ["agg_ns_per_event", "hit_p50_ns", "hit_p95_ns", "hit_p99_ns", "hit_p999_ns", "hit_max_ns", "miss_p50_ns", "miss_p99_ns", "decl_p50_ns", "decl_p99_ns", "exit_p50_ns", "exit_p99_ns"]:
            v = list(t[impl][mt].values()); m_, h = ci(v) if v else (float('nan'), 0)
            cells.append(f"{m_:.0f}±{h:.0f}")
        P(f"| {c} | {impl} | " + " | ".join(cells) + " |")

P("\n## Paired per-rep differences (V2 − other; negative = V2 faster). Same trace, same rep, randomized order.\n")
P("| corpus | metric | other | mean Δ ns | 95% CI | verdict |")
P("|---|---|---|---|---|---|")
for c in CORP:
    if not os.path.exists(f"raw/time_{c}.csv"): continue
    t = timing(c)
    for other in ["Conventional", "ScopedId", "AbslFlat"]:
        for mt in ["agg_ns_per_event", "hit_p50_ns", "hit_p99_ns", "miss_p50_ns", "decl_p50_ns", "exit_p50_ns"]:
            reps = sorted(set(t['V2'][mt]) & set(t[other][mt]))
            if len(reps) < 2: continue
            dlt = [t['V2'][mt][r] - t[other][mt][r] for r in reps]; m_, h = ci(dlt)
            P(f"| {c} | {mt} | {other} | {m_:+.1f} | ±{h:.1f} | {'V2 faster' if m_ + h < 0 else ('V2 slower' if m_ - h > 0 else 'inconclusive')} |")

open("raw/analysis_tables.md", "w").write("\n".join(out))
print("\n".join(out))
