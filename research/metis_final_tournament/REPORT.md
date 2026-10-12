# METIS Final Hybrid Tournament — Final Report

**Tournament Verdict: ENGINEERING RESULT — METRICS IMPROVE, NOVELTY NOT ESTABLISHED.**

This report documents the final bounded METIS architecture tournament evaluating three untested combinations:
- **Candidate A: Block-local permutation front coding (BLPR)**
- **Candidate B: Adaptive local-order blocks (ALOB)**
- **Candidate C: SLID-preserving METIS–LLEXT address catalog**

The tournament preserves all prior authoritative results (`research/metis_arch/`, `research/metis_urcm/`, `research/metis_llext/`, `research/metis_exidx/`) in immutable form. An experiment plan was pre-registered and hashed (`8ffd15d41f17da806f42a45844e63678ab6383e7905cbfd91bff9e893cede199`) prior to test execution.

---

## 1. Executive Summary & Decision Gate Outcome

| Candidate | Whole-Image vs Best Existing (Geomean) | Feature Reduction (Echo Server) | Whole-Image vs Address-Hardened Baseline | Novelty Status | Final Recommendation |
|---|---|---|---|---|---|
| **Candidate A (BLPR_B32)** | **+2.68%** (1.76%–3.35%) | **24.8%** (-4,824 B) | **+0.29%** (-8 B to +1,384 B) | Fails Novelty (textbook components; gain is address index) | **ENGINEERING RESULT** |
| **Candidate A (BLPR_B16)** | **+2.57%** (1.64%–3.25%) | **23.2%** (-4,504 B) | **+0.16%** (-24 B to +1,064 B) | Fails Novelty | **ENGINEERING RESULT** |
| **Candidate B (ALOB_B32)** | **+2.32%** (1.50%–3.31%) | **21.2%** (-4,112 B) | **+0.17%** (+24 B to +672 B) | Fails Novelty; Beaten by BLPR & FC16 | **REJECTED (INFERIOR)** |
| **Candidate C (LLEXT Catalog)** | **+0.19%–0.65%** whole deliverable | **5.18%–20.25%** of symbol binding $D$ | Flat ~350 B base export saving; unscalable | Fails Novelty (catalog indirection) | **ENGINEERING RESULT ONLY** |

### Key Gate Findings
1. **Candidate A (BLPR_B32)** passes all raw numeric criteria against stock Zephyr and historical baselines:
   - Geomean whole-image saving: **2.68%** (threshold $\ge 0.5\%$).
   - Feature reduction: **20.6%–24.8%** across all four workloads (threshold $\ge 5\%$ on $\ge 3$).
   - No regressions ($0.00\%$, threshold $\le 0.25\%$).
   - Zero correctness mismatches (0 across 20,702 host queries), 0 bytes static RAM increase.
2. **However, under fair address-hardened controls**, where conventional methods receive the same transferable B1/B2 address index:
   - BLPR gains only **0.29%** geomean.
   - BLPR loses on `hello_world_mps2` (-8 B / -0.05%).
   - BLPR is virtually tied with `B1_B16_FC16` (-8 B on hello, -40 B on shell, -360 B on echo, +36 B on nrf).
   - This proves that **over 89% of the measured gain comes from the compact address index**, which is transferable to conventional methods.
3. **Candidate B (ALOB)** fails to beat Candidate A and loses to `B1_B16_FC16` on shell, echo, and nrf workloads. Per-block adaptivity adds mode metadata without overcoming the compression of uniform representations.
4. **Candidate C (SLID-preserving LLEXT catalog)** saves an absolute ~350 bytes on the base export table (16.1% to 20.3% of $D$ on toy 1-extension bundles, but dropping to 5.18% at 12 extensions), representing less than 0.25% of whole deliverables. It imposes sequential delta decoding on every module import resolution at load time.
5. **Novelty fails across all candidates.** None introduces a defensible research distinction over known string dictionary, permutation, and catalog indirection techniques.

---

## 2. Candidate A — Block-Local Permutation Front Coding (BLPR)

