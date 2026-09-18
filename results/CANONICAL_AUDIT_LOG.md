# Canonical Data Audit Log & Reconciliation Summary

**Total Registered Canonical Rows**: 433

### Data Source Breakdown:
- `results/real_world_benchmark.csv`: 182 entries (26 corpora x 7 implementations, physical heap allocator-measured with repeated timing)
- `results/embedded_benchmark.csv`: 16 entries (4 embedded workloads x 4 implementations: ConventionalHost, EmbeddedConventional, Interned, SymTabV3)
- `results/synthetic_experiments_A_F.csv`: 210 entries (Experiments A-F across parameter sweeps, physical heap allocator-measured)
- `results/multiseed_v4_summary.csv`: 25 entries (N=30 seeds per workload, validating V4 negative result)

### Reconciled Provenance & Verification:
1. `data/real_world_benchmark.csv` and `results/real_world_benchmark.csv` synchronized and generated via `src/real_world_bench_main.cpp` with multi-repetition passes.
2. `results/embedded_benchmark.csv` evaluates EmbeddedConventional against SymTabV3 with physical heap counters.
3. Modeled vs Allocator-Measured memory strictly distinguished across all data tables.
