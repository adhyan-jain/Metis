// Reads results/pareto_results.csv fresh from disk -- same honesty rule as
// /api/benchmarks: real file or an explicit empty state, never fabricated.
//
// The raw file has ~1,700 swept configs per (workload, SymTabV2) combo, far
// too much to ship to the client. Baseline implementations (Conventional/
// Interned/BudgetSymV1) have no parameter sweep and are returned as-is;
// SymTabV2 rows are reduced to their 2D (latency, memory) Pareto-efficient
// subset per workload so the client renders the frontier, not noise.
import { NextResponse } from "next/server";
import fs from "fs";
import { parseCsv, toNumberRow } from "../../../lib/csv";
import { resultsCsvPath } from "../../../lib/repoPaths";

export const runtime = "nodejs";

const NUMERIC_FIELDS = [
  "config_id", "declarations", "uses", "unique_names",
  "measured_peak_heap_bytes", "measured_final_heap_bytes",
  "cold_lookup_p50_us", "cold_lookup_p95_us", "cold_lookup_p99_us",
  "count_inline", "count_interned", "count_compressed",
  "memory_ratio_vs_conv", "cold_latency_ratio_vs_conv",
];

type Row = Record<string, string | number>;

function isDominated(candidate: Row, others: Row[]): boolean {
  return others.some((o) => {
    if (o === candidate) return false;
    const betterOrEqualLatency = (o.cold_lookup_p50_us as number) <= (candidate.cold_lookup_p50_us as number);
    const betterOrEqualMemory = (o.measured_final_heap_bytes as number) <= (candidate.measured_final_heap_bytes as number);
    const strictlyBetter =
      (o.cold_lookup_p50_us as number) < (candidate.cold_lookup_p50_us as number) ||
      (o.measured_final_heap_bytes as number) < (candidate.measured_final_heap_bytes as number);
    return betterOrEqualLatency && betterOrEqualMemory && strictlyBetter;
  });
}

function paretoFilter(rows: Row[]): Row[] {
  return rows.filter((r) => !isDominated(r, rows));
}

export async function GET() {
  const csvPath = resultsCsvPath("pareto_results.csv");
  if (!csvPath) {
    return NextResponse.json({ available: false, rows: [], message: "results/pareto_results.csv not found. Run the Pareto sweep from the repo root to generate it." });
  }
  const text = fs.readFileSync(csvPath, "utf-8");
  const allRows = parseCsv(text).map((r) => toNumberRow(r, NUMERIC_FIELDS));

  const byWorkload = new Map<string, Row[]>();
  for (const row of allRows) {
    const key = `${row.workload}`;
    if (!byWorkload.has(key)) byWorkload.set(key, []);
    byWorkload.get(key)!.push(row);
  }

  const reduced: Row[] = [];
  for (const [, rows] of byWorkload) {
    const baselines = rows.filter((r) => r.implementation !== "SymTabV2");
    const v2Rows = rows.filter((r) => r.implementation === "SymTabV2");
    reduced.push(...baselines, ...paretoFilter(v2Rows));
  }

  return NextResponse.json({ available: true, rows: reduced, sourcePath: csvPath, totalRawRows: allRows.length });
}
