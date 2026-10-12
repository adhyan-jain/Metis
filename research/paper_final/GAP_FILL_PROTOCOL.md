# Gap-Fill Protocol — Anatomy of Runtime Symbolization Cost in Zephyr Firmware

Status: **FROZEN before the first gap-fill measurement** (hashes and timestamps in `FREEZE_MANIFEST.txt`).
Authorised by `research/paper_audit/PUBLICATION_AUDIT.md` (decision: MINIMAL GAP-FILL). No new architecture is built; this is a
controlled examination of existing schemes and baselines.

## 1. Question and hypotheses
Q: In small embedded firmware images, how much of the achievable flash saving from compact runtime symbol tables comes from the
**address-index representation** versus **symbol-name encoding**, and how much remains when baselines receive the same enabling
optimisation?

* H1 (address share): the compact address index accounts for >= 70 % of the whole-image saving of the proposed scheme over the best existing scheme, on >= 75 % of workloads.
* H2 (residual): once the address index is equalised, the proposed name coding improves whole-image size by <= 1 % on >= 75 % of workloads.
* H3 (FSST): for workloads with N > 1000, a reference-FSST name coder is within 1 % of whole image of the front-coded dictionary scheme.
* H4: zero decoder mismatches in every tested configuration.
* H5: baseline inclusion and linking checks are valid and image accounting is uniform.

## 2. Definitions (all frozen)
* **Image** = flat binary produced by Zephyr's own recipe `objcopy --gap-fill 0xFF --output-target=binary --remove-section=.comment --remove-section=COMMON` on the final ELF (bytes). Whole-image denominator for every percentage = the **image of the arm being improved upon** (the baseline in `(base − cand)/base`).
* **Feature cost** of an arm = image(arm) − image(NOSYM), NOSYM = same sample/board/toolchain built with `CONFIG_SYMTAB=n` (no symbolization, no consumer). Reported for context only; gates use whole images.
* **Arms** (14 per workload; each relinked to a fixed point):
  * STOCK — Zephyr's generated `symtab.c` (gen_symtab.py on the image's own ELF) + stock `symtab_find_symbol_name`, reached via the same consumer as every arm. Reference/sanity only; excluded from `E`.
  * EXISTING `E` = {KALL256, KALL16 (Linux-kallsyms-style, marker interval 256/16, `scripts/kallsyms_generator.py`), CAND_DIRECT, CAND_CP16 (earlier project 64/128-token design with raw u32 offsets; budget 64 if N<300 else 128)}.
  * HARDENED `H0` = {B1_B16_TOK, B1_B16_KALL, B2_B16_TOK, B2_B16_KALL}: compact address index (B1 = restarted LEB128 delta blocks of 16; B2 = bit-packed delta blocks of 16) + conventional names (pair-token pool / kallsyms tokens).
  * CONTROL = {RAW32_B16_TOK, RAW32_B16_KALL}: u32 offsets + same names.
  * PROPOSED `P` = **B1_B16_FC16** (single fixed configuration; B2_B16_FC16 reported as variant, not used by gates): B1 address index + checkpointed (16) front-coded dictionary names.
  * FSST = **B1_B16_FSST**: B1 address index + names coded by the reference FSST library (github.com/cwida/fsst, commit `e638d4cf`, scalar encoder), trained on exactly the image's own names, one string per name, length-prefixed records (1-byte length, 255 = escape), per-block start offsets in the shared directory, symbol table serialised compactly (nsym, len[], concatenated symbols; decoder walks prefix sums — flash-minimal). Alternative layouts (reference 9-byte slots; 16-bit offsets) are reported as byte sensitivities only.
* Per workload w: `E_w = min image over E`; `H_w = min(E_w, min image over H0)`; `P_w`, `F_w` = images of PROPOSED, FSST.
* **S_total,w** = E_w − P_w; **S_addr,w** = E_w − H_w; **residual gain g_w** = 100·(H_w − P_w)/H_w (negative or zero = no improvement); **address share a_w** = S_addr,w / S_total,w when S_total,w > 0.
* Sensitivity (reported, not gating): g against B1_B16_TOK only; share computed with geometric mean; B2 variant.

