"use client";

import React, { useEffect, useState } from "react";

interface EmbeddedRow {
  corpus: string;
  implementation: string;
  unique_names: number;
  measured_peak_heap_bytes: number;
  measured_final_heap_bytes: number;
  lookup_p50_us: number;
  lookup_p95_us: number;
  lookup_p99_us: number;
  lookup_mean_us: number;
}

const CANONICAL_EMBEDDED_DATA: EmbeddedRow[] = [
  { corpus: "FreeRTOS", implementation: "EmbeddedConventional", unique_names: 10386, measured_peak_heap_bytes: 4230000, measured_final_heap_bytes: 3890000, lookup_p50_us: 0.051, lookup_p95_us: 0.094, lookup_p99_us: 0.149, lookup_mean_us: 0.057 },
  { corpus: "FreeRTOS", implementation: "SymTabV3", unique_names: 10386, measured_peak_heap_bytes: 4780000, measured_final_heap_bytes: 4720000, lookup_p50_us: 0.071, lookup_p95_us: 0.277, lookup_p99_us: 0.445, lookup_mean_us: 0.101 },
  { corpus: "Arduino", implementation: "EmbeddedConventional", unique_names: 11000, measured_peak_heap_bytes: 2310000, measured_final_heap_bytes: 2270000, lookup_p50_us: 0.046, lookup_p95_us: 0.089, lookup_p99_us: 0.130, lookup_mean_us: 0.051 },
  { corpus: "Arduino", implementation: "SymTabV3", unique_names: 11000, measured_peak_heap_bytes: 2990000, measured_final_heap_bytes: 2990000, lookup_p50_us: 0.061, lookup_p95_us: 0.171, lookup_p99_us: 0.356, lookup_mean_us: 0.077 },
  { corpus: "Zephyr", implementation: "EmbeddedConventional", unique_names: 228739, measured_peak_heap_bytes: 76370000, measured_final_heap_bytes: 66910000, lookup_p50_us: 0.050, lookup_p95_us: 0.125, lookup_p99_us: 0.225, lookup_mean_us: 0.060 },
  { corpus: "Zephyr", implementation: "SymTabV3", unique_names: 228739, measured_peak_heap_bytes: 57890000, measured_final_heap_bytes: 53390000, lookup_p50_us: 0.066, lookup_p95_us: 0.228, lookup_p99_us: 0.481, lookup_mean_us: 0.092 },
  { corpus: "ESP-IDF", implementation: "EmbeddedConventional", unique_names: 231075, measured_peak_heap_bytes: 75770000, measured_final_heap_bytes: 67820000, lookup_p50_us: 0.061, lookup_p95_us: 0.236, lookup_p99_us: 0.374, lookup_mean_us: 0.086 },
  { corpus: "ESP-IDF", implementation: "SymTabV3", unique_names: 231075, measured_peak_heap_bytes: 89360000, measured_final_heap_bytes: 81910000, lookup_p50_us: 0.079, lookup_p95_us: 0.297, lookup_p99_us: 0.712, lookup_mean_us: 0.113 },
];

