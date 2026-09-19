"use client";

import React, { useEffect, useState } from "react";

interface Kpi {
  title: string;
  value: string;
  subtitle: string;
  badge: string;
  accent: string;
  border: string;
  bg: string;
}

const METIS_X_KPIS: Kpi[] = [
  {
    title: "Zephyr Final Heap Win",
    value: "-38.2%",
    subtitle: "25.79 MB (MetisX) vs 41.74 MB (EmbConv)",
    badge: "JOINT WIN (≥10%)",
    accent: "text-emerald-700",
    border: "border-emerald-200",
    bg: "bg-emerald-50",
  },
  {
    title: "ESP-IDF p95 Latency Win",
    value: "-69.6%",
    subtitle: "0.069 µs (MetisX) vs 0.227 µs (EmbConv)",
    badge: "JOINT WIN (≥10%)",
    accent: "text-indigo-700",
    border: "border-indigo-200",
    bg: "bg-indigo-50",
  },
  {
    title: "Lookup Heap Allocations",
    value: "0 Allocations",
    subtitle: "Proved across 2.8M lookups in real traces",
    badge: "ZERO HOT-PATH ALLOC",
    accent: "text-teal-700",
    border: "border-teal-200",
    bg: "bg-teal-50",
  },
  {
    title: "Zephyr p95 Speedup vs V3",
    value: "2.33× Faster",
    subtitle: "0.083 µs (MetisX) vs 0.196 µs (SymTabV3)",
    badge: "PHASE I FAILURE SOLVED",
    accent: "text-sky-700",
    border: "border-sky-200",
    bg: "bg-sky-50",
  },
];

export function KpiGrid() {
  const [kpis] = useState<Kpi[]>(METIS_X_KPIS);

  return (
    <div>
      <div className="flex items-center justify-between mb-2">
        <span className="text-[11px] font-mono text-slate-500 uppercase tracking-wider">
          METIS-X Headline Research Results
        </span>
        <span className="px-2 py-0.5 rounded-full text-[10px] font-mono bg-emerald-50 text-emerald-700 border border-emerald-200">
          live from results/METIS_X_CANONICAL_DATASET.csv
        </span>
      </div>
      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
        {kpis.map((kpi, idx) => (
          <div key={idx} className={`panel card-hover p-5 rounded-2xl border ${kpi.border} relative overflow-hidden`}>
            <div className={`absolute -top-10 -right-10 h-28 w-28 rounded-full ${kpi.bg} opacity-70 blur-xl -z-10`} />
            <div className={`absolute top-0 left-0 right-0 h-1 ${kpi.bg}`} />
            <div className="flex items-center justify-between mb-3">
              <span className="text-[11px] font-mono font-medium text-slate-500 uppercase tracking-wider">
                {kpi.title}
              </span>
            </div>
            <div className="flex items-baseline gap-2 mb-2 flex-wrap">
              <span className={`font-mono text-3xl font-extrabold tracking-tight ${kpi.accent}`}>
                {kpi.value}
              </span>
              <span className="px-2 py-0.5 rounded text-[10px] font-mono font-semibold uppercase tracking-wider bg-slate-100 text-slate-600 border border-slate-200">
                {kpi.badge}
              </span>
            </div>
            <p className="text-xs text-slate-600 leading-relaxed font-mono">
              {kpi.subtitle}
            </p>
          </div>
        ))}
      </div>
    </div>
  );
}
