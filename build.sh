#!/usr/bin/env bash
# BUDGET-SYM build script. No CMake / make available in the target
# environment (verified), so this is a flat, explicit set of g++ invocations.
# Run from the repo root: ./build.sh
set -e

CXX=${CXX:-g++}
STD=-std=c++14   # this toolchain (MinGW.org GCC 6.3.0) lacks C++17 stdlib headers -- see docs/methodology.md
FLAGS="$STD -O2 -Wall -Wextra"

# Review-2 Phase 5: regenerate include/predicted_thresholds.hpp from
# scripts/train_threshold_predictor.py before compiling, per PRD Section 10
# ("run separately before the C++ build"). Optional and soft-skipped (same
# pattern as the corpora block below) -- sklearn/numpy/pandas/scipy are not
# required to build or run BUDGET-SYM itself, only to retrain the predictor.
# The generated header is committed, so a skip here just means the build
# uses whatever predicted_thresholds.hpp is already on disk.
echo "== Regenerating ML threshold predictor (Review-2) =="
if [ -f venv/bin/activate ]; then
    (source venv/bin/activate && python scripts/train_threshold_predictor.py)
elif python3 -c "import sklearn" >/dev/null 2>&1; then
    python3 scripts/train_threshold_predictor.py
else
    echo "  no venv/ and no system sklearn -- skipping retrain, using committed include/predicted_thresholds.hpp"
fi

echo ""
echo "== Building smoke test =="
$CXX $FLAGS tests/smoke_test.cpp -o tests/smoke_test.exe

echo "== Building differential test (cross-implementation resolve() agreement) =="
$CXX $FLAGS tests/differential_test.cpp -o tests/differential_test.exe

echo "== Building SymTabV2 compressed-tier test =="
$CXX $FLAGS tests/symtab_v2_compressed_test.cpp -o tests/symtab_v2_compressed_test.exe

echo "== Building CLI demo (budget_sym_demo) =="
$CXX $FLAGS src/demo_main.cpp -o budget_sym_demo.exe

echo "== Building benchmark =="
$CXX $FLAGS src/benchmark_main.cpp -o benchmark.exe

echo "== Building ablation =="
$CXX $FLAGS src/ablation_main.cpp -o ablation.exe

echo "== Building analyze (web dashboard's real backend engine) =="
$CXX $FLAGS src/analyze_main.cpp -o analyze.exe

echo "== Building grid search (Review-2: threshold optimization) =="
$CXX $FLAGS src/grid_search_main.cpp -o grid_search.exe

echo "== Building multiseed (Review-2: statistical validation) =="
$CXX $FLAGS src/multiseed_main.cpp -o multiseed.exe

echo "== Building corpus_bench (Review-2: real-world corpus evaluation) =="
$CXX $FLAGS src/corpus_bench_main.cpp -o corpus_bench.exe

echo "== Building memory audit (measured heap vs modeled bytes) =="
$CXX $FLAGS src/memory_audit_main.cpp -o memory_audit.exe

echo "== Building V2 memory audit (SymTabV2 vs Conventional/Interned/RobinHood) =="
$CXX $FLAGS src/memory_audit_v2_main.cpp -o memory_audit_v2.exe

echo "== Building V2 block-compression parameter sweep (P0.3) =="
$CXX $FLAGS src/block_compression_sweep_main.cpp -o block_compression_sweep.exe

echo "== Building V2 demotion ON/OFF experiment (ECC review H3 fix) =="
$CXX $FLAGS src/demotion_experiment_main.cpp -o demotion_experiment.exe

echo "== Building cache benchmark (docs/caching.md) =="
$CXX $FLAGS src/cache_benchmark_main.cpp -o cache_benchmark.exe

echo "== Building algorithm comparison benchmark (docs/algorithm_comparison.md) =="
$CXX $FLAGS src/algorithm_benchmark_main.cpp -o algorithm_benchmark.exe

echo "== Building V2 research-grade benchmark (CLAUDE_RESEARCH.md Section 9) =="
$CXX $FLAGS src/v2_benchmark_main.cpp -o v2_benchmark.exe

echo "== Building semantic corpus event benchmark (CLAUDE_RESEARCH.md Section 10) =="
$CXX $FLAGS src/corpus_event_bench_main.cpp -o corpus_event_bench.exe

echo "== Building Pareto optimization benchmark (CLAUDE_RESEARCH.md Section 12) =="
$CXX $FLAGS src/pareto_main.cpp -o pareto.exe

