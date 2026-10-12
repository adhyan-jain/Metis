#!/usr/bin/env python3
"""METIS architecture tournament: encoders + reference decoder (exact byte accounting).

Address side (offsets in 2-byte units: every manifest address is Thumb/odd, all gaps even):
  RAW32 : control = u32 offsets, as in every historical method
  A     : fixed-B microblocks, per-block smallest fixed width local offsets (W1/W2), binary search in block
  B1    : fixed-B restarted LEB128 delta blocks          B2: fixed-B restarted bit-packed delta blocks
  D     : variable blocks (<=Bmax) + per-block codec in {W1,W2,VAR,BPw}, exact DP over boundaries/codecs
Name side:
  TOK : NUL-terminated names with pair tokens (>=0x80); name checkpoint shares the address block directory
  FC  : sorted unique names, checkpointed block front coding (+pair tokens on suffixes), u8/u16/bit-packed IDs
"""
import json, collections

FILES = {"hello_world_mps2": "hello_world_158", "shell_module_mps2": "shell_module_557",
         "echo_server_mps2": "echo_server_1381", "hello_world_nrf52840": "nrf52840_hello_226"}

def load(wl, root="."):
    s = json.load(open(f"{root}/data/zephyr_symtab_{FILES[wl]}.json"))
    first = s[0]["addr"]
    offs = [(x["addr"] - first) >> 1 for x in s]
    assert all((x["addr"] - first) % 2 == 0 for x in s) and all(b > a for a, b in zip(offs, offs[1:]))
    assert s[-1]["name"] == "?"
    return dict(first=first, offs=offs[:-1], end=offs[-1], names=[x["name"] for x in s[:-1]], wl=wl)

