# Phase 3 TS-TFDR Formal Specification

**Date:** October 10, 2026  
**Repository:** `adhyan-jain/Metis`  
**Revision:** HEAD `@0b4344050873968a7f3f940d47f8b46f50a23353`  
**Phase:** Phase 2.1 (Evidence Reconciliation & TS-TFDR Feasibility Audit)  
**Deliverable:** 2 of 4 (`research/l3/phase2/phase3_ts_tfdr_spec.md`)

---

## 1. Algorithmic Overview & Core Paradigm

**TS-TFDR (Transient-Scope Tombstone-Free Displacement Rollback)** is a specialized transaction logging and rollback mechanism for Robin Hood open-addressing symbol tables.

Instead of performing iterative single-element backward shifts during `exitScope()`, TS-TFDR records every slot displacement caused by Robin Hood insertion within an active scope frame. Upon `exitScope()`, TS-TFDR traverses the active scope's displacement log in exact reverse chronological order and restores each modified slot to its pre-scope state, guaranteeing bulk $O(m)$ LIFO scope unwinding with zero tombstones.

---

## 2. Formal Data Structure & State Definitions

### 2.1 Slot Representation (`MetisXSlot`, 32 Bytes)
```cpp
struct MetisXSlot {
    uint32_t hash;            // Full 32-bit FNV-1a symbol hash
    uint16_t scopeId;         // Scope depth tag (0 = global, 0xFFFF = empty)
    uint8_t  probeDist;       // Robin Hood probe distance (0..250)
    uint8_t  typeId;          // Compact type classification tag (0..255)
    int32_t  declId;          // Sequential declaration ordinal (-1 = empty)
    union {
        char inlineBytes[12]; // Short String Optimization (SSO <= 12B)
        char* heapPtr;        // Heap fallback pointer for names > 12B
    } name;
};
```

### 2.2 Displacement Log Entry (`DisplacementLogEntry`, 32 Bytes)
```cpp
struct DisplacementLogEntry {
    uint32_t slotIndex;       // Physical slot index modified in main slot vector
    uint32_t oldHash;         // Pre-displacement hash
    uint16_t oldScopeId;      // Pre-displacement scopeId (0xFFFF if slot was empty)
    uint8_t  oldProbeDist;    // Pre-displacement Robin Hood probe distance
    uint8_t  oldTypeId;       // Pre-displacement typeId
    int32_t  oldDeclId;       // Pre-displacement declId (-1 if slot was empty)
    union {
        char oldInlineBytes[12];
        char* oldHeapPtr;
    } oldName;
};
```

### 2.3 Scope Frame Log (`ScopeLogBuffer`)
Each active scope level $S$ maintains a contiguous displacement log vector:
$$\text{scopeLogs\_}[S] = \left[ E_0, E_1, E_2, \dots, E_{m-1} \right]$$
where $m$ is the number of slot mutations recorded during insertions in scope $S$.

---

## 3. Mutation Logging & Scope Life-Cycle Rules

### 3.1 Logging Rules During `insert(name, typeId)`
When inserting a symbol into active scope $S$:
1. **Empty Slot Occupancy:** Before writing into an unallocated slot $i$ (`scopeId == 0xFFFF`), append a log entry $E$ to $\text{scopeLogs\_}[S]$ recording $i$ with $\text{oldScopeId} = \text{0xFFFF}$ and $\text{oldDeclId} = -1$.
2. **Robin Hood Swap / Displacement:** When an incoming or shifting slot displaces an existing occupied slot at index $i$ (because $\text{existing.probeDist} < \text{incoming.probeDist}$), append a log entry $E$ to $\text{scopeLogs\_}[S]$ recording the exact pre-swap contents of slot $i$.
3. **Same-Scope Redeclaration:** If `insert()` matches a key already present in scope $S$ at slot $i$, record a log entry with slot $i$'s previous `declId` and `typeId`, update `declId` and `typeId` in place, and terminate without slot displacement.

### 3.2 Log Discard & Pruning Policy
- **Active Scopes:** Log entries **cannot** be discarded while scope $S$ or any child scope $S' > S$ is active.
- **Scope Exit:** Upon completion of scope exit rollback for scope $S$, the log vector $\text{scopeLogs\_}[S]$ is cleared and returned to a pool for reuse by future scope entries.

---

## 4. Scope Exit Rollback Procedure

Upon invocation of `exitScope()` for target scope $S$:

