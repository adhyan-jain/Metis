# Phase 8: Integration Target Selection

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`

## Candidate A: LLVM / Clang
- **Semantic Compatibility:** High complexity. Clang separates `IdentifierTable` (which manages string interning and token identity) from `IdentifierResolver` (which manages lexical scoped name lookup). `IdentifierResolver` uses a chained linked list stored inside the `IdentifierInfo` object (the `IdDeclInfo` struct).
- **Integration Complexity:** Extreme. Replacing the chained `IdentifierResolver` with a flat open-addressing table like METIS-X would require overhauling Clang's core AST name lookup semantics, pointer lifetimes, and memory allocator boundaries (`BumpPtrAllocator`).
- **Resource Requirements:** Building Clang from source requires 20-30+ GB of disk space and >30 minutes to compile per run on standard CI hardware.

## Candidate B: Tiny C Compiler (TCC)
- **Semantic Compatibility:** High. TCC manages symbols in a global hash table `sym_hash` (an array of `Sym*` linked lists). Lexical scopes are handled by pushing symbols onto a local scope linked list, and upon scope exit, walking the local list to remove symbols from the global `sym_hash`.
- **Integration Complexity:** Moderate. The C API and struct-based `Sym` token representation in TCC (`tcc.h`) must be adapted to interface with the C++ `METIS-X` table. We must map TCC's integer token hashes to `METIS-X`'s string-based interface or adapt METIS-X to hash TCC tokens.
- **Resource Requirements:** Minimal. TCC is <5 MB of source code and compiles in under 5 seconds, enabling rapid, controlled $A/B$ end-to-end benchmarking.

## Target Selection
**Tiny C Compiler (TCC)** is selected as the primary integration target. 
*Reasoning:* Clang's build time and integration surface are infeasible for a rapid empirical proof-of-concept. TCC provides a semantically faithful, highly observable $C$ compiler environment where the identifier resolution hot-path can be swapped with METIS-X to measure end-to-end wall-clock speedup and peak memory without polluting the metric with massive unrelated C++ frontend overhead.
