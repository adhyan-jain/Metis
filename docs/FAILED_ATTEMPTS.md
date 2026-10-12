# METIS — Complete Ledger of Failed / Killed / Invalidated Attempts

**Purpose:** hand this file to a planner (human or LLM) so it does not re-propose anything already tried.
**Scope:** the whole `Metis` repo (88 commits, Aug–Oct 2026), not one session. Built 2026-10-11 by reading `docs/`, `docs/research/`, `docs/patent/`, `results/*.md`, `research/l3/**`, `research/odhst/`, `research/metis_arch/`, `CLAUDE_RESEARCH.md`, and `git log`.
**Reliability note:** numbers are quoted from the repo's own reports; I did not re-run old experiments. The repo contains several internal inconsistencies (listed in §6) — treat any single number as "as reported".
Out of scope: other projects on the same machine (OS_RAG, Proteus).

---

## 0. One-paragraph story

The repo started as **BUDGET-SYM / SymTab (V1→V4)**: a compiler symbol table whose per-symbol representation (INLINE / INTERNED / front-coded COMPRESSED) adapts to memory pressure. On real corpora it kept **losing on memory to a plain `std::unordered_map` with SSO**, and the compression tiers cost latency. It was then redesigned as **METIS-X** (flat Robin-Hood table, 32-byte slots, 12-byte inline strings, scope-LIFO unwinding), which looked great in trace replay but (a) contains no novel algorithm, (b) was compared against weak baselines, and (c) cannot be integrated into real compilers because they intern at the lexer. Compiler direction **formally stopped 2026-10-10**. The work pivoted to **Zephyr RTOS runtime symbol-table compression** (flash, not RAM): a 64-token byte-pair scheme saves 7–8% of whole firmware but loses to Linux Kallsyms on the largest workload; later architectures (ODHST, a four-family tournament) either missed their preregistered gate or won only through a compact address index that any baseline can adopt and that Linux already has. Verdicts so far: **engineering-only, no defensible novelty.**

---

## 1. What actually worked (do not discard — these are the assets)

| Asset | Evidence |
|---|---|
| Rigorous measurement infrastructure: allocator-measured heap (`heap_counter.hpp`), 20→26 real corpora with extractors, frozen manifests, claim-audit culture, reproducible whole-image Zephyr linking harness | `docs/research/RESULT_PROVENANCE.md`, `research/metis_arch/link_image.py` |
| Closed-form interning break-even: `k_breakeven(L) = (2L+86)/(L+13)` for L>15 B, no solution for L≤15; 59–76% of identifiers in real corpora are ≤15 B so Conventional (SSO) wins 20/20 | `results/FINAL_PAPER_READINESS_REPORT.md`, `docs/research/interning_sanity_check.md` |
| Real Zephyr savings (engineering result): K=16 candidate saves 1,656 B (8.4% image) hello_world, 6,424 B shell, 16,504 B echo_server vs stock; Kallsyms M=256 saves 17,720 B on echo_server; zero static RAM | `results/FINAL_FAIRNESS_AUDIT_REPORT.md` |
| Compact address index + front-coded name dictionary: −1.6% … −3.5% whole image (≈20–25% of the symtab feature) vs best prior method on 4 workloads, correct on host, 0 static RAM | `research/metis_arch/REPORT.md` |
| METIS-X trace-replay results vs *EmbeddedConventional* (Zephyr −38…−40% heap, p95 −30…−46%) — valid but baseline-dependent (see §3.5) | `docs/METIS_X_FINAL_VALIDATION_REPORT.md` |

---

## 2. Track A — BUDGET-SYM / SymTabV1–V4 (adaptive-representation symbol table)

