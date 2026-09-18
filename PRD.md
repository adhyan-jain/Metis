# Metis — Original Requirements & Historical Design Specification

> [!NOTE]
> **Historical Design Specification**: This document describes the original project requirements and historical design stages (Review-1 and Review-2). The authoritative, frozen final research evaluation, canonical datasets, and scientific findings are documented in [`docs/FINAL_COMPLETION_AUDIT.md`](./docs/FINAL_COMPLETION_AUDIT.md), [`docs/FINAL_REPRODUCIBILITY_REPORT.md`](./docs/FINAL_REPRODUCIBILITY_REPORT.md), [`results/CANONICAL_FINAL_DATASET.csv`](./results/CANONICAL_FINAL_DATASET.csv), and the publication manuscript [`metis_v2.tex`](./metis_v2.tex) / [`Metis_v2_IEEE.pdf`](./Metis_v2_IEEE.pdf).

**Repo:** https://github.com/adhyan-jain/Metis  
**Course:** Compiler Design, VIT Vellore — Guide: Prof. Bhuvaneshwari M  
**Authors:** Shubhi Singh, Adhyan Jain, Akshith Venkataramana, Arjuman  

---

## 1. Overview (Historical Context)

BUDGET-SYM is a C++14 header-only adaptive symbol table for embedded compilers operating under tight memory constraints. Instead of storing all identifiers the same way, BUDGET-SYM selects a per-symbol storage representation at insert time based on five runtime signals, reclaims memory eagerly at scope exit, and adapts its own policy thresholds mid-compilation using a lightweight ML predictor trained offline.

The research goal is to demonstrate that BUDGET-SYM moves the memory-latency Pareto frontier outward — achieving better memory reduction than any fixed representation strategy, while reducing lookup latency overhead through an LRU cache layer. A multi-model comparison study justifies the ML design choices with empirical evidence.

---

## 2. System Architecture

BUDGET-SYM consists of eight named components. Every component must be implemented, tested, and described in the paper.

```
                    ┌─────────────────────────────────────────────┐
                    │              BUDGET-SYM Controller                │
                    │                                             │
  Identifiers ──▶  │  ┌─────────────┐   ┌──────────────────┐   │
                    │  │  Workload   │──▶│  Threshold       │   │
                    │  │  Profiler   │   │  Predictor (ML)  │   │
                    │  └─────────────┘   └────────┬─────────┘   │
                    │                             │ PolicyConfig  │
                    │  ┌──────────────────────────▼──────────┐   │
                    │  │       Adaptive Representation        │   │
                    │  │         Policy — decide()            │   │
                    │  └──────┬──────────┬──────────┬────────┘   │
                    │         │          │          │             │
                    │      INLINE    INTERNED  COMPRESSED        │
                    │                    │          │             │
                    │  ┌─────────────────▼──────────▼────────┐   │
                    │  │           Compact Index              │   │
                    │  └──────────────────┬──────────────────┘   │
                    │                     │                       │
  Lookup ────────▶  │  ┌──────────────────▼──────────────────┐   │
                    │  │         LRU Lookup Cache (64)        │   │
                    │  └──────────────────┬──────────────────┘   │
                    │                     │                       │
                    │  ┌──────────────────▼──────────────────┐   │
                    │  │  Memory Monitor  │  Scope Analyzer   │   │
                    │  │  (tracked bytes) │  (exitScope reclaim)  │
                    │  └─────────────────────────────────────┘   │
                    └─────────────────────────────────────────────┘
```

### 2.1 Component Descriptions

**Memory Monitor**
Tracks total allocated bytes using a deterministic model (not OS RSS). Formula per representation:
- INLINE: `name.length + 1 + 28`
- INTERNED: `28` per entry; pool pays `sizeof(string) + name.length + 1` once
- COMPRESSED: `1 + suffix.length + 1 + 29`
Exposes: `memoryUsage()`, `pressure()` (usage/budget ratio), `budget`.

**Scope/Lifetime Analyzer**
Maintains a scope stack. `enterScope()` pushes a new layer with a symbol list. `exitScope()` walks all symbols in the top layer, calls `releaseEntry()` on each, reclaims their tracked bytes, invalidates their LRU cache entries, and pops the layer. This is the dominant memory-saving mechanism per the ablation study.

