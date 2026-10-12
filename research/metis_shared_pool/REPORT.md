# METIS Final Cheap Gate — Cross-Consumer String Sharing: Feasibility Screen Report

**Final Status:**
### **B. FEASIBILITY SCREEN FAILS — FREEZE METIS ARCHITECTURE SEARCH**

---

## 1. Executive Summary

This audit evaluated whether sharing strings between METIS runtime symbolization and other actual flash-resident Zephyr consumers (e.g. `__func__`, assertions, device names, shell commands, and diagnostic strings) has enough remaining headroom to justify an architecture experiment.

The audit was strictly read-only: no code was written, no firmware rebuilt, and all prior artifacts remain immutable.

Before measuring overlap, the decision rule was frozen in [`research/metis_shared_pool/GATE_FROZEN.md`](file:///home/adhyan/Desktop/Compiler/research/metis_shared_pool/GATE_FROZEN.md):
- **Frozen Rule SHA-256:** `891d72ee27a6211ae41d9eb2f8f9154150b922f8d596c00efce7e0d9cf033781`
- **Frozen Continuation Criterion:** Optimistic, implementation-aware whole-image flash reduction $\ge 1.0\%$ on at least 3 of 4 authoritative workloads, including `echo_server_mps2`.

### Screen Result: **DEFINITIVE FAILURE (Both Mechanisms Fail on All 4 Workloads)**
- Across all 4 workloads, **98.8% to 99.6% of METIS function names do not exist as strings anywhere else in loadable flash**.
- The theoretical absolute upper bound (assuming 100% of all duplicate strings vanish from flash with zero reference, metadata, or decoder overhead) is **0.018% to 0.055% of whole-image flash**, missing the 1.0% threshold by an order of magnitude (18× to 55× short).
- When accounting for realistic reference representation (pointers/offsets, per-entry tag bitmaps, and decoder modifications), cross-consumer sharing produces a **net flash regression** (-52 B to -128 B) because the metadata required to index external strings exceeds the compressed size of the names in METIS.

---

## 2. Deployed String Inventory & Consumer Analysis

The audit inspected the final linked ELFs, linker maps, and loadable flash sections across the four authoritative workloads:
1. `hello_world_mps2` (mps2/an385, Cortex-M3)
2. `shell_module_mps2` (mps2/an385, Cortex-M3)
3. `echo_server_mps2` (mps2/an385, Cortex-M3)
4. `hello_world_nrf52840` (nrf52840dk/nrf52840, Cortex-M4)

### Candidate Consumer Breakdown
1. **METIS Compressed Function Names**: Represented in `name_pool` using block-local front coding and pair tokens (~7.5 to 8.5 bytes per symbol).
2. **`__func__` / Assertions**: In release/optimized builds (`-Os`, `CONFIG_ASSERT=n`), Zephyr's `__ASSERT` macros expand to empty statements. No `__func__` strings are generated or retained in flash for functions that do not log them.
3. **Device Names (`struct device::name`)**: Device-tree node identifiers (e.g., `"UART_0"`, `"GPIO_0"`, `"sys_clock"`) represent peripheral labels, not C function identifiers (`uart_init`, `gpio_pin_set`). Zero function names matched device names.
4. **Shell Commands (`struct shell_cmd`)**: Command tokens (e.g., `"clear"`, `"history"`, `"version"`) are short CLI verbs. A tiny number of generic verbs (`"init"`, `"execute"`, `"enable"`) coincidentally match static helper functions.
5. **LLEXT Symbols**: As verified in [`research/metis_llext/REPORT.md`](file:///home/adhyan/Desktop/Compiler/research/metis_llext/REPORT.md), the export string table (`llext_exports_strtab`) is linked at address 0 without `SHF_ALLOC` (off-flash). On-device SLID tables use 32-bit hashes (`llext_const_symbol_area`), not string names.

### Existing Linker String Merging
The compiler and linker already merge identical string literals in loadable sections:
- Mergeable string sections (`.rodata.str1.1`) have `SHF_MERGE | SHF_STRINGS`.
- The linker (LLD/BFD) coalesces identical string constants across translation units.
- Any duplication between non-METIS consumers is already merged; sharing with METIS must justify itself against this deduplicated baseline.

---

## 3. Measured Overlap & Sharing Headroom

| Workload | Whole-Image Flash (.bin) | METIS Symbol Count | Functions with Flash Match | Match % | Total Raw Duplicate Bytes | Unique Matched String Bytes | Theoretical Max Upper Bound % | Gate Threshold | Screen Result |
|---|---|---|---|---|---|---|---|---|---|
| **`hello_world_mps2`** | 17,568 B | 157 | 1 (`main`) | 0.64% | 5 B | 5 B | **0.028%** | $\ge 1.0\%$ | **FAIL** |
| **`shell_module_mps2`** | 80,624 B | 556 | 6 | 1.08% | 41 B | 41 B | **0.051%** | $\ge 1.0\%$ | **FAIL** |
| **`echo_server_mps2`** | 269,448 B | 1,380 | 16 | 1.16% | 148 B | 148 B | **0.055%** | $\ge 1.0\%$ | **FAIL** |
| **`hello_world_nrf52840`**| 27,120 B | 225 | 1 (`main`) | 0.44% | 5 B | 5 B | **0.018%** | $\ge 1.0\%$ | **FAIL** |

### Complete Match List for `echo_server_mps2` (16 functions, 148 bytes total)
`conn_mgr_if_connect` (20 B), `init` (5 B), `net_if_down` (12 B), `close` (6 B), `recv` (5 B), `main` (5 B), `conn_mgr_if_disconnect` (23 B), `handle_data` (12 B), `bind` (5 B), `net_if_up` (10 B), `quit` (5 B), `sys_getopt` (11 B), `enable` (7 B), `send` (5 B), `idle` (5 B), `process_udp` (12 B).

---

## 4. Modeling of Sharing Mechanisms

### Mechanism A — Reuse an Existing Full String from Flash
Instead of storing the $K$ matching names in METIS's compressed pool, METIS stores a reference to the existing uncompressed string in `.rodata`:
- **Gross Bytes Removed from METIS Pool**: $K \times \text{avg\_compressed\_name\_bytes}$ (~8 B/name).
- **External Reference Overhead**: 2 bytes per matching entry (16-bit relative offset into `.rodata`).
- **Tag Bitmap**: 1 bit per symbol in table to distinguish internal compressed names from external pointers ($N/8$ bytes).
- **Decoder Modification**: Branching logic in `lookup_symbol` to handle external pointers (+36 bytes ARM code).
- **Prefix Disruption**: Removing a symbol from a front-coded block breaks LCP sharing for its successor (+1.0 B/symbol loss).

| Workload | Gross METIS Pool Removed | Ref Offset (2B/match) | Tag Bitmap | Decoder Delta | Prefix Loss | **Net Flash Saving** | Net % of Image | Gate Status |
|---|---|---|---|---|---|---|---|---|
| `hello_world_mps2` | +7.4 B | -2 B | -20 B | -36 B | -1.0 B | **-51.6 B** | **-0.294%** | **FAIL** |
| `shell_module_mps2` | +48.4 B | -12 B | -70 B | -36 B | -6.0 B | **-75.6 B** | **-0.094%** | **FAIL** |
| `echo_server_mps2` | +128.8 B | -32 B | -173 B | -36 B | -16.0 B | **-128.2 B** | **-0.048%** | **FAIL** |
| `hello_world_nrf52840`| +7.8 B | -2 B | -29 B | -36 B | -1.0 B | **-60.2 B** | **-0.222%** | **FAIL** |

*Result: Mechanism A produces a net flash expansion on all four workloads.*

### Mechanism B — Shared Canonical Pool Across Consumers
- Standard C runtime consumers (`printk("%s", ...)`, `struct device::name`, `struct shell_cmd::syntax`) require `const char *` NUL-terminated uncompressed strings in memory. They cannot take an index into a compressed pool without allocating a RAM buffer and inserting decompression calls at every call site.
- If the shared pool stores uncompressed strings, duplicate strings are already merged by the linker (`SHF_MERGE | SHF_STRINGS`). METIS pointing to them is mathematically identical to Mechanism A (net negative).
- If non-METIS consumers were converted to compressed references, caller code deltas across the kernel would add hundreds of bytes of call-site overhead and require static RAM.
- *Result: Mechanism B is bounded by Mechanism A and fails.*

---

## 5. Artifacts and Unresolved Assumptions

### Artifact Inventory
- Pre-registered gate rule: [`research/metis_shared_pool/GATE_FROZEN.md`](file:///home/adhyan/Desktop/Compiler/research/metis_shared_pool/GATE_FROZEN.md) (SHA-256: `891d72ee27a6211ae41d9eb2f8f9154150b922f8d596c00efce7e0d9cf033781`)
- Full audit results (JSON): [`research/metis_shared_pool/audit_results.json`](file:///home/adhyan/Desktop/Compiler/research/metis_shared_pool/audit_results.json)
- Summary table (CSV): [`research/metis_shared_pool/audit_summary.csv`](file:///home/adhyan/Desktop/Compiler/research/metis_shared_pool/audit_summary.csv)
- Complete matching string inventory (JSON): [`research/metis_shared_pool/string_inventory.json`](file:///home/adhyan/Desktop/Compiler/research/metis_shared_pool/string_inventory.json)
- Complete matching string inventory (CSV): [`research/metis_shared_pool/string_inventory.csv`](file:///home/adhyan/Desktop/Compiler/research/metis_shared_pool/string_inventory.csv)
- Audit analysis script: [`research/metis_shared_pool/audit_cross_consumer.py`](file:///home/adhyan/Desktop/Compiler/research/metis_shared_pool/audit_cross_consumer.py)

### Missing Artifacts & Assumptions
- **Missing artifacts**: None. All 4 authoritative ELFs, linker maps, and symbol manifests were directly present, readable, and inspected.
- **Assumptions**: The audit conservatively assumed a 16-bit relative offset (2 bytes) for external string references rather than a full 32-bit pointer (4 bytes). Using a full pointer makes Mechanism A even more negative.

---

## 6. Checksums (SHA-256)

```
891d72ee27a6211ae41d9eb2f8f9154150b922f8d596c00efce7e0d9cf033781  GATE_FROZEN.md
a14dd60f9e31d45ffec7009cb73752e2e816a75f0aeb5aefd94d3a0e5b721e90  GATE_FROZEN.sha256
7177726a26759e69ee8a3bc6f42c1fae4860b73c2419eb7b37df214ff7e2311b  audit_results.json
05ec0db2ef8e72782977f6b4dca7cb4b6bda93e36e3557e05fec22a101f3014c  audit_summary.csv
b9f33e726477e6dd27d8db5e903bc70fe7d6c63e2307221200faec7c15efd40c  string_inventory.json
dd00908eb8aebe1e3ccb89ffaa01a91d90479b4aebcbbe0efcfd0fe2c0f681a5  string_inventory.csv
```

---

## 7. Final Recommendation

**B. FEASIBILITY SCREEN FAILS — FREEZE METIS ARCHITECTURE SEARCH**

Cross-consumer string sharing has zero viable headroom:
1. Only 1 to 16 function names exist anywhere else in flash.
2. The absolute theoretical maximum bound is **0.018% to 0.055%**, failing the 1.0% gate by a factor of 18× to 55×.
3. Realistic sharing increases flash footprint due to pointer and tag overhead.
4. No further architecture prototypes or experiments are justified. The METIS architecture search is definitively closed.
