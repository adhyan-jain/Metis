"""APPROXIMATE FSST-style name-pool size (offline estimate only; NOT the reference FSST implementation, NOT built into firmware).
Symbols <=8 bytes, <=255, 5 count/merge generations (gain = count*len), unmatched bytes escaped (2 bytes), 1-byte length prefix per name.
Table cost = packed (sum(len)+#symbols). Compared against the name-side bytes (pool + dictionary [+IDs]) of the ODHST-v2 TOK/FC formats."""
import collections, sys, json
sys.path.insert(0, "research/metis_arch")
import metis_arch as M

def encode(name, table, maxlen):
    out = []; i = 0; b = name.encode()
    while i < len(b):
        for L in range(min(maxlen, len(b) - i), 0, -1):
            if b[i:i + L] in table: out.append(b[i:i + L]); break
        else: out.append(("ESC", b[i:i + 1])); L = 1
        i += L
    return out

def train(names, gens=5):
    table = set(); 
    for g in range(gens):
        cnt = collections.Counter()
        for n in names:
            enc = encode(n, table, 8); prev = None
            for s in enc:
                sb = s if isinstance(s, bytes) else s[1]
                cnt[sb] += 1
                if prev is not None and len(prev) + len(sb) <= 8: cnt[prev + sb] += 1
                prev = sb
        gain = {s: c * len(s) for s, c in cnt.items()}
        table = set(s for s, _ in sorted(gain.items(), key=lambda kv: (-kv[1], kv[0]))[:255])
    return table

for wl in M.FILES:
    d = M.load(wl); names = d["names"]; tb = train(names)
    pool = 0
    for n in names:
        enc = encode(n, tb, 8); pool += 1 + sum(1 if isinstance(s, bytes) else 2 for s in enc)
    table = sum(len(s) for s in tb) + len(tb)
    F = M.build(d, "B1", 16, "FC", Bn=16); bd = M.breakdown(F)
    T = M.build(d, "B1", 16, "TOK"); bt = M.breakdown(T)
    print(f"{wl:22s} FSST-like pool+table={pool+table:6d} (pool {pool}, table {table}) | FC pool+dict+ids={bd['name_pool']+bd['dictionary']+bd['ids']:6d} | TOK pool+dict={bt['name_pool']+bt['dictionary']:6d}  (names-only; each + block checkpoints)")
