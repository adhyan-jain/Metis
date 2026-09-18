"use client";

import React, { useEffect, useState } from "react";

interface EmbeddedLatencyRow {
  corpus: string;
  implementation: string;
  unique_names: number;
  lookup_p50_us: number;
  lookup_p95_us: number;
  lookup_p99_us: number;
  lookup_mean_us: number;
  insert_p50_us: number;
  insert_p95_us: number;
}

const CANONICAL_EMBEDDED_LATENCY: EmbeddedLatencyRow[] = [
  { corpus: "FreeRTOS", implementation: "EmbeddedConventional", unique_names: 10386, lookup_p50_us: 0.051, lookup_p95_us: 0.094, lookup_p99_us: 0.149, lookup_mean_us: 0.057, insert_p50_us: 1.419, insert_p95_us: 10.81 },
  { corpus: "FreeRTOS", implementation: "SymTabV3", unique_names: 10386, lookup_p50_us: 0.071, lookup_p95_us: 0.277, lookup_p99_us: 0.445, lookup_mean_us: 0.101, insert_p50_us: 0.402, insert_p95_us: 1.057 },
  { corpus: "Arduino", implementation: "EmbeddedConventional", unique_names: 11000, lookup_p50_us: 0.046, lookup_p95_us: 0.089, lookup_p99_us: 0.130, lookup_mean_us: 0.051, insert_p50_us: 0.228, insert_p95_us: 13.914 },
  { corpus: "Arduino", implementation: "SymTabV3", unique_names: 11000, lookup_p50_us: 0.061, lookup_p95_us: 0.171, lookup_p99_us: 0.356, lookup_mean_us: 0.077, insert_p50_us: 0.261, insert_p95_us: 0.889 },
  { corpus: "Zephyr", implementation: "EmbeddedConventional", unique_names: 228739, lookup_p50_us: 0.050, lookup_p95_us: 0.125, lookup_p99_us: 0.225, lookup_mean_us: 0.060, insert_p50_us: 0.182, insert_p95_us: 13.594 },
  { corpus: "Zephyr", implementation: "SymTabV3", unique_names: 228739, lookup_p50_us: 0.066, lookup_p95_us: 0.228, lookup_p99_us: 0.481, lookup_mean_us: 0.092, insert_p50_us: 0.295, insert_p95_us: 0.890 },
  { corpus: "ESP-IDF", implementation: "EmbeddedConventional", unique_names: 231075, lookup_p50_us: 0.061, lookup_p95_us: 0.236, lookup_p99_us: 0.374, lookup_mean_us: 0.086, insert_p50_us: 0.365, insert_p95_us: 284.002 },
  { corpus: "ESP-IDF", implementation: "SymTabV3", unique_names: 231075, lookup_p50_us: 0.079, lookup_p95_us: 0.297, lookup_p99_us: 0.712, lookup_mean_us: 0.113, insert_p50_us: 0.325, insert_p95_us: 0.948 },
];

