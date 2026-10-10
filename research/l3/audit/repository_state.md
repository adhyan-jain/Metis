# Phase 0 Audit: Repository State, Data Integrity, and Research Plan

**Date:** October 10, 2026  
**Repository:** `adhyan-jain/Metis`  
**Target Level:** METIS L3 — Adversarial Algorithm Discovery & Research Execution  

---

## 1. Verified Working Tree and Git State

- **Current HEAD SHA:** `0b4344050873968a7f3f940d47f8b46f50a23353`
- **Commit Subject:** `fix(metis-x): resolve relocation hazard, expand identifier length, reconcile canonical dataset & artifacts`
- **Upstream Remote (`origin/main`):** `0b4344050873968a7f3f940d47f8b46f50a23353` (0 commits ahead/behind)
- **Local Working Tree Modifications:** Uncommitted update to `.gitignore` (added `graphify-out/` and `.gemini/`). No tracked code or data files modified.

---

## 2. Test Execution and Build Verification

Executed `./build.sh` synchronously:
- **Build Status:** Clean compilation with `g++ -std=c++14 -O2 -Wall -Wextra`.
- **Generated Binaries in `bin/`:**
  - `smoke_test`
  - `differential_test`
  - `metis_x_bench`
  - `metis_x_instr`
  - `metis_x_ablation`
  - `metis_x_failure_cases`
  - `metis_x_sweep`
  - `real_world_bench`
  - `embedded_bench`
- **Executed Test Results:**
  - `smoke_test`: **PASSED**
  - `differential_test`: **PASSED** (200 fuzzing traces + 300 relocation/displacement stress traces verified against reference map)

---

## 3. Claim-to-Data Map & Discrepancy Audit

| Claim Source | Claim Text / Metric | Canonical Data Location | Actual Recorded Data | Audit Verdict / Discrepancy |
| :--- | :--- | :--- | :--- | :--- |
| `paper/Metis.tex` (L30), `README.md` (L74) | $R=7$ repetitions across all workloads | `results/METIS_X_CANONICAL_DATASET.csv` | Zephyr: $R=5$, ESP-IDF: $R=5$, FreeRTOS: $R=7$, Arduino: $R=7$ | **Inconsistency:** Paper/README claim $R=7$ across all workloads, but CSV dataset has $R=5$ for Zephyr and ESP-IDF. |
| `paper/Metis.tex` (L30), `README.md` (L78) | Zephyr RAM: $-40.2\%$, $p_{95}$: $-45.7\%$ | `METIS_X_CANONICAL_DATASET.csv` L10-11 | Baseline: 41,742,312 B, MetisX: 24,951,976 B ($-40.22\%$). $p_{95}$: 0.151 us vs 0.082 us ($-45.70\%$) | **Verified.** Accurately matches canonical dataset. |
| `paper/Metis.tex` (L30), `README.md` (L79) | ESP-IDF RAM: $-17.6\%$, $p_{95}$: $-56.7\%$ | `METIS_X_CANONICAL_DATASET.csv` L14-15 | Baseline: 42,651,888 B, MetisX: 35,150,120 B ($-17.59\%$). $p_{95}$: 0.187 us vs 0.081 us ($-56.68\%$) | **Verified.** Accurately matches canonical dataset. |
| `paper/Metis.tex` (L30), `README.md` (L80) | FreeRTOS RAM: $-11.2\%$, $p_{95}$: $-8.0\%$ | `METIS_X_CANONICAL_DATASET.csv` L2-3 | Baseline: 1,794,768 B, MetisX: 1,594,208 B ($-11.17\%$). $p_{95}$: 0.087 us vs 0.080 us ($-8.05\%$) | **Classification Discrepancy:** README lists FreeRTOS as "JOINT WIN ($\ge 10\%$)" despite $p_{95}$ win being only $-8.0\%$ (below the $10\%$ threshold). |
| `paper/Metis.tex` (L30), `README.md` (L81) | Arduino RAM: $+2.5\%$, $p_{95}$: $-16.1\%$ | `METIS_X_CANONICAL_DATASET.csv` L6-7 | Baseline: 1,484,216 B, MetisX: 1,521,592 B ($+2.52\%$). $p_{95}$: 0.093 us vs 0.078 us ($-16.13\%$) | **Verified.** Accurately matches canonical dataset (Partial/Tradeoff). |
| `paper/Metis.tex` (L30), `README.md` (L83) | 0 hot-path dynamic allocations across 2,813,369 lookups | `results/metis_x_instrumentation.csv` | 2,813,369 lookups, 0 dynamic allocations | **Verified.** |

---

## 4. Source-Level Correctness Risks (Phase 1 Target Audit)

1. **Probe Distance Overflow Risk (`uint8_t probeDistance`):**  
   In `MetisXSlot` (`include/metis_x.hpp#L72`), `probeDistance` is an 8-bit unsigned integer (`uint8_t`). Under extreme collision clusters or high load factors, if probe distance reaches 255, `probeDistance++` will silently wrap around to 0. This corrupts Robin Hood swap order and breaks the Robin Hood early-exit lookup invariant (`s.probeDistance < dist`).
2. **Name Length Capping / Truncation (`uint16_t nameLen`):**  
   In `insert` and `resolve` (`include/metis_x.hpp#L194`), names longer than 65,535 bytes are clamped to 65,535 bytes:
   `uint16_t nameLen16 = static_cast<uint16_t>(name.size() > 65535 ? 65535 : name.size());`  
   If two distinct strings sharing a 65,535-byte prefix are inserted, they will be treated as having length 65,535 and may falsely compare equal in `memcmp(p, data, 65535)`. Long names $>65,535$ bytes must be deterministically rejected or safely handled.
