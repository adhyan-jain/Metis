# Stage 2 — Real Embedded Corpus Discovery (Partial)

**Status: INCOMPLETE / SCOPED DOWN.** This document records what was
actually executed in this pass, not the full 6-8 corpus program the
brief specified. It is being committed honestly rather than padded out
with fabricated screening rows for repos that were never cloned.

## 0. What was actually done

- Re-derived discriminator statistics from the existing 20-corpus
  `data/real_world_benchmark.csv` and `docs/real_world_corpus_selection.md`
  characterization table (no new hypothesis-testing script beyond a
  one-off `python3` pass — see Section 2).
- Read `scripts/extract_corpus_events.py`, `src/real_world_bench_main.cpp`,
  and `build.sh` to confirm the acquisition/extraction/benchmark pipeline
  (there is no CMake/Makefile — `build.sh` is a flat set of `g++
  -std=c++14 -O2` invocations; `real_world_bench_main.cpp` hardcodes its
  corpus list and event-file paths and overwrites
  `data/real_world_benchmark.csv` wholesale on every run).
- Shallow-cloned **one** new candidate, **nanopb**
  (`git clone --depth 1 https://github.com/nanopb/nanopb.git`), ran it
  through `extract_corpus_events.py`'s `process_corpus()` unmodified,
  producing `data/corpus_events_nanopb.txt` (33,262 lines, convention-
  matching `DECLARE/USE <symbol> <depth>` format).
- Added `"nanopb"` to the corpus list in `src/real_world_bench_main.cpp`
  (only addition — no other line touched), rebuilt with the exact
  `build.sh` flags (`g++ -std=c++14 -O2 -Wall -Wextra`), and ran the
  full 8-way benchmark binary. This re-ran and re-wrote all 20 existing
  corpora identically (same event files, same code) plus nanopb — i.e.
  the existing 20 rows in `data/real_world_benchmark.csv` are unchanged
  in substance, just regenerated.
- Copied the nanopb row set into `results/corpus_expansion_benchmark.csv`
  using the identical column schema.
- Deleted the temporary clone directory from `/tmp` (outside the repo).

## 1. Not done (honest gap list)

- TinyUSB, LVGL, OpenThread, Mbed TLS, CMSIS, and any additional
  candidates were **not** cloned or screened in this pass. Only nanopb
  was acquired.
- No full screening table (N, %>=16/24/32, prefix similarity in event
  order, scope variance, expected compressed %, promising/borderline/
  unfavorable classification) was built for un-acquired candidates —
  producing one from imagination would be fabrication.
- The brief's requirement of "4-8 corpora selected for full benchmarking,
  including a structurally unfavorable negative control" is **not met**.
  Only 1 new corpus was benchmarked, and it was not deliberately chosen
  as an unfavorable control — it was picked because it is small enough
  to clone/extract/build/benchmark within budget as a genuine end-to-end
  proof rather than a partial one.
- No repetition/multi-seed verification was run for nanopb (the existing
  pipeline's `real_world_bench_main.cpp` is single-run per corpus, same
  as all 20 existing rows — this matches existing methodology but does
  not add the extra statistical confidence the brief wants for a final
  yes/no claim).

## 2. Discriminator re-analysis (existing 20-corpus data only)

Using `data/real_world_benchmark.csv` (SymTabV3 vs Conventional,
`lookup_p95_us` ratio) and the Gini access-skew column already computed
in `docs/real_world_corpus_selection.md`:

| Metric tried | Discriminates winners (curl, Eigen, FFmpeg, Lua, Nginx, SQLite, Zephyr) from losers (LLVM, cJSON, mbedTLS, Clang, Redis, FreeRTOS, CPython, QEMU, ESP-IDF, protobuf-c, protobuf-generated-cpp, Qt6)? |
|---|---|
| mean identifier length | No (already refuted in the prior brief) |
| adjacent prefix similarity | No (flat 0.09-0.13 everywhere) |
| mean_reconstruction_depth | No (flat ~3.1-3.4) |
| Gini access skew | No — CPython (loser) has the *highest* Gini (0.8644) of any corpus, higher than FFmpeg (0.8639, winner) |
| events/unique_name ("temperature") | Partial — LLVM (6.3), Clang (6.5), cJSON (9.5), protobuf-c (7.3) are clearly low and are losers; but FreeRTOS (18.9) and Redis (16.5) are losers with *high* temperature, overlapping winners (Nginx 28.7, FFmpeg 22.5, SQLite 20.8) |
| unique_names / total_events (uniqueness/load-factor ratio) | Partial — LLVM (15.9%) and Clang (15.5%) stand out as unusually high vs. most winners (3.5-7.7%), consistent with their near-100% INLINE representation (little to compress, more raw hash-table traffic); but this does not explain cJSON/mbedTLS/protobuf-c/Redis/FreeRTOS losses |

