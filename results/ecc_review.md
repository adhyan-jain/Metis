# ECC Review — SymTabV2 (P0 complete, pre-benchmark-infrastructure gate)

Required deliverable per `CLAUDE_RESEARCH.md` §8. Adversarial code review of
`include/symtab_v2.hpp` and the P0.3/P0.4 deliverables built on top of it
(`src/block_compression_sweep_main.cpp`, `docs/hot_cold_design.md`,
`tests/symtab_v2_compressed_test.cpp`). **No code was modified during this
review** — findings only. Reviewed against commit `0c416ca` (HEAD at review
time).

> **UPDATE (ECC fixes pass, post-review commit)**: every CRITICAL and HIGH
> finding below, plus all four MEDIUM findings, has since been fixed. Each
> finding is now annotated `STATUS: FIXED` (or `STATUS: LEFT AS-IS`) with
> what changed and how it was verified. The findings themselves are left
> otherwise unedited below, as the original record of what the review found
> — see each `STATUS` block for the resolution. This file remains the
> historical review record; `docs/v2_architecture.md` §10 and
> `docs/hot_cold_design.md` §9 carry the corrected, current numbers.

Methodology: read every line of the reviewed files (not a diff skim),
traced every state transition by hand (not "tests pass so it's fine"),
grepped for every call site of anything whose correctness looked
conditional (e.g. `byteFootprint`, `wasPromoted`, `representationOf`), and
constructed concrete failure-triggering call sequences for each claimed bug
rather than asserting plausibility.

---

## CRITICAL

### C1 — Stale `wasPromoted`/`lastAccessEpoch` on slot reuse causes demotion to misfire on entries that were never promoted

**File/location**: `include/symtab_v2.hpp`, `allocSlot()` (lines 539-550) and
`insert()` (lines 383-425), specifically the field-reset list at lines
400-407.

**Evidence**: `allocSlot()` recycles a freed `slotId` from `freeSlots_`
without resetting the `PackedEntry` at that index. `insert()` then
re-populates only `scopeId`, `typeId`, `accessCount` (=0), `nameLen`,
`representation`, and `live` (=true) -- **`wasPromoted` and
`lastAccessEpoch` are never reset**. Concrete failure sequence:

1. Insert long name `"X"` -> COMPRESSED at slot `S`.
2. `resolve("X")` `hotAccessThreshold` times -> `maybePromote()` sets
   `representation=INTERNED_REP`, `wasPromoted=true`,
   `lastAccessEpoch=<some epoch>`.
3. Redeclare `"X"` in the same scope, or exit the scope -> `releaseSlot(S,
   e)` sets `e.live=false`, pushes `S` onto `freeSlots_`. `wasPromoted`
   and `lastAccessEpoch` are **not cleared** by `releaseSlot()` either
   (lines 552-562).
4. A new, unrelated `insert("Y")` where `Y` is short enough (or an exact
   repeat) that `decide()` natively routes it to `INTERNED_REP`.
   `allocSlot()` pops `S` off `freeSlots_` (LIFO -- `S` is very likely the
   next id handed out) and reuses it. `insert()` sets
   `representation=INTERNED_REP` for `"Y"` but leaves `wasPromoted=true`
   and the stale `lastAccessEpoch` from `"X"` in place.
5. `runMaintenance()` is called: its eligibility check (line 465) is
   `!e.live || !e.wasPromoted || e.representation != INTERNED_REP` -- all
   three conditions pass for `"Y"` (it is live, `wasPromoted` is stale-true,
   representation is INTERNED), and `epoch_ - e.lastAccessEpoch` uses the
   stale epoch from `"X"`, which is very likely already `>= coldIdleEpochs`
   the instant `"Y"` exists -- `"Y"` gets demoted to COMPRESSED **on the
   very first maintenance sweep, without ever being resolved even once**.

**Why it matters**: This directly falsifies the documented and tested
invariant "a name `decide()` routed natively to INTERNED is never
force-demoted" (`docs/hot_cold_design.md` section 3, and the very test meant
to guard it -- see below). It corrupts the memory/representation model
`decide()`'s own comment (lines 627-642) empirically justifies: forcing a
repeat-heavy name back into COMPRESSED is exactly the regime that comment
says made SymTabV2 *worse* than Conventional on every corpus. A benchmark
that enables demotion (`coldIdleEpochs > 0`) on a real corpus (heavy scope
churn -- function-local declarations entering/exiting thousands of scopes,
exactly BUDGET-SYM's target workload) will silently misclassify an unknown
fraction of INTERNED entries as demotion-eligible from the moment they are
created.

**Why existing tests miss it**: `test_naturally_interned_entry_never_demoted`
(`tests/symtab_v2_compressed_test.cpp`) inserts its INTERNED-native name as
only the *second* insert into a fresh table -- the free list is still empty
at that point, so `allocSlot()` takes the `entries_.emplace_back()` branch
(a genuinely fresh, zero-initialized `PackedEntry`), never exercising the
reuse path. The bug requires two *different* logical declarations to
literally share a recycled `slotId` -- the free-list reuse mechanism that is
this entire redesign's primary purpose (see the file's own header comment,
lines 13-22) -- and no test does that combined with promotion+demotion.