```
Algorithm 1: TS-TFDR Scope Exit Rollback
Input: Target scope ID S, Table slot vector T, Scope log scopeLogs_[S]
Output: Table T restored to pre-scope state

1. LogBuffer L = scopeLogs_[S]
2. For i = L.size() - 1 down to 0 do:
3.     Entry E = L[i]
4.     Slot &slot = T[E.slotIndex]
5.     If E.oldScopeId == 0xFFFF then:
6.         // Slot was empty before scope S
7.         If slot.name > 12B and slot.name.heapPtr != nullptr then:
8.             delete[] slot.name.heapPtr;
9.         End If
10.        slot.scopeId = 0xFFFF
11.        slot.declId = -1
12.        slot.probeDist = 0
13.    Else:
14.        // Slot was occupied before scope S by an outer-scope declaration
15.        slot.hash = E.oldHash
16.        slot.scopeId = E.oldScopeId
17.        slot.probeDist = E.oldProbeDist
18.        slot.typeId = E.oldTypeId
19.        slot.declId = E.oldDeclId
20.        slot.name = E.oldName
21.    End If
22. End For
23. L.clear()
24. currentScopeId = currentScopeId - 1
```

---

## 5. Reachability Invariant & Algorithmic Correctness

### Correctness Invariant Statement:
> **Theorem (Outer Scope Reachability Invariant):**  
> *For any symbol declaration $D$ residing in scope $S_{outer} < S_{closing}$ at index $idx$ with hash $H$ and probe distance $d$, executing Algorithm 1 restores slot $idx$ to hash $H$, probe distance $d$, scopeId $S_{outer}$, and declId $D_{id}$. No slot outside the displacement sequence is modified, and no tombstones are introduced.*

### Proof Sketch:
1. Robin Hood insertion is a sequence of pairwise slot swaps along a deterministic linear probe sequence starting at $H \pmod N$.
2. Each swap preserves the exact contents of the displaced slot prior to overwrite in $\text{scopeLogs\_}[S]$.
3. Because log traversal in Algorithm 1 executes in exact reverse chronological order ($L[m-1], L[m-2], \dots, L[0]$), every slot overwrite is reversed in strict LIFO order.
4. Reversing all pairwise swaps restores all slots to their exact pre-scope state $T_0$. $\blacksquare$

---

## 6. Handling Edge Cases & System Constraints

### 6.1 Rehashing While Scopes Are Active
If the load factor $\alpha \ge 0.85$ during insertion within scope $S$, the table capacity expands ($N \to 2N$).

**Rehash Log Re-indexing Policy:**
Because physical slot indices change during rehash ($idx_{old} \to idx_{new}$), active scope logs cannot use raw physical array offsets across a rehash boundary.
- **Option A (Logical Key Logging):** Log entries store `oldHash` and `oldDeclId` instead of `slotIndex`. Upon rollback post-rehash, slots are located via hash probe lookup.
- **Option B (Log Re-indexing Sweep):** When rehash builds the new slot array of size $2N$, it sweeps active log buffers $\text{scopeLogs\_}[0 \dots S]$ and updates `E.slotIndex` to match the newly assigned physical slot index in $2N$. **(Preferred)**

### 6.2 Failed Insertion (Max Probe Distance Exceeded)
If `insert()` reaches `kMaxProbeDist = 250` without finding an empty slot or valid swap position, the insertion fails.
- **Action:** Roll back only the log entries appended during *that specific failed insertion call*, leaving table state identical to pre-call state.

---

## 7. Mathematical Complexity Bounds

Let:
- $k$ = Number of declarations inserted into scope $S$.
- $m$ = Total slot displacement mutations recorded in $\text{scopeLogs\_}[S]$.
- $d_{max}$ = Configured maximum probe distance ($d_{max} = 250$).
- $\bar{d}$ = Measured average probe distance ($\bar{d} \approx 0.14 - 0.71$ steps).

### Complexity Derivation:
1. **Worst-Case Rollback Bound:**
   Each insertion causes at most $d_{max}$ displacement swaps. Thus:
   $$m \le k \cdot d_{max}$$
   Worst-case rollback latency is $O(m) = O(k \cdot d_{max})$.
2. **Average-Case Rollback Bound:**
   Under uniform hashing with load factor $\alpha \le 0.85$, expected probe distance $\mathbb{E}[d] = \bar{d} < 0.72$. Expected displacement swaps per insertion is $\le 1 + \bar{d} \approx 1.72$.
   $$\mathbb{E}[m] \le 1.72 \cdot k$$
   Therefore, expected scope exit rollback latency is **strictly $O(k)$** with a tiny constant factor ($\approx 1.72$ slot writes per declaration).

---

## 8. Technical Distinction from Generic Undo Logging

| Feature | Generic Database / STM Undo Log | TS-TFDR Displacement Log |
| :--- | :--- | :--- |
| **Granularity** | Page / Block / Structure Diff | Single 32-byte slot header diff |
| **Log Storage** | Heap-allocated transaction buffer | Contiguous stack-allocated slot log array |
| **Memory Locality** | Random pointer references | Sequential cache-line contiguous writes |
| **Rollback Operation** | Arbitrary memory block memcpy | Linear reversed slot register restoration |
| **Open-Addressing Interaction** | Unaware of hash collision chains | Exploits Robin Hood LIFO swap symmetry |
