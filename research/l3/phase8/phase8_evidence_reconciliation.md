# Phase 8: Evidence Reconciliation

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`

## The Discrepancy
- **Earlier Phases (Phase 2/3):** Reported Zephyr peak memory reduction of ~49% (48.8 MB to 24.7 MB).
- **Phase 7 Manuscript:** Reported Zephyr peak memory at 4.51 MB.

## Investigation
An inspection of the raw CSVs reveals two separate artifact files for the benchmark:
1. `results/metis_x_benchmark.csv`: The canonical, multi-repetition benchmark containing detailed metrics (e.g., `measured_peak_heap_bytes`, `lookup_mean_us`) for all four codebases (FreeRTOS, Arduino, Zephyr, ESP-IDF) across four implementations.
2. `results/metis_x_ablation.csv`: A secondary ablation file containing exactly 10 rows, detailing metrics for specific ablation variants (`A0_ConventionalHost`, `A5_FullMetisX`).

The figures in `metis_x_benchmark.csv` for Zephyr `METIS-X` are:
- `measured_peak_heap_bytes` = 25,924,248 (24.7 MB)
- Baseline (`EmbeddedConventional`) = 51,203,416 (48.8 MB)

The figures in `metis_x_ablation.csv` for Zephyr `A5_FullMetisX` are:
- `final_heap_bytes` = 4,728,976 (4.51 MB)
- Baseline (`A3_EmbeddedConventional`) = 21,534,528 (20.5 MB)

## Resolution
The difference stems from different metrics and trace configurations:
- `metis_x_benchmark.csv` recorded `measured_peak_heap_bytes` on the full trace.
- `metis_x_ablation.csv` recorded `final_heap_bytes` (not peak heap) and ran on a differently configured or truncated trace iteration, resulting in substantially lower absolute byte counts.

**Authoritative Source:** We officially designate `results/metis_x_benchmark.csv` as the canonical source for headline whole-trace comparisons. The 48.8 MB to 24.7 MB reduction (~49% reduction) is the correct empirical metric for the full Zephyr trace. 

*(Note: The manuscript generated in Phase 7 relied on the ablation CSV. A full paper revision must pull from `metis_x_benchmark.csv` to assert the 49% peak heap reduction rather than the 78% final-heap reduction, correcting the reporting boundary).*
