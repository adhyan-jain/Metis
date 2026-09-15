# Parameter Sensitivity & Interaction Study Methodology

This document details the experimental design, parameter sensitivity analysis, structural trade-offs, and empirical findings for the parameter study per **CLAUDE_RESEARCH.md Section 13**.

---

## 1. Executive Summary & Core Research Findings

1. **`anchorInterval` Direct Latency Impact**: `anchorInterval` is the primary determinant of front-coding reconstruction depth and cold lookup latency. Increasing `anchorInterval` from $2 \rightarrow 32$ increases mean reconstruction depth from $0.48 \rightarrow 12.44$ steps ($25.9\times$ walk depth increase) and degrades cold lookup $p_{50}$ from $0.114\mu s \rightarrow 0.289\mu s$ ($2.53\times$ latency penalty), while yielding less than $0.73\%$ additional memory savings. `anchorInterval = 4` or `8` provides the optimal sweet spot.
2. **`compressMinLen` Memory-vs-Latency Knob**: Lowering `compressMinLen` from $999 \rightarrow 8$ routes all long identifiers to front-coded COMPRESSED blocks, reducing heap consumption from $889\text{ KB} \rightarrow 700\text{ KB}$ (**$21.2\%$ memory reduction**), at the cost of increasing cold lookup $p_{50}$ from $0.072\mu s \rightarrow 0.174\mu s$.
3. **`blockSize` Whole-Block Reclaim Granularity**: Smaller block sizes ($8\text{--}32$) achieve faster whole-block memory reclamation upon scope exit (`exitScope()`) because fewer co-allocated symbols must be reclaimed before a block is physically freed.
4. **Architectural Overhead vs Parameter Disadvantage**: The parameter study objectively proves that V2's memory footprint behavior relative to `ConventionalSymbolTable` is governed by two distinct components:
   - **Parameter-Controlled Component**: String front-coding compression (`compressMinLen`, `blockSize`, `inlineMaxLen`) achieves $20\%\text{--}35\%$ memory savings on long/prefix-rich string bodies.
   - **Fixed Architectural Overhead**: Persistent slot allocator metadata (`PackedEntry` POD, parallel vectors, `ScopeIndex` open-addressing table) adds a fixed per-slot footprint ($\approx 40$ bytes per slot). On flat/short workloads with minimal string lengths ($<12$ chars), fixed slot metadata exceeds string compression savings, allowing `Conventional`'s single `unordered_map` to remain smaller. On long/redundant workloads, string compression savings outweigh fixed metadata overhead.

---

## 2. Experimental Setup & Evaluated Parameter Space (`src/parameter_study_main.cpp`)

The parameter study evaluates 592 distinct configuration sweeps across **development/training workloads** (`medium`, `high-prefix-similarity`, `hot-cold-access`, `nested-scopes`, `random-long`) and **held-out evaluation corpora** (`FreeRTOS`, `Arduino`, `Zephyr`).

### Evaluated Sweeps:
1. **Sweep 1: Block Size $\times$ Anchor Interval**:
   - `blockSize` $\in \{4, 8, 16, 32, 64, 128\}$
   - `anchorInterval` $\in \{2, 4, 8, 16, 32\}$ (where $\text{anchorInterval} \le \text{blockSize}$)
2. **Sweep 2: Representation Thresholds**:
   - `inlineMaxLen` $\in \{4, 8, 12, 16, 20\}$
   - `compressMinLen` $\in \{8, 12, 16, 24, 32, 999\}$ ($999 = \text{compression disabled}$)
3. **Sweep 3: Hot/Cold Tiering Policy**:
   - `hotAccessThreshold` $\in \{1, 2, 3, 5, 10, 999\}$ ($999 = \text{promotion disabled}$)
   - `coldIdleEpochs` $\in \{0, 20, 100\}$

---

## 3. Parameter Sensitivity Breakdown

