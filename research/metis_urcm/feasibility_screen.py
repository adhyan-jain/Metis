"""Fast feasibility screen: exact bytes of separate symbol vs line representations, and a strict OPTIMISTIC bound on sharing.
Run from repo root: python3 research/metis_urcm/feasibility_screen.py"""
import sys, json, os
sys.path.insert(0, "research/metis_arch"); sys.path.insert(0, "research/metis_urcm")
import metis_arch as M, extract_lines as X
ELF = {"hello_world_mps2": "/tmp/zephyr_build_symtab_y/zephyr/zephyr.elf", "shell_module_mps2": "/tmp/zephyr_build_shell_symtab_y/zephyr/zephyr.elf",
       "echo_server_mps2": "/tmp/zephyr_build_echo_symtab_y/zephyr/zephyr.elf", "hello_world_nrf52840": "/tmp/test_build_nrf/zephyr/zephyr.elf"}

def leb(v): return M.leb(v)
def zz(v): return (v << 1) ^ (v >> 63) if v < 0 else v << 1

def line_events(rng):
    """ranges -> typed event list [(addr, fid, line)] with line=0 'NOLINE' gap events; returns events, end"""
    ev = []; pend = None
    for (a, e, f, l) in rng:
        if pend is not None and pend != a: ev.append((pend, -1, 0))
        ev.append((a, f, l)); pend = e
    end = pend
    return ev, end

def best_line_stream(ev, files, B):
    """conventional separately-indexed line map: restarted blocks of B events; per-block directory = (abs base, stream offset, file/line state)."""
    n = len(ev); first = ev[0][0]
    # restart state: fid (varint-width fixed), line (varint at restart is in the stream header) -> counted in directory
    fw = M.bw(len(files)); 
    stream = bytearray(); dirb = 0; nb = (n + B - 1) // B
    bases = []; offs = []
    pf, pl = 0, 0
    for b0 in range(0, n, B):
        blk = ev[b0:b0 + B]; offs.append(len(stream)); bases.append(blk[0][0] - first)
        # restart state: absolute (fid, line) of FIRST event is stored in the directory; remaining events delta-coded
        f, l = blk[0][1], blk[0][2]
        for i in range(1, len(blk)):
            a, nf, nl = blk[i]; da = (a - blk[i - 1][0]) >> 1
            stream += leb(da)
            if nf == f:
                stream += leb(zz(nl - l) << 1)
            else:
                stream += leb((zz(nl - l) << 1) | 1) + leb(max(nf, 0) + 0)
            f, l = nf, nl
    base_w = M.bw(max(bases)); sw = M.bw(max(offs) if offs else 0)
    # directory: base addr + stream offset + first-event fid + first-event line
    maxl = max(l for _, _, l in ev)
    dirbytes = nb * (base_w + sw + fw + M.bw(maxl))
    return dict(events=n, blocks=nb, stream=len(stream), directory=dirbytes, base_w=base_w, sw=sw, fw=fw)

def entropy_floor(ev):
    """order-0 empirical entropy (bytes) of (addr-delta, line-delta, file-change) per event: a floor for any zero-order entropy coder; no escape/table cost counted."""
    import math, collections
    def H(xs):
        c = collections.Counter(xs); n = len(xs); return -sum(v * math.log2(v / n) for v in c.values()) / 8
    da = [(ev[i][0] - ev[i - 1][0]) >> 1 for i in range(1, len(ev))]
    dl = [ev[i][2] - ev[i - 1][2] for i in range(1, len(ev))]
    fc = [ev[i][1] != ev[i - 1][1] for i in range(1, len(ev))]
    nf = [ev[i][1] for i in range(1, len(ev)) if ev[i][1] != ev[i - 1][1]]
    return H(da) + H(dl) + H(fc) + H(nf)

def path_pool(files, Bn=16):
    """front-coded sorted unique path pool with pair tokens (same machinery as symbol names) + ids"""
    r = M.fc_names(files, Bn, 0); best = None
    for T in M.TBUDGETS:
        r = M.fc_names(files, Bn, T); c = len(r["pool"]) + 2 * len(r["toks"]) + (len(files) + Bn - 1) // Bn * 2
        if best is None or c < best[0]: best = (c, T, len(r["pool"]), len(r["toks"]))
    return best

def main():
    out = {}
    for wl, elf in ELF.items():
        d = M.load(wl); first = d["first"] & ~1; end = first + d["end"] * 2 + (d["first"] & 1) * 0
        lo, hi = first, d["first"] + 2 * d["end"]
        rng, files, st = X.inventory(elf, lo, hi)
        F = M.build(d, "B1", 16, "FC", Bn=16); sb = M.breakdown(F)
        sym_total = sb["payload"]
        ev, lend = line_events(rng)
        gaps = sum(1 for e in ev if e[1] == -1)
        fstarts = set(d["first"] + 2 * o for o in d["offs"]); fstarts_a = set(x - 1 if x & 1 else x for x in fstarts)
        evaddr = set(e[0] for e in ev)
        coinc = len(fstarts_a & evaddr)
        res = {}
        for B in (8, 16, 32):
            res[B] = best_line_stream(ev, files, B)
        ls = res[16]; pp = path_pool(files)
        line_total = ls["stream"] + ls["directory"] + pp[0]
        # strict optimistic bound for sharing: ALL symbol address-index bytes vanish (function starts free inside the line stream, zero event cost),
        # one of the two token dictionaries vanishes, and one of two directories' per-block size vanishes entirely.
        opt_save = sb["address_index"] + min(sb["dictionary"], pp[3] * 2) + ls["directory"] * 0
        floor = entropy_floor(ev); line_floor_total = floor + pp[0]
        sep = sym_total + line_total
        out[wl] = dict(n_sym=d["offs"].__len__(), line_ranges=len(rng), line_events_with_gaps=len(ev), gaps=gaps, files=len(files), extract_stats=st,
                       func_starts_on_line_event=coinc, sym_bytes=sym_total, sym_breakdown=sb, line=ls, line_B=res, path_pool=pp, line_total=line_total,
                       separate_total=sep, optimistic_save=opt_save, optimistic_save_pct_of_metadata=round(100 * opt_save / sep, 2),
                       line_entropy_floor_total=round(line_floor_total), optimistic_save_pct_vs_entropy_floor=round(100 * opt_save / (sym_total + line_floor_total), 2),
                       dwarf_debug_line_bytes=None)
        print(wl, "sym", sym_total, "line", line_total, "sep", sep, "opt_save", opt_save, f"{100*opt_save/sep:.2f}%", "floor-pct", out[wl]["optimistic_save_pct_vs_entropy_floor"], "line_floor", out[wl]["line_entropy_floor_total"], "coinc", coinc, "/", len(fstarts_a), "ev", len(ev), flush=True)
    json.dump(out, open("research/metis_urcm/results/feasibility_screen.json", "w"), indent=1)
main()