**Access Profiler**
Per-symbol `accessCount` incremented on every `lookup()`. When `accessCount >= hotAccessThreshold`, `maybePromote()` fires — switches COMPRESSED entry to INTERNED, updates memory tracker.

**WorkloadProfiler** *(new — Review 2)*
Observes the first 100 identifier insertions and extracts 5 features:
- `meanNameLength` — average character count across observed identifiers
- `prefixSimilarityCoeff` — fraction of consecutive insertion pairs sharing ≥4 leading characters
- `repeatRate` — duplicate insertions / total insertions
- `scopeDepthVariance` — variance of scope depth at time of each insertion
- `nameEntropy` — Shannon entropy over character distribution of observed names

After 100 insertions, marks itself complete and triggers ThresholdPredictor.

**ThresholdPredictor** *(new — Review 2)*
Runs once after WorkloadProfiler completes. Takes `WorkloadFeatures`, outputs `PolicyConfig` with 6 updated threshold values. Implementation is a hardcoded C++ header of learned Ridge regression coefficients — no ML library at runtime. Just 6 dot products + clamp.

**Adaptive Representation Policy — `decide()`**
The core policy function. Runs at every insert, constant time. Takes 5 inputs (name, isRepeat, pressure, accessFreqHint, prefixSharedChars). Decision order:
1. Exact repeat → INTERNED
2. High pressure + long name + not hot → COMPRESSED
3. Long name + prefix-similar + not hot → COMPRESSED
4. Short name + low pressure → INLINE
5. Default → INTERNED

**LRU Lookup Cache** *(new — Review 2)*
64-entry cache keyed by FNV-1a hash. On lookup hit: return cached entry pointer, skip all chain walking. On miss: do normal lookup, populate cache. On `exitScope()`: invalidate all cache entries whose symbols are being reclaimed. Tracks hit rate, miss rate, latency delta.

**Memoized Reconstruction** *(new — Review 2)*
Per `CompEntry`, a `reconstructedCache` string field. First lookup of a COMPRESSED entry pays the chain-walk cost once and stores the result. All subsequent lookups return the cached string directly. Tracks `memoColdLookups` and `memoHits`.

---

## 3. Three Representations

| Representation | When Used | Memory Cost | Lookup Cost |
|---|---|---|---|
| INLINE | Short name (`< inlineMaxLen`), low pressure | `len + 29` bytes | Direct — fastest |
| INTERNED | Exact repeat, hot symbol (promoted), default fallback | `28` bytes + pool once | Pool index deref |
| COMPRESSED | Long name (`≥ compressMinLen`), prefix-similar, not hot | `suffix_len + 31` bytes | Chain walk (memoized after first) |

---

## 4. Policy Configuration — `PolicyConfig`

Six thresholds, all tunable:

| Threshold | Review-1 Default | Grid-Search Optimum | Description |
|---|---|---|---|
| `inlineMaxLen` | 12 | 10 | Max name length for INLINE eligibility |
| `compressMinLen` | 10 | 8 | Min name length for COMPRESSED eligibility |
| `lowPressureThreshold` | 0.50 | 0.40 | Pressure below which INLINE preferred |
| `highPressureThreshold` | 0.85 | 0.80 | Pressure above which COMPRESSED forced |
| `hotAccessThreshold` | 3 | 3 | Access count triggering promotion |
| `prefixSimilarityMinShared` | 4 | 3 | Min shared prefix chars for COMPRESSED |

ML predictor overrides these after the 100-symbol profiling window.

---

## 5. File Structure

