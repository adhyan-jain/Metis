# FINAL STATUS — Zephyr symbolization gap-fill

## Decision: **STOP. The pre-registered gates were not met; no manuscript was written.**
Frozen analysis (`results/analysis/gate_results.json`): decision label `TERMINATE` (T3 triggered); G1, G2, G3, G4, G5 all FAIL. The conclusion does not depend on the T3 technicality: with `net_ptp_mps2` excluded and under the exploratory 4-KiB-unquantised metric, **G1 and G3 still fail**, and the frozen decision rule without T3 would be INCONCLUSIVE — which also forbids a paper claiming H1/H2. Nothing was committed, pushed, submitted or published; no external disclosure was made.

## Criteria and evidence (10 workloads: 6 × Cortex-M3 `mps2/an385`, 4 × RV32 `qemu_riscv32`; GCC 14.3; N = 137…2,337)

| Gate (frozen threshold) | Frozen metric (flat binary) | Exploratory metric (allocated sections) | Result |
|---|---|---|---|
| G1 address share ≥ 70 % on ≥ 75 % of workloads (need 8/10) | 5 | 6 | **FAIL** |
| G2 residual name-coding gain ≤ 1 % on ≥ 75 % (need 8/10) | 7 (ptp counts as failing) | 9 (ptp excluded from computation but counted) | FAIL frozen / pass exploratory |
| G3 FSST within 1 % of front-coded dictionary for all N > 1000 | 4 of 6 | 5 of 6 (`http_get`: +1.20 %) | **FAIL** |
| G4 zero decoder mismatches | 11,811 — all from the non-converged `net_ptp` KALL16 arm (artefact, see EXPERIMENT_LOG D2); 0 on every converged arm | same | FAIL as defined; 0 on converged arms |
| G5 all arms converge, stock present, stubs valid | stock present 10/10; 139/140 arm-fixed-points converged; stub check false on 4/4 RV32 workloads (96 B stock lookup retained) | same | FAIL |
| T1 residual gain > 1 % on ≥ 50 % of workloads | 2/10 (20 %) — not triggered | 0/10 — not triggered | not triggered |
| T2 pooled address share < 50 % | 62.3 % (RV32 bins quantised) — not triggered | 71.2 % — not triggered | not triggered |
| T3 any required arm fails to reach a fixed point | **triggered**: `net_ptp_mps2` KALL16 oscillates 242,152 ↔ 242,160 B | same | **triggered** |

Per-workload numbers (exploratory metric; E = best existing scheme, H = best address-hardened control, P = proposed `B1_B16_FC16`, F = FSST; all in bytes of whole flash image):

| workload | N | E (arm) | H (arm) | P | F | saved E→P | address share | residual H→P | F vs P |
|---|---|---|---|---|---|---|---|---|---|
| hello_world RV32 | 137 | 24,394 (CAND_CP16) | 23,834 (B1_TOK) | 23,986 | 25,722 | 408 B (1.67 %) | 137 % | −0.64 % | +7.24 % |
| socketpair M3 | 710 | 83,912 (KALL256) | 82,028 (B1_TOK) | 81,344 | 82,232 | 2,568 B (3.06 %) | 73.4 % | 0.83 % | +1.09 % |
| midi2 RV32 | 985 | 168,980 | 166,568 | 165,434 | 166,882 | 3,546 B (2.10 %) | 68.0 % | 0.68 % | +0.88 % |
| http_get M3 | 1,061 | 141,116 | 138,472 | 137,348 | 138,992 | 3,768 B (2.67 %) | 70.2 % | 0.81 % | +1.20 % |
| stats M3 | 1,321 | 206,932 | 203,700 | 202,308 | 204,020 | 4,624 B (2.23 %) | 69.9 % | 0.68 % | +0.85 % |
| lldp M3 | 1,422 | 225,848 | 222,332 | 220,868 | 222,568 | 4,980 B (2.21 %) | 70.6 % | 0.66 % | +0.77 % |
| bridge RV32 | 1,445 | 281,698 | 278,182 | 276,978 | 278,400 | 4,720 B (1.68 %) | 74.5 % | 0.43 % | +0.51 % |
| mqtt_azure RV32 | 2,148 | 388,818 | 383,460 | 381,100 | 383,466 | 7,718 B (1.98 %) | 69.4 % | 0.62 % | +0.62 % |
| prometheus M3 | 2,337 | 390,776 | 384,848 | 382,336 | 384,880 | 8,440 B (2.16 %) | 70.2 % | 0.65 % | +0.67 % |
| ptp M3 | 1,570 | — (KALL16 did not converge) | | | | | | | |

