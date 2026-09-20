# Phase 2 — Ordering Analysis: Conventional vs Interned vs V3

## Direct answers

**Theoretical per-unique-name memory equations.** Let `L` = identifier length
in bytes, `k` = number of live occurrences (declarations) that share one
unique name, `S_meta` = fixed per-symbol metadata (~24B, `SymbolMeta`),
`H` ≈ 16B = heap-allocation header overhead, `C_idx` = Interned's fixed
per-occurrence index-entry cost (`kIndexEntryOverhead` ≈ 28B: 4B pool index +
`SymbolMeta`), `C_pool(L)` = Interned's one-time-per-unique-name pool
infrastructure cost, `S_slot` = V3's fixed per-symbol slot (`kSlotOverhead`
= 32B), `F` ≈ 4–8B = V3's per-scope fingerprint-index entry.

```
S_str(L)      = 0            if L <= 15   (SSO — string lives inside the map node)
              = L + 1 + H    if L  > 15   (heap allocation)

M_conv(k,L)  ≈ k · (S_meta + S_str(L))

C_pool(L)     = (L + 9)                    # pool_ vector<string> entry
              + (L + 9 + kPoolNodeOverhead)  # poolLookup_ unordered_map<string,int> node
              ≈ 2L + 86

M_int(k,L)   ≈ k · C_idx + C_pool(L)

M_v3(k,L)    ≈ k · (S_slot + F) + [pool/compressed overhead if not INLINE]
```

