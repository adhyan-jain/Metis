// Reads results/corpus_characterization.csv fresh from disk. Same honesty
// rule as /api/benchmarks: real file or an explicit empty state.
import { NextResponse } from "next/server";
import fs from "fs";
import { parseCsv, toNumberRow } from "../../../lib/csv";
import { resultsCsvPath } from "../../../lib/repoPaths";

export const runtime = "nodejs";

const NUMERIC_FIELDS = [
  "files", "declarations", "uses", "unique_symbols", "redeclarations", "shadowing",
  "max_scope_depth", "avg_scope_depth", "mean_identifier_length", "prefix_similarity",
  "repeat_rate", "entropy", "access_skew", "churn",
];

export async function GET() {
  const csvPath = resultsCsvPath("corpus_characterization.csv");
  if (!csvPath) {
    return NextResponse.json({ available: false, rows: [], message: "results/corpus_characterization.csv not found. Run the corpus characterization step from the repo root to generate it." });
  }
  const text = fs.readFileSync(csvPath, "utf-8");
  const rows = parseCsv(text).map((r) => toNumberRow(r, NUMERIC_FIELDS));
  return NextResponse.json({ available: true, rows, sourcePath: csvPath });
}
