# Future Work / Known Limitations

Stated explicitly rather than hidden, per this project's own rule of never
letting a chart or claim imply more than what was actually measured.

## 1. COMPRESSED chain-interior reclamation is incomplete

When a scope exits (or an entry is promoted), only the **chain tail** (the
most recently inserted COMPRESSED entry, with no descendants yet) can be
physically reclaimed from `compPool_`. An interior node -- one that a later
COMPRESSED entry's `prevIndex` still points to for decoding -- is only
*logically* dropped (`tombstoned = true`, excluded from lookup) but its bytes
stay allocated in the vector until the next re-anchor point walks past it.
This means `bytesReclaimed` on scope exit can under-report the true eventual
savings for workloads with many overlapping compressed chains, and
`compPool_` can accumulate dead-but-unfreed slots over a long-running
compilation. A real fix needs either a compacting GC pass over `compPool_` or
switching to a structure where interior nodes can be freed without breaking
descendants (e.g. each COMPRESSED entry storing its full decode chain length
capped at `reanchorInterval`, or a copy-on-tombstone repair pass).

## 2. Thresholds are hand-picked, not tuned

`PolicyConfig`'s defaults (`inlineMaxLen=12`, `compressMinLen=10`,
`hotAccessThreshold=3`, `highPressureThreshold=0.85`, etc.) were chosen by
inspection as reasonable starting points. No sweep was run to check whether
they're anywhere near optimal for a given real workload -- see
`docs/experiment_plan.md` item 2.

## 3. Access-frequency signal at insert time is a hint, not a measurement

`insert(name, typeId, accessFreqHint)`'s `accessFreqHint` parameter has to be
supplied by the caller (a real compiler front-end would need its own static
heuristic, e.g. "this identifier is a loop-body variable"). The genuinely
adaptive part is the *post-insertion* promotion via `lookup()`/
`recordAccess()`, which reacts to real access counts -- the insert-time hint
alone is only ever as good as whatever guessed it.

## 4. No statistical significance testing yet

Every number in `results/` comes from a single, deterministic (seeded) run.
Comparing "BudgetSym uses 1.24x-2.37x less memory than Conventional" across
datasets is an observation about these specific runs, not a claim backed by
repeated-trial statistics. See `docs/experiment_plan.md` item 1 for the
concrete next step.

## 5. Synthetic datasets, not real compiler traffic

All 8 benchmark datasets are generated identifier strings (`std::mt19937`,
fixed seeds), not identifiers extracted from an actual embedded C/C++
codebase. They're designed to exercise specific conditions (memory pressure,
prefix similarity, hot/cold access, nested scoping) but a real workload could
combine these differently than the synthetic datasets do.

## 6. Cost model is a documented estimate, not an instrumented measurement

`MemoryTracker`'s per-entry byte costs (see `docs/methodology.md`'s table)
are computed from real stored data lengths plus fixed, documented
bookkeeping constants -- not from an actual `malloc`/allocator
instrumentation pass. The relative comparison between implementations should
be trustworthy (all three use the same style of cost model, computed the same
way), but the absolute byte counts should not be read as "this is exactly how
many bytes the OS allocator would use."

## 7. Single-threaded only

No concurrency story. A real compiler with parallel translation-unit
processing would need to decide how `MemoryTracker`'s budget/pressure state
is shared or partitioned across threads -- not addressed here.
