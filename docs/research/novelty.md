# Novelty Positioning

We do not claim this is the first adaptive data structure, the first use of
front-coding, or the first symbol table with scope management. All of those
are established techniques (see `docs/research_gap.md`). We make a narrower,
specific claim:

> **Proposed adaptive architecture**: a symbol table that selects, per
> symbol and at both insertion time *and* dynamically afterward, between
> INLINE / INTERNED / COMPRESSED representations, driven by a single unified
> policy that combines memory-budget pressure, scope/lifetime, and observed
> access frequency -- and that we built, benchmarked, and ablated it, with
> the honest result reported either way.

## What is actually new here (the proposed contribution)

1. **Unified policy input, not a single-signal heuristic.** Existing
   adaptive-string work usually adapts on one axis (e.g. "intern only if
   seen before", or "compress if the dictionary is large"). BUDGET-SYM's
   `decide()` (see `include/budget_sym.hpp`) combines four signals in one
   decision: exact-repeat status, current memory pressure, prefix similarity
   to the previous declaration, and an access-frequency hint.
2. **Post-insertion adaptivity, not just a one-shot choice.** Most
   discussions of "adaptive" symbol representation stop at insert time. Here,
   `lookup()`/`recordAccess()` can promote a COMPRESSED entry to INTERNED once
   it crosses `hotAccessThreshold` real accesses -- a runtime correction to a
   decision made with only insert-time information. Measured in
   `results/ablation_results.csv`: 4 promotions actually fired on the
   ablation's fixed workload for `BudgetSym-Full`, and 0 for
   `BudgetSym-NoAccessFrequency` (the variant with promotion disabled by
   config) -- the mechanism is demonstrated running, not asserted.
3. **Scope-aware memory accounting as a first-class operation.** `enterScope()`
   / `exitScope()` return an explicit `{symbolsReleased, bytesReclaimed}`
   report; every one of the three representations has documented, non-trivial
   reclamation semantics -- including the honest limitation that a
   chain-interior COMPRESSED node cannot always be physically freed
   immediately (see `docs/future_work.md`).

## What is explicitly NOT novel (built on existing techniques)

- String interning itself (`InternedSymbolTable`, and the INTERNED
  representation inside BudgetSym) is the standard technique.
- Front-coding / prefix-shared string storage (the COMPRESSED representation)
  is a known technique from dictionary compression literature, not invented
  here.
- Stack-of-scopes symbol table structure is standard compiler-construction
  practice.

## Falsifiable claim, checked against real data

The claim "BUDGET-SYM beats both baselines on memory, on realistic
workloads" is checked in `results/benchmark_results.csv` across 8 datasets:
BudgetSym's `compression_ratio` column (bytes-vs-Conventional) ranges from
**1.24x** (`random-identifiers`, the least favorable case -- short, unrelated
identifiers) to **2.37x** (`high-prefix-similarity`), and beats
`InternedSymbolTable` (which itself only reaches ~1.05-1.07x) on every single
dataset. The honest cost: BudgetSym's `insert_us` is consistently the
slowest of the three (more decision logic per insert), and its lookup is
somewhat slower than the two baselines due to the hash-then-reconstruct
lookup path COMPRESSED and INTERNED entries require (see
`docs/methodology.md`). This is a memory-for-latency trade, not a strict
improvement on every axis -- and we say so explicitly rather than only
reporting the number that favors the proposed design.
