// Benchmark, ablation, real-world corpus, and ML policy selection datasets
// from authoritative C++ runs (results/*.csv).
// Stored as typed static constants for instant client-side rendering and SSR.

export interface BenchmarkRow {
  dataset: string;
  implementation: "Conventional" | "Interned" | "BudgetSymV1" | "SymTabV2" | string;
  symbols: number;
  memory_bytes: number;
  memory_per_symbol: number;
  compression_ratio: number;
  insert_us: number;
  lookup_success_us: number;
  lookup_failure_us: number;
  scope_enter_us: number;
  scope_exit_us: number;
}

export interface AblationRow {
  variant: string;
  workload: string;
  symbols: number;
  memory_bytes: number;
  memory_per_symbol: number;
  promotions: number;
  insert_us: number;
  lookup_us: number;
  bytes_reclaimed_total: number;
  memory_delta_vs_full_v2_pct: number;
  cold_latency_delta_vs_full_v2_pct: number;
}

export interface CorpusRow {
  corpus: string;
  implementation: "Conventional" | "Interned" | "BudgetSymV1" | "SymTabV2";
  declarations: number;
  uses: number;
  unique_names: number;
  modeled_peak_bytes: number;
  measured_peak_heap_bytes: number;
  measured_final_heap_bytes: number;
  measured_bytes_per_unique_symbol: number;
  lookup_p50_us: number;
  lookup_p95_us: number;
  lookup_p99_us: number;
  lookup_mean_us: number;
  insert_p50_us: number;
  insert_mean_us: number;
  promotions: number;
  reconstruction_count: number;
}

export interface MlComparisonRow {
  latency_constraint: string;
  model_name: string;
  dataset_type: string;
  mean_memory_kb: number;
  mean_cold_latency_us: number;
  constraint_violation_rate: number;
  mean_regret_kb: number;
  exact_config_accuracy: number;
  model_size_bytes: number;
  inference_latency_us: number;
}

