#!/usr/bin/env bash
# METIS / METIS-X build script.
# Compiles active C++ executables into bin/ and runs verification tests.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

CXX=${CXX:-g++}
CXXFLAGS="-std=c++14 -O2 -Wall -Wextra -Iinclude -Iinclude/historical"

mkdir -p bin build results figures

echo "================================================================="
echo "  METIS-X: Building Active Executables & Test Suite             "
echo "================================================================="

echo "== Building smoke test =="
$CXX $CXXFLAGS tests/smoke_test.cpp -o bin/smoke_test

echo "== Building differential test =="
$CXX $CXXFLAGS tests/differential_test.cpp -o bin/differential_test

echo "== Building Phase II METIS-X real-world benchmark runner =="
$CXX $CXXFLAGS src/metis_x_bench_main.cpp -o bin/metis_x_bench

echo "== Building METIS-X allocation & instruction profiler =="
$CXX $CXXFLAGS src/metis_x_instrumentation_main.cpp -o bin/metis_x_instr

echo "== Building METIS-X ablation study runner =="
$CXX $CXXFLAGS src/metis_x_ablation_main.cpp -o bin/metis_x_ablation

echo "== Building METIS-X failure cases & stress runner =="
$CXX $CXXFLAGS src/metis_x_failure_case_main.cpp -o bin/metis_x_failure_cases

echo "== Building METIS-X parameter sweep driver =="
$CXX $CXXFLAGS src/metis_x_sweep_main.cpp -o bin/metis_x_sweep

echo "== Building Real-World benchmark driver =="
$CXX $CXXFLAGS src/real_world_bench_main.cpp -o bin/real_world_bench

echo "== Building Embedded benchmark driver =="
$CXX $CXXFLAGS src/embedded_bench_main.cpp -o bin/embedded_bench

echo ""
echo "================================================================="
echo "  Running Verification Tests                                     "
echo "================================================================="
echo "-> Running Smoke Test..."
./bin/smoke_test

echo "-> Running Differential Test..."
./bin/differential_test

echo ""
echo "Build completed successfully. All binaries generated in bin/."
