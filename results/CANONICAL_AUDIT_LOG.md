# Canonical Data Audit Log & Reconciliation Summary

**Total Registered Canonical Rows**: 350

### Data Source Breakdown:
- `results/real_world_benchmark.csv`: 140 entries (20 corpora x 7 implementations, physical heap allocator-measured)
- `results/synthetic_experiments_A_F.csv`: 211 entries (Experiments A-F across parameter sweeps, physical heap allocator-measured)
- `results/multiseed_v4_summary.csv`: 25 aggregated multiseed entries (N=30 seeds per workload)

### Reconciled Divergences:
1. `data/real_world_benchmark.csv` and `results/real_world_benchmark.csv` synchronized (141 lines each).
2. All historical pre-V4 redesign CSVs (`real_world_redesign_benchmark.csv`) retired in favor of canonical 20-corpus dataset.
3. Modeled vs Allocator-Measured memory strictly distinguished across all data tables.
