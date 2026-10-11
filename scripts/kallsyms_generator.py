#!/usr/bin/env python3
"""
Faithful adaptation of Linux kernel kallsyms for Zephyr runtime symbolization.

Architecture:
  - kallsyms_names: concatenated length-prefixed tokenized symbols:
      [uint8_t len] [byte_0] ... [byte_{len-1}]
  - kallsyms_markers: offsets into kallsyms_names at every MARKER_INTERVAL symbols.
  - kallsyms_token_table: packed string pool containing 256 null-terminated token strings.
  - kallsyms_token_index: 256 uint16_t offsets into kallsyms_token_table for bytes 0..255.
  - kallsyms_offsets: relative address offsets (addr - base_addr).
  - kallsyms_lookup: binary search on offsets, jump via marker, linear scan remaining symbols,
    expand tokens into caller buffer.
"""

import os
import sys
import json
import collections

def build_kallsyms_representation(symbols, marker_interval=256):
    """
    Given a list of symbol names, build the 256-token dictionary,
    length-prefixed names array, and marker table.
    """
    # 1. Collect all initial single characters present in symbol names
    # Initial token table maps each unique character to itself
    names = [s["name"] for s in symbols]
    
    # We will use single-character tokens for all used ASCII characters,
    # and train multi-character tokens for the remaining slots up to 256.
    words = [[c for c in name] for name in names]
    
    # Track assigned tokens
    # To faithfully reflect Linux kallsyms:
    # Any byte 0..255 is a token index.
    # Single characters take 1 token slot each.
    used_chars = sorted(list(set(c for name in names for c in name)))
    
    token_dict = {} # token_id -> string
    char_to_token = {}
    next_token_id = 0
    
    for c in used_chars:
        if next_token_id < 256:
            token_dict[next_token_id] = c
            char_to_token[c] = next_token_id
            next_token_id += 1
            
    # Now iteratively find the best recurring pair or n-gram to assign to remaining tokens
    available_tokens = 256 - next_token_id
    
    # Represent words as lists of token IDs
    word_tokens = [[char_to_token[c] for c in name] for name in names]
    
    for step in range(available_tokens):
        # Count frequency of adjacent pairs
        pair_counts = collections.defaultdict(int)
        for wt in word_tokens:
            for i in range(len(wt) - 1):
                pair_counts[(wt[i], wt[i+1])] += 1
                
        if not pair_counts:
            break
            
        best_pair, best_count = max(pair_counts.items(), key=lambda x: x[1])
        # Each replacement saves 1 byte per occurrence, minus dictionary cost of expansion
        if best_count <= 2:
            break # No net benefit
            
        new_token_id = next_token_id
        next_token_id += 1
        
        # Expansion of new token is expansion of pair[0] + expansion of pair[1]
        token_dict[new_token_id] = token_dict[best_pair[0]] + token_dict[best_pair[1]]
        
        # Replace occurrences of best_pair in word_tokens
        p0, p1 = best_pair
        for w_idx in range(len(word_tokens)):
            wt = word_tokens[w_idx]
            new_wt = []
            i = 0
            while i < len(wt):
                if i < len(wt) - 1 and wt[i] == p0 and wt[i+1] == p1:
                    new_wt.append(new_token_id)
                    i += 2
                else:
                    new_wt.append(wt[i])
                    i += 1
            word_tokens[w_idx] = new_wt

    # Fill unused tokens up to 256 with empty or dummy single bytes
    while next_token_id < 256:
        token_dict[next_token_id] = ""
        next_token_id += 1

    # 2. Build kallsyms_token_table and kallsyms_token_index
    token_table_bytes = bytearray()
    token_index = []
    
    for tid in range(256):
        tok_str = token_dict[tid]
        token_index.append(len(token_table_bytes))
        token_table_bytes.extend(tok_str.encode("ascii") + b"\0")

    # 3. Build kallsyms_names (length-prefixed) and kallsyms_markers
    kallsyms_names = bytearray()
    markers = []
    
    for idx, wt in enumerate(word_tokens):
        if idx % marker_interval == 0:
            markers.append(len(kallsyms_names))
            
        # 1-byte length prefix followed by token bytes
        kallsyms_names.append(len(wt))
        kallsyms_names.extend(wt)

    # 4. Symbol addresses / offsets
    first_addr = symbols[0]["addr"]
    addr_offsets = [s["addr"] - first_addr for s in symbols]
    
    return {
        "num_symbols": len(symbols),
        "first_addr": first_addr,
        "marker_interval": marker_interval,
        "token_table": bytes(token_table_bytes),
        "token_index": token_index,
        "names": bytes(kallsyms_names),
        "markers": markers,
        "addr_offsets": addr_offsets,
        "dictionary": token_dict
    }

