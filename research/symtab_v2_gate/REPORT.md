# SymTab V2 (BUDGET-SYM) — Novelty and Experimental Validation Gate

Scope: compiler symbol table project (NOT the Zephyr firmware METIS project). Nothing committed. Old reports/CSVs untouched.
Everything here is reproducible from `research/symtab_v2_gate/` (`scripts/`, `src/`, `raw/`).

## 1. Verdict: **C. ENGINEERING RESULT ONLY — CONTRIBUTION NOT ESTABLISHED**

On semantically valid traces V2 is Pareto-dominated by simpler designs. It uses 2.0–2.7x the physical heap of a compact scoped identifier table. Its median hit is only about 12–31 ns faster than std::unordered_map-based tables, while its tail, insert, scope-exit and aggregate costs are 2–4x worse. No tested regime has V2 winning on memory against the best baseline (0 of 252 synthetic configs, 0 of 6 real corpora). The effective mechanisms (slot recycling, the adaptive policy) improve V2 relative to its own naive variants, not relative to the field.

## 2. Verified V2 architecture (from `include/historical/symtab_v2.hpp`, not the PRD)

- One `PackedEntry` per live declaration (declId, scopeId, accessCount, epoch, nameLen, flags, 20-byte union payload).
- Per-scope open-addressing index: Robin-Hood probing, slots `{fp32, slotId, dist, occupied}` (16 B). "Fingerprint" is the full 32-bit hash folded from FNV, used as probe key and equality prefilter. It is a cached hash, not a 1-byte fingerprint. A genuine 8-bit `fp8` exists only inside compressed block members.
- Three tiers: INLINE if len<12; INTERNED if repeat or len 12–13; COMPRESSED (front-coded blocks of 32, anchor every 8, each `BlockMember` holds a `std::string` suffix) if len>=14 and not repeat. Promotion COMPRESSED→INTERNED after 3 accesses.
- `liveSeenRep_`: an `unordered_map<string,uint32>` holding a full copy of EVERY live name regardless of tier, used only to decide "is this a repeat". Interned names are stored again (pool vector + `poolLookup_` key). So the compressed tier cannot reduce physical memory below one full copy plus overhead.
- Lookup walks scopes innermost to outermost (O(depth), a hash probe per scope); failed lookups probe every scope.
- Scope exit frees/recycles slots, pool refs and blocks (genuine lifetime reclamation).
- NOT present: lookup cache/memoization (`lookup_cache.hpp` is not used by V2, so that ablation is N/A), a learned predictor (`predicted_thresholds.hpp` is not wired in), a 1-byte fingerprint index.

## 3. Reconciliation of historical results (preserved, not edited)

| Source | Conventional FreeRTOS final heap | V2 | Status |
|---|---|---|---|
| pasted table / `docs/real_world_results.md` | 3.74 MB | 5.61 MB | Not reproducible from current code/traces. Conventional equals the older `data/real_world_redesign_benchmark.csv` (3,744,448); the V2 value matches no committed CSV. |
| `data/real_world_benchmark.csv` (committed) | 1.65 MB | 3.02 MB | **Reproduced exactly** in a scratch copy (1,647,264 / 3,022,040). |
| `results/embedded_benchmark.csv` | 1.65 MB | n/a | different baseline set |

Under all committed data V2 uses more heap than Conventional on every corpus (1.8x on FreeRTOS). On the doc's "lower median latency" claim: in the committed CSV V2 p50 = 0.095 µs vs Conventional 0.094 (equal), and V2 p99 = 0.966 vs 0.298 µs (3.2x worse). Re-running the old harness gave p50 0.144 vs 0.146 µs. "46–65% reduction" is relative to BudgetSym V1 (a 5–10x bloated baseline) only. Measurement types: `modeled_*` = analytic tracker (V2's modeled 1.4 MB vs 0.5 MB Conventional already shows V2 self-reports larger); `measured_*` = allocator (operator new, malloc_usable_size), i.e. physical.

## 4. Benchmark methodology audit

The old traces (`scripts/extract_corpus_events.py`, `extract_and_characterize_all_corpora.py`) are **not valid symbol-table workloads**: a regex token stream where any unseen identifier becomes a DECLARE (struct members, call targets, macro args, enum values), one global table across the whole source tree (never reset per translation unit), brace counting without preprocessing. `data/real_world_corpus_characterization.csv` shows 54% of FreeRTOS "declarations" (39,209/72,376) are same-scope redeclarations, which C forbids. The old harness also calls resolve twice per USE. Old corpus claims should not be used for compiler-workload claims.

