# SymTabV2 Hot/Cold Tiering Design (P0.4)

Required deliverable per `CLAUDE_RESEARCH.md` §7 P0.4. Documents the
promotion and demotion policy implemented in `include/symtab_v2.hpp`,
validated by `tests/symtab_v2_compressed_test.cpp`.

## 1. Tiers

```
HOT
 └── INTERNED   (decide()-native, or promoted from COMPRESSED)
COLD
 └── COMPRESSED (block front-coded)
```

INLINE is not part of hot/cold tiering: `decide()` only offers INLINE to
names below `inlineMaxLen`, and a name's length never changes, so an INLINE
entry never transitions to any other representation and no other
representation ever transitions to INLINE.

## 2. Hot threshold (promotion: COMPRESSED -> INTERNED)

`PolicyConfigV2::hotAccessThreshold` (default `3`). A COMPRESSED entry whose
`accessCount` (incremented on every `resolve()` hit) reaches this value is
promoted to INTERNED **immediately, inline, from within `resolve()`** (see
`maybePromote()`). This is unconditional once the runtime signal is
observed -- there is no separate "cold threshold" gating promotion; the hot
threshold alone decides it.

**Signal source**: `accessCount`, incremented only by past `resolve()`
calls. No future information, no benchmark labels, no test-set knowledge --
satisfies CLAUDE_RESEARCH.md §5.4's "No future information" requirement.
See `maybePromote()`'s "ONLINE vs ORACLE" comment for the explicit
distinction from a hypothetical oracle experiment (not implemented; would be
a separate, clearly-labeled code path if ever added).

## 3. Cold threshold (demotion: INTERNED -> COMPRESSED)

`PolicyConfigV2::coldIdleEpochs` (default `0`, i.e. **demotion disabled**).
When non-zero, an INTERNED entry that was **specifically promoted** from
COMPRESSED (tracked via `PackedEntry::wasPromoted`) becomes eligible for
demotion once at least `coldIdleEpochs` have passed since its last
`resolve()` hit, where an "epoch" is one online lookup-operation tick
(`SymTabV2::epoch_`, incremented once per `resolve()` call -- hit or miss).

