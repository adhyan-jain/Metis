# Patent Claim Feature Matrix: METIS-X Engine

This document provides a technical matrix mapping structural features and technical implementation elements of METIS-X to their underlying source code files and technical interactions.

---

## Technical Feature & Code Mapping Matrix

| Feature ID | Technical Feature Element | Structural Interaction | Supporting Code Location | Verified Technical Effect |
| :--- | :--- | :--- | :--- | :--- |
| **F-01** | **32-Byte Fixed-Size Slot Structure** | Aligns 2 slots per 64B L1 hardware cache line; packs 15B inline SSO string, 64-bit compact hash, scope ID, and payload | `include/metis_x.hpp` (`struct Slot`) | Eliminates double-indirection pointer chasing; optimizes cache line density |
| **F-02** | **64-Bit Inline Compact Hash Verification** | Stores FNV-1a hash directly in metadata header; compares integer hashes during probing before string evaluation | `include/metis_x.hpp` (`MetisX::lookup`, `MetisX::insert`) | Rejects non-matching slots in $O(1)$ CPU cycle without accessing memory string buffers |
| **F-03** | **Recyclable Scope-Bound Arenas** | Allocates strings $> 15\text{B}$ out of non-moving contiguous arenas; instant bump-pointer reset on scope exit | `include/metis_x.hpp` (`ScopeArena`, `enter_scope`, `exit_scope`) | Achieves $0$ heap allocations across $2.86\text{M}$ lookups; instant $O(1)$ scope reclamation |
| **F-04** | **Robin Hood Bounded Open Addressing** | Maintains age/displacement counters per slot to bound maximum probe sequences | `include/metis_x.hpp` (`MetisX::insert_slot`) | Prevents tail-latency degradation during high load factors |
| **F-05** | **Sibling Scope Block Reuse** | Recycles memory blocks freed by out-of-scope sibling blocks during compiler AST traversal | `include/metis_x.hpp` (`ArenaChunkPool`) | Yields $87.1\%$ physical memory reduction ($A_2 \to A_5$) on Zephyr AST symbol workloads |

---

## Detailed System Interaction Diagram

```
+-----------------------------------------------------------------------------------+
| METIS-X 32-Byte Slot Layout (Aligned to 64B L1 Cache Line)                       |
+-----------------------------------+-----------------------------------------------+
| Bytes 0..7  : 64-Bit FNV Hash     | Fast 64-bit integer compare during probing    |
| Bytes 8..11 : 32-Bit Scope ID     | Scope visibility & nesting validation         |
| Bytes 12..15: 32-Bit Symbol ID    | Internal declaration index                    |
| Bytes 16..31: 15B SSO / Arena Ptr | Inline string if <=15B; Arena Ptr if >15B     |
+-----------------------------------+-----------------------------------------------+
```
