# Phase 6: Prior-Art Source Audit & Correction

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`

This document corrects prior-art characterizations from Phase 5 using direct source-level analysis.

## 1. LLVM `ScopedHashTable`
**Phase 5 Claim:** "LLVM uses a standard DenseMap pointing to a linked list... It suffers from heavy pointer-chasing... provides no native mechanism for hierarchical scope unwinding."
**Correction:** False. LLVM's `ScopedHashTableScope` destructor iterates its own linked list of active insertions and directly updates the `DenseMap` pointers to restore shadows in $O(1)$ time per element. It *does* have a natively integrated hierarchical scope unwinding mechanism.
**The Actual METIS-X Distinction:** LLVM achieves $O(1)$ unwinding by paying for per-node allocation (`BumpPtrAllocator`) and indirection (pointer chasing). METIS-X forces a flat contiguous array to eliminate pointer chasing and per-node allocation, paying the price of $O(P)$ Robin Hood backward-shifts during deletion.

## 2. Small-String Optimization (SSO)
**Phase 5 Claim:** The 12-byte SSO + Heap Fallback represents a novel structural binding.
**Correction:** SSO is a ubiquitous, established C++ idiom (e.g., `<string>`, `fbstring`). Using an SSO string as a hash table key is standard engineering practice. The performance delta between `A2_FlatOA_RobinHood` ($0.475 \mu\text{s}$) and `A5_FullMetisX` ($0.110 \mu\text{s}$) is entirely attributable to standard SSO avoiding cache misses. It is an excellent systems optimization, but mathematically it is not a novel algorithm.

## 3. Robin Hood Backward Shift
**Phase 5 Claim:** Using backward shift for LIFO scope unwinding is a unique mapping.
**Correction:** Backward shift is the textbook deletion algorithm for Robin Hood hashing (Pedro Celis, 1986). Applying it to symbols popped from a scope stack is a direct combination of two independent, well-known techniques (scoped index arrays + standard Robin Hood deletion).

## Conclusion: The Boundary of Novelty
METIS-X contains **no isolated novel algorithmic mechanism**. It is an elegant, highly optimized synthesis of:
1. Standard Robin Hood Open Addressing.
2. Standard Small-String Optimization (SSO).
3. Standard auxiliary arrays tracking LIFO scope insertions.
4. A standard bidirectional index (`frameIndex`) to cross-reference the two structures.

The contribution is empirical and systemic: demonstrating that this specific *combination* of known techniques outperforms LLVM-style chained structures and arena-backed tables for embedded compilation workloads.