## 3. Workloads (selected by deterministic rule from the pre-freeze feasibility sweep; build success and N only)
Source: `results/sweep_results.jsonl`; script `scripts/select_workloads.py`; output `results/workloads_frozen.json` (hash recorded).
* Candidate pool: every `samples/**/CMakeLists.txt` of the checked-out Zephyr tree (commit 403a7d79, local modifications recorded in `logs/zephyr_local_modifications.diff`), built with `CONFIG_SYMTAB=y`, default `prj.conf`, no overlays.
* ARM (`mps2/an385`, Cortex-M3, Zephyr SDK GCC 14.3.0): pilot samples excluded; 6 samples at N-rank quantiles k/5.
* RISC-V (`qemu_riscv32`, RV32IMAC, Zephyr SDK GCC 14.3.0): `hello_world` + 3 samples (min, median, max N) from the RISC-V-built set excluding pilot, `hello_world`, and the ARM picks. The RISC-V sweep was stopped early for time (20 of 59 planned samples built; decided before any size result existed).
* Result: 10 workloads, 9 distinct non-pilot samples, 2 architectures. **Gate population = these 10.** Pilot (clang) results are reported separately and never mixed into gates. The development workload (`qemu_cortex_m3`/hello_world) used to debug the harness is excluded from analysis.
* Symbol-count coverage: sweep maximum on `mps2/an385` was N = 2,337 and on RISC-V N = 2,148 for built samples. **No real sample reached N ≈ 3,000**; this is a declared coverage limitation. N > 1000 workloads in the gate set are used for H3.
* No sample is dropped after measurement. A workload that fails to build/link in the main run is reported as failed and counted as not satisfying any gate it participates in (conservative).
* Not performed: new clang builds of the new samples (the existing clang configuration is represented by the 4 pilot workloads). GCC covers all 10.

## 4. Link methodology
For each workload the stock image is built with `west build` (+`-DCONFIG_SYMTAB=y`) and NOSYM with `-DCONFIG_SYMTAB=n`. Every arm is linked by replaying the build's own recorded link command (`ninja -t commands zephyr/zephyr.elf`) with `symtab.c.obj` replaced by the arm's objects, compiled with the build's own compile flags for `symtab.c.obj`. A consumer `zephyr_fault_symbol_logger` (forced by `-Wl,-u`) keeps the lookup path reachable in every arm, including STOCK (consumer = `scripts/stock_consumer.c`, the historical stock adapter). Flash accounting only: the caller-supplied output buffer lives on the caller's stack/RAM and is not counted.
* **Fixed point**: iteration k encodes the manifest of ELF(k−1) (iteration 0: the stock west ELF), links ELF(k), re-extracts the manifest (FUNC symbols, same rules as `gen_symtab.py`, validated bit-exact against the four historical manifests). Converged iff manifest(ELF(k)) equals the encoded manifest exactly (names, addresses, end). Maximum 8 iterations.
* Unavoidable stub: the unmodified library `symtab.c.obj` references `z_symtab`; candidate arms therefore carry Zephyr's 16-byte stub from `libsymtab.a` (counted in their images; ≤16 B, same in every candidate arm, slightly conservative against candidates vs STOCK).
* Explicit inclusion checks (Blocker B fix): STOCK passes iff `z_symtab_entries` read from the linked ELF equals the manifest entry by entry (offsets and strings, plus `"?"` sentinel) and `symtab_find_symbol_name` is present. Candidate arms pass iff `symtab_find_symbol_name` is absent, `lookup_symbol` present and the stub `z_symtab_entries` ≤ 8 B.

