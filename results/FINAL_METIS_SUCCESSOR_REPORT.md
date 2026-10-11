# Final Single Revision Report: Net Compression Benefit of On-Device Zephyr Symbol Tables

**Status:** REVISION COMPLETE  
**Pre-Registered Kill Threshold:** >200 bytes net flash savings  
**Measured Whole-Image Net Savings:** **1,664 bytes** (Checkpoint) / **1,408 bytes** (Direct)  
**Static RAM Overhead:** **0 bytes** (reentrant stack buffer, no global state)  
**Decision:** **CONTINUE**  

---

## 1. Selected Encoding & Decoder Architecture

### Architecture Overview
The evaluated compression scheme replaces Zephyr's uncompressed array of symbol descriptors (`struct z_symtab_entry` with raw 32-bit offset + 32-bit string pointer) and flat string pool with an **on-device byte-pair substring token compressor**:

1. **Token Byte Space:**
   - Standard ASCII characters `0x00..0x7F` represent literal characters.
   - Byte tokens `0x80..0xC0` (up to 65 tokens) map to high-frequency substring idioms learned offline across the symbol corpus (e.g., `_kernel_`, `_thread_`, `z_arm_`, `atomic_`, `arch_`, `cbprintf_`).
2. **Compact Address Indexing:**
   - Symbol entry offsets from `first_addr` (0xc1) are packed into a monotonic 32-bit array `z_symtab_addr_offsets[158]`.
   - Binary search over $N=158$ entries locates the target symbol index in $O(\log_2 N) \le 8$ steps.
3. **Reentrant Zero-RAM Decoder (`symtab_find_symbol_name_r`):**
   - Replaces the unsafe non-reentrant API (`symtab_find_symbol_name` returning `const char *`) with a reentrant caller-buffered contract:
     ```c
     int symtab_find_symbol_name_r(uintptr_t addr, uint32_t *offset, char *buf, size_t buflen);
     ```
   - **Zero static RAM:** `.bss` and `.data` allocation is exactly 0 bytes.
   - Decoding runs entirely on the caller's stack frame (32–48 bytes buffer allocated at call site during exception/crash/log handling).
   - Strict length truncation guards prevent buffer overflow at any buffer size down to 0 bytes.

### Two Indexing Trade-Off Variants Evaluated
- **Variant A: Checkpoint Indexing ($K=16$):** Stores a checkpoint pointer every 16 symbol boundaries (10 entries $\times$ 2 bytes = 20 bytes total indexing overhead). Random access seeks to `sym_idx / 16`, then linearly skips up to 15 null-terminators. Yields maximum flash savings.
- **Variant B: Direct Indexing ($K=1$):** Stores a 16-bit offset for every symbol (158 entries $\times$ 2 bytes = 316 bytes total indexing overhead). Decodes directly from `&z_symtab_names_compressed[offset]` in $O(1)$ time without linear scanning.

---

## 2. Component-by-Component Offline Cost Sweep

All variants were compiled using the real target toolchain:  
`arm-none-eabi-clang -mcpu=cortex-m3 -mthumb -Os` (Clang 22.1.8 / LLD 22.1.5).

### Diagnostic Baseline: Address-Order Front-Coding
As established in our diagnostic measurement, front-coding in physical address order achieves only **2,426 bytes** string storage vs. **3,144 bytes** uncompressed (ratio 0.77, only 23% reduction). Because Zephyr symbols are sorted by memory address rather than lexicographically, prefix sharing is sparse. By contrast, byte-pair token compression exploits shared substrings regardless of string position.

### Compiled Sweep Across Token Budgets (0 to 128) vs. Stock Delta (4,416 Bytes)

