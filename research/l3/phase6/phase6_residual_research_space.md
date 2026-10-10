# Phase 6: Residual Storage & Layout Space

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`

This document assesses the remaining uninvestigated algorithmic avenues.

## 1. Separate Compact Metadata (SIMD Grouped Probing)
**Concept:** Extracting metadata (e.g., hash fingerprints, `scopeId`) into a parallel dense array (like `SwissTable`'s 1-byte control bytes) to enable 16-way SIMD lookup.
**Assessment:** `SwissTable` and `Folly F14` exhaust this research space. While SIMD speeds up flat associative arrays, integrating hierarchical LIFO scope masking (e.g., finding the highest `scopeId` in a SIMD register match) is an established engineering exercise. Furthermore, separating metadata breaks the 32-byte dense layout containing the SSO buffer, forcing a dual-lookup (metadata array -> payload array) which degrades latency for the $90\%$ of strings that currently benefit from unified cache locality.

## 2. Segmented / Chunked Arenas
**Concept:** Replacing `std::vector<char>` (Candidate F) with a chunked `std::deque`-like arena or BumpPtrAllocator to avoid the $+20\%$ geometric reallocation capacity spikes.
**Assessment:** LLVM already uses `BumpPtrAllocator` for symbol arenas. Implementing a chunked arena to mitigate vector scaling is standard systems programming, not novel computer science. `ptmalloc` (the current METIS-X baseline) is already an extremely robust, production-hardened chunked arena manager. Attempting to outperform `ptmalloc` with a bespoke segmented arena offers no theoretical algorithmic advancement.

## 3. String Interning (Deduplication)
**Concept:** Hash-consing or interning all identifiers to eliminate memory redundancy for highly reused names.
**Assessment:** The trace profiles reveal that $73.3\%$ of embedded compiler declarations are unique. Deduplication requires a secondary global hash table, whose synchronization and probe costs dwarf the memory saved by deduplicating the remaining $26.7\%$.

## Conclusion
The residual research space consists entirely of well-documented engineering trade-offs (SIMD vs unified memory, custom allocators vs `ptmalloc`). No theoretically novel, unexamined algorithms remain for this specific domain.
