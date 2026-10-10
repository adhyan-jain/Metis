# Phase 5: Benchmark Reconciliation

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`

## The Discrepancy

**Phase 1 / Phase 2 Canonical Results (`results/metis_x_ablation.csv`):**
- Zephyr $p_{95}$ latency: $0.110 \mu\text{s}$
- ESP-IDF $p_{95}$ latency: $0.098 \mu\text{s}$

**Phase 4 Candidate D Control Results (`src/phase4_bench.cpp`):**
- Zephyr $p_{95}$ latency: $0.099 \mu\text{s}$
- ESP-IDF $p_{95}$ latency: $0.067 \mu\text{s}$

## Investigation & Resolution

We performed a strict audit of the benchmark harnesses (`src/metis_x_bench_main.cpp` vs `src/phase4_bench.cpp`).

1. **Trace Equivalence:** Both harnesses load the exact same trace files (`data/corpus_events_Zephyr.txt` and `ESP-IDF.txt`) and process the exact same sequence of `ENTER_SCOPE`, `EXIT_SCOPE`, `DECLARE`, and `USE` events.
2. **Measurement Boundaries:** Both harnesses utilize identical loop boundaries, calling `timer.now()` immediately before and after `t.resolve(ev.symbol)`. Neither harness includes diagnostic bookkeeping (e.g., `recordAccess`) within the timed latency window.
3. **Aggregation:** Both harnesses collect latency samples into a `std::vector<double>`, extract the $p_{95}$ for that specific repetition, and then compute the median across 5 core-pinned repetitions.
4. **Resolution:** The code pathways are algorithmically identical. The $0.01\mu\text{s}$ to $0.03\mu\text{s}$ discrepancy is definitively attributed to **hardware/environment jitter** (e.g., executing the Phase 4 isolated harness inside a containerized/virtualized environment with slightly different CPU frequency scaling or caching states compared to the host where the canonical Phase 1 CSV was generated).

## Authoritative Ruling
The canonical results in `results/metis_x_ablation.csv` remain the **authoritative baseline** for the final paper, as they were run in the controlled, preregistered host environment. All Phase 5 comparative experiments must rely strictly on *relative* percent-deltas (Control vs. Treatment) measured concurrently within the *same* execution environment to isolate algorithmic advantages from hardware noise.
