# Negative Results & Failed Approaches Log

To preserve full scientific provenance and document design iterations, this document records all data structure prototypes, algorithm variants, and machine learning experiments in METIS development that failed to meet performance targets or introduced intolerable trade-offs.

---

## 1. Summary Matrix of Negative Results

| Approach Prototype | Hypothesized Benefit | Empirical Outcome | Technical Root Cause | Reason for Abandonment |
| :--- | :--- | :--- | :--- | :--- |
| **Incremental Prefix Front-Coding** | $20\text{--}35\%$ memory reduction for identifier strings sharing prefixes | $2.4\times$ lookup latency slowdown; memory savings $< 4\%$ | Sequential decoding dependence; unaligned byte traversals; slice header metadata overhead | Violates real-time compiler lookup latency constraints |
| **Split Metadata Side-Tables (`SymTabV4`)** | Higher L1 cache density by isolating 8-byte hot metadata from payload | $+14\%$ physical RAM overhead; $1.8\times$ cache miss rate increase | Pointer indirection between metadata vector and remote payload vector; cache line fragmentation | Sparse side-table overheads exceed metadata savings |
| **ML-Driven Demotion Oracle (`train_ml_oracle.py`)** | Optimal scope demotion decisions predicted via Decision Trees / Random Forests | $+350\ \mu\text{s}$ scope latency penalty; model size footprint $> 450\text{ KB}$ | Feature extraction runtime and model evaluation latency exceed scope execution budget | Deterministic bump-pointer scope recycling outperforms ML inference |
| **Global Dynamic String Interning** | Single copy of distinct identifier strings across entire AST | $+18\%$ runtime latency penalty; high global lock contention | Overhead of interning hash table lookups for short-lived, out-of-scope variables | Short-lived AST variables do not benefit from cross-scope deduplication |

---

## 2. Deep-Dive Analyses

### 2.1 Incremental Prefix Front-Coding
* **Concept**: Sort identifiers lexicographically and represent subsequent entries by storing the length of the matching prefix alongside suffix bytes (e.g., `buffer_size` and `buffer_index` $\to$ `6"index"`).
* **Empirical Failure**: To execute string equality or hash verification during a lookup operation, the engine was forced to decode ancestor strings sequentially starting from the nearest anchor entry. This transformed $O(1)$ string lookup into an $O(K \cdot L)$ sequential byte decoding loop, introducing a $2.4\times$ tail latency regression.

### 2.2 Machine Learning Threshold Predictor
* **Concept**: Train offline machine learning models (Random Forests, Logistic Regression) to predict optimal scope allocation sizes and demotion thresholds based on static AST features (nesting depth, identifier count).
* **Empirical Failure**: Evaluating ML model decision trees at runtime required extracting features (e.g., mean name length, scope depth variance) and executing tree traversals. The inference latency ($+350\ \mu\text{s}$) and memory footprint ($> 450\text{ KB}$) far outweighed the minor memory gains. A simple, zero-overhead $O(1)$ adaptive bump-pointer arena reset proved far superior.

### 2.3 Split Metadata Side-Tables (`SymTabV4`)
* **Concept**: Split symbol representations into a dense 8-byte metadata array (`hash`, `scope_id`, `state`) for fast linear scanning and store full symbol payloads (`name`, `type_info`) in a remote payload vector.
* **Empirical Failure**: While initial metadata probing fit within L1 cache lines, every candidate match triggered an un-cached secondary pointer dereference to access the remote payload vector. This doubled L2/L3 cache misses and increased overall RAM footprint by $14\%$ due to dual vector header overheads.

### 2.4 Dynamic Global String Interning
* **Concept**: Maintain a global hash map of unique string buffers across all scopes to guarantee exact string deduplication.
* **Empirical Failure**: In compiler workloads, $80\text{--}90\%$ of symbols declared in local functions or nested blocks are transient and out-of-scope after block completion. Global interning paid lock synchronization and global table insertion costs for short-lived names that provided zero long-term memory benefit.