Pooled address share 71.2 % (geometric mean 76 %, median 70.2 %). Five of the nine usable shares lie within ±1 point of the 70 % threshold, so the 75 %-of-workloads rule cannot be satisfied or refuted with confidence on this data.

## What the data does support (exploratory/observational; not gate-passing claims)
* Over the best existing scheme the best compact scheme saves 1.7–3.1 % of the flash image (0.4–8.4 KB) on these Zephyr images; roughly 70 % of that comes from the address-index representation and the rest from name coding.
* Once baselines receive the same compact address index, the proposed front-coded dictionary improves the image by 0.4–0.8 % for N ≥ 710 and is slightly worse at N = 137.
* Reference FSST (compact table, same address index) is 0.5–1.2 % larger than the front-coded dictionary for N > 1,000 and ≈ equal to the best conventional kallsyms-style name coding at large N; at N = 137 its table makes it +7 %.
* Decoders for the compared arms return the oracle's answers on the emulated targets (8/8 QEMU self-tests, both architectures).
These are measurements on 9 usable workloads from two architectures and one compiler; they are not generalisable and not peer verified.

## Reasons to terminate the publication direction
1. The pre-registered gates for H1 and H3 failed under both metrics; the frozen rule also records T3.
2. The effect sizes sit at the thresholds (shares ≈ 70 %, FSST gaps ≈ 1 %); further workloads or metric choices would be tuning around thresholds, which the protocol forbids after measurement.
3. Mechanisms have close prior art (relative-offset and block-delta kallsyms tables, FSST), and the remaining finding is an engineering decomposition of 1–3 % of an image — not publishable as a contribution on its own.

## Limitations and unresolved items
* No sample reached N ≈ 3,000 (max 2,337); RV32 sweep truncated; one compiler family (GCC) in the gate set; the clang-wrapper pilot (4 workloads) is separate and was not re-run with fixed-point relinking.
* Flash only: no timing, energy or runtime-performance claim; caller buffer RAM not counted.
* The cause of the KALL16 oscillation on `net_ptp` was not investigated.
* All checks were performed by the project's own tooling/agent; none is independent verification.
* The "Cromie" kallsyms patch cited in the earlier audit could not be located and is not cited.

## Disclosure / authorship
`docs/patent/*` (INVENTION_DISCLOSURE, PRIOR_ART_RISKS, IP_READINESS_REPORT) document statutory-bar / public-disclosure risk and an invention-disclosure package; whether any filing or public release is intended is unresolved in the repository. This study is local only. The existing manuscript lists Adhyan Jain and Shubhi Singh; their agreement to authorship of any new document is not recorded, so no author list was assigned. The website page `frontend/components/ResearchContent.tsx` was edited locally (not deployed, not committed); if the site is public, review that panel against the patent notes before publishing.

## Reproduce
`python research/paper_final/scripts/select_workloads.py` → `gapfill_run.py <id>` (×10) → `analyze.py` and `analyze_exploratory.py`; verify `FREEZE_MANIFEST.txt` and `SHA256SUMS`. Requires Zephyr SDK 1.0.1, Zephyr @403a7d79, FSST @e638d4cf, pyelftools (see EXPERIMENT_LOG §1). Artifact map: `ARTIFACT_MAP.md`; claims: `CLAIMS_LEDGER.md`.
