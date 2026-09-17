# Phase 3 — Interning Sanity Check

## A/B/C/D distinction, mapped to the actual implementation

| Category | Definition | Does `InternedSymbolTable` implement this? |
|---|---|---|
| A. Symbol-table key duplication | Same identifier text used as a hash-map key in multiple *disjoint* scope maps (each scope's own map, no sharing) | No — this is what Conventional already handles fine via SSO; Interned doesn't specifically target this. |
| B. Independently allocated string duplication | Each occurrence heap-allocates its own copy of a string even when identical to another live copy | Partially — Interned prevents this *only* for names that don't fit in SSO (>15B); for SSO-range names, Conventional never allocated a duplicate copy to begin with, so there's nothing for Interned to save. |
| C. Repeated semantic identifiers | The same logical name (e.g. `i`, `self`, `len`) reappears across many, possibly non-adjacent, scopes over the symbol table's lifetime | Yes, in the trivial sense of the shared pool, but the pool is *global*, not partitioned — it doesn't distinguish "the same name reused because it's semantically common" from "the same name reused because it's the same logical entity," which matters for correctness of the interning decision below. |
| D. Intern-pool sharing across scopes/TUs | A single pool table shared by construction across the entire compilation unit (or across TUs), independent of the live scope stack | **Yes — this is exactly and unconditionally what `InternedSymbolTable` does.** `pool_`/`poolLookup_`/`poolRefCount_` are single global structures; scope entries only ever store a 4-byte pool index. |

**The implementation only does D.** It is architecturally a global,
scope-agnostic string pool, applied uniformly to every symbol regardless of
whether that symbol's duplication is case A, B, or C. This is the right
design for a workload dominated by long, highly-duplicated, long-lived
strings shared across many independent producers (e.g. a distributed system
interning RPC field names). It is *not* obviously the right design for a
compiler symbol table, where:
- most identifiers are short (SSO-range; A/B don't cost anything in
  Conventional to begin with — see `docs/ordering_analysis.md`),
- duplication is dominated by C (the same common short name reused across
  scopes), which Conventional's per-scope map already handles at near-zero
  marginal memory cost,
- and D's fixed cost (doubled pool storage + never-compacted growth) is paid
  unconditionally on every symbol, whether or not that symbol's duplication
  pattern would actually benefit from a shared pool.

## Break-even condition (restated in interning-specific terms)

Using the notation from `docs/ordering_analysis.md` (`L` = name length,
`k` = live duplicate occurrences of one unique name):

```
k_breakeven(L) = (2L + 86) / (L + 13)      for L > 15
                 no solution                for L <= 15
```

In words: interning only pays for itself when (a) the name is long enough to
force Conventional into a heap allocation (`L > 15`), **and** (b) that name
is live-duplicated at least ~2–4 times simultaneously in the compilation.
Neither condition is about "the identifier appears many times in the source
text" (raw repeat_rate) — it is specifically about how many *independently
represented, currently-live* copies of that name Conventional would
otherwise be holding. A name declared once and looked up 1000 times costs
Conventional nothing extra (`k` here is occurrence *count*, not access
*count* — repeated reads of the same live symbol don't multiply storage in
either representation).

## Check against real corpus data

None of the 20 real corpora satisfy `L > 15` for enough of their identifier
mass to matter: `frac≤15B` (fraction of identifiers ≤15 bytes) ranges
0.59–0.76 across all corpora in `data/real_world_corpus_characterization.csv`,
meaning 60–76% of every corpus's identifiers are in the SSO range where no
break-even exists at all, by construction. The measured
`Interned/Conventional bytes-per-unique-symbol` delta is positive (a loss for
Interned) on **20/20** corpora, ranging **+21.4% (Nginx)** to **+86.1%
(LLVM)** — see the table in `docs/ordering_analysis.md`.

**This is not a tuning failure or an unlucky corpus selection.** It is the
direct, correctly-predicted consequence of applying a "D-only" (global,
duplication-agnostic) interning strategy to a workload whose duplication is
structurally dominated by C (short, common, semantically-repeated names) —
exactly the case the break-even inequality above shows interning cannot win
on.

## Verdict

The current `InternedSymbolTable` baseline is a **conceptually reasonable
comparison point but a structurally disadvantaged one for compiler symbol
tables specifically**, and that disadvantage is not an implementation bug —
it is the expected outcome of unconditional global pooling applied to a
short-identifier-dominated workload. This should be stated plainly in the
paper (see `docs/final_research_audit.md`) rather than treated as a surprise
or a flaw to "fix" by artificially handicapping Conventional or inflating
Interned. A scope/TU-partitioned or duplication-threshold-gated interning
strategy (interning only names that are both long and already known to be
locally duplicated) is the natural next comparison point if the project
wants a baseline that could plausibly win under some regime — that is
exactly the "global/shared string arena" and "eliminate per-symbol metadata
where unneeded" candidates listed in `docs/ordering_analysis.md`, not yet
implemented.
