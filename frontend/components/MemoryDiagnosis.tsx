"use client";

import React from "react";
import { StackedBarChart, StackedBarGroup } from "./charts/StackedBarChart";

// Sourced from docs/v2_real_world_memory_diagnosis.md -- the authoritative
// architectural investigation into why SymTabV2 can use MORE physical heap
// than Conventional on real-world code. Not derived from a live CSV: the
// 6-corpus diagnostic dataset behind this doc (FreeRTOS, Arduino, Lua,
// CPython, ESP-IDF, Zephyr) lives only in the doc itself, so these are
// static citations, clearly labeled as such below.
const RATIO_ROWS = [
  { corpus: "FreeRTOS", conventional_mb: 0.72, interned_mb: 2.06, v2_mb: 2.51, ratio: 3.5 },
  { corpus: "Arduino", conventional_mb: 0.72, interned_mb: 1.94, v2_mb: 1.87, ratio: 2.58 },
  { corpus: "Lua", conventional_mb: 0.31, interned_mb: 0.61, v2_mb: 0.62, ratio: 2.03 },
  { corpus: "CPython", conventional_mb: 3.75, interned_mb: 14.62, v2_mb: 15.25, ratio: 4.06 },
  { corpus: "ESP-IDF", conventional_mb: 18.22, interned_mb: 44.31, v2_mb: 63.15, ratio: 3.47 },
  { corpus: "Zephyr", conventional_mb: 5.43, interned_mb: 34.56, v2_mb: 35.41, ratio: 6.52 },
];

const COMPONENT_BREAKDOWN: { component: string; pctRange: string }[] = [
  { component: "EverSeen Set (everSeenRep_) — never pruned on scope exit", pctRange: "10.6% – 49.3%" },
  { component: "Pool Lookup Map (poolLookup_) — never pruned on scope exit", pctRange: "9.2% – 20.6%" },
  { component: "PackedEntry Storage (56B/slot, fixed regardless of representation)", pctRange: "7.8% – 27.7%" },
  { component: "String Pool (pool_)", pctRange: "6.0% – 15.8%" },
  { component: "ScopeIndex Open-Addressing Table (16B/slot, 0.70 load factor)", pctRange: "2.1% – 25.1%" },
  { component: "Parallel Metadata Vectors (declIdOf_, poolIndexOf_, compressedRefOf_)", pctRange: "3.5% – 10.1%" },
  { component: "Block Compression Pool", pctRange: "2.9% – 7.6%" },
];

export function MemoryDiagnosis() {
  const groups: StackedBarGroup[] = RATIO_ROWS.map((r) => ({
    label: r.corpus,
    segments: [
      { key: "conv", label: "Conventional", value: r.conventional_mb, color: "#94a3b8" },
      { key: "interned", label: "Interned", value: r.interned_mb, color: "#818cf8" },
      { key: "v2", label: "SymTabV2", value: r.v2_mb, color: "#14b8a6" },
    ],
  }));

  return (
    <div className="panel p-6 rounded-2xl space-y-5 border-amber-200 bg-amber-50/20">
      <div>
        <div className="flex items-center justify-between">
          <h3 className="font-mono text-base font-semibold text-slate-900">
            Architectural Memory Diagnosis — Why SymTab V2 Can Lose to Conventional
          </h3>
          <span className="px-2 py-0.5 rounded text-[10px] font-mono font-bold bg-amber-100 text-amber-800 border border-amber-300 shrink-0">
            docs/v2_real_world_memory_diagnosis.md
          </span>
        </div>
        <p className="text-xs text-slate-600 mt-2 leading-relaxed">
          On real-world C/C++ codebases, SymTab V2 requires <strong>1.87×–6.52× more physical heap</strong> than
          Conventional. This is the opposite of the synthetic-workload result and is not hidden here: it is the
          subject of an active architectural redesign (see the corpus ratios below, cited from the diagnosis document).
        </p>
      </div>

      <div>
        <h4 className="text-[11px] font-mono font-semibold text-slate-500 uppercase tracking-wider mb-3">
          Physical Peak Heap by Corpus (relative bar widths, MB)
        </h4>
        <StackedBarChart groups={groups} />
      </div>

      <div className="overflow-x-auto rounded-xl border border-slate-200">
        <table className="w-full text-left border-collapse text-xs font-mono">
          <thead>
            <tr className="bg-slate-50 text-slate-500 border-b border-slate-200 text-[11px] uppercase tracking-wider">
              <th className="py-2.5 px-4 font-semibold">Corpus</th>
              <th className="py-2.5 px-4 text-right font-semibold">Conventional</th>
              <th className="py-2.5 px-4 text-right font-semibold">Interned</th>
              <th className="py-2.5 px-4 text-right font-semibold">SymTabV2</th>
              <th className="py-2.5 px-4 text-right font-semibold text-amber-800">V2 / Conv Ratio</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-slate-100">
            {RATIO_ROWS.map((r) => (
              <tr key={r.corpus} className="text-slate-700">
                <td className="py-2.5 px-4 font-bold">{r.corpus}</td>
                <td className="py-2.5 px-4 text-right">{r.conventional_mb.toFixed(2)} MB</td>
                <td className="py-2.5 px-4 text-right">{r.interned_mb.toFixed(2)} MB</td>
                <td className="py-2.5 px-4 text-right font-semibold text-teal-700">{r.v2_mb.toFixed(2)} MB</td>
                <td className="py-2.5 px-4 text-right font-bold text-amber-800">{r.ratio.toFixed(2)}×</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      <div>
        <h4 className="text-[11px] font-mono font-semibold text-slate-500 uppercase tracking-wider mb-2">
          Component Contribution to V2 Peak Heap (% range across corpora)
        </h4>
        <ul className="text-xs text-slate-600 space-y-1.5">
          {COMPONENT_BREAKDOWN.map((c) => (
            <li key={c.component} className="flex items-center justify-between gap-4 py-1 border-b border-slate-100 last:border-0">
              <span>{c.component}</span>
              <span className="font-mono text-slate-500 shrink-0">{c.pctRange}</span>
            </li>
          ))}
        </ul>
      </div>

      <div className="bg-white border border-slate-200 rounded-xl p-4 text-xs text-slate-600 leading-relaxed space-y-2">
        <p>
          <strong>Root cause:</strong> <code className="text-[11px]">everSeenRep_</code> and{" "}
          <code className="text-[11px]">poolLookup_</code> retain every unique identifier ever seen for the lifetime
          of the program and are never pruned on scope exit, unlike Conventional&apos;s{" "}
          <code className="text-[11px]">scopeMaps_.pop_back()</code>, which immediately returns memory to the OS.
          Vector capacities in V2 are also sized to peak slot count and never shrunk.
        </p>
        <p>
          This diagnosis is the basis for the ongoing V2 architectural redesign work; figures above will update once
          that redesign lands new measured results in <code className="text-[11px]">results/</code>.
        </p>
      </div>
    </div>
  );
}