Minimum correction (`src/trace_extract.cpp`, libclang C API, real AST): DECLARE for Var/Function/Param/Typedef/EnumConstant at true lexical scope (visible before its own initializer); USE for resolved DeclRefExpr and typedef TypeRef (named by the referenced entity); scopes for prototype, function body, compound stmt, for. One fresh table per translation unit including headers, as a compiler does. Excluded namespaces: tags, members, labels, macros. TUs with parse errors excluded; `__builtin_*` uses excluded (compiler builtin table). Corpora (C only, those that parse cleanly in this checkout): Lua, cJSON, protobuf-c, FreeRTOS kernel, Redis deps, Redis src. mbedTLS was dropped because its tf-psa-crypto submodule is empty; curl/nginx were dropped because they need generated config headers. Characterization: `raw/trace_characterization.csv` (same-scope redeclarations 0.4–4.5% of decls, real shadowing, ~0 unresolved uses after the builtin filter). Failed lookups are absent from source traces, so **derived negative probes** (a real identifier declared later in the same TU, verified invisible) are injected 1 per 8 uses and labelled as derived. No access frequencies were invented. Synthetic workloads are separate (`raw/synth.csv`) and labelled synthetic.
Limits: C only (no C++ templates), 6 corpora, no embedded hardware, host timings (i7-13620H P-core, powersave governor, TSC timers with 150-cycle overhead subtracted).

## 5. Baselines and correctness

Same API (enter/exit/declare/lookup) for: Conventional and Interned (repo), AbslFlat (per-scope `absl::flat_hash_map`), **ScopedId** (my compact Clang-style design: names once in a byte arena, global open-addressing identifier table, shadow chain + LIFO undo stack), V2 and its ablations. Differential test against a reference on all 6 corpora, including post-scope-exit lifetime probes: **0 mismatches for all 15 implementations** (`raw/check_*.csv`). The ablation copy `symtab_v2_abl.hpp` (adds only `disableSlotRecycle`, `omitSeenMap`) matches the original within 0.11% heap (size-class noise; `./harness ident`).
Caveats: Conventional/Interned use std::hash while V2/ScopedId use FNV (hash confound not removed); ScopedId never frees names within a TU (standard compiler practice, but unfavourable under heavy churn, see synthetic); allocator is glibc malloc with usable-size counting.

## 6. Results (real traces; `raw/analysis_tables.md`, `raw/mem_*.csv`, `raw/time_*.csv`)

Physical peak heap, mean per TU (KiB), V2 / Conv / Abseil / ScopedId: lua 157/76/79/67; freertos 125/59/58/50; redisdeps 287/138/120/121; **redis 910/408/386/334**. V2/Conv = 2.0–2.7x and V2/ScopedId = 2.4–2.7x (bootstrap CIs in tables, all well above 1).
Latency (redis, ns, mean of 9 reps ±95% CI): hit p50 V2 87 vs Conv 111, Abseil 118, ScopedId 75; hit p99 1015 vs 427 / 359 / 226; decl p50 338 vs 199 / 169 / 105; scope-exit p50 215 vs 79 / 87 / 43 (p99 1145 vs 249 / 202 / 107); aggregate ns/event 289 vs 137 / 124 / 63. Paired differences: V2's median hit is faster than Conv/Abseil by 12–31 ns (CI excludes 0 on all 6 corpora) but slower than ScopedId by 11–26 ns. V2 is slower on p99, insert, exit and aggregate on every corpus (CIs exclude 0). V2 is faster than Conv on derived failed lookups (p50 ~70–85 ns) but slower than ScopedId.
Pareto: V2 is dominated by ScopedId (memory and aggregate time) on all 6 corpora. Against Conventional/Abseil it is non-dominated only on median hit latency, a narrow point contradicted by p99. No Pareto improvement over any fair baseline.

## 7. Ablations (real traces; peak heap on redis; latency in `analysis_tables.md`)

| Variant | redis peak (KiB) | Reading |
|---|---|---|
| V2 | 910 | |
| fixedInterned (adaptive policy off) | 1258 (+38%) | policy helps inside V2 |
| noCompress | 1095 (+20%) | compression tier saves ~17% inside V2 (vs its own bloated pool) |
| compressFirst | 1114 | worse than adaptive |
| noSlotRecycle / noReclaim | 1308 / 1322 (+44–45%) | slot recycling is the largest real mechanism; noReclaim makes exit cheap (p50 61 vs 215 ns) |
| noFingerprint | 910 (0%) | latency unchanged or slightly faster; the fingerprint adds no benefit |
| noSeenMap (diagnostic) | 651 (-28%) | duplicate name map is 28% of V2's heap |
| lookup cache | N/A (not implemented) | |

On real TUs V2's default is the oracle choice among its four policy variants in 100% of TUs (`raw/heldout_policy.txt`), so the in-family policy is sound, but the whole family is 2.4–2.7x larger than the best baseline.
Upper bound for ANY front-coding tier: ideal zero-overhead front-coding of distinct names saves only 5–9% of a compact table's peak heap (`raw/frontcoding_bound.csv`; 42–58% of name bytes, but names are a small part of per-symbol cost). Fixed descriptor overhead dominates.

