#!/usr/bin/env python3
"""ODHST generator (offline). Address-ordered index of name IDs + independent sorted,
block-front-coded name dictionary with optional BPE suffix tokens (>=0x80).
Record: [lcp:u8][suffix bytes, NUL-terminated]. Block start has lcp=0.
Per-block checkpoint: u16 byte offset (u8 not enough). Exact sizes reported."""
import json, math, sys, collections

def lcp(a, b):
    n = min(len(a), len(b)); i = 0
    while i < n and a[i] == b[i]: i += 1
    return i

def front_code(uniq, B):
    recs = []; cps = []; pos = 0
    for i, n in enumerate(uniq):
        p = 0 if i % B == 0 else min(lcp(uniq[i-1], n), 255)
        if i % B == 0: cps.append(pos)
        recs.append((p, list(n[p:].encode())))
        pos += 1 + len(recs[-1][1]) + 1
    return recs, cps

def train_tokens(recs, budget):
    """Greedy pair merging on suffixes; token ids 0x80+k. Stops when budget hit or a merge's
    net byte gain (count - expansion bytes - 1 table byte) <= 0 (hybrid: never store a token that costs more)."""
    exp = {}
    toks = []
    seqs = [s for _, s in recs]
    def explen(t): return len(exp[t]) if t in exp else 1
    while len(toks) < budget:
        cnt = collections.Counter()
        for s in seqs:
            for a, b in zip(s, s[1:]): cnt[(a, b)] += 1
        if not cnt: break
        (a, b), c = max(cnt.items(), key=lambda kv: (kv[1], kv[0]))
        # token table cost: expansion stored as 2 token-ids (2 bytes) -> entry = 2 bytes
        if c - 2 <= 0: break
        t = 0x80 + len(toks); toks.append((a, b)); exp[t] = (a, b)
        for k, s in enumerate(seqs):
            o = []; i = 0
            while i < len(s):
                if i + 1 < len(s) and s[i] == a and s[i+1] == b: o.append(t); i += 2
                else: o.append(s[i]); i += 1
            seqs[k] = o
    return toks, seqs

def build(names_by_addr, B, budget, id_mode):
    uniq = sorted(set(names_by_addr))
    U = len(uniq)
    recs, cps = front_code(uniq, B)
    toks, seqs = train_tokens(recs, budget)
    pool = sum(1 + len(s) + 1 for (p, _), s in zip(recs, seqs))
    cpb = 2 * len(cps) if pool > 255 else len(cps)
    dictb = 2 * len(toks)               # each token = pair of byte ids
    N = len(names_by_addr)
    bits = max(1, math.ceil(math.log2(U))) if U > 1 else 1
    ids = {"u8": N if U <= 256 else None, "u16": 2 * N, "bitpacked": math.ceil(N * bits / 8)}[id_mode]
    return dict(U=U, N=N, B=B, budget=len(toks), id_mode=id_mode, id_bits=bits, id_bytes=ids,
                pool_bytes=pool, checkpoint_bytes=cpb, dict_bytes=dictb, addr_bytes=4 * N,
                payload=(4 * N if ids is not None else 0) + (ids or 0) + pool + cpb + dictb) if ids is not None else None

if __name__ == "__main__":
    files = {"hello_mps2": "hello_world_158", "shell_mps2": "shell_module_557",
             "echo_mps2": "echo_server_1381", "hello_nrf": "nrf52840_hello_226"}
    for w, f in files.items():
        syms = json.load(open(f"data/zephyr_symtab_{f}.json"))
        names = [s["name"] for s in syms]
        rows = []
        for B in (4, 8, 16, 32):
            for bud in (0, 16, 32, 64, 128):
                for m in ("u8", "u16", "bitpacked"):
                    r = build(names, B, bud, m)
                    if r: rows.append(r)
        best = min(rows, key=lambda r: r["payload"])
        b16 = min((r for r in rows if r["B"] == 16), key=lambda r: r["payload"])
        print(w, "N", best["N"], "U", best["U"], "best", best, "\n   B16", b16)
