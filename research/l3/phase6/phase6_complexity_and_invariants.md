# Phase 6: Complexity and Invariants Audit

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`

This document corrects inaccurate complexity bounds and hardware invariants hypothesized in earlier phases.

## 1. Scope Unwinding Complexity (Correction)
**Claim:** "O(1) tombstone-free memory reclamation" (Phase 5).
**Correction:** False. `exitScope()` triggers `backwardShift(slotIdx)` for every declaration in the exiting scope. `backwardShift` iterates through the probe cluster until it finds an empty slot or a slot with `probeDistance == 0`. 
- **Single deletion:** $O(P)$, where $P$ is the average displacement distance (probe sequence length).
- **Scope exit (k declarations):** Amortized $O(k \times P)$.
- **Worst-case:** $O(k \times \min(N, 250))$ because `kMaxProbeDist = 250`.
*Conclusion:* METIS-X scope unwinding is proportional to probe cluster density, not strictly $O(1)$.

## 2. The `frameIndex` Backlink Advantage
**Claim:** `frameIndex` provides an $O(1)$ bidirectional link.
**Verification:** True. When a Robin Hood swap occurs, `updateSlotLocation` uses `frameIndex` to execute `scopeFrames_[s.scopeId][s.frameIndex] = newIdx` in $O(1)$ time. Without this 4-byte metadata field, every displacement would require an $O(S)$ linear scan of the scope frame to update the relocated index, which would catastrophically degrade insertion throughput during high-load clustered insertions.

## 3. Hardware and Layout Alignment (Correction)
**Claim:** "The slot fits exactly into half a standard 64B cache line" and "is cache-aligned" (Phase 5).
**Correction:** Structurally false. `sizeof(MetisXSlot) == 32`, but it lacks an `alignas(32)` specifier. Its base alignment is dictated by its largest scalar member (`int32_t` / `uint32_t`), which is $4$ bytes. When allocated in a `std::vector`, the array is typically 16-byte aligned (x86_64 default new). This guarantees that every alternating slot *crosses* a 64-byte cache line boundary, requiring two L1 cache fetches.
*Conclusion:* Cache residency claims based on 32-byte density are valid, but strict cache-line boundary alignment is not natively enforced by the current code.