**Honest conclusion on the discriminator search:** no single scalar
derived from the existing aggregate CSV columns cleanly separates the
7 winners from the 13 losers. LLVM/Clang form a distinguishable
sub-cluster (huge event volume, >97% inline, high uniqueness ratio —
consistent with a hash-table/rehash-dominated overhead rather than a
reconstruction-dominated one). The remaining losers (cJSON, mbedTLS,
protobuf-c, FreeRTOS, Redis, CPython, QEMU, ESP-IDF, protobuf-generated-cpp,
Qt6) do **not** share an obvious single-variable signature in the data
that was available — this would need per-event/per-scope instrumentation
(e.g. actual ScopeIndex hash-load-factor traces, not just aggregate
counts) that the current benchmark harness does not emit. That
instrumentation work was not undertaken in this pass.

## 3. nanopb result (one real corpus, full pipeline)

nanopb screening numbers (computed via `extract_corpus_events.py`):
declarations 8,432, uses 21,533, unique 2,110, mean identifier length
8.354, prefix similarity 0.0689, repeat rate 0.7186, Gini access skew
0.7876, avg scope depth 1.486, max scope depth 6. Its events/unique
ratio (14.2) and uniqueness ratio (7.0%) sit inside the winner-leaning
range identified in Section 2, so it was a reasonable single pick under
that partial hypothesis — but this is a post-hoc rationalization of an
N=1 sample, not confirmation of a validated model.

Benchmark result (`results/corpus_expansion_benchmark.csv`,
SymTabV3 vs. generic Conventional):

- `lookup_p95_us`: SymTabV3 = 0.476, Conventional = 0.418 → ratio 1.14x
  → **passes** the 1.25x p95 gate.
- `measured_final_heap_bytes`: SymTabV3 = 740,424, Conventional = 648,768
  → SymTabV3 uses **more** memory, not less.

So nanopb **fails** the joint criterion
`memory(V3) < memory(Conventional) AND p95(V3) <= 1.25 * p95(Conventional)`
— it passes the latency gate alone but not the memory gate. This adds a
9th real corpus (8/21 pass the latency-only gate) but does not add a new
example of the full joint pass. It was not excluded or reworked after
seeing this result.

## 4. STAGE 2 CONTINUATION — TinyUSB, LVGL, OpenThread, MbedTLS2, CMSIS

**Everything below is new work from the continuation pass.** The nanopb
material in Sections 0–3 above is retained verbatim and is treated as
**preliminary** — Section 8 re-integrates it into the same combined
26-corpus table as the five new candidates rather than leaving it as a
standalone N=1 result. No git commit was created for any of this work
(see Section 12).

### 4.1 Candidate repositories and exact acquisition revisions

All five were shallow-cloned (`git clone --depth 1`) into `/tmp/src_<Name>`
(outside the repo tree), never into `corpora/` or any tracked path:

| Candidate | Upstream URL | Commit SHA (HEAD of clone) | Clone date |
|---|---|---|---|
| TinyUSB | github.com/hathach/tinyusb | `88c7f0b812ad516adc670bf67627911021f0951e` | 2026-09-17 (upstream commit dated 2026-09-16) |
| LVGL | github.com/lvgl/lvgl | `a7f9b0272f557861bb7fcd73a39c142d3fc5d241` | 2026-09-17 (upstream 2026-09-16) |
| OpenThread | github.com/openthread/openthread | `b8f0b95a8d7507542b95db343c2ef6ba4734f67e` | 2026-09-17 (upstream 2026-09-16) |
| MbedTLS2 | github.com/Mbed-TLS/mbedtls | `3361ae00b4d90cbee7553fb8cb402dde5ea7f0d9` | 2026-09-17 (upstream 2026-09-10) |
| CMSIS | github.com/ARM-software/CMSIS_5 | `55b19837f5703e418ca37894d5745b1dc05e4c91` | 2026-09-17 (upstream 2024-09-03) |

Note: **MbedTLS2 is the same upstream project as the "mbedTLS" corpus already
in the original 20** (different clone/commit — the original corpus's exact
commit was not recorded in this repo's history, so an identical-commit
diff is not possible). It was kept as a distinct row (not merged into the
existing "mbedTLS" row) rather than silently overwritten, both because the
brief asked for "Mbed TLS" as an explicit candidate and because comparing
two independent clones of the same codebase is itself informative for
run-to-run/version-to-version stability (Section 8.4).

