# Research Gap

## What already exists

Three established techniques for managing identifier storage in a symbol table:

1. **Conventional hash table** (`std::unordered_map<std::string, Symbol>`) --
   simple, universal, but pays a full string copy per symbol, every time,
   even for identifiers declared hundreds of times across a codebase.
2. **String interning** -- dedupe identical identifier strings into a shared,
   refcounted pool; symbols hold a small index instead of a copy. Standard
   practice in production compilers (e.g. how many compiler front-ends
   canonicalize identifiers before symbol-table insertion).
3. **Compressed / trie-based dictionaries** -- front-coding, tries, and
   similar prefix-sharing structures reduce the bytes needed to store a large
   set of strings with shared prefixes, at some lookup-time cost to
   reconstruct or traverse.

Each of these is a **static** choice: a symbol table built on interning
interns everything; one built on a trie compresses everything. The
representation is a property of the *table's design*, decided once, at
compile-time of the compiler itself -- not of the *running program's current
conditions*.

## The gap

None of the three, on their own, make a **per-symbol representation decision
using live signals from the compilation environment**:

- How much of the configured memory budget is already spent (memory
  pressure)?
- Is this identifier short-lived (about to leave scope) or long-lived?
- Has this exact identifier been declared before (exact repeat)?
- Does it share a long prefix with whatever was just declared (compressible
  as part of a related group)?
- Once compiled, how often is it actually being looked up (worth paying for
  faster access)?

A production compiler for a resource-constrained target (embedded /
memory-bounded compilation, the stated scope here) has to make trade-offs a
desktop-scale compiler doesn't: it cannot always afford the peak memory a
conventional table would need for a large translation unit, but it also
cannot uniformly pay compression's lookup-time cost for every symbol,
including the ones on the hot path.

## What this project investigates

**Can a symbol table that adapts its per-symbol representation to memory
pressure, scope/lifetime, and observed access frequency measurably beat both
a conventional table and a plain interning table on the same workload, on
both memory and lookup latency -- not just one or the other?**

This is an empirical question, answered with real measurements
(`results/benchmark_results.csv`, `results/ablation_results.csv`), not a
theoretical claim. See `docs/experiment_plan.md` for how it was tested and
`docs/methodology.md` for exactly what "Tracked Memory" means.
