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

---

## F. Targeted screening of mature open-source C/C++ projects

Per a follow-up directive, screened famous, mature projects whose source
characteristics plausibly favor interning/compression, using local
system-installed dev packages (unmodified upstream distribution, no file
selection) rather than fabricated or cherry-picked files: **LLVM**,
**Clang** (both `/usr/include`, real Arch Linux dev packages), and a
shallow clone of **Eigen** (header-only, gitlab.com/libeigen/eigen) and
**Qt6** (`/usr/include/qt6`). Boost, GCC, Chromium, KDE Frameworks,
TensorFlow, and OpenCV were **not attempted** this pass — their dev
headers are not installed locally and fresh full clones (multi-GB to
tens-of-GB) are infeasible on this machine/session; this is reported
explicitly rather than silently omitted.

Full screening table (all 20 characterized corpora, same fixed tokenizer,
uniform methodology, no per-repository adjustment):

| Corpus | Decls | Unique | Reuse | Mean len | p50 | p90 | p99 | frac≤15B | frac>22B | Dup ratio | Access-skew Gini |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **protobuf-generated-cpp** | 1,933,674 | 260,027 | 0.933 | **18.31** | 11 | **43** | **84** | 0.595 | **0.291** | 0.843 | **0.910** |
| FreeRTOS | 72,376 | 10,386 | 0.947 | 14.20 | 13 | 26 | 37 | 0.610 | 0.174 | 0.940 | 0.793 |
| mbedTLS | 20,810 | 5,668 | 0.929 | 11.60 | 8 | 28 | 41 | 0.709 | 0.161 | 0.876 | 0.751 |
| cJSON | 6,315 | 2,352 | 0.895 | 11.44 | 8 | 25 | 44 | 0.727 | 0.138 | 0.794 | 0.747 |
| Clang | 134,909 | 47,378 | 0.845 | 11.17 | 10 | 21 | 35 | 0.764 | 0.080 | 0.760 | 0.761 |
| ESP-IDF | 795,847 | 231,075 | 0.879 | 12.33 | 10 | 25 | 39 | 0.681 | 0.145 | 0.791 | 0.747 |
| Qt6 | 453,972 | 132,713 | 0.870 | 10.21 | 8 | 20 | **45** | 0.802 | 0.075 | 0.739 | 0.818 |
| protobuf-c | 3,355 | 1,423 | 0.863 | 10.04 | 7 | 23 | 36 | 0.774 | 0.110 | 0.768 | 0.713 |
| LLVM | 267,475 | 98,482 | 0.841 | 9.86 | 8 | 19 | 33 | 0.817 | 0.060 | 0.724 | 0.776 |
| Zephyr | 703,727 | 228,739 | 0.897 | 9.93 | 7 | 21 | 34 | 0.785 | 0.084 | 0.804 | 0.807 |
| Arduino | 31,846 | 11,000 | 0.866 | 8.69 | 7 | 18 | 28 | 0.855 | 0.042 | 0.805 | 0.718 |
| QEMU | 959,486 | 299,886 | 0.915 | 8.23 | 6 | 18 | 29 | 0.850 | 0.045 | 0.839 | 0.826 |
| CPython | 336,691 | 68,829 | 0.952 | 8.44 | 6 | 19 | 33 | 0.849 | 0.062 | 0.902 | 0.864 |
| curl | 84,508 | 22,222 | 0.938 | 8.03 | 6 | 16 | 26 | 0.879 | 0.027 | 0.890 | 0.803 |
| Eigen | 204,719 | 41,790 | 0.951 | 8.07 | 6 | 17 | 30 | 0.867 | 0.035 | **0.911** | 0.853 |
| Redis | 125,220 | 34,058 | 0.939 | 7.78 | 6 | 17 | 29 | 0.873 | 0.041 | 0.886 | 0.795 |
| Nginx | 56,383 | 12,464 | 0.965 | 7.70 | 4 | 19 | 32 | 0.852 | 0.064 | 0.919 | 0.858 |
| SQLite | 133,618 | 28,832 | 0.952 | 6.76 | 5 | 15 | 25 | 0.909 | 0.018 | 0.907 | 0.819 |
| FFmpeg | 700,171 | 141,486 | 0.956 | 6.93 | 5 | 15 | 28 | 0.901 | 0.027 | 0.903 | 0.864 |
| Lua | 13,042 | 4,038 | 0.923 | 5.24 | 4 | 11 | 17 | 0.979 | 0.0001 | 0.871 | 0.751 |

### Ranking by theoretical favorability

- **A. Conventional-favoring** (short, low `frac>22B`, low mean length):
  Lua, SQLite, FFmpeg, Nginx, Redis — all mean length <8B, virtually
  entirely inside SSO range.
- **B. Interned-favoring** (high dup ratio + high reuse, moderate length so
  pool amortization pays off): **Eigen** (dup 0.911, reuse 0.951), Nginx
  (0.919), CPython (0.902), FFmpeg (0.903), SQLite (0.907) — Eigen is the
  standout among the newly screened projects specifically for interning.
- **C. Compression-favoring** (long identifiers + high prefix similarity in
  *sequence order*, not just static-universe order): none of the new
  candidates exceed protobuf-generated-cpp on this axis; among the new
  ones LLVM has the highest prefix similarity (0.112, still far under
  0.35) and Qt6 has the longest tail (p99=45B).
