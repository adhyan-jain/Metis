# Phase 3 TS-TFDR Experimental Falsification Plan

**Date:** October 10, 2026  
**Repository:** `adhyan-jain/Metis`  
**Revision:** HEAD `@0b4344050873968a7f3f940d47f8b46f50a23353`  
**Phase:** Phase 2.1 (Evidence Reconciliation & TS-TFDR Feasibility Audit)  
**Deliverable:** 3 of 4 (`research/l3/phase2/phase3_ts_tfdr_falsification_plan.md`)

---

## 1. Executive Summary & Falsification Objective

The purpose of this plan is to define a rigorous set of adversarial experiments designed to **falsify** TS-TFDR before committing to a full production implementation. 

If TS-TFDR fails to demonstrate a statistically significant performance advantage over standard Robin Hood backward-shift deletion or if its memory logging overhead exceeds its performance benefits under realistic compiler workloads, the candidate algorithm will be formally rejected or reframed.

---

## 2. Experimental Stress Matrix & Falsification Criteria

### Table 2.1: The 7 Core Falsification Test Scenarios

| Test ID & Name | Synthetic / Workload Configuration | Primary Metric & Outcome Measured | Falsification Threshold (Disproof Criteria) |
| :--- | :--- | :--- | :--- |
| **F-01: Adversarial Long-Probe Clusters** | Table load factor $\alpha = 0.84$; forced hash collisions generating probe chains of $d = 150-200$ slots. | Insertion latency ($\mu\text{s}$) & log entry count $m$. | If log allocation overhead during `insert()` causes TS-TFDR insertion to be $>25\%$ slower than standard Robin Hood. |
| **F-02: Dense Scopes with Many Declarations** | Single scope containing $k = 10,000$ symbol declarations closed simultaneously. | `exitScope()` latency ($\mu\text{s}$) & total slot writes. | If TS-TFDR `exitScope()` latency is NOT at least $30\%$ faster than standard backward-shift deletion. |
| **F-03: Deep Nested Scopes with Heavy Shadowing** | Scope depth 50; 20 levels of variable shadowing on identical symbol names. | Lookup latency ($p_{95}, \mu\text{s}$) & probe count. | If TS-TFDR lookup latency is slower than standard `METIS-X` reference. |
| **F-04: Active-Scope Rehashing** | Insert 5,000 symbols in scope $S_{depth=5}$, forcing 3 table rehash expansions ($1K \to 2K \to 4K \to 8K$). | Scope log re-indexing latency ($\mu\text{s}$) & memory safety. | If log re-indexing overhead during rehash adds $>15\%$ to total compilation time or causes ASan failures. |
| **F-05: Mixed Inline & Heap Names** | 50% names $\le 12\text{B}$, 50% names $20-50\text{B}$; 10,000 insertions across 100 scopes. | Dynamic heap allocation count & physical peak heap memory. | If TS-TFDR leaks heap string pointers or increases peak heap footprint by $>15\%$ vs standard `METIS-X`. |
| **F-06: High Scope Churn (Zero-Lookup)** | 5,000 rapid scope enter/exit cycles with 1-5 declarations per scope and 0 lookups. | Scope enter/exit throughput (ops/sec). | If TS-TFDR scope management overhead is slower than standard LIFO scope frame stack recycling. |
| **F-07: Log Overhead vs. Saved Relocation** | Run across all 4 canonical AST corpora (Zephyr, ESP-IDF, FreeRTOS, Arduino). | Net execution time (total compilation stream $\mu\text{s}$) & log RAM bytes. | **Primary Gate Falsifier:** If TS-TFDR fails to reduce total end-to-end execution time on Zephyr and ESP-IDF traces by at least $5\%$. |

---

## 3. Evaluation Baselines & Comparison Framework

To ensure a fair evaluation, TS-TFDR will be benchmarked against four reference baselines:

1. **`METIS-X Reference` (Phase II):** Robin Hood open addressing with iterative backward-shift deletion upon `exitScope()`.
2. **`EmbeddedConventional`:** Compact arena-backed symbol table with linear probing.
3. **`LLVM ScopedHashTable Wrapper`:** A modern C++17 wrapper around LLVM's `ScopedHashTable` linked-list architecture.
4. **`SwissTable Scope Tagged`:** An open-addressing table using 7-bit SIMD control bytes (`absl::flat_hash_map` style) with scope tags.

---

## 4. Differential Correctness Oracle

Every falsification run will be validated against a deterministic **Differential Correctness Oracle**:

```cpp
// Correctness Oracle Pseudocode
void verifyOracle(const SequenceOfEvents& stream) {
    MetisXTable refTable;
    TsTfdrTable tsTable;
    ConventionalHostSymbolTable oracleTable;

    for (const auto& ev : stream) {
        if (ev.kind == ENTER_SCOPE) {
            refTable.enterScope(); tsTable.enterScope(); oracleTable.enterScope();
        } else if (ev.kind == EXIT_SCOPE) {
            refTable.exitScope(); tsTable.exitScope(); oracleTable.exitScope();
        } else if (ev.kind == DECLARE) {
            int id1 = refTable.insert(ev.symbol);
            int id2 = tsTable.insert(ev.symbol);
            int id3 = oracleTable.insert(ev.symbol);
            assert(id1 == id2 && id2 == id3 && "Declaration ID mismatch!");
        } else if (ev.kind == USE) {
            int id1 = refTable.resolve(ev.symbol);
            int id2 = tsTable.resolve(ev.symbol);
            int id3 = oracleTable.resolve(ev.symbol);
            assert(id1 == id2 && id2 == id3 && "Lookup resolution mismatch!");
        }
    }
}
```

---

## 5. Execution Plan & Falsification Decision Rule

- **Phase 3 Prototype Stage:** Build TS-TFDR in an isolated header (`include/metis_ts_tfdr.hpp`).
- **Pass Gate Criterion:** TS-TFDR must pass **all 7 test scenarios** and pass the Differential Correctness Oracle across $10,000,000$ randomized fuzzing operations.
- **Fail Gate Criterion:** If TS-TFDR fails F-07 or F-02, the project will immediately execute the `ABANDON TS-TFDR` or `REFRAME` protocol without proceeding to paper drafting.