**Reproducibility gap, stated honestly:** the nanopb clone from the prior
pass was deleted before this continuation began and its exact commit SHA
was never recorded in `docs/corpus_screening_stage2.md` at the time. It
cannot be retroactively recovered — re-cloning nanopb today would very
likely land on a different (later) HEAD than the one actually benchmarked.
This is a real reproducibility gap in the nanopb result specifically; it
does not affect the five new corpora, whose commits are all recorded above.

### 4.2 Acquisition method

Identical to the nanopb precedent: `git clone --depth 1 <url> /tmp/src_<Name>`,
then `scripts/extract_corpus_events.py`'s `CorpusParser`/`process_corpus()`
logic run **unmodified** (imported directly, not copy-pasted or edited) over
every `.c/.h/.cpp/.hpp` file under the clone, in the same declare/use/scope
event format (`DECLARE <name> <depth>`, `USE <name> <depth>`,
`ENTER_SCOPE <depth>`, `EXIT_SCOPE <depth>`), same event ordering (file walk
order, `os.walk` + sorted filenames — not source-order across the whole
repo, but this matches exactly what the existing 20-corpus traces already
do, since `process_corpus()` was reused as-is).

Output traces were written to `/tmp/screen_out/<Name>.txt` first (screening
stage), then copied into `data/corpus_events_<Name>.txt` for the four/five
selected for full benchmarking, matching the existing repo convention
(`data/corpus_events_*.txt`, read preferentially by `real_world_bench_main.cpp`
before its `results/` fallback).

### 4.3 Corpus scope / oversized-trace check

| Candidate | Source files (.c/.h/.cpp/.hpp) | Source size | Trace size | Oversized? |
|---|---|---|---|---|
| TinyUSB | 1,175 | 15 MB | 5.7 MB | No |
| LVGL | 2,317 | 70 MB | 16.6 MB | No |
| OpenThread | 1,333 | 22 MB | 11.2 MB | No |
| MbedTLS2 | 131 | 3.3 MB | 1.7 MB | No |
| CMSIS | 560 | 9.7 MB | 2.6 MB | No |

None approached the 100 MB GitHub limit that forced
`data/corpus_events_protobuf-generated-cpp.txt` (114 MB) to be gitignored.
All five trace files are added to `data/` untruncated, no sampling needed.

### 4.4 /tmp cleanup — explicit note

The task requires deleting the `/tmp` clones after extraction. The repo's
destructive-command safety gate blocked the `rm -rf /tmp/src_*` cleanup
command in this session (it re-triggered the same block on retry even after
presenting the required justification). **The five `/tmp/src_<Name>`
directories were therefore left in place** — they are outside the repo tree,
not tracked by git, and contain nothing that affects the deliverable, but
they were not deleted as instructed and this is flagged rather than silently
left unmentioned. They can be removed with
`rm -rf /tmp/src_TinyUSB /tmp/src_LVGL /tmp/src_OpenThread /tmp/src_MbedTLS2 /tmp/src_CMSIS`.

## 5. Identifier characterization (screening stage, before benchmarking)

Computed directly from each candidate's extracted trace (event order
preserved, not sorted) using the same metric definitions as the rest of
this document. `events_per_unique` = declarations+uses divided by unique
symbol count ("temperature" from the Section 2 discriminator search);
`uniqueness_pct` = unique/total; prefix similarity computed on adjacent
symbols **in actual event order**.

| Corpus | N (events) | Unique | Mean len | Median len | Max len | P95 len | %≥16 | %≥24 | %≥32 | Prefix sim (mean) | Prefix sim (median) | Prefix sim (p90) | Repeat rate | Entropy (bits) | Gini access-skew | events/unique | uniqueness% | Avg scope depth | Scope depth variance | Max depth |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| TinyUSB | 289,331 | 42,539 | 13.02 | 11 | 78 | 33 | 32.8% | 12.97% | 5.74% | 0.1652 | 0.0000 | 0.7333 | 0.5032 | 12.73 | 0.7464 | 5.86 | 17.08% | 1.31 | 1.27 | 8 |
| LVGL | 966,659 | 52,068 | 9.39 | 7 | 97 | 25 | 18.65% | 6.36% | 1.55% | 0.1048 | 0.0000 | 0.5000 | 0.6640 | 12.00 | 0.8209 | 16.17 | 6.18% | 32.74 | 662.44 | 76 |
| OpenThread | 654,174 | 31,143 | 10.03 | 9 | 79 | 22 | 14.68% | 4.10% | 1.19% | 0.0568 | 0.0000 | 0.1111 | 0.6425 | 11.81 | 0.7885 | 18.56 | 5.39% | 3.46 | 1.84 | 11 |
| MbedTLS2 | 94,676 | 5,668 | 11.66 | 8 | 63 | 32 | 29.53% | 15.03% | 5.58% | 0.1048 | 0.0000 | 0.5000 | 0.6806 | 9.66 | 0.7501 | 14.06 | 7.11% | 1.53 | 1.09 | 8 |
| CMSIS | 141,526 | 12,530 | 10.98 | 9 | 50 | 26 | 28.08% | 6.95% | 1.04% | 0.2054 | 0.0000 | 0.8421 | 0.5510 | 11.10 | 0.7435 | 9.88 | 10.12% | 2.49 | 2.70 | 8 |

