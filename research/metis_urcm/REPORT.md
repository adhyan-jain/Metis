# URCM (Unified Restartable Crash Metadata) — gate report, 2026-10-11

**Verdict: ENGINEERING RESULT ONLY — NO DEFENSIBLE NOVELTY.** URCM was stopped at the feasibility screen; the full architecture was NOT implemented.

## What was measured (real artifacts)
Inputs: the four existing stock Zephyr ELFs (`/tmp/zephyr_build_*`, DWARF 5 `.debug_line` present) and the frozen symbol manifests `data/zephyr_symtab_*.json`
(same function inventory as the METIS tournament). `extract_lines.py` builds the normalised line inventory (rules in its docstring: last row per address wins,
equal consecutive (file,line) merged, gc'd/tombstoned sequences dropped, column/is_stmt ignored, innermost row for inlined code = non-inline locations only).

| workload | symbols | line ranges (events incl. gaps) | files | symbol side (B1_B16_FC16 payload) | line side, my restarted-delta B=16 | order-0 entropy floor of line side | **optimistic sharing saving** | % of metadata (my codec / entropy floor) |
|---|---|---|---|---|---|---|---|---|
| hello mps2 | 157 | 2211 (2233) | 80 | 1,733 B | 8,115 B | 4,723 B | 439 B | 4.46% / 6.80% |
| shell mps2 | 556 | — (9912) | — | 5,826 B | 33,267 B | 19,004 B | 963 B | 2.46% / 3.88% |
| echo mps2 | 1380 | — (30963) | — | 14,433 B | 111,217 B | 60,417 B | 2,177 B | 1.73% / 2.91% |
| hello nrf52840 | 225 | — (3926) | — | 2,422 B | 14,366 B | 8,567 B | 531 B | 3.16% / 4.83% |

Raw: `results/feasibility_screen.json`, `logs/feasibility_screen.log`.

**The optimistic bound is deliberately generous**: it deletes the ENTIRE symbol address index (stream + directory), makes function-start events cost zero extra bytes, and
removes one of the two token dictionaries, with no joint-decoder or state-snapshot cost. 99+% of function starts do coincide with a line-event address (152/157, 552/556, 1376/1380, 220/225),
so the shared-address premise is sound — but the thing it shares is small. Source-line data is 4–8× the symbol table (hundreds of bytes/KB of events vs ~1.3 B/symbol of addresses),
so any joint scheme is dominated by line-record coding, where URCM and a separate line map are the same algorithm.

**Criterion 5 (≥10% on ≥3/4) is unreachable**: best case 6.8% (hello, against an entropy floor no practical codec reaches), 1.7–4.5% against a real codec. Criteria 6–7 follow (whole-image saving ≤ the metadata saving, before any
joint-decoder cost). The gate stops here as the brief requires. Not built: decoder, generated C, differential tests vs the ELF, linked images, RAM/stack — none of them can rescue criterion 5.

## Novelty gate (focused; not exhaustive)
- **Linux kallsyms lineinfo (Levin, v9 3/4, [openwall](https://lists.openwall.net/linux-kernel/2026/09/17/1878))**: blocks of 64, uncompressed per-block (first address, byte offset) index, binary search then ≤64 sequential delta decodes
  (ULEB128 addr, SLEB128 file/line), ~2.7× smaller (10→3.8 B/entry). Verified from the fetched page: it keeps **separate arrays** from kallsyms; the only coupling is that the containing symbol's start address bounds the lookup.
  This is the same mechanism as URCM's line half, in the kernel, with function-start awareness.
- **Zephyr symtab ([docs](https://docs.zephyrproject.org/latest/services/debugging/symtab.html))**: function names + addresses, `symtab_find_symbol_name()` only; no line/file support documented. So the *gap* in Zephyr is real, but filling it is an engineering port.
- **LLVM GSYM** ([llvm-gsymutil](https://llvm.org/docs/CommandGuide/llvm-gsymutil.html)): described as compact, fast-lookup format covering address ranges, line tables and inline info. The fetched page does not describe the internal layout; from my own knowledge (UNVERIFIED here) it
  stores per-function records with line tables under a sorted address table and a shared string/file table — i.e. a joint function+line representation already exists off-target. Breakpad `.sym` (FUNC + line records) is similar prior art (also unverified in this session).
- Not searched to exhaustion: Zephyr coredump, embedded crash-dump tools, restart-state papers.

Strongest reviewer objection: "A joint function+line address-sorted format is GSYM; block-restart delta line tables are Levin's lineinfo; you merged two address streams and saved ≤7% of a metadata set dominated by line records."
Falsification evidence for the hypothesis: the screen above (already observed).

## Decision (brief §7)
Recommendation: **freeze METIS as an engineering result** (symbolization-only: B1_B16_FC16, 1.6–3.5% whole-image vs the strongest conventional design, ≈20–25% symtab; see `research/metis_arch/REPORT.md`, whose own finding is that the gain is mostly a compact address index already deployed in Linux for lineinfo). Do not pursue URCM. No refinement is recommended: the remaining unaddressed mechanism (sharing addresses across symbol and line streams) is now bounded at ≤4.5% of a metadata set, and the only full-image effect would be smaller still.

## Not done / theoretical
Everything past the screen: no URCM implementation, no C lookup, no baselines 1–2 as firmware, no fixed-point relink, no timing. Line-side byte counts are Python-exact for my codec and an entropy lower bound, not linked firmware. Only 4 workloads; symbol manifests come from stock images (same limitation as the tournament). The line-side extraction was not cross-checked against `llvm-addr2line` because no lookup implementation was built.