def generate_kallsyms_c(data, prefix="kallsyms"):
    """Generate C source code for the kallsyms adaptation."""
    num_symbols = data["num_symbols"]
    first_addr = data["first_addr"]
    interval = data["marker_interval"]
    
    c_lines = [
        "/* Generated Linux kallsyms-style symbol table for Zephyr */",
        "#include <stdint.h>",
        "#include <stddef.h>",
        "#include <stdbool.h>",
        "",
        f"#define KALLSYMS_NUM_SYMS {num_symbols}",
        f"#define KALLSYMS_FIRST_ADDR 0x{first_addr:x}",
        f"#define KALLSYMS_MARKER_INTERVAL {interval}",
        "",
        "/* Token table and index */",
        f"static const uint16_t {prefix}_token_index[256] = {{"
    ]
    
    # Token index
    for i in range(0, 256, 8):
        chunk = ", ".join(f"0x{data['token_index'][j]:04x}" for j in range(i, min(i+8, 256)))
        c_lines.append(f"\t{chunk},")
    c_lines.append("};")
    c_lines.append("")
    
    # Token table bytes
    tt = data["token_table"]
    c_lines.append(f"static const uint8_t {prefix}_token_table[{len(tt)}] = {{")
    for i in range(0, len(tt), 16):
        chunk = ", ".join(f"0x{b:02x}" for b in tt[i:i+16])
        c_lines.append(f"\t{chunk},")
    c_lines.append("};")
    c_lines.append("")
    
    # Markers
    markers = data["markers"]
    c_lines.append(f"static const uint32_t {prefix}_markers[{len(markers)}] = {{")
    for i in range(0, len(markers), 8):
        chunk = ", ".join(f"0x{m:x}" for m in markers[i:i+8])
        c_lines.append(f"\t{chunk},")
    c_lines.append("};")
    c_lines.append("")
    
    # Address offsets
    offsets = data["addr_offsets"]
    c_lines.append(f"static const uint32_t {prefix}_offsets[{len(offsets)}] = {{")
    for i in range(0, len(offsets), 8):
        chunk = ", ".join(f"0x{o:x}" for o in offsets[i:i+8])
        c_lines.append(f"\t{chunk},")
    c_lines.append("};")
    c_lines.append("")
    
    # Compressed names
    names = data["names"]
    c_lines.append(f"static const uint8_t {prefix}_names[{len(names)}] = {{")
    for i in range(0, len(names), 16):
        chunk = ", ".join(f"0x{b:02x}" for b in names[i:i+16])
        c_lines.append(f"\t{chunk},")
    c_lines.append("};")
    c_lines.append("")
    
    # Lookup and expand functions
    c_lines.extend([
        f"/* Faithful Linux kallsyms decompressor */",
        f"static unsigned int {prefix}_expand_symbol(unsigned int off, char *result, size_t maxlen) {{",
        f"\tif (maxlen == 0) return 0;",
        f"\tconst uint8_t *data = &{prefix}_names[off];",
        f"\tunsigned int len = *data++;",
        f"\tsize_t out = 0;",
        f"\twhile (len > 0) {{",
        f"\t\tuint8_t token_id = *data++;",
        f"\t\tlen--;",
        f"\t\tconst char *tptr = (const char *)&{prefix}_token_table[{prefix}_token_index[token_id]];",
        f"\t\twhile (*tptr != '\\0') {{",
        f"\t\t\tif (out + 1 < maxlen) {{",
        f"\t\t\t\tresult[out++] = *tptr;",
        f"\t\t\t}}",
        f"\t\t\ttptr++;",
        f"\t\t}}",
        f"\t}}",
        f"\tresult[out < maxlen ? out : maxlen - 1] = '\\0';",
        f"\treturn (unsigned int)out;",
        f"}}",
        f"",
        f"static unsigned int {prefix}_get_symbol_offset(unsigned int pos) {{",
        f"\tunsigned int marker_idx = pos / KALLSYMS_MARKER_INTERVAL;",
        f"\tunsigned int name_off = {prefix}_markers[marker_idx];",
        f"\tconst uint8_t *name = &{prefix}_names[name_off];",
        f"\tunsigned int rem = pos % KALLSYMS_MARKER_INTERVAL;",
        f"\tfor (unsigned int i = 0; i < rem; i++) {{",
        f"\t\tname += (*name + 1);",
        f"\t}}",
        f"\treturn (unsigned int)(name - {prefix}_names);",
        f"}}",
        f"",
        f"int {prefix}_lookup(uintptr_t addr, char *buf, size_t buflen, uint32_t *offset) {{",
        f"\tif (buflen == 0) return -1;",
        f"\tif (addr < KALLSYMS_FIRST_ADDR) {{",
        f"\t\tif (offset) *offset = 0;",
        f"\t\tbuf[0] = '?'; buf[1] = '\\0';",
        f"\t\treturn 1; /* Not found */",
        f"\t}}",
        f"\tuint32_t sym_offset = addr - KALLSYMS_FIRST_ADDR;",
        f"\tif (sym_offset > {prefix}_offsets[KALLSYMS_NUM_SYMS - 1] + 0x1000) {{",
        f"\t\tif (offset) *offset = 0;",
        f"\t\tbuf[0] = '?'; buf[1] = '\\0';",
        f"\t\treturn 1;",
        f"\t}}",
        f"\t/* Binary search */",
        f"\tint left = 0, right = KALLSYMS_NUM_SYMS - 1;",
        f"\tint match = -1;",
        f"\twhile (left <= right) {{",
        f"\t\tint mid = left + (right - left) / 2;",
        f"\t\tif ({prefix}_offsets[mid] <= sym_offset) {{",
        f"\t\t\tmatch = mid;",
        f"\t\t\tleft = mid + 1;",
        f"\t\t}} else {{",
        f"\t\t\tright = mid - 1;",
        f"\t\t}}",
        f"\t}}",
        f"\tif (match < 0) {{",
        f"\t\tif (offset) *offset = 0;",
        f"\t\tbuf[0] = '?'; buf[1] = '\\0';",
        f"\t\treturn 1;",
        f"\t}}",
        f"\tif (offset) *offset = sym_offset - {prefix}_offsets[match];",
        f"\tunsigned int name_off = {prefix}_get_symbol_offset((unsigned int)match);",
        f"\t{prefix}_expand_symbol(name_off, buf, buflen);",
        f"\treturn 0; /* Success */",
        f"}}"
    ])
    
    return "\n".join(c_lines) + "\n"

