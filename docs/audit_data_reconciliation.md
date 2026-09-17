# Phase 0 — Benchmark Data Reconciliation

## What changed

`src/real_world_bench_main.cpp` only iterated 10 of the 20 corpora that have
extracted event traces in `data/corpus_events_*.txt` (missing: cJSON, curl,
FFmpeg, LLVM, mbedTLS, Nginx, protobuf-c, QEMU, Redis, SQLite). It was rebuilt
unchanged except for extending the `corpora` vector (line 436) to all 20 names,
then rerun clean:

```
g++ -std=c++14 -O2 -Wall -Wextra src/real_world_bench_main.cpp -o real_world_bench.exe
./real_world_bench.exe
```

Output: `data/real_world_benchmark.csv` now has 120 data rows (20 corpora ×
6 representations: Conventional, Conventional-HeapString, Interned, BudgetSymV1,
SymTabV2, SymTabV3), up from 61 rows (10 corpora × 6, with one representation
missing on one row). Old file preserved as
`data/real_world_benchmark.csv.bak_before_reconcile`. Copied to
`results/real_world_benchmark.csv` (that path previously didn't exist —
only `data/` had it).

## Headline claims re-verified against the full 20-corpus run

| Claim | Prior status | Now |
|---|---|---|
| V3 beats V2 on physical/tracked heap, 20/20 | Verifiable only on 10/20 | **Confirmed 20/20** |
| V3 loses to Conventional on heap, 20/20 | Verifiable only on 10/20 | **Confirmed 20/20** |
| V3 beats Interned on heap, 20/20 | Verifiable only on 10/20 | **Confirmed 20/20** |

These three "20/20" claims in `docs/v3_overhead_and_representation_aware_analysis.md`
were true in spirit but not previously checkable against a single consolidated
20-row dataset — they are now directly verified.

## Corrections needed to existing docs

1. **Conventional-HeapString vs Conventional delta.** `docs/v3_overhead_and_representation_aware_analysis.md`
   (~line 400-402) states the delta is "0.4-2.2%". On the full 20-corpus set the
   actual range is **-2.94% (cJSON) to +1.45% (LLVM)**, with most corpora under
   0.5% in magnitude. The claimed floor (0.4%) and ceiling (2.2%) are both violated
   by corpora not in the original 10. The qualitative conclusion — Conventional's
   advantage over Conventional-HeapString-isolated-SSO is small and structural,
   not SSO-driven — still holds, but the specific numeric range in the doc must be
   corrected to **-2.9% to +1.5%**.

2. **Absolute latency figures are run-dependent, not corpus-count-dependent.**
   The 7-corpus latency table in `docs/v3_overhead_and_representation_aware_analysis.md`
   (line 117-125, e.g. FreeRTOS Conv p95=0.148us, V2=0.398us, V3=0.416us) does not
   match the new run's FreeRTOS numbers (Conv p95=0.199, SymTabV2=0.511,
   SymTabV3=0.516) in absolute terms -- these come from a different machine/run,
   not from a corpus-selection difference (heap numbers for the same run agree to
   within ~0.007%: 4,783,784 new vs 4,784,104 doc). **The ratios are consistent**
   (V3/Conv p95 ~ 2.6-2.8x in both runs), and the qualitative conclusion the doc
   draws from them -- "V3 p95 <= 1.25xConv? No" for FreeRTOS/Arduino -- is
   reproduced exactly on the new run. Absolute latency numbers should not be
   treated as cross-run-comparable; only ratios/pass-fail against the 1.25x
   threshold are stable. `docs/real_world_results.md` and
   `docs/v3_overhead_and_representation_aware_analysis.md` should each add a note
   that absolute latency us values are hardware/run-specific and only relative
   comparisons within one run are meaningful -- this matches the disclosed
   ML-threshold-predictor non-determinism already noted in `docs/review2_status.md`.

3. **`docs/real_world_results.md`'s per-corpus V2 heap table** (e.g. FreeRTOS
   peak=5,684,880) diverges ~9% from the new canonical run (SymTabV2
   peak=5,185,864). This is a stale prior run, not a data-entry error -- the new
   `data/real_world_benchmark.csv` / `results/real_world_benchmark.csv` is now
   the canonical source; `real_world_results.md`'s table should be regenerated
   from it rather than hand-copied again.

## Known caveats carried forward (not fixed by this reconciliation)

- Heap is a **modeled/tracked** metric (`include/memory_tracker.hpp`, deterministic
  per-entry byte accounting), not OS-level RSS or malloc-hook-measured physical
  memory. This affects every "heap" number cited above and in every later phase.
- The ML threshold predictor's effect on compression-ratio figures
  (`docs/review2_status.md`) was not re-audited in this pass -- Phase 1 should
  check whether `real_world_bench_main.cpp` invokes any ML-driven threshold at
  all (a quick grep shows it does not: it only links `budget_sym.hpp`,
  `symtab_v2.hpp`, `symtab_v3.hpp` directly with fixed policy configs), so the
  real-world benchmark numbers above are **not** subject to that specific
  non-determinism -- only the standalone ablation/compression-ratio CSVs
  (`results/benchmark_results_v3.csv`) are.

## CSV schema (for reference)

`corpus,implementation,declarations,uses,unique_names,modeled_peak_bytes,modeled_final_bytes,measured_peak_heap_bytes,measured_final_heap_bytes,measured_bytes_per_unique_symbol,lookup_p50_us,lookup_p95_us,lookup_p99_us,lookup_mean_us,insert_p50_us,insert_p95_us,insert_p99_us,insert_mean_us,promotions,demotions,reconstruction_count,reconstruction_steps_total,mean_reconstruction_depth,count_inline,count_interned,count_compressed`
