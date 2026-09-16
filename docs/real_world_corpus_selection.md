# Real-World Corpus Selection Methodology & Workload Justification

## Executive Overview

To evaluate **SymTab V2** (BUDGET-SYM) on real-world C/C++ compilation traffic without cherry-picking, we conducted a 2-phase evaluation process:
1. **Phase 1 Characterization**: Semantic corpus extraction and quantitative workload profiling across 15 open-source C/C++ codebases prior to running performance experiments.
2. **Phase 2 Workload Clustering & Selection**: Selection of a representative workload matrix spanning favorable, neutral, large-scale, and structurally unfavorable real-world codebases.

This document records the empirical characterization metrics and selection rationale for the representative corpus set.

---

## 1. Characterization Matrix (15 Candidate Repositories)

The table below summarizes the Phase 1 characterization measurements across all 15 candidates obtained using the unified semantic event parser (`scripts/extract_and_characterize_all_corpora.py`).

| Repository | Category | Files | LOC | Events | Declarations | Uses | Unique Syms | Avg Scope Depth | Mean Length (B) | Prefix Sim | Repeat Rate | Gini Skew | Scope Churn |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **FreeRTOS** | Embedded RTOS | 655 | 349,140 | 227,222 | 72,376 | 123,602 | 10,386 | 1.580 | 14.202 | 0.1643 | 0.6307 | 0.7931 | 0.0688 |
| **Arduino** | Embedded HAL | 332 | 144,620 | 94,232 | 31,846 | 50,273 | 11,000 | 1.446 | 8.691 | 0.1362 | 0.6122 | 0.7182 | 0.0641 |
| **Zephyr** | Scalable RTOS | 4,298 | 1,700,855 | 2,605,813 | 703,727 | 1,523,992 | 228,739 | 10.503 | 9.932 | 0.0994 | 0.6841 | 0.8070 | 0.0726 |
| **CPython** | Compiler / Interpreter | 1,126 | 1,048,996 | 1,716,669 | 336,691 | 1,091,338 | 68,829 | 2.140 | 8.444 | 0.1567 | 0.7642 | 0.8644 | 0.0840 |
| **Lua** | Embedded Interpreter | 68 | 34,250 | 59,295 | 13,042 | 39,109 | 4,038 | 1.416 | 5.244 | 0.0728 | 0.7499 | 0.7505 | 0.0601 |
| **SQLite** | Embedded RDBMS | 357 | 443,833 | 704,928 | 133,618 | 466,974 | 28,832 | 2.444 | 6.760 | 0.0879 | 0.7775 | 0.8189 | 0.0740 |
| **FFmpeg** | DSP / Media Engine | 4,701 | 1,852,094 | 3,725,989 | 700,171 | 2,487,848 | 141,486 | 1.804 | 6.928 | 0.1159 | 0.7804 | 0.8639 | 0.0721 |
| **QEMU** | System Emulator | 6,981 | 2,476,276 | 4,122,773 | 959,486 | 2,573,238 | 299,886 | 1.385 | 8.233 | 0.1067 | 0.7284 | 0.8263 | 0.0715 |
| **mbedTLS** | Security / Crypto | 131 | 87,381 | 94,224 | 20,810 | 58,436 | 5,668 | 1.535 | 11.602 | 0.1042 | 0.7374 | 0.7507 | 0.0795 |
| **Redis** | Data Store | 796 | 377,927 | 641,800 | 125,220 | 436,193 | 34,058 | 1.537 | 7.776 | 0.1165 | 0.7770 | 0.7953 | 0.0625 |
| **Nginx** | Web / Proxy Server | 412 | 256,429 | 424,153 | 56,383 | 301,302 | 12,464 | 3.464 | 7.702 | 0.0776 | 0.8424 | 0.8582 | 0.0784 |
| **cJSON** | Compact JSON Library | 99 | 22,518 | 27,559 | 6,315 | 15,989 | 2,352 | 1.128 | 11.443 | 0.1161 | 0.7169 | 0.7471 | 0.0953 |
| **protobuf-c** | Serialization Runtime | 27 | 9,055 | 11,892 | 3,355 | 7,043 | 1,423 | 1.525 | 10.035 | 0.0831 | 0.6773 | 0.7131 | 0.0628 |
| **ESP-IDF** | Embedded IoT SDK | 5,870 | 2,079,310 | 2,229,361 | 795,847 | 1,115,502 | 231,075 | 1.638 | 12.332 | 0.1829 | 0.5836 | 0.7473 | 0.0711 |
| **curl** | Transfer Library | 1,036 | 295,948 | 409,262 | 84,508 | 273,179 | 22,222 | 3.207 | 8.027 | 0.0952 | 0.7637 | 0.8033 | 0.0630 |

---

## 2. Workload Taxonomy & Clustering

Based on empirical metrics, the candidate workloads cluster into five distinct structural profiles:

1. **High Length & High Prefix Similarity (Structural Fit for Block Front-Coding)**:
   - *ESP-IDF*, *FreeRTOS*, *mbedTLS*. Characterized by mean identifier length > 11B and high prefix similarity (0.16–0.18).
2. **High Redundancy & Access Skew (Structural Fit for String Interning & Hot Promotion)**:
   - *CPython*, *Nginx*, *FFmpeg*. Characterized by repeat rates > 0.76 and Gini access skew > 0.85.
3. **Massive Unique Identifier Scale & Deep Lexical Scope Nesting**:
   - *Zephyr*, *QEMU*. Characterized by >200,000 unique symbols and deep scope stacks (up to depth 21).
