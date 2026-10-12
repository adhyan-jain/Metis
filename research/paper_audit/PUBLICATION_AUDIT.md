# METIS Research Recovery — Publication Audit (2026-10-11)

Scope: METIS, METIS-X, STRATA/SymTab V1–V4, ODHST and the Zephyr symbolization tournament, plus the invention-branch side experiments. Nothing was committed, modified, or re-run over historical artifacts. My recomputations live in `research/paper_audit/` (`verify_headlines.py`, its `.out`, and a scratch copy of the offline generator with its output redirected).

Evidence levels used below:
- **I** = implemented (code exists)
- **E** = executed (result artifacts exist)
- **R** = reproduced by me this session from raw artifacts (arithmetic or offline generator; no firmware rebuilt, no benchmark re-timed)
- **V** = independently verified (nobody other than the project's own agents; **no experiment has this level**)

---

## 0. Decision

**Selected outcome: MINIMAL GAP-FILL for one narrow paper (Paper 1). Do not write a manuscript yet. The other two directions: STOP.**

Calibration matters more than the label. Even if the gap-fill passes, the result is an *engineering/experience-report-grade* empirical note, not a research-conference contribution. Every mechanism has verified prior art, and the single best result is mostly a transfer of Linux's address-delta idea. If you need a PLDI/CGO/ASPLOS-level result, the answer is STOP.

| Rank | Paper | Central question | Outcome |
|---|---|---|---|
| 1 | Anatomy of runtime symbolization cost in Zephyr firmware | Where do the flash bytes go, and what is left after the baselines get the same enabling trick? | **MINIMAL GAP-FILL** (§7) |
| 2 | Baseline/harness sensitivity in the METIS family (retrospective) | How much of each reported win was the baseline or harness? | **STOP as a paper.** Mixes unrelated systems; n = 3–4 anecdotes by the same authors. Usable as a discussion section of Paper 1 or an internal lessons document. |
| 3 | Headroom of identifier compression in scoped compiler symbol tables | Can representation-adaptive or compressed identifier storage materially reduce symbol-table memory? | **STOP.** Valid-trace evidence is 6 C corpora, with a self-written best baseline; earlier 26-corpus traces were judged invalid by the project's own audit. Needs C++ and many more corpora (a new campaign). |

---

## 1. Project chronology (git + reports)

| Date (2026) | Commit / source | Event | Why |
|---|---|---|---|
| 09-01/03 | `8afc4ca`…`8669bec` | BUDGET-SYM: adaptive scope-aware symbol table, grid-search thresholds, multi-seed, corpus tooling | Initial course-project hypothesis: representation chosen by memory pressure |
| 09-13 | `15fad17`, `bcfc670`, `ecbf2cb` | Memoization, LRU cache, ML threshold predictor; **cache broke shadowing (fixed)**; RobinHood/Trie comparisons | Latency + algorithm-comparison review requests |
| 09-15 | `48094cb`…`18ff32d` | SymTabV2: free-list slots, scope index, block front-coding; found pool-refcount under-charge, same-scope redeclaration double-charge, quadratic tombstones | Audit-driven fixes; numbers re-run |
| 09-16 | `a24b584`…`d256510` | Benchmark infra, Pareto, ablation, statistics, ML policy, hostile review ("REJECT AND REVISE"), 20-corpus real-world pipeline | Paper hardening |
| 09-16 | `722bc33`, `4cd7546`, `019f6d6` | V2 physical memory 1.87–6.52× Conventional → redesign 1.14–1.35× → **V3** (44→32 B slot) | Real-allocator measurement exposed that V2 lost |
| 09-17 | `ceaf318`, `887088c` | V4 side-table (negative); EmbeddedConventional baseline; corpus 20→26 | Try to find any winning regime |
| 09-17/18 | `233d653`, `c02344e` | "Final research freeze"; paper declared "READY" | (later contradicted, §3) |
| 09-19 | `5269b60`, `e1b1b54` | **METIS-X** (flat Robin-Hood, 32 B slot, 12 B SSO), canonical dataset, reconciliation | Pivot after SymTab lost to Conventional on memory 0/26 |
| 09-20/21 | branches `invention-*`, `semantic-residency`, `capacity-architecture`, `coexistence-planner`, `final-claim-battle` | Patent/novelty hunt: SASI-Tab, segmented capacity, coexistence planner | Look for a protectable mechanism → all rejected/falsified; `NO_DEFENSIBLE_CLAIM` |
| 10-07 | `0b43440` | METIS-X relocation hazard fix, canonical reconciliation | |
| 10-10 | `7b3dacf`, `research/l3` | L3 candidates TS-TFDR, D, F killed; TCC/Clang integration shows raw-string scoped lookup does not exist in production compilers → **compiler line STOPPED** | |
| 10-11 | `9d9db2b`, untracked `research/*` | Pivot to Zephyr runtime symbolization (flash). Fairness audit → ODHST (no-go) → architecture tournament → URCM, LLEXT, shared-pool, ex-idx gates (all stop) → final hybrid tournament (engineering-only) | Search for a transferable result; pre-registered, SHA-frozen gates |

---

## 2. Experiment inventory and evidence level

| # | Experiment | Problem / mechanism | Baselines | Workloads | Level | Outcome | Evidence location |
|---|---|---|---|---|---|---|---|
| A1 | BUDGET-SYM V1 / Interned | adaptive repr. by memory pressure | Conventional | synthetic + regex traces | E | V1 5–10× bloated; superseded | `results/benchmark_results*.csv`, `CLAUDE_RESEARCH.md` §3 |
| A2 | SymTabV2 (interning + front-coded blocks) | compress long/repeated names | Conventional, Interned, HeapString | 20 regex-trace corpora | E, R(0/26 mem) | 1.9–6.5× then 1.14–1.35× Conv; loses 20/20 | `docs/research/v2_*.md`, `data/real_world_benchmark.csv` |
| A3 | SymTabV3 (32 B slot, hash-fingerprint seen-map) | cut fixed overhead | same | 26 corpora | E, **R** | mem ≥ 1.10× Conv on 26/26; p95 ≤1.25× on 20/26; joint 0/26 | `data/real_world_benchmark.csv`, `verify_headlines.out` |
| A4 | Hot/cold demotion/promotion | latency-aware tiering | V2 | 3 workloads | E | ~5% mem, thrashing; disabled | `results/demotion_experiment.csv` |
| A5 | SymTabV4 (side-table) | density | V3, Conv | 20 corpora | E, R | loses to V3 19/20, Conv 26/26 | `docs/research/v4_evaluation.md` |
| A6 | ML policy predictor | choose thresholds | hand heuristic | synthetic | E | 22–55% constraint violations; heuristic better | `results/adversarial_review.md` |
| A7 | Trie / RobinHood / hash functions | alt. structures | Conv | synthetic | E | no win | `docs/trie.md`, `robinhood.md` |
| A8 | **SymTab V2 gate** (libclang AST traces, 6 C corpora, 15 impls) | valid-workload re-test | Conv, Abseil, **ScopedId** (own Clang-style compact table) | lua, cJSON, protobuf-c, FreeRTOS, redis(+deps) | E, **R** | V2 = 2.1–2.7× Conv, 2.35–2.76× ScopedId; ideal front-coding bound 5–9% of peak | `research/symtab_v2_gate/` |
| B1 | METIS-X (flat RH, 32 B, 12 B SSO, LIFO scopes) | scoped lookup latency + memory | EmbeddedConventional, ConventionalHost, SymTabV3 | Zephyr, ESP-IDF, FreeRTOS, Arduino traces | E, **R** | see §3: baseline/harness-dependent | `results/METIS_X_CANONICAL_DATASET.csv`, `metis_x_ablation.csv` |
| B2 | L3: TS-TFDR, D, F | scope-exit rollback, arenas | METIS-X | same | E | killed (Amdahl; +20–36% mem) | `research/l3/` |
| B3 | TCC/Clang source integration study | real-compiler fit | — | source reading only | I (analysis) | premise absent in production compilers | `research/l3/phase8*,9*` |
| B4 | SASI-Tab; capacity-arch; coexistence planner; semantic residency | patent/novelty hunt | METIS-X | 4 traces | E (branches, not in HEAD) | rejected/falsified; planner Δpeak ≤ 0.006% | `git show research/final-claim-battle:research/...` |
| B5 | LS-ZCVM, GS-SRER | patent candidates | — | — | **not implemented** | never run | `docs/patent/INVENTION_GAP_ANALYSIS.md` |
| C1 | Zephyr fairness audit: stock vs Kallsyms-256/16 vs 64-token K=1/K=16 | flash saving | stock Zephyr, faithful kallsyms | 4 real linked images | E, **R** (arithmetic) | K=16 saves 4.8–8.4% image; loses to Kallsyms-256 on echo by 1,216 B | `results/fairness_audit_results.json` |
| C2 | ODHST | decoupled order + FC | feature-size lower bound | 4 manifests | E (offline) | NO-GO (pre-registered) | `research/odhst/` |
| C3 | Architecture tournament (A/B1/B2/C/D families) | address + name coding | best-existing, address-hardened | 4 linked images | E, **R** (offline JSON bit-identical) | gain 2.67% geomean vs best existing; **0.26% vs address-hardened** | `research/metis_arch/`, `metis_final_tournament/` |
| C4 | BLPR/ALOB/LLEXT catalog | local permutations etc. | same | same | E, R | inferior/engineering-only | `research/metis_final_tournament/` |
| C5 | Shared-pool, ex-idx, URCM, LLEXT-ABI gates | cross-structure sharing | frozen gates | real ELFs | E | all fail gate; bounds 0.055% / ≤1.04% / ≤4.5–6.8% of metadata / ≤1.5% | each `REPORT.md` |

Pre-registration integrity (checked): all `SHA256SUMS` verify, `GATE_FROZEN.sha256` and `PLAN.sha256` match their files, and file mtimes put each freeze 14 s – 3 min before its first result. That covers the Track C gates only; the compiler track pre-dates this practice.

---

## 3. Audit findings (recomputed from raw data)

### 3.1 METIS-X headline is mostly a harness/baseline property (Reproduced)
`src/metis_x_bench_main.cpp:169-184` keeps a `std::unordered_set<std::string> uniqueSymbols` **inside** the measured `heap::Scope` for every implementation. Comparing the canonical CSV to the ablation harness (which lacks it) gives a constant offset:

| Zephyr final heap (MB) | canonical | ablation | offset |
|---|---|---|---|
| EmbeddedConventional | 41.74 | 21.53 | 20.21 |
| METIS-X | 24.95 | 4.73 | 20.22 |
| ConventionalHost | 22.59 | 2.25 | 20.34 |

ESP-IDF offset is 21.3 MB for all three. Consequences:

| METIS-X vs … | canonical (diluted) | ablation (structure only) |
|---|---|---|
| EmbeddedConventional, Zephyr | −40.2% | −78.0% |
| **ConventionalHost, Zephyr** | **+10.5% (METIS-X larger)** | **+110.3%** |
| ConventionalHost, ESP-IDF | −13.2% | −27.7% |

The README's "−40.2%" memory claim uses only the weakest baseline. The canonical CSV already contained ConventionalHost, on which METIS-X **loses memory on Zephyr**. Latency vs ConventionalHost is robustly better (p95 −47…−73%), which is the expected node-based vs open-addressing effect. Canonical rows store one value per implementation with no per-repetition dispersion. The invention-branch harness shows EmbeddedConventional Zephyr final heap of 1.65 MB, not 21.5 MB (on the same 2.6M-event trace per its own report); this cross-harness 13× disagreement for the same nominal baseline is **unresolved** (not reproduced; needs harness diff).

### 3.2 Zephyr symbolization (Reproduced from linked-image sizes)
- Fairness-audit table recomputes exactly (K=16: 8.37 / 7.21 / 5.65 / 7.09% of image vs stock).
- Tournament: B1_B16_FC16 vs best existing = 3.09 / 2.46 / 1.63 / 3.48%, geomean **2.67%**. Against the address-hardened control (same compact address index on token/Kallsyms names): −0.09 / 0.53 / 0.38 / 0.24%, geomean **0.26%**. Raw u32 offsets with the same tokens: 0.62%.
- Share of the saving obtainable by the address index alone: **81% by bytes (sum), ~90% by geomean ratio** (the report's "over 89%" holds only for the geomean basis).
- Offline generator re-run in a scratch copy: output JSON is bit-identical, round-trips verified.
- Anomaly (unresolved): `baselines_relinked.json` "stock_adapter" images are 15,640 / 74,608 / 255,104 / 24,480 B, only ~264 B above the no-symtab images, so the stock table appears *not linked* in that adapter build. Headlines use the fairness-audit stock (19,792 …) and best-existing baselines, so they are unaffected, but "vs stock" claims from the relink harness must not be used.
- Not executed: images were linked and sized, never run (no ARM QEMU); variable-length tables embed addresses taken from the stock image (no fixed-point relink); one toolchain (Clang 22.1.8/LLD), 4 workloads (3 on one board), N ≤ 1,381; host timings only.

### 3.3 Compiler track
- 26-corpus memory: V2/V3/V4/Interned ≥ Conventional on **26/26** (reproduced; V3 range 1.10–1.69×, median 1.44). The committed CSV **does not match** the stage-2 document's numbers (e.g. Zephyr V3/Conv 1.249 vs 1.118; FreeRTOS 1.590 vs 1.259): the CSV was regenerated after the document. Direction is stable; magnitudes are not.
- Latency: 20/26 pass p95 ≤ 1.25× (reproduced), but timing used 2–5 repetitions and Conventional's p95 relative stddev has median 9% and max 75% → per-corpus latency verdicts are within noise. The "7/20" framing is not reproducible from the committed file.
- Conventional-HeapString beats Conventional on memory in 16/26 (median 0.993): SSO is not the explanation, consistent with the structural-overhead argument.
- **Trace validity:** the project's own V2-gate audit says the regex-token traces (one global table, no TU reset, 54% of FreeRTOS "declarations" were same-scope redeclarations) "are not valid symbol-table workloads". The memory conclusion was independently reproduced for V2 on valid AST traces (§A8), not for V3/V4.
- The "closed-form break-even `(2L+86)/(L+13)`" is arithmetic over the V2 implementation's own byte constants plus libstdc++ node overhead; its "empirical validation" (synthetic L=32, k=3.33) checks the model against the same implementation. It is not a general law and cannot carry a paper.
- "Ready for submission" (`FINAL_PAPER_READINESS_REPORT.md`) is contradicted by the later V2-gate audit and by the 66.91 vs 41.74 MB paper/CSV mismatch (`METIS_X_BASELINE_RECONCILIATION.md`).
- `docs/research/FAILED_APPROACHES.md` states V4 "1.8× cache miss rate" and ML "+350 µs / >450 KB"; no cache-counter measurement exists in the repo (L1 residency is listed as unmeasured in `project_closure.md`) and `adversarial_review.md` gives 42 µs. Treat those as unsupported. `ARCHITECTURAL_EVOLUTION.md` calls V4 an arena design, which contradicts the code/eval docs (V4 = side table), and calls scope exit "O(1)" (retracted elsewhere as O(P)).

---

## 4. Failure analysis — what generalizes

Supported by raw data, across ≥2 tracks:

1. **Win size is dominated by baseline choice.** V2: −46–65% vs BudgetSym V1 → +110–170% vs Conventional → +135–176% vs a compact scoped table. METIS-X memory: −40% / −78% vs one baseline → +10…+110% vs another. Zephyr: 2.67% vs best existing → 0.26% once the baseline gets the same index. In each case the enabling idea (flat compact layout; delta-coded index) was available to the baseline.
2. **Fixed per-entry and per-structure overhead beats string payload when strings are short.** Ideal front coding saves 42–58% of name bytes but only 5–9% of a compact table's peak heap (6 C corpora, reproduced); in firmware, 4 B/symbol of address index is ~2% of image on echo_server while a perfect zero-byte name pool would still leave it.
3. **Fixed-size dictionaries create N-crossovers.** The 64-token scheme beats Kallsyms-256 at N = 158 (+1,104 B), is at parity at 557, and loses at 1,381 (−1,216 B); only three points on one toolchain, so the "N ≈ 600" location is indicative.
4. **Cross-structure sharing has no headroom** in these images: string sharing ≤ 0.055% of image, ex-idx absent (0% coverage), line-table joint coding ≤ 4.5% of metadata.
5. **Measurement hygiene failures recur** (harness constants, regenerated CSVs overwriting papers, single-run timings, invalid extractor).

Not supported: anything about *why* adaptive compression fails in general; the discriminator search found no predictive metric.

---

## 5. Prior-art and novelty assessment

Verified from primary pages this session: Cromie 2026 kallsyms direct 3-byte name index, markers removed (lkml 2609.2/10784); Levin 2026 kallsyms lineinfo, 64-entry blocks, ULEB128/SLEB128 deltas, uncompressed block index, binary search + sequential decode, 10.0 → 3.7 B/entry (lkml 2607.1/04652); Biesheuvel relative-offset kallsyms table, merged 2016 (commit 2213e9a6); FSST (Boncz, Neumann, Leis, PVLDB 2020); Partitioned Elias–Fano (Ottaviano & Venturini, SIGIR 2014, variable-length partitions by near-optimal DP); Zephyr symtab docs (name + address, binary search only). Previously seen by the project (LLVM source, not re-checked by me): Clang IdentifierTable/IdentifierResolver, LLVM ScopedHashTable, Martínez-Prieto et al. string dictionaries. Not verified by me: Aho et al. cactus stacks, extendible hashing, GSYM/Breakpad (recollection).

Strongest surviving claim: *on Zephyr-scale tables the compact address index, not name coding, accounts for ~80–90% of the achievable saving.* Closest counter-arguments: (i) Levin and Biesheuvel already deploy block-delta and relative address coding in Linux; (ii) block front coding and dictionary-size/crossover trade-offs are textbook; (iii) the residual vs the hardened baseline is 0.26%. What the combined work adds beyond known designs: a measured, linked-image decomposition at MCU scale with pre-registered negative gates. That is evidence, not an algorithm; it is not novel in mechanism. I did not search exhaustively; absence of an identical paper is not evidence of novelty.

STRATA/SymTab V2 conclusions are preserved: nothing here contradicts "limited novelty" / "dominated by simpler baselines".

---

## 6. Unified results matrix (comparable metrics only)

| Setting | Metric | Proposed | Weak baseline | Strong baseline | Unc. |
|---|---|---|---|---|---|
| SymTab V2 (valid AST, 6 C corpora) | peak heap / TU | 1.0 | BudgetSym V1: n/a | ScopedId: 2.35–2.76× larger; Conv: 2.08–2.68× | bootstrap CI over TUs, e.g. redis 119 TUs [2.13, 2.21] |
| SymTab V3 (26 regex-trace corpora) | final heap / Conv | 1.10–1.69× | Interned 1.55–2.70× | — | memory deterministic; latency noisy (2–5 reps) |
| METIS-X Zephyr | final heap (canonical) | 24.95 MB | EmbConv 41.74 (−40%) | ConvHost 22.59 (**+10.5%**) | 5 reps, no dispersion stored |
| METIS-X Zephyr | final heap (structure only) | 4.73 MB | 21.53 (−78%) | 2.25 (**+110%**) | single run |
| METIS-X Zephyr | p95 lookup | 0.082 µs | 0.151 | 0.300 (−73%) | 5 reps, point value |
| Zephyr image, best design | whole image vs best existing | 2.67% geomean | stock 5.6–8.4% (K=16) | hardened baseline **0.26%** | deterministic sizes; n = 4 workloads |
| Cross-consumer sharing | max image saving | ≤0.055% | — | — | 4 images |

---

## 7. Paper 1: minimal gap-fill

**Claim to establish:** For Zephyr runtime symbol tables spanning N ≈ 150–5,000 across more than one architecture, the compact address index accounts for ≥ 70% of the whole-image saving over the best conventional scheme, and once equalized the remaining name-coding gain is ≤ 1% of image.

**Why existing data cannot support it:** 4 workloads, 3 on mps2/an385, N ≤ 1,381, one toolchain, no run-time execution, a missing close baseline (FSST built into firmware), and N-dependence resting on three points.

**Smallest experiment:** with the frozen generators and no new design, build (link-only) ≥ 8 additional existing Zephyr samples, covering N up to ≥ 3,000, ≥ 2 architectures (e.g. add a RISC-V board), and the GCC toolchain on ≥ 2 of them; add a reference FSST name coder as a baseline. Pre-register and SHA-freeze before measuring (same practice as the gates).
- **Pass:** hardened-control gain ≤ 1% on ≥ 75% of workloads, address-index share ≥ 70% on ≥ 75%, FSST within 1% of image of the FC+dictionary scheme for N > 1,000, and 0 decoder mismatches.
- **Terminate the paper direction if:** the hardened-control gain exceeds 1% on at least half of the workloads (then name coding matters and the whole premise changes), or the address share falls below 50%, or a fixed-point relink of the variable-length table fails to converge on any workload.

Cost: builds are link-level; no new architecture and no broad benchmark. Venue reality: an experience/negative-results report, plus an optional upstream Zephyr proposal.

---

## 8. Remaining-work list (only what makes Paper 1 defensible)
1. Freeze and hash the gap-fill plan (§7) before building anything.
2. Resolve the `stock_adapter` anomaly (is the stock table linked?) and drop "vs stock" claims from the relink harness until fixed.
3. Build FSST (reference implementation) into the harness.
4. Fixed-point relink for variable-length tables, so the images are self-consistent.
5. Execute at least one image (QEMU or hardware), or state plainly that none was executed.
6. Add repetition statistics to any timing claim; keep size claims separate from host-timing claims.
7. Append, rather than rewrite, correction notes for the contradictory historical claims listed in §3 (README METIS-X claim vs ConventionalHost; "READY FOR SUBMISSION"; FAILED_APPROACHES cache/ML figures; ARCHITECTURAL_EVOLUTION V4/O(1)).
8. Resolve the cross-harness EmbeddedConventional disagreement (21.5 MB vs 1.65 MB) before citing any METIS-X number again.

## 9. What would not change this decision
More compiler-track corpora, more METIS-X variants, or another architecture search. Five audits and my recomputation agree the bottleneck is novelty and baseline strength, not data volume.
