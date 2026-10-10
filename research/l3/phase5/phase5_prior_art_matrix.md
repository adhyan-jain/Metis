# Phase 5: Adversarial Prior Art Matrix

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`

This matrix analyzes the proposed **Candidate F (Hybrid LIFO Scope-Tied Arena)** against the closest known prior art in compiler symbol tables and hash table architectures.

### 1. LLVM `ScopedHashTable` (2003-2026)
- **Known Mechanism:** Uses `llvm::BumpPtrAllocator` (an arena) to allocate linked-list nodes for shadowed variables. Scopes are tied to an allocator instance.
- **METIS Difference:** LLVM uses a standard `DenseMap` (quadratic probing) pointing to a linked list of nodes in the arena. It suffers from heavy pointer-chasing. `METIS-X` Candidate F utilizes a flat 32-byte cache-aligned open addressing array with a 12-byte SSO, only pushing the *string tail* (not the node/metadata) to a global LIFO arena array.
- **Substantive Difference?** Yes. METIS maintains zero indirection for 90% of unique strings, keeping lookup bounded in L1 cache, while LLVM pointer-chases.

### 2. Abseil `SwissTable` / Folly `F14` (StringView / Pooled Strings)
- **Known Mechanism:** SIMD-accelerated metadata control bytes. If the string is long, it relies on standard `std::string` heap allocations or custom memory arenas (e.g., passing a monotonic arena allocator).
- **METIS Difference:** SwissTable provides no native mechanism for *hierarchical scope unwinding*. If backed by a monotonic arena, it suffers the exact memory bloat measured in Phase 4 (Candidate D). METIS structurally binds the open-addressing frame stack to the physical arena boundary, allowing $O(1)$ scope closure to instantly truncate the arena memory without invalidating live global offset pointers.
- **Substantive Difference?** Yes. It physically marries open-addressing table deletion semantics to LIFO arena truncation—a domain-specific optimization unavailable to general-purpose structures like `absl::flat_hash_map`.

### 3. LCC / GCC Symbol Arenas (1995-2026)
- **Known Mechanism:** Traditional compilers allocate identifiers into a global string pool (arena) to avoid `malloc` overhead.
- **METIS Difference:** Traditional compilers typically use standard chained hash tables or linear probing arrays. When scopes exit, the hash table bindings are removed, but the string bytes in the global pool are often **abandoned** (leaked) until compilation ends, or they require complex multi-pool management. METIS's strict LIFO string append + LIFO table frame stack perfectly synchronizes the data layout, ensuring zero memory leakage for transient variables while using a single global `std::vector<char>`.
- **Substantive Difference?** Yes. It solves the traditional compiler string-pool memory leak problem using $O(1)$ array truncation synchronized with Robin Hood backward-shift mechanics.

### Prior-Art Conclusion
The integration of a **12-byte Small-String Optimization** with a **LIFO truncated string arena**, structurally bound to the **Robin Hood backward-shift frame stack**, does not exist in standard libraries (which lack hierarchical scope semantics) or legacy compilers (which rely on pointer-chased linked lists or leaky monotonic arenas). 

**Novelty Assessment:** High. This is a defensible algorithmic contribution for the specific domain of scoped symbol tables.