4. **Short Identifier / Minimal Redundancy (Unfavorable / Adversarial for Compression)**:
   - *Lua*. Characterized by extremely short identifier names (mean 5.24B). Because V2 compressed/interned entry header structures consume ~28–29B, compressing short 5B names adds link overhead exceeding raw string payload.
5. **Intermediate / Infrastructure Codebases**:
   - *Arduino*, *SQLite*, *Redis*, *curl*, *cJSON*, *protobuf-c*. Moderate identifier lengths (6.7–11.4B) and standard repeat rates.

---

## 3. Selected Representative Corpus Matrix

To ensure scientific credibility and prevent cherry-picking, we selected six codebases covering all five structural workload profiles:

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        EVALUATION CORPUS SELECTION MATRIX                              │
├───────────────────────┬──────────────────────────────────┬─────────────────────────────┤
│ Repository            │ Workload Profile                 │ V2 Mechanism Tested         │
├───────────────────────┼──────────────────────────────────┼─────────────────────────────┤
│ ESP-IDF               │ High Prefix Sim & Long Names     │ Block Front-Coding & FP     │
│ CPython               │ High Symbol Reuse & Access Skew  │ Interning Pool & Hot Promo  │
│ Zephyr RTOS           │ Massive Scale & Deep Scopes      │ Scope Slot Reclamation      │
│ FreeRTOS              │ Embedded RTOS Baseline           │ Scope Reclamation & FP      │
│ Arduino Core          │ Microcontroller HAL Baseline     │ Inline / Interned Boundary  │
│ Lua                   │ Short Names (Adversarial)        │ Overhead Boundary Check     │
└───────────────────────┴──────────────────────────────────┴─────────────────────────────┘
```

### Detailed Justifications per Repository

#### 1. ESP-IDF (Espressif IoT SDK)
- **Workload Family**: High Prefix Similarity & Long Identifier Names.
- **Selection Rationale**: Demonstrates the highest prefix similarity (0.1829) and long mean identifier length (12.332B) among large multi-file frameworks.
- **Key Statistics**: 5,870 files, 2,079,310 LOC, 795,847 declarations, 231,075 unique symbols.
- **Exercised V2 Mechanisms**: Evaluates whether front-coded block compression and 1-byte hash fingerprints effectively reduce heap footprint on hardware abstraction naming conventions (`esp_hal_gpio_...`).
- **Extraction Limitations**: Contains nested C preprocessor macros and FreeRTOS wrapper calls.
- **New Coverage**: Extends embedded benchmarks beyond microcontrollers to modern 32-bit Wi-Fi/BT IoT SDK codebases.

#### 2. CPython (Python Language Runtime)
- **Workload Family**: High Symbol Reuse & Skewed Access Frequency.
- **Selection Rationale**: Exhibits high repeat rate (0.7642) and high Gini access skew (0.8644) representing language compiler and virtual machine frontends.
- **Key Statistics**: 1,126 files, 1,048,996 LOC, 336,691 declarations, 1,091,338 uses, 68,829 unique symbols.
- **Exercised V2 Mechanisms**: Evaluates string interning pool efficiency and runtime hot-cold access promotion on core AST and bytecode interpreter symbols (`PyObject_...`, `_Py_...`).
- **Extraction Limitations**: C source files contain extensive macro-generated dispatch tables.
- **New Coverage**: Introduces language interpreter runtime traffic not present in embedded RTOS codebases.

#### 3. Zephyr RTOS
- **Workload Family**: Massive Unique Scale & Deep Lexical Scopes.
- **Selection Rationale**: Preserved from primary benchmark suite. Represents deep scope depth (max 21, avg 10.503) and large scale (228,739 unique symbols).
- **Key Statistics**: 4,298 files, 1,700,855 LOC, 703,727 declarations, 1,523,992 uses.
- **Exercised V2 Mechanisms**: Tests scope slot recycling across nested subsystem initializations.
- **Extraction Limitations**: Device tree macro expansions generated at build time are omitted.

#### 4. FreeRTOS & Arduino Core
- **Workload Family**: Embedded Real-Time OS & Microcontroller Hardware Abstraction Baselines.
- **Selection Rationale**: Preserved for historical comparison and microcontroller memory-constraint validation.
- **Key Statistics**: FreeRTOS (655 files, 14.2B mean len), Arduino (332 files, 8.69B mean len).
- **Exercised V2 Mechanisms**: Tests small-to-medium MCU memory pressure handling.

#### 5. Lua (Lightweight Embedded Interpreter) — *Adversarial Candidate*
- **Workload Family**: Short Identifier Names / Low-Redundancy Adversarial Workload.
- **Selection Rationale**: Selected specifically as an **unfavorable workload** for compressed symbol tables due to its extremely short mean identifier length (5.244B).
- **Key Statistics**: 68 files, 34,250 LOC, 13,042 declarations, 3,9109 uses, 4,038 unique symbols.
- **Exercised V2 Mechanisms**: Tests the operational boundary where V2 representation headers (~28B) incur higher per-symbol overhead than simple inline string allocations.
- **Extraction Limitations**: Single-pass ANSI C parser code with dense C macro usages.
- **New Coverage**: Provides explicit negative/adversarial benchmark data necessary for scientific rigor.

---

## 4. Methodological Guarantee

1. **No Cherry-Picking**: Candidate characterization was completed **prior** to running benchmark performance experiments.
2. **Unified Semantic Parser**: Identical AST lexing and scope tracking logic was executed across all 15 codebases.
3. **Primary Comparison**: V2 is evaluated directly against **Conventional** and **Interned** baselines on identical event streams.