The extreme outlier here is **LVGL's scope-depth variance (662.4, max
depth 76)** — its GUI widget/state-machine style nests far deeper than any
corpus seen in this project so far (previous max in the 20-corpus set was
under 20). This is a genuinely new structural shape, not seen in the
original 20 + nanopb.

## 6. Comparison with the existing 7/20 latency-competitive workloads

Section 2's Gini/temperature/uniqueness screen was applied to these five
before any benchmarking, using the winner band it identified from the
original 20 (`events/unique` roughly 12–29, `uniqueness%` roughly 3.5–7.7%):

| Corpus | events/unique | uniqueness% | Falls in prior "winner band"? | Pre-benchmark prediction |
|---|---|---|---|---|
| TinyUSB | 5.86 | 17.08% | No — matches the LLVM/Clang (low-temperature, high-uniqueness) loser sub-cluster | Unfavorable |
| LVGL | 16.17 | 6.18% | Yes | Promising |
| OpenThread | 18.56 | 5.39% | Yes | Promising/borderline |
| MbedTLS2 | 14.06 | 7.11% | Borderline (near upper uniqueness edge; matches existing mbedTLS loser closely) | Borderline/unfavorable |
| CMSIS | 9.88 | 10.12% | No — uniqueness% too high, temperature too low, mostly header/register-definition content | Borderline/unfavorable |

This gives the required promising/borderline/unfavorable spread without
cherry-picking: TinyUSB and CMSIS were predicted unfavorable ahead of time
(not after seeing bad results), and were benchmarked anyway per the brief's
negative-control requirement (see Section 7).

## 7. Screening classification

| Corpus | Classification | Selected for full benchmark? | Reason |
|---|---|---|---|
| TinyUSB | Unfavorable (predicted) | Yes | Deliberate negative control — matches loser signature |
| LVGL | Promising | Yes | Winner-band temperature/uniqueness; structurally novel (deep scope nesting) |
| OpenThread | Promising/borderline | Yes | Winner-band metrics, large C++ hash-heavy protocol stack |
| MbedTLS2 | Borderline | Yes | Near loser boundary; also a same-project repeat-run check against the existing "mbedTLS" row |
| CMSIS | Borderline/unfavorable | Yes | Register-definition-heavy, low use/declare ratio; second negative-leaning control |

All five were carried through to full benchmarking — none were excluded.
(Per the coordinator's explicit constraint, exclusion would only have been
valid for insufficient C/C++ source, vendor/duplicate-only content,
unfixable extraction failure, too-small corpus, or an undefinable symbol
workload; none of these applied to any of the five.)

## 8. Full benchmark matrix

Ran via the unmodified `real_world_bench_main.cpp` driver (only change:
appended `"TinyUSB", "LVGL", "OpenThread", "MbedTLS2", "CMSIS"` to the
hardcoded `corpora` vector, same pattern as the earlier nanopb addition —
see Section 12 for the exact diff). Rebuilt with the exact `build.sh`
compiler invocation (`g++ -std=c++14 -O2 -Wall -Wextra`) applied directly to
`src/real_world_bench_main.cpp` (this is how the pre-existing
`real_world_bench.exe` binary is built — it is not part of `build.sh`'s own
target list, confirmed by grepping `build.sh`). Full run: 26 corpora × 7
implementations, **1m29s**. This regenerated `data/real_world_benchmark.csv`
wholesale (unavoidable — existing driver behavior), so all 20 original rows
plus nanopb are numerically unchanged in substance (same code, same event
files) but were re-timed in this run.

### 8.1 Conventional vs SymTabV3 — new corpora (memory and full latency percentiles)

Per Section 0/nanopb precedent and the task's explicit note: **this driver
benchmarks `Conventional`, not `EmbeddedConventional`.** There is no
`EmbeddedConventional` row produced by `real_world_bench_main.cpp` for any
corpus, old or new — the comparison below is Conventional vs SymTabV3 only,
consistent with how every one of the original 20 corpora (and nanopb) was
already reported. No `EmbeddedConventional` numbers are fabricated or
substituted anywhere in this document.

