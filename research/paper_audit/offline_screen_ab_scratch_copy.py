#!/usr/bin/env python3
"""Stage 1: Offline screening for Candidate A (BLPR) and Candidate B (ALOB).
Evaluates exact representation bytes, local rank maps, dictionaries,
block directories, worst-case work bounds, and roundtrips all symbols.
Compares against B1_B16_TOK and B1_B16_FC16 baselines.
"""
import json, collections, math, os, sys

ROOT = "/home/adhyan/Desktop/Compiler"
DATA_DIR = f"{ROOT}/data"
FILES = {
    "hello_world_mps2": "hello_world_158",
    "shell_module_mps2": "shell_module_557",
    "echo_server_mps2": "echo_server_1381",
    "hello_world_nrf52840": "nrf52840_hello_226"
}

def load(wl):
    s = json.load(open(f"{DATA_DIR}/zephyr_symtab_{FILES[wl]}.json"))
    first = s[0]["addr"]
    offs = [(x["addr"] - first) >> 1 for x in s]
    assert all((x["addr"] - first) % 2 == 0 for x in s)
    assert all(b > a for a, b in zip(offs, offs[1:]))
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

def lcp(a, b):
    n = min(len(a), len(b)); i = 0
    while i < n and a[i] == b[i]: i += 1
    return i

# Token training
def _apply(s, a, b, t):
    o = []; i = 0; n = len(s)
    while i < n:
        if i + 1 < n and s[i] == a and s[i + 1] == b: o.append(t); i += 2
        else: o.append(s[i]); i += 1
    return o

_MERGE_CACHE = {}
def train_pairs(seqs, T, key=None):
    if key is not None and key in _MERGE_CACHE: merges = _MERGE_CACHE[key]
    else:
        merges = []; w = [list(s) for s in seqs]
        while len(merges) < 128:
            cnt = collections.Counter()
            for s in w:
                for a, b in zip(s, s[1:]): cnt[(a, b)] += 1
            if not cnt: break
            (a, b), c = max(cnt.items(), key=lambda kv: (kv[1], kv[0]))
            if c <= 2: break
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

# B1 address encoding
def enc_b1_address(offs, B):
    n = len(offs)
    blocks = []
    stream = bytearray()
    ao = []
    base_list = []
    for st in range(0, n, B):
        c = min(B, n - st)
        base = offs[st]
        base_list.append(base)
        ao.append(len(stream))
        if c > 1:
            d = [offs[st + i] - offs[st + i - 1] for i in range(1, c)]
            stream += b"".join(leb(x) for x in d)
    base_w = bw(offs[-1])
    am_w = bw(len(stream))
    return dict(
        B=B, n=n, nblk=len(base_list),
        base=pk(base_list, base_w), base_w=base_w,
        am=pk(ao, am_w), am_w=am_w,
        stream=bytes(stream),
        addr_bytes=len(base_list)*base_w + len(ao)*am_w + len(stream)
    )