### A0. Audit findings that invalidated early work (CLAUDE_RESEARCH.md §3)
| ID | Problem | Consequence |
|---|---|---|
| A1 | V1 append-oriented storage: Zephyr ≈3.7 M inserts, 268 K live names, ≈297 MB measured (≈73% of heap) | storage architecture itself was the problem |
| A2 | Conventional counted unique live symbols; BudgetSym counted declaration occurrences | early comparisons invalid |
| A3 | Corpus extractor tokenized prose-like words (e.g. "the") as identifiers; later also double-counted `#ifdef/#else` braces (depth 3465) and split `A::B::C` | old corpus results preliminary only; extractor fixed later (commits `6815ef0`, `835fac7`, `fix` for protobuf) |
| A4 | Single-run timings, no warm-up/variance | not evidence |
| A5 | LRU lookup cache not scope-aware → shadowed names resolved to the stale outer entry; cache capacity 64 caused a latency cliff (→256) | fixed (`bcfc670`, `5a14b35`); `docs/caching.md` |
| A6 | Grid search optimized modeled memory only | not a latency-aware policy |
| A7 | ML threshold predictor learned memory-optimal thresholds, not memory-subject-to-latency | redesign required |
| A8 | p-values computed over deterministic modeled memory | statistics invalid |
| — | V1/V2 pool-refcount under-charge bug; same-scope redeclaration double-charge/leak across all five tables; tombstoned redeclaration quadratic blow-up | numbers had to be re-run (`18ff32d`, `5e5c744`, `1cf404a`) |

### A1. SymTabV2 (interning + block front-coding + free-list slots) — memory failure
- Measured **1.87×–6.52× more physical heap than Conventional** on 6 real corpora (Zephyr 35.41 MB vs 5.43 MB; ESP-IDF 63.15 vs 18.22; CPython 15.25 vs 3.75).
- Root causes: (1) persistent never-pruned registries `everSeenRep_`/`poolLookup_` = 45–68% of V2 peak heap; (2) only *logical* slot reclamation on scope exit vs Conventional's physical `pop_back()`; (3) SSO makes Conventional pay 0 heap for ≤15 B names, V2 pays ~120 B metadata/symbol.
- Redesign (lifetime-aware registry, single 44 B slot): narrowed to **1.14–1.35× Conventional** — still lost; beat Interned by 1–6%. `docs/research/v2_real_world_memory_diagnosis.md`, `v2_memory_redesign.md`.

### A2. SymTabV3 (32 B representation-aware slots) — still loses to Conventional everywhere
- Beats V2 and Interned 20/20; **loses to Conventional on memory 20/20 (later 26/26)**. Forcing Conventional to heap-allocate every string barely changed it (31.65 vs 31.51 MB): the advantage is structural (one map node per symbol vs 2–3 structures), *not* only SSO. `v3_overhead_and_representation_aware_analysis.md`, `docs/latency_claim_reconciliation.md`.
- Latency gate (p95 ≤ 1.25× baseline): vs generic Conventional passes 7/20 (original framing) or 20/26 (later recomputation — inconsistent, see §6); vs EmbeddedConventional on Zephyr **p95 = 1.824× → gate FAILED** (0.228 vs 0.125 µs; 8.8% of lookups hit front-coded entries). `results/FINAL_CLAIMS_AUDIT.md`.
- Joint gate (memory < Conventional AND p95 ≤ 1.25×) over 26 corpora: **0/26**. Only positive: LVGL p95 0.356× (deep-scope variance). `corpus_screening_stage2.md`.

### A3. Block / incremental prefix front-coding as the compression mechanism
- Reported **2.4× lookup-latency slowdown and <4% memory saving** (sequential ancestor decoding, slice-header overhead) → abandoned for inline SSO. `docs/research/FAILED_APPROACHES.md`, `ARCHITECTURAL_EVOLUTION.md`.
- Caveat: V3 *did* save 20.2% vs EmbeddedConventional on Zephyr in the Phase-I paper run (see §6 on that baseline).

### A4. Hot/cold demotion & promotion (COMPRESSED↔INTERNED)
- Modest benefit (~5% memory) at +13–20% mean latency on its target workload; on a "sustained-hot" workload it **thrashed**: 15× more promotions (9000 vs 600), 8960 demotions, same ~5% memory — because the idle clock is a *global* resolve-tick counter. Left **disabled by default** (`coldIdleEpochs = 0`). `docs/research/hot_cold_design.md`, `results/demotion_experiment.csv`.