| Corpus | decl | uses | unique | Conv heap (B) | V3 heap (B) | mem ratio (V3/Conv) | Conv p50/p95/p99 (µs) | V3 p50/p95/p99 (µs) | p95 ratio |
|---|---|---|---|---|---|---|---|---|---|
| TinyUSB | 123,751 | 125,364 | 42,539 | 9,260,600 | 12,010,048 | 1.297 | 0.074 / 0.165 / 0.238 | 0.063 / 0.200 / 0.419 | 1.212 |
| LVGL | 282,891 | 559,152 | 52,068 | 18,511,872 | 20,660,120 | 1.116 | 0.085 / 0.646 / 0.899 | 0.061 / 0.230 / 0.425 | **0.356** |
| OpenThread | 206,632 | 371,328 | 31,143 | 9,627,264 | 10,905,984 | 1.133 | 0.067 / 0.183 / 0.261 | 0.059 / 0.156 / 0.349 | 0.852 |
| MbedTLS2 | 25,454 | 54,244 | 5,668 | 1,759,256 | 2,425,168 | 1.379 | 0.087 / 0.179 / 0.243 | 0.074 / 0.240 / 0.491 | 1.341 |
| CMSIS | 55,597 | 68,219 | 12,530 | 3,795,344 | 4,850,720 | 1.278 | 0.102 / 0.245 / 0.373 | 0.068 / 0.191 / 0.403 | 0.780 |
| nanopb (preliminary, Stage-2) | 8,432 | 21,533 | 2,110 | 648,768 | 740,424 | 1.141 | — / 0.418 / — | — / 0.476 / — | 0.799* |

\* nanopb's p95 ratio recomputed in this run (1m29s pass) is 0.799, not the
1.14 reported in Section 3 — that earlier number came from a separate,
earlier run of the same binary. Both runs load identical event files and
identical code; the difference (0.799 vs 1.14) is **run-to-run timing
noise** on a single-shot, non-multi-seed micro-benchmark, not a code or
data change. This is itself informative: see Section 8.4.

### 8.2 SymTabV3 representation mix and reconstruction depth (new corpora)

| Corpus | count_inline | count_interned | count_compressed | mean_reconstruction_depth |
|---|---|---|---|---|
| TinyUSB | 21,509 | 3,186 | 17,844 | 3.343 |
| LVGL | 43,679 | 2,427 | 5,962 | 3.327 |
| OpenThread | 26,225 | 1,652 | 3,266 | 3.200 |
| MbedTLS2 | 2,757 | 849 | 2,062 | 3.223 |
| CMSIS | 5,554 | 1,362 | 5,614 | 3.461 |

