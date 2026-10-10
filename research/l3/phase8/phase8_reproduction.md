# Phase 8: Reproduction Guide

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`

This guide details how to verify the integration incompatibility identified in Phase 8.

## 1. Environment Setup
To reproduce the source analysis of Tiny C Compiler (TCC):
```bash
# Clone the TCC repository
git clone https://github.com/TinyCC/tinycc.git research/l3/phase8/tinycc
```

## 2. Verifying the Lexer/Parser Decoupling
Execute the following searches in the TCC source tree to verify that scoped symbol resolution operates on integer tokens, rendering string-keyed SSO hash tables obsolete for this specific compiler stage:

```bash
# 1. Verify that tok_alloc interns strings into integer `tok` IDs:
grep -A 20 "tok_alloc(" research/l3/phase8/tinycc/tccpp.c

# 2. Verify that the `Sym` struct uses the integer `v` (not a string):
grep -A 10 "struct Sym {" research/l3/phase8/tinycc/tcc.h

# 3. Verify that sym_push operates on the integer token `v`, not a string array:
grep -A 5 -B 5 "sym_push" research/l3/phase8/tinycc/tccgen.c
```

These searches confirm that integrating METIS-X into TCC would force a semantic regression (un-interning tokens back to strings), preventing a fair end-to-end performance comparison.
