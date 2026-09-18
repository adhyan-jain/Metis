"use client";

import React, { useEffect, useState } from "react";

interface BenchmarkEntry {
  corpus: string;
  implementation: string;
  declarations: number;
  uses: number;
  unique_names: number;
  measured_final_heap_bytes: number;
  lookup_p50_us: number;
  lookup_p95_us: number;
  count_inline: number;
  count_interned: number;
  count_compressed: number;
  reconstruction_count: number;
  mean_reconstruction_depth: number;
}

const FALLBACK_CORPORA = [
  "FreeRTOS", "Arduino", "Zephyr", "ESP-IDF", "CPython", "Lua", "Clang", "Qt6",
  "Eigen", "Nginx", "SQLite", "Linux-Kernel", "V8", "PostgreSQL", "Redis",
  "LLVM-Core", "FFmpeg", "OpenSSL", "Git", "OpenCV", "Zstandard", "Protobuf",
  "Mbed-TLS", "Curl", "cJSON", "Nanopb"
];

export function WorkloadCharacteristics() {
  const [data, setData] = useState<BenchmarkEntry[]>([]);
  const [selectedCorpus, setSelectedCorpus] = useState<string>("Zephyr");
  const [selectedImpl, setSelectedImpl] = useState<string>("All");

  useEffect(() => {
    fetch("/api/workload-characteristics")
      .then((r) => r.json())
      .then((res) => {
        if (res.available && Array.isArray(res.benchmarkRows) && res.benchmarkRows.length > 0) {
          setData(res.benchmarkRows);
        }
      })
      .catch(() => {});
  }, []);

  const corpora = data.length > 0 ? Array.from(new Set(data.map((r) => r.corpus))) : FALLBACK_CORPORA;
  const filteredData = data.filter((r) => {
    const matchCorpus = selectedCorpus === "All" || r.corpus === selectedCorpus;
    const matchImpl = selectedImpl === "All" || r.implementation === selectedImpl;
    return matchCorpus && matchImpl;
  });

  const formatBytes = (b: number) => {
    if (!b) return "—";
    if (b >= 1024 * 1024) return `${(b / (1024 * 1024)).toFixed(2)} MB`;
    return `${(b / 1024).toFixed(1)} kB`;
  };

  const currentV3 = data.find((r) => r.corpus === selectedCorpus && r.implementation === "SymTabV3");

  return (
    <div className="space-y-6">
      {/* Controls & Filter */}
      <div className="panel p-6 rounded-2xl border border-slate-200 space-y-4">
        <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4 pb-4 border-b border-slate-200">
          <div>
            <h3 className="font-mono text-base font-semibold text-slate-900">
              26 Real-World Software Corpora Workload Explorer
            </h3>
            <p className="text-xs text-slate-500 mt-1">
              Semantic event streams (DECLARE, USE, ENTER_SCOPE, EXIT_SCOPE) across production embedded &amp; host software
            </p>
          </div>

          <div className="flex flex-wrap items-center gap-3">
            <div>
              <label className="text-xs font-mono text-slate-500 mr-2">Corpus:</label>
              <select
                value={selectedCorpus}
                onChange={(e) => setSelectedCorpus(e.target.value)}
                className="bg-white border border-slate-300 rounded-lg px-3 py-1.5 text-xs font-mono text-teal-700 font-semibold focus:outline-none focus:border-teal-400 cursor-pointer"
              >
                <option value="All">All Corpora (Summary)</option>
                {corpora.map((c) => (
                  <option key={c} value={c}>{c}</option>
                ))}
              </select>
            </div>

            <div>
              <label className="text-xs font-mono text-slate-500 mr-2">Impl:</label>
              <select
                value={selectedImpl}
                onChange={(e) => setSelectedImpl(e.target.value)}
                className="bg-white border border-slate-300 rounded-lg px-3 py-1.5 text-xs font-mono text-indigo-700 font-semibold focus:outline-none focus:border-indigo-400 cursor-pointer"
              >
                <option value="All">All Architectures</option>
                <option value="Conventional">Conventional (SSO)</option>
                <option value="EmbeddedConventional">EmbeddedConventional</option>
                <option value="Interned">Interned</option>
                <option value="SymTabV3">SymTabV3 (Proposed)</option>
                <option value="SymTabV4">SymTabV4 (Negative Result)</option>
                <option value="BudgetSymV1">BudgetSymV1 (Historical)</option>
                <option value="SymTabV2">SymTabV2 (Historical)</option>
              </select>
            </div>
          </div>
        </div>

        {/* Selected Corpus Representation Breakdown Cards */}
        {selectedCorpus !== "All" && currentV3 && (
          <div className="grid grid-cols-1 sm:grid-cols-4 gap-3 pt-2">
            <div className="panel-soft p-3 rounded-xl border border-teal-200 bg-teal-50/40">
              <span className="text-[10px] font-mono uppercase tracking-wider text-teal-700 font-semibold block mb-1">
                Inline Symbols (≤12B)
              </span>
              <span className="font-mono text-lg font-bold text-teal-900">
                {currentV3.count_inline.toLocaleString()}
              </span>
              <span className="text-[10px] font-mono text-teal-700 block mt-0.5">
                {currentV3.unique_names > 0 ? `${((currentV3.count_inline / currentV3.unique_names) * 100).toFixed(1)}% of names` : ""}
              </span>
            </div>

            <div className="panel-soft p-3 rounded-xl border border-indigo-200 bg-indigo-50/40">
              <span className="text-[10px] font-mono uppercase tracking-wider text-indigo-700 font-semibold block mb-1">
                Interned Symbols
              </span>
              <span className="font-mono text-lg font-bold text-indigo-900">
                {currentV3.count_interned.toLocaleString()}
              </span>
              <span className="text-[10px] font-mono text-indigo-700 block mt-0.5">
                {currentV3.unique_names > 0 ? `${((currentV3.count_interned / currentV3.unique_names) * 100).toFixed(1)}% of names` : ""}
              </span>
            </div>

            <div className="panel-soft p-3 rounded-xl border border-amber-200 bg-amber-50/40">
              <span className="text-[10px] font-mono uppercase tracking-wider text-amber-700 font-semibold block mb-1">
                Compressed (Front-Coded)
              </span>
              <span className="font-mono text-lg font-bold text-amber-900">
                {currentV3.count_compressed.toLocaleString()}
              </span>
              <span className="text-[10px] font-mono text-amber-700 block mt-0.5">
                {currentV3.unique_names > 0 ? `${((currentV3.count_compressed / currentV3.unique_names) * 100).toFixed(1)}% of names` : ""}
              </span>
            </div>

            <div className="panel-soft p-3 rounded-xl border border-slate-200 bg-slate-50/60">
              <span className="text-[10px] font-mono uppercase tracking-wider text-slate-600 font-semibold block mb-1">
                Mean Reconstruct Depth
              </span>
              <span className="font-mono text-lg font-bold text-slate-800">
                {currentV3.mean_reconstruction_depth ? `${currentV3.mean_reconstruction_depth.toFixed(2)} steps` : "—"}
              </span>
              <span className="text-[10px] font-mono text-slate-500 block mt-0.5">
                {currentV3.reconstruction_count ? `${currentV3.reconstruction_count.toLocaleString()} decodes` : "0 decodes"}
              </span>
            </div>
          </div>
        )}
      </div>

      {/* Table */}
      <div className="panel p-6 rounded-2xl border border-slate-200 overflow-x-auto">
        <table className="w-full text-left border-collapse text-xs font-mono">
          <thead>
            <tr className="bg-slate-50 text-slate-500 border-b border-slate-200 text-[11px] uppercase tracking-wider">
              <th className="py-3 px-4 font-semibold">Corpus</th>
              <th className="py-3 px-4 font-semibold">Architecture</th>
              <th className="py-3 px-4 text-right font-semibold">Declarations</th>
              <th className="py-3 px-4 text-right font-semibold">Unique Names</th>
              <th className="py-3 px-4 text-right font-semibold text-slate-900">Final Heap</th>
              <th className="py-3 px-4 text-right font-semibold text-teal-700">Lookup p50</th>
              <th className="py-3 px-4 text-right font-semibold text-amber-700">Lookup p95</th>
              <th className="py-3 px-4 text-right font-semibold">Inline / Int / Comp</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-slate-100">
            {filteredData.slice(0, 100).map((r, i) => {
              const isV3 = r.implementation === "SymTabV3";
              const isConv = r.implementation === "Conventional";
              const isV4 = r.implementation === "SymTabV4";
              return (
                <tr
                  key={i}
                  className={`hover:bg-slate-50/80 transition-colors ${
                    isV3 ? "bg-teal-50/20 font-semibold text-slate-900" : "text-slate-700"
                  }`}
                >
                  <td className="py-3 px-4 font-bold">{r.corpus}</td>
                  <td className="py-3 px-4">
                    <span className={`px-2 py-0.5 rounded text-[10px] font-semibold border ${
                      isV3 ? "bg-teal-50 text-teal-700 border-teal-200" :
                      isConv ? "bg-slate-100 text-slate-700 border-slate-200" :
                      isV4 ? "bg-rose-50 text-rose-700 border-rose-200" :
                      "bg-indigo-50 text-indigo-700 border-indigo-200"
                    }`}>
                      {r.implementation}
                    </span>
                  </td>
                  <td className="py-3 px-4 text-right">{r.declarations.toLocaleString()}</td>
                  <td className="py-3 px-4 text-right">{r.unique_names.toLocaleString()}</td>
                  <td className="py-3 px-4 text-right font-bold">{formatBytes(r.measured_final_heap_bytes)}</td>
                  <td className="py-3 px-4 text-right text-teal-800">{r.lookup_p50_us ? `${r.lookup_p50_us.toFixed(3)} µs` : "—"}</td>
                  <td className="py-3 px-4 text-right text-amber-800">{r.lookup_p95_us ? `${r.lookup_p95_us.toFixed(3)} µs` : "—"}</td>
                  <td className="py-3 px-4 text-right text-[10px] text-slate-500">
                    {r.count_inline || r.count_interned || r.count_compressed
                      ? `${r.count_inline} / ${r.count_interned} / ${r.count_compressed}`
                      : "—"}
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>
    </div>
  );
}