**Affects**: correctness (violates a documented, user-facing invariant) and
research validity (any future experiment using demotion is unverified until
fixed).

**Recommended fix**: reset `wasPromoted = false` and `lastAccessEpoch = 0`
in `insert()`'s field-reset block (alongside `accessCount = 0`), or reset
the whole `PackedEntry` to a default-constructed value in `releaseSlot()`
before pushing to `freeSlots_`.

**Requires rerunning experiments**: Yes, if/when any experiment enables
`coldIdleEpochs > 0` (none currently do -- see item 3 below for why the
current P0.3/P0.4 evidence is not itself invalidated, only blocked from
safe extension).

**STATUS: FIXED.** `insert()` (`include/symtab_v2.hpp`) now explicitly
resets `wasPromoted`, `lastAccessEpoch`, `poolIndexOf_[slotId]`, and
`compressedRefOf_[slotId]` for every reused slot. New regression test:
`tests/symtab_v2_compressed_test.cpp::test_slot_reuse_does_not_inherit_promotion_state`
-- forces the exact promote -> release -> free-list reuse ->
natively-INTERNED-insert -> `runMaintenance()` sequence described above.
Verified rigorously, not just "tests pass": the test was run against a
scratch copy of the header with the fix's two reset lines removed, and it
failed exactly as predicted (`demoted == 0` failed, `t.demotions() == 0`
failed, `representationOf(...) == INTERNED_REP` failed) before passing
clean against the real, fixed code, including under
`-fsanitize=address,undefined`. `coldIdleEpochs > 0` is now safe to use;
see H3's STATUS below for the dedicated on/off experiment this unblocked.

---

## HIGH

### H1 — `ScopeIndex`'s own memory is never charged to `MemoryTracker`

**File/location**: `include/symtab_v2.hpp`, `ScopeIndex::byteFootprint()`
(line 184) is defined but **never called anywhere in the file** (confirmed
via `grep -rn byteFootprint include/ src/ tests/` -- the only occurrence is
its own definition).

**Evidence**: Every scope constructs a `ScopeIndex<HashFn>` with a real
16-slot `std::vector<Slot>` allocation (`ScopeIndex`'s constructor, line
110) the moment `enterScope()`/the table constructor runs `Scope{}`'s
default member initializer. `ScopeIndex::grow()` (line 198) doubles this
allocation as entries accumulate. None of this -- not the initial 16 slots,
not any subsequent doubling -- is ever passed to `tracker_.add()`. Grepping
`insert()`, `enterScope()`, `exitScope()`, and every private helper in
`SymTabV2` confirms the only `tracker_.add()`/`tracker_.reclaim()` call
sites are for slot cost, pool cost, compressed-block cost, and the
`everSeenRep_` registration cost -- never index cost.

