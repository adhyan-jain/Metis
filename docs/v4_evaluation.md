# Phase 4 -- V4 Evaluation Results (HANDOFF: session ended early, see "Continuation" at bottom)

## Status: V4 implemented, differentially verified correct, measured on all 20 real corpora. Result: NEGATIVE. Microbenchmarks A-F NOT YET RUN.

## What was built

`include/symtab_v4.hpp` (`budgetsym::v4::SymTabV4`) implements exactly the
approved candidate: `CoreEntry` (24B, confirmed via `sizeof`) drops
`accessCount`/`lastAccessEpoch` from V3's `PackedEntry` (32B), moving them to
a sparse `HotMeta` side table populated only for COMPRESSED entries and
entries promoted from COMPRESSED to INTERNED. Two implementations of the
side table were tried:

1. **First iteration**: `std::unordered_map<uint32_t, HotMeta>`.
2. **Second iteration** (after measuring #1): a contiguous Robin Hood
   open-addressing table (`HotMetaTable`, reusing the `ScopeIndex` pattern
   already in the codebase) to eliminate per-node `malloc` calls, since the
   real allocator hook (`heap_counter.hpp`) showed `unordered_map`'s per-node
   allocation overhead was the dominant cost, not the architecture itself.

Both were verified correct: a 200,000-step randomized differential fuzz test
against `SymTabV3` (identical event trace fed to both, asserting identical
`insert` declIds, `resolve` hit/miss, `size()`, `exitScope()` reclamation
counts, and promotion/demotion counts) passes with **zero mismatches** under
`-fsanitize=address,undefined`. Correctness requirements 1-6 (lookup
semantics, lexical scope, shadowing, physical reclamation, promotion/
demotion, collision safety) are verified by this fuzz test, not just
asserted. Requirements 7-10 (baselines untouched, extractor untouched, timing
methodology untouched, V3 kept as named baseline) are satisfied by
construction: V4 is a new file, V3/V2/V1/Conventional/Interned/
Conventional-HeapString were not modified except `src/real_world_bench_main.cpp`
gaining a `runV4()` function alongside the existing `runV3()` etc.

## Full 20-corpus real-world results (physical heap, allocator-measured via `heap_counter.hpp`, `data/real_world_benchmark.csv`)

**V4 lost to V3 on 19/20 corpora** (only Lua improved, -1.42%) and **lost to
Conventional on 20/20** (worse margin than V3's own 20/20 loss -- median
~+17% vs Conventional, range +4.9% to +40.3%). **V4 beat Interned on 20/20**
(inherited from V3, unaffected by this change). The open-addressing table
(iteration 2) did not fix the regression versus iteration 1 -- on some
corpora (e.g. cJSON) it was measurably worse (+10.03% vs V3, vs +4.83% for
the unordered_map version), likely because `HotMetaTable` never shrinks
(mirrors `ScopeIndex`'s never-shrink policy) so promotion/demotion churn
(FreeRTOS: 7105 promotions in one run) drives it to a high-water-mark size
that isn't reclaimed, adding real allocator cost.

**Latency gate**: `cold_p95(V4) <= 1.25 * cold_p95(Conventional)` -- measured
pass rate is **6/20** corpora (not measured to full rigor -- single run, not
multiseed; this number should be treated as provisional).

## Root cause (diagnosed, not fully fixed)

The design note's theory (core-struct savings of 8B/entry x majority-INLINE
population should net-positive) was falsified in practice: introducing
**any** second container to hold `HotMeta`, even a compact one, costs more
in real allocator terms than the flat 8B/entry saved on `CoreEntry`, once
you account for:
1. The container's own baseline allocation (starts at capacity 16, grows by
   doubling) -- a fixed cost paid once per table instance regardless of how
   sparse the population is.
2. High promotion/demotion churn (FreeRTOS/QEMU-scale corpora produce
   thousands of promotions) repeatedly touching the side table, and the
   table's own growth is driven by peak occupancy, never shrinking.
3. `CoreEntry`'s union still costs 12B regardless of representation -- the
   8B/entry saved on the *scalar* hot/cold fields is small relative to the
   ~24-32B baseline, so the side-table's fixed overhead only needs to be
   modest to erase the gain. Quantified: cJSON (smallest INLINE fraction,
   699/2352 unique names) shows the side table costing ~34-57B **per
   COMPRESSED-track entry**, several times the 8B saved per INLINE entry,
   and cJSON's non-INLINE population (1653/2352, i.e. 70%) is large enough
   relative to its INLINE population (30%) that the trade is strictly
   negative overall.

**This directly validates the concern flagged in the design note's "What was
deliberately NOT done" section and the user's explicit warning**: "If a
metadata structure is needed globally, do not simply move the same
per-symbol cost from PackedEntry into another vector/map and call it a
win." The measurement confirms it is *not* a win as built -- INLINE entries
do genuinely avoid the cost (verified: `hasHotMeta()` is false for 100% of
INLINE entries, checked via the differential test's promotion/demotion
counters matching V3 exactly), but the side table's own overhead exceeds
what was saved, aggregated across the actual (not majority-favorable-case)
representation mix in real corpora.

## Break-even analysis update

Phase 2/3's equations (`docs/ordering_analysis.md`) assumed
`M_v3(k,L) ~= k*(S_slot+F)` with no separate side-structure cost. V4's
measured result requires adding a side-table term:

```
M_v4(k,L) ~= k_inline*(S_core) + k_hot*(S_core + S_hotmeta_marginal) + T_table(N_hot)
```

