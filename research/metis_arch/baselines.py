"""Re-link the archived closure objects (frozen, not modified) in this harness -> clean baseline denominators."""
import json, os, sys
sys.path.insert(0, os.path.dirname(__file__))
import link_image as L
from tournament_util import sections
ROOT = L.ROOT; out = {}
NOSYM = {"hello_world_mps2": "/tmp/zephyr_build_symtab_n", "shell_module_mps2": "/tmp/zephyr_build_shell_symtab_n",
         "echo_server_mps2": "/tmp/zephyr_build_echo_symtab_n", "hello_world_nrf52840": "/tmp/test_build_nrf_symtab_n"}
for wl in L.WL:
    out[wl] = {"A_no_symtab": os.path.getsize(f"{NOSYM[wl]}/zephyr/zephyr.bin"), "B_stock_zephyr": os.path.getsize(f"{L.WL[wl][0]}/zephyr/zephyr.bin")}
    for m in ("stock_adapter", "kallsyms_m256", "kallsyms_m16", "cand_direct", "cand_cp16"):
        sz, elf, mp, secs = L.link(wl, f"{ROOT}/build/closure/{wl}_{m}.o", f"base_{m}", f"{ROOT}/research/metis_arch/work/base")
        s = sections(secs)
        out[wl][m] = dict(whole_image_bin=sz, sections=s, elf=elf, map=mp)
        print(wl, m, sz, flush=True)
json.dump(out, open(f"{ROOT}/research/metis_arch/results/baselines_relinked.json", "w"), indent=1)