// Authoritative multi-seed statistical means (N=30 seeds) from results/statistical_summary.csv
export const benchmarkRows: BenchmarkRow[] = [
  // small (N=100) -- Note fixed directory overhead caveat (~8KB). Conventional is 2.1KB, SymTabV2 is 8.3KB.
  { dataset: "small", implementation: "Conventional", symbols: 100, memory_bytes: 2128, memory_per_symbol: 21.28, compression_ratio: 1.0, insert_us: 0.183, lookup_success_us: 0.0766, lookup_failure_us: 0.0766, scope_enter_us: 0.062, scope_exit_us: 5.09 },
  { dataset: "small", implementation: "Interned", symbols: 100, memory_bytes: 13460, memory_per_symbol: 134.60, compression_ratio: 0.158, insert_us: 0.205, lookup_success_us: 0.0874, lookup_failure_us: 0.0874, scope_enter_us: 0.040, scope_exit_us: 2.40 },
  { dataset: "small", implementation: "BudgetSymV1", symbols: 100, memory_bytes: 22764, memory_per_symbol: 227.64, compression_ratio: 0.093, insert_us: 0.541, lookup_success_us: 0.2789, lookup_failure_us: 0.2789, scope_enter_us: 0.037, scope_exit_us: 5.52 },
  { dataset: "small", implementation: "SymTabV2", symbols: 100, memory_bytes: 8263, memory_per_symbol: 82.63, compression_ratio: 0.258, insert_us: 0.380, lookup_success_us: 0.0759, lookup_failure_us: 0.0759, scope_enter_us: 0.037, scope_exit_us: 5.52 },

  // medium (N=2000)
  { dataset: "medium", implementation: "Conventional", symbols: 2000, memory_bytes: 32848, memory_per_symbol: 16.42, compression_ratio: 1.0, insert_us: 0.085, lookup_success_us: 0.0688, lookup_failure_us: 0.0688, scope_enter_us: 0.066, scope_exit_us: 4.72 },
  { dataset: "medium", implementation: "Interned", symbols: 2000, memory_bytes: 245385, memory_per_symbol: 122.69, compression_ratio: 0.134, insert_us: 0.170, lookup_success_us: 0.0805, lookup_failure_us: 0.0805, scope_enter_us: 0.037, scope_exit_us: 2.05 },
  { dataset: "medium", implementation: "BudgetSymV1", symbols: 2000, memory_bytes: 369501, memory_per_symbol: 184.75, compression_ratio: 0.089, insert_us: 0.268, lookup_success_us: 0.2082, lookup_failure_us: 0.2082, scope_enter_us: 0.051, scope_exit_us: 5.04 },
  { dataset: "medium", implementation: "SymTabV2", symbols: 2000, memory_bytes: 133550, memory_per_symbol: 66.78, compression_ratio: 0.246, insert_us: 0.250, lookup_success_us: 0.0794, lookup_failure_us: 0.0794, scope_enter_us: 0.051, scope_exit_us: 5.04 },

  // large (N=20000 symbols / N=5000 decls) -- Headline 69.89% savings vs V1, 52.89% vs Interned
  { dataset: "large", implementation: "Conventional", symbols: 20000, memory_bytes: 131152, memory_per_symbol: 6.56, compression_ratio: 1.0, insert_us: 0.093, lookup_success_us: 0.0633, lookup_failure_us: 0.0633, scope_enter_us: 0.046, scope_exit_us: 5.24 },
  { dataset: "large", implementation: "Interned", symbols: 20000, memory_bytes: 765595, memory_per_symbol: 38.28, compression_ratio: 0.171, insert_us: 0.256, lookup_success_us: 0.0734, lookup_failure_us: 0.0734, scope_enter_us: 0.065, scope_exit_us: 2.87 },
  { dataset: "large", implementation: "BudgetSymV1", symbols: 20000, memory_bytes: 1197731, memory_per_symbol: 59.89, compression_ratio: 0.109, insert_us: 0.409, lookup_success_us: 0.1753, lookup_failure_us: 0.1753, scope_enter_us: 0.117, scope_exit_us: 13.18 },
  { dataset: "large", implementation: "SymTabV2", symbols: 20000, memory_bytes: 360651, memory_per_symbol: 18.03, compression_ratio: 0.364, insert_us: 0.350, lookup_success_us: 0.0725, lookup_failure_us: 0.0725, scope_enter_us: 0.117, scope_exit_us: 13.18 },

  // high-prefix-similarity (N=2000)
  { dataset: "high-prefix-similarity", implementation: "Conventional", symbols: 2000, memory_bytes: 32848, memory_per_symbol: 16.42, compression_ratio: 1.0, insert_us: 0.182, lookup_success_us: 0.0590, lookup_failure_us: 0.0590, scope_enter_us: 0.024, scope_exit_us: 10.59 },
  { dataset: "high-prefix-similarity", implementation: "Interned", symbols: 2000, memory_bytes: 398732, memory_per_symbol: 199.37, compression_ratio: 0.082, insert_us: 0.279, lookup_success_us: 0.0709, lookup_failure_us: 0.0709, scope_enter_us: 0.013, scope_exit_us: 3.89 },
  { dataset: "high-prefix-similarity", implementation: "BudgetSymV1", symbols: 2000, memory_bytes: 1018371, memory_per_symbol: 509.19, compression_ratio: 0.032, insert_us: 0.370, lookup_success_us: 0.3659, lookup_failure_us: 0.3659, scope_enter_us: 0.011, scope_exit_us: 4.81 },
  { dataset: "high-prefix-similarity", implementation: "SymTabV2", symbols: 2000, memory_bytes: 248073, memory_per_symbol: 124.04, compression_ratio: 0.132, insert_us: 0.360, lookup_success_us: 0.4379, lookup_failure_us: 0.4379, scope_enter_us: 0.011, scope_exit_us: 4.81 },

  // random-long (N=2000)
  { dataset: "random-long", implementation: "Conventional", symbols: 2000, memory_bytes: 32848, memory_per_symbol: 16.42, compression_ratio: 1.0, insert_us: 0.227, lookup_success_us: 0.0677, lookup_failure_us: 0.0677, scope_enter_us: 0.015, scope_exit_us: 6.75 },
  { dataset: "random-long", implementation: "Interned", symbols: 2000, memory_bytes: 389210, memory_per_symbol: 194.60, compression_ratio: 0.084, insert_us: 0.272, lookup_success_us: 0.0788, lookup_failure_us: 0.0788, scope_enter_us: 0.015, scope_exit_us: 4.01 },
  { dataset: "random-long", implementation: "BudgetSymV1", symbols: 2000, memory_bytes: 767468, memory_per_symbol: 383.73, compression_ratio: 0.043, insert_us: 0.785, lookup_success_us: 0.2765, lookup_failure_us: 0.2765, scope_enter_us: 0.014, scope_exit_us: 6.02 },
  { dataset: "random-long", implementation: "SymTabV2", symbols: 2000, memory_bytes: 243662, memory_per_symbol: 121.83, compression_ratio: 0.135, insert_us: 0.659, lookup_success_us: 0.4146, lookup_failure_us: 0.4146, scope_enter_us: 0.014, scope_exit_us: 6.02 },

  // nested-scopes (N=2000)
  { dataset: "nested-scopes", implementation: "Conventional", symbols: 2000, memory_bytes: 32848, memory_per_symbol: 16.42, compression_ratio: 1.0, insert_us: 0.107, lookup_success_us: 0.0696, lookup_failure_us: 0.0696, scope_enter_us: 0.012, scope_exit_us: 4.32 },
  { dataset: "nested-scopes", implementation: "Interned", symbols: 2000, memory_bytes: 275291, memory_per_symbol: 137.65, compression_ratio: 0.119, insert_us: 0.122, lookup_success_us: 0.0820, lookup_failure_us: 0.0820, scope_enter_us: 0.009, scope_exit_us: 1.93 },
  { dataset: "nested-scopes", implementation: "BudgetSymV1", symbols: 2000, memory_bytes: 787698, memory_per_symbol: 393.85, compression_ratio: 0.042, insert_us: 0.305, lookup_success_us: 0.3683, lookup_failure_us: 0.3683, scope_enter_us: 0.013, scope_exit_us: 5.05 },
  { dataset: "nested-scopes", implementation: "SymTabV2", symbols: 2000, memory_bytes: 260323, memory_per_symbol: 130.16, compression_ratio: 0.126, insert_us: 0.300, lookup_success_us: 0.2866, lookup_failure_us: 0.2866, scope_enter_us: 0.013, scope_exit_us: 5.05 },

  // hot-cold-access (N=2000)
  { dataset: "hot-cold-access", implementation: "Conventional", symbols: 2000, memory_bytes: 49232, memory_per_symbol: 24.62, compression_ratio: 1.0, insert_us: 0.081, lookup_success_us: 0.0633, lookup_failure_us: 0.0633, scope_enter_us: 0.011, scope_exit_us: 5.67 },
  { dataset: "hot-cold-access", implementation: "Interned", symbols: 2000, memory_bytes: 290553, memory_per_symbol: 145.28, compression_ratio: 0.169, insert_us: 0.142, lookup_success_us: 0.0766, lookup_failure_us: 0.0766, scope_enter_us: 0.010, scope_exit_us: 1.93 },
  { dataset: "hot-cold-access", implementation: "BudgetSymV1", symbols: 2000, memory_bytes: 491572, memory_per_symbol: 245.79, compression_ratio: 0.100, insert_us: 0.256, lookup_success_us: 0.2026, lookup_failure_us: 0.2026, scope_enter_us: 0.011, scope_exit_us: 5.02 },
  { dataset: "hot-cold-access", implementation: "SymTabV2", symbols: 2000, memory_bytes: 222899, memory_per_symbol: 111.45, compression_ratio: 0.221, insert_us: 0.250, lookup_success_us: 0.1145, lookup_failure_us: 0.1145, scope_enter_us: 0.011, scope_exit_us: 5.02 },

  // memory-stress (N=2000)
  { dataset: "memory-stress", implementation: "Conventional", symbols: 2000, memory_bytes: 32848, memory_per_symbol: 16.42, compression_ratio: 1.0, insert_us: 0.088, lookup_success_us: 0.0633, lookup_failure_us: 0.0633, scope_enter_us: 0.010, scope_exit_us: 4.41 },
  { dataset: "memory-stress", implementation: "Interned", symbols: 2000, memory_bytes: 356257, memory_per_symbol: 178.13, compression_ratio: 0.092, insert_us: 0.150, lookup_success_us: 0.0735, lookup_failure_us: 0.0735, scope_enter_us: 0.010, scope_exit_us: 1.99 },
  { dataset: "memory-stress", implementation: "BudgetSymV1", symbols: 2000, memory_bytes: 702389, memory_per_symbol: 351.19, compression_ratio: 0.047, insert_us: 0.448, lookup_success_us: 0.2448, lookup_failure_us: 0.2448, scope_enter_us: 0.011, scope_exit_us: 6.95 },
  { dataset: "memory-stress", implementation: "SymTabV2", symbols: 2000, memory_bytes: 227535, memory_per_symbol: 113.77, compression_ratio: 0.144, insert_us: 0.380, lookup_success_us: 0.3512, lookup_failure_us: 0.3512, scope_enter_us: 0.011, scope_exit_us: 6.95 },
];