# Candidate A (BLPR)
def build_blpr(d, B, T=None):
    offs, names = d["offs"], d["names"]
    n = len(names)
    nblk = (n + B - 1) // B
    
    # Within each block:
    # 1. Lexicographically sort block names
    # 2. Compute local rank for each address-order position
    # 3. Compute front-coding suffixes
    block_records = []
    all_sufs = []
    
    # rank bit width
    rbits = bl(B - 1) # B=8 -> 3, B=16 -> 4, B=32 -> 5
    
    for b in range(nblk):
        st = b * B
        c = min(B, n - st)
        blk_names = names[st : st + c]
        # Sort uniquely / stably
        # Store (name, orig_idx)
        sorted_blk = sorted(enumerate(blk_names), key=lambda x: x[1])
        # local rank map: orig_idx -> local rank
        rank_map = [0] * c
        sorted_names = []
        for r, (orig_idx, nm) in enumerate(sorted_blk):
            rank_map[orig_idx] = r
            sorted_names.append(nm)
        
        # Front coding within block
        lcps = [0] * c
        sufs = []
        for i, s_nm in enumerate(sorted_names):
            p = 0 if i == 0 else min(lcp(sorted_names[i-1], s_nm), 255)
            lcps[i] = p
            sufs.append(list(s_nm.encode()[p:]))
            all_sufs.append(list(s_nm.encode()[p:]))
            
        block_records.append(dict(
            st=st, c=c, rank_map=rank_map,
            sorted_names=sorted_names, lcps=lcps, sufs=sufs
        ))
        
    # Train tokens on all suffixes
    t_budgets = (0, 16, 32, 48, 64, 96, 128) if T is None else [T]
    best_cand = None
    
    for t_cand in t_budgets:
        toks, enc_sufs = train_pairs(all_sufs, t_cand, key=('blpr', B, hash(tuple(names))))
        # Pack pool
        pool = bytearray()
        noff = []
        suf_idx = 0
        for b_rec in block_records:
            noff.append(len(pool))
            c = b_rec["c"]
            for i in range(c):
                enc_s = enc_sufs[suf_idx]
                suf_idx += 1
                pool.append(b_rec["lcps"][i])
                pool += bytes(enc_s) + b"\0"
        
        # Pack rank map
        # Each block has c entries of rbits width
        # Packed globally or per block:
        # Standard: array of n entries, each rbits width
        # For B=16: 4 bits per entry = (n * 4 + 7) // 8 bytes.
        rank_map_flat = []
        for b_rec in block_records:
            rank_map_flat.extend(b_rec["rank_map"])
        
        acc = 0
        for i, r in enumerate(rank_map_flat):
            acc |= (r << (i * rbits))
        rank_bytes = (len(rank_map_flat) * rbits + 7) // 8
        rank_data = acc.to_bytes(rank_bytes, "little")
        
        noff_w = bw(len(pool))
        noff_data = pk(noff, noff_w)
        dict_bytes = 2 * len(toks)
        
        total_name_bytes = len(pool) + len(noff_data) + len(rank_data) + dict_bytes
        cand = (total_name_bytes, t_cand, toks, enc_sufs, pool, noff, noff_w, rank_data, rbits, block_records)
        if best_cand is None or cand[0] < best_cand[0]:
            best_cand = cand
            
    total_name_bytes, T, toks, enc_sufs, pool, noff, noff_w, rank_data, rbits, block_records = best_cand
    addr_info = enc_b1_address(offs, B)
    
    F = dict(
        cand="BLPR", B=B, T=T, rbits=rbits,
        addr_info=addr_info,
        pool=bytes(pool), noff=pk(noff, noff_w), noff_w=noff_w,
        rank_data=rank_data, toks=toks,
        tokb=bytes(x for p in toks for x in p),
        first=d["first"], end=d["end"], offs=offs, names=names,
        n=n, nblk=nblk, block_records=block_records,
        sd=stack_need(toks, enc_sufs)
    )
    
    # Breakdown
    payload = addr_info["addr_bytes"] + len(F["noff"]) + len(rank_data) + len(F["tokb"]) + len(pool)
    F["breakdown"] = dict(
        address_index=addr_info["addr_bytes"],
        name_checkpoints=len(F["noff"]),
        rank_map=len(rank_data),
        dictionary=len(F["tokb"]),
        name_pool=len(pool),
        payload=payload
    )
    return F

# Candidate B (ALOB)
def greedy_prefix_order(names_list):
    """Greedy prefix-similarity ordering (max LCP TSP heuristic)."""
    c = len(names_list)
    if c <= 1: return list(range(c))
    unvisited = set(range(c))
    # start with first symbol in address order
    curr = 0
    order = [curr]
    unvisited.remove(curr)
    while unvisited:
        curr_name = names_list[curr]
        # find best unvisited with max LCP
        best_cand = max(unvisited, key=lambda idx: (lcp(curr_name, names_list[idx]), -len(names_list[idx])))
        order.append(best_cand)
        unvisited.remove(best_cand)
        curr = best_cand
    return order

