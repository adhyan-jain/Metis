# Phase 2 Workload Profiling & Experimental Methodology Plan

**Date:** October 10, 2026  
**Repository:** `adhyan-jain/Metis`  
**Revision:** HEAD `@0b4344050873968a7f3f940d47f8b46f50a23353`  
**Phase:** Phase 2 (Workload Profiling, Baseline Validation & Research Viability Gate)  

---

## 1. Experimental Environment & Setup

- **Host System:** Linux x86_64 (Linux Kernel 6.x)
- **Compiler:** `g++ (GCC) -std=c++14 -O2 -Wall -Wextra`
- **Execution Controls:** Process core pinning (`taskset -c 0`) to eliminate inter-core OS migration jitter.
- **Physical Memory Interception:** Global `operator new/delete` overrides querying `malloc_usable_size(p)` to capture physical allocator page allocations and slab alignment overheads.
- **Instrumentation Isolation:** Profiling metrics collected using an isolated profiler harness (`src/metis_x_instr_main.cpp` and `src/metis_x_profiler_harness.cpp`), ensuring `METISX_TEST_HOOK` is disabled during performance benchmark runs.

---

## 2. Workload Corpus Provenance & Audited Datasets

| Dataset Label | Event Count | Unique Symbols | Name SSO Distribution ($\le 12$B vs $>12$B) | Corpus Provenance & Extraction Method |
| :--- | :---: | :---: | :---: | :--- |
| **Zephyr RTOS** | 2,646,692 | 228,739 | **93.4%** $\le 12$B / 6.6% $>12$B | Real AST symbol event trace (`data/corpus_events_Zephyr.txt`) extracted from C/C++ header builds. |
| **ESP-IDF** | 2,418,912 | 231,075 | **89.9%** $\le 12$B / 10.1% $>12$B | Real AST symbol event trace (`data/corpus_events_ESP-IDF.txt`) from ESP32 SDK header inclusion trees. |
| **FreeRTOS** | 108,432 | 10,386 | **91.2%** $\le 12$B / 8.8% $>12$B | Real AST symbol event trace (`data/corpus_events_FreeRTOS.txt`) from kernel headers. |
| **Arduino** | 114,210 | 11,000 | **52.2%** $\le 12$B / 47.8% $>12$B | Real AST symbol event trace (`data/corpus_events_Arduino.txt`) from Arduino core libraries. |

*Note on Corpus Expansion:* As noted in `docs/research/corpus_screening_stage2.md`, Stage 2 corpus expansion across secondary host targets remains partial. The 4 canonical datasets above are primary verified event streams.

---

## 3. Baselines for Comparison

1. **`METIS-X` (Phase II Reference):** 32-byte cache-aligned slot slab, Robin Hood open addressing, LIFO scope stack relocation updates, 12B inline SSO.
2. **`EmbeddedConventionalSymbolTable`:** Compact 16-byte `CompactEntry` table, flat contiguous string arena, linear probing hash index, scope-lifetime arena reclamation.
3. **`ConventionalHostSymbolTable`:** `std::unordered_map<std::string, SymbolMeta>` with standard C++ libstdc++ Short String Optimization ($\le 15$B inline).
4. **`SymTabV3` (Phase I Reference):** 3-tier representation (Inline, Interned, Front-Coded Compressed blocks).
5. **LLVM `ScopedHashTable` (Analytical Reference):** Per-scope linked list symbol binding stack (standard compiler reference architecture).

---

## 4. Measurement Protocol & Metrics Matrix

### 4.1 Lookup Path Metrics
- **Latency Distribution:** $p_{50}, p_{95}, p_{99}$, mean, and max lookup time ($\mu\text{s}$).
- **Hit vs Miss Ratio:** Probed lookups resolving to active declaration IDs versus unsuccessful symbol lookups.
- **Probe Distance Histogram:** Distribution of probe steps per lookup ($0, 1, 2, 3, \dots, \text{max}$).
- **In-Slot vs Heap Dereference:** Direct `memcmp` against 13B `inlineBytes` vs 8B heap pointer dereference.

### 4.2 Insertion Path Metrics
- **Declaration vs Shadowing:** New symbol declarations vs outer-scope variable shadowing.
- **Same-Scope Redeclaration:** In-place update of `declId` and `typeId` without slot relocation.
- **Displacement Swaps:** Number of Robin Hood `std::swap` operations per insertion.
- **Rehash Frequency & Cost:** Table capacity expansions ($16 \to 32 \to 64 \dots$), time spent rehousing entries.

### 4.3 Scope Exit Path Metrics
- **Symbols Released:** Number of bindings invalidated per `exitScope()`.
- **Backward-Shift Slots Moved:** Total physical slot moves performed during Robin Hood tombstone-free backward shifts.
- **Relocation Updates:** Time spent updating `scopeFrames_[scopeId][frameIndex]`.

### 4.4 Memory & Allocation Metrics
- **Physical Heap Footprint:** Tracked via `malloc_usable_size` for slot vector, scope frame stack, and heap-allocated strings $>12$B.
- **Peak vs Final Heap:** Physical peak heap memory recorded during compilation vs final heap upon trace completion.
- **Dynamic Allocation Count:** Number of `operator new` / `malloc` calls across lookups, insertions, and scope exits.
