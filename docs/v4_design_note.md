# Phase 4 — V4 Design Note: Representation-Conditional Metadata

## Principle

"A symbol should only pay for metadata required by its active representation."
Applied specifically to the one candidate approved for this phase: eliminate
per-symbol fixed metadata for INLINE entries that is only needed by the
hot/cold promotion/demotion machinery.

## Current V3 per-symbol cost, and exact sources

`PackedEntry` (measured `sizeof` = 32B) fields:

| Field | Bytes | Who actually reads it |
|---|---|---|
| `declId` | 4 | All representations (declaration identity) |
| `scopeId` | 2 | All representations (scope-exit reclamation) |
| `accessCount` | 2 | **Only** `maybePromote()` (COMPRESSED->INTERNED trigger) |
| `lastAccessEpoch` | 4 | **Only** `runMaintenance()`'s demotion check, and only for entries with `wasPromoted()==true` |
| `nameLen` | 2 | All representations (equality pre-filter, avoids reconstructing COMPRESSED just to compare length) |
| `typeId` | 1 | All representations |
| `representation` | 1 | All representations (tag) |
| `flags` | 1 | All representations (live/promoted bits) |
| `payload` (union) | 12 | All representations (union already amortizes inline-bytes vs 4B index) |

Traced through the actual control flow (`symtab_v3.hpp:302-331` `resolve()`,
`582-592` `maybePromote()`, `317-328` `runMaintenance()`): `accessCount` is
incremented on every `resolve()` call for every representation, but is only
ever *read* by `maybePromote()`, which returns immediately unless
`representation == COMPRESSED_REP`. `lastAccessEpoch` is written on every
`resolve()` but only *read* by `runMaintenance()`, which skips every entry
except `wasPromoted() && representation == INTERNED_REP`. INLINE entries
never promote or demote in the existing policy (`decide()` only ever assigns
INLINE at insert time; there is no code path that transitions an entry into
or out of INLINE after creation). **So for INLINE entries, and for
directly-inserted INTERNED entries that were never promoted from COMPRESSED,
`accessCount` and `lastAccessEpoch` are write-only dead weight -- 6 of 32
bytes (18.75%) paid by every symbol for a feature only a minority
representation-track ever uses.**

