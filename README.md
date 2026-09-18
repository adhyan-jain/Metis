# Metis

**An Adaptive Scope-Aware Symbol Table for Memory-Bounded Embedded Compilation**

A prototype (not a claim of theoretical optimality -- see `docs/novelty.md`)
compiler symbol table that picks, per identifier, between three storage
representations -- **INLINE**, **INTERNED**, **COMPRESSED** -- based on live
memory pressure, scope/lifetime, exact-repeat status, prefix similarity to
the previous declaration, and observed access frequency (with runtime
promotion once an entry proves "hot"). Benchmarked against two baselines
(`ConventionalSymbolTable`, `InternedSymbolTable`) on 8 synthetic datasets,
and ablated to isolate which mechanism contributes how much of the result.
Every number in `results/` and `figures/` comes from actually running this
code -- see `docs/methodology.md` for the exact accounting model and two real
toolchain bugs caught and fixed while building this.

---

## Quick Start & Running Commands

### ⚙️ Backend (C++ Compiler Engine & Python Environment)

#### 1. Build and Run All C++ Components & Plotting
Run the comprehensive build script to compile all C++ binaries, execute tests, run benchmarks, and generate visualization plots:

```bash
./build.sh
```

#### 2. Running C++ Components Individually

- **Correctness Smoke Tests**:
  ```bash
  g++ -std=c++14 -O2 -Wall -Wextra tests/smoke_test.cpp -o tests/smoke_test.exe && ./tests/smoke_test.exe
  ```
- **Interactive Live Walkthrough Demo**:
  ```bash
  g++ -std=c++14 -O2 -Wall -Wextra src/demo_main.cpp -o budget_sym_demo.exe && ./budget_sym_demo.exe
  ```
- **Benchmark Suite** (generates `results/benchmark_results.csv` across 8 datasets):
  ```bash
  g++ -std=c++14 -O2 -Wall -Wextra src/benchmark_main.cpp -o benchmark.exe && ./benchmark.exe
  ```
- **Ablation Suite** (generates `results/ablation_results.csv` isolating mechanisms):
  ```bash
  g++ -std=c++14 -O2 -Wall -Wextra src/ablation_main.cpp -o ablation.exe && ./ablation.exe
  ```

#### 3. Python Plotting & Virtual Environment

Set up the Python virtual environment and run the plot generator script:

```bash
# Create virtual environment & install dependencies
python3 -m venv venv
./venv/bin/pip install -r requirements.txt

# Generate PNG charts in figures/ from CSV results
./venv/bin/python3 scripts/plot_results.py
```

---

### 💻 Frontend (Next.js Interactive Web Dashboard)

The project includes a Next.js 16 + React 19 + Tailwind CSS web dashboard and live client-side symbol table simulator.

#### Commands to Run the Frontend:

```bash
# 1. Navigate to the frontend directory
cd frontend

# 2. Install dependencies (if not already installed)
npm install

# 3. Start the Development Server
npm run dev
```

Open your browser at `http://localhost:3000` (or `http://localhost:3001` if port 3000 is occupied) to view the application.

#### Production Build Commands:

```bash
# Build production bundle
cd frontend
npm run build

# Start production server
npm run start
```

---

## Layout

