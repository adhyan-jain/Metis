"use client";

import React, { useEffect, useState } from "react";
import { corpusCharacterizationRows as fallbackRows, CorpusCharacterizationRow } from "../lib/benchmark-data";
import { ScatterChart, ScatterPoint } from "./charts/ScatterChart";

export function WorkloadCharacteristics() {
  const [rows, setRows] = useState<CorpusCharacterizationRow[]>(fallbackRows);
  const [source, setSource] = useState<"live" | "fallback">("fallback");

  useEffect(() => {
    fetch("/api/workload-characteristics")
      .then((r) => r.json())
      .then((data) => {
        if (data.available && Array.isArray(data.rows) && data.rows.length > 0) {
          setRows(data.rows);
          setSource("live");
        }
      })
      .catch(() => {});
  }, []);

  const lengthVsChurn: ScatterPoint[] = rows.map((r) => ({
    x: r.mean_identifier_length,
    y: r.churn,
    series: "SymTabV2",
    label: r.corpus,
  }));

  const prefixVsScopeDepth: ScatterPoint[] = rows.map((r) => ({
    x: r.prefix_similarity,
    y: r.avg_scope_depth,
    series: "SymTabV2",
    label: r.corpus,
  }));

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <h2 className="text-sm font-semibold text-slate-900">Real-World Workload Characterization</h2>
        <span className={`px-2 py-0.5 rounded-full text-[10px] font-mono border ${source === "live" ? "bg-teal-50 text-teal-700 border-teal-200" : "bg-slate-50 text-slate-500 border-slate-200"}`}>
          {source === "live" ? "results/corpus_characterization.csv" : "authoritative results"}
        </span>
      </div>

      <div className="panel p-6 rounded-2xl overflow-x-auto">
        <table className="w-full text-left border-collapse text-xs font-mono">
          <thead>
            <tr className="bg-slate-50 text-slate-500 border-b border-slate-200 text-[11px] uppercase tracking-wider">
              <th className="py-3 px-4 font-semibold">Corpus</th>
              <th className="py-3 px-4 text-right font-semibold">Files</th>
              <th className="py-3 px-4 text-right font-semibold">Decls</th>
              <th className="py-3 px-4 text-right font-semibold">Unique Symbols</th>
              <th className="py-3 px-4 text-right font-semibold">Redeclarations</th>
              <th className="py-3 px-4 text-right font-semibold">Shadowing</th>
              <th className="py-3 px-4 text-right font-semibold">Max Scope Depth</th>
              <th className="py-3 px-4 text-right font-semibold">Mean Ident. Len</th>
              <th className="py-3 px-4 text-right font-semibold">Prefix Similarity</th>
              <th className="py-3 px-4 text-right font-semibold">Repeat Rate</th>
              <th className="py-3 px-4 text-right font-semibold">Access Skew (Gini-like)</th>
              <th className="py-3 px-4 text-right font-semibold">Churn</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-slate-100">
            {rows.map((r) => (
              <tr key={r.corpus} className="hover:bg-slate-50 text-slate-700">
                <td className="py-3 px-4 font-bold">{r.corpus}</td>
                <td className="py-3 px-4 text-right">{r.files.toLocaleString()}</td>
                <td className="py-3 px-4 text-right">{r.declarations.toLocaleString()}</td>
                <td className="py-3 px-4 text-right">{r.unique_symbols.toLocaleString()}</td>
                <td className="py-3 px-4 text-right">{r.redeclarations.toLocaleString()}</td>
                <td className="py-3 px-4 text-right">{r.shadowing.toLocaleString()}</td>
                <td className="py-3 px-4 text-right">{r.max_scope_depth}</td>
                <td className="py-3 px-4 text-right">{r.mean_identifier_length.toFixed(2)}B</td>
                <td className="py-3 px-4 text-right">{r.prefix_similarity.toFixed(3)}</td>
                <td className="py-3 px-4 text-right">{r.repeat_rate.toFixed(3)}</td>
                <td className="py-3 px-4 text-right">{r.access_skew.toFixed(3)}</td>
                <td className="py-3 px-4 text-right">{r.churn.toFixed(3)}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        <div className="panel p-6 rounded-2xl">
          <h3 className="text-xs font-mono font-semibold text-slate-500 uppercase tracking-wider mb-3">
            Identifier Length vs. Scope Churn
          </h3>
          <ScatterChart
            points={lengthVsChurn}
            xLabel="Mean Identifier Length (bytes)"
            yLabel="Scope Churn"
            formatX={(v) => `${v.toFixed(0)}B`}
            formatY={(v) => v.toFixed(2)}
          />
          <p className="text-[10px] text-slate-400 mt-2 leading-relaxed">
            Association shown across only 3 corpora — not sufficient to establish causation, only to compare the sampled workloads.
          </p>
        </div>
        <div className="panel p-6 rounded-2xl">
          <h3 className="text-xs font-mono font-semibold text-slate-500 uppercase tracking-wider mb-3">
            Prefix Similarity vs. Avg Scope Depth
          </h3>
          <ScatterChart
            points={prefixVsScopeDepth}
            xLabel="Prefix Similarity"
            yLabel="Avg Scope Depth"
            formatX={(v) => v.toFixed(2)}
            formatY={(v) => v.toFixed(1)}
          />
          <p className="text-[10px] text-slate-400 mt-2 leading-relaxed">
            Association shown across only 3 corpora — not sufficient to establish causation, only to compare the sampled workloads.
          </p>
        </div>
      </div>
    </div>
  );
}