// Authoritative empirical ablation results from results/ablation.csv (medium workload, N=2000)
export const ablationRows: AblationRow[] = [
  { variant: "FullV2", workload: "medium", symbols: 2000, memory_bytes: 305832, memory_per_symbol: 152.92, promotions: 0, insert_us: 0.575, lookup_us: 0.132, bytes_reclaimed_total: 88844, memory_delta_vs_full_v2_pct: 0, cold_latency_delta_vs_full_v2_pct: 0 },
  { variant: "V2-NoScopeReclamation", workload: "medium", symbols: 2000, memory_bytes: 474360, memory_per_symbol: 237.18, promotions: 0, insert_us: 0.392, lookup_us: 0.100, bytes_reclaimed_total: 0, memory_delta_vs_full_v2_pct: 55.10, cold_latency_delta_vs_full_v2_pct: -24.24 },
  { variant: "V2-NoAdaptiveRepresentation", workload: "medium", symbols: 2000, memory_bytes: 487136, memory_per_symbol: 243.57, promotions: 0, insert_us: 0.472, lookup_us: 0.098, bytes_reclaimed_total: 88844, memory_delta_vs_full_v2_pct: 59.28, cold_latency_delta_vs_full_v2_pct: -25.76 },
  { variant: "V2-NoBlockCompression", workload: "medium", symbols: 2000, memory_bytes: 440760, memory_per_symbol: 220.38, promotions: 0, insert_us: 0.474, lookup_us: 0.101, bytes_reclaimed_total: 88844, memory_delta_vs_full_v2_pct: 44.12, cold_latency_delta_vs_full_v2_pct: -23.48 },
  { variant: "V2-NoFingerprints", workload: "medium", symbols: 2000, memory_bytes: 305808, memory_per_symbol: 152.90, promotions: 0, insert_us: 0.432, lookup_us: 0.134, bytes_reclaimed_total: 88844, memory_delta_vs_full_v2_pct: -0.01, cold_latency_delta_vs_full_v2_pct: 1.52 },
  { variant: "V2-NoHotColdPromotion", workload: "medium", symbols: 2000, memory_bytes: 305632, memory_per_symbol: 152.82, promotions: 0, insert_us: 0.398, lookup_us: 0.108, bytes_reclaimed_total: 88844, memory_delta_vs_full_v2_pct: -0.07, cold_latency_delta_vs_full_v2_pct: -18.18 },
];

