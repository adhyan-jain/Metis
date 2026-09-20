#!/usr/bin/env bash
# METIS Research Artifact Master Reproducibility Script
# Target: adhyan-jain/Metis
# Strict reproducible pipeline: tests -> benchmarks -> reconciliation -> figures -> manuscript

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

CXX=${CXX:-g++}
CXXFLAGS="-std=c++14 -O2 -Wall -Wextra -Iinclude -Iinclude/historical"
PYTHON=${PYTHON:-./venv/bin/python3}
if [ ! -x "$PYTHON" ]; then
    PYTHON=python3
fi

mkdir -p bin build results figures

echo "================================================================="
echo "  METIS: Final Research Reproducibility & Verification Pipeline  "
echo "================================================================="
echo "Compiler : $($CXX --version | head -n 1)"
echo "Python   : $($PYTHON --version)"
echo "Date     : $(date)"
echo "================================================================="

MODE="${1:-all}"

if [ "$MODE" = "phase1" ] || [ "$MODE" = "all" ]; then
    echo ""
    echo "[Phase I] Running Correctness & Differential Fuzzing Tests..."
    $CXX $CXXFLAGS tests/smoke_test.cpp -o bin/smoke_test
    ./bin/smoke_test

    $CXX $CXXFLAGS tests/differential_test.cpp -o bin/differential_test
    ./bin/differential_test

    echo ""
    echo "[Phase I] Building & Running Benchmark Harnesses..."
    $CXX $CXXFLAGS src/embedded_bench_main.cpp -o bin/embedded_bench
    ./bin/embedded_bench

    $CXX $CXXFLAGS src/real_world_bench_main.cpp -o bin/real_world_bench
    $CXX $CXXFLAGS src/synthetic_experiments_main.cpp -o bin/synthetic_experiments
    $CXX $CXXFLAGS src/multiseed_v4_main.cpp -o bin/multiseed_v4

    echo ""
    echo "[Phase I] Reconciling Canonical Dataset & Figures..."
    $PYTHON scripts/reconcile_canonical_dataset.py || true
    $PYTHON scripts/plot_results.py || true
fi

if [ "$MODE" = "phase2" ] || [ "$MODE" = "all" ]; then
    echo ""
    echo "================================================================="
    echo "  PHASE II (METIS-X): Cache-Conscious Benchmark & Validation    "
    echo "================================================================="

    echo ""
    echo "[Phase II] Building & Running MetisX Correctness & Instrumentation..."
    $CXX $CXXFLAGS src/metis_x_instrumentation_main.cpp -o bin/metis_x_instr
    ./bin/metis_x_instr

    echo ""
    echo "[Phase II] Running Independent Validation Benchmark (R=7, taskset -c 0)..."
    $CXX $CXXFLAGS src/metis_x_bench_main.cpp -o bin/metis_x_bench
    taskset -c 0 ./bin/metis_x_bench 7 results/metis_x_validation.csv

    echo ""
    echo "[Phase II] Running Component Ablation & Failure Cases..."
    $CXX $CXXFLAGS src/metis_x_ablation_main.cpp -o bin/metis_x_ablation
    ./bin/metis_x_ablation

    $CXX $CXXFLAGS src/metis_x_failure_case_main.cpp -o bin/metis_x_failure_cases
    ./bin/metis_x_failure_cases

    echo ""
    echo "[Phase II] Generating Statistical Summaries & Canonical Dataset..."
    $PYTHON scripts/metis_x_stats.py
    $PYTHON scripts/phase_comparison.py
    $PYTHON scripts/generate_canonical_dataset.py
fi

echo ""
echo "================================================================="
echo "  REPRODUCIBILITY PIPELINE COMPLETED SUCCESSFULLY! ($MODE)      "
echo "  Phase I Dataset  : results/embedded_benchmark.csv             "
echo "  Phase II Dataset : results/METIS_X_CANONICAL_DATASET.csv       "
echo "  Phase II Report  : docs/METIS_X_FINAL_VALIDATION_REPORT.md     "
echo "  Config Freeze    : docs/METIS_X_CONFIG_FREEZE.md               "
echo "================================================================="