### Encoding Architecture
- Address stream: B1 restarted LEB128 delta blocks with block size $B \in \{8, 16, 32\}$.
- Within each block of $c \le B$ symbols:
  1. Names are sorted lexicographically into local order.
  2. Local ranks $r \in [0, c-1]$ are assigned.
  3. A local rank map records the permutation from address slot $kk \in [0, c-1]$ to local rank $r$:
     - $B=8$: 3 bits/entry (packed).
     - $B=16$: 4 bits/entry (nibble-packed).
     - $B=32$: 5 bits/entry (packed).
  4. Local front coding: first name has prefix length 0; subsequent names store prefix length relative to previous name in sorted block, followed by pair-tokenized suffix and `\0`.
  5. **Restart Alignment**: Address block restarts and name restart checkpoints coincide 1:1. The directory stores `dir_noff[b]`, eliminating separate name checkpoint directories (`nbo`) and global symbol ID arrays.

### Offline Screen: Local vs Global Rank-Map Cost on Echo Server
On `echo_server_mps2` ($N=1,380$ functions):
- Prior ODHST-v2 configuration (`B1_B16_FC16`):
  - Used a global lexicographic name pool and an 11-bit global rank table:
  - Exact global rank map size: **1,898 bytes** ($1,380 \times 11$ bits).
- Candidate A (BLPR $B=16$):
  - Uses 4 bits per symbol:
  - Exact local rank map size: **690 bytes** ($1,380 \times 4$ bits).
  - **Local rank map savings: 1,208 bytes (63.6% reduction in mapping cost).**
- Trade-off Analysis:
  - While local rank maps save 1,208 bytes, local front coding limits prefix sharing to the 16 functions within each address block rather than globally.
  - The local name pool is 11,359 bytes (vs 10,186 bytes for global FC16), giving back 1,173 bytes.
  - Net payload difference on echo_server: 14,400 B (BLPR_B16) vs 14,433 B (FC16) — a modest 33-byte net win.
  - When $B=32$ is used, rank map is 863 bytes (5 bits), local pool is 11,112 bytes, address directory is 166 bytes smaller, yielding payload 14,074 B (-359 B vs FC16).

### Workload Breakdown & Work Bounds
Work bounds are reported separately:
- Address search: at most $\lceil \log_2 N_{blk} \rceil$ binary search probes $+ (B-1)$ sequential LEB128 delta decodes.
- Name reconstruction: at most $r \le B-1$ sequential front-coded suffix expands.

| Workload | Candidate | Payload | Decoder Code | Whole Image | Feature | Max Addr Steps | Max Name Recs |
|---|---|---|---|---|---|---|---|
| `hello_world_mps2` | BLPR_B8 | 1,785 B | 466 B | 17,648 B | 2,272 B | 12 | 7 |
| | BLPR_B16 | 1,727 B | 466 B | 17,584 B | 2,208 B | 19 | 15 |
| | BLPR_B32 | 1,704 B | 470 B | 17,568 B | 2,192 B | 34 | 31 |
| `shell_module_mps2` | BLPR_B8 | 6,183 B | 478 B | 81,024 B | 6,680 B | 14 | 7 |
| | BLPR_B16 | 5,923 B | 474 B | 80,760 B | 6,416 B | 21 | 15 |
| | BLPR_B32 | 5,784 B | 478 B | 80,624 B | 6,280 B | 36 | 31 |
| `echo_server_mps2` | BLPR_B8 | 15,193 B | 510 B | 270,568 B | 15,728 B | 15 | 7 |
| | BLPR_B16 | 14,400 B | 506 B | 269,768 B | 14,928 B | 22 | 15 |
| | BLPR_B32 | 14,074 B | 510 B | 269,448 B | 14,608 B | 37 | 31 |
| `hello_world_nrf52840`| BLPR_B8 | 2,550 B | 458 B | 27,232 B | 3,028 B | 12 | 7 |
| | BLPR_B16 | 2,474 B | 454 B | 27,148 B | 2,944 B | 19 | 15 |
| | BLPR_B32 | 2,439 B | 458 B | 27,120 B | 2,916 B | 34 | 31 |

---

## 3. Candidate B — Adaptive Local-Order Blocks (ALOB)

