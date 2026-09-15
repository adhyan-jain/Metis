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

All four pass, including clean under `-fsanitize=address,undefined`.

## 9. What is deliberately NOT implemented

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