## 5. Decoder correctness (gate 4)
For every arm × workload on the converged manifest, the generated C is compiled with host gcc and driven with the historical query set (`research/metis_arch/test_roundtrip.queries`: out-of-range probes, every symbol start, +1, interior and last byte, plus 2000 random addresses) against the manifest oracle, at buffer capacity 256 (all names fit). Metis-family and FSST arms must also match the truncation contract at capacity 12. Legacy arms (KALL*, CAND*): out-of-range probes must return the name `?` (their rc/offset convention differs); symbol hits must match exactly. Metis-family arms additionally pass the Python reference decoder; FSST names are additionally verified from emitted bytes by Python and by the FSST library round trip. **Gate 4 passes iff total mismatches over all arms and workloads = 0.**

## 6. Gates (all numeric thresholds frozen)
* **G1** (address share): a_w ≥ 0.70 on ≥ 75 % of the 10 workloads; a workload with S_total,w ≤ 0 counts as failing.
* **G2** (residual): g_w ≤ 1.0 % on ≥ 75 % of workloads.
* **G3** (FSST): over workloads with converged N_final > 1000: |F_w − P_w| / P_w ≤ 1.0 % on **all** of them.
* **G4** zero decoder mismatches (§5). **G5** all baseline/inclusion checks pass and all 14 arms converged on all workloads.
* **PASS** = G1 ∧ G2 ∧ G3 ∧ G4 ∧ G5.
* **TERMINATE (no paper)** if any of: (T1) g_w > 1.0 % on ≥ 50 % of workloads; (T2) pooled address share Σ S_addr,w / Σ S_total,w (workloads with S_total,w > 0) < 0.50 — pooled-bytes aggregation is the frozen rule; geometric-mean and median shares are reported as sensitivities only; (T3) any arm required by a compared quantity (any arm in E, H0, P, FSST) fails to reach a fixed point on any workload.
* **INCONCLUSIVE** otherwise (e.g. 25 %–50 % of workloads with g_w > 1 %): no paper claiming H1/H2. Thresholds will not be changed after measurement.

## 7. Aggregates and statistics
Per-workload tables are primary; aggregates (pooled, geometric-mean, median) are secondary. Image sizes are deterministic for a given toolchain and are reported without confidence intervals. No timing is measured, so no latency/energy/runtime-performance claim is made. Any change from this protocol is documented as a deviation and the affected analysis labelled exploratory.

## 8. Execution validation (optional, outside the gates)
Where feasible a built image is booted in QEMU (Zephyr SDK 1.0.1 host tools) and the observed console output recorded. Booting an image proves only that the linked image starts; it is **not** evidence of lookup correctness unless a self-test is included, which is stated explicitly where used. No claim about runtime correctness or performance of a lookup on target is made without it.

## 9. Environment and commands
Host: Linux 7.2.4-arch, 16 cores. Zephyr tree `corpora/zephyr` @403a7d79 (+ local diff), Zephyr SDK 1.0.1 (arm-zephyr-eabi / riscv64-zephyr-elf GCC 14.3.0), host gcc 16.2.1, Python (repo venv) with pyelftools 0.33, fsst @e638d4cf. Commands:
`python scripts/select_workloads.py` → `python scripts/gapfill_run.py <workload-id>` (one per workload) → `python scripts/analyze.py`.

## 10. Limitations that will constrain the claims
Real samples cover N = 137 … ~2,300 only; two architectures, one compiler family (GCC) in the gate set; sizes only (no execution-time or energy); symbol tables built from the image's own function symbols (FUNC only, like Zephyr); decoder code compiled with the build's flags (-Os); caller buffer RAM not counted; FSST trained on the image's own names (best case for FSST, small training set); the 4 pilot workloads use a different toolchain wrapper (clang) and unfixed-point-relinked historical objects; all checks were performed by project agents, not independent verification; the compared "existing" schemes are project-internal faithful adaptations of kallsyms-style design, not Linux's code.
