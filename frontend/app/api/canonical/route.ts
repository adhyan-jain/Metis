// Serves the authoritative canonical research dataset from results/CANONICAL_FINAL_DATASET.csv
// and embedded evaluation metrics from results/embedded_benchmark.csv
import { NextResponse } from "next/server";
import fs from "fs";
import { parseCsv, toNumberRow } from "../../../lib/csv";
import { resultsCsvPath } from "../../../lib/repoPaths";

export const runtime = "nodejs";

const NUMERIC_FIELDS = [
  "symbols", "unique_symbols", "events", "peak_heap_bytes", "final_heap_bytes",
  "memory_bytes", "memory_per_symbol", "compression_ratio",
  "p50_us", "p95_us", "p99_us", "mean_us",
  "lookup_p50_us", "lookup_p95_us", "lookup_p99_us", "lookup_mean_us",
  "inline_symbols", "interned_symbols", "compressed_symbols",
  "reconstructions", "mean_depth", "p95_depth", "max_depth"
];

export async function GET() {
  const canonicalPath = resultsCsvPath("CANONICAL_FINAL_DATASET.csv");
  const embeddedPath = resultsCsvPath("embedded_benchmark.csv");

  let canonicalRows: Record<string, any>[] = [];
  let embeddedRows: Record<string, any>[] = [];

  if (canonicalPath && fs.existsSync(canonicalPath)) {
    const text = fs.readFileSync(canonicalPath, "utf-8");
    canonicalRows = parseCsv(text).map((r) => toNumberRow(r, NUMERIC_FIELDS));
  }

  if (embeddedPath && fs.existsSync(embeddedPath)) {
    const text = fs.readFileSync(embeddedPath, "utf-8");
    embeddedRows = parseCsv(text).map((r) => toNumberRow(r, NUMERIC_FIELDS));
  }

  return NextResponse.json({
    available: canonicalRows.length > 0,
    totalRows: canonicalRows.length,
    canonicalRows,
    embeddedRows,
    headline: {
      embeddedWorkloads: 4,
      hostCorpora: 26,
      totalMeasuredConfigurations: 433,
      zephyrFinalHeapSavingsPct: 20.2,
      zephyrPeakHeapSavingsPct: 24.2,
      zephyrFinalHeapSavedMb: 13.52,
      zephyrP95LatencyRatio: 1.824,
      zephyrP50Us: 0.066,
      zephyrP95Us: 0.228,
      embConvZephyrP95Us: 0.125,
      latencyGatePassed: false,
      breakEvenAt32B: 3.33,
      ssoThresholdBytes: 15
    }
  });
}
