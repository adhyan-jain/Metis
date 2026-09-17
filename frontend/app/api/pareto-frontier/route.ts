// Reads results/pareto_frontier.csv fresh from disk -- small (48-row) file,
// no aggregation needed. Same honesty rule as /api/benchmarks.
import { NextResponse } from "next/server";
import fs from "fs";
import { parseCsv, toNumberRow } from "../../../lib/csv";
import { resultsCsvPath } from "../../../lib/repoPaths";

export const runtime = "nodejs";

const NUMERIC_FIELDS = [
  "latency_constraint", "satisfied", "optimal_config_id",
  "inline_max_len", "compress_min_len", "block_size", "anchor_interval", "hot_access_threshold",
  "conv_cold_lookup_p50_us", "conv_measured_final_heap_bytes",
  "actual_cold_lookup_p50_us", "actual_measured_final_heap_bytes",
  "latency_ratio_vs_conv", "memory_ratio_vs_conv", "memory_savings_pct_vs_conv",
];

export async function GET() {
  const csvPath = resultsCsvPath("pareto_frontier.csv");
  if (!csvPath) {
    return NextResponse.json({ available: false, rows: [], message: "results/pareto_frontier.csv not found. Run the Pareto sweep from the repo root to generate it." });
  }
  const text = fs.readFileSync(csvPath, "utf-8");
  const rows = parseCsv(text).map((r) => toNumberRow(r, NUMERIC_FIELDS));
  return NextResponse.json({ available: true, rows, sourcePath: csvPath });
}
