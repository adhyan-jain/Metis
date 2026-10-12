"""Symbol-manifest extraction that replicates corpora/zephyr/scripts/build/gen_symtab.py exactly
(FUNC symbols with st_value != 0, name sanitised with the same regex, de-duplicated by address, sorted by address,
sentinel end address = last.addr + last.size, or last.addr + sizeof(uintptr_t) if the last symbol has size 0).
Returns the dict layout consumed by research/metis_arch/metis_arch.py (offsets in 2-byte units)."""
import re
from elftools.elf.elffile import ELFFile
from elftools.elf.descriptions import describe_symbol_type


def sanitize(name):
    m = re.match(r'(^[a-zA-Z_][a-zA-Z0-9_]*)', name)
    if not m:
        raise ValueError(f"cannot sanitise {name!r}")
    return m.group(0)


def extract_entries(elf_path):
    """-> (entries[list of (addr,size,name)], end_addr, ptr_bytes)"""
    with open(elf_path, "rb") as f:
        elf = ELFFile(f)
        ptr = elf.elfclass // 8
        seen, out = set(), []
        for s in elf.get_section_by_name(".symtab").iter_symbols():
            if describe_symbol_type(s["st_info"]["type"]) == "FUNC" and s["st_value"] != 0:
                a = s["st_value"]
                if a in seen:
                    continue
                seen.add(a)
                out.append((a, s["st_size"], sanitize(s.name)))
    out.sort(key=lambda e: e[0])
    a, sz, _ = out[-1]
    end = a + sz if sz else a + ptr
    return out, end, ptr


def load_elf(elf_path, wl="wl"):
    ents, end, ptr = extract_entries(elf_path)
    first = ents[0][0]
    diffs_even = all((a - first) % 2 == 0 for a, _, _ in ents) and (end - first) % 2 == 0
    if not diffs_even:
        raise ValueError("address differences are not all even; 2-byte unit assumption violated")
    offs = [(a - first) >> 1 for a, _, _ in ents]
    assert all(b > a for a, b in zip(offs, offs[1:]))
    return dict(first=first, offs=offs, end=(end - first) >> 1, names=[n for _, _, n in ents],
                addrs=[a for a, _, _ in ents], wl=wl, ptr=ptr)
