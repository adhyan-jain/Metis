"use client";

import React, { useEffect, useState } from "react";
import Link from "next/link";
import { KpiGrid } from "../components/KpiGrid";

interface StatusResponse {
  prototypeOperational: boolean;
  compilerStandard: string;
  memoryModel: string;
  representations: string[];
  backend: { analyzeBinaryAvailable: boolean };
  data: { benchmarkResultsAvailable: boolean; ablationResultsAvailable: boolean };
}

const SECTIONS = [
  { href: "/compiler", title: "Compiler", desc: "Type source code, run it through the real C++ engine, see the representation decisions.", icon: "⌘", pastel: "pastel-violet" },
  { href: "/symbols", title: "Symbol Table", desc: "Every symbol from the last compile: scope, type, representation, memory, and why.", icon: "▤", pastel: "pastel-pink" },
  { href: "/scopes", title: "Scopes", desc: "Scope tree from the last compile — enter/exit, symbol counts, memory reclaimed.", icon: "◱", pastel: "pastel-sky" },
  { href: "/memory", title: "Memory", desc: "Budget, pressure, and per-dataset memory breakdown from measured benchmark data.", icon: "◫", pastel: "pastel-mint" },
  { href: "/benchmarks", title: "Benchmarks", desc: "8 datasets × 3 implementations — memory and latency, measured, not simulated.", icon: "▥", pastel: "pastel-peach" },
  { href: "/experiments", title: "Experiments", desc: "Ablation study: which mechanism is responsible for how much of the result.", icon: "◧", pastel: "pastel-lemon" },
  { href: "/architecture", title: "Architecture", desc: "How a declaration flows from source text to a stored representation.", icon: "◈", pastel: "pastel-violet" },
  { href: "/research", title: "Research", desc: "Problem, research gap, proposed architecture, limitations — stated plainly.", icon: "◎", pastel: "pastel-mint" },
];

export default function DashboardHome() {
  const [status, setStatus] = useState<StatusResponse | null>(null);
  const [statusError, setStatusError] = useState(false);

  useEffect(() => {
    fetch("/api/status")
      .then((r) => r.json())
      .then(setStatus)
      .catch(() => setStatusError(true));
  }, []);

  return (
    <div className="space-y-8">
      {/* Hero */}
      <div className="panel rounded-2xl p-6 lg:p-8 relative overflow-hidden">
        <div
          className="absolute inset-0 -z-10"
          style={{
            background:
              "radial-gradient(500px circle at -5% -20%, rgba(244,114,182,0.16), transparent 55%), radial-gradient(500px circle at 105% -20%, rgba(129,140,248,0.20), transparent 55%), radial-gradient(500px circle at 60% 130%, rgba(45,212,191,0.16), transparent 55%)",
          }}
        />
        <div className="flex flex-col lg:flex-row lg:items-start justify-between gap-6">
          <div className="max-w-2xl">
            <div className="flex items-center gap-2 mb-3">
              <span className={`inline-flex items-center gap-1.5 px-2.5 py-1 rounded-full text-[11px] font-mono font-semibold border ${
                status?.prototypeOperational
                  ? "bg-teal-50 text-teal-700 border-teal-200"
                  : "bg-slate-50 text-slate-500 border-slate-200"
              }`}>
                <span className={`h-1.5 w-1.5 rounded-full ${status?.prototypeOperational ? "bg-teal-500" : "bg-slate-400"}`} />
                {statusError ? "Status unavailable" : status ? (status.prototypeOperational ? "Prototype Operational" : "Not detected") : "Checking…"}
              </span>
            </div>
            <h1 className="text-2xl lg:text-3xl font-bold text-slate-900 tracking-tight">
              Metis
            </h1>
            <p className="text-sm text-slate-600 mt-2 leading-relaxed">
              The Memory-Constrained Compressed Symbol Table for Embedded Compilers — an
              adaptive, scope-aware symbol representation system for memory-bounded embedded
              compilation. This dashboard runs against the actual C++ prototype in this
              repository: real benchmark CSVs, and a real backend call to the compiled engine
              for the Compiler playground.
            </p>
          </div>

          <div className="grid grid-cols-3 gap-3 shrink-0 w-full lg:w-auto">
            <StatCell label="Compiler" value={status?.compilerStandard ?? "—"} />
            <StatCell label="Memory Model" value="Tracked" title={status?.memoryModel} />
            <StatCell label="Representations" value={String(status?.representations.length ?? 3)} />
          </div>
        </div>

        <div className="mt-5 flex flex-wrap gap-2">
          <RepBadge color="teal" label="INLINE" />
          <RepBadge color="indigo" label="INTERNED" />
          <RepBadge color="amber" label="COMPRESSED" />
          <span className="mx-2 h-5 w-px bg-slate-200 self-center" />
          <BackendPill ok={status?.backend.analyzeBinaryAvailable} label="analyze.exe" />
          <BackendPill ok={status?.data.benchmarkResultsAvailable} label="benchmark_results.csv" />
          <BackendPill ok={status?.data.ablationResultsAvailable} label="ablation_results.csv" />
        </div>
      </div>

      {/* KPI Grid */}
      <KpiGrid />

      {/* Section cards */}
      <div>
        <h2 className="text-sm font-semibold text-slate-900 mb-3">Explore</h2>
        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
          {SECTIONS.map((s) => (
            <Link
              key={s.href}
              href={s.href}
              className="panel card-hover rounded-xl p-4 flex flex-col gap-3"
            >
              <span className={`h-9 w-9 rounded-lg flex items-center justify-center text-base font-semibold ${s.pastel}`}>
                {s.icon}
              </span>
              <span className="text-sm font-semibold text-slate-900">{s.title}</span>
              <span className="text-xs text-slate-500 leading-relaxed">{s.desc}</span>
            </Link>
          ))}
        </div>
      </div>
    </div>
  );
}

function StatCell({ label, value, title }: { label: string; value: string; title?: string }) {
  return (
    <div className="panel-soft rounded-xl px-3 py-2.5 text-center" title={title}>
      <div className="text-[10px] font-mono uppercase tracking-wider text-slate-400 mb-0.5">{label}</div>
      <div className="text-sm font-mono font-bold text-slate-800 truncate">{value}</div>
    </div>
  );
}

function RepBadge({ color, label }: { color: "teal" | "indigo" | "amber"; label: string }) {
  const styles = {
    teal: "bg-teal-50 text-teal-700 border-teal-200",
    indigo: "bg-indigo-50 text-indigo-700 border-indigo-200",
    amber: "bg-amber-50 text-amber-700 border-amber-200",
  }[color];
  return (
    <span className={`px-2.5 py-1 rounded-md text-[11px] font-mono font-semibold border ${styles}`}>
      ● {label}
    </span>
  );
}

function BackendPill({ ok, label }: { ok?: boolean; label: string }) {
  return (
    <span className={`px-2.5 py-1 rounded-md text-[11px] font-mono border flex items-center gap-1.5 ${
      ok ? "bg-slate-50 text-slate-600 border-slate-200" : "bg-red-50 text-red-600 border-red-200"
    }`}>
      <span className={`h-1.5 w-1.5 rounded-full ${ok ? "bg-teal-500" : "bg-red-400"}`} />
      {label}
    </span>
  );
}