where `T_table(N_hot)` is the side table's own baseline+growth footprint
(nonzero even for one entry, since the table starts at capacity 16 slots).
For V4 to beat V3, need:

```
k_inline * (S_slot - S_core) > k_hot * S_hotmeta_marginal + T_table(N_hot)
```

i.e. `k_inline * 8B > k_hot * (measured ~34-57B) + T_table`. Since
`k_hot` (COMPRESSED-track population) is never zero in any of the 20 real
corpora (ranges ~10-70% of unique names depending on corpus), and the
per-entry marginal cost of the side table (34-57B) is 4-7x the per-entry
saving (8B), **this inequality does not hold for any of the 20 real
corpora measured**, matching the observed 19/20 (unordered_map) and 20/20
(open-addressing) losses to V3.

## Does V4 satisfy the primary gate?

`physical_heap(V4) < physical_heap(Conventional)` on any real workload:
**NO -- 0/20.** Per the plan's explicit fairness rule, this is reported
without cherry-picking; all 20 corpora lose, ranging from the closest
(LLVM, +4.9%) to the worst (mbedTLS, +40.3%).

## Recommendation: STOP (do not do a further architectural iteration on this specific approach)

The representation-conditional metadata idea, as specified (move hot/cold
fields to a side structure), does not survive contact with real allocator
measurement, under two different side-table implementations. The core-struct
saving (8B/entry) is real and correctly isolated to the INLINE majority, but
it is smaller than any side-table implementation's realistic per-entry
marginal cost for the minority population, given that population is never
negligible (10-70% non-INLINE across real corpora) and the side structure
has nonzero fixed cost. A genuinely free version of this idea would require
eliminating the side structure's own overhead entirely -- e.g., only
possible if hot/cold tracking piggybacks on a structure that already exists
for another reason (this was not attempted and is a legitimate, different,
next idea, not a variant of what was tried here).

**Recommend freezing V3 as the project's best-performing representation-aware
design**, documenting this V4 attempt and its negative, quantified result as
a genuine (disclosed, not hidden) negative finding for the paper -- it
strengthens rather than weakens the paper's honesty, and directly answers
the Phase 4 objective ("test whether representation-dependent metadata is
the missing structural mechanism") with a clear, evidence-based **no, not as
implemented here**.

## What was NOT completed (time ran out)

- **Synthetic microbenchmarks A-F** (100% INLINE, 90/10, 70/30, long-name,
  compressed-heavy, hot/cold mixed) were never built or run. These were
  specified to isolate the architectural effect independent of real-corpus
  noise and would likely make the root-cause diagnosis above even sharper
  (e.g., workload A, 100% INLINE, should show V4's *pure* upside with zero
  side-table population -- worth confirming the 8B/entry core saving in
  isolation; workload C, 70/30 INTERNED, would stress-test the side-table
  overhead in isolation).
- **Multiseed/statistical repetition** of the 20-corpus run was not done --
  this was a single deterministic run per corpus (same seed methodology as
  the existing `real_world_bench_main.cpp`, which itself does not currently
  do repeated seeds). The `data/real_world_benchmark.csv` numbers should be
  treated as point estimates, consistent with how V1/V2/V3/baselines are
  already reported in that file.
- **Full test suite / lint / broader sanitizer pass** requested in the
  CODE QUALITY section was not run beyond the targeted smoke test and
  200K-step differential fuzz test shown above. `tests/differential_test.cpp`
  and `tests/smoke_test.cpp` do not yet include V4 -- they were not extended.
- **The component-by-component memory breakdown** (bytes attributable to
  CoreEntry vs HotMetaTable vs pool vs blocks specifically) was inferred
  from aggregate deltas above, not measured directly via a dedicated
  instrumentation pass.

## Continuation (for the next agent/session)

1. Read this file and `docs/v4_design_note.md` first -- both are current
   and accurate as of this commit.
2. `data/real_world_benchmark.csv` is the latest run (includes V4).
   `results/real_world_benchmark.csv` still has the pre-V4 Phase-0
   reconciled file (V1-V3 + baselines only, no V4 rows). Decide whether to
   promote `data/`'s version to `results/` (`cp data/real_world_benchmark.csv
   results/real_world_benchmark.csv`) before citing it as canonical in any
   later phase -- verify row count first (should be 20 corpora x 7 impls +
   header = 141 lines).
3. Do NOT try another side-table implementation variant hoping for a
   different number -- two independent implementations (node-based hash map,
   contiguous open-addressing) both failed for the same structural reason
   (fixed table overhead exceeds the scalar-field saving). This is a
   sufficiently confirmed negative result per the plan's "do not keep
   iterating blindly" rule.
4. If you want to test the "next idea" flagged above (piggyback hot/cold
   tracking on an already-existing structure with zero marginal cost), that
   is a genuinely different architecture from what was tried here and would
   warrant a fresh design note, not a patch to `symtab_v4.hpp`.
5. Otherwise, the recommended next step is Phase 6/7: write
   `docs/final_verdict.md` and the paper-ready package treating V4 as a
   documented, disclosed negative result, and V3 as the project's frozen
   best design. `docs/ordering_analysis.md`'s break-even equations should be
   updated with the `T_table`/`S_hotmeta_marginal` terms derived above if a
   future pass wants full rigor there.
6. `include/symtab_v4.hpp` should NOT be deleted -- keep it as a disclosed,
   reproducible negative-result artifact (matches Requirement 10's spirit:
   every tried design stays available and named).
