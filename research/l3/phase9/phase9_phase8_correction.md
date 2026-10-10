# Phase 9: Phase 8 Generalization Correction

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`

## What Phase 8 Actually Proved
Phase 8 proved that the Tiny C Compiler (TCC) fundamentally decouples **identifier interning** from **lexical name resolution**. 
1. **Identifier Interning:** Occurs during the lexing phase (`tok_alloc`). Raw strings are hashed, matched against a global intern table (`hash_ident`), and assigned a canonical integer token ID (e.g., `v = 250`).
2. **Lexical Name Resolution:** Occurs during parsing (`sym_push` / `sym_pop`). The AST parser operates entirely on the integer `v`. Shadowing is managed via a direct $O(1)$ array lookup (`sym_hash[v]`), linking previous symbols via a `prev_tok` pointer.

## The Scope of the Limitation
The discovery from Phase 8 must not be mischaracterized as "TCC is weird." It is standard compiler architecture.
- METIS-X assumes that scoped symbol resolution and string matching are a **concurrent operation**.
- Real static compilers (TCC, Clang/LLVM) separate them. String-to-Token mapping is done once; Token-to-Declaration mapping is done millions of times, but exclusively using integers or pointers.
- Therefore, dropping METIS-X (a string-keyed hash table) into a compiler's scope-management phase requires reversing the compiler's architecture, forcing it to look up raw strings when it already has perfectly unique canonical IDs.

Phase 8 established that METIS-X is inapplicable as a drop-in replacement for the AST name-resolution phase of a tokenized static compiler. It does *not* prove METIS-X is useless for systems that dynamically resolve raw strings without a separate lexing/interning phase.
