# SymTabV2 Architecture Audit (P0.1)

This document is the P0.1 deliverable required by `CLAUDE_RESEARCH.md` §7
("Full architecture audit"). It traces SymTabV2's complete symbol
lifecycle, inspects ownership/allocation/metadata/index/compression/cache/
representation-transition/accounting/pointer-validity concerns, and records
what is implemented vs. still outstanding before the ECC review phase.

Source: `include/symtab_v2.hpp` (as of commit `7675bd3`).

---

## 1. Complete lifecycle trace

```
DECLARE (insert(name, typeId))
  -> fingerprint(name)                              [32-bit, low32 XOR high32 of HashFn::hash]
  -> same-scope redeclaration check
       ScopeIndex::find(fp, nameEquals) in the CURRENT (innermost) scope only
       if found: erase from ScopeIndex (backward-shift) + releaseSlot()
                 (representation-specific teardown, tracker.reclaim, push to freeSlots_)
  -> policy decision: decide(name)
       len < inlineMaxLen           -> INLINE
       name in everSeenRep_ (exact repeat, any scope, ever) -> INTERNED
       len >= compressMinLen        -> COMPRESSED
       else                          -> INTERNED
  -> allocSlot()                     [reuse freeSlots_.back() if non-empty, else grow entries_]
  -> PackedEntry populated (scopeId, typeId, accessCount=0, nameLen, representation, live=true)
  -> materialize(): representation-specific storage write
       INLINE:      memcpy into PackedEntry::inlineBytes (no allocation)
       INTERNED:    internName() -> pool_/poolLookup_/poolRefCount_ (refcounted)
       COMPRESSED:  insertCompressed() -> open block, front-code against predecessor,
                    or full-string anchor every anchorInterval-th slot
  -> tracker_.add(cost) for the representation-specific bytes
     (+ tracker_.add(everSeenRep_ registration cost) the FIRST time this exact
       string is ever assigned a non-INLINE representation)
  -> ScopeIndex::insert(fp, slotId) in the current scope
  -> Scope::liveSlots.push_back(slotId)
  -> declIdOf_[slotId] = nextDeclId_++; return declId

LOOKUP (resolve(name) / lookup(name) / recordAccess(name))
  -> fingerprint(name)
  -> walk scopes_ innermost-to-outermost (scopes_.rbegin() .. rend())
       ScopeIndex::find(fp, nameEquals) per scope, first hit wins (lexical shadowing)
  -> on hit: entries_[slotId].accessCount++
  -> maybePromote(slotId, name)
  -> return declIdOf_[slotId]  (or -1 if no scope's index has fp)

nameEquals(slotId, name) -- the correctness authority, always exact:
  -> length check first (free, no reconstruction, for every representation)
  -> INLINE:     memcmp(inlineBytes, name)
  -> INTERNED:   pool_[poolIndexOf_[slotId]] == name
  -> COMPRESSED: reject on BlockMember::fp8 (8-bit, independent hash bits) first,
                 else reconstructMember() (bounded decode) then full string ==

PROMOTION (maybePromote, invoked from resolve() on every hit)
  -> only applies to COMPRESSED entries
  -> if accessCount >= hotAccessThreshold:
       releaseBlockMember(oldRef)   [decrement Block::liveCount; may or may not
                                      physically free the block yet]
       representation = INTERNED
       poolIndexOf_[slotId] = internName(name)
       promotions_++
  -> declId, slotId, ScopeIndex mapping, and fingerprint are untouched
     (promotion is representation-only; correctness of subsequent lookups is
     unaffected because fingerprint()/fingerprint8() depend only on the name,
     never on representation)

SCOPE EXIT (exitScope())
  -> for every slotId in the exiting scope's liveSlots:
       skip if !e.live (already redeclared-away earlier in this same scope)
       skip if e.scopeId != exitingScopeId (defense-in-depth; see below)
       releaseSlot(slotId, e)  [reclaim tracker bytes, representation teardown,
                                 e.live=false, push to freeSlots_]
  -> scopes_.pop_back()

RECLAMATION (releaseSlot / releaseBlockMember / releasePoolRef)
  -> INTERNED: poolRefCount_[idx]--; on reaching 0, tracker_.reclaim(pool bytes)
               (pool_[idx] slot itself is NOT compacted -- index stability)
  -> COMPRESSED: Block::liveCount--; on reaching 0 (whole block dead),
               tracker_.reclaim(trackedBytes), members.clear()+shrink_to_fit(),
               block pushed to blockFreeList_ for reuse
  -> slot itself: freeSlots_.push_back(slotId) (immediately reusable by the
               next allocSlot(), anywhere in the table)
```

