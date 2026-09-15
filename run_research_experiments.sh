#!/usr/bin/env bash
# ==============================================================================
# SymTabV2 Research Reproducibility Pipeline
# (CLAUDE_RESEARCH.md Section 19)
#
# Single entry point script that compiles the codebase, runs unit/differential
# tests, extracts real-world corpus traces, executes the full research benchmark
# suite (Core V2, Pareto Optimization, Parameter Study, Ablation Study, Statistical
# Validation, ML Policy Selection), and generates all publication figures.
# ==============================================================================
set -e

echo "================================================================================"
echo "           SymTabV2 End-to-End Research Reproducibility Pipeline                "
echo "================================================================================"
echo "Started at: $(date)"

CXX=${CXX:-g++}
STD=-std=c++14
FLAGS="$STD -O2 -Wall -Wextra"

# Ensure output directories exist
mkdir -p results figures

# ------------------------------------------------------------------------------
# 1. Environment & Toolchain Verification
# ------------------------------------------------------------------------------
echo ""
echo "=== Step 1: Environment & Toolchain Audit ==="
echo "Compiler: $($CXX --version | head -n 1)"
echo "C++ Standard: $STD"
echo "Optimization Flags: $FLAGS"

PYTHON=python3
if [ -f venv/bin/activate ]; then
    PYTHON="venv/bin/python"
    echo "Python Environment: Virtual Environment (venv/bin/python)"
else
    echo "Python Environment: System Python (python3)"
fi
echo "Python Version: $($PYTHON --version)"

# ------------------------------------------------------------------------------
# 2. Compilation of Unit Tests and Benchmarks
# ------------------------------------------------------------------------------
echo ""
echo "=== Step 2: Compiling Test Suite and Research Executables ==="
echo "  [1/10] Building tests/smoke_test.exe..."
$CXX $FLAGS tests/smoke_test.cpp -o tests/smoke_test.exe

echo "  [2/10] Building tests/differential_test.exe..."
$CXX $FLAGS tests/differential_test.cpp -o tests/differential_test.exe

echo "  [3/10] Building tests/symtab_v2_compressed_test.exe..."
$CXX $FLAGS tests/symtab_v2_compressed_test.cpp -o tests/symtab_v2_compressed_test.exe

echo "  [4/10] Building v2_benchmark.exe (Section 9)..."
$CXX $FLAGS src/v2_benchmark_main.cpp -o v2_benchmark.exe

echo "  [5/10] Building corpus_event_bench.exe (Section 10)..."
$CXX $FLAGS src/corpus_event_bench_main.cpp -o corpus_event_bench.exe

echo "  [6/10] Building pareto.exe (Sections 11 & 12)..."
$CXX $FLAGS src/pareto_main.cpp -o pareto.exe

echo "  [7/10] Building parameter_study.exe (Section 13)..."
$CXX $FLAGS src/parameter_study_main.cpp -o parameter_study.exe

echo "  [8/10] Building ablation.exe (Section 14)..."
$CXX $FLAGS src/ablation_main.cpp -o ablation.exe

echo "  [9/10] Building statistical_validation.exe (Section 15)..."
$CXX $FLAGS src/statistical_validation_main.cpp -o statistical_validation.exe

echo "  [10/10] Building budget_sym_demo.exe..."
$CXX $FLAGS src/demo_main.cpp -o budget_sym_demo.exe

# ------------------------------------------------------------------------------
# 3. Unit and Differential Test Execution
# ------------------------------------------------------------------------------
echo ""
echo "=== Step 3: Running Unit and Differential Test Suites ==="
./tests/smoke_test.exe
./tests/differential_test.exe
./tests/symtab_v2_compressed_test.exe

# ------------------------------------------------------------------------------
# 4. Real-World Corpus Event Trace Extraction & Evaluation
# ------------------------------------------------------------------------------
echo ""
echo "=== Step 4: Semantic Corpus Event Extraction & Evaluation (Section 10) ==="
CORPORA_FOUND=0
for corpus in freertos arduino-core zephyr; do
    if [ -d "corpora/$corpus" ]; then
        CORPORA_FOUND=1
    fi
done

if [ "$CORPORA_FOUND" = "1" ]; then
    echo "  -- Extracting semantic event traces from corpora/ --"
    $PYTHON scripts/extract_corpus_events.py
    echo "  -- Running semantic corpus event benchmark --"
    ./corpus_event_bench.exe
else
    echo "  Notice: Vendored source directories not under corpora/ -- using pre-extracted event files in results/"
    ./corpus_event_bench.exe
fi

# ------------------------------------------------------------------------------
# 5. Core V2 Research Benchmark
# ------------------------------------------------------------------------------
echo ""
echo "=== Step 5: Core V2 Latency/Memory Benchmark (Section 9) ==="
./v2_benchmark.exe

# ------------------------------------------------------------------------------
# 6. Latency-Constrained Pareto Optimization
# ------------------------------------------------------------------------------
echo ""
echo "=== Step 6: Latency-Constrained Pareto Optimization (Section 12) ==="
./pareto.exe

# ------------------------------------------------------------------------------
# 7. Systematic Parameter Study
# ------------------------------------------------------------------------------
echo ""
echo "=== Step 7: Systematic Parameter Study (Section 13) ==="
./parameter_study.exe

# ------------------------------------------------------------------------------
# 8. Comprehensive V2 Ablation Study
# ------------------------------------------------------------------------------
echo ""
echo "=== Step 8: Comprehensive V2 Ablation Study (Section 14) ==="
./ablation.exe

# ------------------------------------------------------------------------------
# 9. Statistical Validation
# ------------------------------------------------------------------------------
echo ""
echo "=== Step 9: Statistical Validation (N=30 Seeds, Section 15) ==="
./statistical_validation.exe
$PYTHON scripts/statistical_validation.py

# ------------------------------------------------------------------------------
# 10. Latency-Constrained ML Policy Selection & Audit
# ------------------------------------------------------------------------------
echo ""
echo "=== Step 10: Latency-Constrained ML Policy Selection & Audit (Section 16) ==="
$PYTHON scripts/train_ml_oracle.py

# ------------------------------------------------------------------------------
# 11. Plotting & Figure Generation
# ------------------------------------------------------------------------------
echo ""
echo "=== Step 11: Generating Publication Figures ==="
$PYTHON scripts/plot_results.py
$PYTHON scripts/plot_pareto.py

echo ""
echo "================================================================================"
echo "         Reproducibility Pipeline Executed and Verified Successfully            "
echo "================================================================================"
echo "Finished at: $(date)"