`sizeof` check (compiled and measured, g++ -std=c++14):
`PackedEntryV3` = 32B (confirmed). Removing `accessCount`+`lastAccessEpoch`
from the core struct: `CoreEntryV4` = **24B** (confirmed by compiler, not
hand math -- a 25% reduction from V3, 45% reduction from V2's 44B).

## Proposed layout

**`CoreEntry`** (24B, paid by every symbol, every representation):
`declId(4) + scopeId(2) + nameLen(2) + typeId(1) + representation(1) +
flags(1) + payload union(12B: inlineBytes[12] | poolIndex(4) |
compressedRef(6))`.

**`HotMeta`** (8B logical payload: `accessCount(2) + lastAccessEpoch(4)`,
padded to 8), stored in a **sparse side map** (`std::unordered_map<uint32_t
/*slotId*/, HotMeta>`), populated **only** when an entry is created as
COMPRESSED, or transitions through the promotion/demotion machinery. INLINE
entries and directly-inserted INTERNED entries (never promoted) never get a
`HotMeta` row and pay nothing beyond the 24B core. This directly matches the
approved candidate: "tagged storage where additional metadata exists only
for INTERNED/COMPRESSED entries" -- specifically, only for the subset of
INTERNED/COMPRESSED entries actually subject to hot/cold tracking, which is
narrower than "every INTERNED/COMPRESSED entry."

Cost of a `HotMeta` row is tracked honestly via the same
`kMapNodeOverhead = 68` constant V3 already uses for `liveSeenRep_` (a
hash-map node of comparable shape) -- **not hidden**: total tracked cost per
HotMeta-holding entry is `sizeof(HotMeta) + kMapNodeOverhead` ~= 76B, charged
on top of the 24B core, so a COMPRESSED entry does not get cheaper overall --
it is exactly as expensive as before (probably slightly more, due to the map
node tax), but it no longer taxes the INLINE majority to pay for a feature
it never uses.

### Expected bytes per representation

| Representation | V3 (32B always) | V4 |
|---|---|---|
| INLINE | 32B | **24B** (core only, no HotMeta ever) |
| INTERNED (direct, never promoted) | 32B + pool-node cost | **24B** + pool-node cost (no HotMeta) |
| INTERNED (promoted from COMPRESSED) | 32B + pool-node cost | 24B + pool-node cost + 76B HotMeta (carried over from its COMPRESSED phase, needed for demotion timing) |
| COMPRESSED | 32B + compressed-member cost | 24B + compressed-member cost + 76B HotMeta |

Net effect depends on the representation mix. Per `count_inline` /
`count_interned` / `count_compressed` columns in
`data/real_world_benchmark.csv`, INLINE is the dominant representation in
every one of the 20 corpora (typically 55-75% of live symbols), so the
aggregate effect should be a net reduction -- but COMPRESSED/promoted-INTERNED
entries individually get *more* expensive (76B heavier), not lighter. This
is the honest trade the design principle demands: hot/cold tracking cost is
now visible and isolated to the population that actually needs it, instead
of amortized invisibly across everyone.

## Lookup path per representation (unchanged from V3, core-struct only)

- INLINE: `memcmp` against `payload.inlineBytes`, no side-table touch.
- INTERNED: pool string compare via `payload.poolIndex`, no side-table touch
  unless `resolve()` needs to bump access tracking (see below).
- COMPRESSED: fingerprint-byte prefilter, then `reconstructMember` walk;
  `maybePromote` now looks up `HotMeta` by `slotId` (present by construction
  for every live COMPRESSED entry) to read/increment `accessCount`.

## Promotion path

`maybePromote(slotId, name)`: unchanged trigger condition, but reads/writes
`accessCount` through the `HotMeta` map instead of the core struct. On
promotion (COMPRESSED->INTERNED), the `HotMeta` row is **kept** (not erased)
because `runMaintenance()` still needs `lastAccessEpoch` to decide demotion
eligibility for `wasPromoted()` entries -- the row transfers ownership
conceptually from "COMPRESSED promotion tracking" to "INTERNED demotion
tracking," same underlying map entry.

## Scope-exit path

`releaseSlot(slotId, e)`: after reclaiming `costOf(e)` (now 24B not 32B) and
the existing `liveSeenRep_`/pool/block bookkeeping, additionally erase the
`HotMeta` row for `slotId` if present, reclaiming `sizeof(HotMeta) +
kMapNodeOverhead` from the tracker. This is the one place V3 didn't need to
touch a second structure and V4 does -- a genuine, disclosed extra cost of
the split, paid only when a COMPRESSED/promoted-INTERNED entry's scope ends.

## Expected latency implications

- INLINE lookup: **no change** -- same single `memcmp`, no extra indirection.
- INTERNED (never promoted) lookup: **no change** -- same pool compare, no
  `HotMeta` touch (only `resolve()`'s post-match access bump is skipped for
  these, since they can never demote -- matches V3's runMaintenance() gate
  exactly, just made explicit by absence-of-row rather than
  wasPromoted()==false).
- COMPRESSED / promoted-INTERNED lookup: **one extra hash-map probe** per
  `resolve()` call to reach `HotMeta` (previously an in-struct field read).
  This is a real latency cost, isolated to the minority representation-track
  that already pays the most (reconstruction walk for COMPRESSED). Given
  COMPRESSED/promoted-INTERNED is a small fraction of live symbols in every
  corpus, the *aggregate* p95 should not move much, but per-entry cost on
  that specific path is higher than V3, not lower. This must be measured,
  not assumed -- see benchmark results in docs/v4_evaluation.md.

## What was deliberately NOT done

A stronger cut -- moving the inline-bytes payload itself out of `CoreEntry`
into a densely-packed side array with its own slot-ID space (eliminating the
union entirely, core to ~12B) -- was considered and rejected for this pass: it
requires a second independent allocator/free-list domain synchronized with
`entries_`, which is a materially higher-risk change to scope-exit
reclamation and shadowing correctness than the time budget for this phase
allows to implement *and* verify safely. It is recorded as the recommended
next architectural iteration if V4's measured results justify further work
(see recommendation section of docs/v4_evaluation.md).
