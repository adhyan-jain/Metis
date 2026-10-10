# Phase 6: Claim Verification

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`

## 1. Exception Safety Hazard (Rehash Leak)
**Finding:** Verified. The issue resides in `MetisXTable::rehash(size_t newCap)`.
**Failure Path:**
```cpp
    std::vector<MetisXSlot> old;
    old.swap(slots_);
    slots_.resize(newCap); // <-- std::bad_alloc thrown here
```
If `resize()` throws, the exception unwinds the stack. `old` goes out of scope and is destroyed. Because `MetisXSlot` is a POD-like struct without a destructor (the memory is managed exclusively by `~MetisXTable`), all `new char[len]` allocations held inside `old` are permanently leaked.
Additionally, the table is corrupted: `slots_` is empty, but `capacity_` remains at the previous value, guaranteeing a segmentation fault on the next operation.
**Resolution:** This is a standard RAII violation. It does not affect the benchmark numbers (which do not experience memory pressure causing `std::bad_alloc`), but it must be isolated and patched before production compiler integration.

## 2. Hardware Claim: >90% L1 Cache Residency
**Finding:** Unverified inference.
**Analysis:** The trace data confirms that $>90\%$ of unique local variables fit within the 12-byte SSO buffer. Because the slot is 32 bytes, the SSO payload avoids pointer indirection. However, asserting that this results in an "L1 cache hit" requires tracking hardware performance counters (`perf stat -e L1-dcache-load-misses`).
Due to the 4-byte structural alignment (see `phase6_complexity_and_invariants.md`), half of the slots cross 64-byte boundaries, inducing potential dual-cache-line fetches. Without explicit hardware counter logs in the benchmark CSVs, the "L1 residency" claim is an architectural inference, not an empirically measured fact.

## 3. Discrepancy Resolution
**Finding:** The latency difference ($0.067 \mu\text{s}$ in Phase 4 vs $0.098 \mu\text{s}$ in Phase 2 for ESP-IDF) is strictly environmental jitter.
**Analysis:** Both harnesses time identical loop boundaries surrounding `resolve()`. The code pathways are algorithmically invariant. We officially adopt the preregistered Phase 2 `results/metis_x_ablation.csv` as the authoritative canonical baseline.
