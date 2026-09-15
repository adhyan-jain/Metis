"use client";

import React, { useEffect, useState } from "react";
import { ablationRows as fallbackRows, AblationRow } from "../lib/benchmark-data";

function getVariantLabel(v: string) {
  switch (v) {
    case "FullV2":
    case "BudgetSym-Full":
      return { title: "SymTab V2 — Full Policy", desc: "All mechanisms enabled (scope slot recycling, block compression, adaptive representation, 1-byte fingerprints)", isFull: true };
    case "V2-NoScopeReclamation":
    case "BudgetSym-NoScope":
      return { title: "Ablation: No Scope Reclamation", desc: "Scope frames entered but never popped — scope slot recycling disabled (+55.10% memory impact)", isFull: false };
    case "V2-NoAdaptiveRepresentation":
    case "BudgetSym-NoAdaptiveSelection":
      return { title: "Ablation: No Adaptive Representation", desc: "decide() policy bypassed — every symbol forced to INTERNED pool (+59.28% memory impact)", isFull: false };
    case "V2-NoBlockCompression":
      return { title: "Ablation: No Block Compression", desc: "Front-coded prefix compression disabled — entries stored uncompressed (+44.12% memory impact)", isFull: false };
    case "V2-NoFingerprints":
      return { title: "Ablation: No Hash Fingerprints", desc: "1-byte hash fingerprints disabled — directory lookup requires full string comparison (+1.52% cold lookup latency impact)", isFull: false };
    case "V2-NoHotColdPromotion":
    case "BudgetSym-NoAccessFrequency":
      return { title: "Ablation: No Hot/Cold Promotion", desc: "Runtime promotion threshold unreachable — COMPRESSED entries never promoted to INTERNED on access", isFull: false };
    default:
      return { title: v, desc: "", isFull: false };
  }
}

export function AblationView() {
  const [rows, setRows] = useState<AblationRow[]>(fallbackRows);
  const [source, setSource] = useState<"live" | "fallback">("fallback");

  useEffect(() => {
    fetch("/api/experiments")
      .then((r) => r.json())
      .then((data) => {
        if (data.available && Array.isArray(data.rows) && data.rows.length > 0) {
          setRows(data.rows);
          setSource("live");
        }
      })
      .catch(() => {});
  }, []);

  return (
    <div className="space-y-6">
      {/* Overview Banner */}
      <div className="panel p-6 rounded-2xl space-y-2">
        <div className="flex items-center justify-between">
          <h2 className="text-xl font-mono font-bold text-slate-900">
            Ablation Study: Architectural Component Contributions
          </h2>
          <span className={`px-2 py-0.5 rounded-full text-[10px] font-mono border ${source === "live" ? "bg-teal-50 text-teal-700 border-teal-200" : "bg-slate-50 text-slate-500 border-slate-200"}`}>
            {source === "live" ? "results/ablation.csv" : "authoritative results"}
          </span>
        </div>
        <p className="text-xs text-slate-500 max-w-3xl leading-relaxed">
          To verify that every architectural mechanism in SymTab V2 earns its place, each component was systematically disabled in isolation on the same fixed workload (N=2,000). Below are the empirical impacts on physical heap memory, insert/lookup latency, and memory delta vs Full V2.
        </p>
      </div>

      {/* Grid of Ablation Cards */}
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
        {rows.map((row) => {
          const info = getVariantLabel(row.variant);
          const memDelta = Number(row.memory_delta_vs_full_v2_pct);
          const latDelta = Number(row.cold_latency_delta_vs_full_v2_pct);
          return (
            <div
              key={row.variant}
              className={`panel card-hover p-5 rounded-2xl ${info.isFull ? "border-teal-300 ring-1 ring-teal-100 bg-teal-50/20" : ""}`}
            >
              <div className="flex items-center justify-between mb-3">
                <span className={`text-[10px] font-mono font-bold uppercase tracking-wider px-2 py-0.5 rounded ${
                  info.isFull ? "bg-teal-50 text-teal-700 border border-teal-200" : "bg-slate-100 text-slate-500 border border-slate-200"
                }`}>
                  {info.isFull ? "Full V2 Baseline" : "Ablation Variant"}
                </span>
                <span className="font-mono text-xs text-slate-400">{row.symbols} symbols</span>
              </div>

              <h3 className="font-mono text-sm font-bold text-slate-900 mb-1">{info.title}</h3>
              <p className="text-xs text-slate-500 mb-4 min-h-[40px]">{info.desc}</p>

              <div className="space-y-2 border-t border-slate-100 pt-3 text-xs font-mono">
                <Row label="Heap Memory" value={`${(Number(row.memory_bytes) / 1024).toFixed(1)} kB`} accent="text-teal-700" />
                <Row label="Memory / Symbol" value={`${Number(row.memory_per_symbol).toFixed(1)} B`} />
                <Row label="Memory Impact vs V2" value={memDelta === 0 ? "Baseline (0%)" : `+${memDelta.toFixed(1)}%`} accent={memDelta > 0 ? "text-rose-700" : "text-slate-600"} />
                <Row label="Insert Latency" value={`${Number(row.insert_us).toFixed(3)} µs`} />
                <Row label="Cold Lookup Latency" value={`${Number(row.lookup_us).toFixed(3)} µs`} />
                {latDelta !== 0 && (
                  <Row label="Cold Latency Impact" value={`${latDelta > 0 ? "+" : ""}${latDelta.toFixed(1)}%`} accent={latDelta > 0 ? "text-amber-700" : "text-emerald-700"} />
                )}
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}

function Row({ label, value, accent }: { label: string; value: string; accent?: string }) {
  return (
    <div className="flex justify-between items-center text-slate-600">
      <span className="text-slate-400">{label}</span>
      <span className={`font-bold ${accent ?? "text-slate-700"}`}>{value}</span>
    </div>
  );
}