```
include/                       Header-only C++ implementations
  budget_sym.hpp                 Core adaptive symbol table (decide(), promotion, memoized reconstruction)
  lookup_cache.hpp                Review-2: 64-entry LRU lookup cache
  workload_profiler.hpp           Review-2: 5-feature workload profiler (100-symbol window)
  predicted_thresholds.hpp        Review-2: GENERATED Ridge-regression threshold predictor (do not hand-edit)
  conventional_symbol_table.hpp   Baseline #1
  interned_symbol_table.hpp       Baseline #2
  robinhood_symbol_table.hpp      Robin Hood open-addressing baseline, direct string-keyed (docs/robinhood.md)
  trie_symbol_table.hpp           Shared-trie compression baseline (docs/trie.md)
  hash_functions.hpp              FnvHash/Murmur3Hash/Djb2Hash -- swappable BudgetSymT<HashFn> backends
  memory_tracker.hpp, hires_timer.hpp, dataset_generators.hpp, bench_metrics.hpp, common.hpp
src/demo_main.cpp              -> budget_sym_demo.exe (live interactive demonstration)
src/benchmark_main.cpp         -> benchmark.exe (8-dataset x implementation comparison; v3 pass adds
                                   cache/ML-predicted variants -> results/benchmark_results_v3.csv)
src/ablation_main.cpp          -> ablation.exe (4-variant mechanism isolation)
src/grid_search_main.cpp       -> grid_search.exe (Review-2: 15,000-combination threshold sweep)
src/multiseed_main.cpp         -> multiseed.exe (Review-2: 30-seed statistical validation)
src/corpus_bench_main.cpp      -> corpus_bench.exe (Review-2: real-world corpus evaluation)
src/cache_benchmark_main.cpp   -> cache_benchmark.exe (LRU cache on/off comparison, docs/caching.md)
src/algorithm_benchmark_main.cpp -> algorithm_benchmark.exe (5-way hash/storage comparison, docs/algorithm_comparison.md)
tests/smoke_test.cpp           -> tests/smoke_test.exe (assert-based correctness checks)
scripts/
  plot_results.py                CSV -> figures/*.png plot script (needs venv/'s matplotlib)
  multiseed_stats.py              Computes 95% CI / p-values from multiseed_raw.csv
  extract_identifiers.py          Review-2: tokenizes real .c/.h sources for corpus_bench.exe
  train_threshold_predictor.py    Review-2: trains + compares 7 models, exports predicted_thresholds.hpp
corpora/                        Review-2: real-world source trees (FreeRTOS/Arduino/Zephyr), gitignored
                                 -- see docs/corpus_setup.md to (re)vendor them
frontend/                      Next.js + React + Tailwind CSS web dashboard and live simulator
venv/                          Python virtual environment (sklearn/numpy/pandas/scipy/matplotlib)
requirements.txt               Python package dependencies
results/                       All generated measured data -- see docs/architecture.md's component
                                 map for which binary/script writes which CSV
figures/                       PNG charts generated from results CSVs
docs/                          Research gap, novelty, methodology, architecture, experiment plan, Q&A,
                                 corpus setup, and docs/review2_status.md (Review-2 handoff status)
metis_v2.tex                    IEEE paper source (Review-2), compiled to Metis_v2_IEEE.pdf
DEMO.md                        5-minute presentation plan for faculty review
```

## Headline Result (from `results/benchmark_results.csv`)

Across 8 datasets, BudgetSym's tracked-memory **compression ratio vs the
conventional baseline** ranges from **1.24x** (`random-identifiers`, the
least favorable case) to **2.37x** (`high-prefix-similarity`), beating plain
string interning (which only reaches ~1.05x-1.07x on every dataset) in every
single case. The cost: BudgetSym's insert is consistently the slowest of the
three (more decision logic per symbol), and its lookup is somewhat slower
than both baselines due to the hash-then-reconstruct lookup path every
BudgetSym entry uses. This is a memory-for-latency trade, reported honestly
in both directions -- not a strict win, and we don't present it as one. Full
breakdown, including where the adaptive policy does and doesn't help, in
`docs/novelty.md` and `docs/faculty_questions.md`.

## What's Proposed vs. What's Established

See `docs/research_gap.md` and `docs/novelty.md` for the full breakdown. In
short: interning, front-coding, and scope-stack symbol tables are all
existing techniques, used here as baselines or as one representation among
three. The **proposed** part is the unified, budget/lifetime/frequency-aware
policy that chooses between them per symbol, and that adapts the choice
after insertion based on real access counts.
