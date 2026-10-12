# EXIDX-reuse gate (frozen 2026-10-11, before any comparative-saving computation)
Observed BEFORE freezing (only): `.ARM.exidx` is 16 B in all four stock builds (section size seen in readelf). Nothing about savings was computed.

Eligible function start = an entry of the frozen METIS symbol manifest (data/zephyr_symtab_*.json), excluding the end sentinel, unique addresses (Thumb bit stripped).
Coverage = fraction of eligible starts that equal (after prel31 decode, Thumb bit cleared) the function address of a real .ARM.exidx entry (excluding the terminator/sentinel entry).

Scenarios
 S1 "as built": exidx content in the existing images.
 S2 "enable unwind tables" (only if S1 coverage is low): requires CONFIG_ARM_UNWIND-style retention of unwind tables for all functions; incremental flash is charged (>= 8 B per covered function, the minimum exidx entry size) because exidx is NOT otherwise required by the baseline configuration.

Optimistic saving bound per workload (B = strongest fair baseline = B1_B16_FC16, whose bytes are in research/metis_arch/results):
  saving = [address-stream + address-directory bytes of B1_B16_FC16 for covered starts]  - [incremental exidx bytes not already retained]
  (all decoder/adapter/residual/mapping costs set to ZERO in the candidate's favour).
PROCEED to implementation only if ALL hold:
 (1) exidx is already present AND required in a justified target config, or its incremental cost is charged as above;
 (2) coverage >= 90% of eligible starts (else the residual index duplicates the METIS index it was meant to replace);
 (3) optimistic saving >= 1.0% of whole-image flash (B1_B16_FC16 whole-image bytes as denominator) on >= 2 workloads, one being echo_server.
Otherwise STOP; no decoder is implemented. Prior-art result cannot rescue a failed numeric gate.