**Why gated on `wasPromoted`, not just "any INTERNED entry idle long
enough"**: an entry `decide()` natively routed to INTERNED (below
`inlineMaxLen`/`compressMinLen`, or an exact repeat of a name seen before --
see `decide()`'s comment on why exact repeats always intern) was placed
there by the insert-time policy, not by the runtime hot/cold mechanism.
Demoting it would silently override that policy decision and reintroduce
compression overhead the policy deliberately avoided (e.g. paying a full
COMPRESSED-block cost for a name that recurs often, which `decide()`'s
repeat-detection rule exists specifically to prevent -- see its comment,
"enabling length-only COMPRESSED routing ... made SymTabV2 WORSE than
Conventional's measured heap on every corpus"). Only demote what promotion
itself put there.

**Why demotion is NOT run automatically on every `resolve()`**: unlike
promotion (an O(1) check on the single slot already being resolved),
finding "which entries have been idle long enough" requires scanning live
slots -- `runMaintenance()` is `O(live-slot-count)`. Running that on every
lookup would silently turn every `resolve()` call into an O(n) operation,
which is exactly the class of hidden benchmark-artifact behavior
CLAUDE_RESEARCH.md warns against. Instead, `runMaintenance()` is a public
method the caller invokes explicitly at a natural checkpoint (e.g. once per
K declarations, once per translation unit, or once per scope exit at
function granularity in a real compiler) -- matching how a real system
would schedule non-critical-path maintenance work off the hot path.

## 4. Promotion behavior

1. `resolve()` hits a COMPRESSED entry, increments `accessCount`.
2. If `accessCount >= hotAccessThreshold`: `maybePromote()` releases the
   block member (`releaseBlockMember()` -- decrements `Block::liveCount`;
   physically reclaims only if this was the block's last live member),
   interns the name (`internName()`), flips `representation` to
   `INTERNED_REP`, sets `wasPromoted = true`, increments `promotions_`.
3. The slot id, declaration id, and `ScopeIndex` mapping are **untouched** --
   `fingerprint()`/`fingerprint8()` depend only on the name string, not
   representation, so no re-insertion into any index is needed and no
   in-flight lookup can observe an inconsistent state.

## 5. Demotion behavior

1. `runMaintenance()` scans `entries_`. For each live, `wasPromoted`,
   currently-`INTERNED_REP` slot: if `epoch_ - lastAccessEpoch >=
   coldIdleEpochs`, call `demote()`.
2. `demote()` reads the name back out of the pool (`pool_[poolIndexOf_[...]]`),
   releases the pool reference (`releasePoolRef()`), flips `representation`
   back to `COMPRESSED_REP`, clears `wasPromoted`, **resets `accessCount` to
   0** (a demoted entry must re-earn promotion from scratch -- it does not
   retain "credit" toward crossing `hotAccessThreshold` again), and
   re-inserts the name into the currently-open compression block via
   `insertCompressed()` (i.e. it gets a **fresh** `CompressedRef`, not a
   reuse of its old, possibly-already-reclaimed one).
3. Same invariant as promotion: slot id, declaration id, and `ScopeIndex`
   mapping are untouched.

## 6. Scope interaction

Hot/cold state (`accessCount`, `wasPromoted`, `lastAccessEpoch`,
`representation`) lives on the `PackedEntry`, which is scoped exactly like
any other slot: `exitScope()` releases it (via `releaseSlot()` ->
representation-specific teardown) regardless of its current tier, with no
special-casing needed. A promoted-then-demoted-then-promoted-again cycle
within the same scope is legal and exercised implicitly by the differential
fuzz harness's interleaved insert/resolve/enterScope/exitScope traces.

## 7. Memory / lookup transition cost

| Transition | Memory effect | Lookup-path effect |
|---|---|---|
| COMPRESSED -> INTERNED (promotion) | Trades a front-coded block-member cost for a (possibly larger, possibly not, depending on prefix similarity -- see `results/block_compression_sweep.csv`) full pool-string cost | Future hits skip block reconstruction entirely (`nameEquals()`'s INTERNED branch is a direct string compare) |
| INTERNED -> COMPRESSED (demotion) | Reclaims the pool-string cost (once refcount hits 0), pays a fresh block-member cost | Future hits pay reconstruction again (bounded by `anchorInterval`, see `results/block_compression_sweep.csv` for the measured depth/latency relationship) |

`kSlotOverhead` (the `PackedEntry` + `poolIndexOf_` slot cost) is charged
identically for every representation and is untouched by either transition
-- only the representation-specific extra cost changes.

## 8. Correctness tests

`tests/symtab_v2_compressed_test.cpp`:
- `test_promotion_on_hot_access` -- promotion fires exactly at threshold,
  declaration id and block-neighbor state are preserved.
- `test_demotion_on_idle_after_maintenance` -- demotion does not fire before
  `coldIdleEpochs`, fires exactly once `runMaintenance()` is called after
  the idle threshold, is idempotent on a second immediate sweep, and
  preserves declaration id / correctness of the round trip.
- `test_demotion_disabled_by_default` -- `coldIdleEpochs == 0` (the default)
  keeps `runMaintenance()` an unconditional no-op; promotion stays
  one-directional, matching pre-P0-4 behavior.
- `test_naturally_interned_entry_never_demoted` -- an entry `decide()`
  routed to INTERNED natively (exact-repeat rule, never went through
  COMPRESSED) is never touched by `runMaintenance()` even when idle past the
  threshold.
- `test_slot_reuse_does_not_inherit_promotion_state` -- ECC review C1 fix
  (`results/ecc_review.md`): forces a promote -> release (scope exit) ->
  free-list slot reuse -> insertion of a natively-INTERNED symbol ->
  `runMaintenance()` sequence and asserts the new symbol is untouched.
  Before the fix, `allocSlot()`/`insert()` left `wasPromoted` and
  `lastAccessEpoch` stale on a reused slot, so a brand-new,
  never-resolved, decide()-native INTERNED entry could inherit a prior
  occupant's promotion state and get demoted on the very first
  `runMaintenance()` sweep -- verified to actually reproduce (by
  temporarily reverting the fix in a scratch copy and confirming this exact
  test fails) before being fixed.

All five pass, including clean under `-fsanitize=address,undefined`.

## 9. Demotion ON/OFF experiment (ECC review H3 fix)

Required by `results/ecc_review.md` finding H3: demotion was implemented
with a plausible rationale but zero measured evidence it helps, and
`decide()`'s own comment (`include/symtab_v2.hpp`) documents a finding that
pushes the other way. `src/demotion_experiment_main.cpp` runs a dedicated
comparison, writing `results/demotion_experiment.csv`.

**Design**: two workloads (3000 unique, prefix-similar names; 20% marked
"hot"), deliberately at opposite ends of the axis demotion is supposed to
help on, each run identically except for `coldIdleEpochs` (0 = off, 40 =
on):

- `hot-then-cold`: the hot subset is driven past `hotAccessThreshold`
  early (promoting all of it), then never touched again -- the case
  demotion is explicitly designed for.
- `sustained-hot`: the ENTIRE hot subset is re-touched every round for the
  whole run -- the adversarial case where `decide()`'s comment predicts
  demotion should hurt (a genuinely repeat-heavy name bounced back into
  COMPRESSED only to immediately need re-promotion).

Both workloads include real scope churn (short-lived nested scopes with
repeated short names) throughout, to advance the epoch clock realistically
and to exercise free-list slot reuse under demotion at experiment scale --
the exact combination the C1 fix (section 8 above) is meant to make safe.

**Results** (representative run; latency figures carry meaningful run-to-run
variance at this microsecond scale -- see the caveat below):

| Workload | Memory (on vs off) | Mean lookup latency (on vs off) | Promotions (off/on) | Demotions |
|---|---:|---:|---:|---:|
| hot-then-cold | -4.96% | +13% to +20% | 600 / 600 | 600 |
| sustained-hot | -4.63% | +13% to +19% | 600 / **9000** | 8960 |

**Findings, reported honestly (not tuned to look favorable)**:

1. On `hot-then-cold` -- the case demotion is designed for -- it delivers a
   real but modest memory reduction (~5%) at a non-trivial latency cost
   (roughly +13-20% mean lookup latency across repeated runs). This is a
   genuine memory/latency tradeoff, not obviously a net win: a ~5% memory
   saving for a ~15% latency cost is a design decision a real system would
   need to justify against its own constraints, not something this project
   should present as an unqualified improvement.
2. On `sustained-hot` -- entries that are accessed on every single round of
   the workload -- demotion still fires 8960 times and forces **15x more
   promotions** than with demotion off (9000 vs 600), because `coldIdleEpochs`
   is measured in *global* resolve()-call ticks (`SymTabV2::epoch_`, shared
   across every name in the table), not a per-name notion of "wall-clock
   idle time." A workload with heavy overall lookup traffic (this one
   performs hundreds of resolve() calls per round, across churn and the hot
   subset) can make an entry's `lastAccessEpoch` look stale relative to the
   *current* epoch well before that specific entry has gone idle in any
   meaningful sense -- causing continuous demote-then-immediately-
   re-promote thrashing. This is a genuine, previously-undocumented failure
   mode, not a coding bug: `runMaintenance()`, `maybePromote()`, and
   `demote()` all behave exactly as designed; the *design* of a
   global-tick-based idle clock is what produces this outcome on a
   traffic-heavy workload.
3. Memory savings are nearly identical on both workloads (-4.96% vs -4.63%)
   despite wildly different demotion/promotion churn -- the memory
   accounting is correct either way (see the `demotions` column: 600 vs
   8960, yet memory converges to nearly the same place), but the *cost* of
   getting there (9000 promotion/demotion cycles' worth of pool-intern and
   block-insert work) is very different and entirely wasted on
   `sustained-hot`.

**Conclusion**: demotion, as currently designed, is not a clear net
improvement. It provides a real but modest memory benefit on the workload
it's designed for, at real latency cost, and can actively thrash (many
promotion/demotion cycles for zero additional memory benefit) on a
workload with high sustained access traffic and the tested `coldIdleEpochs`
value. This is reported as a genuine, currently-unresolved limitation, not
forced into a positive result. Per `CLAUDE_RESEARCH.md`'s rule 13 ("if [a
mechanism] does not improve over a simpler heuristic, report that
honestly"), **`coldIdleEpochs` remains 0 (disabled) by default**, and no
benchmark in this repository enables it. A fix worth investigating in a
later phase (not implemented here, to avoid tuning the experiment to
produce a nicer number): a per-name idle measure less sensitive to global
lookup volume -- e.g. counting only lookups on *other* entries since this
one's last hit, or a much larger `coldIdleEpochs` scaled to expected
per-round traffic -- but any such change would itself need the same
on/off comparison before being trusted.

