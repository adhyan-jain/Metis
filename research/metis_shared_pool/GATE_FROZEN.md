# METIS Final Cheap Gate — Cross-Consumer String Sharing: Frozen Decision Rule

Date: 2026-10-11
Audit directory: `research/metis_shared_pool/`

---

## 1. Objective & Scope

Determine whether sharing strings between METIS runtime symbolization and other actual flash-resident Zephyr consumers has enough remaining headroom to justify an architecture experiment.

This audit is strictly read-only:
- No architecture implementation.
- No modifications to existing sources.
- No firmware rebuilds.
- No modification of past gates or historical reports.
- Analysis uses existing ELFs, linker maps, section allocations, and generated artifacts.

---

## 2. Authoritative Workloads & Base Images

The audit evaluates the four authoritative workloads:
1. `hello_world_mps2` (mps2/an385, Cortex-M3, flat .bin baseline: 17,568 B [BLPR] / 17,576 B [B1_FC16] / 19,792 B [Stock])
2. `shell_module_mps2` (mps2/an385, Cortex-M3, flat .bin baseline: 80,624 B [BLPR] / 80,664 B [B1_FC16] / 89,112 B [Stock])
3. `echo_server_mps2` (mps2/an385, Cortex-M3, flat .bin baseline: 269,448 B [BLPR] / 269,808 B [B1_FC16] / 291,936 B [Stock])
4. `hello_world_nrf52840` (nrf52840dk/nrf52840, Cortex-M4, flat .bin baseline: 27,120 B [BLPR] / 27,084 B [B1_FC16] / 30,200 B [Stock])

---

## 3. Potential Flash-Resident String Consumers

Eligible candidates for cross-consumer sharing must be **actually resident in loadable flash sections** (e.g. `.rodata`, `.text`, `.rodata.str1.1`):
1. **METIS symbolization strings**: Compressed function names in METIS symbol tables.
2. **`__func__` strings**: Function name strings embedded by the compiler for assertions or diagnostics.
3. **Diagnostic / log strings**: Format strings and printk constants that duplicate function names.
4. **Device / Shell strings**: Device driver names (`struct device`) and shell command strings.
5. **LLEXT runtime strings**: Flash-resident export/import tables where actually resident on device.

**Strict Exclusions from MCU Flash**:
- Host-only debug sections (`.debug_*`).
- Unallocated ELF symbol/string tables (`.symtab`, `.strtab`).
- Off-flash LLEXT string tables linked at address 0 without `SHF_ALLOC` (`llext_exports_strtab`).

---

## 4. Predeclared Feasibility Gate (Immutable)

The feasibility screen **PASSES** if and only if:
1. **Whole-image flash reduction $\ge 1.0\%$** on at least three of the four authoritative workloads.
2. The passing set **MUST include `echo_server_mps2`**.
3. **Net achievable saving**: The reduction is calculated after charging all unavoidable references, indices, metadata, adapter/decoder code deltas, alignment, and any lost compression benefits from removing strings from METIS's tokenized/front-coded representation.
4. Existing linker string merging (`.rodata.str1.1` / ICF / compiler constant pooling) already present in the baseline image is subtracted; only additional removable bytes count.

If the optimistic bound fails this criterion, the screen **FAILS** and the METIS architecture search ends immediately without prototyping.
