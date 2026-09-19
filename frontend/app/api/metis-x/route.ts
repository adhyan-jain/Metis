import { NextResponse } from "next/server";
import fs from "fs";
import path from "path";
import { parseCsv } from "../../../lib/csv";
import { repoRoot } from "../../../lib/repoPaths";

export async function GET() {
  try {
    const root = repoRoot();
    const canonicalPath = path.join(root, "results", "METIS_X_CANONICAL_DATASET.csv");
    const ablationPath = path.join(root, "results", "metis_x_ablation.csv");
    const failurePath = path.join(root, "results", "metis_x_failure_cases.csv");

    let canonicalData: any[] = [];
    let ablationData: any[] = [];
    let failureData: any[] = [];

    if (fs.existsSync(canonicalPath)) {
      canonicalData = parseCsv(fs.readFileSync(canonicalPath, "utf-8"));
    }

    if (fs.existsSync(ablationPath)) {
      ablationData = parseCsv(fs.readFileSync(ablationPath, "utf-8"));
    }

    if (fs.existsSync(failurePath)) {
      failureData = parseCsv(fs.readFileSync(failurePath, "utf-8"));
    }

    return NextResponse.json({
      success: true,
      canonical: canonicalData,
      ablation: ablationData,
      failureCases: failureData,
    });
  } catch (error: any) {
    return NextResponse.json({ success: false, error: error.message }, { status: 500 });
  }
}