export function LatencyTable() {
  const [rows, setRows] = useState<EmbeddedLatencyRow[]>(CANONICAL_EMBEDDED_LATENCY);

  useEffect(() => {
    fetch("/api/canonical")
      .then((r) => r.json())
      .then((data) => {
        if (data.available && Array.isArray(data.embeddedRows) && data.embeddedRows.length > 0) {
          const embRows: EmbeddedLatencyRow[] = data.embeddedRows.map((r: any) => ({
            corpus: r.corpus,
            implementation: r.implementation,
            unique_names: Number(r.unique_names),
            lookup_p50_us: Number(r.lookup_p50_us),
            lookup_p95_us: Number(r.lookup_p95_us),
            lookup_p99_us: Number(r.lookup_p99_us),
            lookup_mean_us: Number(r.lookup_mean_us),
            insert_p50_us: Number(r.insert_p50_us),
            insert_p95_us: Number(r.insert_p95_us),
          }));
          setRows(embRows);
        }
      })
      .catch(() => {});
  }, []);

  return (
    <div className="panel p-6 rounded-2xl border border-slate-200 space-y-6">
      <div className="pb-4 border-b border-slate-200">
        <h3 className="font-mono text-base font-semibold text-slate-900">
          Tail Latency Distribution &amp; Reconstruction Overhead Analysis
        </h3>
        <p className="text-xs text-slate-500 mt-1">
          Measured lookup and insert latency percentiles (p50, p95, p99) under multi-repetition execution (R=3)
        </p>
      </div>

      {/* Latency Trade-off Explanation Banner */}
      <div className="panel-soft p-4 rounded-xl border border-amber-200 bg-amber-50/50 space-y-2">
        <div className="flex items-center gap-2">
          <span className="font-mono font-bold text-xs uppercase tracking-wider text-amber-900">
            Scientific Latency Finding &amp; Gate Failure:
          </span>
          <span className="px-2 py-0.5 rounded text-[10px] font-mono font-bold bg-amber-200 text-amber-800">
            Zephyr p95 Ratio = 1.824× (1.25× Target FAILED)
          </span>
        </div>
        <p className="text-xs text-slate-700 leading-relaxed">
          &ldquo;The compressed representation reduces physical memory footprint, but introduces reconstruction work during lookup. METIS therefore exposes a measurable memory/latency trade-off rather than eliminating it.&rdquo;
        </p>
        <p className="text-[11px] text-slate-600 leading-relaxed">
          On Zephyr, reconstructing front-coded members hits 8.81% of lookups with an average depth of 3.31 steps to anchor strings. Even with zero dynamic memory allocation (<code className="font-mono bg-white px-1 rounded border">char stackBuf[512]</code>), copying multiple memory slices introduces measured latency overhead that exceeds the tight 1.25&times; gate.
        </p>
      </div>

      {/* Table */}
      <div className="overflow-x-auto rounded-xl border border-slate-200">
        <table className="w-full text-left border-collapse text-xs font-mono">
          <thead>
            <tr className="bg-slate-50 text-slate-500 border-b border-slate-200 text-[11px] uppercase tracking-wider">
              <th className="py-3 px-4 text-left font-semibold">Workload</th>
              <th className="py-3 px-4 text-left font-semibold">Implementation</th>
              <th className="py-3 px-4 text-right font-semibold text-teal-700">Lookup p50 (µs)</th>
              <th className="py-3 px-4 text-right font-semibold text-amber-700">Lookup p95 (µs)</th>
              <th className="py-3 px-4 text-right font-semibold text-rose-700">Lookup p99 (µs)</th>
              <th className="py-3 px-4 text-right font-semibold">Lookup Mean (µs)</th>
              <th className="py-3 px-4 text-right font-semibold">Insert p50 (µs)</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-slate-100">
            {rows.map((r, i) => {
              const isV3 = r.implementation === "SymTabV3";
              const isZephyr = r.corpus === "Zephyr";
              return (
                <tr
                  key={i}
                  className={`transition-colors ${
                    isZephyr && isV3
                      ? "bg-teal-50/50 font-semibold text-slate-900"
                      : "hover:bg-slate-50 text-slate-700"
                  }`}
                >
                  <td className="py-3 px-4 font-bold text-slate-800">{r.corpus}</td>
                  <td className="py-3 px-4 flex items-center gap-2">
                    <span className={`w-2 h-2 rounded-full ${isV3 ? "bg-teal-500" : "bg-slate-400"}`} />
                    <span>{r.implementation}</span>
                  </td>
                  <td className="py-3 px-4 text-right text-teal-800 font-bold">{r.lookup_p50_us.toFixed(3)}</td>
                  <td className="py-3 px-4 text-right text-amber-800 font-bold">{r.lookup_p95_us.toFixed(3)}</td>
                  <td className="py-3 px-4 text-right text-rose-800">{r.lookup_p99_us.toFixed(3)}</td>
                  <td className="py-3 px-4 text-right">{r.lookup_mean_us.toFixed(3)}</td>
                  <td className="py-3 px-4 text-right">{r.insert_p50_us.toFixed(3)}</td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>
    </div>
  );
}
