# Phase 4 Experiment Plan: Arena Offsets vs. Heap SSO

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`
**Phase:** Phase 4 (Candidate D Controlled Experiment)
**Deliverable:** `research/l3/phase4_experiment_plan.md`

## 1. Primary Research Question
On the same real embedded-SDK symbol-table traces, does replacing METIS-X's inline-string/heap-overflow representation with a 4-byte prefix plus 32-bit arena-offset storage reduce peak physical memory without materially worsening $p_{95}$ lookup latency?

## 2. Experimental Pre-registration

### Workloads and Configurations
- **Control:** Current `METIS-X` implementation (`include/metis_x.hpp`), unchanged.
- **Treatment:** `Candidate D` (`include/metis_x_arena.hpp`), modifying only string storage layout to use a contiguous `std::vector<char>` arena.
- **Workloads:** The exact canonical Zephyr ($2.65\text{M}$ events) and ESP-IDF ($2.42\text{M}$ events) AST traces.
- **Measurement Tooling:** Identical batch-driven isolated profiling harness. Linux core-pinned execution (`taskset -c 0`). Peak heap tracked via `malloc_usable_size` interception.

### Metrics Definitions
- **Primary Metric 1 (Memory):** Peak Physical Heap (bytes), encompassing the slot vector capacity and all string storage overhead (glibc chunks for Control, `std::vector` capacity for Treatment).
- **Primary Metric 2 (Latency):** $p_{50}$ and $p_{95}$ lookup latency ($\mu\text{s}$).
- **Secondary Metrics:** Insertion throughput (ops/sec), insertion latency, bytes per live entry, total retained unused arena capacity.
- **Correctness:** Verified via differential lookup equality against the Control.

### Hypothesis & Confounds
- **Null Hypothesis ($H_0$):** Arena indirection will increase $p_{95}$ lookup latency due to L2/L3 cache misses exceeding any L1 density benefits, and memory will not substantially decrease due to transient string accumulation in the non-reclaiming arena.
- **Alternative Hypothesis ($H_1$):** Eliminating 32-byte glibc chunk overhead per long identifier will reduce peak heap by $>10\%$, and the 4-byte in-slot prefix will reject enough hash collisions to maintain $p_{95}$ lookup latency within a 5% margin of the Control.
- **Confounder:** Lifetime model. `METIS-X` frees heap strings on `exitScope()`. Candidate D appends to a contiguous `std::vector<char>` arena which *cannot* reclaim individual strings upon scope exit. Consequently, transient long identifiers will accumulate throughout compilation.

### Decision Thresholds
- **GO:** Candidate D reduces Peak Physical Heap on ESP-IDF by $\ge 10\%$ AND degrades $p_{95}$ lookup latency by $\le 5\%$.
- **NO-GO:** Candidate D increases Peak Physical Heap (due to transient string accumulation) OR degrades $p_{95}$ lookup latency by $> 5\%$.
- **INCONCLUSIVE:** Measurement noise (variance across 5 repetitions $> 5\%$) obscures the effect.