// Authoritative empirical real-world corpus benchmark results from results/corpus_benchmark.csv
export const corpusRows: CorpusRow[] = [
  // FreeRTOS (72,376 decls, 10,386 unique names)
  { corpus: "FreeRTOS", implementation: "Conventional", declarations: 72376, uses: 123602, unique_names: 10386, modeled_peak_bytes: 526504, measured_peak_heap_bytes: 3992248, measured_final_heap_bytes: 3744448, measured_bytes_per_unique_symbol: 360, lookup_p50_us: 0.117, lookup_p95_us: 0.307, lookup_p99_us: 0.464, lookup_mean_us: 0.1451, insert_p50_us: 0.159, insert_mean_us: 0.2053, promotions: 0, reconstruction_count: 0 },
  { corpus: "FreeRTOS", implementation: "Interned", declarations: 72376, uses: 123602, unique_names: 10386, modeled_peak_bytes: 496566, measured_peak_heap_bytes: 5169912, measured_final_heap_bytes: 5085088, measured_bytes_per_unique_symbol: 489, lookup_p50_us: 0.110, lookup_p95_us: 0.219, lookup_p99_us: 0.309, lookup_mean_us: 0.1260, insert_p50_us: 0.157, insert_mean_us: 0.2264, promotions: 0, reconstruction_count: 0 },
  { corpus: "FreeRTOS", implementation: "BudgetSymV1", declarations: 72376, uses: 123602, unique_names: 10386, modeled_peak_bytes: 570273, measured_peak_heap_bytes: 20411280, measured_final_heap_bytes: 16181376, measured_bytes_per_unique_symbol: 1557, lookup_p50_us: 0.107, lookup_p95_us: 0.575, lookup_p99_us: 0.992, lookup_mean_us: 0.1918, insert_p50_us: 0.531, insert_mean_us: 0.7480, promotions: 762, reconstruction_count: 0 },
  { corpus: "FreeRTOS", implementation: "SymTabV2", declarations: 72376, uses: 123602, unique_names: 10386, modeled_peak_bytes: 1447253, measured_peak_heap_bytes: 5684880, measured_final_heap_bytes: 5614072, measured_bytes_per_unique_symbol: 540, lookup_p50_us: 0.102, lookup_p95_us: 0.235, lookup_p99_us: 0.651, lookup_mean_us: 0.1263, insert_p50_us: 0.319, insert_mean_us: 0.4277, promotions: 873, reconstruction_count: 6424 },

  // Arduino (31,846 decls, 11,000 unique names)
  { corpus: "Arduino", implementation: "Conventional", declarations: 31846, uses: 50273, unique_names: 11000, modeled_peak_bytes: 543104, measured_peak_heap_bytes: 2423120, measured_final_heap_bytes: 2422768, measured_bytes_per_unique_symbol: 220, lookup_p50_us: 0.124, lookup_p95_us: 0.293, lookup_p99_us: 0.465, lookup_mean_us: 0.1515, insert_p50_us: 0.197, insert_mean_us: 0.2496, promotions: 0, reconstruction_count: 0 },
  { corpus: "Arduino", implementation: "Interned", declarations: 31846, uses: 50273, unique_names: 11000, modeled_peak_bytes: 515360, measured_peak_heap_bytes: 3696768, measured_final_heap_bytes: 3696624, measured_bytes_per_unique_symbol: 336, lookup_p50_us: 0.112, lookup_p95_us: 0.220, lookup_p99_us: 0.327, lookup_mean_us: 0.1278, insert_p50_us: 0.220, insert_mean_us: 0.2915, promotions: 0, reconstruction_count: 0 },
  { corpus: "Arduino", implementation: "BudgetSymV1", declarations: 31846, uses: 50273, unique_names: 11000, modeled_peak_bytes: 466361, measured_peak_heap_bytes: 6778328, measured_final_heap_bytes: 6778328, measured_bytes_per_unique_symbol: 616, lookup_p50_us: 0.090, lookup_p95_us: 0.470, lookup_p99_us: 0.731, lookup_mean_us: 0.1495, insert_p50_us: 0.501, insert_mean_us: 0.6378, promotions: 535, reconstruction_count: 0 },
  { corpus: "Arduino", implementation: "SymTabV2", declarations: 31846, uses: 50273, unique_names: 11000, modeled_peak_bytes: 1211387, measured_peak_heap_bytes: 3626072, measured_final_heap_bytes: 3625928, measured_bytes_per_unique_symbol: 329, lookup_p50_us: 0.089, lookup_p95_us: 0.219, lookup_p99_us: 0.742, lookup_mean_us: 0.1164, insert_p50_us: 0.171, insert_mean_us: 0.3674, promotions: 547, reconstruction_count: 3593 },

  // Zephyr (703,727 decls, 228,739 unique names)
  { corpus: "Zephyr", implementation: "Conventional", declarations: 703727, uses: 1523992, unique_names: 228739, modeled_peak_bytes: 3896220, measured_peak_heap_bytes: 50918616, measured_final_heap_bytes: 47755968, measured_bytes_per_unique_symbol: 208, lookup_p50_us: 0.254, lookup_p95_us: 1.148, lookup_p99_us: 3.040, lookup_mean_us: 0.4320, insert_p50_us: 0.372, insert_mean_us: 0.6195, promotions: 0, reconstruction_count: 0 },
  { corpus: "Zephyr", implementation: "Interned", declarations: 703727, uses: 1523992, unique_names: 228739, modeled_peak_bytes: 3672472, measured_peak_heap_bytes: 81517784, measured_final_heap_bytes: 81218544, measured_bytes_per_unique_symbol: 355, lookup_p50_us: 0.134, lookup_p95_us: 0.424, lookup_p99_us: 1.035, lookup_mean_us: 0.1839, insert_p50_us: 0.305, insert_mean_us: 0.4711, promotions: 0, reconstruction_count: 0 },
  { corpus: "Zephyr", implementation: "BudgetSymV1", declarations: 703727, uses: 1523992, unique_names: 228739, modeled_peak_bytes: 4185099, measured_peak_heap_bytes: 212195968, measured_final_heap_bytes: 190964432, measured_bytes_per_unique_symbol: 834, lookup_p50_us: 0.093, lookup_p95_us: 0.574, lookup_p99_us: 1.452, lookup_mean_us: 0.1833, insert_p50_us: 0.597, insert_mean_us: 0.8884, promotions: 32135, reconstruction_count: 0 },
  { corpus: "Zephyr", implementation: "SymTabV2", declarations: 703727, uses: 1523992, unique_names: 228739, modeled_peak_bytes: 18953853, measured_peak_heap_bytes: 84671600, measured_final_heap_bytes: 83712864, measured_bytes_per_unique_symbol: 365, lookup_p50_us: 0.098, lookup_p95_us: 0.535, lookup_p99_us: 1.023, lookup_mean_us: 0.1591, insert_p50_us: 0.250, insert_mean_us: 0.5803, promotions: 24954, reconstruction_count: 182562 },
];

