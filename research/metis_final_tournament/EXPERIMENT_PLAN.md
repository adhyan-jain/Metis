# METIS Final Hybrid Tournament — Experiment Plan

Pre-registered plan for final bounded METIS architecture tournament.
Target workloads:
1. `hello_world_mps2` (mps2/an385, Cortex-M3, N=157)
2. `shell_module_mps2` (mps2/an385, Cortex-M3, N=556)
3. `echo_server_mps2` (mps2/an385, Cortex-M3, N=1380)
4. `hello_world_nrf52840` (nrf52840dk/nrf52840, Cortex-M4, N=225)
LLEXT bundles:
- `mod_slid`: sample base (202 exports) + 1 extension (`hello_world.llext`)
- `bundle_slid`: test base (233 exports) + 12 test extensions (`hello_world`, `logging`, `syscalls`, `object`, `relative_jump`, `inspect`, `threads_kernel_objects`, `export_dependent`, `export_dependency`, `align`, `movwmovt`, `init_fini`)

---

## 1. Candidate Definitions & Exact Encodings

### Candidate A — Block-Local Permutation Front Coding (BLPR)
- **Address side**: B1 restarted LEB128 delta blocks with fixed block size $B \in \{8, 16, 32\}$.
  Directory stores block base address `dir_base` (width `base_w`) and stream offset `dir_am` (width `am_w`).
  Address stream contains LEB128 deltas between consecutive function offsets.
- **Name side**:
  Within each address block $b$ of size $c \le B$:
  1. Names in block $b$ are sorted lexicographically into local order.
  2. Local ranks $0, \dots, c-1$ correspond to the sorted names.
  3. A local rank mapping table maps the address-order slot $kk \in [0, c-1]$ to its local name rank $r \in [0, c-1]$.
     - For $B=8$: 3 bits per entry (packed, 24 bits = 3 bytes per 8-entry block).
     - For $B=16$: 4 bits per entry (packed, 64 bits = 8 bytes per 16-entry block; nibble-packed).
     - For $B=32$: 5 bits per entry (packed, 160 bits = 20 bytes per 32-entry block).
  4. Front coding: within each block, name 0 has prefix length $p_0=0$. Each subsequent name $i \in [1, c-1]$ has prefix length $p_i = \min(\text{LCP}(\text{name}_{i-1}, \text{name}_i), 255)$, followed by suffix bytes encoded with global pair tokens (tokens $\ge 0x80$), terminated by `\0`.
  5. Restart alignment: because name restarts coincide exactly with address block boundaries, the name block start offset `dir_noff` is indexed by address block index $b$. No separate name checkpoint directory (`nbo`) or global IDs table is required.
  6. Lookup:
     - Address binary search identifies block $b$; sequential LEB128 decode gives local offset $kk$.
     - Local rank $r$ is read from rank map for entry $(b \cdot B + kk)$.
     - Sequential front-coded decode from `dir_noff[b]` unpacks $r+1$ records to reconstruct the target name.
     - Work bounds: address search $\le \lceil \log_2(\text{nblk}) \rceil$ probes $+ (B-1)$ delta steps; name reconstruction $\le r \le B-1$ steps.

### Candidate B — Adaptive Local-Order Blocks (ALOB)
- **Verification vs Prior Art & Family D**:
  Family D in `research/metis_arch` was a *joint dynamic programming over variable address block lengths* ($c \le B_{\max}$) using different *address* codecs (W1, W2, VAR, BPw) with global name streams.
  ALOB operates on fixed address blocks ($B \in \{8, 16, 32\}$) and adapts the *local name-stream ordering* per block. ALOB is distinct from Family D.
- **Encoding Modes per Block**:
  For each address block $b$:
  1. Mode 0 (`ADDR_ORDER`): Names emitted in natural address order. No local permutation table needed (rank is identity). Suffixes tokenized.
  2. Mode 1 (`LEX_LOCAL`): Names sorted lexicographically within block $b$, front-coded with local prefix sharing + tokens. Local rank map packed (width $\lceil \log_2 B \rceil$).
  3. Mode 2 (`GREEDY_PREFIX`): Greedy prefix-similarity ordering (Hamming/LCP TSP heuristic) within block $b$, front-coded + tokens. Local rank map packed (width $\lceil \log_2 B \rceil$).
- **Selection**:
  For each block, the encoder computes the exact byte cost:
  - Mode 0: `len(name_bytes)`
  - Mode 1: `len(front_coded_bytes) + len(rank_map_bytes)`
  - Mode 2: `len(front_coded_bytes) + len(rank_map_bytes)`
  The cheapest mode is selected. Block mode tags (2 bits per block) are stored in the directory.
- **Fair Accounting**:
  Token dictionary, mode bits, directory entries, rank maps, and decoder code are all accounted for.
  If a mode is never selected across the entire binary, its decoder branch is omitted.

