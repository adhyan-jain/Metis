# METIS-X: Cache-Conscious Symbol Table Architecture

[![Build & Reproduce](https://img.shields.io/badge/reproducibility-automated-teal.svg)](./reproduce_all.sh)
[![Paper](https://img.shields.io/badge/paper-IEEE%20PDF-blue.svg)](./paper/Metis.pdf)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](./LICENSE)

**METIS-X** is a cache-conscious, compiler-aware symbol table engine designed for resource-constrained embedded toolchains and language runtimes. It resolves the fundamental tail-latency trade-off discovered in adaptive compression studies by eliminating dynamic allocation and dictionary decoding overheads from the hot lookup path.

---

## Repository Architecture & Organization

The codebase is organized into clean, logical directories separating active code, test suites, canonical datasets, paper manuscripts, and IP/patent disclosure packages:

```
.
├── paper/                              # Active research paper artifacts (LaTeX, PDF, Word Template)
│   ├── Metis.tex                       # IEEE 3-page conference LaTeX source
│   ├── Metis.pdf                       # Compiled paper PDF
│   └── Metis.docx                      # IEEE Template-2 formatted Word document
├── include/                            # Primary active C++ header-only METIS-X library
│   ├── metis_x.hpp                     # [ACTIVE] METIS-X cache-centric symbol table engine
│   ├── bench_metrics.hpp               # Active benchmark metrics helper
│   ├── common.hpp / hash_functions.hpp # Shared FNV-1a hash and utility headers
│   ├── heap_counter.hpp                # Dynamic allocation counter profiler
│   └── historical/                     # Reference historical implementations (Phase I / V1-V4)
│       ├── metis_v1.hpp / metis_v2.hpp / metis_v3.hpp / metis_v4.hpp
│       └── budget_sym.hpp
├── src/                                # Active driver mains & benchmark executables
│   ├── metis_x_bench_main.cpp          # Canonical Phase II real-world benchmark runner
│   ├── metis_x_instr_main.cpp          # Allocation & instruction counter profiler
│   ├── metis_x_ablation_main.cpp       # Component ablation waterfall driver
│   ├── metis_x_failure_case_main.cpp   # Edge-case & stress evaluation harness
│   └── real_world_bench_main.cpp       # AST graph benchmark driver
├── tests/                              # Automated test suite
│   ├── smoke_test.cpp                  # Unit sanity test suite
│   └── differential_test.cpp           # Cross-implementation differential fuzzing suite
├── data/                               # Canonical header workload graphs (Zephyr, ESP-IDF)
├── results/                            # Canonical CSV result datasets & generated plots
│   ├── METIS_X_CANONICAL_DATASET.csv   # Phase II canonical dataset
│   └── embedded_benchmark.csv          # Phase I canonical dataset
├── scripts/                            # Analysis & figure generation scripts
│   ├── plot_metis_x.py                 # Manuscript chart plotting script
│   └── build_template2_docx.py         # Word docx layout generator
├── docs/                               # Documentation, Research History, & IP Disclosures
│   ├── REPOSITORY_STRUCTURE.md         # Full directory tree description
│   ├── FINAL_REPOSITORY_CLEANUP_REPORT.md # Comprehensive cleanup report
│   ├── research/                       # Research evolution & evidence
│   │   ├── ARCHITECTURAL_EVOLUTION.md
│   │   ├── FAILED_APPROACHES.md
│   │   └── RESULT_PROVENANCE.md
│   └── patent/                         # Formal Patent Disclosures & IP Review Package
│       ├── INVENTION_DISCLOSURE.md
│       ├── CLAIM_FEATURE_MATRIX.md
│       ├── TECHNICAL_EFFECTS.md
│       ├── PRIOR_ART_RISKS.md
│       └── IP_READINESS_REPORT.md
├── frontend/                           # Next.js 16 Interactive Web Dashboard
├── build.sh                            # Active compilation script
└── reproduce_all.sh                    # Master paper reproduction script
```

---

## Research Evolution Summary

1. **Phase I (Adaptive Compression & Interning)**: Investigated prefix front-coding (`SymTabV3`) and dynamic string interning. While front-coding achieved a $20.2\%$ physical RAM reduction on Zephyr RTOS, it introduced a $2.4\times$ lookup tail-latency regression due to sequential decoding overhead. Sparse side-tables (`SymTabV4`) lost to baseline tables due to container overhead.
2. **Phase II (METIS-X Architecture)**: Replaced prefix compression with fixed 32-byte cache-line aligned inline slots (15-byte string SSO), 64-bit compact inline hashes, Robin Hood open addressing, and recyclable scope bump arenas.

---

## Canonical Empirical Results

Evaluated under $R=7$ independent repetitions with process CPU core pinning (`taskset -c 0`) and physical allocator tracking (`malloc_usable_size`):

| Workload | Unique Symbols | Baseline RAM | METIS-X RAM | Physical RAM Advantage | Baseline $p_{95}$ | METIS-X $p_{95}$ | $p_{95}$ Latency Advantage | Classification |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Zephyr RTOS** | 228,739 | $41.74\text{ MB}$ | **$24.95\text{ MB}$** | **$-40.2\%$ RAM** | $0.151\ \mu\text{s}$ | **$0.082\ \mu\text{s}$** | **$-45.7\%$ p95 Latency** | **JOINT WIN ($\ge 10\%$)** |
| **ESP-IDF** | 231,075 | $42.65\text{ MB}$ | **$35.15\text{ MB}$** | **$-17.6\%$ RAM** | $0.187\ \mu\text{s}$ | **$0.081\ \mu\text{s}$** | **$-56.7\%$ p95 Latency** | **JOINT WIN ($\ge 10\%$)** |
| **FreeRTOS** | 10,386 | $1.79\text{ MB}$ | **$1.59\text{ MB}$** | **$-11.2\%$ RAM** | $0.087\ \mu\text{s}$ | **$0.080\ \mu\text{s}$** | **$-8.0\%$ p95 Latency** | **JOINT WIN ($\ge 10\%$)** |
| **Arduino** | 11,000 | $1.48\text{ MB}$ | **$1.52\text{ MB}$** | **$+2.5\%$ RAM** | $0.093\ \mu\text{s}$ | **$0.078\ \mu\text{s}$** | **$-16.1\%$ p95 Latency** | **PARTIAL / TRADEOFF** |

- **Zero Hot-Path Allocations**: Verified $0$ dynamic heap allocations across $2,813,369$ symbol lookup operations in real-world AST execution traces.
- **Component Ablation Waterfall**: Memory footprint reduced from $43.44\text{ MB}$ ($A_2$: Flat Robin Hood with dynamic per-string heap allocations) to $4.73\text{ MB}$ ($A_5$: Full METIS-X with 32B inline slots and scope recycling), yielding an **$89.1\%$ physical memory reduction** on Zephyr AST symbol workloads.

---

## Quickstart & Replication

### 1. Build & Run Tests
```bash
./build.sh
./bin/smoke_test
./bin/differential_test
```

### 2. Full Paper Replication
```bash
./reproduce_all.sh phase2
```

### 3. Launch Web Dashboard
```bash
cd frontend
npm install
npm run build
npm run start
```