| Budget | Mode | Tokens | Encoded Names | Dict Pool + Offsets | Index Table (Addr + Name) | Total Rodata | Decoder Text (Thumb2) | Total Flash | Net Savings vs 4,416 B |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **0** | Direct | 0 | 3,144 B | 0 B | 948 B (632 + 316) | 4,092 B | 202 B | **4,294 B** | 122 B |
| **0** | Checkpoint | 0 | 3,144 B | 0 B | 652 B (632 + 20) | 3,796 B | 254 B | **4,050 B** | 366 B |
| **8** | Direct | 8 | 2,289 B | 83 B | 948 B | 3,320 B | 267 B | **3,587 B** | 829 B |
| **8** | Checkpoint | 8 | 2,289 B | 83 B | 652 B | 3,024 B | 313 B | **3,337 B** | 1,079 B |
| **16** | Direct | 16 | 1,980 B | 145 B | 948 B | 3,073 B | 266 B | **3,339 B** | 1,077 B |
| **16** | Checkpoint | 16 | 1,980 B | 145 B | 652 B | 2,777 B | 312 B | **3,089 B** | 1,327 B |
| **32** | Direct | 32 | 1,612 B | 246 B | 948 B | 2,806 B | 266 B | **3,072 B** | 1,344 B |
| **32** | Checkpoint | 32 | 1,612 B | 246 B | 652 B | 2,510 B | 312 B | **2,822 B** | 1,594 B |
| **64** | **Direct** | **64** | **1,278 B** | **499 B** | **948 B** | **2,725 B** | **275 B** | **3,000 B** | **1,416 B (32.1%)** |
| **64** | **Checkpoint** | **64** | **1,278 B** | **499 B** | **652 B** | **2,429 B** | **317 B** | **2,746 B** | **1,670 B (37.8%)** |
| **128** | Direct | 66* | 1,264 B | 513 B | 948 B | 2,725 B | 275 B | **3,000 B** | 1,416 B |
| **128** | Checkpoint | 66* | 1,264 B | 513 B | 652 B | 2,429 B | 317 B | **2,746 B** | 1,670 B |

*\*Note: Dictionary learning saturates at 66 tokens for the 158-symbol dataset. Beyond 64 tokens, remaining repeated substrings appear $\le 2$ times, where $|S|+2$ bytes dictionary overhead matches or exceeds raw text reduction.*

---

## 3. Whole-Image Linked Footprint Comparison

Both candidate variants were linked directly into the Zephyr 4.4.99 build pipeline (`zephyr_final`) targeting ARM Cortex-M3 (`mps2/an385`), with the decoder reachable and retained via `-Wl,-u,symtab_find_symbol_name_r`.

### Flash & RAM Footprint Table

| Configuration | Linked Flash (ELF / bin) | Flash Delta vs. SYMTAB=n | Net Flash Savings vs. Stock | Feature Delta Reduction | Static RAM | RAM Delta |
|---|:---:|:---:|:---:|:---:|:---:|:---:|
| **Stock `CONFIG_SYMTAB=n`** | **15,376 B** | — | — | — | **6,208 B** | — |
| **Stock `CONFIG_SYMTAB=y`** | **19,792 B** | **+4,416 B** | **Baseline** | **0.0%** | **6,208 B** | **0 B** |
| **Candidate 64 Direct ($K=1$)** | **18,384 B** | **+3,008 B** | **1,408 B** | **31.88%** | **6,208 B** | **0 B** |
| **Candidate 64 Checkpoint ($K=16$)** | **18,128 B** | **+2,752 B** | **1,664 B** | **37.68%** | **6,208 B** | **0 B** |

### Footprint Analysis
1. **Whole-Image Alignment:** The whole-image linked savings (1,664 B for Checkpoint, 1,408 B for Direct) closely track the compilation-unit savings (1,670 B and 1,416 B), with minor variance ($\le 8$ bytes) attributable to ELF 4-byte section alignment.
2. **Zero RAM Regression:** Static RAM remains byte-for-byte identical at **6,208 bytes** in both stock and compressed configurations.
3. **Threshold Margin:** The pre-registered kill rule required $>200$ bytes net flash savings. Candidate 64 Checkpoint delivers **1,664 bytes**, surpassing the threshold by **8.3$\times$**. Candidate 64 Direct delivers **1,408 bytes**, surpassing the threshold by **7.0$\times$**.

---

## 4. Correctness & Reconstruction Verification

Exhaustive verification tests were executed across all 12 generated variants against the reference symbol table extracted from Zephyr's generated build artifact (`data/zephyr_symtab_158.json`).

