# Phase 1.1: Correctness Fix Verification and Gate G0 Closure Report

**Date:** October 10, 2026  
**Repository:** `adhyan-jain/Metis`  
**Phase:** Phase 1.1 (Correctness Fixes & G0 Final Hardening Verification)  

---

## Executive Summary

All three primary correctness boundaries identified during the Phase 1 audit—Scope-ID 16-bit overflow, identifier length clamping $>65,535$ bytes, and 8-bit probe-distance overflow—have been fixed, bounded, and validated in [include/metis_x.hpp](file:///home/adhyan/Desktop/Compiler/include/metis_x.hpp). In addition, documentation discrepancies regarding baseline hash table algorithms and slot alignment guarantees were rectified.

Following the final G0 challenge, we eliminated probe-triggered capacity inflation by restricting rehash triggers strictly to standard load-factor growth thresholds (`(liveCount_ + 1) * 10 > capacity_ * 7`) and validating probe availability in advance via a 100% atomic pre-flight check (`dryRunCanInsert`). We verified this using a test-only hash seam (`METISX_TEST_HOOK`) mapping 251 distinct valid string identifiers to identical 32-bit hashes (`0x12345678`).

All fixes were verified through the dedicated regression harness ([tests/phase1_adversarial_tests.cpp](file:///home/adhyan/Desktop/Compiler/tests/phase1_adversarial_tests.cpp)), the smoke suite ([tests/smoke_test.cpp](file:///home/adhyan/Desktop/Compiler/tests/smoke_test.cpp)), the differential fuzzing suite ([tests/differential_test.cpp](file:///home/adhyan/Desktop/Compiler/tests/differential_test.cpp)), and full AddressSanitizer (ASan) + UndefinedBehaviorSanitizer (UBSan) runs.

**Gate G0 Verdict:** **PASS WITH EXPLICIT LIMITATIONS**.

---

## 1. Initial State & Working Tree Verification

- **Branch / HEAD:** `main` @ `0b4344050873968a7f3f940d47f8b46f50a23353`
- **Dirty State:** `.gitignore` (added `graphify-out/` and `.gemini/`), research audit files in `research/l3/audit/`, `tests/phase1_adversarial_tests.cpp`, `include/metis_x.hpp`, and `include/historical/embedded_conventional_symbol_table.hpp`.
- **Applied Modifications:**
  - `include/metis_x.hpp`: Applied scope limit, name length check, `typeId` range check, test hash seam (`METISX_TEST_HOOK`), atomic `dryRunCanInsert` probe check, and `insertSlotInternal` frame location tracking.
  - `include/historical/embedded_conventional_symbol_table.hpp`: Corrected header comments describing table search policy.

---

## 2. Genuine Probe-Distance Failure Path & Bounded Resource Verification

### Verification Setup (`METISX_TEST_HOOK`)
We added a C++14 header-only test seam (`METISX_TEST_HOOK`) to `include/metis_x.hpp` to inject a custom test hash function `force_constant_hash(const std::string&) -> 0x12345678`.

1. **Initial Cluster Construction:**  
   Inserted 251 distinct valid string identifiers (`identical_hash_key_0` .. `identical_hash_key_250`) into Scope 1.  
   - **Result:** All 251 insertions succeeded with distinct declaration IDs `0` through `250`.  
   - **Initial Table Capacity:** `512` slots.  
   - **Live Count:** `251` items.  
   - **Maximum Probe Distance:** `250`.  

2. **150 Repeated Probe-Distance Failure Attempts:**  
   Attempted 150 additional distinct valid string identifiers (`identical_hash_key_251` .. `identical_hash_key_400`) with valid names (24B), valid `typeId = 0`, and valid scope depth (Scope 1).  
   - **Result:** All 150 attempts bypassed input validation, reached `dryRunCanInsert()`, detected probe distance overflow $>250$, and were rejected with `-1`.  
   - **Capacity Before Attempts:** `512` slots.  
   - **Capacity After 150 Failed Attempts:** `512` slots (**100% Constant, 0 Rehashes, 0 Growth**).  
   - **Live Count After Attempts:** `251` items (100% Constant).  

3. **Symbol Reachability & Scope Exit Verification:**  
   - Every one of the 251 pre-existing symbols resolved to its original declaration ID (`0` .. `250`).  
   - A subsequent valid non-colliding insertion (`normal_valid_key_after_collisions`) succeeded cleanly with ID `251`.  
   - Scope 1 exit released all 252 symbols cleanly (`symbolsReleased == 252`, `liveCount == 0`).  
   - Zero memory leaks, double frees, or dangling pointers detected under AddressSanitizer.

---

## 3. Root Cause, Fixes, and Verification Details

### Fix 1: Scope-ID Boundary Enforcement
- **Root Cause:** `scopeId` is a 16-bit integer (`uint16_t`). Nesting beyond 65,535 scopes wrapped to 0, corrupting scope frame tracking and symbol cleanup on `exitScope()`.
- **Applied Fix:**
  - Introduced `kMaxScopeDepth = 65535`.
  - In `enterScope()`: Returns `-1` if `scopeFrames_.size() >= 65535`.
  - In `insert()`: Rejects insertions if `scopeFrames_.empty() || scopeFrames_.size() > 65535`.
  - In `exitScope()`: Protects root scope (scope 0) from exit attempt.
- **Verification:** `test_scope_depth_boundary` verified that entering scope 65,535 returns `-1` deterministically without corrupting existing scope frames or symbol bindings.

### Fix 2: Identifier-Length Boundary Enforcement
- **Root Cause:** `nameLen` is stored as `uint16_t`. String length $>65,535$ bytes was clamped to 65,535, causing silent data truncation and false redeclaration matching.
- **Applied Fix:**
  - In `insert()`: Returns `-1` deterministically if `name.size() > 65535`.
  - In `resolve()`: Returns `-1` if `name.size() > 65535`.
  - Table state and allocated heap memory remain 100% unchanged on rejection.
- **Verification:** `test_identifier_length_boundary` verified that 12B (inline), 13B (heap), and 65,535B names insert correctly, while 65,536B names are safely rejected with `-1`. Two distinct 65,536B names sharing a 65,535B prefix both return `-1` without false redeclaration or memory leak.

### Fix 3: Bounded Capacity Policy & Failure Atomicity (`dryRunCanInsert`)
- **Root Cause:** If a probe-distance overflow occurred, attempting probe-triggered rehashes repeatedly on unresolvable identical hash clusters caused table capacity to double exponentially ($C \to 2C \to 4C \dots \to 2^{28}$), consuming gigabytes of RAM without resolving the collision cluster.
- **Applied Fix:**
  - Standardized capacity growth strictly to normal load-factor thresholds (`liveCount_ * 10 > capacity_ * 7`).
  - Added `dryRunCanInsert()` pre-flight check. If an insertion cannot be accommodated within `kMaxProbeDist = 250` under the current capacity, `insert()` rejects the item immediately (`toInsert.clear()`, `nextId_--`, `return -1;`).
  - **Result:** Capacity growth is 100% bounded by physical symbol count. Repeated failed insertions perform 0 table rehashes and consume 0 additional memory bytes.

### Fix 4: `typeId` Contract Verification
- **Root Cause:** `MetisXSlot` stores `typeId` as `uint8_t` (0..255). Values outside this range were silently narrowed.
- **Applied Fix:** Added validation in `insert()`: `if (typeId < 0 || typeId > 255) return -1;`.
- **Verification:** `test_type_id_contract` verified that `typeId` values 0 and 255 succeed, while 256 and -1 are safely rejected with `-1`.

---

## 4. Layout, Baseline, and Documentation Clarifications

1. **32-Byte Slot Layout Preserved:** `sizeof(MetisXSlot) == 32` bytes. Enforcing boundary bounds preserved the 32-byte slot layout without expanding slot size to 34B or 64B.
2. **Alignment Guarantee Clarification:** `sizeof(MetisXSlot) == 32` bytes guarantees a 32-byte slot footprint, but 32-byte or 64-byte cache-line alignment is only guaranteed when using a custom aligned allocator.
3. **Baseline Code Correction:** Corrected header comments in [embedded_conventional_symbol_table.hpp:L9](file:///home/adhyan/Desktop/Compiler/include/historical/embedded_conventional_symbol_table.hpp#L9) to explicitly state **linear probing with flat string arena**, correcting the stale "Robin Hood" reference.

---

## 5. Test Suite and Sanitizer Execution Results

| Test Suite | Execution Command | Result | Sanitizer (ASan+UBSan) |
| :--- | :--- | :---: | :---: |
| **Phase 1.1 Adversarial Suite** | `./bin/phase1_adversarial_tests` | **PASSED** | **PASSED** (0 errors / 0 leaks) |
| **Smoke Test Suite** | `./bin/smoke_test` | **PASSED** | **PASSED** (0 errors / 0 leaks) |
| **Differential Fuzzing Suite** | `./bin/differential_test` | **PASSED** (200 fuzz + 300 stress traces) | **PASSED** (0 errors / 0 leaks) |

---

## 6. Remaining Documented Limitations

1. **Maximum Scope Depth:** Supported scope depth is strictly `0` to `65,534` (65,535 maximum scope frames). Depth $\ge 65,535$ returns `-1`.
2. **Maximum Identifier Length:** Maximum identifier byte length is `65,535` bytes. Length $>65,535$ returns `-1`.
3. **Type ID Range:** Supported `typeId` values are `0` to `255`. Values outside this range return `-1`.
4. **Probe Distance Bound:** Identical-hash clusters or extreme collision sequences exceeding `kMaxProbeDist = 250` return `-1` deterministically without table mutation or capacity growth.
5. **Lookup vs Insert Allocation Contract:** Identifiers $\le 12$ bytes pay 0 dynamic allocations on both insert and lookup. Identifiers $>12$ bytes pay 0 allocations on lookup, but perform 1 dynamic heap allocation on insert.

---

## 7. Final Gate Verdict

**Gate G0 Decision:** **PASS WITH EXPLICIT LIMITATIONS**.

All correctness defects are resolved, genuine probe-distance failure atomicity is mathematically guaranteed via pre-flight `dryRunCanInsert()`, repeated failed insertions exhibit 0 capacity growth, all test suites pass 100% under ASan/UBSan, and the reference implementation is verified sound.

Gate G0 is **CLOSED**. The codebase is ready for **Phase 2 Workload Profiling**.