// Authoritative Pareto ML & Heuristic Policy Selection evaluation from results/ml_comparison.csv
export const mlComparisonRows: MlComparisonRow[] = [
  { latency_constraint: "1.10x", model_name: "Hand Heuristic", dataset_type: "Synthetic (LOWO)", mean_memory_kb: 422.48, mean_cold_latency_us: 0.2396, constraint_violation_rate: 0.667, mean_regret_kb: 141.39, exact_config_accuracy: 0.111, model_size_bytes: 64, inference_latency_us: 249.32 },
  { latency_constraint: "1.10x", model_name: "Hand Heuristic", dataset_type: "Real-World (Held-Out)", mean_memory_kb: 1190.18, mean_cold_latency_us: 0.0913, constraint_violation_rate: 0.0, mean_regret_kb: 153.70, exact_config_accuracy: 0.0, model_size_bytes: 64, inference_latency_us: 249.32 },
  { latency_constraint: "1.25x", model_name: "Hand Heuristic", dataset_type: "Synthetic (LOWO)", mean_memory_kb: 483.13, mean_cold_latency_us: 0.2923, constraint_violation_rate: 0.778, mean_regret_kb: 95.51, exact_config_accuracy: 0.111, model_size_bytes: 64, inference_latency_us: 289.07 },
  { latency_constraint: "1.25x", model_name: "Hand Heuristic", dataset_type: "Real-World (Held-Out)", mean_memory_kb: 1190.18, mean_cold_latency_us: 0.0913, constraint_violation_rate: 0.0, mean_regret_kb: 153.70, exact_config_accuracy: 0.0, model_size_bytes: 64, inference_latency_us: 289.07 },
  { latency_constraint: "1.50x", model_name: "Hand Heuristic", dataset_type: "Synthetic (LOWO)", mean_memory_kb: 483.13, mean_cold_latency_us: 0.2923, constraint_violation_rate: 0.667, mean_regret_kb: 75.77, exact_config_accuracy: 0.0, model_size_bytes: 64, inference_latency_us: 426.79 },
  { latency_constraint: "1.50x", model_name: "Hand Heuristic", dataset_type: "Real-World (Held-Out)", mean_memory_kb: 1190.18, mean_cold_latency_us: 0.0913, constraint_violation_rate: 0.0, mean_regret_kb: 153.70, exact_config_accuracy: 0.0, model_size_bytes: 64, inference_latency_us: 426.79 },
  { latency_constraint: "2.00x", model_name: "Hand Heuristic", dataset_type: "Synthetic (LOWO)", mean_memory_kb: 483.13, mean_cold_latency_us: 0.2923, constraint_violation_rate: 0.667, mean_regret_kb: 151.97, exact_config_accuracy: 0.0, model_size_bytes: 64, inference_latency_us: 294.17 },
  { latency_constraint: "2.00x", model_name: "Hand Heuristic", dataset_type: "Real-World (Held-Out)", mean_memory_kb: 1190.18, mean_cold_latency_us: 0.0913, constraint_violation_rate: 0.0, mean_regret_kb: 153.70, exact_config_accuracy: 0.0, model_size_bytes: 64, inference_latency_us: 294.17 },
];