```
compiler/
├── include/
│   ├── budget_sym.hpp          # Core BUDGET-SYM implementation (rename internally to mnemo.hpp NOT recommended — keep budget_sym.hpp)
│   ├── lookup_cache.hpp        # NEW: LRU cache implementation
│   ├── workload_profiler.hpp   # NEW: 5-feature workload profiler
│   ├── predicted_thresholds.hpp # GENERATED: hardcoded Ridge coefficients
│   └── common.hpp              # Shared types: PolicyConfig, WorkloadFeatures, etc.
├── scripts/
│   ├── train_threshold_predictor.py  # NEW: data gen + model comparison + export
│   ├── plot_results.py               # Existing plotting
│   └── extract_identifiers.py        # NEW: tokenize real C codebases
├── benchmark/
│   ├── run_benchmark.cpp       # Extended with BudgetSym-WithCache variant
│   └── datasets/               # Synthetic + real codebase identifier sequences
├── tests/
│   └── smoke_test.cpp          # Extended with new component tests
├── results/
│   ├── benchmark_results_v3.csv     # Fresh full benchmark run
│   ├── model_comparison.csv         # ML model R², MAE, downstream compression
│   └── feature_ablation.csv         # Ridge R² with each feature dropped
├── docs/
│   ├── architecture.md         # Component-level architecture notes
│   └── future_work.md          # Known gaps
├── paper/
│   └── budget_sym_v2.tex       # IEEE paper source
├── frontend/                   # Existing Next.js dashboard (do not break)
├── build.sh                    # Must still work end-to-end
├── BUDGET_SYM_PRD.md                # This file
└── README.md
```

---

## 6. Implementation Requirements

### 6.1 `include/lookup_cache.hpp` — LRU Lookup Cache

```cpp
template<size_t N = 64>
class LRULookupCache {
public:
    struct CacheEntry {
        uint64_t hash;
        std::string name;
        void* entryPtr;
    };

    // Returns true on hit, sets outPtr. Moves hit entry to front.
    bool get(uint64_t hash, const std::string& name, void*& outPtr);

    // Insert at front. Evict LRU tail if size > N.
    void put(uint64_t hash, const std::string& name, void* entryPtr);

    // Called by exitScope() for each reclaimed symbol.
    void invalidate(uint64_t hash);

    struct CacheStats {
        int hits;
        int misses;
        double hitRate;
    };
    CacheStats stats() const;

    void reset();

private:
    std::list<CacheEntry> lruList_;
    std::unordered_map<uint64_t, typename std::list<CacheEntry>::iterator> index_;
    int hits_ = 0;
    int misses_ = 0;
};
```

**Constraints:**
- Header-only, C++14
- No external dependencies
- `invalidate()` must be called for every symbol reclaimed by `exitScope()`
- Must handle hash collisions: on `get()`, verify `entry.name == name` before returning hit

### 6.2 `include/workload_profiler.hpp` — WorkloadProfiler

```cpp
struct WorkloadFeatures {
    double meanNameLength;
    double prefixSimilarityCoeff;
    double repeatRate;
    double scopeDepthVariance;
    double nameEntropy;
};

class WorkloadProfiler {
public:
    static const int SAMPLE_TARGET = 100;

    // Call on every insert before profiling is complete
    void observe(const std::string& name, int scopeDepth, bool isRepeat);

    bool isComplete() const;

    // Call only after isComplete() returns true
    WorkloadFeatures extract() const;

    void reset();

private:
    int sampleCount_ = 0;
    // accumulators for each feature
    double totalLen_ = 0;
    int prefixSimilarPairs_ = 0;
    int totalPairs_ = 0;
    int repeatCount_ = 0;
    std::string prevName_;
    std::vector<int> scopeDepths_;
    std::unordered_map<char, int> charFreq_;
};
```

**Prefix similarity check:** `sharedPrefixLength(prevName_, name) >= 4` counts as a similar pair.
**Entropy formula:** `H = -sum(p * log2(p))` over character frequencies.

### 6.3 `include/predicted_thresholds.hpp` — Generated by Training Script

This file is generated by `scripts/train_threshold_predictor.py` and committed. Format:

```cpp
// AUTO-GENERATED by scripts/train_threshold_predictor.py
// DO NOT EDIT MANUALLY
// Model: Ridge Regression (alpha=1.0), trained on 300 synthetic workloads
// R² scores: inlineMaxLen=X.XX, compressMinLen=X.XX, ...

#pragma once
#include "common.hpp"

struct ThresholdPredictor {
    static PolicyConfig predict(const WorkloadFeatures& f) {
        PolicyConfig c;

        // inlineMaxLen: coefficients [w1..w5] + intercept
        double raw0 = W0_0*f.meanNameLength + W0_1*f.prefixSimilarityCoeff
                    + W0_2*f.repeatRate + W0_3*f.scopeDepthVariance
                    + W0_4*f.nameEntropy + B0;
        c.inlineMaxLen = clamp((int)raw0, 6, 20);

        // ... repeat for all 6 thresholds ...

        return c;
    }

private:
    static int clamp(int v, int lo, int hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // Weights filled in by training script
    static constexpr double W0_0 = /* GENERATED */;
    // ...
};
```

