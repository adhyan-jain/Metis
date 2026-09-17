"use client";

import React, { useEffect, useState } from "react";
import { paretoRows as fallbackPareto, paretoFrontierRows as fallbackFrontier, ParetoRow, ParetoFrontierRow } from "../lib/benchmark-data";
import { ScatterChart, ScatterPoint } from "./charts/ScatterChart";

const CONSTRAINTS = [1.10, 1.25, 1.50, 2.00];

export function ParetoView() {
  const [rows, setRows] = useState<ParetoRow[]>(fallbackPareto);
  const [frontierRows, setFrontierRows] = useState<ParetoFrontierRow[]>(fallbackFrontier);
  const [source, setSource] = useState<"live" | "fallback">("fallback");
  const [constraint, setConstraint] = useState<number>(1.25);
  const [workload, setWorkload] = useState<string>("all");

  useEffect(() => {
    Promise.all([
      fetch("/api/pareto").then((r) => r.json()).catch(() => null),
      fetch("/api/pareto-frontier").then((r) => r.json()).catch(() => null),
    ]).then(([p, f]) => {
      if (p?.available && Array.isArray(p.rows) && p.rows.length > 0) {
        setRows(p.rows);
        setSource("live");
      }
      if (f?.available && Array.isArray(f.rows) && f.rows.length > 0) {
        setFrontierRows(f.rows);
      }
    });
  }, []);

  const workloads = Array.from(new Set(rows.map((r) => r.workload)));
  const filteredRows = workload === "all" ? rows : rows.filter((r) => r.workload === workload);

  const points: ScatterPoint[] = filteredRows.map((r) => ({
    x: r.cold_lookup_p50_us,
    y: r.measured_final_heap_bytes,
    series: r.implementation,
    label: `${r.workload} #${r.config_id}`,
  }));

  const satisfyingRows = frontierRows.filter(
    (r) => Math.abs(r.latency_constraint - constraint) < 0.001 && (workload === "all" || r.workload === workload)
  );

  return (
    <div className="space-y-6">
      <div className="panel p-6 rounded-2xl space-y-4">
        <div className="flex items-center justify-between">
          <div>
            <h2 className="text-sm font-semibold text-slate-900">Memory / Latency Pareto Frontier</h2>
            <p className="text-xs text-slate-500 mt-1">
              This is a memory-vs-latency optimization problem, not a single-winner comparison. Each point is one configuration; lower-left is better on both axes.
            </p>
          </div>
          <span className={`px-2 py-0.5 rounded-full text-[10px] font-mono border shrink-0 ${source === "live" ? "bg-teal-50 text-teal-700 border-teal-200" : "bg-slate-50 text-slate-500 border-slate-200"}`}>
            {source === "live" ? "results/pareto_results.csv" : "authoritative results"}
          </span>
        </div>

        <div className="flex flex-wrap items-center gap-4 text-xs">
          <label className="flex items-center gap-2">
            <span className="text-slate-500 font-medium">Workload</span>
            <select
              value={workload}
              onChange={(e) => setWorkload(e.target.value)}
              className="border border-slate-200 rounded-lg px-2 py-1 text-xs font-mono"
            >
              <option value="all">All</option>
              {workloads.map((w) => (
                <option key={w} value={w}>{w}</option>
              ))}
            </select>
          </label>
        </div>

        <ScatterChart
          points={points}
          xLabel="Cold Lookup p50 Latency (µs)"
          yLabel="Final Physical Heap (bytes)"
          formatX={(v) => `${v.toFixed(2)}µs`}
          formatY={(v) => v >= 1_000_000 ? `${(v / 1_000_000).toFixed(1)}MB` : `${(v / 1024).toFixed(0)}kB`}
        />
      </div>

      <div className="panel p-6 rounded-2xl space-y-4">
        <div>
          <h3 className="text-sm font-semibold text-slate-900">Latency-Constrained Configuration Selection</h3>
          <p className="text-xs text-slate-500 mt-1">
            Select a maximum acceptable latency ratio vs. Conventional. Shown: the configuration that minimizes memory while satisfying that bound, per workload.
          </p>
        </div>

        <div className="flex flex-wrap gap-2">
          {CONSTRAINTS.map((c) => (
            <button
              key={c}
              onClick={() => setConstraint(c)}
              className={`px-3 py-1.5 rounded-lg text-xs font-mono font-semibold border transition-colors ${
                constraint === c
                  ? "bg-indigo-600 text-white border-indigo-600"
                  : "bg-white text-slate-600 border-slate-200 hover:border-slate-300"
              }`}
            >
              {c.toFixed(2)}×
            </button>
          ))}
        </div>

        <div className="overflow-x-auto rounded-xl border border-slate-200">
          <table className="w-full text-left border-collapse text-xs font-mono">
            <thead>
              <tr className="bg-slate-50 text-slate-500 border-b border-slate-200 text-[11px] uppercase tracking-wider">
                <th className="py-3 px-4 font-semibold">Workload</th>
                <th className="py-3 px-4 font-semibold">Satisfied</th>
                <th className="py-3 px-4 font-semibold">Optimal Impl</th>
                <th className="py-3 px-4 text-right font-semibold">Latency Ratio vs Conv</th>
                <th className="py-3 px-4 text-right font-semibold">Memory Savings vs Conv</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-slate-100">
              {satisfyingRows.length === 0 ? (
                <tr><td colSpan={5} className="py-6 text-center text-slate-400">No frontier rows for this constraint/workload.</td></tr>
              ) : satisfyingRows.map((r, i) => (
                <tr key={i} className="hover:bg-slate-50 text-slate-700">
                  <td className="py-3 px-4 font-bold">{r.workload}</td>
                  <td className="py-3 px-4">
                    <span className={`px-2 py-0.5 rounded-full text-[10px] font-semibold ${r.satisfied ? "bg-teal-50 text-teal-700 border border-teal-200" : "bg-rose-50 text-rose-700 border border-rose-200"}`}>
                      {r.satisfied ? "yes" : "no"}
                    </span>
                  </td>
                  <td className="py-3 px-4">{r.optimal_impl}</td>
                  <td className="py-3 px-4 text-right">{r.latency_ratio_vs_conv.toFixed(2)}×</td>
                  <td className="py-3 px-4 text-right">{r.memory_savings_pct_vs_conv.toFixed(1)}%</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </div>
    </div>
  );
}