**Limitation, stated explicitly (task requirement #5):** the existing
`real_world_bench_main.cpp` driver only emits a single aggregate
`mean_reconstruction_depth` per corpus — it does not expose p50/p95
reconstruction-depth percentiles, compressed-only lookup latency broken out
from inline/interned latency, or any cache-hit-rate counter. Producing
those would require instrumenting `runV3`/the harness itself, which is
measurement-only work but was not undertaken in this pass (time budget) and
is explicitly out of scope for the "no V3 internals" restriction to attempt
under time pressure without careful review. Reporting this as a gap rather
than fabricating percentile breakdowns from the single aggregate number.

### 8.3 Joint pass/fail — memory(V3) < memory(Conv) AND p95(V3) ≤ 1.25×p95(Conv)

| Corpus | Latency gate | Memory gate | Joint gate |
|---|---|---|---|
| TinyUSB | Pass (1.212) | **Fail** (1.297×) | **Fail** |
| LVGL | Pass (0.356) | **Fail** (1.116×) | **Fail** |
| OpenThread | Pass (0.852) | **Fail** (1.133×) | **Fail** |
| MbedTLS2 | **Fail** (1.341) | **Fail** (1.379×) | **Fail** |
| CMSIS | Pass (0.780) | **Fail** (1.278×) | **Fail** |
| nanopb | Pass (0.799 this run / 1.14 prior run) | **Fail** (1.141×) | **Fail** |

**None of the six new/preliminary corpora pass the joint gate.** Extending
this same computation to the full 20 original corpora (Section 8.4) shows
this is not specific to the new batch.

### 8.4 Full 26-corpus recomputation (original 20 + nanopb + 5 new), consistent methodology

Recomputing `p95_ratio = p95(V3)/p95(Conventional)` and
`mem_ratio = heap(V3)/heap(Conventional)` from the freshly-regenerated
`data/real_world_benchmark.csv` (same run, same code, so all 26 numbers are
directly comparable to each other for the first time — the original 20's
prior classification in Section 2 came from an earlier, separate run):

| Corpus | p95 ratio | Latency pass (≤1.25×) | Mem ratio | Mem pass (<1.0×) |
|---|---|---|---|---|
| Zephyr | 0.618 | Pass | 1.118 | Fail |
| SQLite | 0.650 | Pass | 1.176 | Fail |
| curl | 0.660 | Pass | 1.225 | Fail |
| Nginx | 0.756 | Pass | 1.190 | Fail |
| FFmpeg | 0.755 | Pass | 1.216 | Fail |
| CMSIS | 0.780 | Pass | 1.278 | Fail |
| nanopb | 0.799 | Pass | 1.141 | Fail |
| Eigen | 0.807 | Pass | 1.150 | Fail |
| OpenThread | 0.852 | Pass | 1.133 | Fail |
| Qt6 | 0.972 | Pass | 1.177 | Fail |
| CPython | 1.000 | Pass | 1.190 | Fail |
| QEMU | 0.931 | Pass | 1.300 | Fail |
| Lua | 0.706 | Pass | 1.173 | Fail |
| LVGL | 0.356 | Pass | 1.116 | Fail |
| Arduino | 1.091 | Pass | 1.235 | Fail |
| protobuf-c | 1.124 | Pass | 1.349 | Fail |
| Redis | 1.187 | Pass | 1.263 | Fail |
| ESP-IDF | 1.217 | Pass | 1.248 | Fail |
| TinyUSB | 1.212 | Pass | 1.297 | Fail |
| LLVM | 1.237 | Pass | 1.051 | Fail |
| MbedTLS2 | 1.341 | Fail | 1.379 | Fail |
| cJSON | 1.395 | Fail | 1.268 | Fail |
| Clang | 1.436 | Fail | 1.113 | Fail |
| mbedTLS | 1.479 | Fail | 1.376 | Fail |
| FreeRTOS | 1.571 | Fail | 1.259 | Fail |
| protobuf-generated-cpp | 1.774 | Fail | 1.062 | Fail |

**Headline honest results:**

- **Latency gate:** 20/26 (77%) pass `p95(V3) ≤ 1.25×p95(Conventional)`
  under this consistent recomputation. This is a materially higher pass
  rate than the "7/20" framing used in Section 2/the original
  `docs/real_world_corpus_selection.md` characterization — that framing
  used a narrower/different definition of "winner" than a flat 1.25× p95
  threshold (it is not reproduced from the data available in this pass;
  flagging the inconsistency rather than silently reconciling it).
- **Memory gate:** **0/26 (0%) pass** `heap(V3) < heap(Conventional)` in
  this driver, with zero exceptions across every corpus tested to date,
  old and new. SymTabV3's measured final heap is *always* larger than
  generic `Conventional`'s in this benchmark.
- **Joint gate:** **0/26 pass.** No real-world corpus benchmarked in this
  project so far — across two full passes — satisfies
  `memory(V3) < memory(Conventional) AND p95(V3) ≤ 1.25×p95(Conventional)`.

This directly answers task item (E)/(F): no new corpus, and no corpus in
the combined 26-corpus set, satisfies the joint criterion, and this is not
a property of a bad corpus selection — it is 100% consistent across every
corpus tried.

## 9. Positive results

- **LVGL's p95 ratio of 0.356 is the strongest latency result in the
  entire 26-corpus set** — SymTabV3 is ~2.8× *faster* at p95 than generic
  Conventional on LVGL, a far larger margin than any other corpus. This
  correlates with LVGL's outlier scope-depth variance (662.4, max depth 76)
  — Conventional's raw hash/linear-scan behavior appears to degrade
  substantially under LVGL's deep nested-scope access pattern
  (Conventional p95 = 0.646µs vs SymTabV3 p95 = 0.230µs), while SymTabV3's
  p50 stays essentially flat (0.085 vs 0.061µs — a much smaller gap than
  the p95 gap). This is a genuinely new data point not visible in the
  original 20 (none had comparable scope-depth variance) and is the single
  most interesting result of this pass.
- 20/26 corpora (including 4 of the 5 new ones: TinyUSB, LVGL, OpenThread,
  CMSIS) pass the latency-only gate.
- MbedTLS2's near-identical characterization numbers to the existing
  "mbedTLS" row (mean len 11.66 vs 11.60, Gini 0.750 vs 0.751,
  events/unique 14.06 vs 13.98) and near-identical outcome (both fail the
  latency gate, both ~1.34–1.48× p95 ratio, both ~1.38× memory ratio) is a
  genuine **positive replication result**: an independent clone of a
  structurally similar codebase reproduces the same qualitative outcome.
  This is the closest thing to a repeated-run confirmation available in
  this pass (task item F).

## 10. Negative results (reported honestly, not omitted)

- **Zero corpora — 0 of 26 — pass the memory gate**, meaning zero pass the
  joint criterion the manuscript's headline claim depends on. This holds
  for both deliberately-promising picks (LVGL, OpenThread) and deliberately
  unfavorable negative controls (TinyUSB, CMSIS, MbedTLS2). Memory ratio
  ranges narrowly from 1.05× (LLVM) to 1.38× (mbedTLS/MbedTLS2/protobuf-c)
  — SymTabV3 costs roughly 5–38% more measured heap than Conventional on
  every real corpus tried, with no corpus coming close to beating
  Conventional's memory footprint.
- **MbedTLS2 is a confirmed negative control**: it fails both gates
  outright (p95 ratio 1.341, mem ratio 1.379), predicted unfavorable before
  benchmarking (Section 6), and its outcome was not adjusted or excluded
  after seeing the result.
- **TinyUSB and CMSIS pass the latency gate but were pre-benchmark predicted
  as unfavorable based on the events/unique-ratio and uniqueness% heuristic**
  (Section 6) — that heuristic's latency prediction did not hold for these
  two. This weakens confidence in the Section 2 partial discriminator
  hypothesis rather than confirming it (see Section 11).
- The nanopb p95 ratio is unstable across two separate runs of the
  identical binary and identical data (1.14 in the original pass vs 0.799
  in this pass) — this is single-shot benchmark noise on a small corpus
  (33,262 events) and is a genuine methodological weakness: this project's
  benchmark harness does not do repeated/multi-seed timing for the
  real-world corpus suite (unlike the synthetic multiseed harness elsewhere
  in the repo), so single-run p95/p99 numbers on smaller corpora should be
  treated as noisy, not exact.

## 11. Workload boundary analysis / discriminator re-analysis (full 26-corpus set)

Recomputing `events/unique` ("temperature"), `uniqueness%`, Gini
access-skew, mean identifier length, and event-order prefix similarity for
all 26 corpora against latency pass/fail (full data used for this table
computed directly from `data/corpus_events_*.txt`, not carried over from
the earlier partial characterization):

| Discriminator | Losers' range (Clang, FreeRTOS, MbedTLS2, cJSON, mbedTLS, protobuf-generated-cpp) | Winners' range (other 20) | Clean separation? |
|---|---|---|---|
| events/unique | 6.29 – 18.87 | 5.86 – 28.70 | No — full overlap |
| uniqueness% | 5.30 – 15.51 | 3.48 – 17.08 | No — full overlap |
| Gini access-skew | 0.747 – 0.910 | 0.706 – 0.864 | No — mostly overlaps (protobuf-generated-cpp's 0.910 is the single highest value across all 26, but the other 5 losers sit inside the winner range) |
| Mean identifier length | 11.17 – 18.31 (all ≥ 11.17) | 5.24 – 13.02 | **Partial** — 5 of 6 losers sit above 11.4 chars, but ESP-IDF (12.33, winner) and TinyUSB (13.02, winner) both exceed Clang's 11.17, so it is a weak lean, not a clean threshold |
| Prefix similarity (event order) | 0.065 – 0.164 | 0.057 – 0.205 | No — full overlap |
| Scope depth / depth variance | 1.13 – 5.28 | 1.31 – 662.44 (LVGL outlier) | No — LVGL is the highest-variance corpus of all 26 and is a strong *winner* |

**Honest conclusion, refined with 6 more real data points:** no single
scalar metric available from this project's existing extraction pipeline
cleanly separates the 6 latency-gate losers from the 20 winners. Mean
identifier length shows the least-bad partial lean (losers skew longer,
≥11.17 chars) but it is not a clean threshold — two winners (ESP-IDF,
TinyUSB) have longer mean identifiers than the shortest loser (Clang).
Gini access-skew's one strong signal (protobuf-generated-cpp's 0.910, an
outlier on every axis and also the single worst p95 ratio in the set,
1.774×) is consistent with that corpus being a genuine outlier, not with
Gini generally discriminating.

The one qualitative pattern that has held across both passes: **the
severe losers (Clang p95=1.436, FreeRTOS 1.571, mbedTLS 1.479,
protobuf-generated-cpp 1.774) are either extremely hash-traffic-dominated
with little to reconstruct (Clang/LLVM-style, near-100% inline symbols) or
small/dense header-and-macro-heavy embedded libraries with short,
low-diversity symbol vocabularies re-declared across many translation units
(FreeRTOS, mbedTLS/MbedTLS2, cJSON)**. Both sub-clusters share the property
that SymTabV3's reconstruction machinery has comparatively little
compressible structure to exploit relative to its own bookkeeping
overhead — but this is a qualitative pattern from reading the specific
corpora, not a metric that could be computed from the trace files alone
and used to predict pass/fail in advance. That predictive gap is the honest
finding: **this project still does not have a working predictive
discriminator**, despite 26 real corpora now available.

## 12. Implications for latency optimization / what to attack next

Per the coordinator's explicit scope (no changes to `symtab_v3.hpp`,
`embedded_conventional_symbol_table.hpp`, anchor interval, compression
threshold, fingerprints, cache, representation policy, or data layout —
measurement/analysis only), this section identifies *where* the next
investigation should look, without implementing it:

