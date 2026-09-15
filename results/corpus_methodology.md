# Real-World Corpus Event Extraction Methodology

This document details the semantic event extraction methodology, scope tracking, limitation analysis, and empirical characterization for the real-world corpus evaluation suite per **CLAUDE_RESEARCH.md Section 10**.

---

## 1. Overview & Research Motivation

Previous V1 corpus evaluation relied on simple identifier token streams (`extract_identifiers.py`), treating every token occurrence as an insertion into a single flattened global scope. As documented in Audit Finding **A3**:
- Plain token streams count prose-like keywords and repetitive identifiers without compiler semantic context.
- Dead entry accumulation was masked or exaggerated because symbols were never scoped or reclaimed.

The **Real-World Corpus Pipeline** (Section 10) fixes this by transforming C/C++ source trees into explicit, semantically valid compiler event streams:
1. `DECLARE(symbol, scope_depth)`
2. `USE(symbol, scope_depth)`
3. `ENTER_SCOPE(scope_depth)`
4. `EXIT_SCOPE(scope_depth)`

---

## 2. Extraction Pipeline Architecture (`scripts/extract_corpus_events.py`)

The extraction pipeline scans source trees (`.c`, `.h`, `.cpp`, `.hpp`) and executes four sequential parsing phases:

### Phase 1: Comment & Literal Filtering
- Single-line (`//...`) and multi-line (`/*...*/`) comments are stripped.
- String literals (`"..."`) and character literals (`'...'`) are removed.
- C/C++ language keywords (`if`, `for`, `while`, `return`, `int`, `void`, `struct`, etc.) are filtered out so syntax control words do not become symbol declarations.

### Phase 2: Preprocessor Line Handling
- Preprocessor lines (`#include`, `#if`, `#ifdef`, `#ifndef`, `#else`, `#endif`) are stripped to avoid preprocessor condition prose from contaminating the symbol stream.
- `#define MACRO_NAME ...` lines emit `DECLARE(MACRO_NAME, 0)` in global scope. Macro body tokens are evaluated against active symbol scope tables.

### Phase 3: Lexical Scope Stack Tracking
- Open braces (`{`) emit `ENTER_SCOPE` and push a new symbol tracking table onto the scope stack.
- Close braces (`}`) emit `EXIT_SCOPE` and pop the top scope table from the stack, reclaiming all symbols declared within that lexical scope.
- Scope depth $d=0$ represents the global translation unit scope.

### Phase 4: Declaration vs. Use Disambiguation
- **Declarations (`DECLARE`)**: Detected via syntactic C/C++ declaration patterns (e.g. type specifiers, storage qualifiers `static`/`extern`/`const`, parameter lists `(type name, ...)`).
  - *Same-scope redeclaration*: If `name` is already in the current scope set, a redeclaration count is recorded.
  - *Shadowing*: If `name` exists in an outer scope set, a shadowing event is recorded.
- **Uses (`USE`)**: If an identifier token matches an active, visible symbol in the current or any outer scope, it is emitted as `USE(name, depth)`.
- **First-occurrence Fallback**: If an identifier appears without prior outer declaration, its first occurrence emits `DECLARE(name, depth)` into the current scope so that no un-declared `USE` can ever occur.

---

## 3. Explicit Limitations & Defensible Approximations

Per CLAUDE_RESEARCH.md Section 10 ("implement the strongest defensible approximation and document limitations explicitly"), the following limitations of static header-less parsing are acknowledged:

1. **Unresolved Preprocessor Configuration Macros**: Standalone parsing of vendored source directories without generated build headers (e.g., missing `FreeRTOSConfig.h` or Zephyr Kconfig `autoconf.h`) means conditional compilation branches defaults to raw file order.
2. **Type Disambiguation Heuristics**: Custom typedefs not explicitly preceded by `typedef` keywords are recognized via forward symbol lookup; complex C++ template metaprogramming contexts default to identifier declaration patterns.
3. **Macro Expansion Bodies**: Macro expansions within function bodies are tokenized as statement expressions rather than expanded via full preprocessor AST.

---

## 4. Hand-Written Fixture Validation (`tests/fixtures/sample_corpus_fixture.c`)

To guarantee parser correctness, the extractor is validated against hand-written C test fixtures ([tests/fixtures/sample_corpus_fixture.c](file:///home/adhyan/Desktop/Compiler/tests/fixtures/sample_corpus_fixture.c)) covering all required language constructs:

- Global declarations (`extern int globalVar; int globalVar = 42;`)
- Local declarations (`int localVar1`, `int localVar2`)
- Function parameters (`int paramA`, `char paramB`)
- Nested blocks (`{ int localVar1 = 100; ... }`)
- Shadowing (inner `localVar1` shadowing outer `localVar1`)
- Same-scope redeclaration (extern vs definition)
- Repeated symbol uses (`paramA`, `globalVar`, `innerVar`)
- Scope exits (`innerVar` reclaimed on block exit)

The automated test runner ([tests/test_corpus_parser.py](file:///home/adhyan/Desktop/Compiler/tests/test_corpus_parser.py)) verifies that the extracted trace matches expected semantics.

---

## 5. Corpus Characterization Summary (`results/corpus_characterization.csv`)

| Metric | FreeRTOS | Arduino | Zephyr |
|---|---:|---:|---:|
| **Scanned Files** | 655 | 332 | 4,298 |
| **Declarations** | 72,376 | 31,846 | 703,727 |
| **Uses** | 123,602 | 50,273 | 1,523,992 |
| **Unique Symbols** | 10,386 | 11,000 | 228,739 |
| **Redeclarations** | 39,209 | 6,985 | 50,928 |
| **Shadowing Events** | 4,084 | 1,166 | 42,035 |
| **Max Scope Depth** | 10 | 11 | 21 |
| **Average Scope Depth** | 1.58 | 1.45 | 10.50 |
| **Mean Identifier Length** | 14.20 | 8.69 | 9.93 |
| **Prefix Similarity** | 0.1643 | 0.1362 | 0.0994 |
| **Repeat Rate** | 0.6307 | 0.6122 | 0.6841 |
| **Entropy (bits/symbol)** | 10.67 | 11.31 | 12.69 |
| **Access Skew (Gini)** | 0.7931 | 0.7182 | 0.8070 |
| **Scope Churn** | 0.0688 | 0.0641 | 0.0726 |

---

## 6. Real-World Semantic Benchmark Results (`results/corpus_benchmark.csv`)

Replaying full semantic event streams ($2.6\text{M}$ events for Zephyr) yields measured heap consumption:

- **Zephyr ($2.6\text{M}$ events)**:
  - `Conventional`: $47.76\text{ MB}$
  - `Interned`: $81.22\text{ MB}$
  - `BudgetSym` V1: $190.96\text{ MB}$ (append-only Entry bloat)
  - `SymTabV2`: $83.71\text{ MB}$ ($>2.28\times$ heap reduction vs V1)

- **FreeRTOS ($227\text{K}$ events)**:
  - `Conventional`: $3.74\text{ MB}$
  - `Interned`: $5.09\text{ MB}$
  - `BudgetSym` V1: $16.18\text{ MB}$
  - `SymTabV2`: $5.61\text{ MB}$ ($2.88\times$ heap reduction vs V1)