def build_alob(d, B, T=None):
    offs, names = d["offs"], d["names"]
    n = len(names)
    nblk = (n + B - 1) // B
    rbits = bl(B - 1)
    
    # We first collect suffix candidates for token training
    # Mode 0: address order (raw names, no rank map)
    # Mode 1: lexicographic local (front coded, rank map)
    # Mode 2: greedy prefix local (front coded, rank map)
    # We can pre-train tokens on all unique names / suffixes
    # To be strictly fair, we train tokens on all names
    seqs = [list(nm.encode()) for nm in names]
    t_budgets = (0, 16, 32, 48, 64, 96, 128) if T is None else [T]
    best_cand = None
    
    for t_cand in t_budgets:
        toks, _ = train_pairs(seqs, t_cand, key=('alob_tok', hash(tuple(names))))
        
        # Helper to encode string with trained toks
        def enc_str(s_bytes):
            w = [list(s_bytes)]
            for i, (a, b) in enumerate(toks):
                w = [_apply(w[0], a, b, 0x80 + i)]
            return w[0]
        
        # Evaluate each block for Mode 0, Mode 1, Mode 2
        block_decisions = []
        mode_counts = collections.Counter()
        
        for b in range(nblk):
            st = b * B
            c = min(B, n - st)
            blk_names = names[st : st + c]
            
            # --- Mode 0: Natural Address Order (no rank map)
            # Just tokenized names: enc(nm) + \0
            m0_pool = bytearray()
            for nm in blk_names:
                m0_pool += bytes(enc_str(nm.encode())) + b"\0"
            m0_cost = len(m0_pool) # 0 bytes rank map!
            
            # --- Mode 1: Lexicographic local + front coding + rank map
            sorted_blk = sorted(enumerate(blk_names), key=lambda x: x[1])
            m1_rank_map = [0] * c
            m1_sorted_names = []
            for r, (orig_idx, nm) in enumerate(sorted_blk):
                m1_rank_map[orig_idx] = r
                m1_sorted_names.append(nm)
            m1_pool = bytearray()
            for i, s_nm in enumerate(m1_sorted_names):
                p = 0 if i == 0 else min(lcp(m1_sorted_names[i-1], s_nm), 255)
                m1_pool.append(p)
                m1_pool += bytes(enc_str(s_nm.encode()[p:])) + b"\0"
            m1_rank_bytes = (c * rbits + 7) // 8
            m1_cost = len(m1_pool) + m1_rank_bytes
            
            # --- Mode 2: Greedy Prefix local + front coding + rank map
            order = greedy_prefix_order(blk_names)
            m2_rank_map = [0] * c
            m2_ordered_names = []
            for r, orig_idx in enumerate(order):
                m2_rank_map[orig_idx] = r
                m2_ordered_names.append(blk_names[orig_idx])
            m2_pool = bytearray()
            for i, s_nm in enumerate(m2_ordered_names):
                p = 0 if i == 0 else min(lcp(m2_ordered_names[i-1], s_nm), 255)
                m2_pool.append(p)
                m2_pool += bytes(enc_str(s_nm.encode()[p:])) + b"\0"
            m2_rank_bytes = (c * rbits + 7) // 8
            m2_cost = len(m2_pool) + m2_rank_bytes
            
            # Select cheapest mode
            options = [(m0_cost, 0, m0_pool, None),
                       (m1_cost, 1, m1_pool, m1_rank_map),
                       (m2_cost, 2, m2_pool, m2_rank_map)]
            # If tie, prefer Mode 0 (simplest decode, no rank map), then Mode 1
            best_opt = min(options, key=lambda x: (x[0], x[1]))
            mode_counts[best_opt[1]] += 1
            block_decisions.append(dict(
                mode=best_opt[1], cost=best_opt[0],
                pool_data=best_opt[2], rank_map=best_opt[3], c=c
            ))
            
        # Pack final ALOB pool, rank maps, and mode bits
        # Mode bits: 2 bits per block
        mode_acc = 0
        for i, bd in enumerate(block_decisions):
            mode_acc |= (bd["mode"] << (i * 2))
        mode_bytes = (nblk * 2 + 7) // 8
        mode_data = mode_acc.to_bytes(mode_bytes, "little")
        
        # Pool & noff
        pool = bytearray()
        noff = []
        rank_stream = bytearray()
        rank_offsets = [] # offset in rank_stream for blocks using Mode 1 or 2
        
        for bd in block_decisions:
            noff.append(len(pool))
            pool += bd["pool_data"]
            if bd["mode"] in (1, 2):
                rank_offsets.append(len(rank_stream))
                c = bd["c"]
                acc = 0
                for i, r in enumerate(bd["rank_map"]):
                    acc |= (r << (i * rbits))
                b_bytes = (c * rbits + 7) // 8
                rank_stream += acc.to_bytes(b_bytes, "little")
            else:
                rank_offsets.append(0)
                
        noff_w = bw(len(pool))
        noff_data = pk(noff, noff_w)
        dict_bytes = 2 * len(toks)
        total_name_bytes = len(pool) + len(noff_data) + len(rank_stream) + len(mode_data) + dict_bytes
        cand = (total_name_bytes, t_cand, toks, pool, noff, noff_w, rank_stream, mode_data, rbits, block_decisions, mode_counts)
        if best_cand is None or cand[0] < best_cand[0]:
            best_cand = cand
            
    total_name_bytes, T, toks, pool, noff, noff_w, rank_stream, mode_data, rbits, block_decisions, mode_counts = best_cand
    addr_info = enc_b1_address(offs, B)
    
    F = dict(
        cand="ALOB", B=B, T=T, rbits=rbits,
        addr_info=addr_info,
        pool=bytes(pool), noff=pk(noff, noff_w), noff_w=noff_w,
        rank_data=bytes(rank_stream), mode_data=mode_data,
        toks=toks, tokb=bytes(x for p in toks for x in p),
        first=d["first"], end=d["end"], offs=offs, names=names,
        n=n, nblk=nblk, block_decisions=block_decisions,
        mode_counts=dict(mode_counts)
    )
    payload = addr_info["addr_bytes"] + len(F["noff"]) + len(rank_stream) + len(mode_data) + len(F["tokb"]) + len(pool)
    F["breakdown"] = dict(
        address_index=addr_info["addr_bytes"],
        name_checkpoints=len(F["noff"]),
        rank_map=len(rank_stream),
        mode_tags=len(mode_data),
        dictionary=len(F["tokb"]),
        name_pool=len(pool),
        payload=payload
    )
    return F

