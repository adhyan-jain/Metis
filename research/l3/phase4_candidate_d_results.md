# Phase 4 Candidate D Results: Arena Offsets vs. Heap SSO

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`
**Phase:** Phase 4 (Candidate D Controlled Experiment)
**Deliverable:** `research/l3/phase4_candidate_d_results.md`

## 1. Experimental Summary

A controlled, side-by-side benchmark was executed comparing the baseline `METIS-X` (12-byte inline strings + heap overflow) against `Candidate D` (4-byte inline prefix + 32-bit arena offset). The test was performed on the canonical ESP-IDF and Zephyr traces, measuring $p_{50}$/$p_{95}$ lookup latency, peak physical heap (`malloc_usable_size`), and mutative insertions.

## 2. Results Table

*Results are the median of 5 repetitions per corpus on core-pinned Linux (`taskset -c 0`).*

### Workload: ESP-IDF ($2.42\text{M}$ events)
| Metric | Control (METIS-X) | Candidate D (Prefix + Arena) | Delta | Outcome |
| :--- | :--- | :--- | :--- | :--- |
| **Peak Heap** | $40.56\text{ MB}$ | $55.16\text{ MB}$ | $+35.9\%$ | **FAIL** (Increased) |
| **Final Heap** | $31.64\text{ MB}$ | $46.24\text{ MB}$ | $+46.1\%$ | **FAIL** (Increased) |
| **$p_{95}$ Lookup Latency** | $0.067\mu\text{s}$ | $0.106\mu\text{s}$ | $+58.2\%$ | **FAIL** (Slower) |
| **$p_{50}$ Lookup Latency** | $0.037\mu\text{s}$ | $0.051\mu\text{s}$ | $+37.8\%$ | **FAIL** (Slower) |
| **Insert Time (total)** | $90.53\text{ ms}$ | $115.89\text{ ms}$ | $+27.9\%$ | **FAIL** (Slower) |
| **Total Allocations** | $629,518$ | $142,226$ | $-77.4\%$ | **PASS** (Fewer allocs) |

### Workload: Zephyr RTOS ($2.65\text{M}$ events)
| Metric | Control (METIS-X) | Candidate D (Prefix + Arena) | Delta | Outcome |
| :--- | :--- | :--- | :--- | :--- |
| **Peak Heap** | $37.76\text{ MB}$ | $50.20\text{ MB}$ | $+32.9\%$ | **FAIL** (Increased) |
| **$p_{95}$ Lookup Latency** | $0.099\mu\text{s}$ | $0.222\mu\text{s}$ | $+124.2\%$ | **FAIL** (Slower) |
| **Total Allocations** | $664,326$ | $166,696$ | $-74.9\%$ | **PASS** (Fewer allocs) |

## 3. Analysis & Interpretation

Candidate D conclusively failed all primary decision thresholds. 

### Why did Peak Memory Increase? (The Lifetime Confounder)
While Candidate D eliminated glibc chunk overhead (reducing allocation calls by $\sim 75\%$), it suffered from the **Monotonic Arena Accumulation** problem. `METIS-X` natively reclaims heap strings when `exitScope()` pops a symbol. A contiguous `std::vector` arena *cannot* safely erase transient strings without shifting subsequent data and invalidating all $O(1)$ offsets. As a result, Candidate D accumulated every transient local variable ever declared throughout the compilation trace, bloating peak memory by $\sim35\%$ over the Control.

### Why did Lookup Latency Regress? (The Indirection Wall)
Candidate D increased $p_{95}$ lookup latency by $58\%$ on ESP-IDF and $124\%$ on Zephyr. 
- The 4-byte prefix was insufficient to fully contain lookups.
- Fetching the remaining string characters required a dependent memory fetch into a massive (up to $46\text{ MB}$) arena.
- Because transient dead strings were interspersed with live global strings, the arena working set was highly fragmented, causing severe L2/L3 cache misses compared to `METIS-X`'s L1-resident 12-byte inline strings.

## 4. Final Decision: NO-GO

The experiment is a strictly falsifiable negative result. **Reject Candidate D.**

The baseline `METIS-X` (32-byte slots with 12-byte SSO + Heap fallback) represents a mathematically superior pareto optimal frontier for embedded compilers: the heap overhead of global macros is more than offset by the immediate reclamation of transient local variables, and the 12-byte SSO keeps $p_{95}$ lookup latencies tightly bounded in the L1 cache.
