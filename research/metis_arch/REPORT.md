# METIS architecture tournament — report (2026-10-11)

**Verdict: ENGINEERING-ONLY — NO DEFENSIBLE NOVELTY.**
Every numeric pilot criterion (1–6) passes against the pre-declared baseline, but the gain is almost entirely a compact address
index that applies equally to every conventional method; criterion 7 (novelty) fails. No further hybrid refinement was run
(the bottleneck is novelty, not a solvable technical limitation).

## Setup (all builds identical)
Zephyr `403a7d79a` (dirty tree as in the audit), clang/LLD 22.1.x, `-Os`, same `linker.cmd`/archives/link recipe as
`scripts/fairness_audit_master.py` (`research/metis_arch/link_image.py`), same consumer (`lookup_symbol` + `zephyr_fault_symbol_logger`, `-Wl,-u`),
same API/64 B buffer. Whole-image = flat `.bin` size. Feature = image − no-symtab image. Baselines were **re-linked** in this harness from the frozen
closure objects (`results/baselines_relinked.json`): they differ from historical numbers by +0…56 B (<0.03%); winners unchanged
(K=16 on hello/shell/nrf, Kallsyms M=256 on echo). My Kallsyms-M16 re-implementation in the generator (`RAW32_B16_KALL`) lands within 24 B of the archived one.
Reproduce: `python3 research/metis_arch/{baselines,run_tournament,analyze,final_gate}.py` (and `KALL=1 run_tournament.py`).

## Families (exact bytes; all verified)
| Family | What | Result (geomean whole-image vs best existing) | Status |
|---|---|---|---|
| A adaptive-width microblocks (W1/W2) | 2.2% best (B=8+FC) | beaten by B | rejected |
| B1/B2 restarted delta (LEB128 / per-block bit-packed) | **2.66% (B1,B=16,+FC16)**, 2.60% (B2) | best address codec; B2 −10 B/−0.1% data but +24 B decoder | best |
| C ODHST-v2 (sorted unique names, front coding, bit-packed IDs) | +0.3…0.6% over token names on shell/echo, **−0.1% on hello** | helps only N ≳ 500 | marginal |
| D joint DP over variable blocks/codecs | 2.33% | larger directory (count field) + more decoder (584 B vs 512 B) outweighs gains; loses to fixed-B | rejected |
| control raw u32 offsets + same tokens | 0.61%, −0.28% on echo | fails gate | — |
Full table: `results/normalized_results.csv|json`, `results/config_summary.json`, `results/final_gate.json`.

## Strongest design: B1_B16_FC16 (restarted LEB128 delta blocks B=16, front-coded IDs, 16-name FC blocks)
| workload | best existing (img B) | ODHST-v2/B1 img | Δ bytes | Δ % | feature red. |
|---|---|---|---|---|---|
| hello_world mps2 | cand K16 18,136 | 17,576 | −560 | 3.09% | 20.3% |
| shell_module | cand K16 82,696 | 80,664 | −2,032 | 2.46% | 24.3% |
| echo_server | Kallsyms M256 274,272 | 269,808 | −4,464 | 1.63% | 23.0% |
| hello_world nrf52840 | cand K16 28,060 | 27,084 | −976 | 3.48% | 25.3% |
Geomean 2.66%. Static RAM delta 0 (ELF .data/.bss identical). Stack: lookup frame 56 B (Kallsyms 32 B) + the 64 B caller buffer.
Work: name reconstruction ≤15 records (B=16); address search ≈ ⌈log2 #blocks⌉ block probes (≤ 7 on echo, 87 blocks) + ≤15 sequential delta decodes (worst-case 32 "steps" incl. varint bytes) — reported separately, nothing is called O(1).
Decoder+adapter code 440–512 B. Predicted (payload+object code) vs actual feature bytes: +12…+54 B (alignment/padding/section rounding).
Correctness: Python reference decoder and the generated C (host-compiled, cap 64 and 12, interior/odd/boundary/out-of-range addresses, 3–10k queries per workload) — 0 mismatches
(`results/test_roundtrip.log`, `results/host_c_test*.log`).

## Why the gate result does not support a paper
1. **Ablation ladder (whole-image bytes; hello / shell / echo / nrf):**
   best existing 18,136 / 82,696 / 274,272 / 28,060 → raw offsets + my tokens 17,912 / 82,544 / 275,032 / 27,692 →
   **compact addresses (B1) + tokens 17,560 / 81,128 / 271,552 / 27,148** → + front coding 17,576 / 80,664 / 269,808 / 27,084.
   The address index (4 B → ~1.3 B/symbol incl. directory) delivers most of the gain; front coding adds 0.5% on shell/echo and *costs* bytes on tiny tables.
2. **Address-hardened baseline** (apply the same compact address index to the conventional name schemes, i.e. Kallsyms names or tokens + B1/B2): the best design
   improves only 0.26% geomean, loses on hello (−0.09%), and fails criteria 4 and 5 (`results/final_gate.json`, 'address-hardened'). A skeptical reviewer will demand exactly this baseline.
3. **FSST-style names** (offline *approximate* estimator `fsst_estimate.py`, not the reference implementation, not built): name pool+table 12,595 B vs 12,340 B for FC+dict+IDs on echo (2% apart), worse than FC on small tables.
   FSST/OptFSST were **not** built into firmware; this close random-access baseline must be disclosed as missing in any submission.

## Prior art (adversarial)
- Delta + ULEB128 in blocks with an uncompressed block index, binary search then bounded sequential decode: Levin's 2026 Linux kallsyms lineinfo series
  (blocks of 64, 2.7× on lineinfo; [v7](https://lkml.iu.edu/2607.1/04652.html)) — same mechanism as B1 on a neighboring table. Relative 32-bit offsets for `kallsyms_offsets`:
  Biesheuvel's merged patch. Direct 3-byte name index (no markers): Cromie 2026 ([lkml](https://lkml.iu.edu/2609.2/10784.html)). Prefix ("stem") compression predates Linux token compression
  ([2002 patch](https://lkml.indiana.edu/0212.2/1646.html)).
- Optimal variable partitioning with per-partition encoding choice (our D): Partitioned Elias–Fano, Ottaviano & Venturini, SIGIR 2014 (doi 10.1145/2600428.2609615). Block front coding / compressed string dictionaries: textbook.
- FSST (Boncz et al., VLDB 2020), OptFSST (arXiv:2607.11271, ADMS 2026): random-access string compression with a static symbol table.
- I found no paper evaluating this combination for embedded runtime symbolization, but the search was focused, not exhaustive, and the combination is an assembly of known pieces
  with the principal gain from an address-delta idea already deployed (for lineinfo) in Linux. **Strongest reviewer objection:** "delta-encode the address table, then recompute your baselines."
  Falsifier of any novelty claim: the address-hardened baseline result above (already observed).

## Limitations
Four workloads; one toolchain; host-only checks (no ARM QEMU available, so firmware images were linked and size-measured but not executed; the call path is retained by the consumer + `-u`, as in the historical harness, not invoked at boot);
all methods embed manifest addresses taken from the stock image (variable-length encodings would require a fixed-point re-link in a real deployment — not exercised for any method);
no physical-MCU timing; FSST not built; prior-art search not exhaustive.

## Remaining honest options
(i) Report as a Zephyr/Linux-style engineering contribution (≈2–3% image, 20–25% symtab) — publishable, if at all, as an experience/engineering report.
(ii) Stop the METIS architecture search. A broader real-firmware set would not change criterion 7.
