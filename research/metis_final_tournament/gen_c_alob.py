#!/usr/bin/env python3
"""Emit specialized C lookup for Candidate B (ALOB).
Supports only the modes actually present across the blocks.
"""

def arr(name, data, typ="uint8_t"):
    if not data: return f"/* {name}: empty */\n"
    rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 16]) for i in range(0, len(data), 16)]
    return f"static const {typ} {name}[{len(data)}] = {{\n\t" + ",\n\t".join(rows) + ",\n};\n"

RD = """static inline uint32_t rdw(const uint8_t *p, unsigned w) {
\tuint32_t v = p[0];
\tif (w > 1) v |= (uint32_t)p[1] << 8;
\tif (w > 2) v |= (uint32_t)p[2] << 16;
\tif (w > 3) v |= (uint32_t)p[3] << 24;
\treturn v;
}
"""

def gen_alob(F, comment=""):
    B, n, rbits = F["B"], F["n"], F["rbits"]
    addr_info = F["addr_info"]
    nb = addr_info["nblk"]
    bwid, amw = addr_info["base_w"], addr_info["am_w"]
    noff_w = F["noff_w"]
    ntok = len(F["toks"])
    mode_counts = F["mode_counts"]
    has_mode0 = mode_counts.get(0, 0) > 0
    has_perm = (mode_counts.get(1, 0) + mode_counts.get(2, 0)) > 0
    
    # Calculate rank block offsets for blocks using Mode 1 or 2
    rank_pos = 0
    rank_block_offsets = []
    for bd in F["block_decisions"]:
        rank_block_offsets.append(rank_pos)
        if bd["mode"] in (1, 2):
            c = bd["c"]
            rank_pos += (c * rbits + 7) // 8
    ro_w = max(1, (max(rank_block_offsets + [1]).bit_length() + 7) // 8) if has_perm else 0
    ro_bytes = b"".join(int(v).to_bytes(ro_w, "little") for v in rank_block_offsets) if has_perm else b""

    c = [
        f"/* METIS Candidate B (ALOB): B={B} modes={mode_counts} {comment} */",
        "#include <stdint.h>", "#include <stddef.h>", RD,
        f"#define FIRST_ADDR 0x{F['first']:x}u",
        f"#define NSYM {n}u",
        f"#define NBLK {nb}u",
        f"#define END_O {F['end']}u",
        f"#define BSZ {B}u",
        f"#define RBITS {rbits}u",
        f"#define BASE_W {bwid}u",
        f"#define AM_W {amw}u",
        f"#define NOFF_W {noff_w}u",
        f"#define RO_W {ro_w}u",
        f"#define STACK_DEPTH 12u"
    ]
    
    c.append(arr("dir_base", addr_info["base"]))
    c.append(arr("dir_am", addr_info["am"]))
    c.append(arr("addr_stream", addr_info["stream"]))
    c.append(arr("dir_noff", F["noff"]))
    c.append(arr("mode_data", F["mode_data"]))
    if has_perm:
        c.append(arr("rank_offsets", ro_bytes))
        c.append(arr("local_ranks", F["rank_data"]))
    c.append(arr("name_pool", F["pool"]))
    if ntok:
        c.append(arr("tok_tab", F["tokb"]))
        c.append("""static size_t xp(const uint8_t **pp, char *out, size_t cap, size_t pos) {
\tconst uint8_t *p = *pp; uint8_t st[STACK_DEPTH]; unsigned sp = 0;
\tfor (;;) {
\t\tuint8_t ch;
\t\tif (sp) ch = st[--sp]; else { ch = *p++; if (!ch) break; }
\t\tif (ch >= 0x80) { const uint8_t *t = &tok_tab[(ch - 0x80) * 2]; st[sp++] = t[1]; st[sp++] = t[0]; continue; }
\t\tif (pos + 1 < cap) out[pos] = (char)ch;
\t\tpos++;
\t}
\t*pp = p; return pos;
}""")
    else:
        c.append("""static size_t xp(const uint8_t **pp, char *out, size_t cap, size_t pos) {
\tconst uint8_t *p = *pp; uint8_t ch;
\twhile ((ch = *p++) != 0) { if (pos + 1 < cap) out[pos] = (char)ch; pos++; }
\t*pp = p; return pos;
}""")

    L = [
        "int lookup_symbol(uintptr_t addr, char *output_buffer, size_t capacity, uint32_t *offset_out) {",
        "\tif (capacity == 0 || output_buffer == NULL) return -1;",
        "\tif (addr < FIRST_ADDR) goto nf;",
        "\tuint32_t rel = (uint32_t)(addr - FIRST_ADDR);",
        "\tif ((rel >> 1) >= END_O) goto nf;",
        "\tuint32_t o = rel >> 1, lo = 0, hi = NBLK;",
        "\twhile (lo < hi) { uint32_t m = (lo + hi) >> 1; if (rdw(&dir_base[m * BASE_W], BASE_W) <= o) lo = m + 1; else hi = m; }",
        "\tuint32_t b = lo - 1; uint32_t base = rdw(&dir_base[b * BASE_W], BASE_W);",
        "\tuint32_t ao = rdw(&dir_am[b * AM_W], AM_W);",
        "\tuint32_t ol = o - base, loc = 0, kk = 0;",
        "\tuint32_t cnt = (b == NBLK - 1u) ? (NSYM - (NBLK - 1u) * BSZ) : BSZ;",
        "\tconst uint8_t *q = &addr_stream[ao];",
        "\twhile (kk < cnt - 1u) {",
        "\t\tuint32_t v = 0; unsigned sh = 0; uint8_t ch;",
        "\t\tdo { ch = *q++; v |= (uint32_t)(ch & 127u) << sh; sh += 7; } while (ch & 128u);",
        "\t\tif (loc + v > ol) break;",
        "\t\tloc += v; kk++;",
        "\t}",
        "\tuint32_t symrel = (base + loc) << 1;",
        "\tconst uint8_t *p = &name_pool[rdw(&dir_noff[b * NOFF_W], NOFF_W)];",
        "\tsize_t full = 0;",
        "\tunsigned mode_bit = b * 2u; unsigned mode = (mode_data[mode_bit >> 3] >> (mode_bit & 7u)) & 3u;"
    ]
    
    if has_mode0 and has_perm:
        L += [
            "\tif (mode == 0) {",
            "\t\twhile (kk--) { while (*p++) {} }",
            "\t\tfull = xp(&p, output_buffer, capacity, 0);",
            "\t} else {",
            "\t\tuint32_t r_base = rdw(&rank_offsets[b * RO_W], RO_W);",
            "\t\tuint32_t bit = kk * RBITS; const uint8_t *rkp = &local_ranks[r_base + (bit >> 3)];",
            f"\t\tuint32_t r = ((rkp[0] | ((uint32_t)rkp[1] << 8)) >> (bit & 7u)) & {((1 << rbits) - 1)}u;",
            "\t\tfor (uint32_t j = 0; j <= r; j++) { uint32_t lc = *p++; full = xp(&p, output_buffer, capacity, lc); }",
            "\t}"
        ]
    elif has_mode0:
        L += [
            "\twhile (kk--) { while (*p++) {} }",
            "\tfull = xp(&p, output_buffer, capacity, 0);"
        ]
    else:
        L += [
            "\tuint32_t r_base = rdw(&rank_offsets[b * RO_W], RO_W);",
            "\tuint32_t bit = kk * RBITS; const uint8_t *rkp = &local_ranks[r_base + (bit >> 3)];",
            f"\tuint32_t r = ((rkp[0] | ((uint32_t)rkp[1] << 8)) >> (bit & 7u)) & {((1 << rbits) - 1)}u;",
            "\tfor (uint32_t j = 0; j <= r; j++) { uint32_t lc = *p++; full = xp(&p, output_buffer, capacity, lc); }"
        ]

    L += [
        "\tif (full + 1 <= capacity) output_buffer[full] = '\\0'; else output_buffer[capacity - 1] = '\\0';",
        "\tif (offset_out != NULL) *offset_out = rel - symrel;",
        "\treturn (capacity >= full + 1) ? 0 : 2;",
        "nf:",
        "\tif (offset_out) *offset_out = 0;",
        "\tif (capacity >= 2) { output_buffer[0] = '?'; output_buffer[1] = '\\0'; } else { output_buffer[0] = '\\0'; }",
        "\treturn 1;",
        "}",
        "",
        "__attribute__((used, noinline))",
        "void zephyr_fault_symbol_logger(uintptr_t fault_pc, char *log_buf, size_t buf_cap, uint32_t *offset_out) {",
        "\tlookup_symbol(fault_pc, log_buf, buf_cap, offset_out);",
        "}"
    ]
    return "\n".join(c) + "\n" + "\n".join(L) + "\n"