export interface ParetoRow {
  workload: string;
  category: string;
  implementation: "Conventional" | "Interned" | "BudgetSymV1" | "SymTabV2" | string;
  config_id: number;
  declarations: number;
  uses: number;
  unique_names: number;
  measured_peak_heap_bytes: number;
  measured_final_heap_bytes: number;
  cold_lookup_p50_us: number;
  cold_lookup_p95_us: number;
  cold_lookup_p99_us: number;
  count_inline: number;
  count_interned: number;
  count_compressed: number;
  memory_ratio_vs_conv: number;
  cold_latency_ratio_vs_conv: number;
}

export interface ParetoFrontierRow {
  workload: string;
  category: string;
  latency_constraint: number;
  satisfied: number;
  optimal_impl: string;
  optimal_config_id: number;
  conv_cold_lookup_p50_us: number;
  conv_measured_final_heap_bytes: number;
  actual_cold_lookup_p50_us: number;
  actual_measured_final_heap_bytes: number;
  latency_ratio_vs_conv: number;
  memory_ratio_vs_conv: number;
  memory_savings_pct_vs_conv: number;
}

export interface CorpusCharacterizationRow {
  corpus: string;
  files: number;
  declarations: number;
  uses: number;
  unique_symbols: number;
  redeclarations: number;
  shadowing: number;
  max_scope_depth: number;
  avg_scope_depth: number;
  mean_identifier_length: number;
  prefix_similarity: number;
  repeat_rate: number;
  entropy: number;
  access_skew: number;
  churn: number;
}