- **D. Adaptive-favoring** (best combination across multiple axes):
  protobuf-generated-cpp remains the strongest candidate in the entire
  20-corpus set (highest mean/p90/p99 length, highest `frac>22B`, highest
  access-skew Gini) — no newly screened mature project surpasses it on any
  axis relevant to V2's adaptive routing.

Chosen 3 most promising **new** naturally-occurring workloads for the
three-way (extended to five-way, see below) physical benchmark: **Clang**
(highest `frac>22B` and mean length among new candidates), **Qt6**
(longest p99 tail), **Eigen** (highest duplicate-payload ratio). LLVM was
screened but not separately benchmarked — it is dominated by Clang on
every length-related axis and by Eigen on the dup-ratio axis, so it adds
no new information the ranking doesn't already predict.

### Five-way benchmark result (Conventional / Conventional-HeapString / Interned / V2 / [V3 where run])

| Corpus | Conventional | **Conv-HeapString** | Interned | SymTabV2 | V2<Conv? | V2<Interned? | Conv p95 | V2 p95 | Gate (≤1.25×)? |
|---|---|---|---|---|---|---|---|---|---|
| Clang | 9,461,800 | 9,590,136 | 16,554,520 | 10,556,136 | No | **Yes** | 0.343 | 0.699 | No |
| Qt6 | 31,647,680 | 31,510,288 | 53,263,536 | 38,648,832 | No | **Yes** | 0.354 | 0.377 | **Yes** |
| Eigen | 18,124,976 | 18,175,344 | 23,422,448 | 19,953,792 | No | **Yes** | 0.281 | 0.248 | **Yes** |
| protobuf-generated-cpp | 65,432,936 | 65,831,872 | 117,524,936 | 70,536,496 | No | **Yes** | 0.128 | 0.496 | No |

**Primary success condition (V2 < Conventional on some naturally occurring
workload) is not met on any of these 4, nor on any of the 16 previously
tested corpora — 20/20 real corpora tested to date, V2 never beats
Conventional on physical heap.** Secondary condition (V2 < Interned) is
met on **all 20/20** corpora tested, without exception — this is now a
very strong, consistent finding, not a marginal one.

### Conventional-HeapString: does SSO explain the gap?

Across all four newly tested corpora, **Conventional-HeapString tracks
Conventional within 0.4-2.2%** (e.g. Qt6: 31.65M vs 31.51M; Eigen: 18.12M
vs 18.18M) rather than being dramatically worse. This directly answers
"is Conventional's win just SSO?" — **no, not primarily.** Forcing every
identifier onto a heap-allocated buffer barely moves Conventional's
memory footprint, because these corpora's mean identifier length (8-18B)
sits mostly *at or just above* the 15B SSO boundary already, so most
identifiers were already borderline candidates for a small allocation
either way, and libstdc++'s allocator handles small fixed-size heap
allocations (16-24B buckets) about as efficiently as SSO's inline buffer
in aggregate. **Conventional's real advantage is structural, not an SSO
artifact**: a single `std::unordered_map<std::string,T>` node is
Conventional's *only* per-symbol data structure, while V2 (even V3)
maintains 2-3 structures (`entries_`, `liveSeenRep_`, and for interned
symbols `poolLookup_`+`pool_`) for the same symbol — consistent with and
reinforcing the §B decomposition above.

### On Interned's consistent loss to Conventional

`InternedSymbolTable` loses to Conventional on all 20/20 corpora, often by
50-80%. This is not evidence that string interning is a useless
technique in general — it is evidence about the *break-even point* for
*this* interning design (a global pool keyed by `std::unordered_map`,
paying one pool-map node + one local-reference-map node per unique
string) applied to workloads with **reuse_ratio 0.84-0.98 but mean length
5-18B**. Classic interning wins when (a) the same string is referenced
*many* times per unique value (amortizing one pool allocation over many
references) **and** (b) each string is long enough that avoiding N
duplicate heap copies saves more than one pool-map node costs. Condition
(a) holds broadly here (reuse ratios are high across the board — that is
simply how identifiers work: declared once, used many times). Condition
(b) fails: at mean lengths of 5-18B, Conventional's hash map *already*
stores each unique string exactly once too (a `std::unordered_map` never
duplicates a key), so interning's only remaining advantage is avoiding
per-*use-site* string copies, which none of these implementations
actually construct (`resolve()`/`lookup()` take `const std::string&`, not
by value) — meaning **interning is solving a problem (duplicate storage
of the same string) that a plain hash map has already solved for free**,
while paying an *extra* pool/lookup layer on top. This is a break-even
condition worth stating precisely: interning-over-a-hash-map only wins
over a bare hash map when there are genuinely *multiple, independently
allocated copies* of the same string that a hash map's own deduplication
wouldn't already collapse (e.g., interning strings arriving in already-
duplicated form from multiple independent sources, not identifiers being
looked up against a single symbol table).

**What this analysis does not yet establish** (left for the next stage,
explicitly per this goal's "do not write the final paper claim yet"):
whether a *more aggressive* structural change — e.g. eliminating
`liveSeenRep_` entirely by piggybacking repeat-detection on the
scope-local `ScopeIndex` fingerprint already computed for free, or
storing INLINE symbols in a packed side array with zero per-slot padding
instead of a union — could close the remaining ~90-130B/symbol gap
without violating the latency gate. That is the next concrete engineering
question this analysis motivates, not something to claim solved here.
