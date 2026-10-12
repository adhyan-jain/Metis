# Claims Ledger

Status of evidence: **R** = reproduced from existing artifacts, **N** = newly measured here, **I** = inferred, **U** = unverified (not used). No claim is independently verified (all checks by project tooling). "Where used" = `FINAL_STATUS.md` (FS), `EXPERIMENT_LOG.md` (EL), frontend panel (FE).

| # | Claim | Source / computation | Workload & configuration | Status | Where |
|---|---|---|---|---|---|
| 1 | Historical EmbeddedConventional final heap on the 2.6 M-event Zephyr trace = 21.53 MB bare, 41.74 MB with the 20.2 MB unique-name set | `results/blockerA_diag.txt` (`scripts/blockerA_diag.cpp`) | `data/corpus_events_Zephyr.txt`, g++ -O2 | N (reproduces canonical CSV 41,742,312 B) | EL §2, FE |
| 2 | A different class of the same name (`vector<unordered_map>`) measures 1.65 MB on the same trace | same | same | N | EL §2 |
| 3 | Old `stock_adapter` images link only the 16-byte stub table, not the generated table | `research/metis_arch/work/base/*stock_adapter.map` (symtab_stub.c.obj, z_symtab_entries 4 B) vs `/tmp/zephyr_build_symtab_y` symtab.c.obj (0x4f0 B) | 4 pilot workloads | R | EL §3 |
| 4 | New harness links the real stock table (entry-by-entry match with manifest) | `results/workloads/*.json` → `arms.STOCK.stock_table`, `check_baseline_present` | 10 workloads | N | FS, EL |
| 5 | Manifest extractor reproduces the four historical manifests bit-exactly | validation run (names+addresses equal to `data/zephyr_symtab_*.json`) | hello_world, shell, echo_server, nrf52840 | N | EL |
| 6 | 139 of 140 arm×workload fixed points converge in ≤3 iterations; KALL16 on net_ptp oscillates 242,152↔242,160 B | `results/workloads/*.json` `hist` | 10 workloads | N | FS, EL |
| 7 | Compact schemes save 1.7–3.1 % (408–8,440 B) of whole flash image vs best existing | `results/analysis_exploratory/per_workload.csv` (S_total) | 9 workloads, exploratory metric | N (exploratory) | FS, FE |
| 8 | Pooled address share 71.2 %, median 70.2 %, geomean 76 % | `analysis_exploratory/gate_results.json` | 9 workloads | N (exploratory) | FS, FE |
| 9 | Frozen-metric pooled share 62.3 % (distorted by 4 KiB quantisation on RV32) | `analysis/gate_results.json` | 9 | N | FS |
| 10 | Residual name-coding gain after equalised address index: 0.43–0.83 % for N ≥ 710, −0.64 % at N = 137 | `analysis_exploratory/per_workload.csv` (g_pct) | 9 | N (exploratory) | FS, FE |
| 11 | FSST (compact table) is 0.5–1.2 % larger than front-coded dictionary for N > 1000; +7.2 % at N = 137 | same (fsst_vs_P_pct) | 9 | N (exploratory) | FS |
| 12 | G1 and G3 fail on both metrics; T3 triggered | `analysis*/gate_results.json` vs `GAP_FILL_PROTOCOL.md` §6 | 10 | N | FS |
| 13 | RISC-V flat-binary sizes move in exact 4096-B steps; ARM bin = allocated sections ±8 B | per-arm `bin` and `facts.alloc_progbits_bytes` | 10 workloads | N | EL D1 |
| 14 | Decoders return the oracle's answers when executed in QEMU (8/8 self-tests) | `results/exec/*.json` | hello_world RV32, socketpair M3; FC16, FSST, TOK, KALL256 | N (tests lookup correctness only; not timing) | FS, EL |
| 15 | FSST: Boncz, Neumann, Leis, PVLDB 13(12):2649–2661, 2020, doi 10.14778/3407790.3407851 | web search (CWI repository record) | — | verified bibliographic metadata, paper text not re-read here | references.bib |
| 16 | Partitioned Elias–Fano: Ottaviano & Venturini, SIGIR 2014, doi 10.1145/2600428.2609615 | web search (CNR record) | — | verified metadata; **not used in experiments** | references.bib |
| 17 | Linux relative-offset kallsyms: Biesheuvel, commit 2213e9a6, March 2016 | web search (kernel mirrors) | — | verified | references.bib |
| 18 | Levin kallsyms lineinfo delta compression (64-entry blocks, ULEB128, ~2.7×; patch series v2 Mar–v7 Jul 2026, under review) | lkml mirror lkml.iu.edu/2607.1/04652.html | — | verified from mirror search summaries | references.bib |
| 19 | "Cromie 2026 direct 3-byte name index" (from earlier audit) | search found no such patch | — | **U — not cited, not used** | — |
| 20 | Zephyr stock table = {offset u32, name ptr} entries + binary search | `corpora/zephyr/scripts/build/gen_symtab.py`, `subsys/debug/symtab/symtab.c` | Zephyr @403a7d79 | R (source read) | EL |
| 21 | Earlier METIS-X headline memory gains depend on baseline/harness | claims 1–2 + audit §3.1 | — | R | FE caveat |
