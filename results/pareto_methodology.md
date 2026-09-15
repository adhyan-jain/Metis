# Latency-Constrained Pareto Optimization Methodology & Trade-off Analysis

This document details the experimental design, dataset taxonomy, latency-constrained optimization framework, and empirical findings for the Pareto optimization experiment per **CLAUDE_RESEARCH.md Sections 11 & 12**.

---

## 1. Executive Summary & Core Findings

1. **V2 vs V1 Memory Breakthrough**: Across all evaluated real-world corpora and synthetic workloads, `SymTabV2` reduces measured heap consumption by **$52\%\text{--}80\%$ compared to BudgetSym V1** ($0.20\times\text{--}0.48\times$ of V1's heap). Free-list slot allocation successfully eliminates V1's append-only vector bloat.
2. **Lookup Speed Advantage**: On real-world embedded corpora (FreeRTOS, Arduino, Zephyr), `SymTabV2` achieves a **$34\%\text{--}41\%$ lookup speedup** over `ConventionalSymbolTable` ($p_{50}$ cold lookup latency $0.064\mu s\text{--}0.083\mu s$ vs Conventional $0.100\mu s\text{--}0.125\mu s$), driven by open-addressing index locality and 32-bit/8-bit fingerprint rejection.
3. **Memory vs. Latency Trade-off**: Enabling front-coding block compression (`compressMinLen < 999`) reduces string storage footprint but increases lookup latency due to character reconstruction depth. Disabling compression (`compressMinLen = 999`) or setting high inline thresholds (`inlineMaxLen = 20`) minimizes lookup latency at a higher memory footprint, establishing an explicit Pareto trade-off curve.

---

## 2. Dataset Taxonomy (CLAUDE_RESEARCH.md Section 11)

The evaluation spans 12 workloads across three distinct categories:

### Category A — Representative Real-World Corpora
- **FreeRTOS**: Real embedded C kernel event stream (25,000 events, 10K unique symbols).
- **Arduino**: C++ microcontroller framework event stream (25,000 events, 11K unique symbols).
- **Zephyr**: Large RTOS build event stream (25,000 events, 228K unique symbols).

### Category B — Redundancy-Rich Workloads
- `small`, `medium`, `large`: Synthetic baseline workloads (100 to 10,000 declarations).
- `high-prefix-similarity`: Contiguous symbol runs sharing long common prefixes (`moduleConfigParameter0`...`moduleConfigParameterN`).
- `hot-cold-access`: Skewed access locality where 10% of symbols receive repeated lookups.
- `nested-scopes`: High lexical scope depth (40 scopes, 50 symbols per scope) testing reclamation.

### Category C — Adversarial Workloads
- `random-long`: Uniform random 20–40 character identifiers (no shared prefixes).
- `high-churn`: Short-lived variables in rapidly created/destroyed scopes (chunk size 5).
- `memory-stress`: Long unique identifiers under tight memory budgets.

---

## 3. Latency-Constrained Pareto Optimization Framework

The central objective is:

$$\text{MINIMIZE MEMORY} \quad \text{subject to} \quad \text{LOOKUP\_LATENCY} \le L \times \text{CONVENTIONAL\_LOOKUP\_LATENCY}$$

for predetermined latency constraints:
- $L = 1.10\times$ ($10\%$ latency overhead allowed)
- $L = 1.25\times$ ($25\%$ latency overhead allowed)
- $L = 1.50\times$ ($50\%$ latency overhead allowed)
- $L = 2.00\times$ ($100\%$ latency overhead allowed)

### Evaluated Policy Parameter Space (`src/pareto_main.cpp`)
The grid sweep evaluates $1,700$ distinct `PolicyConfigV2` combinations per workload:
- `inlineMaxLen`: $\{8, 12, 16, 20\}$
- `compressMinLen`: $\{8, 12, 16, 24, 999\}$ ($999 = \text{compression disabled}$)
- `blockSize`: $\{4, 8, 16, 32, 64\}$
- `anchorInterval`: $\{2, 4, 8, 16\}$ ($\le \text{blockSize}$)
- `hotAccessThreshold`: $\{1, 2, 3, 5, 999\}$ ($999 = \text{promotion disabled}$)

---

## 4. Answers to Research Specification Questions (Section 12.3)

### Q1: What memory savings are achieved at $1.10\times, 1.25\times, 1.50\times, 2.00\times$ latency bounds?
- **Real-world Corpora (FreeRTOS, Arduino, Zephyr)**: `SymTabV2` achieves its fastest lookup latencies ($0.59\times\text{--}0.66\times$ of Conventional) when using INLINE/INTERNED representations (`compressMinLen \ge 24`), easily satisfying the $1.10\times$ constraint. However, `ConventionalSymbolTable` maintains the lowest measured heap footprint ($0.63\text{ MB}\text{--}0.82\text{ MB}$ vs V2's $0.83\text{ MB}\text{--}1.31\text{ MB}$) due to V2's slot metadata vector (`entries_`) and pool indexing structures.
- **Redundancy-Rich Workloads (`nested-scopes`, `hot-cold-access`)**: At $L=1.10\times$, `SymTabV2` achieves **$8.15\%$ memory savings over Conventional** on `nested-scopes` while being $49\%$ faster ($0.118\mu s$ vs $0.230\mu s$). Compared to BudgetSym V1, `SymTabV2` saves **$52\%\text{--}80\%$ memory** across all latency bounds.

### Q2: Which workloads have no useful Pareto frontier?
- **Short-lived Adversarial Workloads (`high-churn`, `random-long`)**: On workloads with zero prefix similarity and rapid scope destruction, front-coding block compression offers no memory savings because shared prefix lengths are $\le 1$ char. For these workloads, disabling compression (`compressMinLen=999`) or using `Conventional` is optimal.

### Q3: Which workload properties correlate with improvement?
1. **Prefix Similarity**: High prefix similarity ($>0.15$) enables block front-coding to compress string bytes by $35\%\text{--}50\%$.
2. **Access Skew & Locality**: Skewed access patterns (Gini $>0.75$) allow `hotAccessThreshold` promotion to shift hot symbols to INTERNED tier, eliminating reconstruction latency for frequent lookups.
3. **Lexical Scope Churn & Depth**: High scope depth and churn allow V2's free-list slot allocator to achieve $50\%\text{--}80\%$ slot reuse ratios, avoiding heap expansion.

---

## 5. Artifacts & Generated Files

- **Source Code**: [src/pareto_main.cpp](file:///home/adhyan/Desktop/Compiler/src/pareto_main.cpp)
- **Full Results CSV**: [results/pareto_results.csv](file:///home/adhyan/Desktop/Compiler/results/pareto_results.csv) ($20,400$ configuration evaluation rows)
- **Pareto Frontier CSV**: [results/pareto_frontier.csv](file:///home/adhyan/Desktop/Compiler/results/pareto_frontier.csv) ($48$ optimal constrained configuration rows)
- **Visualization Figures**:
  - [figures/pareto_memory_vs_latency.png](file:///home/adhyan/Desktop/Compiler/figures/pareto_memory_vs_latency.png)
  - [figures/pareto_frontier_by_workload.png](file:///home/adhyan/Desktop/Compiler/figures/pareto_frontier_by_workload.png)