def bl(v): return max(1, int(v).bit_length())
def bw(v): return max(1, (bl(v) + 7) // 8)
def pk(vals, w): return b"".join(int(v).to_bytes(w, "little") for v in vals)
def leb(v):
    o = bytearray()
    while True:
        b = v & 127; v >>= 7
        if v: o.append(b | 128)
        else: o.append(b); return bytes(o)

# ------------------------------------------------------------------ address block codecs
class Blk:
    def __init__(s, st, c, codec, data): s.st, s.c, s.codec, s.data = st, c, codec, data

def enc_block(offs, st, c, codec):
    if c == 1: return b""
    base = offs[st]
    d = [offs[st + i] - offs[st + i - 1] for i in range(1, c)]
    loc = [offs[st + i] - base for i in range(1, c)]
    if codec == "W1": return bytes(loc) if max(loc) < 256 else None
    if codec == "W2": return pk(loc, 2) if max(loc) < 65536 else None
    if codec == "VAR": return b"".join(leb(x) for x in d)
    w = int(codec[2:])
    if max(d) >= (1 << w): return None
    acc = 0
    for i, x in enumerate(d): acc |= x << (i * w)
    return acc.to_bytes((len(d) * w + 7) // 8, "little")

CODECS = {"A": ["W1", "W2"], "B1": ["VAR"], "B2": [f"BP{w}" for w in range(1, 17)],
          "D": ["W1", "W2", "VAR"] + [f"BP{w}" for w in range(1, 17)]}

def best_block(offs, st, c, kind):
    if c == 1: return ("W1" if kind in ("A", "D") else CODECS[kind][0], b"")
    best = None
    for k in CODECS[kind]:
        x = enc_block(offs, st, c, k)
        if x is not None and (best is None or len(x) < len(best[1])): best = (k, x)
    return best

def plan_fixed(offs, B, kind):
    return [Blk(st, min(B, len(offs) - st), *best_block(offs, st, min(B, len(offs) - st), kind))
            for st in range(0, len(offs), B)]

def plan_dp(offs, Bmax, ov):
    n = len(offs); INF = 1e18; cost = [INF] * (n + 1); cost[0] = 0; back = [None] * (n + 1)
    for i in range(n):
        for c in range(1, min(Bmax, n - i) + 1):
            k, x = best_block(offs, i, c, "D")
            t = cost[i] + ov + len(x)
            if t < cost[i + c]: cost[i + c] = t; back[i + c] = Blk(i, c, k, x)
    out = []; j = n
    while j > 0: b = back[j]; out.append(b); j = b.st
    return out[::-1]

# ------------------------------------------------------------------ name codecs
_MERGE_CACHE = {}
def _apply(s, a, b, t):
    o = []; i = 0; n = len(s)
    while i < n:
        if i + 1 < n and s[i] == a and s[i + 1] == b: o.append(t); i += 2
        else: o.append(s[i]); i += 1
    return o

def train_pairs(seqs, T, key=None):
    """greedy pair merging; the T-token result is the first T merges of a 128-merge run (cached by key)."""
    if key is not None and key in _MERGE_CACHE: merges = _MERGE_CACHE[key]
    else:
        merges = []; w = [list(s) for s in seqs]
        while len(merges) < 128:
            cnt = collections.Counter()
            for s in w:
                for a, b in zip(s, s[1:]): cnt[(a, b)] += 1
            if not cnt: break
            (a, b), c = max(cnt.items(), key=lambda kv: (kv[1], kv[0]))
            if c <= 2: break                   # each use saves 1 byte, table entry costs 2
            t = 0x80 + len(merges); merges.append((a, b))
            w = [_apply(s, a, b, t) for s in w]
        if key is not None: _MERGE_CACHE[key] = merges
    toks = merges[:T]; w = [list(s) for s in seqs]
    for i, (a, b) in enumerate(toks): w = [_apply(s, a, b, 0x80 + i) for s in w]
    return toks, w

def stack_need(toks, seqs):
    mx = 1
    for s in seqs:
        for t in s:
            st = [t]
            while st:
                mx = max(mx, len(st)); x = st.pop()
                if x >= 0x80: a, b = toks[x - 0x80]; st.append(b); st.append(a)
    return mx + 1

def lcp(a, b):
    n = min(len(a), len(b)); i = 0
    while i < n and a[i] == b[i]: i += 1
    return i

TBUDGETS = (0, 16, 32, 48, 64, 96, 128)

def tok_names(names, T):
    seqs = [list(n.encode()) for n in names]
    toks, enc = train_pairs(seqs, T, ('tok', hash(tuple(names))))
    pool = bytearray(); st = []
    for e in enc: st.append(len(pool)); pool += bytes(e) + b"\0"
    return dict(toks=toks, enc=enc, pool=bytes(pool), starts=st)

def fc_names(names, Bn, T):
    uniq = sorted(set(names)); lc = []; suf = []
    for i, u in enumerate(uniq):
        p = 0 if i % Bn == 0 else min(lcp(uniq[i - 1], u), 255); lc.append(p); suf.append(list(u.encode()[p:]))
    toks, enc = train_pairs(suf, T, ('fc', Bn, hash(tuple(names))))
    pool = bytearray(); blk = []
    for i, e in enumerate(enc):
        if i % Bn == 0: blk.append(len(pool))
        pool += bytes([lc[i]]) + bytes(e) + b"\0"
    return dict(uniq=uniq, toks=toks, enc=enc, pool=bytes(pool), blkoff=blk)

# ------------------------------------------------------------------ assemble format
def build(d, kind, B, nk="TOK", T=None, Bn=16):
    offs, names = d["offs"], d["names"]; n = len(offs)
    F = dict(wl=d["wl"], kind=kind, B=B, nk=nk, Bn=Bn, n=n, end=d["end"], first=d["first"])
    # --- names (pick token budget by exact total bytes)
    if nk == "KALL":
        import sys as _s; _s.path.insert(0, "scripts"); _s.path.insert(0, ".")
        from scripts.kallsyms_generator import build_kallsyms_representation as _bk
        kd = _bk([dict(name=nm, addr=a) for nm, a in zip(names, offs)], marker_interval=B)
        starts = []; pos = 0
        for nm in range(n): starts.append(pos); pos += kd["names"][pos] + 1
        N = dict(pool=bytes(kd["names"]), starts=starts, toks=[], enc=[])
        F.update(T=256, toks=[], pool=N["pool"], tokb=b"", sd=0, kidx=pk(kd["token_index"], 2), ktab=bytes(kd["token_table"]))
        cand = [(0, 256, N)]
    elif nk == "TOK":
        cand = [(len(r["pool"]) + 2 * len(r["toks"]), t, r) for t in ([T] if T is not None else TBUDGETS)
                for r in [tok_names(names, t)]]
    else:
        cand = [(len(r["pool"]) + 2 * len(r["toks"]), t, r) for t in ([T] if T is not None else TBUDGETS)
                for r in [fc_names(names, Bn, t)]]
    _, T, N = min(cand, key=lambda x: x[0])
    if nk != "KALL":
        F["T"] = T; F["toks"] = N["toks"]; F["pool"] = N["pool"]; F["tokb"] = bytes(x for p in N["toks"] for x in p)
        F["sd"] = stack_need(N["toks"], N["enc"])
    if nk == "FC":
        U = len(N["uniq"]); rank = {u: i for i, u in enumerate(N["uniq"])}; ids = [rank[x] for x in names]
        idb = bl(U - 1)
        modes = {"u16": 2 * n, "bp": (n * idb + 7) // 8}
        if U <= 256: modes["u8"] = n
        im = min(modes, key=modes.get); F["idm"] = im; F["idb"] = idb; F["U"] = U
        if im == "u8": F["ids"] = bytes(ids)
        elif im == "u16": F["ids"] = pk(ids, 2)
        else:
            acc = 0
            for i, v in enumerate(ids): acc |= v << (i * idb)
            F["ids"] = acc.to_bytes((n * idb + 7) // 8, "little")
        F["nbo_w"] = bw(max(N["blkoff"])); F["nbo"] = pk(N["blkoff"], F["nbo_w"])
    # --- address
    if kind == "RAW32":
        F["blocks"] = [Blk(st, min(B, n - st), "RAW", b"") for st in range(0, n, B)]
        F["raw"] = pk([o * 2 for o in offs] + [d["end"] * 2], 4)
        F["noff_w"] = bw(max(N["starts"][b.st] for b in F["blocks"]))
        F["noff"] = pk([N["starts"][b.st] for b in F["blocks"]], F["noff_w"])
        return F
    base_w = bw(offs[-1])
    noff_w = bw(max(N["starts"])) if nk in ("TOK", "KALL") else 0
    xw = bw(n) if (kind == "D" and nk == "FC") else 0     # per-block first-symbol index (needed for IDs)
    if kind == "D":
        ov = base_w + 3 + noff_w + xw
        for _ in range(3):
            F["blocks"] = blocks = plan_dp(offs, B, ov)
            ov = base_w + _amw(blocks, True) + noff_w + xw
    else:
        F["blocks"] = blocks = plan_fixed(offs, B, kind)
    var = kind == "D"
    stream = bytearray(); ao = []
    for b in blocks: ao.append(len(stream)); stream += b.data
    cids = sorted({b.codec for b in blocks}, key=lambda k: (len(k), k)); cmap = {k: i for i, k in enumerate(cids)}
    cbits = (len(cids) - 1).bit_length(); nbits = 4 if var else 0
    am = [((a << cbits | cmap[b.codec]) << nbits) | ((b.c - 1) if var else 0) for a, b in zip(ao, blocks)]
    F.update(stream=bytes(stream), cids=cids, cbits=cbits, nbits=nbits, am_w=bw(max(am)), am=pk(am, bw(max(am))),
             base_w=base_w, base=pk([offs[b.st] for b in blocks], base_w))
    if nk in ("TOK", "KALL"):
        F["noff_w"] = noff_w; F["noff"] = pk([N["starts"][b.st] for b in blocks], noff_w)
    if xw: F["sidx_w"] = xw; F["sidx"] = pk([b.st for b in blocks], xw)
    return F

def _amw(blocks, var):
    s = sum(len(b.data) for b in blocks)
    return bw(((s << 5) << 4) | 15)

def breakdown(F):
    g = lambda k: len(F.get(k, b""))
    if F["kind"] == "RAW32": addr = g("raw"); cp = g("noff"); dirb = 0; stream = 0
    else:
        addr = g("base") + g("am") + g("stream") + g("sidx"); cp = g("noff"); dirb = g("base") + g("am") + g("sidx"); stream = g("stream")
    d = dict(address_index=addr, addr_directory=dirb, addr_stream=stream, name_checkpoints=cp + g("nbo"),
             dictionary=g("tokb") + g("kidx") + g("ktab"), name_pool=g("pool"), ids=g("ids"))
    d["payload"] = addr + cp + g("nbo") + g("tokb") + g("kidx") + g("ktab") + g("pool") + g("ids")
    return d

# ------------------------------------------------------------------ reference decoder (works only on emitted bytes)
def rd(a, i, w): return int.from_bytes(a[i * w:(i + 1) * w], "little")

def expand(F, p_bytes, out, cap_stats):
    st = []; i = 0
    tb = F["tokb"]
    while True:
        if st: c = st.pop()
        else:
            c = p_bytes[i]; i += 1
            if c == 0: break
        if c >= 0x80:
            t = c - 0x80; st.append(tb[2 * t + 1]); st.append(tb[2 * t]); continue
        out.append(c)
    return i

def lookup(F, addr, stats=None):
    if addr < F["first"]: return None
    rel = addr - F["first"]; o = rel >> 1
    if o >= F["end"]: return None
    k = F["kind"]; n = F["n"]; B = F["B"]; blocks = F["blocks"]; nb = len(blocks)
    if k == "RAW32":
        lo, hi, st = 0, n - 1, 0
        while lo <= hi:
            m = (lo + hi) // 2; st += 1
            if rd(F["raw"], m, 4) <= rel: lo = m + 1
            else: hi = m - 1
        s = hi; sym_rel = rd(F["raw"], s, 4); b = s // B; kk = s % B
        if stats is not None: stats["asteps"] = st
    else:
        lo, hi, st = 0, nb - 1, 0
        while lo <= hi:
            m = (lo + hi) // 2; st += 1
            if rd(F["base"], m, F["base_w"]) <= o: lo = m + 1
            else: hi = m - 1
        b = hi; base = rd(F["base"], b, F["base_w"]); ol = o - base
        am = rd(F["am"], b, F["am_w"]); var = F["nbits"] > 0
        cnt = ((am & 15) + 1) if var else (B if b < nb - 1 else n - (nb - 1) * B)
        am >>= F["nbits"]; codec = F["cids"][am & ((1 << F["cbits"]) - 1)] if F["cbits"] else F["cids"][0]
        ao = am >> F["cbits"]; S = F["stream"]
        kk = 0
        if codec in ("W1", "W2"):
            w = 1 if codec == "W1" else 2; lo, hi = 0, cnt - 1; kk = 0
            while lo <= hi:
                m = (lo + hi) // 2; st += 1
                v = 0 if m == 0 else int.from_bytes(S[ao + (m - 1) * w: ao + m * w], "little")
                if v <= ol: lo = m + 1
                else: hi = m - 1
            kk = hi; loc = 0 if kk == 0 else int.from_bytes(S[ao + (kk - 1) * w: ao + kk * w], "little")
        elif codec == "VAR":
            p = ao; loc = 0; kk = 0
            while kk < cnt - 1:
                v = 0; sh = 0
                while True:
                    c = S[p]; p += 1; v |= (c & 127) << sh; sh += 7; st += 1
                    if not c & 128: break
                if loc + v > ol: break
                loc += v; kk += 1
        else:
            w = int(codec[2:]); loc = 0; kk = 0
            while kk < cnt - 1:
                bit = kk * w; chunk = int.from_bytes(S[ao + (bit >> 3): ao + (bit >> 3) + 3], "little")
                v = (chunk >> (bit & 7)) & ((1 << w) - 1); st += 1
                if loc + v > ol: break
                loc += v; kk += 1
        sym_rel = (base + loc) * 2; s = (rd(F["sidx"], b, F["sidx_w"]) if "sidx" in F else b * B) + kk
        if stats is not None: stats["asteps"] = st
    # ---- name
    out = bytearray()
    if F["nk"] == "KALL":
        p = rd(F["noff"], b, F["noff_w"]); pool = F["pool"]
        for _ in range(kk): p += pool[p] + 1
        ln = pool[p]; p += 1
        for j in range(ln):
            t = pool[p + j]; i0 = rd(F["kidx"], t, 2); e = F["ktab"].index(0, i0); out += F["ktab"][i0:e]
        if stats is not None: stats["nrec"] = kk
    elif F["nk"] == "TOK":
        p = rd(F["noff"], b, F["noff_w"]); pool = F["pool"]
        for _ in range(kk):
            while pool[p] != 0: p += 1
            p += 1
        if stats is not None: stats["nrec"] = kk
        expand(F, pool[p:], out, None)
    else:
        if F["idm"] == "u8": sid = F["ids"][s]
        elif F["idm"] == "u16": sid = rd(F["ids"], s, 2)
        else:
            bit = s * F["idb"]; sid = (int.from_bytes(F["ids"][(bit >> 3):(bit >> 3) + 4], "little") >> (bit & 7)) & ((1 << F["idb"]) - 1)
        Bn = F["Bn"]; p = rd(F["nbo"], sid // Bn, F["nbo_w"]); pool = F["pool"]; cur = bytearray()
        for j in range(sid % Bn + 1):
            lc = pool[p]; p += 1
            suf = bytearray(); used = expand(F, pool[p:], suf, None); p += used
            cur = cur[:lc] + suf
        if stats is not None: stats["nrec"] = sid % Bn
        out = cur
    return out.decode(), rel - sym_rel