**Why Interned loses under the current model (quantified).** `InternedSymbolTable`
stores every unique string **twice**: once in `pool_` and once again as the
*key* of `poolLookup_` (an `unordered_map<string,int>`, itself paying
`kPoolNodeOverhead` ≈ 68B of node overhead). That double-storage is a fixed
tax paid once per unique name, on top of a 4B-larger index entry paid on
*every* live occurrence (`C_idx` ≈ 28B vs Conventional's `S_meta` ≈ 24B).
For any name ≤ 15 bytes (SSO range), Conventional pays `S_meta` with **zero**
extra string cost, so Interned's flat 4B/occurrence tax is never recovered —
there is no break-even `k` at all. This is not a corner case: `frac≤15B` is
0.59–0.76 across all 20 real corpora (`data/real_world_corpus_characterization.csv`),
i.e. most identifiers in every corpus are in the SSO range where Interned
*cannot* win by construction.

**Break-even condition for Interned to beat Conventional** (derived by
solving `M_int(k,L) < M_conv(k,L)`):

```
k > C_pool(L) / (S_meta + S_str(L) - C_idx)
```

This only has a positive solution when `S_meta + S_str(L) > C_idx`, i.e.
`L > 15` (S_str must be nonzero — Conventional must itself be forced to
heap-allocate). For `L` just above 15: `k_breakeven ≈ 116/28 ≈ 4.1`. As
`L → ∞`, `k_breakeven → 2`. **So Interned only has a chance when names are
both long (>15B) and duplicated at least ~2–4×.**

**Closest real corpus.** None of the 20 corpora cross this condition — the
measured `Interned/Conventional bytes-per-unique-symbol` delta is positive
(Interned costs more) on **20/20** corpora. The closest is **Nginx**
(repeat_rate 0.84, mean length 7.7B) at **+21.4%** — still a clear loss,
because Nginx's names are short (well inside SSO), so the `L>15` precondition
for any break-even isn't met regardless of how high the duplicate ratio is.
protobuf-generated-cpp has the longest mean identifier length (18.31B,
already rejected in Phase 0 screening for unrelated reasons) but still shows
**+79.7%** — its aggregate mean crossing 15B doesn't mean most *individual*
occurrences do, and it was already flagged as structurally atypical
(declare/use order doesn't preserve namespace-prefix locality).

**Break-even condition for V3 to beat Conventional, and what blocks it.**
V3's fixed floor (`S_slot + F` ≈ 32–40B) is *already higher* than
Conventional's SSO floor (`S_meta` ≈ 24B) before any string cost is counted,
and V3 additionally pays reconstruction cost on every lookup (walking the
inline/pool-chain/compressed-block representation to confirm equality — CPU,
not memory, but it's what caps the achievable memory-reduction policy: the
adaptive representation selector must avoid promoting too aggressively or
latency blows the 1.25× constraint). Structurally, V3's break-even `k` is
strictly larger than Interned's for the same `L` (worse starting floor, same
shape of inequality) — since Interned itself never breaks even on any real
corpus, **V3 cannot either**, which matches the measured 20/20 loss exactly.
This is a fixed-cost problem, not a tuning problem: shrinking `S_slot`
further (V2→V3 already went 44B→32B) helps only at the margin; the
qualitative conclusion (V3 loses to Conventional in this corpus regime)
would only flip by removing the *existence* of a fixed per-symbol slot or
the reconstruction cost, not by shrinking either further.

## Measured ordering (20/20 real corpora, `data/real_world_benchmark.csv`)

All three candidate orderings collapse to the same one in every corpus tested:

```
Conventional  <  V3  <  V2  <  Interned  <  V1(BudgetSym)   (heap, ascending)
```

`Conventional < Interned < V3` and `V3 < Interned < Conventional` do **not**
occur in any of the 20 corpora. `V3 < Conventional and V3 < Interned` also
does not occur (V3 beats V2 and Interned, but not Conventional).

| Corpus (closest to Interned break-even) | Int/Conv delta | repeat_rate | mean_len | frac≤15B (from characterization) |
|---|---|---|---|---|
| Nginx | +21.4% | 0.842 | 7.70 | high (short names) |
| Lua | +26.1% | 0.750 | 5.24 | high |
| SQLite | +30.9% | 0.778 | 6.76 | high |

No corpus is within an order of magnitude of the break-even boundary derived
above; this is a structural gap, not a measurement-noise gap.

## Candidate architectural changes (options only — not implemented)

| Candidate | What it removes | Est. memory effect | Est. latency effect | Expected ordering |
|---|---|---|---|---|
| Name store separated from symbol metadata (compact handle in symbol record, names in a flat arena keyed by handle, no per-name hash lookup) | Removes the *second* hash probe (Interned's `poolLookup_`, V3's fingerprint reconstruction) by making the handle itself the identity — no string comparison needed on hit | Could remove `C_pool`'s doubled storage (~2L+86 → ~L+9, roughly half); comparable or below Conventional's SSO floor for names >15B | Should approach Conventional's single-probe latency, since no reconstruction/second-probe is needed for equality | Plausibly `V4 ≈ Conventional` for names >15B, still `>` for names ≤15B (arena has no SSO-equivalent) |
| Scope-local (not global) name dictionaries | Removes the "never compacted" global-pool growth; bounds dictionary size to the live scope's working set | Reduces peak heap on corpora with many disjoint short-lived scopes; no effect on corpora with mostly global-scope symbols (most C code) | Neutral-to-positive (smaller table → faster probe) | Helps deeply-scoped/short-scope-lifetime workloads (e.g. FFmpeg, redeclaration-heavy embedded code); little effect on flat-namespace corpora |
| Eliminate per-symbol metadata for representations that don't need it (i.e. don't pay `S_slot`/`F` for INLINE symbols at all — store them exactly like Conventional does, only add slot overhead for INTERNED/COMPRESSED entries) | Removes V3's fixed-floor disadvantage for the ~60–76% of identifiers that are short/INLINE in every corpus | Could bring V3's floor for INLINE entries down to ~Conventional's, since INLINE is already the majority representation | No latency effect for INLINE path (still hashes directly); no regression for INTERNED/COMPRESSED paths | Most promising: could flip `V3` below `Conventional` in aggregate if the per-corpus mix is INLINE-dominated (it is, per `count_inline` columns in the benchmark CSV) |
| Dictionary/front-coded compression over *simultaneously-live* names only (not all ever-seen names) | Reduces `C_pool`-style overhead by amortizing prefix storage across the live working set, bounded by scope lifetime | Could reduce V3's COMPRESSED-path cost further, but COMPRESSED is already the minority representation — small aggregate effect | Neutral if block size is bounded; risks regressing reconstruction cost if blocks grow unbounded | Marginal — addresses a representation that's already a small fraction of symbols in these corpora |
| Global/shared string arena (single flat byte buffer, no per-string map node) instead of Interned's `pool_` + `poolLookup_` pair | Removes the doubled-storage tax (`2L+86` → `L+const`) that is the single biggest driver of Interned's loss | Could roughly halve `C_pool(L)`, which directly lowers the `k_breakeven` derived above | Requires a way to look up existing entries without a string-keyed map — likely still needs *some* index (hash of content), so latency effect depends on implementation, not free | Best positioned to make Interned itself competitive with Conventional for L>15 corpora, though none of the 20 tested corpora have enough L>15 mass to benefit |

Of these, **"eliminate per-symbol metadata for INLINE" (V4 idea)** is the
strongest candidate: it directly attacks the fixed-floor disadvantage shown
above to be the actual, quantified cause of V3's loss, and it targets the
representation that dominates every real corpus in this dataset (INLINE is
the majority `count_inline` value in every row of `data/real_world_benchmark.csv`).
It is presented here as an option only; no implementation was performed.