---

## 2. Ownership

| Data | Owner | Lifetime |
|---|---|---|
| `PackedEntry` (per slot) | `entries_` vector, indexed by `slotId` | Vector entry persists (slot recycled) for the table's whole lifetime; logically dead once `live=false` |
| Slot id | `Scope::liveSlots` (creator scope) + `freeSlots_` (once released) | A slot is owned by exactly one of: a live scope's `liveSlots`, or `freeSlots_`. Never both. |
| Interned string | `pool_` vector, refcounted via `poolRefCount_` | Refcount-owned; multiple slots (same string, different scopes) can hold refs simultaneously |
| Compressed block member | `Block::members`, referenced via `CompressedRef{blockIndex, slotInBlock}` | Owned by the block; individual members are never removed, only whole-block-freed |
| Block | `blocks_` vector + `blockFreeList_` | Recycled the same way slots are, once `liveCount` hits 0 |

No raw pointers are held across mutating calls; all cross-references are
vector indices (`slotId`, `blockIndex`/`slotInBlock`, `poolIndexOf_`), so
`entries_`/`blocks_`/`pool_` reallocation (vector growth) cannot dangle a
reference the way a raw pointer into a growing vector would. This is a
structural invariant, not something that needs separate testing.

## 3. Allocation / deallocation

- **Slots**: free-list-backed (`freeSlots_`, LIFO). `allocSlot()` reuses
  before growing `entries_`. This is the direct fix for the V1 finding
  (append-only `entries_`, 297MB / 73% of measured Zephyr heap from an
  8x-inserts-to-live-names ratio never being reclaimed).
- **Blocks**: same free-list pattern (`blockFreeList_`), coarser granularity
  (whole block, not single slot).