### Candidate C — SLID-Preserving METIS–LLEXT Address Catalog
- **ABI & Compatibility Contract**:
  Preserves the standard Zephyr SLID hash contract: extensions still import symbols by 32-bit SLID hash (`sizeof(uintptr_t)` bytes of SHA-256). Rebuild tolerance across base rebuilds is preserved.
- **Base Table Structure**:
  Currently, Zephyr stores `struct llext_const_symbol { uintptr_t slid; const void *addr; }` ($8N$ bytes for $N$ exports).
  Candidate C splits the table:
  1. `uint32_t slid_table[N]`: sorted 32-bit SLID hashes ($4N$ bytes), searched via binary search.
  2. `uint16_t target_ref[N]`: compact 16-bit reference ($2N$ bytes).
     - Bit 15 = 0: Function is present in METIS address table. Bits 0–14: METIS symbol index $s$.
       Function address is recovered by METIS address decode (`metis_get_addr(s)`).
     - Bit 15 = 1: Fallback entry. Bits 0–14: index $f$ into `fallback_table`.
  3. `uint32_t fallback_table[F]`: full 32-bit addresses for exported data objects and functions not in the METIS function table ($4F$ bytes).
- **Offline Screening & Cost Model**:
  - $N$ exports, $F$ fallbacks.
  - Idealized base table: $4N + 2N + 4F = 6N + 4F$ bytes vs $8N$ bytes for stock SLID.
  - Theoretical base saving: $2N - 4F$ bytes.
  - Complete deliverable cost: base image (including new loader / METIS address decoder) + extension bundles.
  - Break-even requires $F < N/2$ and net base table saving to exceed any loader code delta.

---

## 2. Primary Metrics & Baseline Controls

### Primary Metrics
1. **Whole-image / Complete-deliverable size**: Flat `.bin` size (gap-filled) for static symbolization; base `.bin` + all shipped `.llext` files for LLEXT.
2. **Feature-specific footprint**: Static symbol table size ($S = \text{image} - \text{no\_symtab}$); LLEXT symbol-binding bytes ($D = \text{base export table} + \text{shipped extension binding bytes}$).
3. **Savings percentages & absolute deltas**:
   - $\Delta \text{image} = B_{\text{base}} - B_{\text{cand}}$
   - $\% \Delta \text{image} = 100 \times (B_{\text{base}} - B_{\text{cand}}) / B_{\text{base}}$
   - $\% \Delta \text{feature} = 100 \times (F_{\text{base}} - F_{\text{cand}}) / F_{\text{base}}$
4. **RAM & Stack**: Static `.bss`/`.data` delta, lookup stack frame bytes, buffer constraints.
5. **Work bounds**: Maximum address search probes + delta steps; maximum name reconstruction steps.
6. **Correctness**: Zero mismatches across exhaustive query of all valid symbols, odd addresses, out-of-range addresses, and buffer capacities.

### Baseline Controls
1. **Stock Zephyr symtab**
2. **Kallsyms M=256 and M=16** (transplanted and original)
3. **B1_B16_FC16** (frozen winner from prior tournament)
4. **B1_B16_TOK** (B1 address index + tokenized names in address order)
5. **Address-hardened baseline**: Best of conventional baselines equipped with B1/B2 address index.
6. **Stock SLID** for LLEXT.

---

## 3. Predeclared Continuation Gates

### Static Candidates (A and B)
To advance past Stage 1 and Stage 2 to recommendation:
1. **Geometric-mean whole-image saving**: $\ge 0.5\%$ over the best fair existing baseline across the 4 workloads.
2. **Feature footprint reduction**: $\ge 5\%$ on $\ge 3$ of the 4 workloads, including `echo_server_mps2`.
3. **No regression**: $\le 0.25\%$ whole-image regression on any individual workload.
4. **Correctness & RAM**: Zero unexplained correctness mismatches; 0 bytes static RAM increase.

### LLEXT Candidate (C)
To advance:
1. **Symbol-binding saving**: $\ge 5\%$ of denominator $D$ on $\ge 3$ available bundles.
2. **Deliverable flash**: Net positive whole-deliverable flash reduction (base `.bin` + extensions).
3. **Zero compatibility regressions**: Preserves exact SLID hash lookup and extension compatibility contract.
4. **Correct resolution**: 100% correct resolution of all tested imports.

---

## 4. Novelty Gate Criteria
- Prior art comparison against:
  - Block front coding & string dictionary compression (textbook, Buettcher et al., Moffat et al.).
  - Block-local permutations vs global rank tables (ODHST, Elias-Fano, succinct permutations).
  - Adaptive block compression (Ottaviano & Venturini 2014, PEF).
  - SLID hash tables, perfect hashing, compact dynamic symbol tables (Linux modversions, PE ordinals).
- Decision options:
  - `PAPER CANDIDATE — SURVIVES METRICS AND NOVELTY SCREEN`
  - `ENGINEERING RESULT — METRICS IMPROVE, NOVELTY NOT ESTABLISHED`
  - `STOP — NONE OF THE REMAINING HYBRIDS JUSTIFIES CONTINUATION`