# Decoder expander for verification
def expand_str(toks, p_bytes, i_start):
    st = []; i = i_start
    out = bytearray()
    while True:
        if st: c = st.pop()
        else:
            c = p_bytes[i]; i += 1
            if c == 0: break
        if c >= 0x80:
            t = c - 0x80
            st.append(toks[t][1])
            st.append(toks[t][0])
            continue
        out.append(c)
    return out.decode("latin1"), i

def verify_blpr(F):
    names = F["names"]
    pool = F["pool"]
    toks = F["toks"]
    B = F["B"]
    rbits = F["rbits"]
    n = F["n"]
    nblk = F["nblk"]
    noff_w = F["noff_w"]
    
    # verify every symbol
    for idx in range(n):
        b = idx // B
        kk = idx % B
        # read rank
        # bit position in rank_data
        bit = idx * rbits
        chunk = int.from_bytes(F["rank_data"][bit >> 3 : (bit >> 3) + 4], "little")
        r = (chunk >> (bit & 7)) & ((1 << rbits) - 1)
        
        # decode from noff[b]
        noff_pos = int.from_bytes(F["noff"][b * noff_w : (b + 1) * noff_w], "little")
        p = noff_pos
        cur = ""
        for j in range(r + 1):
            lc = pool[p]; p += 1
            suf, p = expand_str(toks, pool, p)
            cur = cur[:lc] + suf
        expected = names[idx]
        assert cur == expected, f"Mismatch at idx {idx}: got {cur}, exp {expected}"
    return True

def verify_alob(F):
    names = F["names"]
    pool = F["pool"]
    toks = F["toks"]
    B = F["B"]
    rbits = F["rbits"]
    n = F["n"]
    nblk = F["nblk"]
    noff_w = F["noff_w"]
    
    # rank offset per block
    rank_pos = 0
    rank_block_pos = []
    for bd in F["block_decisions"]:
        rank_block_pos.append(rank_pos)
        if bd["mode"] in (1, 2):
            c = bd["c"]
            rank_pos += (c * rbits + 7) // 8
            
    for idx in range(n):
        b = idx // B
        kk = idx % B
        # read mode from mode_data
        mode_bit = b * 2
        mode_byte = F["mode_data"][mode_bit >> 3]
        mode = (mode_byte >> (mode_bit & 7)) & 3
        
        noff_pos = int.from_bytes(F["noff"][b * noff_w : (b + 1) * noff_w], "little")
        p = noff_pos
        
        if mode == 0:
            # Mode 0: sequential scan kk entries
            for _ in range(kk):
                while pool[p] != 0: p += 1
                p += 1
            cur, _ = expand_str(toks, pool, p)
        else:
            # Mode 1 or 2: read local rank
            b_rpos = rank_block_pos[b]
            bit = kk * rbits
            chunk = int.from_bytes(F["rank_data"][b_rpos + (bit >> 3) : b_rpos + (bit >> 3) + 4], "little")
            r = (chunk >> (bit & 7)) & ((1 << rbits) - 1)
            cur = ""
            for j in range(r + 1):
                lc = pool[p]; p += 1
                suf, p = expand_str(toks, pool, p)
                cur = cur[:lc] + suf
        expected = names[idx]
        assert cur == expected, f"Mismatch at idx {idx}: got {cur}, exp {expected}"
    return True

