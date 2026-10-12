# METIS — unwind-index reuse (`.ARM.exidx`) gate, 2026-10-11

**Verdict: STOP.** The preregistered gate failed on measured evidence; no decoder, hybrid, residual index or baseline-transplant matrix was built.
Rule frozen before any comparative computation: `GATE_FROZEN.md` (`GATE_FROZEN.sha256`). Only fact seen before freezing: `.ARM.exidx` = 16 B.

## 1. Measured coverage (S1: the four existing builds; `results/coverage.json`, `logs/coverage.log`)
| workload | symtab entries | unique eligible starts | in `.ARM.exidx` | coverage | exidx entries w/o symbol | eligible missing | `.ARM.exidx` bytes | METIS address index (B1_B16_FC16) that could vanish | whole image |
|---|---|---|---|---|---|---|---|---|---|
| hello mps2 | 157 | 157 | 0 | **0%** | 2 | 157 | 16 | 183 B (153 stream + 30 directory) | 17,576 |
| shell mps2 | 556 | 556 | 0 | **0%** | 2 | 556 | 16 | 707 B (567 + 140) | 80,664 |
| echo mps2 | 1380 | 1380 | 0 | **0%** | 2 | 1380 | 16 | 1,921 B (1,486 + 435) | 269,808 |
| hello nrf52840 | 225 | 225 | 0 | **0%** | 2 | 225 | 16 | 275 B (230 + 45) | 27,084 |
The 16 B is two entries: a `0x0 → CANTUNWIND` stub and the end-of-text terminator. The same 16 B exists in the no-symtab images, i.e. it is unrelated to symbolization. Zephyr only passes `-funwind-tables` under `CONFIG_ARCH_STACKWALK` (arch/arm/core/CMakeLists.txt:28), which needs `CONFIG_EXTRA_EXCEPTION_INFO`; none of the four workloads enable either, so unwind data for functions **does not exist** in these builds.

## 2. The only justified scenario (S2: enable stack walking) — measured on a real clean build (hello_world, mps2/an385, same toolchain/libc, `work/hello_sw`)
- `.ARM.exidx` **832 B** + `.ARM.extab` 24 B = **856 B**; whole image 20,188 → 23,564 B (+3,376 B, of which only the 856 B is index/unwind data; the rest is stack-walk code and exception info).
- That build has 176 eligible FUNC symbols; **100 are in exidx = 56.8%**; 3 exidx entries have no symbol (CANTUNWIND); 76 eligible functions missing (`results/s2_hello_coverage.json`).
- Even before decoding, 856 B of unwind data to cover 100 functions is ≈4× the **entire** METIS address index (≈183 B) for the whole table.
- Format floor (8 B per function, `results/gate_result.json`): the index would cost 1,256 / 4,448 / 11,040 / 1,800 B vs the 183 / 707 / 1,921 / 275 B it replaces → net **−3.4% to −6.1%** of whole image at 100% coverage.
- Absolute ceiling (impossible: 100% coverage, exidx free, zero decoder/residual/mapping cost): 1.04% / 0.88% / **0.71% (echo)** / 1.02%. Echo fails the 1% bar even then.

## 3. Gate result (frozen rule)
(1) exidx present *and* required: **fail** (not required; absent in practice; S2 charges it fully). (2) coverage ≥90%: **fail** (0% S1, 56.8% S2-hello). (3) ≥1% on ≥2 workloads incl. echo: **fail** (echo ceiling 0.71%; S2 net negative everywhere).
Not done (by the gate): residual index design, shared-dictionary test, name↔exidx mapping cost, the five-way transplant matrix, differential tests. I therefore make no claim about which name codec benefits most from index reuse.

## 4. Prior art (focused; nothing here rescues the numeric failure)
- **Zephyr** `arch/arm/core/stacktrace.c` (read from source): binary-searches `__exidx_start..__exidx_end` by PC to *unwind*; symbolization is separate (symtab). EHABI index = 8 B entries, prel31 function address, sorted ascending (verified via LLDB's `ArmUnwindInfo.cpp` and LLVM review D24728; the ARM IHI0038 PDF itself was not opened). Linux `arch/arm/kernel/unwind.c` and barebox `ARM_UNWIND` use it the same way.
- **`.eh_frame_hdr` binary-search table** (gimli, Zig `Unwind.zig`): a sorted function-start index consulted to find the covering FDE — the DWARF analogue.
- I found no source that reuses an unwind index as the address index for name symbolization on an MCU; I did not search exhaustively and absence is not novelty. Closest design: the above unwind-index lookups. Strongest reviewer objection: "exidx exists only if you pay for unwind tables (~8 B/function, 56.8% coverage, `.cantunwind`/asm gaps), which costs more than a delta-coded address index; a residual index then duplicates the thing you removed."
- What would have defended it: a target that already ships ≥90%-covering exidx (e.g. C++/exceptions firmware) where marginal cost is zero — not any of these four workloads. What falsifies it: the measurements above.

## Final
**STOP.** METIS closes as: static symtab compression (engineering result, `research/metis_arch/`), URCM (stopped), compact LLEXT ABI (no-go), unwind-index reuse (stopped). Nothing committed or pushed. Limits: 4 workloads (one S2 build only), host-side analysis, one toolchain.