echo "== Building systematic parameter study benchmark (CLAUDE_RESEARCH.md Section 13) =="
$CXX $FLAGS src/parameter_study_main.cpp -o parameter_study.exe

echo ""
echo "== Running smoke test =="
./tests/smoke_test.exe

echo ""
echo "== Running differential test =="
./tests/differential_test.exe

echo ""
echo "== Running SymTabV2 compressed-tier test =="
./tests/symtab_v2_compressed_test.exe

echo ""
echo "== Running benchmark (writes results/benchmark_results.csv) =="
./benchmark.exe

echo ""
echo "== Running ablation (writes results/ablation.csv) =="
./ablation.exe

# Grid search sweeps 15,000 configs and takes several minutes; skip on quick
# iteration cycles with RUN_GRID_SEARCH=0.
if [ "${RUN_GRID_SEARCH:-1}" = "1" ]; then
    echo ""
    echo "== Running grid search (writes results/grid_search_full.csv, results/optimal_policy.csv; several minutes) =="
    ./grid_search.exe
else
    echo ""
    echo "== Skipping grid search (RUN_GRID_SEARCH=0) =="
fi

echo ""
echo "== Running multiseed (writes results/multiseed_raw.csv) =="
./multiseed.exe

echo ""
echo "== Computing multiseed statistics (writes results/multiseed_summary.csv) =="
python scripts/multiseed_stats.py || python3 scripts/multiseed_stats.py

echo ""
echo "== Real-world corpus evaluation (CLAUDE_RESEARCH.md Section 10) =="
CORPORA_FOUND=0
for corpus in freertos arduino-core zephyr; do
    if [ -d "corpora/$corpus" ]; then
        CORPORA_FOUND=1
    fi
done
if [ "$CORPORA_FOUND" = "1" ]; then
    echo "  -- extracting semantic event traces & computing characterization metrics --"
    (python scripts/extract_corpus_events.py || python3 scripts/extract_corpus_events.py)
    echo "  -- running semantic corpus event benchmark --"
    ./corpus_event_bench.exe
else
    echo "  no corpora vendored under corpora/ -- skipping real-world eval, see docs/corpus_setup.md"
fi

echo ""
echo "== Running memory audit (writes results/memory_audit.csv) =="
AUDIT_ARGS=""
for corpus in freertos arduino-core zephyr; do
    if [ -f "results/corpus_ids_${corpus}.txt" ]; then AUDIT_ARGS="$AUDIT_ARGS results/corpus_ids_${corpus}.txt $corpus"; fi
done
./memory_audit.exe $AUDIT_ARGS

echo ""
echo "== Running V2 memory audit (writes results/memory_audit_v2.csv) =="
./memory_audit_v2.exe $AUDIT_ARGS

echo ""
echo "== Running V2 block-compression parameter sweep (writes results/block_compression_sweep.csv) =="
./block_compression_sweep.exe

echo ""
echo "== Running V2 demotion ON/OFF experiment (writes results/demotion_experiment.csv) =="
./demotion_experiment.exe

echo ""
echo "== Running cache benchmark (writes results/cache_benchmark_results.csv) =="
./cache_benchmark.exe

echo ""
echo "== Running algorithm comparison benchmark (writes results/algorithm_comparison.csv) =="
./algorithm_benchmark.exe

echo ""
echo "== Running V2 research-grade benchmark (writes results/v2_latency_memory.csv) =="
./v2_benchmark.exe

echo ""
echo "== Running Pareto optimization benchmark (writes results/pareto_results.csv, results/pareto_frontier.csv) =="
./pareto.exe

echo ""
echo "== Running systematic parameter study (writes results/parameter_sweep.csv) =="
./parameter_study.exe

echo ""
echo "== Building and Running statistical validation (CLAUDE_RESEARCH.md Section 15) =="
$CXX $FLAGS src/statistical_validation_main.cpp -o statistical_validation.exe
./statistical_validation.exe
python scripts/statistical_validation.py || python3 scripts/statistical_validation.py

echo ""
echo "== Running Latency-Constrained ML Policy Evaluation (CLAUDE_RESEARCH.md Section 16) =="
if [ -f venv/bin/activate ]; then
    (source venv/bin/activate && python scripts/train_ml_oracle.py)
else
    python3 scripts/train_ml_oracle.py
fi

echo ""
echo "== Generating figures (requires python + matplotlib + pandas is NOT required, csv module only) =="
python scripts/plot_results.py || python3 scripts/plot_results.py

echo ""
echo "All done. Run ./budget_sym_demo.exe for the live walkthrough."