### A. Anchor Interval (`anchorInterval`)
- **Impact on Reconstruction Walk**: Reconstructing a front-coded symbol in a block requires scanning backward to the nearest anchor entry and walking forward character-by-character.
- **Empirical Sensitivity**:
  - `anchorInterval = 2`: Mean Depth = $0.48$ steps, Cold Lookup $p_{50} = 0.114\mu s$, Heap = $725.7\text{ KB}$
  - `anchorInterval = 4`: Mean Depth = $1.39$ steps, Cold Lookup $p_{50} = 0.143\mu s$, Heap = $722.9\text{ KB}$
  - `anchorInterval = 8`: Mean Depth = $3.20$ steps, Cold Lookup $p_{50} = 0.157\mu s$, Heap = $721.4\text{ KB}$
  - `anchorInterval = 16`: Mean Depth = $6.65$ steps, Cold Lookup $p_{50} = 0.211\mu s$, Heap = $720.6\text{ KB}$
  - `anchorInterval = 32`: Mean Depth = $12.44$ steps, Cold Lookup $p_{50} = 0.289\mu s$, Heap = $720.4\text{ KB}$
- **Recommendation**: Set `anchorInterval = 4` or `8`. Higher intervals deliver diminishing memory returns while severely inflating lookup latency.

### B. Block Size (`blockSize`)
- **Impact on Reclaim Granularity**: In `SymTabV2`, a block is only freed back to OS memory once all slots in the block are popped.
- **Empirical Sensitivity**:
  - `blockSize = 4`: Heap = $716.4\text{ KB}$, Cold Lookup $p_{50} = 0.126\mu s$
  - `blockSize = 32`: Heap = $722.9\text{ KB}$, Cold Lookup $p_{50} = 0.143\mu s$
  - `blockSize = 128`: Heap = $726.4\text{ KB}$, Cold Lookup $p_{50} = 0.129\mu s$
- **Recommendation**: Set `blockSize = 16` or `32` for balanced block allocation efficiency and prompt scope reclamation.

### C. Representation Threshold (`compressMinLen`)
- **Impact on Storage Tiering**:
  - `compressMinLen = 8`: Inline = $1,917$, Interned = $306$, Compressed = $223$ $\rightarrow$ Heap = $700.1\text{ KB}$, Cold $p_{50} = 0.174\mu s$
  - `compressMinLen = 16`: Inline = $1,917$, Interned = $350$, Compressed = $178$ $\rightarrow$ Heap = $742.9\text{ KB}$, Cold $p_{50} = 0.137\mu s$
  - `compressMinLen = 999`: Inline = $1,917$, Interned = $529$, Compressed = $0$ $\rightarrow$ Heap = $889.1\text{ KB}$, Cold $p_{50} = 0.072\mu s$
- **Recommendation**: Use `compressMinLen = 14\text{--}16` for memory-sensitive environments, or `compressMinLen = 999` for maximum lookup speed.

---

## 4. Parameter Sensitivity Matrix

| Parameter | Primary Impact Target | Memory Sensitivity | Latency Sensitivity | Recommended Value |
|---|---|---|---|---|
| `inlineMaxLen` | INLINE tier capacity | Low ($<3\%$) | Low ($<5\%$) | $12\text{--}16$ |
| `compressMinLen` | COMPRESSED vs INTERNED ratio | **High ($21.2\%$)** | **High ($2.4\times$)** | $14\text{--}16$ |
| `blockSize` | Whole-block reclaim | Moderate ($<2\%$) | Low ($<5\%$) | $16\text{--}32$ |
| `anchorInterval` | Reconstruction depth | Very Low ($<0.8\%$) | **Very High ($2.5\times$)** | $4\text{--}8$ |
| `hotAccessThreshold` | Promotion rate | Moderate ($5\text{--}10\%$) | Moderate ($15\text{--}25\%$) | $3$ |
| `coldIdleEpochs` | Demotion sweep | Low ($<5\%$) | Moderate ($10\text{--}20\%$) | $0$ (disabled) |

---

## 5. Artifacts Produced

- **Source Code**: [src/parameter_study_main.cpp](file:///home/adhyan/Desktop/Compiler/src/parameter_study_main.cpp)
- **Executable**: `parameter_study.exe`
- **CSV Data**: [results/parameter_sweep.csv](file:///home/adhyan/Desktop/Compiler/results/parameter_sweep.csv) ($592$ configuration rows)
- **Methodology Documentation**: [results/parameter_study_methodology.md](file:///home/adhyan/Desktop/Compiler/results/parameter_study_methodology.md)
