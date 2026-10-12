"""Read-only .ARM.exidx coverage + byte-cost analysis for the four METIS workloads. Run with /usr/bin/python from repo root."""
import json, struct, sys, glob, os
from elftools.elf.elffile import ELFFile
sys.path.insert(0, "research/metis_arch")
import metis_arch as M
ELF = {"hello_world_mps2": "/tmp/zephyr_build_symtab_y/zephyr/zephyr.elf", "shell_module_mps2": "/tmp/zephyr_build_shell_symtab_y/zephyr/zephyr.elf",
       "echo_server_mps2": "/tmp/zephyr_build_echo_symtab_y/zephyr/zephyr.elf", "hello_world_nrf52840": "/tmp/test_build_nrf/zephyr/zephyr.elf"}
NOSYM = {"hello_world_mps2": "/tmp/zephyr_build_symtab_n/zephyr/zephyr.elf", "shell_module_mps2": "/tmp/zephyr_build_shell_symtab_n/zephyr/zephyr.elf",
         "echo_server_mps2": "/tmp/zephyr_build_echo_symtab_n/zephyr/zephyr.elf", "hello_world_nrf52840": "/tmp/test_build_nrf_symtab_n/zephyr/zephyr.elf"}
def prel31(w, at):
    v = w & 0x7fffffff
    if v & 0x40000000: v -= 0x80000000
    return (at + v) & 0xffffffff
def exidx(path):
    e = ELFFile(open(path, "rb")); s = e.get_section_by_name(".ARM.exidx")
    if s is None: return None
    d = s.data(); base = s["sh_addr"]; ents = []
    for i in range(0, len(d), 8):
        w0, w1 = struct.unpack("<II", d[i:i + 8]); ents.append((prel31(w0, base + i), w1))
    return dict(size=s["sh_size"], addr=base, entries=ents)
def main():
    T = {}
    for wl, p in ELF.items():
        d = M.load(wl); starts = sorted(set((d["first"] + 2 * o) & ~1 for o in d["offs"]))
        ex = exidx(p); real = [(a, w) for a, w in ex["entries"]]
        exset = set(a for a, w in real)
        # last entry is the terminator (addr = end of text)
        cov = [a for a in starts if a in exset]
        miss = [a for a in starts if a not in exset]
        extra = [a for a, _ in real if a not in set(starts)]
        nosym = exidx(NOSYM[wl]) if os.path.exists(NOSYM[wl]) else None
        tj = json.load(open(f"research/metis_arch/results/tournament_{wl}.json"))
        b = next(r for r in tj if r["tag"] == "B1_B16_FC16")
        bk = b["bytes"]
        T[wl] = dict(symtab_entries=len(d["offs"]), unique_eligible_starts=len(starts), exidx_section_bytes=ex["size"], exidx_entries=len(real),
                     exidx_entries_hex=[(hex(a), hex(w)) for a, w in real], covered=len(cov), coverage_pct=round(100 * len(cov) / len(starts), 2),
                     exidx_entries_without_symbol=len(extra), eligible_missing_from_exidx=len(miss),
                     no_symtab_image_exidx_bytes=(nosym or {}).get("size"), metis_b1_address_bytes=bk["address_index"], b1_addr_stream=bk["addr_stream"], b1_addr_directory=bk["addr_directory"],
                     b1_payload=bk["payload"], b1_whole_image=b["whole_image_bin"],
                     exidx_if_all_functions_8B=8 * len(starts))
        print(wl, {k: v for k, v in T[wl].items() if k != "exidx_entries_hex"}, T[wl]["exidx_entries_hex"])
    json.dump(T, open("research/metis_exidx/results/coverage.json", "w"), indent=1)
main()
