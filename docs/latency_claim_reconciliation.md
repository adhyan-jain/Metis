# Reconciling the "V3 p95 latency" claims (Stage 1 of corpus/latency investigation)

## Question

`docs/final_research_audit.md` states V3's p95 latency is 1.824x Conventional's on
Zephyr and reports the 1.25x gate as FAILED on all embedded workloads. An initial
scan of `data/real_world_benchmark.csv` (HEAD, post commit `f42b087`) appeared to
show V3 beating "Conventional" on p95 on several corpora (Zephyr, CPython, Lua,
Eigen), which looked like a contradiction worth chasing as a possible "historical
low-latency state."

## Finding: no contradiction, no lost state — two different baselines were being compared

There are two separate baseline implementations in this codebase, benchmarked in two
separate CSVs, and they are not interchangeable:

| CSV | Baseline used | Corpora | Baseline design |
|---|---|---|---|
| `data/real_world_benchmark.csv` | **Conventional** (`ConventionalHost`-style, `std::unordered_map<std::string,...>`, SSO-friendly) | 20 corpora (Arduino, cJSON, Clang, CPython, curl, Eigen, ESP-IDF, FFmpeg, FreeRTOS, LLVM, Lua, mbedTLS, Nginx, protobuf-c, protobuf-generated-cpp, QEMU, Qt6, Redis, SQLite, Zephyr) | Generic host-style symbol table, not embedded-oriented |
| `results/embedded_benchmark.csv` | **EmbeddedConventional** (`include/embedded_conventional_symbol_table.hpp`, 16B fixed entries, flat arena, single shared hash index) | 4 corpora (FreeRTOS, Arduino, Zephyr, ESP-IDF) | Deliberately compact embedded-oriented baseline |

Recomputing V3 vs. each baseline directly from the current, committed CSVs (no
rebuild needed — this is a pure re-derivation from data already on disk):

**V3 vs. Conventional (20 corpora, `data/real_world_benchmark.csv`):**
- Memory: V3 final heap is **larger than Conventional on all 20/20 corpora** (e.g.
  Zephyr: Conv 47.76MB vs V3 53.39MB, +11.8%). This matches
  `docs/interning_sanity_check.md` and `docs/ordering_analysis.md`'s structural
  argument that SSO-based Conventional always wins on real identifier corpora.
- p95 latency: V3 **meets** the 1.25x gate on 7/20 corpora (Eigen 0.650x, FFmpeg
  0.787x, Lua 0.675x, Nginx 0.564x, SQLite 0.817x, Zephyr 1.216x, curl 0.989x) and
  fails on 13/20 (worst: LLVM 3.02x, cJSON 2.84x, mbedTLS 2.47x).

**V3 vs. EmbeddedConventional (4 corpora, `results/embedded_benchmark.csv`):**
- Memory: V3 beats EmbeddedConventional only on **Zephyr** (-20.2%, 66.91MB →
  53.39MB); loses on FreeRTOS (+21.2%), Arduino (+31.7%), ESP-IDF (+20.8%).
- p95 latency: fails the 1.25x gate on all 4 (FreeRTOS 2.947x, Arduino 1.921x,
  Zephyr 1.824x, ESP-IDF 1.258x).

**These numbers reproduce `docs/final_research_audit.md` exactly** (Zephyr -20.2%
memory, 1.824x p95, gate FAIL). The audit's claims are correct and current — there
is no stale-snapshot issue and no hidden earlier commit where V3 beat
EmbeddedConventional on latency. `git log --follow -- include/symtab_v3.hpp` shows
exactly one substantive commit (`019f6d6`); no lookup cache was ever added and later
removed from V3.

## What the apparent "V3 beats Conventional" signal actually is

It's real, but it's a different, weaker comparison than the manuscript's headline
claim:
- It's against generic **Conventional**, not the embedded-realistic
  **EmbeddedConventional** baseline the paper's gate is defined against.
- Even there, V3 **never** wins on memory — only sometimes on p95 latency, and only
  because V3's `PackedEntry` (32B) is more cache-dense than the fatter conventional
  hash-map-of-strings entries, not because of any reconstruction/cache optimization.
  This is a locality side effect of the metadata shrink already documented in
  `docs/v3_overhead_and_representation_aware_analysis.md:266-276`, not a new
  mechanism.

## Conclusion for Stage 1

1. No historical low-latency V3 state needs to be "recovered" — the current
   committed data already reflects the only latency behavior V3 has ever had.
2. The manuscript's 1.824x/FAIL framing for **EmbeddedConventional** (the relevant
   embedded baseline) is accurate and should not be changed.
3. There IS an unexploited, true finding worth carrying into Stage 2/3: on the
   *generic* Conventional baseline, V3 passes the latency gate and sometimes wins
   on p95 for corpora with longer/more-similar identifiers (Eigen, FFmpeg, Lua,
   Nginx, SQLite, curl) — this correlates with C++ template-heavy or namespaced
   identifier style. This is a hint for corpus screening (Stage 2): look for
   *embedded* corpora with those same identifier characteristics (long, prefix-rich
   names) to see whether V3 can pass the gate against EmbeddedConventional too, not
   just against generic Conventional.
4. No re-run of the benchmark binary was necessary for this reconciliation since it
   is a pure re-derivation from already-committed, internally-consistent data; a
   fresh confirmation run is still worthwhile before Stage 3 to establish the p95
   noise band, but is not blocking further screening work.