## 8. Break-even model / held-out evaluation

From the layout: V2 per-symbol cost ≈ 48 B PackedEntry + ~23 B index slot + a seen-map node (~48 B + heap string if len>=16), plus either pool copies (2 more name copies + 68 B nodes) or a 40 B block member + suffix. Compressed saves at most the shared prefix (bounded at 5–9% above) while adding >=40 B, so a COMPRESSED break-even does not exist for identifiers of ordinary length. INTERNED beats INLINE only when a >=12-char name repeats across simultaneously live scopes.
Synthetic grid (252 configs: length 6–40, prefix sharing, depth 2–24, duplicate rate, Zipf; synthetic, not source-derived): minimum-memory winner = AbslFlat in 252/252. V2 < Conventional in 0, V2 < best baseline in 0, V2 < ScopedId in 24 (high-churn, mostly 6-char names, ratio 0.88–1.00, while V2 is still >=2.35x Abseil).
Split fixed before analysis (selection lua+cjson; validation protobuf-c+freertos+redisdeps; held-out redis): selection-chosen policy = V2 default; on held-out redis it has 0% regret vs the V2-oracle but is 2.73x the best baseline. No predictive model produced a held-out win over a fixed non-adaptive baseline, because no regime exists where adaptivity wins. The existing ML predictor is not wired into V2 and was not evaluated.

## 9. Novelty matrix (✔ = primary source seen in search this session; others from background knowledge, not re-verified)

| Closest work | Contribution | Overlap with V2 | V2 distinct? |
|---|---|---|---|
| Clang `IdentifierTable` + `IdentifierResolver` (LLVM source ✔) | Uniqued identifiers, per-name shadow chain, scope-exit unwinding | Interning, scope reclamation, O(1) lookup independent of depth | No; ScopedId ≈ this design and is smaller and faster |
| LLVM `ScopedHashTable` (✔) | Scoped hash table with per-scope undo list | Scope-slot recycling | Recycling is standard |
| Martínez-Prieto et al., *Practical Compressed String Dictionaries*, Inf. Syst. 2016 (✔) | Front-coding, bucketized dictionaries, compressed hashing/tries | Block front-coded identifiers with anchors | Static dictionaries; V2's version is a naive std::string implementation |
| Swiss Tables / Abseil (✔: 7-bit H2 control bytes, SIMD probe) | Hash fingerprints for early rejection | Fingerprint filter | V2's "fp" is a 32-bit cached hash with no measured effect |
| Adaptive Radix Tree (Leis et al., ICDE 2013), HAT-trie (Askitis & Sinha) — not re-verified | Adaptive per-node/bucket representation | Tiering by data characteristic | Adaptive representation is established |

One search for a learned per-key representation selector for symbol tables found nothing; that is an incomplete prior-art check, not evidence of novelty. Answers: the three-tier policy is not new and is not shown to beat fixed designs; slot recycling gives no capability the alternatives lack; the fingerprint is a cached-hash compare with no measured gain; an experienced engineer combining a compact table, interning and an undo stack (ScopedId, ~100 lines) gets 2.4–2.7x less memory and faster operations.

## 10. Corrected, defensible claims

Supported: (a) V2 reduces memory vs BudgetSym V1 on the old traces (historical, invalid-workload caveat); (b) within V2, the adaptive policy saves ~28%, the compression tier ~17% and slot recycling ~31% of peak heap; (c) V2's median successful lookup is 11–31 ns faster than std::unordered_map/Abseil per-scope tables (host, 6 corpora) but 11–26 ns slower than a compact scoped identifier table; (d) V2 is lifetime-correct on all tested traces.
Not supported: memory superiority; a latency advantage without tails; fingerprint speedups; "1-byte fingerprints"; "46–65% reduction" without the V1 qualifier; workload claims from the regex corpora; p-values from the old harness.

## 11. Remaining work and risks

Any publication should be an empirical study of scope-aware symbol-table design (compact shadow-chain table vs per-scope maps, plus the negative result that identifier compression is bounded at 5–9%). It would need: C++ corpora (templates, namespaces), more than 6 corpora, measurement on the target embedded hardware, a real Clang `IdentifierResolver` baseline, hash-function normalization, a ScopedId variant that frees names, and a rerun on a pinned-frequency machine. Risks: ScopedId is my own implementation; C-only traces; the 150-cycle timer overhead limits sub-100 ns resolution; powersave governor.

## 12. Next action

Stop V2 as a paper candidate. If a publication is still wanted, scope a short empirical design-space study around ScopedId vs per-scope maps with the front-coding bound, adding C++ corpora and target hardware.