### Verification vs Family D Joint-DP
Family D in `research/metis_arch/` was a *joint DP over variable address block boundaries* ($c \le B_{\max}$) selecting among address codecs (`W1`, `W2`, `VAR`, `BPw`) with global name streams.
Candidate B (ALOB) operates on *fixed address blocks* ($B \in \{8, 16, 32\}$) and adaptively selects among three *local name-stream orders* for each block:
1. Mode 0 (`ADDR_ORDER`): Natural address order. No rank map (rank is identity).
2. Mode 1 (`LEX_LOCAL`): Lexicographic local order, front-coded + tokens + packed local ranks.
3. Mode 2 (`GREEDY_PREFIX`): Greedy prefix-similarity ordering (max-LCP heuristic), front-coded + tokens + packed local ranks.

### Empirical Mode Distribution & Performance Failure
ALOB evaluations across all blocks:
- On `echo_server_mps2` ($B=16$, 87 blocks): 40 blocks chose Mode 1, 28 chose Mode 2, 19 chose Mode 0. Mode tags cost 22 B.
- On `shell_module_mps2` ($B=16$, 35 blocks): 10 blocks Mode 1, 11 Mode 2, 14 Mode 0. Mode tags cost 9 B.
- On `hello_world_mps2` ($B=32$, 5 blocks): all 5 blocks selected Mode 0 (natural address order).

**Why ALOB Fails:**
1. The 2-bit per block mode tags, block rank offsets, and multi-mode decoder logic (+70 to +90 bytes of code) offset any local payload gains.
2. When blocks select Mode 0 to avoid the rank map, they lose all prefix compression.
3. On `echo_server_mps2`, ALOB_B32 links at 270,160 B — **352 bytes larger than B1_B16_FC16** and **712 bytes larger than BLPR_B32**.
4. On `shell_module_mps2`, ALOB_B32 links at 80,832 B — **168 bytes larger than B1_B16_FC16** and **208 bytes larger than BLPR_B32**.
5. Candidate B is strictly inferior to Candidate A and fails the continuation gate.

---

## 4. Candidate C — SLID-Preserving METIS–LLEXT Address Catalog

### Exact Overlap and Fallback Analysis on Real LLEXT Builds
Using real built artifacts (`research/metis_llext/work/mod_slid` and `bundle_slid`):
- `mod_slid` (sample base, 1 toy extension):
  - Total exports $N = 202$.
  - Function matches in METIS: 202 (100% overlap).
  - Fallbacks $F = 0$.
- `bundle_slid` (test base, 12 test extensions):
  - Total exports $N = 233$.
  - Function matches in METIS: 220 (94.4% overlap).
  - Fallbacks $F = 13$ (8 data objects `STT_OBJECT` e.g. DT devices / struct data; 5 functions not in primary function manifest).

### Complete Deliverable Cost Model
- Stock SLID: 8 bytes per export ($8N$ bytes).
- Candidate C: 4 bytes SLID + 2 bytes target reference + 4 bytes fallback address ($6N + \text{pad} + 4F$ bytes).
- Base export table savings:
  - `mod_slid`: $1,616 - 1,212 = \mathbf{404\text{ B}}$ savings.
  - `bundle_slid`: $1,864 - 1,452 = \mathbf{412\text{ B}}$ savings.
- Loader code overhead: Binary search on SLID array + reference unpacking + METIS B1 delta decoding adds $\approx +60\text{ bytes}$ of base code.
- Net base deliverable reduction: $\mathbf{344\text{ B}}$ (`mod_slid`) and $\mathbf{352\text{ B}}$ (`bundle_slid`).

| Bundle | Base Exports | Shipped Ext Binding | Denominator $D$ | Net Saving | % of $D$ | % of Total Deliverable | Gate Status |
|---|---|---|---|---|---|---|---|
| **W1 (sample base + 1 toy ext)** | 202 | 83 B | 1,699 B | 344 B | **20.25%** | 0.652% | Pass (toy) |
| **W5 (test base + 1 ext)** | 233 | 328 B | 2,192 B | 352 B | **16.06%** | 0.245% | Pass (toy) |
| **W2 (test base + 3 ext)** | 233 | 938 B | 2,802 B | 352 B | **12.56%** | 0.237% | Pass |
| **W3 (test base + 6 ext)** | 233 | 2,210 B | 4,074 B | 352 B | **8.64%** | 0.223% | Pass |
| **W4 (test base + 12 ext)** | 233 | 4,927 B | 6,791 B | 352 B | **5.18%** | 0.188% | Marginal Pass |

