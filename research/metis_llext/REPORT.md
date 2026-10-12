# METIS — compact LLEXT symbol ABI gate (2026-10-11)

**Decision: NO-GO. The METIS architecture search ends here.** No prototype was built (the decision rule requires both gates; gate 1 fails).
Thresholds were frozen first: `GATE_FROZEN.md` (sha256 in `GATE_FROZEN.sha256`).

## 1. Exact current SLID behaviour and cost (Zephyr 403a7d79, read from source + built artifacts)
- **Build:** `llext_prepare_exptab.py` sorts the base export table by SLID and rewrites each `struct llext_const_symbol` (`{name|slid, addr}`, 2×`uintptr_t`) in place; the
  names live in section `llext_exports_strtab` linked at address 0 and are **not loaded** (no ALLOC flag). SLID = first `sizeof(ptr)` bytes of SHA-256(name), big-endian; duplicate-SLID collisions are a build error.
  `llext_inject_slids.py` writes the SLID into `st_value` (otherwise 0) of every `NOTYPE GLOBAL UND` symbol of each extension.
- **Load time:** `llext_link.c` passes `sym->st_value` (cast to a pointer) to `llext_find_sym(NULL, …)` → `llext_bsearch_sym` binary search comparing `slid` (`llext.c:166`).
  If the built-in lookup fails it falls back to **name** lookup in already-loaded extensions (`llext_find_extension_sym(name, …)`), so the extension's `.strtab` import names are still required for ext→ext imports, diagnostics and the PLT path.
- **Compatibility properties of SLID:** keyed by name hash, not position → an extension survives any base rebuild that still exports the same names (any order, any count). No ABI/version check exists; a missing name fails at load with ENOENT.
- **Measured (8 B/export on 32-bit ARM; mps2/an385, clang -Os via local toolchain):**
  sample base: 202 exports, `llext_const_symbol_area` = 0x650 = **1,616 B**; test-suite base (with exported DT devices): 233 exports = **1,864 B**; `llext_bsearch_sym` = 134 B of code; `llext_exports_strtab` 4,691 B (off-flash).
  Per extension: imports cost 16 B symtab + name+NUL in strtab + 8 B REL per use; **the SLID itself adds 0 B** (reuses `st_value`). Real sizes: `hello_world` sample 1,128 B; the 12 upstream test extensions 864–13,528 B.
