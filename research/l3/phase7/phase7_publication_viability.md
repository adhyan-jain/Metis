# Phase 7: Publication Viability Gate

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`

## 1. Selected Framing
**Framing B:** An empirical systems study of memory/latency trade-offs in compiler-oriented identifier tables.

## 2. Why This Framing Is Defensible
METIS-X is not a novel algorithm. Framings implying algorithmic novelty (A) or negative-result predominance (D) misrepresent the central finding. The value of this research lies in its rigorous empirical demonstration: combining existing techniques (Robin Hood hashing, backward-shift unwinding, SSO, and scope-array index backlinks) yields a system uniquely suited to the exact macro and variable distribution of embedded compiler AST workloads. The paper demonstrates *why* and *how much* these combined factors outperform both conventional hash chains (LLVM) and modern monotonic arenas (`SwissTable` style) by eliminating 75% of peak memory while keeping $p_{95}$ lookup latency inside L1-fetch boundaries.

## 3. Explicit Contribution Assessment
1. **What would a systems researcher learn?** That applying flat Robin Hood displacement directly to lexical LIFO scope unwinding enables $O(1)$ tombstone-free memory reclamation, entirely avoiding the monotonic memory bloat of standard string arenas without suffering pointer-chasing overhead.
2. **Strongest original observation:** Standard arena-backed hash tables spike peak heap by $+20-35\%$ (due to geometric reallocation and delayed truncation), proving that standard `ptmalloc` heap-fallback is strictly optimal for compiler AST string tracking when paired with a 12-byte inline SSO.
3. **Non-obvious finding:** The $+124\%$ latency penalty introduced by arena indirection (Candidate F), compared to the $+0\%$ penalty of a 12-byte SSO + Heap Fallback architecture.
4. **Workload representation:** Highly representative for embedded compilation (Zephyr, ESP-IDF).
5. **Trace vs. End-to-End:** The evaluation establishes *trace-replay performance*, not end-to-end compilation wall-clock speedups. This is the paper's primary limitation.
6. **Baselines:** The control is compared against rigorous ablations of its own architecture, demonstrating precisely where the latency and memory improvements originate.
7. **Limiting missing experiment:** An end-to-end integration into Clang or GCC showing wall-clock compilation time reductions.
8. **Realistic Publication Venue:** Due to the lack of end-to-end integration, this is a strong candidate for a Systems or Software Engineering track (e.g., CGO, ISMM, or PLDI as a short empirical paper / Experience Report), but it will likely face resistance at SOSP/OSDI without a fully integrated compiler artifact.

## 4. Highest-Value Missing Experiment (If Rejection Occurs)
Integrating METIS-X as a drop-in replacement for Clang's `IdentifierResolver` to measure exact end-to-end wall-clock parsing speedups on a massive `#include` payload. However, the current trace-based evidence is sufficient for a targeted empirical systems/data-structure publication.
