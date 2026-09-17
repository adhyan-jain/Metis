// Locates the repo-root artifacts (the compiled C++ binaries and the
// results/ CSVs) regardless of whether `next dev`/`next start` was launched
// from the repo root or from frontend/ -- both are common depending on how
// the user runs it (see start.sh vs. `cd frontend && npm run dev`).
import fs from "fs";
import path from "path";

function firstExisting(candidates: string[]): string | null {
  for (const c of candidates) {
    if (fs.existsSync(c)) return c;
  }
  return null;
}

const cwd = process.cwd();

export function repoRoot(): string {
  // process.cwd() is either the repo root (start.sh) or frontend/ (npm run dev inside frontend/).
  if (fs.existsSync(path.join(cwd, "include", "budget_sym.hpp"))) return cwd;
  if (fs.existsSync(path.join(cwd, "..", "include", "budget_sym.hpp"))) return path.join(cwd, "..");
  return cwd;
}

export function analyzeBinaryPath(): string | null {
  const root = repoRoot();
  return firstExisting([
    path.join(root, "analyze.exe"),
    path.join(root, "analyze"),
  ]);
}

export function resultsCsvPath(name:
  | "benchmark_results.csv"
  | "ablation_results.csv"
  | "cache_benchmark_results.csv"
  | "algorithm_comparison.csv"
  | "pareto_results.csv"
  | "pareto_frontier.csv"
  | "corpus_characterization.csv"
): string | null {
  const root = repoRoot();
  return firstExisting([
    path.join(root, "results", name),
    path.join(root, "frontend", "data", name),
    path.join(root, "data", name),
  ]);
}
