# Phase 8: Experiment Plan

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`

## 1. Baseline Semantics & Target (TCC)
TCC (Tiny C Compiler) manages identifiers by splitting the problem into two distinct phases:
1. **String Interning (Lexer):** Raw strings are interned into integer Token IDs (`v`) via `tok_alloc`.
2. **Symbol Resolution (Parser):** Scoped declarations operate exclusively on Token IDs. The active symbol is stored in a direct array lookup (`sym_hash[v]`). Shadowing is handled by storing a `prev_tok` pointer inside the new `Sym` struct, and restoring it upon scope exit.

## 2. Proposed Intervention
To integrate METIS-X, we would replace `sym_hash` and the `sym_push`/`sym_pop` functions with `MetisXTable`.
- **Control:** TCC's native `sym_hash` (integer-keyed pointer chaining).
- **Treatment:** `MetisXTable` (string-keyed Robin Hood open addressing with 12-byte SSO).

## 3. Integration Hazards & Hypotheses
- **H1 (Correctness):** METIS-X can preserve TCC's shadowing and visibility rules.
- **H2 (Architecture Mismatch):** METIS-X expects raw strings to utilize its 12-byte SSO. Because TCC passes integer Token IDs to the symbol resolver, we must either:
  a) Pass the string back into the symbol resolver (forcing METIS-X to re-hash and store strings that are already interned).
  b) Modify METIS-X to hash integers instead of strings (which immediately renders the 12-byte SSO—METIS-X's primary memory/latency advantage—useless).

## 4. Decision Criteria
Before executing the integration, we must inspect TCC's source to verify whether the lexer strictly decoupling string-interning from scope-resolution prevents METIS-X from functioning as designed. If METIS-X's core architectural assumption (that scoped symbol tables must store string identifiers) contradicts standard compiler architecture, the integration must be aborted and the incompatibility reported.