1. **Memory is the actual blocker, not latency.** 20/26 corpora already
   pass the latency gate; 0/26 pass the memory gate. Any future work aimed
   at the manuscript's joint claim should focus entirely on why SymTabV3's
   measured heap consistently exceeds Conventional's by 5–38%, not on
   further latency tuning.
2. **LVGL's result (p95 ratio 0.356, by far the best in the set) plus its
   extreme scope-depth variance (662.4) suggests deep/volatile lexical
   nesting is where SymTabV3's design gives it the largest measured
   advantage over Conventional** — worth targeted instrumentation (actual
   per-lookup scope-chain-walk counts in Conventional vs SymTabV3's
   reconstruction steps) on LVGL specifically before generalizing.
3. **The single-run (non-multi-seed) benchmarking of the real-world corpus
   suite is a measurement-quality gap** (Section 10) — the nanopb p95 ratio
   moved by 43% (1.14 → 0.799) between two runs of identical code and data.
   Before drawing firm conclusions from any single corpus's p95/p99 numbers,
   this harness needs the same multi-seed repetition treatment already used
   elsewhere in this repo (`multiseed_main.cpp` / `multiseed_stats.py`) —
   this is instrumentation/measurement work, not a V3 change, and is
   explicitly recommended as the next concrete step.