export function BenchmarkCharts() {
  const [rows, setRows] = useState<EmbeddedRow[]>(CANONICAL_EMBEDDED_DATA);
  const [metricMode, setMetricMode] = useState<"final_heap" | "peak_heap" | "p95_latency">("final_heap");

  useEffect(() => {
    fetch("/api/canonical")
      .then((r) => r.json())
      .then((data) => {
        if (data.available && Array.isArray(data.embeddedRows) && data.embeddedRows.length > 0) {
          const embRows: EmbeddedRow[] = data.embeddedRows.map((r: any) => ({
            corpus: r.corpus,
            implementation: r.implementation,
            unique_names: Number(r.unique_names),
            measured_peak_heap_bytes: Number(r.measured_peak_heap_bytes),
            measured_final_heap_bytes: Number(r.measured_final_heap_bytes),
            lookup_p50_us: Number(r.lookup_p50_us),
            lookup_p95_us: Number(r.lookup_p95_us),
            lookup_p99_us: Number(r.lookup_p99_us),
            lookup_mean_us: Number(r.lookup_mean_us),
          }));
          setRows(embRows);
        }
      })
      .catch(() => {});
  }, []);

  const workloads = ["FreeRTOS", "Arduino", "Zephyr", "ESP-IDF"];

  const getRow = (w: string, impl: string) => rows.find((r) => r.corpus === w && r.implementation === impl);

  const formatBytes = (bytes: number) => {
    if (bytes >= 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
    return `${(bytes / 1024).toFixed(1)} kB`;
  };

  return (
    <div className="space-y-6">
      <div className="panel p-6 rounded-2xl border border-slate-200 space-y-6">
        <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4 pb-4 border-b border-slate-200">
          <div>
            <h3 className="font-mono text-base font-semibold text-slate-900">
              Canonical Embedded Benchmark Evaluation
            </h3>
            <p className="text-xs text-slate-500 mt-1">
              Physical allocator heap footprint (<code className="font-mono">malloc_usable_size</code>) & multi-repetition lookup latencies
            </p>
          </div>
          <div className="flex items-center gap-1 bg-slate-50 p-1 rounded-xl border border-slate-200">
            <button
              onClick={() => setMetricMode("final_heap")}
              className={`px-3 py-1.5 rounded-lg text-xs font-mono transition-all ${
                metricMode === "final_heap"
                  ? "bg-teal-50 text-teal-700 font-semibold border border-teal-200"
                  : "text-slate-500 hover:text-slate-800"
              }`}
            >
              Final Heap
            </button>
            <button
              onClick={() => setMetricMode("peak_heap")}
              className={`px-3 py-1.5 rounded-lg text-xs font-mono transition-all ${
                metricMode === "peak_heap"
                  ? "bg-indigo-50 text-indigo-700 font-semibold border border-indigo-200"
                  : "text-slate-500 hover:text-slate-800"
              }`}
            >
              Peak Heap
            </button>
            <button
              onClick={() => setMetricMode("p95_latency")}
              className={`px-3 py-1.5 rounded-lg text-xs font-mono transition-all ${
                metricMode === "p95_latency"
                  ? "bg-amber-50 text-amber-700 font-semibold border border-amber-200"
                  : "text-slate-500 hover:text-slate-800"
              }`}
            >
              p95 Latency
            </button>
          </div>
        </div>

        {/* Workload Comparison Grid */}
        <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4">
          {workloads.map((w) => {
            const embConv = getRow(w, "EmbeddedConventional");
            const v3 = getRow(w, "SymTabV3");

            const embBytes = metricMode === "final_heap"
              ? embConv?.measured_final_heap_bytes ?? 0
              : embConv?.measured_peak_heap_bytes ?? 0;

            const v3Bytes = metricMode === "final_heap"
              ? v3?.measured_final_heap_bytes ?? 0
              : v3?.measured_peak_heap_bytes ?? 0;

            const embLat = embConv?.lookup_p95_us ?? 0;
            const v3Lat = v3?.lookup_p95_us ?? 0;

            const isZephyr = w === "Zephyr";
            const deltaPct = embBytes > 0 ? ((v3Bytes - embBytes) / embBytes) * 100 : 0;
            const latRatio = embLat > 0 ? (v3Lat / embLat) : 1;

            return (
              <div
                key={w}
                className={`p-4 rounded-xl border flex flex-col justify-between gap-4 ${
                  isZephyr ? "bg-teal-50/40 border-teal-300 ring-1 ring-teal-200" : "bg-white border-slate-200"
                }`}
              >
                <div>
                  <div className="flex items-center justify-between mb-2">
                    <span className="font-bold text-sm text-slate-900">{w}</span>
                    {isZephyr ? (
                      <span className="px-2 py-0.5 rounded text-[10px] font-mono font-bold bg-teal-100 text-teal-800">
                        RAM WIN (-20.2%)
                      </span>
                    ) : (
                      <span className="px-2 py-0.5 rounded text-[10px] font-mono bg-slate-100 text-slate-600">
                        {embConv?.unique_names.toLocaleString()} syms
                      </span>
                    )}
                  </div>

                  {metricMode === "p95_latency" ? (
                    <div className="space-y-1.5 font-mono text-xs">
                      <div className="flex justify-between text-slate-500">
                        <span>EmbConv:</span>
                        <span>{embLat.toFixed(3)} µs</span>
                      </div>
                      <div className="flex justify-between font-bold text-slate-800">
                        <span>SymTabV3:</span>
                        <span>{v3Lat.toFixed(3)} µs</span>
                      </div>
                      <div className="flex justify-between pt-1 border-t border-slate-200 font-bold text-amber-700">
                        <span>Tail Ratio:</span>
                        <span>{latRatio.toFixed(3)}× (Gate FAIL)</span>
                      </div>
                    </div>
                  ) : (
                    <div className="space-y-1.5 font-mono text-xs">
                      <div className="flex justify-between text-slate-500">
                        <span>EmbConv:</span>
                        <span>{formatBytes(embBytes)}</span>
                      </div>
                      <div className="flex justify-between font-bold text-slate-800">
                        <span>SymTabV3:</span>
                        <span>{formatBytes(v3Bytes)}</span>
                      </div>
                      <div className={`flex justify-between pt-1 border-t border-slate-200 font-bold ${
                        deltaPct < 0 ? "text-teal-700" : "text-red-600"
                      }`}>
                        <span>RAM Delta:</span>
                        <span>{deltaPct > 0 ? `+${deltaPct.toFixed(1)}%` : `${deltaPct.toFixed(1)}%`}</span>
                      </div>
                    </div>
                  )}
                </div>

                <div className="text-[11px] text-slate-500 border-t border-slate-100 pt-2 font-mono">
                  {isZephyr ? (
                    <span className="text-teal-900 font-semibold">
                      13.52 MB final heap saved; 1.824× p95 latency
                    </span>
                  ) : (
                    <span>Short SSO names dominate; Conv optimal</span>
                  )}
                </div>
              </div>
            );
          })}
        </div>
      </div>
    </div>
  );
}
