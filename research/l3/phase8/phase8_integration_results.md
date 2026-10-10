# Phase 8: Integration Results & End-to-End Validation

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`

## 1. The Integration Hypothesis
Phase 8 sought to integrate METIS-X into a real compiler (TCC) to measure end-to-end wall-clock speedup and peak memory improvement during an actual compilation, moving beyond synthetic AST trace replays.

## 2. Experimental Execution
TCC (Tiny C Compiler) was successfully cloned and analyzed for semantic integration (`sym_push`, `tok_alloc`, and `Sym` lifetimes). 

## 3. Integration Failure & The Architectural Flaw
The integration was **aborted due to a fatal semantic incompatibility** that reveals a fundamental flaw in the METIS-X architectural assumptions.

METIS-X assumes that a scoped symbol table must store, hash, and match raw string identifiers. Its entire pareto-optimal trade-off (the 12-byte SSO replacing pointer indirection) is predicated on optimizing string-key associative lookups.

However, real compilers (including TCC and Clang) do not use strings for scoped symbol resolution:
1. **String Interning:** In TCC, the lexer interns strings exactly once via `tok_alloc(str, len)`, generating a globally unique integer `tok` ID (e.g., `v = 4012`).
2. **Symbol Resolution:** The parser and AST use only the integer `v`. Scope shadowing is managed by pushing to an integer-indexed array (`table_ident[v]`) and saving the previous pointer. 

**The Blocker:** If METIS-X were integrated into TCC's `sym_push`, we would have to abandon the interned integer `v` and pass the raw string into METIS-X, forcing METIS-X to re-hash the string and execute character-by-character string comparisons on every AST node traversal. This would intrinsically degrade TCC's performance because TCC's native lookup is a $O(1)$ integer array index (`table[v]`), requiring zero hashing or string comparison.

## 4. Conclusion
METIS-X solves a combined string-interning and scope-resolution problem. But in real-world compiler engineering, those two concerns are decoupled. The 12-byte SSO provides massive gains in a trace-replay where raw strings are resolved dynamically, but in a real compiler, those strings are already reduced to integer IDs before scope resolution occurs.

Therefore, METIS-X cannot provide an end-to-end wall-clock speedup in a standard compiler architecture without deliberately handicapping the host compiler's lexer. The Phase 7 systems paper is strictly limited to domains where string-interning and scope-resolution must occur concurrently (e.g., dynamic interpreters, JIT parsers, or single-pass log processors), rather than static C/C++ compilation.