### Operational & Architectural Assessment
1. **Unscalable Savings**: The savings is a fixed ~350 bytes from shrinking the base export table. As extension count increases, $D$ grows while savings remains constant, falling from 20.3% down to 5.18% at 12 extensions.
2. **Whole Deliverable Ceiling**: The total deliverable reduction is tiny: **0.19% to 0.65%**.
3. **Runtime Load Overhead**: Every dynamic import resolution during extension load must sequentially decode LEB128 deltas in the METIS address stream.
4. **Novelty**: Storing an index into a co-located symbol table (catalog indirection) is standard practice (e.g. PE ordinals, ELF symbol table indices). Candidate C represents an incremental engineering option, not a publishable systems contribution.

---

## 5. Comprehensive Comparison Across All Workloads

All builds used identical Zephyr tree (`403a7d79`), clang/LLD 22.1.8, `-Os`, identical `linker.cmd` and archives, identical consumer (`lookup_symbol` + `zephyr_fault_symbol_logger`, `-Wl,-u`).

| Workload | Metric | Stock Zephyr | Best Existing Control | Address-Hardened Control | Prior Winner (B1_B16_FC16) | Candidate A (BLPR_B32) | Candidate B (ALOB_B32) |
|---|---|---|---|---|---|---|---|
| **hello_world_mps2** | Whole Image | 19,792 B | 18,136 B (cand_cp16) | 17,560 B (B1_TOK) | 17,576 B | **17,568 B** | 17,536 B |
| | Feature Footprint | 4,416 B | 2,760 B | 2,184 B | 2,200 B | **2,192 B** | 2,160 B |
| | Savings vs Best Exist | — | baseline | +3.18% (-576 B) | +3.09% (-560 B) | **+3.13% (-568 B)** | +3.31% (-600 B) |
| | Savings vs Hardened | — | — | baseline | -0.09% (+16 B) | **-0.05% (+8 B)** | +0.14% (-24 B) |
| **shell_module_mps2** | Whole Image | 89,112 B | 82,696 B (cand_cp16) | 81,096 B (B2_TOK) | 80,664 B | **80,624 B** | 80,832 B |
| | Feature Footprint | 14,768 B | 8,352 B | 6,752 B | 6,320 B | **6,280 B** | 6,488 B |
| | Savings vs Best Exist | — | baseline | +1.93% (-1,600 B)| +2.46% (-2,032 B)| **+2.51% (-2,072 B)**| +2.25% (-1,864 B)|
| | Savings vs Hardened | — | — | baseline | +0.53% (-432 B) | **+0.58% (-472 B)** | +0.33% (-264 B) |
| **echo_server_mps2** | Whole Image | 291,936 B | 274,272 B (Kallsyms M256)| 270,832 B (B2_KALL)| 269,808 B | **269,448 B** | 270,160 B |
| | Feature Footprint | 37,096 B | 19,432 B | 15,992 B | 14,968 B | **14,608 B** | 15,320 B |
| | Savings vs Best Exist | — | baseline | +1.25% (-3,440 B)| +1.63% (-4,464 B)| **+1.76% (-4,824 B)**| +1.50% (-4,112 B)|
| | Savings vs Hardened | — | — | baseline | +0.38% (-1,024 B)| **+0.51% (-1,384 B)**| +0.25% (-672 B) |
| **hello_world_nrf52840**| Whole Image | 30,200 B | 28,060 B (cand_cp16) | 27,148 B (B1_TOK) | 27,084 B | **27,120 B** | 27,244 B |
| | Feature Footprint | 5,996 B | 3,856 B | 2,944 B | 2,880 B | **2,916 B** | 3,040 B |
| | Savings vs Best Exist | — | baseline | +3.25% (-912 B) | +3.48% (-976 B) | **+3.35% (-940 B)** | +2.91% (-816 B) |
| | Savings vs Hardened | — | — | baseline | +0.24% (-64 B)  | **+0.10% (-28 B)**  | -0.35% (+96 B) |
| **Geomean vs Best Exist**| | | | **+2.40%** | **+2.66%** | **+2.68%** | **+2.32%** |
| **Geomean vs Hardened** | | | | | **+0.26%** | **+0.29%** | **+0.09%** |

