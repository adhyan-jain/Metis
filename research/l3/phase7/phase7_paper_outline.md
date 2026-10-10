# Phase 7: Paper Outline

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`
**Target Venue:** Empirical Systems Track (e.g., CGO / ISMM)

## Title Idea
*METIS-X: Empirical Memory and Latency Trade-offs in Scope-Aware Compiler Symbol Tables*

## 1. Abstract
- **Context:** Compiler ASTs rely heavily on scoped symbol tables. Deep nesting and macro definitions create severe memory pressure and cache misses.
- **Problem:** Conventional chaining (LLVM) causes pointer-chasing, while flat arenas suffer from monotonic memory bloat or geometric capacity spikes.
- **Solution/Architecture:** METIS-X, a synthesis of Robin Hood hashing, 12-byte SSO, and bidirectional scope-frame backlinks.
- **Results:** Trace replay of ESP-IDF and Zephyr shows a 75% reduction in peak physical memory and $0.098\mu\text{s}$ $p_{95}$ latency, decisively rejecting custom arena storage for this domain.

## 2. Introduction
- The compiler identifier storage problem.
- Trade-off space: allocation overhead vs. string reclamation.
- Contributions: an architectural synthesis and a rigorous empirical ablation of alternate storage models.

## 3. Background & Related Work
- Open-Addressing & Robin Hood Hashing (Pedro Celis).
- Small-String Optimization (SSO).
- Prior Art: LLVM `ScopedHashTable` (chained) and Abseil `SwissTable` (flat, no native scope unwinding).
- Delineating our focus: the intersection of LIFO lexical lifetimes and flat associative arrays.

## 4. The METIS-X Architecture
- **Data Structure:** 32-byte slot layout, omitting alignment claims but emphasizing density.
- **The Bidirectional Link:** The `frameIndex` enabling $O(1)$ updates during displacement.
- **Unwinding:** Tombstone-free backward-shifting (noting $O(P)$ complexity).
- **Storage Policy:** 12-byte SSO with `ptmalloc` heap fallback.

## 5. Experimental Methodology
- **Traces:** Extraction from ESP-IDF, Zephyr, etc.
- **Harness:** Controlled replay, median of 5 reps, pinned CPUs.
- **Metrics:** $p_{95}$ latency and physical peak heap via `malloc_usable_size`.

## 6. Results & Analysis
- **Baseline Comparison:** METIS-X vs Embedded Conventional (LLVM-style). Show the 78% memory reduction and latency bounds.
- **The SSO Impact:** Ablation study comparing standard `std::string` Robin Hood vs. METIS-X 12-byte SSO.

## 7. Negative Results (Ablation of Alternatives)
- **Why Not Transactional Logs?** The failure of TS-TFDR due to Amdahl's Law.
- **Why Not String Arenas?** The failure of Candidate D (monotonic bloat) and Candidate F (geometric vector scaling), proving that standard `ptmalloc` is optimal for AST LIFO reclamation.

## 8. Threats to Validity
- **Trace Replay vs End-to-End:** Measured on AST event streams, not wall-clock compilation time.
- **Hardware Proxies:** L1 cache residency is inferred from payload size, lacking hardware counter confirmation.
- **Exception Safety:** Disclosing the `rehash()` RAII leak limitation.

## 9. Conclusion
- Reaffirming that METIS-X is an empirically justified combination of known techniques forming a highly optimized domain-specific architecture.