def main():
    if len(sys.argv) < 3:
        print("Usage: kallsyms_generator.py <input_symtab.json> <output.c> [interval]")
        sys.exit(1)
        
    in_json = sys.argv[1]
    out_c = sys.argv[2]
    interval = int(sys.argv[3]) if len(sys.argv) > 3 else 256
    
    with open(in_json, "r") as f:
        symbols = json.load(f)
        
    data = build_kallsyms_representation(symbols, marker_interval=interval)
    c_code = generate_kallsyms_c(data)
    
    os.makedirs(os.path.dirname(out_c) or ".", exist_ok=True)
    with open(out_c, "w") as f:
        f.write(c_code)
        
    print(f"Generated {out_c} for {data['num_symbols']} symbols with interval {interval}:")
    print(f"  Token index: 512 B")
    print(f"  Token table: {len(data['token_table'])} B")
    print(f"  Markers: {len(data['markers']) * 4} B")
    print(f"  Offsets: {len(data['addr_offsets']) * 4} B")
    print(f"  Names: {len(data['names'])} B")
    total_rodata = 512 + len(data['token_table']) + len(data['markers'])*4 + len(data['addr_offsets'])*4 + len(data['names'])
    print(f"  Total rodata: {total_rodata} B")

if __name__ == "__main__":
    main()
