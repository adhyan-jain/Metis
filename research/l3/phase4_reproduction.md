# Phase 4 Reproduction Guide

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`
**Deliverable:** `research/l3/phase4_reproduction.md`

## 1. Environment & State
- **HEAD Revision:** `@0b4344050873968a7f3f940d47f8b46f50a23353` (unchanged from Phase 3).
- **Compiler:** `g++` (std=c++14).
- **Hardware Profile:** Linux, execution constrained to core 0 via `taskset -c 0`.

## 2. Experimental Artifacts
All Phase 4 work was strictly isolated. No canonical results or baseline structures (`include/metis_x.hpp`) were modified.
- **Candidate D Implementation:** `include/metis_x_arena.hpp`
- **Benchmark Harness:** `src/phase4_bench.cpp`
- **Executable:** `bin/phase4_bench`

## 3. Reproduction Steps
To re-run the exact paired control/treatment benchmark that yielded the Phase 4 results:

1. Compile the isolated harness:
   ```bash
   g++ -std=c++14 -O2 -Iinclude src/phase4_bench.cpp -o bin/phase4_bench
   ```
2. Execute the benchmark with CPU pinning to eliminate scheduler jitter:
   ```bash
   taskset -c 0 ./bin/phase4_bench
   ```
3. The harness will automatically load `data/corpus_events_ESP-IDF.txt` and `data/corpus_events_Zephyr.txt`, executing 5 repetitions of both `Control (METIS-X)` and `CandidateD (Arena Offset)`.
4. It computes the median of the primary metrics across the 5 runs and outputs the side-by-side comparison to `stdout`.

## 4. Trace Data Verification
Ensure the trace inputs have not been altered. The benchmark relies on the identical inputs used in Phase 2:
- `data/corpus_events_Zephyr.txt`
- `data/corpus_events_ESP-IDF.txt`

## 5. Clean-up
Candidate D is rejected. Do not merge `include/metis_x_arena.hpp` into the production tree. The artifacts serve solely as supporting evidence for the negative-result systems paper.
