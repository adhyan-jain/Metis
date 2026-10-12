#!/usr/bin/env python3
"""EXPLORATORY re-analysis (protocol deviation, documented in EXPERIMENT_LOG.md; the frozen analysis in results/analysis is primary).
Reason: on qemu_riscv32 the flat binary (objcopy --gap-fill) changes only in exact 4096-byte steps (page-aligned section
placement), so sub-4-KiB differences between arms are invisible in the frozen image metric. Here image := sum of the sizes of all
allocated PROGBITS sections of the final ELF (alloc_progbits_bytes, recorded for every arm in the preserved results).
Everything else (arms, gate formulas, thresholds) is identical to analyze.py. Workloads with a non-converged required arm are excluded
and listed."""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
import analyze as A


def img(w, a):
    r = w["arms"].get(a, {})
    return r["facts"]["alloc_progbits_bytes"] if r.get("converged") else None


A.img = img
sys.argv = [sys.argv[0], "workloads", "analysis_exploratory"]
A.main()
