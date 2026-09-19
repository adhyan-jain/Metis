# METIS-X Configuration Freeze Document (v0.1)

> **Date:** 2026-09-19  
> **Status:** FROZEN (Step 7 Complete)  
> **Commit:** `research/metis-x` @ `5269b60`  
> **Independent Validation:** Confirmed across R=7 repetitions under `taskset -c 0`

---

## 1. Frozen Architecture Parameters

The following parameters of `MetisXTable` (in `include/metis_x.hpp`) are **FROZEN** for the core architecture baseline:

| Parameter | Value | Description / Rationale |
|-----------|-------|-------------------------|
| `kInlineCap` | **12 bytes** | Maximum identifier length stored inline inside slot. Covers 93.4% of live Zephyr symbols. |
| `kInitialSlots` | **16** | Power-of-two initial table capacity. |
| `kLoadNum / 10` | **0.70** | Maximum load factor threshold before geometric rehashing. |
| `MetisXSlot` Size | **32 bytes** | Exactly one half cache-line (32B). Contains inline storage, declId, hashCache, scopeId, nameLen, probeDistance, repFlags, typeId. |
| Hash Function | **FNV-1a 64-bit** | Truncated to 32-bit via XOR fold (`h64 ^ (h64 >> 32)`). Cached in `hashCache`. |
| Probing Policy | **Robin Hood Hashing** | Displacement on insertion, early-exit on probe distance overflow. |
| Deletion Policy | **Backward Shift** | Whole-scope reclamation; backward-shift delete maintains RH invariant on scope exit. |
| Scope Reclamation | **LIFO Frame Index** | `scopeFrames_` stores per-scope slot indices. Destructed on `exitScope()`. |
| Memory Accounting | **`heap::Scope`** | Physical heap tracking via `malloc_usable_size` (`include/heap_counter.hpp`). |

---

## 2. Frozen Evaluation Workloads

The following two workloads are **HELD-OUT EVALUATION TARGETS** and must NOT be used for parameter tuning:

1. **Zephyr RTOS** (2,605,813 events, 228,739 unique names)
2. **ESP-IDF** (2,229,361 events, 231,075 unique names)

---

## 3. Frozen Benchmark Environment

| Element | Specification |
|---------|---------------|
| CPU Pinning | `taskset -c 0` (pin to Core 0) |
| Compiler | GCC 16.2.1 20260810 (`g++ -std=c++14 -O2 -Wall -Wextra`) |
| Repetitions | R = 7 independent timing repetitions |
| Aggregation | Median of repetition-level p95 values |

---

## 4. Frozen Validation Evidence

Validated in `results/metis_x_statistical_summary.csv` (R=7 independent run):

| Workload | Physical Final Heap | p95 Lookup Latency | Classification |
|----------|---------------------|--------------------|----------------|
| **Zephyr** | **0.6178×** (−38.2%) | **0.8485×** (−15.2%) | **JOINT WIN (≥10%)** |
| **ESP-IDF** | **0.8252×** (−17.5%) | **0.3040×** (−69.6%) | **JOINT WIN (≥10%)** |
| FreeRTOS | 0.9192× (−8.1%) | 1.0143× (+1.4%) | PARTIAL |
| Arduino | 1.0260× (+2.6%) | 0.8939× (−10.6%) | PARTIAL |

---

## 5. Freeze Rules

1. **NO parameter changes** using Zephyr or ESP-IDF as feedback.
2. **Development parameter sweeps (Step 8)** are restricted exclusively to FreeRTOS and Arduino.
3. If parameter tuning alters `kInlineCap` or `loadFactor`, the new configuration will be designated **METIS-X v0.2** and tested against held-out targets in Step 9.
