# Phase 9: No-Go Analysis

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`

## Systems Investigated
1. **Tiny C Compiler (TCC) & LLVM/Clang:** Rejected due to decoupling of lexing (string interning) and parsing (scope resolution).
2. **CPython 3.12:** Rejected due to isolated per-block symbol dictionaries and global string interning.
3. **Lua 5.4:** Rejected due to string interning and the superiority of $O(N)$ linear backwards scanning for strictly bounded local scopes (max 200).
4. **Template Engines:** Rejected due to lack of materiality (identifier resolution is not the bottleneck in I/O and concatenation-heavy template rendering).

## The Root Cause of Failure
The problem is not a semantic mismatch; it is an architectural anti-pattern.
METIS-X is a brilliant optimization for a flawed architecture. It provides a pareto-optimal solution for *concurrent string-interning and scoped symbol resolution*. However, decades of compiler and interpreter engineering have proven that these two operations must be decoupled for performance. Once decoupled, the need for a scoped string-hashing data structure vanishes entirely.

## Impact on the Systems Paper
The current Phase 7 manuscript claims that METIS-X is a "compact scope-aware symbol table" suitable for compilers. This claim is fundamentally indefensible in a peer-reviewed systems context because no production compiler would utilize raw-string scoped hashing. 
The paper must be narrowed to state: "METIS-X optimizes single-pass raw-string environments." However, finding a production environment where this represents a material bottleneck has proven impossible.

## Conclusion
Further engineering work has **negative expected research value**. We cannot manufacture a real-world use case for an architecture that solves a problem production systems have already architected out of existence.
