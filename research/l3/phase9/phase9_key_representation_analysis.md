# Phase 9: Key Representation Analysis

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`

The METIS-X architecture heavily relies on the 12-byte SSO (Small-String Optimization) to eliminate heap allocations for transient identifiers. We evaluated whether changing the key representation to match production runtimes is viable.

## Option 1: Raw-String Keys
- **Mechanism:** The current METIS-X implementation.
- **Viability:** Only applicable to systems that do not intern strings (e.g., naive stream parsers or unoptimized template engines). Production compilers universally reject this architecture because repeatedly hashing and comparing raw strings during millions of AST traversals is prohibitively slow.

## Option 2: Stable Interned-Name Keys (Pointers)
- **Mechanism:** Replacing the 12-byte SSO buffer with an 8-byte pointer to a globally interned string (e.g., `PyUnicodeObject*` or `TString*`).
- **Viability:** This breaks the core pareto-optimal claim of METIS-X. If strings are interned, the allocation has *already occurred* in the intern pool. The memory-reclamation advantage of METIS-X (avoiding allocations for transient scoped variables) is destroyed, as the string must be allocated in the global intern table anyway. Furthermore, lookup collapses to pointer-equality, meaning simple linear scans (Lua) or integer indexing (TCC) easily outperform Robin Hood hashing.

## Option 3: Integer Symbol IDs
- **Mechanism:** Replacing the key with an integer token ID (like TCC's `v`).
- **Viability:** Redundant. If the key is a dense integer, the optimal data structure is a flat array (`Sym* table[MAX_TOKENS]`), providing guaranteed $O(1)$ lookup with zero hashing overhead.

## Conclusion
The 12-byte SSO is paradoxically the source of METIS-X's benchmark superiority and its real-world uselessness. It optimizes a raw-string lookup path that production systems deliberately bypass through interning or tokenization. Adapting METIS-X to use interned pointers or IDs destroys the architecture's foundational premise.
