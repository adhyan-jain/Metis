# Phase 3 Paper Strategy & Positioning

**Date:** October 10, 2026  
**Repository:** `adhyan-jain/Metis`  
**Revision:** HEAD `@0b4344050873968a7f3f940d47f8b46f50a23353`  
**Phase:** Phase 3 (Paper-First Pivot)  
**Deliverable:** 3 of 3 (`research/l3/phase3_paper_strategy.md`)

---

## 1. Proposed Paper Thesis

**Primary Thesis:** 
*Modern embedded and systems compilers heavily over-allocate memory during symbol resolution due to mismatched structural assumptions. By replacing traditional pointer-chasing scope stacks with a 32-byte cache-aligned Robin Hood open-addressing table backed by a contiguous string arena, compiler front-ends can reduce physical symbol table memory by over 50% while simultaneously reducing $p_{95}$ lookup latency.*

### Supporting Sub-Claims (The "Negative Result"):
*Contrary to theoretical expectations, optimizing scope-exit mechanisms via transactional logging (e.g., TS-TFDR) yields negligible performance gains ($<8\%$ total compilation time). The true architectural bottleneck lies in variable-length global identifier storage (the "12-Byte SSO Wall"), which mandates shifting from in-slot caching to contiguous offset arenas.*

---

## 2. Contributions Supported by Existing Evidence

1. **Empirical Workload Characterization:** The first public empirical characterization of AST symbol event streams across four major embedded ecosystems (Zephyr, ESP-IDF, FreeRTOS, Arduino), highlighting the bifurcation between transient short local variables and resident long global macros.
2. **Memory/Latency Trade-off Baseline:** A rigorous ablation study comparing traditional C-style flat arenas, standard C++ `std::unordered_map` trees, and modern Robin Hood hashing within a compiler context.
3. **The "Zero-Allocation" Lookup Invariant:** Proof that a tightly bounded 32-byte slot architecture can eliminate dynamic memory allocations on the hot lookup path.

### Claims That Must Be Weakened or Removed:
- **TS-TFDR Algorithmic Novelty:** Removed. The kill-test proved that standard Robin Hood reverse-deletion already natively achieves exact layout restoration.
- **"General Purpose" Hash Table Superiority:** We must scope the claims strictly to *compiler symbol tables* and *AST lexical scopes*. METIS-X is not a general replacement for `absl::flat_hash_map`.

---

## 3. Prior-Art Gaps & Positioning

The paper will position METIS-X in the gap between three mature domains:
1. **Compiler Symbol Tables (LLVM `ScopedHashTable`, Clang `IdentifierResolver`):** These prioritize $O(1)$ scope entry/exit but suffer from pointer-chasing cache misses on modern CPUs.
2. **High-Performance Hash Tables (SwissTable, F14):** These prioritize SIMD cache locality but rely on external string storage and lack native hierarchical scope unwinding mechanics.
3. **Arena Allocators (LCC, GCC):** These prioritize bulk memory reclamation but typically rely on linear probing arrays that suffer from severe load-factor degradation and clustering.

METIS-X bridges this gap by embedding LIFO scope unwinding directly into a Robin Hood open-addressing table, maximizing cache density.

---

## 4. The Single Next Required Experiment

To validate the final architectural piece (Candidate D: Compact Prefix + Linear Arena Offset), we must run one definitive experiment.

### Experiment: "Arena Offset Indirection vs. Heap SSO"
- **Hypothesis:** Replacing 12-byte in-slot SSO and heap fallback pointers with a 4-byte prefix and a 32-bit offset into a contiguous string arena will reduce peak physical heap on ESP-IDF without degrading $p_{95}$ lookup latency.
- **Null Hypothesis:** The L2/L3 cache misses incurred by indexing the out-of-slot arena will negate any cache-line alignment benefits, resulting in slower lookups than standard `ptmalloc` heap pointers.
- **Workload:** ESP-IDF and Zephyr RTOS canonical traces.
- **Metrics:** $p_{95}$ lookup latency ($\mu\text{s}$) and peak physical heap via `malloc_usable_size`.
- **Hard Stop Criterion:** If the arena architecture increases $p_{95}$ latency on ESP-IDF above $0.081\mu\text{s}$, the optimization is rejected, and the paper will be published focusing solely on the baseline METIS-X architecture and the TS-TFDR negative result.

---

## 5. Recommended Paper Outline (Systems Track)

1. **Introduction:** The memory wall in modern SDK compilation (e.g., ESP-IDF).
2. **Workload Characterization:** Analysis of the Zephyr/ESP-IDF AST event streams. The local vs. global identifier length dichotomy.
3. **Architecture (METIS-X):** 32B cache-aligned slots, Robin Hood backward-shift scope unwinding.
4. **The False Bottleneck (Negative Result):** Why scope-exit optimization (TS-TFDR) fails Amdahl's Law.
5. **The Real Bottleneck (String Storage):** Ablation of heap fallback vs. contiguous arena offsets.
6. **Empirical Evaluation:** Latency and memory benchmarks against LLVM and standard flat-arena baselines.
7. **Conclusion.**

---

## 6. Go/No-Go Recommendation

**Recommendation: `GO FOR INLINE-STORAGE PROTOTYPE`**
The paper thesis is strong, defensible, and relies on reproducible systems profiling rather than manufactured algorithmic novelty. We recommend proceeding immediately to the Candidate D (Arena Offset) prototype experiment.