3. **Scope Depth Narrowing (`uint16_t scopeId`):**  
   `enterScope()` returns `int`, but slot `scopeId` is `uint16_t` (`include/metis_x.hpp#L70`). Nesting beyond 65,535 scopes will wrap around to scope 0, corrupting scope frame tracking and `exitScope()` reclaimed entries.
4. **Rehash under Active Scope Relocation:**  
   `rehash(newCap)` relocates entries across a new power-of-two table size and invokes `updateSlotLocation`. While `test_metisx_intensive_relocation_fuzz` passed, complex interactions between displacement swaps during rehash while deep nested scopes exist require dedicated stress verification.

---

## 5. Candidate Novelty Matrix (Phase 3 Preliminary Screen)

| Candidate Concept | Proposed Mechanism | Closest Prior Art | Key Novelty Objection / Falsification Risk | Initial Disposition |
| :--- | :--- | :--- | :--- | :--- |
| **TS-TFDR** (Transient-Scope Tombstone-Free Displacement Rollback) | Scope-specific undo log recording original slot indices & Robin Hood swaps. On scope exit, reverse swaps in $O(\text{displacements})$ without backward shift or tombstones. | 1. LLVM `ScopedHashTable` (per-scope linked lists)<br>2. Robin Hood backward-shift deletion<br>3. Database/Transactional undo journals | **Rehash Hazard:** Rehash during active nested scope invalidates logged slot indices unless log is remapped or compacted during rehash. Overhead of log maintenance on insert/swap may offset scope exit savings. | **Pursue** (Rank 1 candidate; requires rehash-safe formalization in Phase 4). |
| **LS-ZCVM** (Source-Span Zero-Copy Virtual Materialization) | Store identifier references as source buffer spans `(file_id, offset, len)` to eliminate string copies for $>12$B names. | 1. Clang/LLVM `SourceManager` & `StringRef`<br>2. Symbol interning pools<br>3. FSST string compression | High prior-art overlap with compiler source location managers. Compilers frequently create synthetic/mangled identifiers not in source files, forcing dynamic fallback allocations. | **Defer** (High prior-art risk; low leverage for $<12$B inline names). |
| **GS-SRER** (Generational Scope-Slot Recycling) | Assign generation ID to slots; increment scope generation on exit to invalidate slots lazily without physical removal. | 1. Generational indices in ECS / GC<br>2. Tombstone lazy deletion | **UNSOUND FOR OPEN ADDRESSING.** Leaving stale slots in place breaks open-addressing lookup search bounds (early exit on probe distance or empty slots fails to find live elements displaced past stale slots). | **REJECT IMMEDIATELY** (Algorithmic flaw / unsound correctness). |

---

## 6. Phase-by-Phase Plan with Go/No-Go Gates

- **Phase 0:** Audit & Repository Integrity (Completed deliverable: `research/l3/audit/repository_state.md`).
- **Phase 1 (Gate G0):** Isolated Correctness Stress & Boundary Verification.  
  *Action:* Create `tests/phase1_correctness_stress.cpp` in an isolated branch/worktree. Stress test probe overflow (>255), name length (>65,535), deep scope nesting (>65,535), and rehash hazards.  
  *Gate G0:* Pass if all boundary conditions are safely bounded or fixed.
- **Phase 2:** Workload Profiling & Bottleneck Report.  
  *Action:* Profile slot displacement counts, scope exit backward-shift overheads, probe length distributions, and rehashes across the 4 real workloads + synthetic stress benchmarks.  
  *Deliverable:* Measured distribution report and formal ranking of hypotheses.
- **Phase 3 (Gate G1):** Formal Prior-Art & Patent Literature Screen.  
  *Action:* Deep search across IEEE, ACM, Google Patents, LLVM codebase for TS-TFDR mechanism.  
  *Gate G1:* Pass if TS-TFDR mechanism remains distinct from prior scoped hash tables.
- **Phase 4 (Gate G2):** Algorithm Invariant & Rehash-Safe Formalization.  
  *Action:* Define exact undo log structures, state invariants, swap logging, and rehash log-remapping algorithm.  
  *Gate G2:* Pass if complexity and correctness invariants hold mathematically under rehashes.
- **Phase 5 (Gate G3):** Prototype, Differential Testing & Preregistered Benchmark.  
  *Action:* Implement prototype in `research/l3/prototype/ts_tfdr_prototype.hpp`. Run differential fuzzing and benchmark against baselines (MetisX backward-shift, LLVM ScopedHashTable style, Tombstone Robin Hood).  
  *Preregistered Kill Criteria:* $\ge 15\%$ scope exit / churn speedup, $\le 5\%$ lookup regression, 0 dynamic allocations on hot path.  
  *Gate G3:* Pass if performance & correctness targets are met across trace suite.
- **Phase 6:** Final Manuscript & L3 Decision Report.

---

## 7. Safe Command Sequence for Benchmark Reproduction

To safely reproduce current METIS-X benchmarks without overwriting canonical datasets:
```bash
# 1. Compile active binaries
./build.sh

# 2. Run benchmark runner outputting to temporary/isolated directory
./bin/metis_x_bench --out /tmp/metis_x_reproduction.csv --reps 5

# 3. Verify reproduction numbers against results/METIS_X_CANONICAL_DATASET.csv
```