### A5. SymTabV4 (24 B core entry + sparse `HotMeta` side table) — negative
- Differentially fuzz-verified correct (200 k steps, ASan/UBSan, 0 mismatches) but **lost to V3 on 19/20 corpora** (only Lua −1.42%) and **to Conventional 20/20** (median ≈ +17%, range +4.9…+40.3%). Latency gate passes 6/20 (provisional).
- Both side-table implementations failed: `unordered_map` per-node malloc; then Robin-Hood `HotMetaTable` was *worse* on some corpora (cJSON +10% vs +4.8%) because it never shrinks under promotion churn (FreeRTOS: 7105 promotions). Side table costs ~34–57 B per compressed entry vs 8 B saved per inline entry. `docs/research/v4_evaluation.md`.
- Microbenchmarks A–F for V4 were never completed.
- **Naming conflict:** `ARCHITECTURAL_EVOLUTION.md` describes "V4 = arena-backed segmented table"; the code/eval docs define V4 as the side-table design above.

### A6. Pure string interning as a win
- Global pool stores each unique string twice (`pool_` + key of `poolLookup_`, ~68 B node overhead). Interned loses to Conventional **20/20** (closest Nginx +21.4%; protobuf-generated-cpp +79.7%); +15–25% / +18% latency from global lookups on short-lived scope variables. `interning_sanity_check.md`, `ordering_analysis.md`.

### A7. ML/learned policy selection
- ExtraTrees/DT/Ridge/Lasso/RF/SVR/KNN threshold predictors: 22–55% latency-constraint violations on synthetic workloads, 42 µs inference overhead (also reported +350 µs and >450 KB model); a 4-line hand heuristic achieved 0% violations with 0.5 µs; "negative regret" was an artifact of violating the latency bound. Ridge exported to `predicted_thresholds.hpp` but superseded. `results/adversarial_review.md`, `FAILED_APPROACHES.md`.

### A8. Review-era comparison baselines (2nd-class ideas)
| Idea | Result | Source |
|---|---|---|
| Trie shared-prefix storage | Worse memory than Conventional on 7/8 datasets (as low as 0.15× on `memory-stress`); only fast absent-key rejection | `docs/trie.md` |
| Robin Hood baseline table | Faster lookups; slack memory ≈ 80% of data size (`high-prefix-similarity`) | `docs/robinhood.md` |
| Hash functions (Murmur3, DJB2 vs FNV-1a) | Barely matters | `docs/algorithm_comparison.md` |
| BudgetSym vs Interned on real corpora | Within ~0.03× of each other (2.3–2.5× vs Conventional model); differentiation only on synthetic prefix datasets | `docs/review2_status.md` |
| Conventional-HeapString isolation baseline | Conventional's win is not SSO-driven (delta −2.9%…+1.5%, not the earlier "0.4–2.2%") | `docs/audit_data_reconciliation.md` |

### A9. Corpus-selection hypotheses
- `protobuf-generated-cpp` rejected as a "winning workload": mean identifier length 18.3 B (needed ≥22), prefix similarity 0.065 (needed ≥0.35); V3 fails both gates (heap 1.774×).
- nanopb, TinyUSB, LVGL, OpenThread, MbedTLS2, CMSIS added as "favorable/negative-control" corpora — none produced a joint win. Discriminators tried (prefix similarity, mean length, reuse ratio, scope-depth variance) did not cleanly separate winners from losers. Memory gate passes **0/26** corpora. `corpus_screening_stage2.md`.
- Adversarial paper review of SymTabV2: **REJECT AND REVISE** — over-generalized memory claim (Conventional up to 4× smaller on N<150 because of ~8 KB fixed directory), regex-based extraction (no macro/template expansion), ML complexity unjustified.

---

## 3. Track B — METIS-X (flat Robin-Hood, 32 B slot, 12 B SSO, scope frames)

### B1. Reported wins and why they are weaker than they look
- Reported: Zephyr heap 41.74→25.79 MB (−38.2%; later R=7 validation −40.2%), p95 0.119→0.083 µs; ESP-IDF −17.5% heap, p95 −48.5…−56.7%; ablation waterfall 43.44→5.59 MB (−87.1%); 0 hot-path allocations over 2,863,367 lookups.
- **Baseline problem:** the −78%/−87% numbers are vs `EmbeddedConventional` (A3, 21.5 MB) / the straw ablation start A2 (flat RH with per-string heap). Against the *host* Conventional baseline the same METIS-X run is **larger** (4.73 MB vs 2.25 MB final heap) — host Conventional destroys scope maps on exit; METIS-X keeps slot capacity. (Phase 2.1 reconciliation called the "78.9% vs host" statement an arithmetic mislabeling.)
- Phase-I paper Table I used EmbeddedConventional Zephyr = 66.91 MB (run `8aa8245`); committed CSV later = 41.742 MB (allocator-state / missing `resetPeak()`); paper never updated. `docs/METIS_X_BASELINE_RECONCILIATION.md`.