- **Interned pool slots**: index-stable, refcounted, **not** compacted on
  refcount 0 (documented trade-off, matches V1's `InternedSymbolTable`).
  Bounded by unique-name count, not insert count.
- **`everSeenRep_`** (repeat-detection set): grows monotonically, bounded by
  unique-name count, **never reclaimed** by design (mirrors V1's `seen_`,
  but V2 actually charges its bytes to the tracker; V1 did not).

## 4. Metadata layout

`PackedEntry` is a fixed 40-ish-byte POD (`sizeof(PackedEntry)` is used
directly as the memory-model cost, not a hand-picked constant -- see
`kSlotOverhead`). Representation-specific payload lives in parallel vectors
indexed by the same `slotId` (`poolIndexOf_`, `compressedRefOf_`), so
`PackedEntry` itself stays small and uniform regardless of which
representation a slot currently uses -- this is what makes promotion
(COMPRESSED -> INTERNED) a cheap in-place representation flip rather than a
slot reallocation.

## 5. Index layout

Per-scope open addressing (`ScopeIndex<HashFn>`, robin-hood probing,
power-of-two capacity, 0.70 max load, doubling growth). Stores only
`{fp, slotId, dist, occupied}` -- 4+4+4+1 bytes per slot, not a full string
or a `unordered_multimap` bucket node. Supports single-key deletion via
standard backward-shift (V1's `RobinHoodSymbolTableT` explicitly does not
implement deletion, since V1 only ever discarded whole scopes at once -- V2
needs single-key delete for same-scope redeclaration while the rest of a
long-lived scope's index stays live).

**Scope lookup order**: `scopes_.rbegin()` to `rend()` -- innermost first --
which is what gives correct lexical shadowing (§7 below).

## 6. Compression layout (block front-coding)

Fixed-size blocks (`cfg.blockSize` members), independent re-anchor stride
(`cfg.anchorInterval`) that decouples reconstruction-depth bound from
reclaim granularity (V1 conflated both into a single `reanchorInterval`).
Addressing is direct: `(blockIndex, slotInBlock)`, no linked `prevIndex`
chain. Reconstruction from any slot walks back at most
`slotInBlock % anchorInterval` steps to the nearest anchor, then decodes
forward -- bounded, not unbounded. Reclaim is whole-block: individual
members are never removed (doing so would corrupt every later member's
front-coding relative to its predecessor), only `liveCount` decrements;
physical free happens in one step when `liveCount` reaches 0.

**Documented, non-hidden trade-off**: batching reclaim to "whole block dead"
means a block can hold dead bytes for longer than V1's per-node reclaim
would, if one long-lived member keeps an otherwise-dead block pinned. This
is the cost of eliminating V1's "only the chain tail is reclaimable"
problem, not a new bug -- see the header comment above `struct Block`.

## 7. Cache / promotion / representation transitions

There is no separate lookup cache in V2 (V1's `LookupCache` LRU layer is not
part of this file -- V2's ScopeIndex probing IS the fast path). Cache-related
correctness risk therefore reduces to: does shadowing correctly invalidate a
previously-resolved binding? Verified by construction -- `resolve()` always
re-walks `scopes_` from innermost outward and returns whatever the *current*
index state says, with no cached last-resolved-id. `tests/differential_test.cpp`'s
fuzz harness (`test_differential_fuzz`, 200 random traces with interleaved
`enterScope`/`exitScope`/insert/resolve) exercises `SymTabV2` against a
reference `std::map`-backed model and asserts full agreement, which covers
shadowing/nested-scope/redeclaration transitively. There is **not yet** an
explicit, named shadowing regression test targeting `SymTabV2` specifically
(the named `test_resolve_respects_shadowing()` test targets V1's
`BudgetSym`) -- flagged as a gap, see §10.

Representation transitions: COMPRESSED -> INTERNED via `maybePromote()`,
gated on `accessCount >= hotAccessThreshold`, using only past-observed
access counts (no future/oracle information -- see the "ONLINE vs ORACLE"
comment on `maybePromote()`). As of P0.4, an explicit, opt-in
INTERNED -> COMPRESSED demotion path also exists (`demote()`, driven by
`runMaintenance()`, gated on `PolicyConfigV2::coldIdleEpochs` and
`PackedEntry::wasPromoted` so a `decide()`-native INTERNED entry is never
force-demoted) -- see `docs/hot_cold_design.md` for the full design and
`tests/symtab_v2_compressed_test.cpp` for its correctness tests. INLINE does
not participate in either transition (INLINE never becomes anything else,
by construction: `decide()` only offers INLINE for names below
`inlineMaxLen`, and length never changes for a given name).

## 8. Memory accounting

`MemoryTracker` (`tracker_`) is charged and reclaimed at every state
transition: slot alloc/free (`kSlotOverhead`, `sizeof(PackedEntry) +
sizeof(uint32_t)` -- the real struct size, not an estimate), interned pool
add/refcount-drop, compressed block member add / whole-block reclaim,
`everSeenRep_` first-registration. Two under-charge bugs were found and
fixed during P0-3 measurement (see `internName()`'s comment): a pool slot
reused after refcount hits 0 was not being re-charged on the next intern of
the same string. The same class of bug was found and fixed in V1 as well.
This is exactly the kind of accounting-invariant violation P0.1's audit is
meant to catch -- see `results/memory_audit_v2.csv`'s `first_cycle_residual`
vs `second_cycle_residual` columns, which are the regression check for
"does releasing then re-inserting return to the same residual" (both
`tests/symtab_v2_compressed_test.cpp::test_whole_block_reclaim_to_zero` and
the audit binary assert this).

**Modeled vs measured**: `tracker_.current()` is the *modeled* (component-
accounted) number. `results/memory_audit_v2.csv`'s `measured_heap_bytes_*`
columns come from a separate real allocator hook (`heap_counter.hpp`), not
from `tracker_`. The two are compared (`measured_over_modeled_insert`
column) but never conflated -- this matches CLAUDE_RESEARCH.md §9.1's
requirement to keep ACTUAL MEASURED MEMORY and MODELED/COMPONENT ACCOUNTING
visibly distinct.

**ECC review H1 fix (previously a gap)**: `ScopeIndex`'s own backing
storage (`ScopeIndex::byteFootprint()`) was defined but never charged to
`tracker_` at all -- `results/ecc_review.md` finding H1. Fixed: the
constructor and `enterScope()` now charge each scope's initial index
allocation, `insert()` charges the exact delta whenever a scope's index
grows (doubling), and `exitScope()` reclaims the scope's full footprint
before popping it. `results/memory_audit_v2.csv` was regenerated after this
fix -- every `SymTabV2` row's `modeled_bytes_after_insert` increased (the
previously-uncharged index memory is now included), most visibly on
`nested-scopes` (many small scopes): `measured_over_modeled_insert` dropped
from 1.65 to 1.35, the largest correction of any dataset, confirming this
was a real, non-trivial omission for scope-heavy workloads. The
`measured_heap_bytes_*` columns are unaffected (they were never wrong --
they come from the independent allocator hook, not `tracker_`), and every
non-`SymTabV2` row (Conventional/Interned/RobinHood) is byte-for-byte
identical to the pre-fix CSV (confirmed via diff), since only `SymTabV2`'s
accounting changed.

## 9. Pointer / reference validity

No raw pointers cross a mutating call boundary in the public API. All
cross-structure references are integer indices (`slotId`, `CompressedRef`,
pool indices), which stay valid across vector reallocation (unlike
`&entries_[i]`, which `insert()` deliberately takes as a local reference
(`PackedEntry& e = entries_[slotId];`) only *after* `allocSlot()` has
already grown `entries_` for this call, and does not hold that reference
across any further `allocSlot()`/`insert()`/`materialize()` call that could
reallocate the vector -- confirmed by inspection: `materialize()` is called
with `e` as a reference parameter, and nothing inside `materialize()` calls
`allocSlot()` or otherwise touches `entries_`'s size).

## 10. Outstanding gaps identified by this audit (feed into P0.2/ECC review)

1. ~~**No explicit named SymTabV2 shadowing test.**~~ FIXED in this pass:
   `tests/symtab_v2_compressed_test.cpp::test_shadowing_nested_scope_and_absent_symbol`
   now covers shadowing, nested-scope shadow visibility, and absent-symbol
   lookups directly against `SymTabV2` on COMPRESSED-tier names (exercising
   the fp8/reconstruction path), passing clean under ASan+UBSan.
2. ~~**No explicit "absent symbol" / "nested scope" named tests**~~ FIXED by
   the same test added in item 1.
3. ~~**Block compression measurement (P0.3) is not yet done**~~ FIXED:
   `src/block_compression_sweep_main.cpp` sweeps `blockSize` in
   `{4,8,16,32,64,128}` x `anchorInterval` in `{2,4,8,16,32}` (anchor <=
   block) over two datasets (`high-prefix-similarity`, `random-long`),
   measuring modeled memory, reconstruction count/steps/mean-depth (new
   `SymTabV2::reconstructionCount()`/`reconstructionStepsTotal()` counters),
   and cold/hot lookup latency percentiles (p50/p95/p99/mean). Output:
   `results/block_compression_sweep.csv` (48 rows).
   **SUPERSEDED VALUES NOTICE**: this item originally cited
   `high-prefix-similarity` memory `630995 -> 581558` bytes and `random-long`
   `704738 -> 704700` bytes, with `reconstruction_count=8000` for n=4000.
   Those specific numbers are **superseded**, not silently replaced -- they
   were correct as measurements of what the tool computed at the time, but
   the tool itself had two bugs the ECC review found (`results/ecc_review.md`
   H1: `SymTabV2`'s `ScopeIndex` memory was uncharged, shifting every memory
   number upward once fixed; H2: the sweep's own pre-pass sanity check
   double-counted reconstructions, inflating `reconstruction_count` 2x).
   Current, corrected values (block=4/anchor=2 vs block=32/anchor=32,
   n=4000): `high-prefix-similarity` memory `762067 -> 712630` bytes (still
   a real, measurable reduction from a larger anchor interval, just at a
   different absolute baseline now that index memory is correctly included);
   `random-long` memory `835810 -> 835770` bytes (still negligible, same
   qualitative finding: front-coding barely helps without shared prefixes);
   `cold_reconstruction_count` is now correctly `4000` (not `8000`) for
   every n=4000 row, and `cold_mean_reconstruction_depth` (unaffected by
   either bug, since it's a ratio of two equally-inflated/deflated
   quantities) is unchanged: `0.5 -> 15.5` across the same anchor range. The
   qualitative conclusion is unchanged and was never dependent on the bugs:
   larger `anchorInterval` trades memory for reconstruction depth/latency,
   the size of that trade is prefix-similarity-dependent, and `blockSize`
   alone (holding `anchorInterval` fixed) has no measurable effect on
   reconstruction depth or latency, confirming the design's decoupling of
   reclaim granularity from the reconstruction-depth bound (section 6).
4. ~~**Hot/cold tiering (P0.4) has no design doc**~~ FIXED:
   `docs/hot_cold_design.md` documents the hot threshold (promotion,
   pre-existing), the cold threshold (demotion, newly implemented --
   `PolicyConfigV2::coldIdleEpochs`, `SymTabV2::runMaintenance()`/`demote()`),
   scope interaction, and memory/lookup transition costs, with four
   correctness tests in `tests/symtab_v2_compressed_test.cpp`.
5. **No latency measurement exists for the INLINE/INTERNED tiers, or for
   SymTabV2 end-to-end across a realistic mixed-representation workload** --
   the P0.3 sweep above isolates the COMPRESSED tier specifically (by
   design, to isolate its own parameters). A full latency-constrained
   Pareto claim (CLAUDE_RESEARCH.md §12) still requires benchmarking
   SymTabV2 end-to-end (INLINE+INTERNED+COMPRESSED+promotion+demotion mixed,
   as a real workload would exercise it) against the other tables, which is
   a later phase (Benchmark infrastructure / Pareto optimization), not part
   of P0.3's block-compression-specific measurement. This item survives
   unchanged through the ECC-fixes pass below (see item 6).

Phases P0.1-P0.4 were completed in this pass, then subjected to an
adversarial ECC review (`results/ecc_review.md`, section 8): 1 CRITICAL,
3 HIGH, 4 MEDIUM, 3 LOW findings. All CRITICAL/HIGH findings, plus the
cheap and directly-relevant MEDIUM findings, are now fixed:

6. ~~**C1 (CRITICAL): stale `wasPromoted`/`lastAccessEpoch` on slot
   reuse**~~ FIXED: `insert()` now explicitly resets `wasPromoted`,
   `lastAccessEpoch`, `poolIndexOf_[slotId]`, and `compressedRefOf_[slotId]`
   for every reused slot, not just the fields a given representation's
   `materialize()` happens to write. Regression test:
   `tests/symtab_v2_compressed_test.cpp::test_slot_reuse_does_not_inherit_promotion_state`,
   which forces exactly the promote -> release -> free-list reuse ->
   natively-INTERNED-insert -> `runMaintenance()` sequence the review
   identified, and was verified (by temporarily reverting the fix in a
   scratch copy) to actually fail without the fix and pass with it.
7. ~~**H1 (HIGH): `ScopeIndex` memory never tracked**~~ FIXED -- see the
   "ECC review H1 fix" note in section 8 above.
8. ~~**H2 (HIGH): sweep tool double-counts reconstructions**~~ FIXED -- see
   item 3's "SUPERSEDED VALUES NOTICE" above.
9. ~~**H3 (HIGH): demotion had no ablation evidence**~~ FIXED:
   `src/demotion_experiment_main.cpp` runs a dedicated demotion ON/OFF
   comparison (`results/demotion_experiment.csv`) -- see
   `docs/hot_cold_design.md` section 9 for the honest, mixed result
   (modest memory savings, non-trivial latency cost, and a genuine
   promotion/demotion thrashing failure mode on a sustained-hot workload).
10. **M1-M4** (fingerprint independence overstatement, stale
    `compressedRefOf_` after promotion, `runMaintenance()` complexity
    mischaracterization, unguarded `blockSize` truncation) all fixed with
    corrected comments, a symmetric reset in `maybePromote()`, and an
    `assert` -- see `results/ecc_review.md` section 4 for the itemized list.
    L1-L3 were left as-is (documentation-only findings, no code risk).

Next: benchmark infrastructure (CLAUDE_RESEARCH.md's execution order,
section 6), with demotion's default (`coldIdleEpochs = 0`) kept off pending
the outcome documented in `docs/hot_cold_design.md`.
