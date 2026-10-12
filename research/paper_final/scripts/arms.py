"""Arm definitions for the gap-fill: every compared symbolization scheme, as generated C + metadata.

Nothing under research/metis_arch, research/metis_final_tournament or scripts/ is modified: the historical encoders and
C generators are imported and reused unchanged; only additions (FSST name coding, the legacy-scheme wrapper) live here.

Arm taxonomy (frozen in GAP_FILL_PROTOCOL.md):
  STOCK          Zephyr's own generated table (scripts/build/gen_symtab.py) + stock lookup, reachable through a consumer
  EXISTING       KALL256, KALL16 (Linux kallsyms-style, marker interval 256 / 16), CAND_DIRECT, CAND_CP16 (project's earlier
                 64/128-token designs with raw u32 offsets)
  HARDENED       B1/B2 compact address index (blocks of 16) + conventional names (pair tokens 'TOK' or kallsyms 'KALL')
  CONTROL        RAW32 (u32 offsets) + same token / kallsyms names
  PROPOSED       B1/B2 address index + checkpointed front-coded dictionary names (FC16)
  FSST           B1 address index + reference-FSST-coded names
"""
import os, re, subprocess, sys

ROOT = "/home/adhyan/Desktop/Compiler"
PF = f"{ROOT}/research/paper_final"
for p in (ROOT, f"{ROOT}/research/metis_arch"):
    if p not in sys.path:
        sys.path.insert(0, p)
import metis_arch as M          # historical encoders + reference decoder (unchanged)
import gen_c as G              # historical C generator (unchanged)
import test_roundtrip as T     # historical query set + oracle (unchanged)

FSST_BIN = f"{PF}/work/bin/fsst_names"

EXISTING = ["KALL256", "KALL16", "CAND_DIRECT", "CAND_CP16"]
HARDENED = ["B1_B16_TOK", "B1_B16_KALL", "B2_B16_TOK", "B2_B16_KALL"]
CONTROLS = ["RAW32_B16_TOK", "RAW32_B16_KALL"]
PROPOSED = ["B1_B16_FC16", "B2_B16_FC16"]
FSSTARMS = ["B1_B16_FSST"]
ALL_ARMS = ["STOCK"] + EXISTING + HARDENED + CONTROLS + PROPOSED + FSSTARMS


def family(arm):
    for fam, names in (("stock", ["STOCK"]), ("existing", EXISTING), ("hardened", HARDENED), ("control", CONTROLS),
                       ("proposed", PROPOSED), ("fsst", FSSTARMS)):
        if arm in names:
            return fam
    raise KeyError(arm)


def legacy_entries(d):
    """entry list (incl. sentinel) in the layout of data/zephyr_symtab_*.json used by the historical generators"""
    first = d["first"]
    ents = [dict(name=n, addr=a, offset=a - first) for n, a in zip(d["names"], d["addrs"])]
    ents.append(dict(name="?", addr=first + 2 * d["end"], offset=2 * d["end"]))
    return ents


WRAP = """
int lookup_symbol(uintptr_t addr, char *output_buffer, size_t capacity, uint32_t *offset_out) {
	return %s_lookup(addr, output_buffer, capacity, offset_out);
}

__attribute__((used, noinline))
void zephyr_fault_symbol_logger(uintptr_t fault_pc, char *log_buf, size_t buf_cap, uint32_t *offset_out) {
	lookup_symbol(fault_pc, log_buf, buf_cap, offset_out);
}
"""


def _legacy_c(arm, d):
    from scripts.kallsyms_generator import build_kallsyms_representation, generate_kallsyms_c
    import importlib
    ents = legacy_entries(d)
    if arm in ("KALL256", "KALL16"):
        mi = 256 if arm == "KALL256" else 16
        pre = f"kallsyms_m{mi}"
        kd = build_kallsyms_representation(ents, marker_interval=mi)
        return generate_kallsyms_c(kd, prefix=pre) + WRAP % pre, dict(prefix=pre)
    # CAND_*: the project's earlier token design; budget rule frozen in the protocol: 64 tokens if N < 300 else 128
    fam = importlib.import_module("scripts.fairness_audit_master")
    from scripts.zephyr_symtab_compressor import learn_dictionary
    budget = 64 if len(d["names"]) < 300 else 128
    names = [e["name"] for e in ents]
    dic, _ = learn_dictionary(names, budget)
    enc = fam.encode_names_candidate(names, dic)
    cp = arm == "CAND_CP16"
    pre = "cand_cp16" if cp else "cand_direct"
    code, stats = fam.generate_candidate_c(ents, dic, enc, use_checkpoints=cp, checkpoint_interval=16, prefix=pre)
    return code + WRAP % pre, dict(prefix=pre, budget=budget)