### 6.4 `include/budget_sym.hpp` — Core Updates

Add the following members to `BudgetSym`:
```cpp
LRULookupCache<64> lookupCache_;
WorkloadProfiler profiler_;
// predicted_thresholds.hpp included, ThresholdPredictor used after profiling
```

**In `insert(name)`:**
```
1. if (!profiler_.isComplete())
       profiler_.observe(name, currentScopeDepth(), isRepeat)
   if (profiler_.isComplete() && !thresholdsPredicted_) {
       cfg_ = ThresholdPredictor::predict(profiler_.extract());
       thresholdsPredicted_ = true;
   }
2. [existing decide() and storage logic]
```

**In `lookup(name)`:**
```
1. h = fnv1a(name)
2. void* ptr = nullptr;
   if (lookupCache_.get(h, name, ptr)) {
       // cache hit: increment accessCount, maybe promote, return true
   }
3. [existing lookup logic]
4. on found: lookupCache_.put(h, name, foundEntryPtr)
```

**In `exitScope()`:**
```
for each symbol s in top scope:
    lookupCache_.invalidate(fnv1a(s.name))
    releaseEntry(s)
pop scope stack
```

**In `CompEntry` (memoization):**
```cpp
struct CompEntry {
    int prevIndex;
    int sharedPrefixLen;
    std::string suffix;
    std::string reconstructedCache;  // NEW: empty = not yet memoized
};
```

**In `reconstructFull(int idx)`:**
```
if (!compPool_[idx].reconstructedCache.empty())
    return compPool_[idx].reconstructedCache;  // memoized hit
// ... existing chain walk ...
compPool_[idx].reconstructedCache = result;    // store before returning
return result;
```

### 6.5 `scripts/train_threshold_predictor.py` — Training Pipeline

**Step 1 — Data Generation (300 workloads)**
```python
for each of 300 workloads:
    sample parameters:
        mean_len ~ Uniform(4, 20)
        prefix_coeff ~ Uniform(0, 0.8)
        repeat_rate ~ Uniform(0, 0.5)
        depth_var ~ Uniform(0, 5)
        entropy ~ Uniform(2.0, 5.0)
    generate 500 identifiers matching these characteristics
    run grid search (all 15,000 PolicyConfig combinations)
    record optimal thresholds for this workload
    record (features, optimal_thresholds) pair
```

**Step 2 — Model Comparison**
Train and evaluate ALL of the following on 80/20 train/test split:

| Model | Library | Notes |
|---|---|---|
| Baseline Mean | numpy | Predict mean of training labels — lower bound |
| Lasso (alpha=1.0) | sklearn | L1 — feature selection |
| Ridge (alpha=1.0) | sklearn | **Chosen model** |
| Decision Tree (max_depth=5) | sklearn | Non-linear baseline |
| Random Forest (100 trees) | sklearn | Best accuracy, not exportable |
| SVR (RBF kernel) | sklearn | Non-linear, runtime dep |
| KNN (k=5) | sklearn | Nearest-neighbor baseline |

**Metrics per model per threshold:**
- R² on test set
- MAE on test set (in real threshold units)
- Downstream compression ratio: use predicted thresholds on 8 benchmark datasets, report mean compression ratio achieved

**Step 3 — Feature Ablation (Ridge only)**
Train Ridge with all 5 features. Then drop one feature at a time. Report R² delta per feature per threshold. This identifies which features matter.

**Step 4 — Export**
Export Ridge coefficients and intercepts as `include/predicted_thresholds.hpp`.

**Output files:**
- `results/model_comparison.csv` — all models × all thresholds × all metrics
- `results/feature_ablation.csv` — Ridge R² per dropped feature per threshold
- `include/predicted_thresholds.hpp` — generated C++ header

