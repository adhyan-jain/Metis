# Experiment Log — Zephyr symbolization gap-fill

Start state: git `9d9db2b96051af14950ff7adcf76b36b0c5ee624`, branch `research/zephyr-symbol-compression`, many untracked research directories (left untouched). Zephyr tree `corpora/zephyr` @`403a7d79` with 5 locally modified files (`logs/zephyr_local_modifications.diff`). Host: Linux 7.2.4-arch, 16 cores, 15 GB RAM. Nothing was committed, pushed, submitted or published.

## 1. Environment
* The repository's "GCC" (`~/local-toolchain/bin/arm-none-eabi-gcc`) is a shell wrapper that execs **clang 22.1.8**; no real GCC and no QEMU-ARM/RISC-V existed. Installed Zephyr SDK 1.0.1 (arm-zephyr-eabi and riscv64-zephyr-elf GCC **14.3.0**, hosttools with QEMU) in `~/zephyr-sdk-1.0.1`; archives verified against the release `sha256.sum`.
* pyelftools 0.33 added to the repo venv. FSST reference library cloned at `e638d4cf8c26129d73c242a4127b42b975de5b63` (MIT), built scalar (no AVX-512). Provenance: `results/fsst_provenance.txt`.

## 2. Blocker A — EmbeddedConventional 21.5 MB vs 1.65 MB: RESOLVED (explained)
Script `scripts/blockerA_diag.cpp`; output `results/blockerA_diag.txt`. One trace (`data/corpus_events_Zephyr.txt`, 2,605,813 events: 189,053 enter / 189,041 exit / 703,727 declare / 1,523,992 use; 228,739 unique names), one allocator counter (`heap_counter.hpp`).

| class replayed | bare final heap | with `unordered_set<string>` of unique names |
|---|---|---|
| `include/historical/embedded_conventional_symbol_table.hpp` (flat arena + Robin-Hood index; arena append-only) | 21,534,528 B | 41,742,440 B |
| `EmbeddedConventionalSymbolTable` in `capacity_proto_bench.cpp` (branch `research/final-claim-battle`): `vector<unordered_map<string,int>>`, frees names on scope exit | 1,651,544 B | 21,918,320 B |
| METIS-X | 4,729,136 B | 24,952,328 B |

The unique-set object alone costs 20,207,240 B. Reproduced: canonical CSV 41,742,312 (harness `metis_x_bench_main.cpp`, unique set inside the scope), ablation 21.53 MB (no unique set), capacity prototype 1,651,448 B. **Cause:** (1) `metis_x_bench_main.cpp` keeps `unordered_set<string> uniqueSymbols` inside the measured `heap::Scope`; (2) two *different implementations* share the name `EmbeddedConventionalSymbolTable` (the historical one never reclaims string-arena bytes at scope exit; the capacity-harness one destroys per-scope maps, leaving only the 12 still-open scopes at trace end). Neither value is a defensible "the Zephyr baseline"; all METIS-X memory comparisons are **excluded** from the study. Trace identity and event counts agree.

## 3. Blocker B — `stock_adapter` images ≈ 264 B above no-symtab: RESOLVED (diagnosed + fixed in new harness)
`research/metis_arch/link_image.py` links a hand-written library list and **never links the generated `symtab.c.obj`**; its map (`work/base/hello_world_mps2_base_stock_adapter.map`) shows `libsymtab.a(symtab_stub.c.obj)` with `z_symtab_entries` = 4 B and `z_symtab` = 12 B. The real generated object has `z_symtab_entries` = 0x4f0 B (158 × 8) in `/tmp/zephyr_build_symtab_y`. So the stock-adapter images contain the stock *lookup function* run over a stub table: not a stock-vs-proposal comparison. Fix (new harness only, historical artifacts untouched): every arm is relinked with the build's own link command (`ninja -t commands`), STOCK uses `gen_symtab.py` output regenerated to a fixed point, and `read_stock_table()` reads `z_symtab_entries` out of the final ELF and requires entry-by-entry equality with the manifest; candidate arms must lack `symtab_find_symbol_name`. Result: STOCK check passed on 10/10 workloads (`stock_ok`). The historical 4-workload relink numbers remain unused.

