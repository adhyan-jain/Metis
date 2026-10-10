# Phase 9: Target Matrix

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`

*(Note: Direct `git clone` of CPython and Lua failed due to network unreachability. Analysis is based on established structural knowledge of CPython 3.12 and Lua 5.4 architectures).*

## 1. CPython (Compile-time Symbol Analysis)
- **Actual use case:** `Python/symtable.c` builds a symbol table from the AST to determine variable scope (local, global, free, cell).
- **Key representation:** Interned string pointers (`PyUnicode_InternInPlace`).
- **Lookup structure:** Separate Python `dict` per scope block (`ste->ste_symbols`), not a monolithic flat array.
- **Scope model:** Hierarchical resolution happens by checking the current block's dict, then parent blocks, but locals are statically mapped to array indices (`FASTLOCAL`) for runtime.
- **Key comparison:** Pointer equality (interned strings).
- **METIS compatibility:** Incompatible. Python isolates scopes into distinct dictionaries. METIS-X assumes a single monolithic flat table with LIFO backward-shift unwinding.
- **Decision:** REJECT.

## 2. Lua (Local-name Resolution)
- **Actual use case:** `lparser.c` resolves local variable names during single-pass compilation.
- **Key representation:** Interned string pointers (`TString*`).
- **Lookup structure:** Linear array of active locals (`FuncState->f->locvars`).
- **Scope model:** `searchvar()` scans the active locals array strictly backwards to naturally find the most deeply nested shadowed variable first.
- **Key comparison:** Pointer equality.
- **METIS compatibility:** Incompatible/Counterproductive. Lua strictly limits local variables to 200 per function. A backward linear scan over an array of 200 contiguous pointers is exceptionally cache-friendly and fast. Introducing a string-hashing Robin Hood table would severely degrade Lua's parser performance.
- **Decision:** REJECT.

## 3. Template/Scripting Engines (e.g., Jinja/Inja C++ ports)
- **Actual use case:** Rendering templates with nested `{% set ... %}` block scopes.
- **Key representation:** Raw strings (`std::string`).
- **Lookup structure:** Chained `std::unordered_map` environments.
- **METIS compatibility:** Direct fit.
- **Expected advantage:** High. Eliminating chained map pointer-chasing and string allocations for short variables.
- **Major risk:** Materiality. Template rendering is typically I/O bound or string-concatenation bound, not identifier-resolution bound. The speedup to the total system would likely be negligible (under 1%).
- **Decision:** REJECT.