### B2. Claims falsified by audit
- "O(1) scope unwind" → actually **O(P)** (probe-cluster length). "Cache-line aligned slot" → no `alignas(32)`. "89.87% long identifiers" → refers to *final live global entries* in ESP-IDF (137,968/153,518 heap slots), not all inserts; 12 B SSO therefore **fails to capture the resident global table** while winning on transient locals.
- Latency numbers for LLVM ScopedHashTable (0.25–0.45 µs) / Clang IdentifierResolver (0.20–0.35 µs) were uncalibrated third-party ranges, reclassified non-comparable; SwissTable/F14/LLVM descriptions in the novelty screen had factual errors corrected.
- Failure cases (`results/metis_x_failure_cases.csv`): `heavy_shadowing_20_levels` p95 0.186 µs (+169% vs typical); 20–50 B names → 5000/5000 heap slots (0% inline); high uniqueness 73% inline.

### B3. Killed algorithmic candidates (research/l3, Oct 10)
| Candidate | Idea | Kill reason | Evidence |
|---|---|---|---|
| **TS-TFDR** | Transient-scope tombstone-free displacement rollback log (patent candidate 1; selected as the single "L3" contribution) | Robin-Hood backward-shift already restores layout exactly (tested); `exitScope()` = 4.6–7.8% of total replay (11.5% in a 10k-decl worst case) → Amdahl ≤8% gain; logging would burden the hot insert path | `phase2_2_exit_cost_measurements.md`, `phase2_2_layout_restoration_test.md` |
| **Candidate D** | 4 B prefix + 32-bit offset into a monotonic arena | ESP-IDF peak heap +35.9%, final +46.1%, p95 +58%, insert +28% (only #allocs −77%) ; Zephyr peak +32.9% | `phase4_candidate_d_results.md` |
| **Candidate F** | 12 B SSO + 8 B prefix + LIFO scope-tied arena | +20–35% peak (vector geometric growth), +124% latency from arena indirection; `ptmalloc` fallback optimal for this workload | `phase5_*`, `phase7_publication_viability.md` |
| Grouped probing (SwissTable/F14/Hopscotch) | fix Robin-Hood clustering | prior art exhausted; would be a port | `phase5_research_space_inventory.md` |
| Remove `frameIndex` (+4 B SSO) | | needed for O(1) bidirectional scope-frame updates; removal → O(S) scans | same |
| Faster shadowed-name lookup (shadow list) | | depth rarely >2 in embedded C; breaks flat 32 B layout; low impact | same |
| Separate SIMD metadata array | | SwissTable-style; breaks unified SSO cache locality (dual lookup) | `phase6_residual_research_space.md` |
| Segmented/chunked arena | | LLVM BumpPtrAllocator / ptmalloc already do it | same |
| Interning/dedup | | 73.3% of embedded declarations unique; secondary table costs more than 26.7% saving | same |
| **LS-ZCVM** (source-span zero-copy virtual materialization) and **GS-SRER** (generation-tagged slot recycling) | patent candidates 2 and 3 | **Never implemented or measured**; LS-ZCVM had an explicit falsifier (>50% growth from page padding). Listed as prior-art-risky. | `docs/patent/INVENTION_GAP_ANALYSIS.md`, `phase2_go_no_go.md` |

### B4. Novelty / publication verdicts
- Phase 6: "no isolated novel algorithm" — Robin Hood (Celis 1986), backward-shift deletion, SSO, scope stack are all prior art. Recommendation then: write an empirical systems paper (Framing B). Mock reviews: Reviewer 1 Weak Accept as experience report; Reviewer 2 **Borderline Reject** (trace replay ≠ compiler, no end-to-end integration).
- Patent docs: component-level prior art for every METIS-X piece; combination only; statutory-bar warning for earlier public commits/preprints.

### B5. Real-compiler integration (Phase 8–9) — fatal
- TCC interns identifiers at the lexer (`tok_alloc` → integer token `v`) and resolves scope via `table_ident[v]` array + saved pointer; Clang `IdentifierResolver` chains `Decl*` in the interned `IdentifierInfo`; CPython uses isolated per-block dicts + global interning; Lua uses ≤200-local linear scan with interned pointer equality; template engines: identifier resolution not the bottleneck.
- Key representation options (raw strings / interned pointers / integer IDs) all destroy METIS-X's premise: raw-string scoped hashing is a pattern production systems deliberately designed away. Phase 9: **"NO VIABLE TARGET — REFRAME OR STOP"; negative expected research value.**
- Unverified residue: L1-residency claim (>90% lookups in L1) never measured with perf counters. `research/l3/project_closure.md`.

---

## 4. Track C — Zephyr runtime symbol-table compression (flash)

Workloads: hello_world mps2 (N=158), shell_module mps2 (557), echo_server mps2 (1,381), hello_world nrf52840 (226). Clang 22.1.8/LLD, `-Os`, stock `CONFIG_SYMTAB=y` 19,792/89,112/291,936/30,200 B.

| # | Attempt | Outcome |
|---|---|---|
| C1 | Address-order front-coding of names | only 23% string reduction (2,426 vs 3,144 B) — addresses don't group prefixes |
| C2 | Byte-pair token compressor (≤65 → 64 tokens) with K=1 / K=16 checkpoints, reentrant zero-RAM decoder | pre-registered kill threshold (>200 B net) **passed** (1,664 B). Net savings vs stock: 1,656 / 6,424 / 16,504 / 2,140 B. Later fairness audit: **loses to Kallsyms M=256 by 1,216 B on echo_server**; wins small workloads; "engineering result only" |
| C3 | Baseline-accounting errors | "4,092 B delta" was a cross-libc subtraction (MINIMAL_LIBC vs PICOLIBC); true delta 4,416 B (minimal) / 4,488 B (picolibc) |
| C4 | Unfair-baseline concerns | re-built Kallsyms faithfully (256 tokens, length-prefixed, markers M=256/M=16); stock adapter and consumer forced via `-Wl,-u` (retained, never executed) |
| C5 | **ODHST** (address-order ID table + sorted front-coded name dictionary + token suffixes) | pre-registered gate: ≥5% whole-image win on ≥3 workloads. Payload lower bound 2,195/7,382/18,062/3,046 B vs ≤1,853/4,210/5,665/2,453 B needed → **NO-GO**. 4-byte address index alone = 5,524 B on echo_server |
| C6 | **Four-family tournament** (A adaptive-width blocks, B1/B2 restarted delta blocks, C ODHST-v2, D DP-synthesized variable blocks) + Kallsyms-names hybrids | Best (B1, B=16, FC16): −3.09/−2.46/−1.63/−3.48% whole image vs best prior (geomean 2.66%), ~20–25% of feature, 0 static RAM, 0 mismatches. **But**: control with raw offsets fails (0.61%); with the same compact address index given to conventional name schemes the gain falls to **0.26%** and fails the gate; family A beaten, D loses to fixed blocks (directory + decoder), FC helps only N≳500, FSST-style estimate within 2% on echo_server. Prior art: Levin 2026 Linux lineinfo delta-ULEB128 blocks (64/block, binary search + sequential decode), Biesheuvel relative offsets, Cromie direct name index, Partitioned Elias–Fano (variable partition DP). **Verdict ENGINEERING-ONLY — NO DEFENSIBLE NOVELTY** |

Not done in Track C: FSST/OptFSST built into firmware; firmware executed (no ARM QEMU); fixed-point re-link for variable-length tables (addresses embedded from the stock image); more than 4 workloads / one toolchain; physical-MCU timing.

---

## 5. Cross-cutting lessons (patterns that killed every direction)

1. **Baseline strength decides everything.** Wins appeared only vs weak/embedded-specific baselines (EmbeddedConventional, straw ablation A2, raw-offset Kallsyms) and vanished vs host Conventional, Interned-with-fixes, or "same trick applied to baseline".
2. **SSO regime.** 59–76% of real identifiers are ≤15 B; any scheme that adds metadata per symbol loses to `std::unordered_map`+SSO in memory.
3. **Compression ⇒ latency.** Every reconstruction-based scheme (front coding, trie, block decode) failed a p95 ≤1.25× gate or the 1.25× was only met on selected corpora.
4. **Fixed overheads create crossovers** (V2 ~8 KB directory, Kallsyms 256-token table ≈1.9 KB, ID tables, never-shrinking side tables).
5. **Novelty gate.** Every mechanism has prior art; combinations repeatedly judged "assembly"; claims like "O(1)", "first", "cache-aligned" repeatedly had to be retracted.
6. **Workload premise.** METIS-X's raw-string scoped lookup does not exist in production compilers.
7. **Measurement hygiene bit repeatedly**: stale runs in papers, different baselines mixed, extractor bugs, single-run timings, lower bounds mistaken for results.

---

## 6. Known inconsistencies inside the repo (verify before trusting)
- V3 latency gate: "7/20 corpora pass" (`latency_claim_reconciliation.md`) vs "20/26 pass" (`corpus_screening_stage2.md`) — different gate definitions; the docs flag the mismatch.
- EmbeddedConventional Zephyr heap: 66.91 MB (paper, `8aa8245`) vs 41.742 MB (committed CSV, Phase II). V3 Zephyr: 53.39 MB (paper) vs 28.224 MB (committed CSV).
- METIS-X Zephyr heap: 25.79 MB (canonical, 2.65 M events) vs 24.95 MB (R=7 validation) vs 4.73 MB (ablation harness on the 703 K-declaration stream).
- "78.04% vs EmbeddedConventional" and "87.1% vs ablation A2" are not comparable to each other or to host Conventional.
- V4 means two different designs across docs (§A5).
- Historical Zephyr closure numbers vs re-linked in the tournament harness differ by 0…56 B (<0.03%).

---

## 7. Open / unexplored (not killed — candidates for a new plan)
- **LS-ZCVM, GS-SRER** (patent candidates 2–3): never built; GS-SRER may be near-trivial prior art (generational indices).
- Shadow-heavy lookup acceleration (p95 +169% at 20 levels) — judged low-impact for embedded C, but not tested on C++/macro-heavy code.
- L1 residency with hardware counters (`perf`), real-MCU timing for any symbolization scheme.
- Building FSST/OptFSST into the Zephyr symtab harness; executing images (needs ARM QEMU); fixed-point re-link of variable-length tables; many more firmware workloads (non-Zephyr RTOSes, bigger configs) before any publication claim.
- Reframes that don't depend on novelty: (a) the interning break-even model + 26-corpus negative-result study; (b) a Zephyr/upstream engineering contribution (compact address index + token names; ≈2–3% image, 20–25% symtab); (c) a measurement-methodology paper on baseline fairness in symbol-table/flash-compression claims (the repo contains many concrete case studies).
- Anything outside symbol tables entirely (the compiler line is formally stopped).

---

## 8. Hard constraints a new plan should respect
- Do not overwrite frozen manifests / historical results (`data/zephyr_symtab_*.json`, `results/*canonical*`, `results/fairness_audit_closure_results.*`).
- Evidence rules used throughout: zero synthetic data as primary evidence; same toolchain/flags/libc for all compared builds; consumer must really call the lookup; report whole-image bytes, not just payload; pre-register gates before measuring; publish failures; never label address search O(1).
- Gate-keeping hooks in the working environment require a short "facts" statement before creating files and before destructive commands.
- Local toolchain: `~/local-toolchain` (clang/LLD 22.1.x), Zephyr tree `corpora/zephyr` @ `403a7d79`; no `qemu-system-arm`.

---

## 9. Source index
`docs/research/{FAILED_APPROACHES,ARCHITECTURAL_EVOLUTION,v2_*,v3_*,v4_*,hot_cold_design,interning_sanity_check,ordering_analysis,corpus_screening_stage2,novelty,research_gap,RESULT_PROVENANCE}.md` ·
`docs/{trie,robinhood,caching,algorithm_comparison,latency_claim_reconciliation,audit_data_reconciliation,METIS_X_*,review2_status,final_research_audit}.md` ·
`docs/patent/*.md` · `results/{FINAL_*,adversarial_review,REVISED_EXPERIMENTAL_PLAN_AND_REAL_WORKLOAD_RESULTS}.md` ·
`research/l3/**` (phases 2–9, `project_closure.md`) · `research/odhst/` · `research/metis_arch/REPORT.md` · `CLAUDE_RESEARCH.md` · `git log`.
