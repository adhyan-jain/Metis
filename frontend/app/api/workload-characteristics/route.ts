// Reads real-world corpus evaluations and characterization metrics from real_world_benchmark.csv
import { NextResponse } from "next/server";
import fs from "fs";
import { parseCsv, toNumberRow } from "../../../lib/csv";
import { resultsCsvPath } from "../../../lib/repoPaths";

export const runtime = "nodejs";

const NUMERIC_FIELDS = [
  "declarations", "uses", "unique_names", "modeled_peak_bytes", "modeled_final_bytes",
  "measured_peak_heap_bytes", "measured_final_heap_bytes", "measured_bytes_per_unique_symbol",
  "lookup_p50_us", "lookup_p95_us", "lookup_p99_us", "lookup_mean_us",
  "insert_p50_us", "insert_p95_us", "insert_p99_us", "insert_mean_us",
  "promotions", "demotions", "reconstruction_count", "reconstruction_steps_total",
  "mean_reconstruction_depth", "count_inline", "count_interned", "count_compressed",
  "lookup_inline_count", "lookup_interned_count", "lookup_compressed_count",
  "files", "unique_symbols", "max_scope_depth", "avg_scope_depth", "mean_identifier_length",
  "prefix_similarity", "repeat_rate", "entropy", "access_skew", "churn"
];

export async function GET() {
  const benchPath = resultsCsvPath("real_world_benchmark.csv");
  const charPath = resultsCsvPath("corpus_characterization.csv");

  let benchmarkRows: Record<string, any>[] = [];
  let characterizationRows: Record<string, any>[] = [];

  if (benchPath && fs.existsSync(benchPath)) {
    const text = fs.readFileSync(benchPath, "utf-8");
    benchmarkRows = parseCsv(text).map((r) => toNumberRow(r, NUMERIC_FIELDS));
  }

  if (charPath && fs.existsSync(charPath)) {
    const text = fs.readFileSync(charPath, "utf-8");
    characterizationRows = parseCsv(text).map((r) => toNumberRow(r, NUMERIC_FIELDS));
  }

  return NextResponse.json({
    available: benchmarkRows.length > 0 || characterizationRows.length > 0,
    benchmarkRows,
    characterizationRows,
    corporaCount: Array.from(new Set(benchmarkRows.map((r) => r.corpus))).length || 26
  });
}
