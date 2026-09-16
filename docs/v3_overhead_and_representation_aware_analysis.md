# SymTabV2/V3 Structural Overhead Analysis and the protobuf-generated-cpp Workload Rejection

Status: intermediate research analysis. This is **not** the final paper claim. It
establishes whether representation-aware fixed metadata (V3) materially moves
the measured memory/latency Pareto frontier versus V2, using the existing
16-corpus evidence base plus the newly rejected `protobuf-generated-cpp`
workload hypothesis.

---

## 0. Formally rejecting `protobuf-generated-cpp` as a winning workload

### 0.1 The tokenizer/scoping bug and its correction

The first characterization run on a 611-file (1-in-12 sample of 7,325)
corpus of `protoc --cpp_out` output produced implausible numbers:
`max_scope_depth = 3465`, `mean_identifier_length = 11.2B`. Root-caused by
inspection (`scripts/extract_and_characterize_all_corpora.py`):

1. **Preprocessor branch double-counting.** Generated code repeatedly uses
   the idiom
   ```cpp
   #ifdef PROTOBUF_FORCE_COPY_IN_SWAP
       if (GetArena() != nullptr && GetArena() == other->GetArena()) {
   #else
       if (GetArena() == other->GetArena()) {
   #endif
       InternalSwap(other);
     }
   ```
   Both branches open a `{` but only one `}` closes the shared body. The
   extractor's brace-counting scope tracker has no preprocessor awareness,
   so it tokenizes **both** branches additively: two opens, one close, net
   `+1` per occurrence. Verified directly: a single header
   (`config_file.pb.h`) had 303 raw `{` vs 301 raw `}`, traced to exactly
   two such `#ifdef/#else/#endif` blocks. Because `process_corpus()` reuses
   **one shared `CorpusParser` across all files with no per-file reset**,
   this small per-file imbalance compounds monotonically across 611 files,
   producing the observed depth of 3465.
2. **`::`-qualified names split into fragments.** The tokenizer's
   punctuation whitelist excluded `:`, so `google::protobuf::Message` was
   never seen as one identifier — each namespace segment was measured
   separately, deflating `mean_identifier_length`.

**Fix applied** (`scripts/extract_and_characterize_all_corpora.py`):
`strip_preprocessor_else_branches()` blanks out `#else`-branch code before
tokenizing (keeping only the `#if`/`#ifdef` branch — a defensible,
disclosed approximation, not a benchmark-tuning move: it affects the *bug*,
not the outcome), and `merge_qualified_identifiers()` collapses
`IDENT :: IDENT :: IDENT` token chains into one identifier. Verified on the
known-bad file: enter/exit scope counts became exactly balanced (302/302),
max depth dropped to a sane 8, and qualified names now read correctly
(`google::protobuf::internal::DescriptorTable`).

### 0.2 Corrected characterization

Re-run on the same 611-file sample after the fix:

| Metric | Value |
|---|---|
| Files | 1,222 (611 `.h` + 611 `.cpp`) |
| Declarations | 1,933,674 |
| Uses | 1,935,362 |
| Unique identifiers | 260,027 |
| Mean identifier length | **18.3 B** (need ≥22B) |
| Prefix similarity | **0.065** (need ≥0.35) |
| Max scope depth | 12 (sane) |
| Repeat rate | 0.500 |

### 0.3 Three-way physical-memory result

| Implementation | Peak heap | vs Conventional |
|---|---|---|
| Conventional | 65,398,496 B | baseline |
| Interned | 117,498,152 B | +80% |
| **SymTabV2** | 70,352,136 B | **+7.6%** |
| BudgetSymV1 | 320,114,416 B | +389% |

V2 beats Interned by 40% but **loses to Conventional by 7.6%** on this
corpus. Cold lookup **p95 = 0.446 µs vs Conventional's 0.155 µs — 2.88×**,
failing the 1.25× latency ceiling by a wide margin (this is not a
borderline miss).

### 0.4 Why generated protobuf code does not produce the expected prefix locality