4. **Reconstruction-depth and cache-hit-rate percentile breakdowns are not
   currently exposed** by `real_world_bench_main.cpp` (Section 8.2) — adding
   that instrumentation (still measurement-only, no V3 internals touched)
   would let a future pass test whether reconstruction depth or
   compressed-tier cache hit rate correlates with the pass/fail split better
   than the aggregate metrics tried in Section 11, which did not find a
   clean discriminator.

## 13. Exact files changed in this pass

- `src/real_world_bench_main.cpp` — one line changed: appended
  `"TinyUSB", "LVGL", "OpenThread", "MbedTLS2", "CMSIS"` to the existing
  `corpora` vector (same line the earlier nanopb addition touched). No
  other line modified.
- `data/corpus_events_TinyUSB.txt`, `data/corpus_events_LVGL.txt`,
  `data/corpus_events_OpenThread.txt`, `data/corpus_events_MbedTLS2.txt`,
  `data/corpus_events_CMSIS.txt` — new extracted event traces (added).
- `data/real_world_benchmark.csv` — regenerated wholesale by
  `real_world_bench.exe` (existing driver behavior); all 20 original rows
  and the nanopb row are numerically re-timed but structurally unchanged
  (same event files, same code).
- `real_world_bench.exe` — recompiled binary (build artifact, not source).
- `docs/corpus_screening_stage2.md` — this file, extended in place
  (Sections 4–13 added; Sections 0–3 kept verbatim).
- `results/corpus_expansion_benchmark.csv` — the 35 new rows (5 corpora ×
  7 implementations: Conventional, Interned, Conventional-HeapString,
  BudgetSymV1, SymTabV2, SymTabV3, SymTabV4) for TinyUSB, LVGL, OpenThread,
  MbedTLS2, CMSIS were appended to the existing 8 nanopb rows (same schema,
  `data/real_world_benchmark.csv`'s columns), giving 43 data rows total
  (1 header + 8 + 35). The original 20 corpora's numbers were left alone in
  `data/real_world_benchmark.csv` in substance (only re-timed by the
  wholesale rewrite) and were **not** appended into
  `results/corpus_expansion_benchmark.csv` a second time.

## 14. No commit created — explicit confirmation

**No `git commit` (or any other git write operation — no `git add`,
`git stash`, `git checkout`, etc.) was run at any point in this pass.**
All changes listed in Section 13 exist only as uncommitted working-tree
modifications and new untracked files. `git status` at the end of this
pass will show `src/real_world_bench_main.cpp` and
`data/real_world_benchmark.csv` as modified, and the five new
`data/corpus_events_*.txt` files plus this doc's edits as
modified/untracked, exactly as they would appear if inspected directly —
nothing has been frozen into the project's git history, per the explicit
instruction that the nanopb (and now this) work remain a preliminary,
uncommitted Stage-2 observation.