// Small representative fallback slice from results/pareto_results.csv --
// real-world corpora (category "A"), each implementation's config_id=0/1
// baseline row (Conventional/Interned/BudgetSymV1 have no parameter sweep;
// SymTabV2's config_id=1 is its default-parameter run). Not the full
// 1,700-config sweep -- see /api/pareto for the live Pareto-filtered set.
export const paretoRows: ParetoRow[] = [
  { workload: "FreeRTOS", category: "A", implementation: "Conventional", config_id: 0, declarations: 8506, uses: 12033, unique_names: 2641, measured_peak_heap_bytes: 893488, measured_final_heap_bytes: 827944, cold_lookup_p50_us: 0.14, cold_lookup_p95_us: 0.315, cold_lookup_p99_us: 0.422, count_inline: 0, count_interned: 0, count_compressed: 0, memory_ratio_vs_conv: 1, cold_latency_ratio_vs_conv: 1 },
  { workload: "FreeRTOS", category: "A", implementation: "Interned", config_id: 0, declarations: 8506, uses: 12033, unique_names: 2641, measured_peak_heap_bytes: 1218672, measured_final_heap_bytes: 1153128, cold_lookup_p50_us: 0.127, cold_lookup_p95_us: 0.235, cold_lookup_p99_us: 0.308, count_inline: 0, count_interned: 0, count_compressed: 0, memory_ratio_vs_conv: 1.39276, cold_latency_ratio_vs_conv: 0.907143 },
  { workload: "FreeRTOS", category: "A", implementation: "BudgetSymV1", config_id: 0, declarations: 8506, uses: 12033, unique_names: 2641, measured_peak_heap_bytes: 3321616, measured_final_heap_bytes: 2732280, cold_lookup_p50_us: 0.118, cold_lookup_p95_us: 0.624, cold_lookup_p99_us: 0.996, count_inline: 0, count_interned: 0, count_compressed: 0, memory_ratio_vs_conv: 3.30008, cold_latency_ratio_vs_conv: 0.842857 },
  { workload: "FreeRTOS", category: "A", implementation: "SymTabV2", config_id: 1, declarations: 8506, uses: 12033, unique_names: 2641, measured_peak_heap_bytes: 1562464, measured_final_heap_bytes: 1505304, cold_lookup_p50_us: 0.126, cold_lookup_p95_us: 0.639, cold_lookup_p99_us: 1.175, count_inline: 546, count_interned: 1696, count_compressed: 399, memory_ratio_vs_conv: 1.81812, cold_latency_ratio_vs_conv: 0.9 },
];