**Latency-variance caveat**: `finalLookupMeanUs` in
`results/demotion_experiment.csv` comes from a single timed pass per
configuration (`HiResTimer`, no repeated-trial averaging), unlike
`block_compression_sweep_main.cpp`'s percentile treatment applied across a
full population sample. Repeated runs during this experiment's development
showed the reported percentage swinging by roughly +/-10 percentage points
(occasionally producing a spurious negative value on one outlier run out of
~6), consistent with OS scheduling jitter at sub-microsecond measurement
scale rather than a real effect. The qualitative findings above (modest
memory win, real latency cost, severe promotion/demotion thrashing on
`sustained-hot`) are robust across every run observed; the exact latency
percentages are not, and a rigorous multi-trial measurement (matching
CLAUDE_RESEARCH.md §9.4's warmup/repetition/variance requirements) is
deferred to the benchmark-infrastructure phase.

## 10. What is deliberately NOT implemented

- **No time-decayed / weighted access counter.** `accessCount` is a simple
  monotonic counter, not an exponentially-decayed frequency estimate. A
  decayed counter was considered and rejected for this slice: it would add
  a second tunable (decay rate) without a measured justification yet -- the
  simpler counter is preferred per CLAUDE_RESEARCH.md §1 rule 15 ("prefer a
  simpler defensible implementation over unnecessary complexity") until a
  Pareto/parameter-study experiment shows the simple counter is
  insufficient.
- **No automatic maintenance scheduling inside SymTabV2 itself** (e.g. "run
  `runMaintenance()` every N inserts internally"). Left to the caller by
  design -- see §3 above. A benchmark/production integration that wants
  automatic scheduling can call `runMaintenance()` from its own
  `insert()`/`exitScope()` wrapper; this is not yet wired into any
  benchmark tool in this repository and is flagged as a follow-up for
  whichever later phase (Parameter study / Ablation) needs demotion
  exercised at corpus scale.
