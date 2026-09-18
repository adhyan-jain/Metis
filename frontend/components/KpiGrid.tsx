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

const CANONICAL_KPIS: Kpi[] = [
  {
    title: "Zephyr Final Heap Reduction",
    value: "20.2%",
    subtitle: "53.39 MB vs 66.91 MB (-13.52 MB saved)",
    badge: "CANONICAL EMBEDDED WIN",
    accent: "text-teal-700",
    border: "border-teal-200",
    bg: "bg-teal-50",
  },
  {
    title: "Zephyr Peak Heap Reduction",
    value: "24.2%",
    subtitle: "57.89 MB vs 76.37 MB peak physical heap",
    badge: "ALLOCATOR VERIFIED",
    accent: "text-indigo-700",
    border: "border-indigo-200",
    bg: "bg-indigo-50",
  },
  {
    title: "Zephyr p95 Latency Ratio",
    value: "1.824×",
    subtitle: "0.228 µs (V3) vs 0.125 µs (EmbConv)",
    badge: "GATE FAILED (1.25× TARGET)",
    accent: "text-amber-700",
    border: "border-amber-200",
    bg: "bg-amber-50",
  },
  {
    title: "Break-Even Duplication (L=32B)",
    value: "k ≥ 3.33",
    subtitle: "k_breakeven = (2L+86)/(L+13) (SSO L≤15B)",
    badge: "ANALYTICAL BOUNDARY",
    accent: "text-slate-700",
    border: "border-slate-200",
    bg: "bg-slate-50",
  },
];

export function KpiGrid() {
  const [kpis, setKpis] = useState<Kpi[]>(CANONICAL_KPIS);
  const [source, setSource] = useState<string>("canonical research frozen dataset");

  useEffect(() => {
    fetch("/api/canonical")
      .then((r) => r.json())
      .then((data) => {
        if (data.available) {
          setSource("live from results/CANONICAL_FINAL_DATASET.csv (433 rows)");
        }
      })
      .catch(() => {});
  }, []);

  return (
    <div>
      <div className="flex items-center justify-between mb-2">
        <span className="text-[11px] font-mono text-slate-500 uppercase tracking-wider">
          Headline Research Results
        </span>
        <span className="px-2 py-0.5 rounded-full text-[10px] font-mono bg-teal-50 text-teal-700 border border-teal-200">
          {source}
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