### 6.6 `scripts/extract_identifiers.py` — Real Codebase Extraction

```python
# Usage: python extract_identifiers.py --repo /path/to/freertos --out datasets/freertos.txt
# Traverses all .c and .h files
# Regex: \b[A-Za-z_][A-Za-z0-9_]*\b
# Filters: C keywords, standard library names (predefined blocklist)
# Output: one identifier per line, in order of first appearance
```

Run on:
- FreeRTOS v10.6.1 (kernel + portable layers)
- Arduino Core AVR
- Zephyr RTOS v3.6 (kernel/ subtree only)

Output: `benchmark/datasets/freertos.txt`, `arduino.txt`, `zephyr.txt`

---

## 7. Benchmark Requirements

### 7.1 Implementations to Benchmark

| Label | Description |
|---|---|
| Conventional | `unordered_map<string, Symbol>` — 1.00× reference |
| Interned | Plain string interning, refcounted pool |
| BudgetSym-R1 | Original hand-picked thresholds, no cache |
| BudgetSym-R2-GridSearch | Grid-search optimized thresholds, no cache |
| BudgetSym-R2-ML | ML-predicted thresholds, no cache |
| **BudgetSym-R2-Full** | ML-predicted thresholds + LRU cache + memoization |

### 7.2 Datasets

**Synthetic (existing):** small, medium, large, high-prefix-similarity, random-identifiers, nested-scopes, hot-cold-access, memory-stress

**Real-world (new):** freertos, arduino, zephyr

### 7.3 Metrics Per Run

```csv
implementation, dataset, tracked_memory_bytes, lookup_latency_us,
insert_latency_us, cache_hit_rate, promotions, bytes_reclaimed,
memo_hits, memo_cold_lookups, compression_ratio
```

### 7.4 Output

`results/benchmark_results_v3.csv` — all implementations × all datasets × all metrics

### 7.5 Ablation Study (existing, keep)

Four variants on fixed workload:
- BudgetSym-Full
- BudgetSym-NoScope
- BudgetSym-NoAccessFrequency
- BudgetSym-NoAdaptiveSelection

---

## 8. Test Requirements

All tests in `tests/smoke_test.cpp`. Build with `bash build.sh`. All must pass.

### 8.1 Existing Tests (must still pass)
- Correctness: FOUND/FOUND/NOT FOUND on basic lookup
- Scope reclamation: symbols released on exitScope
- Promotion: COMPRESSED → INTERNED after hotAccessThreshold lookups

### 8.2 New Tests — Memoization
```
insert 3 COMPRESSED entries (names length >= compressMinLen, prefix-similar)
lookup entry[0] five times
assert: compPool_[entry[0].compIndex].reconstructedCache is not empty
assert: memoColdLookups == 1
assert: memoHits == 4
```

### 8.3 New Tests — LRU Cache
```
insert 10 symbols
lookup same symbol 20 times
assert: cache.stats().hitRate > 0.90
assert: cache.stats().hits == 19  (first lookup is a miss)

insert 5 symbols into a nested scope
exitScope()
assert: none of those 5 symbols are in the cache (invalidation fired)
```

### 8.4 New Tests — WorkloadProfiler
```
insert 100 identifiers all starting with "temperature" (length ~16)
assert: profiler.isComplete() == true
features = profiler.extract()
assert: features.meanNameLength > 14.0
assert: features.prefixSimilarityCoeff > 0.8
```

### 8.5 New Tests — ThresholdPredictor
```
// High-prefix-similarity workload features
WorkloadFeatures f = {16.0, 0.9, 0.1, 1.0, 2.5};
PolicyConfig c = ThresholdPredictor::predict(f);
assert: c.compressMinLen <= 8       // model should predict aggressive compression
assert: c.prefixSimilarityMinShared <= 3
assert: c.inlineMaxLen >= 6 && c.inlineMaxLen <= 20  // within valid range

// Random-identifier workload features (low prefix similarity)
WorkloadFeatures f2 = {8.0, 0.05, 0.05, 2.0, 4.5};
PolicyConfig c2 = ThresholdPredictor::predict(f2);
assert: c2.compressMinLen >= 10     // model should predict conservative compression
```

