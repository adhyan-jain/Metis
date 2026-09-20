# METIS / METIS-X Repository Structure

This document outlines the organization and directory layout of the METIS / METIS-X codebase.

---

## Directory Overview

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
│   ├── REPOSITORY_STRUCTURE.md         # Full directory tree description (this file)
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
