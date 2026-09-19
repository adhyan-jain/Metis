# METIS-X: Cache-Conscious Symbol Table Architecture

[![Build & Reproduce](https://img.shields.io/badge/reproducibility-automated-teal.svg)](./reproduce_all.sh)
[![Paper](https://img.shields.io/badge/paper-IEEE%20PDF-blue.svg)](./Metis_v2_IEEE.pdf)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](./LICENSE)

**METIS-X** is a cache-conscious, compiler-aware symbol-table architecture designed for resource-constrained embedded toolchains and language runtimes. It resolves the fundamental tail-latency trade-off discovered in adaptive compression studies (Phase I, `SymTabV3`) by eliminating dictionary reconstruction from the hot lookup path.

---

## Two-Phase Research Progression

### Phase I: Adaptive Representation Study (`SymTabV3` & `SymTabV4`) — *Frozen / Negative Result*
Phase I investigated whether 3-tier routing (**INLINE**, **INTERNED**, front-coded **COMPRESSED**) could reduce physical RAM under embedded constraints:
- **Finding:** Front-coded compression achieved a **20.2% physical RAM reduction** on Zephyr RTOS (53.39 MB vs 66.91 MB baseline).
- **Negative Result:** Compression incurred a **1.824× $p_{95}$ tail-latency regression** (0.228 µs vs 0.125 µs) due to string decoding overhead during lookup, failing the 1.25× latency gate.
- **Side-Table Metadata (`SymTabV4`):** Moving scalar fields to sparse side-tables lost to Conventional on 20/20 corpora due to container allocation floors.

### Phase II: METIS-X Cache-Conscious Architecture — *Primary Result*
Phase II replaces adaptive compression with a **cache-conscious flat open-addressing table**:
- **32-Byte Cache-Aligned Slot:** Stores short identifiers ($\le 12$\,B) directly inside the slot (`inlineBytes[12]`).
- **Robin Hood Displacement:** Reduces probe variance with early-exit probing.
- **LIFO Scope-Frame Recycling:** Recycles slots on scope exit immediately, eliminating heap fragmentation.
- **Zero Lookup Allocations:** 0 heap allocations across 2.8M lookups in real compiler traces.

---

## Primary Research Question (Phase II)

> *"Can a cache-conscious flat open-addressing symbol table eliminate dictionary reconstruction overhead to achieve simultaneous physical heap and $p_{95}$ latency reductions over embedded conventional baselines?"*

---

## Canonical Empirical Results (Phase II)

Evaluated under $R=7$ independent repetitions with process CPU core pinning (`taskset -c 0`) and physical allocator tracking (`malloc_usable_size`):

| Workload | Unique Names | Baseline Heap | METIS-X Heap | Heap Ratio | Baseline $p_{95}$ | METIS-X $p_{95}$ | $p_{95}$ Ratio | Classification |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **FreeRTOS** | 10,386 | 1.79 MB | **1.65 MB** | **0.9192** | 0.070 µs | **0.071 µs** | **1.0000** | JOINT WIN (<10%) |
| **Arduino** | 11,000 | 1.48 MB | 1.52 MB | 1.0260 | 0.066 µs | **0.059 µs** | **0.9077** | PARTIAL / TRADEOFF |
| **Zephyr** | **228,739** | **41.74 MB** | **25.79 MB** | **0.6178** | **0.119 µs** | **0.083 µs** | **0.6975** | **JOINT WIN ($\ge$10%)** |
| **ESP-IDF** | **231,075** | **42.65 MB** | **35.20 MB** | **0.8252** | **0.136 µs** | **0.070 µs** | **0.5147** | **JOINT WIN ($\ge$10%)** |

---

## Component Ablation (Zephyr RTOS)

| Level | Architecture | Physical Heap | $p_{95}$ Latency | Key Component Identified |
|---|---|---|---|---|
| $A_0$ | ConventionalHost | 2.25 MB | 0.228 µs | `std::unordered_map` with SSO |
| $A_1$ | Flat OA (Linear, Heap Strings) | 68.61 MB | 0.126 µs | Open addressing with per-string heap allocations |
| $A_2$ | Flat OA (Robin Hood, Heap Strings) | 43.44 MB | 0.262 µs | Robin Hood displacement with per-string heap allocations |
| $A_3$ | EmbeddedConventional | 21.53 MB | 0.090 µs | 16B compact entries + flat append-only string arena |
| $A_5$ | **Full METIS-X** | **5.59 MB** | **0.082 µs** | **32B slots + $\le 12$B inline strings + LIFO scope recycling** |

> **Ablation Insight:** Moving names into 32B inline slots with LIFO scope recycling ($A_2 \to A_5$) drops physical heap by **87.1%** (43.44 MB $\to$ 5.59 MB) over open addressing with separate heap strings.

---

## Automated Reproduction

To reproduce Phase II results end-to-end:

```bash
# Full Phase I + Phase II reproduction
./reproduce_all.sh

# Phase II only
./reproduce_all.sh phase2

# Phase I only
./reproduce_all.sh phase1
```

---

## Repository Structure

```
├── include/
│   ├── metis_x.hpp                      # Primary Phase-II architecture (MetisXTable)
│   ├── symtab_v3.hpp                    # Phase-I adaptive result (SymTabV3)
│   ├── embedded_conventional_symbol_table.hpp  # Embedded baseline (EmbeddedConventional)
│   └── heap_counter.hpp                 # Physical allocator tracker (malloc_usable_size)
├── src/
│   ├── metis_x_bench_main.cpp           # Phase-II benchmark harness
│   ├── metis_x_instrumentation_main.cpp # Zero-allocation lookup verification
│   ├── metis_x_ablation_main.cpp        # 5-level component ablation
│   └── metis_x_failure_case_main.cpp    # 8 boundary stress tests
├── results/
│   ├── METIS_X_CANONICAL_DATASET.csv    # Canonical Phase-II dataset
│   ├── metis_x_validation.csv           # R=7 independent validation run
│   ├── metis_x_ablation.csv             # Component ablation numbers
│   └── FINAL_CLAIMS_AUDIT.md            # Verified claims matrix
└── docs/
    ├── METIS_X_FINAL_VALIDATION_REPORT.md # Master Phase-II research report
    └── METIS_X_CONFIG_FREEZE.md           # Parameter freeze document
```
