#!/usr/bin/env bash
# METIS Research Artifact Master Reproducibility Script
# Target: adhyan-jain/Metis
# Strict reproducible pipeline: tests -> benchmarks -> reconciliation -> figures -> manuscript

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

CXX=${CXX:-g++}
CXXFLAGS="-std=c++14 -O2 -Wall -Wextra -Iinclude"
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

echo ""
echo "[Step 1/6] Running Correctness & Differential Fuzzing Tests..."
$CXX $CXXFLAGS tests/smoke_test.cpp -o bin/smoke_test
./bin/smoke_test

$CXX $CXXFLAGS tests/differential_test.cpp -o bin/differential_test
./bin/differential_test

echo ""
echo "[Step 2/6] Building Benchmark Harnesses..."
$CXX $CXXFLAGS src/embedded_bench_main.cpp -o bin/embedded_bench
$CXX $CXXFLAGS src/real_world_bench_main.cpp -o bin/real_world_bench
$CXX $CXXFLAGS src/synthetic_experiments_main.cpp -o bin/synthetic_experiments
$CXX $CXXFLAGS src/multiseed_v4_main.cpp -o bin/multiseed_v4

echo ""
echo "[Step 3/6] Running Embedded Benchmark Harness (Multi-Repetition)..."
./bin/embedded_bench

echo ""
echo "[Step 4/6] Reconciling Canonical Dataset..."
$PYTHON scripts/reconcile_canonical_dataset.py

echo ""
echo "[Step 5/6] Regenerating Publication Figures..."
$PYTHON scripts/plot_results.py
$PYTHON scripts/plot_pareto.py
$PYTHON scripts/generate_v4_evaluation_plots.py

echo ""
echo "[Step 6/6] Compiling Publication Manuscript (metis_v2.tex)..."
if command -v pdflatex >/dev/null 2>&1; then
    pdflatex -interaction=nonstopmode metis_v2.tex >/dev/null 2>&1
    pdflatex -interaction=nonstopmode metis_v2.tex >/dev/null 2>&1
    cp metis_v2.pdf Metis_v2_IEEE.pdf
    echo "  Successfully compiled metis_v2.pdf & Metis_v2_IEEE.pdf"
else
    echo "  pdflatex not found; skipping PDF compilation."
fi

echo ""
echo "================================================================="
echo "  REPRODUCIBILITY PIPELINE COMPLETED SUCCESSFULLY!              "
echo "  Canonical Dataset : results/CANONICAL_FINAL_DATASET.csv       "
echo "  Audit Report      : results/CANONICAL_AUDIT_LOG.md            "
echo "  Claims Audit      : results/FINAL_CLAIMS_AUDIT.md             "
echo "  Manuscript        : Metis_v2_IEEE.pdf                         "
echo "================================================================="