**Why it matters**: `results/memory_audit_v2.csv`'s `nested-scopes` row
(40 scopes, 25 symbols/scope) shows `modeled_bytes_after_insert=145426`
vs `measured_heap_bytes_after_insert=239936` -- a 1.65x measured/modeled
ratio, the *worst* of any synthetic dataset in that file. Part of that gap
is legitimate allocator overhead (documented elsewhere), but an unknown,
unmeasured fraction of it is this specific, structural omission: every one
of those 40 scopes pays a real `sizeof(Slot) * 16` allocation
(`sizeof(Slot)` is 4+4+4+1 bytes, almost certainly padded to 16 on a
typical ABI, so ~256 bytes/scope minimum) that the "modeled" number simply
never includes. For any workload with many short-lived, small scopes (the
realistic case for a compiler processing deeply nested blocks/function
bodies), this is a systematic, uncharged cost that grows with scope count,
not symbol count -- the exact kind of cost this whole memory-accounting
exercise (CLAUDE_RESEARCH.md section 9.1: "clearly distinguish ACTUAL
MEASURED MEMORY from MODELED/COMPONENT ACCOUNTING; never mix the two") is
supposed to catch and report honestly.

**Affects**: research validity (modeled-vs-measured memory claims), and
specifically undermines any future claim like "V2's modeled memory
overhead is X" for scope-heavy workloads.

**Recommended fix**: call `tracker_.add(s.index.byteFootprint())` at scope
creation (and re-charge the delta on `grow()`, or expose a
delta-since-last-charge accessor), and `tracker_.reclaim(...)` the matching
amount in `exitScope()`.

**Requires rerunning experiments**: Yes -- `results/memory_audit_v2.csv`'s
`modeled_bytes_after_insert` column for every dataset needs regenerating
once fixed, since the omission affects every row, not just `nested-scopes`
(it's just most visible there).

**STATUS: FIXED.** The constructor and `enterScope()` now charge
`s.index.byteFootprint()` immediately on scope creation; `insert()` charges
the exact growth delta whenever a scope's `ScopeIndex::insert()` call
triggers a doubling; `exitScope()` reclaims the scope's full footprint
before popping it. `results/memory_audit_v2.csv` was regenerated: every
`SymTabV2` row's `modeled_bytes_after_insert` increased (previously-
uncharged index memory now included); `nested-scopes`' `measured_over_
modeled_insert` ratio improved from 1.65 to 1.35, the largest correction of
any dataset, confirming this was the real, non-trivial gap the review
identified. Verified via diff that every non-`SymTabV2` row
(Conventional/Interned/RobinHood) is byte-for-byte unchanged, and the
automated accounting-invariant check (`memory_audit_v2_main.cpp`'s
cycle-2-vs-cycle-3 repeatability check) still passes for all four tables
post-fix.

### H2 — `block_compression_sweep_main.cpp`'s own sanity checks corrupt the reconstruction-count metric it exists to measure

**File/location**: `src/block_compression_sweep_main.cpp`, lines 114-118
(pre-cold-pass sanity loop) combined with lines 137-140 (metric capture).

**Evidence**: `runOne()` calls `t.representationOf(n)` in a sanity-check
loop over all `names` (line 116) *before* the timed COLD pass. For a
COMPRESSED entry, `representationOf()` internally calls
`ScopeIndex::find()`, whose equality callback is `nameEquals()`, which for
`Rep::COMPRESSED_REP` calls `reconstructMember()` -- and
`reconstructMember()` unconditionally increments the very
`reconstructions_`/`reconstructionSteps_` counters this tool exists to
report (`include/symtab_v2.hpp` lines 744-754). Because `reconAfterCold =
t.reconstructionCount()` (line 137) is a **cumulative** counter read after
both the sanity loop and the cold pass, every reported
`reconstruction_count`/`reconstruction_steps_total` value in
`results/block_compression_sweep.csv` is inflated by exactly one full
extra pass over all `n` names -- which is precisely why every row in that
CSV shows `reconstruction_count = 8000` for `n=4000` (4000 from the
untimed sanity loop + 4000 from the actual cold pass), not the `4000` the
column name and this tool's own file-header comment ("counts actual
`reconstructMember()` calls") implies.

**Secondary consequence**: `representationOf()`'s own doc comment in
`include/symtab_v2.hpp` (lines 499-503) claims it is "a pure introspection
query ... used by tests and analysis tooling to observe promotion without
perturbing it." That claim is **false** with respect to the reconstruction
instrumentation added in this same P0.3 work: calling it on a COMPRESSED
entry does perturb `reconstructionCount()`/`reconstructionStepsTotal()`,
even though it correctly avoids perturbing `accessCount`/promotion as
documented. This is an internal API-contract inconsistency in
`symtab_v2.hpp` itself, not only a bug in the benchmark tool that trusted
the (incomplete) contract.

**Why it matters**: `mean_reconstruction_depth` (steps/count) happens to be
numerically unaffected (both passes reconstruct the identical population at
identical depths, since promotion is disabled in this sweep -- averaging
the same distribution twice doesn't shift the mean), and the *latency*
numbers (p50/p95/p99, `coldSamples`) are unaffected since the sanity loop
is untimed. But the raw `reconstruction_count`/`reconstruction_steps_total`
columns, read as documented ("reconstructions actually performed by
lookups"), overstate the true cold-pass reconstruction volume by 2x. This
is exactly the kind of benchmark-methodology issue CLAUDE_RESEARCH.md
warns about ("places where benchmark methodology could produce misleading
results") -- the tool ran, produced plausible-looking numbers, and the
review-time diagnostic was catching that "8000" appeared for every single
row of a 4000-item dataset without a stated reason, which is a tell.

**Affects**: research validity (one derived metric, in one results file,
mislabeled -- not a correctness bug in `symtab_v2.hpp`'s actual mechanism).

**Recommended fix**: either drop the pre-pass `representationOf()` sanity
check (or replace it with a check that doesn't traverse the reconstruction
path -- e.g. compare against a value captured once at insert time), or
snapshot `reconstructionCount()`/`reconstructionStepsTotal()` immediately
after insertion and report the *delta* across the cold pass, not the
cumulative total. Also fix `representationOf()`'s doc comment in
`symtab_v2.hpp` to state precisely which counters it does and does not
perturb.

**Requires rerunning experiments**: Yes -- `results/block_compression_sweep.csv`'s
`reconstruction_count`/`reconstruction_steps_total` columns must be
regenerated. `mean_reconstruction_depth` and all latency columns do not
need to change (see above) but should be re-verified after the fix as a
matter of hygiene, not because they're expected to move.

**STATUS: FIXED.** `block_compression_sweep_main.cpp` now snapshots
`reconstructionCount()`/`reconstructionStepsTotal()` immediately before and
after each timed pass and reports the DELTA, not a cumulative total --
cold-pass and hot-pass reconstruction work are now separate,
delta-isolated CSV columns (`cold_reconstruction_count`/
`cold_reconstruction_steps_total`/`cold_mean_reconstruction_depth` and the
`hot_` equivalents), immune to any sanity check run before or between the
snapshot points. Also fixed `representationOf()`'s doc comment in
`include/symtab_v2.hpp` to state precisely that it DOES perform a real
reconstruction (and thus DOES perturb the reconstruction counters) for a
COMPRESSED candidate, even though it correctly avoids perturbing
`accessCount`/promotion. Sweep regenerated: `cold_reconstruction_count` is
now `4000` for every n=4000 row (previously `8000`); `mean_reconstruction_
depth` is unchanged (as predicted, since it's a ratio of two equally-
scaled quantities). Memory figures also shifted in this regeneration, but
that shift is due to the H1 fix (ScopeIndex accounting), not this fix --
see H1's STATUS and `docs/v2_architecture.md` §10 item 3's "SUPERSEDED
VALUES NOTICE" for the full before/after comparison.

### H3 — Demotion has zero ablation/comparison evidence that it is net beneficial, and its own re-materialization strategy works against decide()'s documented empirical finding

**File/location**: `include/symtab_v2.hpp`, `demote()` (lines 839-854);
`decide()`'s comment (lines 627-642).

**Evidence**: `decide()`'s own comment states, as an empirically-confirmed
finding from this same project: "enabling length-only COMPRESSED routing
(no repeat check) made SymTabV2 WORSE than Conventional's measured heap on
every corpus ... adding this check is what makes compression a net win."
The entire premise is that entries which get resolved often enough to
justify special treatment should end up INTERNED, not COMPRESSED, because
COMPRESSED costs more to touch repeatedly. Demotion's `hotAccessThreshold`
gate for promotion (3, by default) means, by construction, every demotion
candidate was resolved at least `hotAccessThreshold` times before becoming
eligible -- i.e. demotion specifically targets entries that already proved
they get accessed repeatedly, and moves them back into the representation
`decide()`'s own comment says is worse for exactly that access pattern
once it later goes idle for `coldIdleEpochs`. This may or may not be a net
win depending on the actual balance of "cost saved while cold" vs "cost of
re-promoting if it becomes hot again" -- but **no experiment in this
repository measures that balance**. `results/block_compression_sweep.csv`
explicitly disables promotion/demotion (`hotAccessThreshold` set
unreachably high, see its file header) to isolate the COMPRESSED-tier
measurement, and no other results file exercises demotion at all.

**Additional design concern (not yet measured)**: `demote()` re-inserts the
demoted name via `insertCompressed()` (line 852), which always appends to
whatever block currently happens to be `openBlock_` -- i.e. whichever block
is receiving *newly-declared* names at the moment `runMaintenance()` runs.
A demoted name's front-coding neighbor is therefore essentially random
relative to it (unrelated to its own prefix), unlike a name's *original*
compression, which benefits from genuine source-order prefix locality (see
`docs/v2_architecture.md` section 6 and the measured prefix-similarity-
dependent savings in `results/block_compression_sweep.csv`). This means
demoted entries likely compress worse than their first-time-COMPRESSED
counterparts -- plausible, not yet measured, and directly relevant to
whether demotion actually reclaims the memory it's meant to.

**Why it matters**: CLAUDE_RESEARCH.md section 1 rule 13 ("If ML does not
improve over a simpler heuristic, report that honestly") and the broader
"every important optimization requires an ablation or justification"
(rule 12) apply here just as much to a hand-designed heuristic as to ML:
demotion is a new mechanism added in this same phase with a
plausible-sounding rationale but literally zero measured evidence it
helps, and one piece of the project's own prior evidence (the `decide()`
comment) suggesting it could hurt in exactly the case it's designed to
fire on.

**Affects**: research validity -- this is precisely "an improvement that is
merely implemented, not yet a validated research contribution," which
CLAUDE_RESEARCH.md explicitly warns against presenting as a result.

**Recommended fix**: not a code fix -- an experimental one. Before any
Pareto/ablation claim mentions demotion, run a dedicated comparison
(demotion on vs. off, same corpus, same seeds) measuring memory and
latency, and report whichever way it actually goes, including a negative
result if that's what the data shows.

**Requires rerunning experiments**: Not applicable yet (no experiment
currently claims a demotion benefit) -- but this blocks ever making such a
claim without first running the comparison, and blocks turning on
`coldIdleEpochs > 0` in any future benchmark until C1 is fixed first.

**STATUS: FIXED (experiment run; result is a documented negative/mixed
finding, not a forced positive).** New `src/demotion_experiment_main.cpp`
runs demotion ON vs OFF on two workloads (`hot-then-cold`, designed to
favor demotion, and `sustained-hot`, designed to stress it per `decide()`'s
own documented concern), with real scope churn interleaved to exercise the
C1 fix at experiment scale. Result (`results/demotion_experiment.csv`,
full writeup in `docs/hot_cold_design.md` §9): `hot-then-cold` gets a real
but modest ~5% memory reduction at a ~13-20% mean-latency cost;
`sustained-hot` gets essentially the same ~5% memory reduction but at the
cost of 15x more promotion/demotion churn (9000 vs 600 promotions) than
demotion-off, from a previously-undocumented failure mode: `coldIdleEpochs`
is measured against a *global* resolve()-call clock shared by every name in
the table, so a workload with heavy overall lookup traffic can make even a
genuinely "hot" (repeatedly-accessed) entry look idle relative to the
current epoch, causing continuous demote/re-promote thrashing. Per
CLAUDE_RESEARCH.md's rule 13, this is reported as a genuine, current
limitation -- `coldIdleEpochs` stays `0` (disabled) by default, and no
benchmark tool in this repository enables it.

### M1 — `fingerprint()` and `fingerprint8()` are correlated bit-slices of the same hash, not independent checks, and this is never validated

**File/location**: `include/symtab_v2.hpp`, `fingerprint()` (line 651) and
`fingerprint8()` (line 662); `BlockMember::fp8`'s comment (lines 233-243)
claims these are "two genuinely independent cheap rejection checks."

**Evidence**: With the default `HashFn = FnvHash`, both values are pure
functions of the *same* 64-bit FNV-1a hash: `fingerprint()` =
`low32(h) XOR high32(h)`, `fingerprint8()` = `bits 16-23 of h`. Bits 16-23
of `h` participate directly in `fingerprint()`'s XOR (they're inside
`low32(h)`). If two names produce the exact same 64-bit `h` (a true hash
collision, not merely a fingerprint collision), both `fingerprint()` and
`fingerprint8()` collide together -- there is no scenario where a genuine
64-bit hash collision defeats one check but not the other, because both
are deterministic functions of the one value being tested for collision.
The "independent" framing is accurate only for the narrower claim that a
*fingerprint*-level collision (post-compression to 32 or 8 bits) in one
doesn't imply a fingerprint-level collision in the other -- true, but a
much weaker property than "independent checks," and the comment does not
make that distinction. FNV-1a is also known to have weaker avalanche in
its low-order bits than a hash like Murmur3 (already present in this
codebase as `Murmur3Hash`, see `hash_functions.hpp`) -- the actual degree
of correlation between these two derived values, for this specific hash,
is asserted, not measured.

**Why it matters**: The whole point of `fp8` (per its own comment,
predicting "~8.4 real collisions" at Zephyr scale on the 32-bit fingerprint
alone) is to catch cases the first check misses. If the two checks are more
correlated than assumed, `fp8`'s real-world rejection rate is lower than
the design implies, though correctness is never at risk either way (both
are rejection-only fast paths -- see below).

**Affects**: performance claim validity (does `fp8` actually reduce
unnecessary reconstruction by the assumed amount?), not correctness -- a
false-negative-causing bug here is structurally impossible since
`nameEquals()`'s COMPRESSED branch always falls through to a full
`reconstructMember() == name` compare regardless of what the cheap checks
decide (lines 692-693); a `fp8` mismatch can only produce an early
`return false`, and it is only reached for names that already collided on
the real fingerprint `fp`, so an early-false there is provably safe
(`fp8` differs) rather than a risk of accepting a wrong match.

**Recommended fix**: not a code fix -- either (a) derive `fingerprint8()`
from bits disjoint from those folded into `fingerprint()`'s XOR (e.g. bits
of `high32(h)` alone, still correlated with the *same* underlying collision
event but structurally distinct from `fingerprint()`'s specific bit
combination), or (b) soften the code comment's "independent" language to
what's actually true, and/or (c) add a measured test (not just an assumed
one) of `fp8`'s actual rejection rate under a forced-fingerprint-collision
scenario (the existing `test_forced_hash_collision_still_resolves_correctly`
test uses a *constant* hash, which collides both fp and fp8 together by
construction -- it validates correctness under total collision, not the
"independent-check" performance claim under partial collision).

**Requires rerunning experiments**: No (this affects only the theoretical
justification and possibly the observed rejection-rate numbers in a future
"fingerprint rejections" counter, which doesn't currently exist as a
reported metric anywhere).

**STATUS: FIXED (documentation correction).** `fingerprint8()`'s comment in
`include/symtab_v2.hpp` now states precisely what independence property
actually holds (fingerprint-level, not hash-level) and why correctness is
unaffected either way (the full decode-and-compare always remains the final
arbiter). No code behavior changed -- this was a comment-accuracy fix, not
a functional one, consistent with the finding being about a claim, not a
bug.

### M2 — `demote()`/`maybePromote()` leave a stale `compressedRefOf_` trail (currently harmless, latent footgun)

**File/location**: `include/symtab_v2.hpp`, `maybePromote()` (lines
815-830): after promotion, `compressedRefOf_[slotId]` still holds the
*released* (and potentially later reclaimed-and-reused-by-an-unrelated-block)
`CompressedRef` -- it is never reset to an invalid sentinel. Symmetrically,
`demote()` (line 848) *does* reset `poolIndexOf_[slotId] = UINT32_MAX`
after releasing it, but `maybePromote()` has no equivalent reset for
`compressedRefOf_`.

**Evidence**: Currently harmless because `nameEquals()`'s `switch` on
`e.representation` only ever reads `compressedRefOf_[slotId]` when
`representation == COMPRESSED_REP`; after promotion the representation is
`INTERNED_REP`, so the stale ref is never dereferenced by any code path
that exists today.

**Why it matters**: this is a latent correctness hazard, not a live one --
if a future debugging tool, ablation variant, or refactor ever reads
`compressedRefOf_` unconditionally (e.g. a memory-accounting tool that
walks all slots' representation-specific refs for diagnostics, or a future
"partial promotion" variant), it will observe a `CompressedRef` pointing
into a block/slot that may since have been fully reclaimed and reused for
an entirely unrelated live compressed member, silently reading garbage. The
inconsistency between `maybePromote()` (no reset) and `demote()` (resets
`poolIndexOf_`) suggests this was noticed for one direction but not the
other.

**Recommended fix**: reset `compressedRefOf_[slotId] = CompressedRef{}`
(the `UINT32_MAX`-sentinel default) inside `maybePromote()` symmetrically
with what `demote()` already does for `poolIndexOf_`.

**Requires rerunning experiments**: No.

**STATUS: FIXED.** `maybePromote()` now resets `compressedRefOf_[slotId]`
immediately after `releaseBlockMember()`, symmetric with `demote()`'s
existing `poolIndexOf_` reset. Covered incidentally by the existing
promotion/demotion test suite (all still pass under ASan+UBSan); no new
test needed since this was a latent-only footgun with no currently-reachable
observable effect (see the finding's own evidence).

### M3 — `runMaintenance()`'s documented complexity ("O(live-slot)") is actually O(peak-concurrent-slot-count), and this cost is never measured

**File/location**: `include/symtab_v2.hpp`, `runMaintenance()` (lines
448-471, especially the loop bound `slotId < entries_.size()` at line
463); `docs/hot_cold_design.md` section 3 repeats the same
"O(live-slot-count)" characterization.

**Evidence**: The loop iterates every index in `entries_`, live or dead
(guarded internally by `if (!e.live ...) continue;`), so its cost tracks
`entries_.size()` (== `peakSlotCount()`, the historical high-water mark of
*concurrently* live slots, per the class's own `peakSlotCount()` doc
comment at lines 515-519) -- not the number of slots live *at the moment
`runMaintenance()` is called*. For a workload with a high peak but a low
current live count (e.g. immediately after a large batch of scopes exits),
this scan is far more expensive than "O(live-slot)" implies.

**Why it matters**: overhead review coverage explicitly requires evaluating
promotion/demotion overhead, and this is exactly the kind of
mischaracterized cost that could mislead a future benchmark design (e.g.
someone scheduling `runMaintenance()` "once per N declarations" without
realizing its cost scales with historical peak, not current size, could
build a maintenance schedule that behaves very differently on a
scope-churny workload than intended). No results file currently measures
`runMaintenance()`'s actual wall-clock cost at all -- it is entirely
unbenchmarked.

**Recommended fix**: correct the complexity claim in both
`include/symtab_v2.hpp`'s comment and `docs/hot_cold_design.md` section 3
to "O(peak-concurrent-slot-count)," and, before any experiment relies on
`runMaintenance()`'s cost being negligible, measure it directly (wall-clock
per call, scaling with `peakSlotCount()`) rather than assuming based on the
current (inaccurate) comment.

**Requires rerunning experiments**: No new experiment currently depends on
this being cheap, but any future one that schedules `runMaintenance()`
periodically must account for the corrected complexity.

**STATUS: FIXED (documentation correction).** The complexity claim in both
`include/symtab_v2.hpp`'s comment and `docs/hot_cold_design.md` §3 is
corrected to "O(peak-concurrent-slot-count)". `results/demotion_experiment.csv`'s
experiment (added for H3) exercises `runMaintenance()` repeatedly under
real churn but does not isolate its own wall-clock cost as a separate
metric -- that remains a follow-up for a future phase, as originally noted.

### M4 — `CompressedRef::slotInBlock` (uint16_t) silently truncates if `blockSize` exceeds 65535, with no guard

**File/location**: `include/symtab_v2.hpp`, `insertCompressed()` line 705:
`uint16_t slot = static_cast<uint16_t>(b.members.size());`, and
`PolicyConfigV2::blockSize` (line 282), declared `size_t` with no upper
bound documented or asserted.

**Evidence**: If a caller ever configures `blockSize > 65535` (nothing
prevents this -- it's a plain `size_t`), `insertCompressed()`'s truncating
cast wraps the slot index silently, corrupting addressing for the 65536th+
member of an over-sized block (two different members would alias the same
`slotInBlock`, or worse, reconstruct against the wrong preceding member).
Not currently triggered: `results/block_compression_sweep.csv`'s sweep
only goes up to `blockSize=128`, and the fixed default is `32`.

**Why it matters**: this is exactly the kind of silent-wraparound bug that
would only surface if a future parameter study (CLAUDE_RESEARCH.md section
13) explores a wider `blockSize` range -- e.g. testing "what if reclaim
granularity is deliberately very coarse" (a single very large block per
scope) as a legitimate research question. Currently latent, not exercised.

**Recommended fix**: `assert(cfg_.blockSize <= UINT16_MAX)` in the
constructor, or widen `slotInBlock` to `uint32_t` (small memory cost per
`CompressedRef`, removes the ceiling entirely).

**Requires rerunning experiments**: No (not currently triggered by any
committed configuration).

**STATUS: FIXED.** The `SymTabV2` constructor now `assert`s
`cfg_.blockSize <= 65535`. No committed configuration was ever near this
limit (max swept value is 128), so no experiment output changes.

---

## LOW

### L1 — `anchorInterval > blockSize` combinations are never measured

**File/location**: `src/block_compression_sweep_main.cpp`, the sweep
generator: `if (ai > bs) continue;` skips every combination where the
anchor stride exceeds the block size.

**Evidence/why it matters**: This is a legitimate, deliberate scope
decision (an anchor stride longer than the block itself degenerates to "at
most one anchor per block," a less interesting regime), not a bug -- but it
means `results/block_compression_sweep.csv` provides zero evidence about
that regime's behavior, and a reader could reasonably ask why not. Worth
stating as an explicit limitation in any results writeup that cites this
CSV, rather than leaving it implicit.

**Recommended fix**: none required; documentation-only note for the
eventual `results/FINAL_RESULTS.md` limitations section.

**Requires rerunning experiments**: No.

**STATUS: LEFT AS-IS.** No code fix was ever recommended for this finding;
it is a scope-decision note for future results writeups, unchanged by this
pass.

### L2 — `accessCount` (uint32_t) has no overflow guard for extremely long-lived hot entries

**File/location**: `include/symtab_v2.hpp`, `PackedEntry::accessCount`
(line 77), incremented unconditionally on every `resolve()` hit (line 439)
with no ceiling.

**Evidence/why it matters**: At `2^32` hits (astronomically unlikely for
any dataset in this project's current scale -- Zephyr's corpus is ~3.7M
total operations) the counter would wrap to 0, which for an already-hot,
already-`INTERNED` entry has no behavioral consequence (promotion has
already happened and is one-directional per hit; `demote()` resets the
counter anyway on any demotion). Included for completeness, not because
it's reachable at any realistic scale tested or planned in this project.

**Recommended fix**: none required at current scale.

**Requires rerunning experiments**: No.

**STATUS: LEFT AS-IS.** Not reachable at any scale exercised by this
project's datasets (confirmed: even `demotion_experiment.csv`'s sustained-hot
workload, which drives roughly 9000 promotions, is many orders of magnitude
below `2^32`).

### L3 — `pool_[idx]`'s real (heap-allocated, for names above SSO length) string buffer is not physically freed when its refcount drops to 0, only its tracked byte count

**File/location**: `include/symtab_v2.hpp`, `releasePoolRef()` (lines
615-625), already partially self-documented ("pool_[idx] slot itself is
not compacted").

**Evidence/why it matters**: Already acknowledged in the code's own
comments and empirically visible in `results/memory_audit_v2.csv`'s
`measured_over_modeled` columns (measured heap consistently exceeds
modeled bytes). Restated here only to make explicit, for the ECC-review
record, that this interacts with the new P0.4 demotion path: every
promote-then-demote cycle on the same name re-uses the same stable
`pool_[idx]` slot (confirmed by inspection -- `poolLookup_` is never
erased, so `internName()` finds the existing index and correctly
bumps/drops refcount rather than allocating a new pool slot each cycle),
so repeated promotion/demotion oscillation does **not** leak additional
real string buffers beyond what a single promotion already would -- this is
a genuine, positive finding worth recording (it was checked, not assumed),
not an additional defect.

**Recommended fix**: none required beyond what's already documented;
listed for completeness of the memory-accounting review.

**Requires rerunning experiments**: No.

**STATUS: LEFT AS-IS.** Already-documented, accepted trade-off; this
finding's own text already confirms (by inspection, not assumption) that
the P0.4 demotion path does not compound it.

---

## Summary

### 1. Total findings by severity

| Severity | Count |
|---|---|
| CRITICAL | 1 |
| HIGH | 3 |
| MEDIUM | 4 |
| LOW | 3 |
| **Total** | **11** |

### 2. CRITICAL/HIGH blockers

- **C1** (stale `wasPromoted`/`lastAccessEpoch` on slot reuse) -- blocks
  enabling demotion (`coldIdleEpochs > 0`) in any future experiment until
  fixed. Does not affect any *currently committed* result, since no
  existing benchmark tool enables demotion.
- **H1** (`ScopeIndex` memory never tracked) -- blocks trusting
  `modeled_bytes_after_insert` as a complete structural-cost model for any
  scope-heavy workload; affects the already-committed
  `results/memory_audit_v2.csv`.
- **H2** (sweep tool's own instrumentation double-counts reconstructions) --
  affects the already-committed `results/block_compression_sweep.csv`'s
  `reconstruction_count`/`reconstruction_steps_total` columns specifically
  (not its latency or memory columns, and not its qualitative
  memory/latency-tradeoff conclusion).
- **H3** (demotion has no supporting ablation, and its rationale is in
  tension with `decide()`'s own documented finding) -- blocks making any
  claim that demotion improves the system until a dedicated comparison is
  run.

### 3. Which findings can invalidate current results

- **H1** partially invalidates `results/memory_audit_v2.csv`'s
  `modeled_bytes_after_insert` column (undercounts scope-index memory for
  every row, most visible on `nested-scopes`). It does **not** invalidate
  the `measured_heap_bytes_*` columns (those come from the real allocator
  hook, `heap_counter.hpp`, independent of `tracker_`) or any comparison
  based on measured (not modeled) memory.
- **H2** invalidates `results/block_compression_sweep.csv`'s
  `reconstruction_count`/`reconstruction_steps_total` columns as literal
  numbers (they are 2x the true cold-pass value), but does **not**
  invalidate `mean_reconstruction_depth` (unaffected, see H2's analysis)
  or the latency columns (untimed sanity loop, doesn't touch the timed
  samples) or the qualitative finding ("compression benefit is
  prefix-similarity-dependent, anchorInterval trades memory for
  latency/reconstruction depth, blockSize alone doesn't affect
  latency/depth") -- that conclusion is not sensitive to the 2x count
  inflation.
- **C1** and **H3** do not invalidate any *currently committed* result
  (demotion is not enabled anywhere yet), but block extending P0.4's
  demotion feature into any future phase without first fixing C1 and
  running the H3 comparison.

### 4. Exact fixes required (not yet applied — review only, per instructions)

1. C1: reset `wasPromoted=false` and `lastAccessEpoch=0` on slot
   reuse/release in `insert()`/`releaseSlot()`.
2. H1: charge `ScopeIndex::byteFootprint()` to `tracker_` on scope
   creation/growth/exit; regenerate `results/memory_audit_v2.csv`.
3. H2: fix the sanity-check double-count in
   `block_compression_sweep_main.cpp` (drop the pre-pass check or measure a
   delta, not a cumulative total); regenerate
   `results/block_compression_sweep.csv`; correct `representationOf()`'s
   doc comment in `symtab_v2.hpp`.
4. H3: run a dedicated demotion-on-vs-off comparison before any claim
   depends on demotion helping; report the result honestly either way.
5. M1: soften/correct the "independent checks" claim on `fingerprint()`/
   `fingerprint8()`, or change `fingerprint8()`'s bit source.
6. M2: reset `compressedRefOf_[slotId]` in `maybePromote()`.
7. M3: correct the "O(live-slot)" complexity claim for `runMaintenance()`
   in both the code comment and `docs/hot_cold_design.md`; measure its
   actual cost before relying on it being cheap.
8. M4: assert `blockSize <= UINT16_MAX` or widen `CompressedRef::slotInBlock`.

### 5. Is V2 safe to proceed to the benchmark-infrastructure phase?

**Conditionally yes, with one hard gate**: the core P0.1-P0.3 machinery
(free-list slot reuse, INLINE/INTERNED/COMPRESSED representations,
fingerprint-assisted lookup, block front-coding, promotion) has no
CRITICAL findings and is safe to build the next phase's benchmark
infrastructure on. **Demotion (`coldIdleEpochs > 0`) must not be enabled
in any benchmark until C1 is fixed and verified** -- using it as-is would
silently corrupt representation assignments on any scope-churny workload.
Since no committed result currently enables demotion, this does not block
starting the benchmark-infrastructure phase itself, only the *scope* of
what that phase is allowed to exercise (demotion stays off) until C1 is
fixed. H1 and H2 should be fixed before the benchmark-infrastructure phase
starts generating the "official" memory/reconstruction numbers this
project will build later claims on, since both are cheap, well-understood
fixes and re-deriving numbers is far cheaper now than after downstream
phases (Pareto, ablation, parameter study) have already consumed the
uncorrected CSVs.

---

## POST-FIX UPDATE

All 8 items in section 4's fix list have been applied, in a dedicated
"ECC fixes" pass (see `docs/v2_architecture.md` §10 items 6-10 and
`docs/hot_cold_design.md` §9 for the full writeup of each fix and its
verification). Updated status:

- **Section 1 (totals)**: unchanged as a historical record -- 1 CRITICAL,
  3 HIGH, 4 MEDIUM, 3 LOW were found. All CRITICAL/HIGH/MEDIUM are now
  `STATUS: FIXED`; all LOW are `STATUS: LEFT AS-IS` (none needed a code
  change).
- **Section 2 (blockers)**: no longer blocking. C1 is fixed and covered by
  a regression test verified (via a deliberately-reverted scratch copy) to
  actually catch the bug. H1 and H2 are fixed and their affected results
  files (`results/memory_audit_v2.csv`, `results/block_compression_sweep.csv`)
  regenerated. H3's required experiment (`results/demotion_experiment.csv`)
  is done and reports an honest, mixed/negative result -- demotion remains
  disabled by default.
- **Section 3 (invalidated results)**: the previously-invalidated columns
  in `results/memory_audit_v2.csv` and `results/block_compression_sweep.csv`
  have been regenerated with the fixes applied; the old values are preserved
  in this document's history (see `docs/v2_architecture.md` §10 item 3's
  "SUPERSEDED VALUES NOTICE" for the explicit before/after) rather than
  silently overwritten with no record.
- **Section 5 (verdict)**: **V2 is now unconditionally safe to proceed to
  the benchmark-infrastructure phase.** Demotion remains off by default
  (`coldIdleEpochs = 0`) as a documented DESIGN decision backed by the H3
  experiment's result, not as an unresolved safety gate.