def _fsst_run(names):
    r = subprocess.run([FSST_BIN], input="\n".join(names) + "\n", capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(f"fsst_names failed rc={r.returncode}: {r.stderr[:300]}")
    syms, codes = [], []
    for l in r.stdout.splitlines():
        if l[0] == "S":
            _, ln, hx = l.split(" ")
            syms.append(bytes.fromhex(hx)); assert len(syms[-1]) == int(ln)
        else:
            codes.append(bytes.fromhex(l.split(" ")[1]) if " " in l else b"")
    return syms, codes


def fsst_py_decode(F, i_block_start, kk):
    """decode name using ONLY emitted bytes (symbol table + pool), used by the Python-side FSST self-check"""
    tab = F["fsst_tab"]; n = tab[0]; lens = tab[1:1 + n]; sb = tab[1 + n:]
    offs = [0]
    for x in lens[:-1]:
        offs.append(offs[-1] + x)
    pool = F["pool"]; p = i_block_start
    for _ in range(kk):
        p += pool[p] + 1
    ln = pool[p]; p += 1; out = bytearray(); e = p + ln
    while p < e:
        c = pool[p]; p += 1
        if c == 255:
            out.append(pool[p]); p += 1
        else:
            out += sb[offs[c]:offs[c] + lens[c]]
    return bytes(out)


def build_fsst(d, kind="B1", B=16):
    F = M.build(d, kind, B, "TOK", T=0)       # address side + block directory; names replaced below
    syms, codes = _fsst_run(d["names"])
    recs = []
    for c in codes:
        assert len(c) <= 255
        recs.append(bytes([len(c)]) + c)
    starts, pos = [], 0
    for r in recs:
        starts.append(pos); pos += len(r)
    F["pool"] = b"".join(recs)
    nw = M.bw(max(starts[b.st] for b in F["blocks"]))
    F["noff_w"] = nw; F["noff"] = M.pk([starts[b.st] for b in F["blocks"]], nw)
    assert len(syms) <= 255 and all(1 <= len(s) <= 8 for s in syms)
    # compact serialisation: nsym, len[nsym], concatenated symbol bytes (decoder walks the prefix sums)
    F["fsst_tab"] = bytes([len(syms)]) + bytes(len(s) for s in syms) + b"".join(syms)
    # sensitivity layouts (bytes only): reference-style 8-byte slots + len byte; 16-bit offsets
    F["fsst_tab_alt"] = dict(ref_layout=len(syms) * 9 + 1, offset16=len(F["fsst_tab"]) + 2 * len(syms))
    F["nk"] = "FSST"; F["toks"] = []; F["tokb"] = b""; F["sd"] = 0; F["T"] = 0
    # python self-check: every name round-trips from emitted bytes only
    for i, nm in enumerate(d["names"]):
        b = next(j for j, blk in enumerate(F["blocks"]) if blk.st <= i < blk.st + blk.c)
        st = M.rd(F["noff"], b, F["noff_w"])
        got = fsst_py_decode(F, st, i - F["blocks"][b].st)
        assert got == nm.encode(), (i, nm, got)
    return F


XP_FSST = """static size_t xp(const uint8_t **pp, char *out, size_t cap, size_t pos) {
	const uint8_t *p = *pp, *e; unsigned n = *p++; e = p + n;
	while (p < e) {
		unsigned c = *p++;
		if (c == 255u) { if (pos + 1 < cap) out[pos] = (char)*p; p++; pos++; continue; }
		const uint8_t *len = &fsst_tab[1], *sym = &fsst_tab[1 + fsst_tab[0]];
		for (unsigned k = 0; k < c; k++) sym += len[k];
		for (unsigned k = 0; k < len[c]; k++) { if (pos + 1 < cap) out[pos] = (char)sym[k]; pos++; }
	}
	*pp = e; return pos;
}"""


def fsst_c(F):
    F2 = dict(F, nk="TOK", toks=[], sd=0)
    src = G.gen(F2)
    old_xp = re.search(r"static size_t xp\(const uint8_t \*\*pp.*?\n\}", src, re.S).group(0)
    assert src.count(old_xp) == 1
    src = src.replace(old_xp, G.arr("fsst_tab", F["fsst_tab"]) + XP_FSST)
    skip = "\twhile (kk--) { while (*p++) { } }"
    assert src.count(skip) == 1
    return src.replace(skip, "\twhile (kk--) p += *p + 1;")


def parse_tag(arm):
    kind, b, nk = arm.split("_")
    return kind, int(b[1:]), nk


def make(arm, d):
    """-> dict(arm, c, F|None, payload|None, meta)"""
    if arm in EXISTING:
        c, meta = _legacy_c(arm, d)
        return dict(arm=arm, c=c, F=None, payload=None, meta=meta)
    if arm in FSSTARMS:
        kind, B, _ = parse_tag(arm)
        F = build_fsst(d, kind, B)
        bd = M.breakdown(F); bd["dictionary"] += len(F["fsst_tab"]); bd["payload"] += len(F["fsst_tab"])
        return dict(arm=arm, c=fsst_c(F), F=F, payload=bd, meta=dict(fsst_symbols=F["fsst_tab"][0],
                    fsst_table_bytes=len(F["fsst_tab"]), alt=F["fsst_tab_alt"]))
    kind, B, nk = parse_tag(arm)
    Bn = 16
    if nk.startswith("FC"):
        Bn = int(nk[2:]); nk = "FC"
    F = M.build(d, kind, B, nk, Bn=Bn)
    return dict(arm=arm, c=G.gen(F), F=F, payload=M.breakdown(F),
                meta=dict(tokens=F["T"], blocks=len(F["blocks"])))


# ---------------------------------------------------------------- host-side decoder test (generated C, real decoder)
DRV = r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
int lookup_symbol(uintptr_t, char *, size_t, uint32_t *);
int main(int argc, char **argv) { unsigned long a; unsigned cap = atoi(argv[1]); char buf[512];
  while (scanf("%lu", &a) == 1) { uint32_t off = 0xdeadbeef; int rc = lookup_symbol((uintptr_t)a, buf, cap, &off); printf("%d|%s|%u\n", rc, buf, off); } return 0; }
'''


def expect(d, a, cap, truncation_contract):
    o = T.oracle(d, a)
    if o is None:
        return "1|?|0" if cap >= 2 else "1||0"
    nm, off = o
    if len(nm) + 1 <= cap:
        return f"0|{nm}|{off}"
    return f"2|{nm[:cap - 1]}|{off}" if truncation_contract else None


def host_check(arm_rec, d, tmp, caps=(256, 12)):
    """compile the generated C with the host gcc and compare against the manifest oracle. Returns dict cap -> mismatches.
    Gate-relevant: cap=256 (every name fits).  cap=12 (truncation contract) is checked for arms whose contract defines it."""
    src = os.path.join(tmp, "g.c")
    open(src, "w").write(arm_rec["c"])
    open(os.path.join(tmp, "d.c"), "w").write(DRV)
    exe = os.path.join(tmp, "t")
    r = subprocess.run(["gcc", "-O1", "-Wall", "-Wno-unused-function", "-Wno-unused-variable", src, os.path.join(tmp, "d.c"), "-o", exe],
                       capture_output=True, text=True)
    if r.returncode:
        return dict(error=r.stderr[:1500])
    qs = T.queries(d)
    res = {}
    trunc = arm_rec["arm"] not in EXISTING
    for cap in caps:
        out = subprocess.run([exe, str(cap)], input="\n".join(map(str, qs)), capture_output=True, text=True).stdout.splitlines()
        if len(out) != len(qs):
            res[cap] = dict(mismatches=len(qs), queries=len(qs), note="driver crashed"); continue
        bad = 0
        for a, o in zip(qs, out):
            e = expect(d, a, cap, trunc)
            if e is None:           # legacy arms: truncation semantics not part of their contract
                continue
            if not trunc and T.oracle(d, a) is None:
                # legacy arms: out-of-range probes must resolve to the name "?" (their rc/offset convention differs)
                if o.split("|")[1] != "?":
                    bad += 1
                continue
            if o != e:
                bad += 1
        res[cap] = dict(mismatches=bad, queries=len(qs), contract="full" if trunc else "untruncated-only")
    return res