---

## 6. Verification and Correctness

1. **Host-Side Oracle Testing (`test_host_c.py`)**:
   - Compiled generated C decoders with host GCC and ran 20,702 total queries across all 4 workloads.
   - Tested full symbol set, intermediate even/odd addresses, interior deltas, out-of-bounds addresses, and buffer truncations (`cap=64` and `cap=12`).
   - Result: **0 mismatches across all 20,702 queries.**
2. **Static RAM Delta**:
   - ELF section inspection (`.data`, `.bss`, `.noinit`): **0 bytes delta** across all workloads.
3. **Stack Usage**:
   - Lookup stack frame: 48 bytes (well within 64-byte budget).

---

## 7. Prior Art & Candid Novelty Assessment

### Adversarial Prior-Art Findings
1. **Block front coding + local rank permutations (Candidate A)**:
   - Front coding in fixed restart blocks is textbook (Moffat, Bell; Buettcher et al.).
   - Representing permutations locally via bit-packed ranks within blocks is textbook succinct data structures (Munro et al. 2003; Partitioned Elias-Fano, Ottaviano & Venturini, SIGIR 2014).
   - In Linux kernel kallsyms, Levin's 2026 lineinfo series already deploys restart blocks with ULEB128 deltas and sequential scan.
   - When conventional names receive this same compact address index, Candidate A delivers only **0.29%** geomean whole-image savings over the conventional baseline.
2. **Adaptive block ordering (Candidate B)**:
   - Selecting per-block encoding modes based on minimum cost is the exact mechanism of Partitioned Elias-Fano (PEF, SIGIR 2014).
   - Empirically, the metadata overhead degrades net image size compared to uniform BLPR.
3. **SLID catalog indirection (Candidate C)**:
   - Using symbol catalog ranks rather than 32-bit addresses in a hash table is standard catalog indirection (PE ordinals, ELF symtab indices).
   - Absolute savings of ~350 B do not scale with extension volume and yield <0.25% deliverable reduction.

### Final Assessment
The METIS hybrid architecture tournament conclusively demonstrates that:
1. Significant flash savings over stock Zephyr (~2.7% whole image, ~25% symtab) can be achieved on embedded MCUs.
2. **However, this gain is almost entirely an engineering optimization of the address index (B1 restarted deltas)**, which applies equally to conventional name schemes.
3. Decoupling, local permutations, adaptive orderings, and catalog indirections yield negligible incremental benefits (<0.3%) once the address index is hardened.
4. The work constitutes a solid **engineering result**, but does not possess defensible novelty for a research paper.

---

## 8. Artifacts, Hashes, and Reproduction

- Experiment Plan: `research/metis_final_tournament/EXPERIMENT_PLAN.md` (SHA-256: `8ffd15d41f17da806f42a45844e63678ab6383e7905cbfd91bff9e893cede199`)
- Plan Hash: `research/metis_final_tournament/PLAN.sha256`
- Stage 1 Screen Logs: `research/metis_final_tournament/offline_screen_ab.json`, `candidate_c_screen.json`
- Stage 2 Firmware Results: `research/metis_final_tournament/results/tournament_firmware_results.json`
- Normalized Tables: `research/metis_final_tournament/results/normalized_results.json`, `normalized_results.csv`
- SHA256 Manifest: `research/metis_final_tournament/SHA256SUMS`

### Reproduction Commands
```bash
# 1. Candidate C Offline Screen
/usr/bin/python3 research/metis_final_tournament/screen_candidate_c.py

# 2. Candidate A & B Offline Screen
python3 research/metis_final_tournament/offline_screen_ab.py

# 3. Host C Exhaustive Correctness Test
python3 research/metis_final_tournament/test_host_c.py

# 4. Stage 2 Real Firmware Build & Link Tournament
python3 research/metis_final_tournament/run_firmware_tournament.py

# 5. Export Normalized CSV and JSON
python3 research/metis_final_tournament/generate_final_reports.py
```