### 8.6 New Tests — Integration
```
Run full BudgetSym-R2-Full on high-prefix-similarity dataset
assert: compression_ratio > 1.5x conventional
assert: cache_hit_rate > 0.5
assert: lookup_latency_us < (BudgetSym-R1 lookup_latency_us * 1.2)
   // cache should bring latency within 20% of uncached, not worse
```

---

## 9. IEEE Paper Requirements

File: `paper/budget_sym_v2.tex`

### 9.1 Section Structure

| Section | Content |
|---|---|
| I. Introduction | Problem, gap, contributions list (9 items), Review-1 vs Review-2 split |
| II. Background | Symbol tables, embedded systems, interning, prefix compression, scope, memory-latency tradeoff |
| III. Literature Review | Related work + research gap — hybrid per-symbol selection not found |
| IV. Problem Formulation | Cost function C = αM + βL + γT, heuristic instantiation |
| V. Architecture | All 8 components described with one paragraph each, pipeline diagram |
| VI. Adaptive Representation Policy | Three representations, decide() logic, promotion |
| VII. Memory Accounting Model | Tracked-memory model, not RSS, formula per representation |
| VIII. Implementation | C++14 header-only, interface, bugs found during dev |
| VIII-B. Threshold Optimization [R2] | Grid search → ML predictor motivation |
| VIII-C. Workload Profiler [R2] | 5 features, 100-symbol window, extraction formulas |
| VIII-D. ML Threshold Prediction [R2] | Model comparison table, Ridge chosen, why, feature ablation |
| VIII-E. Latency Reduction [R2] | LRU cache design, memoization, hit rate results per dataset |
| VIII-F. Real-World Evaluation [R2] | FreeRTOS/Arduino/Zephyr results |
| VIII-G. Statistical Validation [R2] | 30-seed, 95% CI, p < 0.01 on all datasets |
| IX. Experimental Methodology | Baselines, datasets, metrics, controlled variables |
| X. Evaluation Metrics | Memory, latency, statistical, adaptive, cache metrics |
| XI. Results | All tables: main compression, cache latency, ML model comparison |
| XII. Ablation Study | 4-variant ablation table |
| XIII. Discussion | Pareto frontier argument, when BUDGET-SYM is and isn't worth it |
| XIV. Threats to Validity | 7 limitations, honestly stated |
| XV. Future Work | Chain fragmentation fix, online adaptation, LLVM integration |
| XVI. Conclusion | Summary of all contributions |

### 9.2 Required Tables

**Table I — Main Compression Results**
Columns: Dataset | Interned | BudgetSym-R1 | BudgetSym-R2-GridSearch | BudgetSym-R2-ML | BudgetSym-R2-Full | 95% CI

**Table II — Ablation Study**
Columns: Variant | Tracked Memory | Promotions | Bytes Reclaimed

**Table III — ML Model Comparison**
Columns: Model | Mean R² | Mean MAE | Downstream Compression Ratio | Exportable as C++ Constants

**Table IV — Optimized Threshold Values**
Columns: Threshold | Review-1 | Grid-Search | ML-Predicted

**Table V — Real-World Codebase Results**
Columns: Codebase | Tokens | Interned | BudgetSym-R1 | BudgetSym-R2-Full

**Table VI — Cache Performance**
Columns: Dataset | Hit Rate | Latency (no cache) µs | Latency (with cache) µs | vs Conventional µs

**Table VII — Feature Ablation (Ridge)**
Columns: Dropped Feature | Mean R² Delta across 6 thresholds | Most affected threshold

### 9.3 Key Claims the Paper Must Make

- "BUDGET-SYM reduces tracked memory by X×–Y× over conventional, compared with 1.05×–1.07× from plain interning"
- "The LRU lookup cache achieves Z% hit rate on hot-path workloads, reducing BudgetSym lookup latency to within W% of the conventional baseline"
- "Ridge regression outperforms the baseline mean predictor and achieves competitive R² vs Random Forest, while being the only model exportable as compile-time constants with no runtime overhead"
- "Scope-aware reclamation remains the dominant mechanism — the ablation shows disabling it increases memory by 45%"
- "All compression ratio advantages are statistically significant at p < 0.01 across 30 seeds"
- "Results transfer to real embedded codebases — FreeRTOS, Arduino, and Zephyr all show >1.4× improvement over conventional"

