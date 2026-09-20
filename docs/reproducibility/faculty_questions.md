# Anticipated Faculty Questions

## 1. "What exactly is novel?"

Not any single representation (interning, front-coding, and scope stacks are
all established). The proposed contribution is the **unified policy** that
picks a representation per symbol from a combination of memory pressure,
scope/lifetime, exact-repeat status, and prefix similarity to the previous
declaration -- and **adapts that choice after insertion** based on real
access counts (COMPRESSED -> INTERNED promotion once "hot"). See
`docs/novelty.md` for the precise claim and what it excludes.

## 2. "Why can't you just use a trie?"

A trie compresses shared prefixes uniformly for every identifier stored in
it -- it doesn't ask "is this identifier worth compressing right now, given
current memory pressure and how often it's likely to be accessed?" A trie
also doesn't natively express "un-compress this specific entry because it
turned out to be hot" without restructuring the whole trie. BUDGET-SYM's
COMPRESSED representation is a lighter-weight, per-entry front-coding chain
specifically because it needs to coexist with two other representations and
be promotable out of at runtime -- a full trie's benefit (arbitrary
prefix/range queries) isn't needed here, since exact-match lookup is the only
operation a symbol table needs.

## 3. "Why not use string interning?"

We measured it directly (`InternedSymbolTable` is one of the two baselines,
not a straw man). Across all 8 benchmark datasets, plain interning only
reaches ~1.05x-1.07x tracked memory versus the conventional baseline
(`results/benchmark_results.csv`) -- because interning only saves memory on
*exact repeats*, and most identifiers in a typical translation unit are
declared once. BudgetSym reaches 1.24x-2.37x on the same datasets by also
exploiting prefix similarity and memory pressure, not just exact repeats. The
ablation study's `BudgetSym-NoAdaptiveSelection` variant (forces every symbol
to INTERNED_REP, i.e. literally collapses BudgetSym into "just intern
everything") uses 50,642 tracked bytes on the ablation workload versus
`BudgetSym-Full`'s 33,581 bytes -- a direct, controlled measurement of what
adaptive selection buys over plain interning on identical data.

## 4. "How are you measuring memory?"

Explicitly *not* physical RSS. "Tracked Memory" is a documented, deterministic
sum of per-entry costs (string bytes actually stored + fixed metadata
constants), computed the same consistent way for all three implementations,
so the *relative* comparison is meaningful even though the *absolute* numbers
are a cost model, not an instrumented allocator measurement. Full breakdown
in `docs/methodology.md`. We say this unprompted rather than waiting to be
asked, because presenting a modeled number as if it were measured RSS would
be exactly the kind of overclaim this project's own rules forbid.

## 5. "Does compression hurt lookup?"

Yes, measurably. `results/benchmark_results.csv`'s `lookup_success_us` column
shows BudgetSym consistently higher than both baselines (e.g. `medium`:
Conventional 0.002us, Interned 0.0014us, BudgetSym 0.0022us per lookup) --
the direct cost of the hash-then-reconstruct lookup path every BudgetSym
entry uses (see `docs/architecture.md`, "Why the lookup index cannot be keyed
by the identifier string itself"), which is strictly more work for a
COMPRESSED entry (walk the decode chain) than an INTERNED or INLINE one
(one dereference). This is exactly why the promotion mechanic exists: an
entry that turns out to be accessed often gets moved off the slower path.

## 6. "Why is this relevant to embedded compilers?"

Embedded/memory-bounded compilation is a case where a compiler cannot always
assume desktop-scale memory for its own internal structures (the symbol table
included), but also cannot uniformly pay a compression scheme's lookup cost
for identifiers on a hot path (e.g. inside a tight interrupt handler being
compiled). A budget-aware, per-symbol adaptive policy is a direct response to
that specific constraint combination -- neither "always fast, sometimes
memory-heavy" (conventional) nor "always compact, always slower" (a uniform
compression scheme) fits it well on its own.

## 7. "How will you prove this is better?"

We don't claim proof -- we report a measured result, honestly, including
where it's *not* strictly better (insert latency, lookup latency). "Better"
here means: on 8 varied synthetic workloads, BudgetSym uses less tracked
memory than both baselines in every case, at a measured latency cost, and the
ablation isolates which mechanism contributes how much of that. Turning this
into a stronger, statistically-backed proof is the concrete next step in
`docs/experiment_plan.md` (multi-seed runs + significance testing + real
compiler-traffic datasets).

## 8. "What is your next step?"

In priority order (detailed in `docs/experiment_plan.md` and
`docs/future_work.md`): (1) multi-seed statistical testing instead of
single-run numbers, (2) feeding it identifiers from a real embedded
codebase instead of synthetic datasets, (3) fixing COMPRESSED's
chain-interior reclamation limitation, (4) a threshold sweep over
`PolicyConfig` instead of hand-picked defaults.
