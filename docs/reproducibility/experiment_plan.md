# Experiment Plan

## Tonight (done, results in `results/`)

1. **Correctness** -- `tests/smoke_test.cpp`: shadowing, scope reclamation,
   compression round-trip, promotion, memory-pressure-driven selection. Run
   via `build.sh`; must print `ALL TESTS PASSED` before anything else is
   trusted.
2. **Benchmark** -- `src/benchmark_main.cpp` across 8 datasets (small,
   medium, large, high-prefix-similarity, random-identifiers, nested-scopes,
   hot-cold-access, memory-stress), 3 implementations each ->
   `results/benchmark_results.csv` (24 rows).
3. **Ablation** -- `src/ablation_main.cpp`, 4 BudgetSym variants
   (Full / NoScope / NoAccessFrequency / NoAdaptiveSelection) on one fixed
   shared workload -> `results/ablation_results.csv` (4 rows).
4. **Figures** -- `scripts/plot_results.py` reads both CSVs, writes 5 PNGs to
   `figures/`.

## Explicitly out of scope tonight (see `docs/future_work.md`)

- Statistical significance testing (paired tests across repeated runs with
  varied seeds) -- tonight's numbers are single-run measurements from
  deterministic, seeded datasets, not averaged over multiple random trials.
  Stated as a limitation, not hidden.
- Real compiler integration (feeding this an actual C/C++ AST's identifier
  stream instead of synthetic datasets).
- Physical memory measurement (RSS/valgrind massif) to cross-check the
  Tracked Memory model against actual allocator behavior.
- Tuning `PolicyConfig` thresholds against a validation workload -- the
  current thresholds (`inlineMaxLen=12`, `compressMinLen=10`,
  `hotAccessThreshold=3`, etc., see `include/common.hpp`) are reasonable
  starting points chosen by inspection, not the result of a tuning sweep.

## What would extend this into a fuller research contribution

1. Run the benchmark across multiple seeds and report mean +/- stdev per
   dataset/implementation, then a paired significance test (the codebase
   already isolates each run cleanly enough to repeat N times with different
   seeds -- this is mechanical work, not a design change).
2. A small threshold sweep over `PolicyConfig` (grid or random search over
   `inlineMaxLen`, `compressMinLen`, `hotAccessThreshold`,
   `highPressureThreshold`) against a held-out workload, to check whether the
   current hand-picked thresholds are anywhere near a local optimum.
3. Feed it real identifiers extracted from an actual embedded C codebase
   (e.g. an open-source RTOS or firmware project) instead of synthetic
   datasets, to check whether the synthetic datasets' compression ratios
   generalize.
4. Physical memory cross-check: run the benchmark under a memory profiler and
   compare Tracked Memory's ranking (not absolute values -- the model is
   explicitly not claiming to match RSS) against real allocator behavior.
