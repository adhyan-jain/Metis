# Benchmark Methodology & Research Infrastructure

This document describes the design, measurement methodology, and empirical standards of the unified benchmark system (`src/v2_benchmark_main.cpp`), which produces `results/v2_latency_memory.csv` per **CLAUDE_RESEARCH.md Section 9**.

---

## 1. Overview & Research Objectives

The goal of this benchmark system is to evaluate adaptive, memory-efficient symbol table designs against conventional baselines under identical workload traces.

The framework compares four distinct symbol table architectures:

1. **ConventionalSymbolTable** (`include/conventional_symbol_table.hpp`): Standard compiler approach using stacked `std::unordered_map<std::string, SymbolMeta>`. No interning, no compression.
2. **InternedSymbolTable** (`include/interned_symbol_table.hpp`): Global refcounted string interning pool with scope-level map of integer pool indices.
3. **BudgetSym V1** (`include/budget_sym.hpp`): Append-only vector of 80-byte `Entry` objects with front-coding decodes and an LRU lookup cache.
4. **SymTabV2** (`include/symtab_v2.hpp`): Scope-aware, free-list backed slot allocator with Robin Hood open-addressing per-scope index, 32-bit/8-bit fingerprint filtering, block-based front-coding, and hot/cold tiering.

---

## 2. Event Trace & Replay Identity

To ensure scientific validity, **all four implementations receive the exact same semantic event trace**:
- **Insertions**: Identical identifier sequence in identical declaration order.
- **Scope Hierarchy**: Identical scope enter/exit lifecycle.
- **Lookups**: Identical lookup targets (both live hits and guaranteed-absent misses).

### Scope Lifecycle & Measurement Isolation
In earlier iterations, executing scope exit prior to lookups caused all symbols to be reclaimed, turning lookups into absent-name misses. The unified benchmark corrects this:
1. **Primary Measurement**: Identifiers are inserted into the global scope and kept live during lookup latency sampling.
2. **Scope Exit Timing**: Scope reclamation is measured separately on a dedicated inner scope populated with $N$ controlled entries, preventing scope destruction from poisoning primary symbol lookup state.

---

## 3. Dual Memory Accounting Methodology

The benchmark clearly separates two memory metrics across dedicated CSV columns:

### A. Modeled Memory (`modeled_peak_bytes`, `modeled_final_bytes`)
- Derived from each table's internal `MemoryTracker`.
- Computed via deterministic, hand-audited per-entry cost formulas (e.g., control block size + key string bytes + index node overhead).
- **Properties**: 100% reproducible across machines, operating systems, and compilers.

### B. Measured Heap Memory (`measured_peak_heap_bytes`, `measured_final_heap_bytes`)
- Intercepts C++ `operator new` / `operator delete` via `include/heap_counter.hpp`.
- Measures net OS-level heap allocation changes (`malloc_usable_size` / usable heap bounds).
- Captures dynamic allocator overhead, std::vector capacity growth, hash table load-factor padding, and metadata bloat.
- **Properties**: Reflects true physical memory consumption; machine/allocator-dependent.

> [!IMPORTANT]
> Modeled memory and measured heap memory are never averaged or combined.

---

## 4. Latency Measurement & Timing Discipline

Latency measurements evaluate per-operation runtime overhead in microseconds ($\mu s$) using high-resolution hardware timers (`include/hires_timer.hpp`).

### Tested Operations
- **Cold Lookup**: First `resolve()` of each target name on a freshly-populated table instance.
- **Hot Lookup**: 5th consecutive `resolve()` of each target name (crossing V2's `hotAccessThreshold=3`, measuring promoted/cached tier speed).
- **Absent Lookup**: Lookups of non-existent identifiers (`ZZZ_ABSENT_N_XYZ`).
- **Insertion**: Per-symbol `insert()` duration during trace replay.
- **Scope Exit**: Teardown and reclamation time for an occupied inner scope.

### Timing Controls
- **Warmup Run**: Every benchmark executes a complete, discarded warmup pass before recording metrics to eliminate cold-cache and OS page-fault noise.
- **Individual Call Timing**: Each call is timed independently (not batch-timed), allowing calculation of fine-grained percentiles ($p_{50}$, $p_{95}$, $p_{99}$).
- **Dead-Code Elimination (DCE) Protection**: Return values are assigned to `volatile` sink variables to prevent compiler optimization loops from eliminating operations.

---

## 5. Internal Counters & Representation Breakdown

The benchmark records detailed internal structural metrics:

| Metric | Description |
|---|---|
| `promotions` | Count of COMPRESSED $\rightarrow$ INTERNED dynamic promotions on hot access |
| `demotions` | Count of INTERNED $\rightarrow$ COMPRESSED demotions during maintenance sweeps |
| `reconstruction_count` | Number of front-coded block decodes performed during lookup |
| `reconstruction_steps_total` | Cumulative character/prefix steps walked across all decodes |
| `mean_reconstruction_depth` | Average front-coding walk depth ($\frac{\text{steps}}{\text{count}}$) |
| `count_inline` | Number of symbols residing in 0-overhead INLINE storage |
| `count_interned` | Number of symbols residing in refcounted INTERNED pool |
| `count_compressed` | Number of symbols residing in front-coded COMPRESSED blocks |
| `v1_cache_hits / misses / hit_rate` | V1 LRU lookup-cache hit statistics |

---

## 6. Dataset Taxonomy

The evaluation suite evaluates three distinct dataset classes (CLAUDE_RESEARCH.md Section 11):

### Category A — Real-World Corpora
- **FreeRTOS** ($\approx 50\text{K}$ tokens): Real C embedded kernel symbol stream.
- **Arduino** ($\approx 50\text{K}$ tokens): C++ micro-controller framework symbols.
- **Zephyr** ($\approx 50\text{K}$ tokens): Large RTOS build symbols.

### Category B — Redundancy-Rich Workloads
- `small`, `medium`, `large`: Synthetic baseline scaling benchmarks ($100$ to $20,000$ symbols).
- `high-prefix-similarity`: Contiguous runs of identifiers sharing long common prefixes.
- `hot-cold-access`: Skewed access pattern with 10% hot identifiers accessed repeatedly.
- `nested-scopes`: High scope-depth hierarchy testing reclamation efficiency.

### Category C — Adversarial Workloads
- `random-long`: Uniform random 20–40 character identifiers (no prefix sharing).
- `high-churn`: Rapid creation and destruction of short-lived scopes.
- `memory-stress`: Long unique identifiers under tight memory budgets.

---

## 7. Execution & Output Files

- Source: `src/v2_benchmark_main.cpp`
- Executable: `v2_benchmark.exe` (built via `build.sh`)
- CSV Artifact: `results/v2_latency_memory.csv`
