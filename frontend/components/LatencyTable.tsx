"use client";

import React, { useEffect, useState } from "react";
import { DATASETS as fallbackDatasets, benchmarkRows as fallbackRows, corpusRows, BenchmarkRow } from "../lib/benchmark-data";

export function LatencyTable() {
  const [rows, setRows] = useState<BenchmarkRow[]>(fallbackRows);
  const [source, setSource] = useState<"live" | "fallback">("fallback");
  const [selectedDataset, setSelectedDataset] = useState<string>("large");

  useEffect(() => {
    fetch("/api/benchmarks")
      .then((r) => r.json())
      .then((data) => {
        if (data.available && Array.isArray(data.rows) && data.rows.length > 0) {
          setRows(data.rows);
          setSource("live");
        }
      })
      .catch(() => {});
  }, []);

  const datasets = source === "live" ? Array.from(new Set(rows.map((r) => r.dataset))) : fallbackDatasets;
  const filteredRows = rows.filter((r) => r.dataset === selectedDataset);

  return (
    <div className="panel p-6 rounded-2xl space-y-6">
      {/* Header & Filter */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4 pb-3 border-b border-slate-200">
        <div>
          <h3 className="font-mono text-base font-semibold text-slate-900">
            Per-Operation Latency &amp; Overhead Breakdown
          </h3>
          <p className="text-xs text-slate-500 mt-1">
            Exact operation timing measurements in microseconds (µs)
          </p>
        </div>

        <div className="flex items-center gap-2">
          <span className={`px-2 py-0.5 rounded-full text-[10px] font-mono border ${source === "live" ? "bg-teal-50 text-teal-700 border-teal-200" : "bg-slate-50 text-slate-500 border-slate-200"}`}>
            {source === "live" ? "statistical_summary.csv" : "authoritative snapshot"}
          </span>
          <label className="text-xs font-mono text-slate-500">Dataset:</label>
          <select
            value={selectedDataset}
            onChange={(e) => setSelectedDataset(e.target.value)}
            className="bg-white border border-slate-300 rounded-lg px-3 py-1.5 text-xs font-mono text-indigo-700 focus:outline-none focus:border-indigo-400 cursor-pointer"
          >
            {datasets.map((ds) => (
              <option key={ds} value={ds}>
                {ds} ({rows.find((r) => r.dataset === ds)?.symbols} symbols)
              </option>
            ))}
          </select>
        </div>
      </div>

      {/* Operation Explanation Grid */}
      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-3 text-xs text-slate-500">
        <div className="panel-soft p-3 rounded-xl">
          <span className="font-mono font-semibold text-slate-800 block mb-1">Insert (Declaration)</span>
          Representation policy selection, directory write, and front-coding block placement.
        </div>
        <div className="panel-soft p-3 rounded-xl">
          <span className="font-mono font-semibold text-slate-800 block mb-1">Cold Lookup (p50)</span>
          Directory search accelerated by 1-byte hash fingerprints before block decode.
        </div>
        <div className="panel-soft p-3 rounded-xl">
          <span className="font-mono font-semibold text-slate-800 block mb-1">Fingerprint Rejection</span>
          1-byte hash fingerprint mismatch skips expensive block decompression.
        </div>
        <div className="panel-soft p-3 rounded-xl">
          <span className="font-mono font-semibold text-slate-800 block mb-1">Scope Enter / Exit</span>
          Pushing scope frame blocks and recycling inactive scope slots on exit.
        </div>
      </div>

      {/* Synthetic Workloads Latency Table */}
      <div className="overflow-x-auto rounded-xl border border-slate-200">
        <table className="w-full text-left border-collapse text-xs font-mono">
          <thead>
            <tr className="bg-slate-50 text-slate-500 border-b border-slate-200 text-[11px] uppercase tracking-wider">
              <th className="py-3 px-4 text-left font-semibold">Implementation</th>
              <th className="py-3 px-4 text-right font-semibold">Memory / Sym</th>
              <th className="py-3 px-4 text-right font-semibold">Relative Scale</th>
              <th className="py-3 px-4 text-right font-semibold text-amber-700">Insert (µs)</th>
              <th className="py-3 px-4 text-right font-semibold text-teal-700">Cold p50 (µs)</th>
              <th className="py-3 px-4 text-right font-semibold">Scope Enter (µs)</th>
              <th className="py-3 px-4 text-right font-semibold">Scope Exit (µs)</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-slate-100">
            {filteredRows.map((r, i) => {
              const isV2 = r.implementation === "SymTabV2" || r.implementation === "BudgetSym";
              const isV1 = r.implementation === "BudgetSymV1";
              const isInterned = r.implementation === "Interned";
              return (
                <tr key={i} className={`transition-colors ${isV2 ? "bg-teal-50/60 text-slate-900 font-semibold" : "hover:bg-slate-50 text-slate-700"}`}>
                  <td className="py-3 px-4 flex items-center gap-2">
                    <span className={`w-2 h-2 rounded-full ${isV2 ? "bg-teal-500" : isV1 ? "bg-rose-400" : isInterned ? "bg-indigo-400" : "bg-slate-400"}`} />
                    <span className={isV2 ? "text-teal-700 font-bold" : ""}>{r.implementation}</span>
                  </td>
                  <td className="py-3 px-4 text-right font-numeric">{r.memory_per_symbol.toFixed(2)} B</td>
                  <td className="py-3 px-4 text-right text-teal-700 font-bold">{r.compression_ratio.toFixed(2)}×</td>
                  <td className="py-3 px-4 text-right font-numeric text-amber-700">{r.insert_us.toFixed(3)}</td>
                  <td className="py-3 px-4 text-right font-numeric font-bold text-teal-700">{r.lookup_success_us.toFixed(4)}</td>
                  <td className="py-3 px-4 text-right font-numeric">{r.scope_enter_us.toFixed(3)}</td>
                  <td className="py-3 px-4 text-right font-numeric">{r.scope_exit_us.toFixed(1)}</td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>

      {/* Real-World Corpus Latency Summary */}
      <div className="panel p-5 rounded-xl space-y-3">
        <h4 className="font-mono text-xs font-bold text-slate-900 uppercase tracking-wider">
          Real-World Corpus Cold p50 Lookup Latency (Measured)
        </h4>
        <div className="grid grid-cols-1 sm:grid-cols-3 gap-3">
          {corpusRows.filter((r) => r.implementation === "SymTabV2").map((c) => {
            const conv = corpusRows.find((r) => r.corpus === c.corpus && r.implementation === "Conventional");
            return (
              <div key={c.corpus} className="panel-soft p-3 rounded-lg border border-slate-200">
                <div className="font-mono text-xs font-bold text-slate-800">{c.corpus}</div>
                <div className="font-mono text-lg font-extrabold text-teal-700 mt-1">
                  {c.lookup_p50_us.toFixed(4)} µs
                </div>
                <div className="text-[10px] font-mono text-slate-500 mt-0.5">
                  vs Conventional: {conv?.lookup_p50_us.toFixed(4)} µs
                </div>
              </div>
            );
          })}
        </div>
      </div>

      {/* Trade-off Callout */}
      <div className="bg-amber-50 border border-amber-200 rounded-xl p-4 flex items-start gap-3 text-xs text-amber-900">
        <span className="text-base leading-none">⚖</span>
        <div>
          <strong className="font-mono text-amber-800 block mb-0.5">Lookup Latency Nuance:</strong>
          1-byte hash fingerprints provide acceleration on real-world corpora (Arduino: 0.089 µs vs Conv 0.124 µs; Zephyr: 0.098 µs vs Conv 0.254 µs) where fingerprint mismatches quickly reject non-matching candidate entries. On synthetic random long string workloads with high prefix similarity, decompression across block chains adds measurable cold lookup overhead (0.415 µs).
        </div>
      </div>
    </div>
  );
}