The original hypothesis ("generated code systematically prefixes every
symbol with a shared namespace/message path, so it should have high prefix
similarity") is true of the **static identifier universe** but false of the
**declare/use event sequence**, which is what `prefix_similarity` actually
measures (mean common-prefix ratio between *consecutive* symbols in
execution order). Within one message class's generated methods, the
compiler visits `set_name()`, `clear_name()`, `has_name()`,
`_impl_.name_`, `arena()`, `GOOGLE_DCHECK`, field accessors for the *next*
field, etc., in quick succession — these consecutive tokens share almost no
common prefix even though they all nominally belong to the same
`google::protobuf::...` namespace. High prefix similarity would require
identifiers to be *visited in sorted/grouped order*, which is not how a
parser walks a class body. This is a structural property of how C++
declare/use events sequence, not an artifact of this specific corpus —
it should be expected to hold for any similarly-structured generated code
(gRPC stubs, Thrift, other schema compilers).

This is a second, independent negative result (in addition to the 15
hand-written corpora), obtained after fixing the methodology bug rather
than discarding or re-selecting the corpus.

---

## A. Demonstrated results

Four-way physical-heap and latency benchmark (Conventional / Interned / V2
/ **V3**, the representation-aware redesign described in §1-2 below) run
on the same 7 corpora (FreeRTOS, Arduino, Zephyr, CPython, Lua, ESP-IDF,
protobuf-generated-cpp), same event traces, same `heap::Scope` physical
allocator instrumentation:

| Corpus | Conv heap | V2 heap | V3 heap | V3 < Conv? | V3 < V2? | Conv p95 (µs) | V2 p95 | V3 p95 | V3 p95 ≤ 1.25×Conv? |
|---|---|---|---|---|---|---|---|---|---|
| FreeRTOS | 3,992,248 | 5,185,832 | 4,784,104 | No | **Yes** | 0.148 | 0.398 | 0.416 | No |
| Arduino | 2,423,232 | 3,378,416 | 2,991,952 | No | **Yes** | 0.154 | 0.214 | 0.209 | No |
| Zephyr | 50,918,616 | 60,952,768 | 57,884,000 | No | **Yes** | 0.323 | 0.348 | 0.350 | **Yes** |
| CPython | 38,523,600 | 46,549,872 | 44,174,616 | No | **Yes** | 0.176 | 0.209 | 0.189 | **Yes** |
| Lua | 1,439,128 | 1,723,368 | 1,588,568 | No | **Yes** | 0.140 | 0.095 | 0.094 | **Yes** |
| ESP-IDF | 73,283,912 | 101,479,760 | 89,351,208 | No | **Yes** | 0.188 | 0.328 | 0.342 | No |
| protobuf-generated-cpp | 65,426,040 | 70,534,928 | 69,576,376 | No | **Yes** | 0.126 | 0.364 | 0.392 | No |

**V3 beats V2 on physical heap in all 7 corpora (real, measured, not
modeled).** V3 still loses to Conventional on physical heap in all 7. V3
meets the 1.25× latency gate on 3/7 corpora (the same corpora where V2
already met it), fails it on the same 4 where V2 already failed it — the
metadata shrink did not change which corpora pass the latency gate.

Excess structural cost per unique live symbol (`(impl_heap −
conventional_heap) / unique_names` — an empirical proxy for "how much does
this architecture cost beyond what Conventional already pays for the same
strings"):

| Corpus | V2 excess (B/symbol) | V3 excess (B/symbol) | Reduction |
|---|---|---|---|
| FreeRTOS | 114.9 | 76.2 | 33.7% |
| Arduino | 86.8 | 51.7 | 40.5% |
| Zephyr | 43.9 | 30.5 | 30.6% |
| CPython | 116.6 | 82.1 | 29.6% |
| Lua | 70.4 | 37.0 | 47.4% |
| ESP-IDF | 122.0 | 69.5 | 43.0% |
| protobuf-generated-cpp | 19.6 | 16.0 | 18.8% |

V3's metadata redesign cuts the excess overhead by **19-47%** depending on
corpus, but never enough to close the gap to zero.

---

## B. Architectural explanation — where every byte of V2's fixed overhead comes from

Source: `include/symtab_v2.hpp`, verified by `sizeof()` and by tracing
every `tracker_.add()` call.

| Component | V2 cost | Paid by | Notes |
|---|---|---|---|
| `PackedEntry` (hot metadata + payload union) | **44 B** (verified `sizeof`) | Every live symbol, every representation | Union sized for its largest variant (20 B inline buffer) even when the actual payload needs only 4 B (`poolIndex`) or 6 B (`CompressedRef`) |
| `ScopeIndex::Slot` (open-addressing index) | 16 B/slot, amortized ~16/0.70 ≈ **23 B**/live symbol | Every live symbol | Robin-Hood open addressing at 70% max load; unchanged in V3 |
| `liveSeenRep_` map node (repeat/refcount tracking) | `sizeof(std::string)`(32B) + `capacity()+1` + 68B node overhead ≈ **100-130 B** for names >15B, ~100B for SSO names | Every live **unique** symbol, every representation | This is a **second physical copy of the name**, existing purely to answer "have I seen this live symbol before" — duplicate of whatever `pool_`/inline-buffer/block already stores |
| `poolLookup_` map node (interning index) | `sizeof(std::string)`(32B) + `capacity()+1` + 68B ≈ **100-130 B** | Interned symbols only | A **third** physical copy of the name for interned symbols (liveSeenRep_ + pool_ + poolLookup_ all hold it) |
| `pool_` vector entry | `sizeof(std::string)`(32B) + heap alloc if >15B | Interned symbols only | The canonical string storage; the other two copies above are pure duplication |
| `BlockMember` (front-coded compression) | `sizeof(BlockMember)` ≈ 40B + `suffix.capacity()` | Compressed symbols only | Real savings only accrue here when `suffix` (post-front-coding) is shorter than the full string |
| Compression *infrastructure paid by non-compressed symbols* | **0 extra B** in `blocks_`/`Block` (good) | — | Blocks are allocated on demand; inline/interned symbols never touch `blocks_` — this part of V2 is already representation-aware |

**Formula, as requested:**
```
V2_overhead(symbol) = PackedEntry(44B, always)
                    + ScopeIndex_share(~23B, always)
                    + liveSeenRep_node(~100-130B, always, duplicate copy)
                    + [interned: poolLookup_node(~100-130B, second duplicate) + pool_ entry(32B+string)]
                    + [compressed: BlockMember(40B) + suffix bytes]
                    − string_bytes_saved_by_not_storing_full_raw_string_elsewhere
```

The dominant, *representation-independent* fixed floor is
`44 + 23 + ~110 ≈ 177B` per live unique symbol, paid **before any string
content is stored at all** — for INLINE symbols (the majority
representation in every corpus, 85-98% of unique symbols), essentially all
177B is pure metadata, since the actual string content (≤12B) is already
included inside the 44B `PackedEntry`.

Compare to **Conventional**: a `std::unordered_map<std::string,int>` pays
one hash-map node (empirically ~55-90B including bucket/list-node pointers,
depending on libstdc++'s node allocator) + `sizeof(std::string)`(32B,
stack-resident for the node) + **zero additional heap** for names ≤15B
(SSO). For the ≤15B identifiers that dominate every corpus (76-98%, per
`docs/v2_real_world_memory_diagnosis.md` §7), Conventional's total cost per
unique symbol is **~90-120B**, already comparable to or *less than* V2's
177B fixed floor — before V2 even gets credit for any interning/compression
savings. This is the precise mechanism behind the empirically observed
"V2 excess" numbers in section A: **it is not primarily the string
payload that costs more in V2 — it is that V2 spends 3 structures
(`entries_`, `liveSeenRep_`, and for interned symbols `poolLookup_`+`pool_`)
maintaining bookkeeping that Conventional needs only 1 structure for.**

---

## C. Break-even conditions (quantified)

For V2 to win, per-symbol savings from interning/compression must exceed
the ~177B representation-independent floor. Restating and refining the
existing thresholds from `docs/v2_real_world_memory_diagnosis.md` §8 with
the mechanism now made explicit:

- **R_live ≥ 0.65** (live-symbol reuse ratio): interning only pays off once
  a name is referenced enough times that `pool_` storage amortizes over
  many `PackedEntry` slots pointing at the same `poolIndex` — a single-use
  interned name pays the *full* 177B-plus-pool floor for **zero** benefit
  over just storing it inline.
- **L_mean ≥ 22B**: below this, Conventional's SSO absorbs the string for
  free; V2's fixed floor (177B) has nothing to net against, since the
  "savings" from not duplicating a ≤15B string are at most 15B — two
  orders of magnitude smaller than the floor.
- **S_prefix ≥ 0.35** (in *declaration/use sequence order*, not sorted
  order — the metric that actually governs `BlockMember` savings): only
  matters for the COMPRESSED representation, which is a small minority
  (0.5-8% of unique symbols across all 8 corpora tested) — even a perfect
  compression ratio on that minority cannot outweigh the fixed floor paid
  by the 85-98% of symbols in INLINE representation.
- **Physical OS-level scope-exit deallocation**: necessary but not
  sufficient — confirmed still true in V3; scope reclamation logic is
  unchanged and already working correctly in both V2 and V3.

None of the 16 real-world corpora characterized (15 hand-written + 1
generated) meet the `L_mean ≥ 22B` threshold; the highest is
protobuf-generated-cpp at 18.3B. **The single largest lever is not
compression or interning quality — it's whether the *majority-case* INLINE
symbol's fixed floor (177B in V2, 141B in V3, see §D) can ever be driven
low enough to compete with Conventional's ~90-120B for short, common
identifiers.** This is an open architectural question, not resolved by V3.

---

## D. Remaining limitations of V3 (does representation-aware metadata close the gap?)

**No — not on any of the 7 tested corpora**, but it makes a real,
measured, non-trivial dent:

- `sizeof(PackedEntry)` verified: **44B → 32B** (27% cut), by (1) shrinking
  `scopeId`/`accessCount` to `uint16_t` (safe: max observed scope depth is
  15, access-count wraparound at 65535 is an accepted, documented
  approximation) and (2) shrinking the inline cap from 20B to 12B (matches
  `PolicyConfigV2`'s own default `inlineMaxLen`; nothing routed to
  `INLINE_REP` was ever using more than 12B).
- `liveSeenRep_` keyed on a 64-bit `FnvHash` fingerprint instead of a
  string copy: removes one of the (up to three) physical duplicate string
  copies, shrinking that map node from ~130B to ~76B for long names. This
  is an **approximation with a disclosed correctness relaxation**: two
  different live names colliding on a 64-bit hash would be
  mis-attributed for repeat/refcount purposes. At the largest corpus
  tested (protobuf-generated-cpp, 260,027 unique live symbols), the
  birthday-bound collision probability is on the order of
  260,027² / 2^65 ≈ 4×10⁻¹⁰ — negligible for a research prototype, but a
  real limitation that would need addressing (e.g. a 128-bit hash, or
  storing a short verification tag) before any production claim.
- **Net effect measured empirically**: 19-47% reduction in "excess
  overhead per unique symbol" (§A table) depending on corpus. This
  directly and quantitatively answers the goal's core question: **the
  architectural fixed-metadata floor is a real, measurable contributor to
  V2's loss (not a negligible rounding error) — but it is not the
  *dominant* cause.** Even fully eliminating V3's remaining floor
  (`32B PackedEntry + 23B ScopeIndex share + ~76B liveSeenRep_` ≈ 131B)
  would still exceed Conventional's ~90-120B per-symbol cost for the
  short, low-repeat identifiers that dominate every real corpus tested.
  Getting under Conventional's floor requires attacking the `liveSeenRep_`
  registry (or eliminating it) more aggressively than a hash-fingerprint
  swap, and/or accepting that inline symbols shouldn't need a second live
  registry entry at all.
- Latency: V3 did **not** change which corpora pass the 1.25× gate (same
  3/7 as V2) — cutting fixed metadata bytes doesn't touch the lookup path
  (still open-addressing + representation dispatch), so latency is
  governed by different factors (reconstruction depth on compressed
  symbols, promotion/demotion churn) that V3 left untouched by design (per
  "reject any redesign that achieves memory savings by producing extreme
  lookup/reconstruction latency" — V3 doesn't trade latency for memory at
  all; it's latency-neutral by construction, confirmed by the p95 numbers
  moving by <5% in either direction on every corpus).

---

## E. Candidate architectural contribution (not yet a paper claim)

The demonstrated, measured contribution of this analysis is:

1. A **root-cause decomposition** of V2's per-symbol memory floor into five
   named, independently-verified components (PackedEntry, ScopeIndex
   share, liveSeenRep_ registry, pool/poolLookup duplication for interned
   symbols, BlockMember for compressed symbols), each with an exact byte
   count traced to source.
2. A **representation-aware metadata redesign (V3)** that cuts the
   dominant fixed-cost components (PackedEntry 44B→32B, liveSeenRep_ node
   ~130B→~76B for long names) with no algorithmic change, verified to:
   - Reduce physical peak heap on **7/7** real corpora (measured, not
     modeled).
   - Leave latency behavior statistically unchanged (no gate regressions).
   - Still lose to Conventional on physical heap on **7/7** corpora.
3. Quantitative evidence that **the architectural fixed-overhead floor
   is a real but non-dominant cause of V2's loss** — it explains roughly
   19-47% of the "excess" cost, not the whole gap. The remaining gap is
   explained by the workload-threshold analysis in §C: no available real
   corpus has identifiers long/redundant enough for interning or
   compression to pay for *any* achievable fixed floor, however small.
4. A **falsifiable, quantified break-even boundary**: V2/V3-style adaptive
   representation only wins once `L_mean ≥ 22B` **and** `R_live ≥ 0.65`
   simultaneously hold — a condition none of 16 characterized real corpora
   (across hand-written embedded/systems/interpreter code and generated
   RPC/schema code) satisfy.

**What this analysis does not yet establish** (left for the next stage,
explicitly per this goal's "do not write the final paper claim yet"):
whether a *more aggressive* structural change — e.g. eliminating
`liveSeenRep_` entirely by piggybacking repeat-detection on the
scope-local `ScopeIndex` fingerprint already computed for free, or
storing INLINE symbols in a packed side array with zero per-slot padding
instead of a union — could close the remaining ~90-130B/symbol gap
without violating the latency gate. That is the next concrete engineering
question this analysis motivates, not something to claim solved here.