def run():
    print("=" * 80)
    print("STAGE 1: OFFLINE SCREENING — CANDIDATE A (BLPR) & CANDIDATE B (ALOB)")
    print("=" * 80)
    
    # Load authoritative baselines from research/metis_arch/results/normalized_results.json
    norm = json.load(open(f"{ROOT}/research/metis_arch/results/normalized_results.json"))
    base_rows = {(r["workload"], r["tag"]): r for r in norm}
    
    summary = {}
    
    for wl in FILES:
        d = load(wl)
        n = len(d["names"])
        print(f"\n>>> Workload: {wl} (N={n})")
        
        # Existing baselines
        b1_tok = base_rows[(wl, "B1_B16_TOK")]
        b1_fc16 = base_rows[(wl, "B1_B16_FC16")]
        print(f"  [Baseline] B1_B16_TOK : payload = {b1_tok['payload']} B (addr={b1_tok['addr_index']} B, name_cp={b1_tok['name_ckpt']} B, tok_dict={b1_tok['dictionary']} B, pool={b1_tok['name_pool']} B, ids=0 B)")
        print(f"  [Baseline] B1_B16_FC16: payload = {b1_fc16['payload']} B (addr={b1_fc16['addr_index']} B, name_cp={b1_fc16['name_ckpt']} B, tok_dict={b1_fc16['dictionary']} B, pool={b1_fc16['name_pool']} B, ids={b1_fc16['ids']} B)")
        
        wl_summary = {
            "N": n,
            "B1_B16_TOK_payload": b1_tok["payload"],
            "B1_B16_FC16_payload": b1_fc16["payload"],
            "BLPR": {},
            "ALOB": {}
        }
        
        # Test BLPR B=8, 16, 32
        for B in (8, 16, 32):
            F = build_blpr(d, B)
            ok = verify_blpr(F)
            bd = F["breakdown"]
            diff_tok = bd["payload"] - b1_tok["payload"]
            diff_fc16 = bd["payload"] - b1_fc16["payload"]
            print(f"  BLPR B={B:2d} (T={F['T']:2d}, rbits={F['rbits']}): payload={bd['payload']:5d} B | pool={bd['name_pool']:4d} B, rank_map={bd['rank_map']:4d} B, dict={bd['dictionary']:3d} B, cp={bd['name_checkpoints']:3d} B, addr={bd['address_index']:4d} B | vs B1_FC16: {diff_fc16:+5d} B | vs B1_TOK: {diff_tok:+5d} B [verified: {ok}]")
            wl_summary["BLPR"][f"B{B}"] = dict(
                payload=bd["payload"],
                diff_vs_b1_tok=diff_tok,
                diff_vs_b1_fc16=diff_fc16,
                breakdown=bd,
                T=F["T"],
                rbits=F["rbits"]
            )
            
        # Test ALOB B=8, 16, 32
        for B in (8, 16, 32):
            F = build_alob(d, B)
            ok = verify_alob(F)
            bd = F["breakdown"]
            diff_tok = bd["payload"] - b1_tok["payload"]
            diff_fc16 = bd["payload"] - b1_fc16["payload"]
            print(f"  ALOB B={B:2d} (T={F['T']:2d}): payload={bd['payload']:5d} B | pool={bd['name_pool']:4d} B, rank_map={bd['rank_map']:4d} B, modes={F['mode_counts']}, mode_tags={bd['mode_tags']:2d} B | vs B1_FC16: {diff_fc16:+5d} B | vs B1_TOK: {diff_tok:+5d} B [verified: {ok}]")
            wl_summary["ALOB"][f"B{B}"] = dict(
                payload=bd["payload"],
                diff_vs_b1_tok=diff_tok,
                diff_vs_b1_fc16=diff_fc16,
                breakdown=bd,
                T=F["T"],
                mode_counts=F["mode_counts"]
            )
            
        summary[wl] = wl_summary
        
    with open(f"/tmp/claude-1000/-home-adhyan-Desktop-Compiler/2b5f63fc-9d99-4008-a7f8-16d65b7ecb47/scratchpad/offline_screen_ab.repro.json", "w") as f:
        json.dump(summary, f, indent=2)

if __name__ == "__main__":
    run()
