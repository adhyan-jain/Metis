#!/usr/bin/env python3
"""Emit a specialized C lookup (normalized API + the closure harness consumer) for a metis_arch format F.
Only decoder branches for codecs actually present in the format are emitted."""

def arr(name, data, typ="uint8_t"):
    if not data: return f"/* {name}: empty */\n"
    rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 16]) for i in range(0, len(data), 16)]
    return f"static const {typ} {name}[{len(data)}] = {{\n\t" + ",\n\t".join(rows) + ",\n};\n"

def arr32(name, data):
    vals = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]
    rows = [", ".join(f"0x{v:x}u" for v in vals[i:i + 8]) for i in range(0, len(vals), 8)]
    return f"static const uint32_t {name}[{len(vals)}] = {{\n\t" + ",\n\t".join(rows) + ",\n};\n"

RD = """static inline uint32_t rdw(const uint8_t *p, unsigned w) {
	uint32_t v = p[0];
	if (w > 1) v |= (uint32_t)p[1] << 8;
	if (w > 2) v |= (uint32_t)p[2] << 16;
	if (w > 3) v |= (uint32_t)p[3] << 24;
	return v;
}
"""

def gen(F, comment=""):
    k, nk, n, B = F["kind"], F["nk"], F["n"], F["B"]; blocks = F["blocks"]; nb = len(blocks)
    c = [f"/* METIS generated: kind={k} names={nk} B={B} {comment} */",
         "#include <stdint.h>", "#include <stddef.h>", RD,
         f"#define FIRST_ADDR 0x{F['first']:x}u", f"#define NSYM {n}u", f"#define NBLK {nb}u",
         f"#define END_O {F['end']}u", f"#define BSZ {B}u"]
    ntok = len(F["toks"])
    if k == "RAW32":
        c.append(arr32("raw_off", F["raw"]))
    else:
        c.append(arr("dir_base", F["base"])); c.append(arr("dir_am", F["am"])); c.append(arr("addr_stream", F["stream"]))
    if nk == "KALL": c.append(arr("kt_index", F["kidx"], "uint8_t")); c.append(arr("kt_table", F["ktab"])); c.append(arr("dir_noff", F["noff"]))
    elif nk == "TOK": c.append(arr("dir_noff", F["noff"]))
    else:
        c.append(arr("dir_nbo", F["nbo"])); c.append(arr("sym_ids", F["ids"]))
        if "sidx" in F: c.append(arr("dir_sidx", F["sidx"]))
    c.append(arr("name_pool", F["pool"]))
    if ntok: c.append(arr("tok_tab", F["tokb"]))
    if nk == "KALL":
        c.append("""static size_t xp(const uint8_t **pp, char *out, size_t cap, size_t pos) {
	const uint8_t *p = *pp; unsigned len = *p++;
	while (len--) { unsigned t = *p++; const uint8_t *tp = &kt_table[kt_index[2 * t] | (kt_index[2 * t + 1] << 8)];
		while (*tp) { if (pos + 1 < cap) out[pos] = (char)*tp; pos++; tp++; } }
	*pp = p; return pos;
}""")
    elif ntok:
        c.append(f"""static size_t xp(const uint8_t **pp, char *out, size_t cap, size_t pos) {{
	const uint8_t *p = *pp; uint8_t st[{F['sd']}]; unsigned sp = 0;
	for (;;) {{
		uint8_t ch;
		if (sp) ch = st[--sp]; else {{ ch = *p++; if (!ch) break; }}
		if (ch >= 0x80) {{ const uint8_t *t = &tok_tab[(ch - 0x80) * 2]; st[sp++] = t[1]; st[sp++] = t[0]; continue; }}
		if (pos + 1 < cap) out[pos] = (char)ch;
		pos++;
	}}
	*pp = p; return pos;
}}""")
    else:
        c.append("""static size_t xp(const uint8_t **pp, char *out, size_t cap, size_t pos) {
	const uint8_t *p = *pp; uint8_t ch;
	while ((ch = *p++) != 0) { if (pos + 1 < cap) out[pos] = (char)ch; pos++; }
	*pp = p; return pos;
}""")
    L = ["int lookup_symbol(uintptr_t addr, char *output_buffer, size_t capacity, uint32_t *offset_out) {",
         "\tif (capacity == 0 || output_buffer == NULL) return -1;",
         "\tif (addr < FIRST_ADDR) goto nf;",
         "\tuint32_t rel = (uint32_t)(addr - FIRST_ADDR);",
         "\tif ((rel >> 1) >= END_O) goto nf;",
         "\tconst uint8_t *p; uint32_t kk, symrel, b, s;"]
    if k == "RAW32":
        L += ["\tuint32_t lo = 0, hi = NSYM;",
              "\twhile (lo < hi) { uint32_t m = (lo + hi) >> 1; if (raw_off[m] <= rel) lo = m + 1; else hi = m; }",
              "\ts = lo - 1; symrel = raw_off[s]; b = s / BSZ; kk = s % BSZ;"]
    else:
        bwid, amw, nbits, cbits, cids = F["base_w"], F["am_w"], F["nbits"], F["cbits"], F["cids"]
        L += ["\tuint32_t o = rel >> 1, lo = 0, hi = NBLK;",
              f"\twhile (lo < hi) {{ uint32_t m = (lo + hi) >> 1; if (rdw(&dir_base[m * {bwid}], {bwid}) <= o) lo = m + 1; else hi = m; }}",
              f"\tb = lo - 1; uint32_t base = rdw(&dir_base[b * {bwid}], {bwid});",
              f"\tuint32_t am = rdw(&dir_am[b * {amw}], {amw}), ol = o - base, cnt, loc = 0;"]
        L.append("\tcnt = (am & 15u) + 1u;" if nbits else
                 "\tcnt = (b == NBLK - 1u) ? (NSYM - (NBLK - 1u) * BSZ) : BSZ;")
        L.append(f"\tam >>= {nbits};")
        L.append(f"\tunsigned ci = {'am & ' + str((1 << cbits) - 1) + 'u' if cbits else '0'}; const uint8_t *sp_ = &addr_stream[am >> {cbits}];")
        L.append("\tunsigned kind, w; kk = 0;")
        L.append("\tswitch (ci) {")
        kinds = set()
        for i, cn in enumerate(cids):
            if cn in ("W1", "W2"): L.append(f"\tcase {i}: kind = 0; w = {cn[1]}; break;"); kinds.add(0)
            elif cn == "VAR": L.append(f"\tcase {i}: kind = 1; w = 0; break;"); kinds.add(1)
            else: L.append(f"\tcase {i}: kind = 2; w = {cn[2:]}; break;"); kinds.add(2)
        L.append("\tdefault: kind = 0; w = 1; break;\n\t}")
        parts = {}
        parts[0] = """{ uint32_t l = 0, h = cnt;
		while (h - l > 1) { uint32_t m = (l + h) >> 1; const uint8_t *q = &sp_[(m - 1) * w];
			uint32_t v = q[0]; if (w > 1) v |= (uint32_t)q[1] << 8;
			if (v <= ol) l = m; else h = m; }
		kk = l; if (l) { const uint8_t *q = &sp_[(l - 1) * w]; loc = q[0]; if (w > 1) loc |= (uint32_t)q[1] << 8; } }"""
        parts[1] = """{ const uint8_t *q = sp_;
		while (kk < cnt - 1u) { uint32_t v = 0; unsigned sh = 0; uint8_t ch;
			do { ch = *q++; v |= (uint32_t)(ch & 127u) << sh; sh += 7; } while (ch & 128u);
			if (loc + v > ol) break; loc += v; kk++; } }"""
        parts[2] = """{ uint32_t bit = 0, mask = (1u << w) - 1u;
		while (kk < cnt - 1u) { const uint8_t *q = &sp_[bit >> 3];
			uint32_t v = ((q[0] | (uint32_t)q[1] << 8 | (uint32_t)q[2] << 16) >> (bit & 7u)) & mask;
			if (loc + v > ol) break; loc += v; kk++; bit += w; } }"""
        ks = sorted(kinds)
        if len(ks) == 1: L.append("\t" + parts[ks[0]])
        else:
            L.append("\t" + "\n\telse ".join(f"if (kind == {kd}) " + parts[kd] for kd in ks))
        L.append("\tsymrel = (base + loc) << 1; s = " + (f"rdw(&dir_sidx[b * {F['sidx_w']}], {F['sidx_w']})" if "sidx" in F else "b * BSZ") + " + kk;")
    if nk == "KALL":
        L += [f"\tp = &name_pool[rdw(&dir_noff[b * {F['noff_w']}], {F['noff_w']})];",
              "\twhile (kk--) p += *p + 1;", "\tsize_t full = xp(&p, output_buffer, capacity, 0);"]
    elif nk == "TOK":
        L += [f"\tp = &name_pool[rdw(&dir_noff[b * {F['noff_w']}], {F['noff_w']})];",
              "\twhile (kk--) { while (*p++) { } }", "\tsize_t full = xp(&p, output_buffer, capacity, 0);"]
    else:
        im, idb, Bn = F["idm"], F["idb"], F["Bn"]
        if im == "u8": L.append("\tuint32_t id = sym_ids[s];")
        elif im == "u16": L.append("\tuint32_t id = sym_ids[2 * s] | (uint32_t)sym_ids[2 * s + 1] << 8;")
        else: L.append(f"\tuint32_t id; {{ uint32_t bit = s * {idb}u; const uint8_t *q = &sym_ids[bit >> 3];\n\t\tid = ((q[0] | (uint32_t)q[1] << 8 | (uint32_t)q[2] << 16) >> (bit & 7u)) & {(1 << idb) - 1}u; }}")
        L += [f"\tp = &name_pool[rdw(&dir_nbo[(id / {Bn}u) * {F['nbo_w']}], {F['nbo_w']})];",
              f"\tsize_t full = 0; for (uint32_t j = id % {Bn}u + 1u; j; j--) {{ uint32_t lc = *p++; full = xp(&p, output_buffer, capacity, lc); }}"]
    L += ["\tif (full + 1 <= capacity) output_buffer[full] = '\\0'; else output_buffer[capacity - 1] = '\\0';",
          "\tif (offset_out != NULL) *offset_out = rel - symrel;",
          "\treturn (capacity >= full + 1) ? 0 : 2;",
          "nf:\n\tif (offset_out) *offset_out = 0;",
          "\tif (capacity >= 2) { output_buffer[0] = '?'; output_buffer[1] = '\\0'; } else { output_buffer[0] = '\\0'; }",
          "\treturn 1;", "}", "",
          "__attribute__((used, noinline))",
          "void zephyr_fault_symbol_logger(uintptr_t fault_pc, char *log_buf, size_t buf_cap, uint32_t *offset_out) {",
          "\tlookup_symbol(fault_pc, log_buf, buf_cap, offset_out);", "}"]
    return "\n".join(c) + "\n" + "\n".join(L) + "\n"