- Inputs are real: the upstream `samples/subsys/llext/modules` and the 12 `tests/subsys/llext` extensions (exported from the repo's git tree to scratch; the workspace checkout itself has no LLEXT sample), built with `CONFIG_LLEXT_EXPORT_BUILTINS_BY_SLID=y`.
  Two build-environment patches (documented, no source changes): the extension partial link needed `-fuse-ld=lld -nostdlib` (host `ld` has no ARM emulation) via `sed` on `build.ninja`. Extensions were built, **not executed** (no QEMU).

## 2. Proposed representation and compatibility contract
Closed, build-scoped ABI: a generated manifest (name→dense id, host-side only); base table = `addr[id]` (4 B/export); an import's `st_value` = `fp16<<16 | id16` (V2) or id plus a per-extension fingerprint note (V1, ~76 B/ext incl. section header);
loader checks fingerprint, rejects mismatch. **Contract change:** SLID extensions survive base rebuilds; compact-ID extensions must be rebuilt for every export-set change (rejection, not tolerance). Ext→ext imports need names or a bundle-wide manifest. V2's 16-bit fingerprint is a probabilistic check (2⁻¹⁶ false accept per import).

## 3. Closest prior art and candid novelty assessment
1. **Zephyr SLID** (ST, 2024; [commit](https://juju.nz/src/michaelh/zephyr/commit/8aa6ae43ce47a990d81ea06d84fa26a584d16a30)) — same goal (integer instead of name, names off-flash). Dense IDs differ only in replacing a 4 B hash with the array position: 4 B/export.
2. **Symbian / PE / OS-2 import-by-ordinal with frozen `.def` files** ([Symbian docs mirror](https://docs.huihoo.com/symbian/nokia-symbian3-developers-library-v0.8/GUID-F9455FE8-5247-58D5-ACEF-52753B88444F.html)) — dense ordinals + a binary-compatibility contract; exactly the closed-ABI trade-off.
3. **Linux modversions CRC + vermagic** ([LKML](https://lkml.iu.edu/hypermail/linux/kernel/1611.3/03974.html)) — build-derived per-symbol/ABI fingerprints checked at module load.
4. **SOS dynamic modules** (Han et al., MobiSys 2005, [paper](https://www.usenix.org/legacy/event/mobisys05/tech/full_papers/han/han.pdf)) — I believe it uses a kernel jump table with function IDs for modules; *I did not open the paper to confirm this*.
5. **Hash-based ELF imports** ([zem.fi](https://zem.fi/2010-08-13-hashlink)) and SysV/GNU hash tables / ELF symbol indices — 4-byte import hashes and index-based lookup; Zephyr's own dense syscall IDs are also an in-tree precedent.
Overlap: all of the mechanism (dense ordinal, fingerprint, manifest). **Strongest rejection argument:** "Import-by-ordinal with an ABI fingerprint is 30-year-old practice; you traded SLID's rebuild tolerance for 4 B/export, and an SLID-compatible perfect hash with a check byte recovers two-thirds of it with no ABI change." No distinct systems contribution beyond packing was identified. Search was focused, not exhaustive (I found no LLEXT-specific compact-ID work; absence of a hit is not evidence of novelty).

## 4. Offline cost model (denominator D = SLID base export table + all symbol-binding bytes in the shipped extensions, defined before measuring)
Extension binding bytes = import names + 16 B symtab per import + 8 B REL per import use + export machinery (global syms, `.exported_sym`, its relocs). Raw: `results/slid_cost.json`, `logs/slid_cost.log`.

| bundle | exports | SLID base | ext binding | D | V2 dense+fp-in-id | V1 dense+fp note | S: SLID + strip import names | M: SLID-compatible MPH (analytic) |
|---|---|---|---|---|---|---|---|---|
| W1 sample, 1 toy ext | 202 | 1,616 | 83 | 1,699 | −804 B (47.3%) | −724 (42.6%) | −7 (0.4%) | −533 (31.4%) |
| W5 test base, 1 ext | 233 | 1,864 | 328 | 2,192 | −928 (42.3%) | −848 (38.7%) | −93 (4.2%) | −616 (28.1%) |
| W2 test base, 3 ext | 233 | 1,864 | 938 | 2,802 | −928 (33.1%) | −696 (24.8%) | −267 (9.5%) | −616 (22.0%) |
| W3 test base, 6 ext | 233 | 1,864 | 2,210 | 4,074 | −928 (22.8%) | −468 (11.5%) | −460 (11.3%) | −616 (15.1%) |
| W4 test base, 12 ext | 233 | 1,864 | 4,927 | 6,791 | −928 (13.7%) | −12 (0.2%) | −1,139 (16.8%) | −616 (9.1%) |
Against the whole deliverable (base .bin 51,644/141,112 B + extension files) the best case is **0.5–1.5%** (V2), 0.3–0.6% for the others.
Not included for V2/V1/M: loader delta (index lookup likely ≈100 B smaller than the 134 B bsearch; unmeasured, would help all dense variants), alignment (all sizes are multiples of 4). M is a size formula, not built or verified.

## 5. Verdict
- **Gate 2 (numeric, ≥5% of D on ≥3 workloads):** V2 passes on all five bundles (13.7–47.3%) — but only by (a) shrinking the table the toy bundles are dominated by, and (b) giving up SLID's rebuild tolerance, replacing it with a 16-bit probabilistic check. V1, the strong-check variant, falls to 0.2% at 12 extensions. The saving is a **constant ≈0.9 KB** that does not scale with extensions, and a compatible perfect-hash baseline reaches 66% of it. Treat as passing only under the declared closed-ABI relaxation.
- **Gate 1 (credible research distinction): FAILS** (section 3).
- Both required → **NO-GO**. Also: workloads are Zephyr's own *test* extensions (tiny, unrepresentative of production plugins), so no general deployment claim is supportable.

## Conclusion of the METIS architecture search
Recorded outcomes: static symtab compression (engineering result, address-delta already in Linux), URCM (sharing bound ≤4.5–6.8%), compact LLEXT ABI (ordinal+fingerprint is prior art; ≤1.5% of deliverable).
Final status: **freeze METIS as an engineering result**; the only item of reportable value is the measured symbolization-table work in `research/metis_arch/`. Nothing was committed or pushed.
Reproduce: see `slid_cost.py`; builds via `cmake -GNinja -S <sample> -B work/<name> -DBOARD=mps2/an385 -DCONFIG_ARM_MPU=n -DCONFIG_LLEXT_EXPORT_BUILTINS_BY_SLID=y` (+ the `sed` patch above), `ZEPHYR_TOOLCHAIN_VARIANT=cross-compile`, `CROSS_COMPILE=~/local-toolchain/bin/arm-none-eabi-`.