// Small representative fallback slice from results/pareto_frontier.csv
// (1.25x latency constraint row per synthetic workload).
export const paretoFrontierRows: ParetoFrontierRow[] = [
  { workload: "small", category: "B", latency_constraint: 1.25, satisfied: 1, optimal_impl: "Conventional", optimal_config_id: 0, conv_cold_lookup_p50_us: 0.06, conv_measured_final_heap_bytes: 8976, actual_cold_lookup_p50_us: 0.06, actual_measured_final_heap_bytes: 8976, latency_ratio_vs_conv: 1, memory_ratio_vs_conv: 1, memory_savings_pct_vs_conv: 0 },
  { workload: "large", category: "B", latency_constraint: 1.25, satisfied: 1, optimal_impl: "Conventional", optimal_config_id: 0, conv_cold_lookup_p50_us: 0.081, conv_measured_final_heap_bytes: 487984, actual_cold_lookup_p50_us: 0.081, actual_measured_final_heap_bytes: 487984, latency_ratio_vs_conv: 1, memory_ratio_vs_conv: 1, memory_savings_pct_vs_conv: 0 },
];

// Authoritative real-world workload characterization from
// results/corpus_characterization.csv.
export const corpusCharacterizationRows: CorpusCharacterizationRow[] = [
  { corpus: "FreeRTOS", files: 655, declarations: 72376, uses: 123602, unique_symbols: 10386, redeclarations: 39209, shadowing: 4084, max_scope_depth: 10, avg_scope_depth: 1.58, mean_identifier_length: 14.202, prefix_similarity: 0.1643, repeat_rate: 0.6307, entropy: 10.6726, access_skew: 0.7931, churn: 0.0688 },
  { corpus: "Arduino", files: 332, declarations: 31846, uses: 50273, unique_symbols: 11000, redeclarations: 6985, shadowing: 1166, max_scope_depth: 11, avg_scope_depth: 1.446, mean_identifier_length: 8.691, prefix_similarity: 0.1362, repeat_rate: 0.6122, entropy: 11.3061, access_skew: 0.7182, churn: 0.0641 },
  { corpus: "Zephyr", files: 4298, declarations: 703727, uses: 1523992, unique_symbols: 228739, redeclarations: 50928, shadowing: 42035, max_scope_depth: 21, avg_scope_depth: 10.503, mean_identifier_length: 9.932, prefix_similarity: 0.0994, repeat_rate: 0.6841, entropy: 12.6882, access_skew: 0.807, churn: 0.0726 },
];

export const DATASETS = Array.from(new Set(benchmarkRows.map((r) => r.dataset)));
export const IMPLEMENTATIONS = ["Conventional", "Interned", "BudgetSymV1", "SymTabV2"] as const;

export function rowFor(dataset: string, implementation: string): BenchmarkRow | undefined {
  return benchmarkRows.find((r) => r.dataset === dataset && r.implementation === implementation);
}