---

## 10. Build Requirements

`bash build.sh` must:
1. Compile all C++ components with `-std=c++14`
2. Run `tests/smoke_test.cpp` — all assertions must pass, zero failures
3. Run `benchmark/run_benchmark.cpp` — produce `results/benchmark_results_v3.csv`
4. Print summary: compression ratios and cache hit rates per dataset to stdout

Python training pipeline (`scripts/train_threshold_predictor.py`) is run **separately** before the C++ build to regenerate `include/predicted_thresholds.hpp`. The generated file is committed so the C++ build has no Python dependency.

**Dependencies:**
- C++: none beyond stdlib + C++14
- Python (training only): `sklearn`, `numpy`, `pandas`, `scipy`
- LaTeX: `IEEEtran.cls` (from `texlive-publishers`), standard packages

---

## 11. Deliverables Checklist

### Code
- [ ] `include/lookup_cache.hpp` — LRU cache, 64 entries, invalidate(), stats()
- [ ] `include/workload_profiler.hpp` — 5-feature profiler, 100-symbol window
- [ ] `include/predicted_thresholds.hpp` — generated, hardcoded Ridge coefficients
- [ ] `include/budget_sym.hpp` — memoization, cache integration, profiler, predictor
- [ ] `include/common.hpp` — WorkloadFeatures added to shared types
- [ ] `scripts/train_threshold_predictor.py` — data gen + 7 models + export
- [ ] `scripts/extract_identifiers.py` — real codebase tokenizer
- [ ] `benchmark/run_benchmark.cpp` — BudgetSym-R2-Full variant added
- [ ] `benchmark/datasets/freertos.txt` — extracted identifiers
- [ ] `benchmark/datasets/arduino.txt`
- [ ] `benchmark/datasets/zephyr.txt`
- [ ] `tests/smoke_test.cpp` — all new tests added

### Results
- [ ] `results/benchmark_results_v3.csv` — all implementations × all datasets
- [ ] `results/model_comparison.csv` — 7 models × 6 thresholds × metrics
- [ ] `results/feature_ablation.csv` — Ridge R² per dropped feature

### Paper
- [ ] `paper/budget_sym_v2.tex` — updated with all new sections and tables
- [ ] `paper/budget_sym_v2.pdf` — compiled, >6 pages, all tables present

### Verification
- [ ] `bash build.sh` exits 0
- [ ] All smoke tests pass
- [ ] All 7 tables present in compiled PDF
- [ ] Cache hit rate > 0% on hot-cold-access dataset (sanity check)
- [ ] ML-predicted thresholds differ from hand-picked on at least 3 thresholds (confirms predictor is doing something)
- [ ] R² > 0.3 on at least 4 of 6 threshold models (otherwise training data gen is broken)

---

## 12. Known Limitations (Do Not Fix in This Phase)

- **COMPRESSED chain interior reclamation** — interior nodes stay allocated until re-anchor (every 8 inserts). Tombstoned but bytes not freed. Document in paper Section XIV, flag in `docs/future_work.md`.
- **Single-threaded only** — no concurrent access support. Document in Threats to Validity.
- **ML model trained on synthetic workloads** — real-world generalization reported but not exhaustively validated. Document in Threats to Validity.
- **LRU cache eviction policy** — LRU chosen for simplicity. LFU or ARC might perform better on some workloads. Flag in future work.
- **Prior-art search incomplete** — full IEEE Xplore / ACM DL search not done. Must be stated in paper Section XIV before any patentability claim.

---

## 13. What Not to Touch

- `frontend/` — Next.js dashboard is functional, do not break it, do not expand it
- Existing benchmark CSV format compatibility — `benchmark_results_v3.csv` adds columns but keeps existing column order
- The IEEE paper's existing Review-1 claims — all original results remain, new sections are additive
- `build.sh` interface — output behavior may change but invocation must remain `bash build.sh`