## 4. Sequence of events
1. Feasibility sweep (`scripts/sweep.py`, results `results/sweep_results.jsonl`): ARM `mps2/an385` 103 samples → 59 built, N = 172…2,337; RISC-V `qemu_riscv32` restricted to the 59 ARM-buildable samples, stopped after 20 for time (decided before any size result). Sweep logged only success and N.
2. Workload selection (`scripts/select_workloads.py`) → 10 workloads; development workload `qemu_cortex_m3/hello_world` used only to debug the harness (excluded).
3. Protocol + scripts frozen; hashes and UTC timestamp in `FREEZE_MANIFEST.txt` (19:16:57 UTC, before any file in `results/workloads/`).
4. Main run: `scripts/gapfill_run.py` per workload (4 in parallel), 14 arms each, fixed-point relink.
5. Frozen analysis `scripts/analyze.py` → `results/analysis/`. Exploratory analysis `scripts/analyze_exploratory.py` → `results/analysis_exploratory/`.
6. Execution validation `scripts/gapfill_exec.py` → `results/exec/`.

## 5. Deviations and anomalies (nothing in the frozen thresholds was changed)
* **D1 RISC-V flat-binary quantisation.** On `qemu_riscv32` the objcopy flat binary changes only in exact 4096-byte steps (page-aligned section placement), e.g. `hello_world_rv32` has bins {24,636; 28,732} across 14 arms while the allocated-section sum varies by hundreds of bytes. The frozen metric therefore misstates small differences on RISC-V (ARM: bin equals allocated-section sum within ≤8 B on 6/6 ARM workloads, measured for the PROPOSED arm). Frozen result stays primary; `analyze_exploratory.py` re-runs the same gates with image := Σ allocated PROGBITS section sizes (recorded per arm). **Exploratory.**
* **D2 `net_ptp_mps2` KALL16 did not converge.** Image alternates 242,152 ↔ 242,160 B over iterations 4–8 (period-2 cycle; manifest never stabilises, N = 1,570). All other 139 arm×workload fixed points converged in 2–3 iterations. Because KALL16 ∈ E and T3 is defined over every arm in E/H0/P/FSST, T3 is triggered as frozen. The 11,811 "decoder mismatches" on this workload are all KALL16 and are an artefact: the host check compares against the last iteration's manifest while the source encodes the previous one. Cause of the oscillation not investigated further (time).
* **D3 RISC-V stub check.** On RV32, Zephyr arch code references `symtab_find_symbol_name` (96 B), so the stock lookup stays linked in every candidate arm (frozen check `stubs_ok` false on 4/4 RISC-V workloads). The 96 B is common to all candidate arms, so it cancels in E/H/P/FSST comparisons; it inflates candidates vs STOCK only. Reported as G5 failure per the frozen definition.
* **D4** RISC-V sweep truncated (20/59 samples) — selection happened before measurement; coverage note in the protocol.
* **D5** `STOCK` consumer is the historical stock adapter; flash-only accounting (caller buffer RAM not counted).

## 6. Execution validation (QEMU, outside the gates)
`gapfill_exec.py` builds a *separate* validation image (not the size-measured one) containing a `SYS_INIT` self-test that calls the arm's real `lookup_symbol()` on every symbol start, start+1, midpoint and 3 out-of-range probes, hashes (name, offset), and prints the hash next to the host oracle's expected hash. 8/8 PASS: `hello_world_rv32` (QEMU `virt`, RV32) and `net_sockets_socketpair_mps2` (QEMU `mps2-an385`, Cortex-M3) × {B1_B16_FC16, B1_B16_FSST, B1_B16_TOK, KALL256}; console output in `results/exec/*.json`. This shows the decoders produce the oracle's answers when executed on the emulated targets for those addresses. It does **not** show timing, power, or behaviour under a real fault path.

## 7. Cleanup
Intermediate build trees (`work/`: ~6.8 GB) and SDK download archives were removed at the end; stripped ELFs, link maps, generated C and per-workload JSON were kept under `results/evidence/` (see `ARTIFACT_MAP.md`). Pre-existing untracked research directories were not touched.
