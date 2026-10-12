"""Normalised source-line inventory from the real firmware ELF's .debug_line (via llvm-dwarfdump).

Ground-truth semantics (documented, used by every method):
  * a sequence row (addr, file, line) covers [addr, next_row.addr) inside the same sequence; end_sequence closes it.
  * column / is_stmt / discriminator / prologue flags are ignored (file:line granularity, like `addr2line` without -i).
  * several rows at the same address: the LAST one wins (zero-length ranges dropped).
  * consecutive rows with equal (file,line) are merged -> LINE_CHANGE events only on real change.
  * sequences whose start address is outside the final .text range (gc'd / tombstoned, addr 0) are dropped.
  * inlined code is attributed to the innermost line-table row (non-inline source locations only; no DW_TAG_inlined_subroutine walk).
File identity = (resolved path string) so equal paths from different CUs share one file id.
"""
import re, subprocess, sys, json, os
DWD = "/usr/bin/llvm-dwarfdump"

def parse(elf):
    txt = subprocess.run([DWD, "--debug-line", elf], capture_output=True, text=True, check=True).stdout
    progs = []; cur = None; dirs = {}; files = {}; mode = None; fi = None
    for ln in txt.splitlines():
        if ln.startswith("debug_line["):
            cur = dict(dirs={}, files={}, rows=[]); progs.append(cur); fi = None; continue
        if cur is None: continue
        m = re.match(r'include_directories\[\s*(\d+)\] = "(.*)"', ln)
        if m: cur["dirs"][int(m[1])] = m[2]; continue
        m = re.match(r'file_names\[\s*(\d+)\]:', ln)
        if m: fi = int(m[1]); cur["files"][fi] = [None, 0]; continue
        m = re.match(r'\s+name: "(.*)"', ln)
        if m and fi is not None: cur["files"][fi][0] = m[1]; continue
        m = re.match(r'\s+dir_index: (\d+)', ln)
        if m and fi is not None: cur["files"][fi][1] = int(m[1]); continue
        m = re.match(r'0x([0-9a-f]+)\s+(\d+)\s+(\d+)\s+(\d+)\s+\d+\s+\d+\s+\d+\s+(.*)', ln)
        if m: cur["rows"].append((int(m[1], 16), int(m[2]), int(m[4]), "end_sequence" in m[5]))
    return progs

def path_of(p, idx):
    name, d = p["files"][idx]
    if name.startswith("/"): return name
    return (p["dirs"][d].rstrip("/") + "/" + name) if d in p["dirs"] else name

def inventory(elf, lo, hi):
    """returns (events [(addr, fileid, line)] sorted, ends [(addr)] range ends, files[list of path], stats)"""
    progs = parse(elf); files = []; fid = {}; segs = []   # segs: list of (start,end, [(addr,fid,line)...])
    st = dict(raw_rows=0, dropped_outside=0, dup_addr=0, merged_same=0, sequences=0)
    for p in progs:
        seq = []
        for (a, line, f, end) in p["rows"]:
            st["raw_rows"] += 1
            if end:
                if seq: segs.append((seq, a))
                seq = []; continue
            seq.append((a, line, f, p))
    # (rows list was closed only at end_sequence; leftover ignored)
    rows = []
    for seq, endaddr in segs:
        st["sequences"] += 1
        if not (lo <= seq[0][0] < hi): st["dropped_outside"] += len(seq); continue
        out = []
        for (a, line, f, p) in seq:
            path = path_of(p, f)
            if path not in fid: fid[path] = len(files); files.append(path)
            e = (a, fid[path], line)
            if out and out[-1][0] == a: st["dup_addr"] += 1; out[-1] = e
            else: out.append(e)
        m = []
        for e in out:
            if m and m[-1][1:] == e[1:]: st["merged_same"] += 1; continue
            m.append(e)
        rows.append((m, endaddr))
    rows.sort(key=lambda r: r[0][0])
    # flatten into ranges
    rng = []   # (start, end, fid, line)
    for m, endaddr in rows:
        for i, (a, f, l) in enumerate(m):
            e = m[i + 1][0] if i + 1 < len(m) else endaddr
            if e > a: rng.append((a, e, f, l))
    rng.sort()
    for x, y in zip(rng, rng[1:]): assert x[1] <= y[0], ("overlap", x, y)
    return rng, files, st

if __name__ == "__main__":
    elf, lo, hi = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3], 0)
    rng, files, st = inventory(elf, lo, hi)
    print(len(rng), len(files), st)