### Test Coverage Summary (902 checks per variant, 10,824 total checks):
1. **Exhaustive Exact Lookups (158 symbols):**
   - Every symbol address looked up via `symtab_find_symbol_name_r(e.addr, &offset, buf, sizeof(buf))`.
   - Verified `offset == 0` and string reconstructed matches golden name byte-for-byte.
2. **Interior Offset Lookups (316 checks):**
   - Evaluated lookups at `e.addr + 2` and `e.addr + 4` for functions with nonzero size.
   - Verified returned offset equals interior displacement and reconstructed symbol name matches exactly.
3. **Boundary Conditions (Underflow & Overflow):**
   - Addressed `< first_addr` (`0x0`, `0x50` < `0xc1`) return `?` with `offset == 0`.
   - Addressed `> last_addr` (`0x3099`, `0x4000`) return `?`.
4. **Dummy Boundary Entry:**
   - Looked up symbol index 157 (`?` at offset `0x2fd8`), verifying proper boundary behavior.
5. **Caller Buffer Truncation Safety (420 checks):**
   - Exercised buffer limits from `buflen = 0` to `buflen = len + 5`.
   - Verified that buffer never overflows, `buflen == 0` returns error (-1), and all `buflen > 0` are safely null-terminated.

**Result: 12 / 12 variants PASSED 100% of checks (0 failures).**

---

## 5. API, RAM, Stack, and Lookup Trade-offs

### API Contract & Migration
Stock Zephyr's `symtab_find_symbol_name` returned a pointer into static flash strings:
```c
const char *symtab_find_symbol_name(uintptr_t addr, uint32_t *offset);
```
Because compressed names cannot be returned as a persistent `const char *` without global RAM, the reentrant API requires the caller to provide a buffer:
```c
int symtab_find_symbol_name_r(uintptr_t addr, uint32_t *offset, char *buf, size_t buflen);
```

### Call-Site Audit
All 7 call-sites across Zephyr RTOS (`arch/arm/core/stacktrace.c`, `arch/arm64/core/fatal.c`, `arch/riscv/core/stacktrace.c`, `shell/modules/.../unwind.c`, `isr_tables_shell.c`, `sysview_config.c`, `intc_plic.c`) immediately consume the symbol name for printing or formatting:
```c
char sym_name[48];
symtab_find_symbol_name_r(frame->pc, &offset, sym_name, sizeof(sym_name));
printk("  [<%08lx>] %s+0x%x\n", frame->pc, sym_name, offset);
```
- Stack footprint: 48 bytes allocated only during exception unwinding or interactive shell listing.
- Static RAM overhead: **0 bytes**.

### Direct vs. Checkpoint Trade-off

| Parameter | Budget 64 Direct ($K=1$) | Budget 64 Checkpoint ($K=16$) |
|---|:---:|:---:|
| **Whole-Image Flash Savings** | 1,408 bytes (31.9%) | **1,664 bytes (37.7%)** |
| **Binary Search Latency** | $\log_2(158) \approx 7.3$ branches | $\log_2(158) \approx 7.3$ branches |
| **Decode Seek Latency** | **$O(1)$ direct array index** | $O(K)$ linear scan (avg 7.5 byte hops, max 15) |
| **Decoder Code Footprint** | **275 bytes** | 317 bytes |
| **RAM Footprint** | 0 bytes static | 0 bytes static |
| **Design Recommendation** | Best for latency-critical unwinding | **Best overall flash efficiency** |

---

## 6. Decisive Recommendation

### Decision: **CONTINUE**
The measured whole-image flash savings of **1,664 bytes** (Checkpoint) and **1,408 bytes** (Direct) exceed the pre-registered kill threshold of 200 bytes by **7.0$\times$ to 8.3$\times$**, with zero static RAM penalty and 100% byte-for-byte reconstruction accuracy.

### Single Next Concrete Step
Proceed to evaluate the single next larger firmware configuration:
- **Target Application:** `samples/subsys/shell/shell_module` (Zephyr 4.4.99 on `mps2/an385`)
- **Estimated Symbol Count:** $N \approx 500$ symbols
- **Objective:** Verify that dictionary token reuse scales favorably as symbol vocabulary expands, while retaining the single generated compression architecture and reentrant caller-stack API contract.
