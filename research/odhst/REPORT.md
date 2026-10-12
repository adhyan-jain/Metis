# ODHST pilot — verdict: NO-GO (stopped at the gate)

Pre-registered gate: research/odhst/GATE_PREREGISTRATION.md (written before measuring).
Code: odhst.py (generator + exact size sweep), test_odhst.py (round-trip, <=B-1 preceding records, all 4 manifests).
Results: pilot_results.json, table.md. Inputs: frozen data/zephyr_symtab_*.json (not modified). No firmware built.

## Result
ODHST B=16, best token budget / ID mode by exact size (4-byte address offsets, IDs, name pool, checkpoints, dictionary),
EXCLUDING decoder/adapter code, so it is a strict lower bound on feature bytes:
see table.md. Needed feature size for a 5% whole-image win is 1,853 / 4,210 / 5,665 / 2,453 B; ODHST payload alone is
2,195 / 7,382 / 18,062 / 3,046 B. It fails on all four workloads (echo_server misses by ~12 KB).
Versus best existing feature size it saves at most ~1.3 KB (echo, 18,062 vs 19,376, before ~0.3-0.5 KB decoder), i.e. <0.5% of the image.

## Why
The 4-byte address index alone is 5,524 B on echo_server = 2% of the image; deduplication barely helps (1,367 unique of 1,381 names);
front coding on address-independent sorted names gives ~7.5 B/name, which is about what kallsyms-style tokens already achieve.
The 5%-of-whole-image bar is effectively unreachable for any name-only scheme: even a zero-byte name pool would leave 5.5 KB of addresses.
Bit-packed IDs (11 bits) cost 1,899 B vs 2,762 B for u16; they are a real gain but the ID table is an extra cost that Kallsyms/K=16 avoid.

## Prior art (checked, not exhaustive)
- Linux kallsyms: token compression + markers every 256; historically replaced an earlier "stem" (prefix) compression with token tables (LKML 2004-05 threads).
- 2026 kallsyms patch (Cromie): 3-byte direct index into compressed names, drops markers; no sorting/front coding.
- OptFSST (arXiv 2607.11271, ADMS 2026): FSST optimization; no kallsyms/front coding mention.
- Block front coding and compressed string dictionaries are textbook. I did not find the exact combination for embedded symbolization,
  but each component is known and the search was shallow (no FSST paper read in full, no literature database search). Novelty is weak.
Not done: FSST/OptFSST baseline (only required after passing the gate); the "disclose missing close baseline" caveat applies if anyone pursues this.

## Limitations
No firmware/linker-map builds were run, because the lower-bound argument is sufficient to fail the gate; decoder bytes and bit-unpack cost were therefore not measured.
Address index kept at 4 B per the spec; a smaller address index would help every method equally and is orthogonal.

## Possible contribution if any
A negative result: decoupled ordering does not beat kallsyms-style tokens on whole-image flash for Zephyr-scale tables. Recommend stopping.
