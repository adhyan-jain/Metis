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
  { href: "/benchmarks", title: "METIS-X & Embedded Benchmarks", desc: "Physical allocator-measured heap and multi-repetition lookup latencies for METIS-X.", icon: "▥", pastel: "pastel-peach" },
  { href: "/workload", title: "Workload Explorer & Diagnostics", desc: "Corpus name lengths, inline vs fallback slot splits, and probe distances.", icon: "▤", pastel: "pastel-pink" },
  { href: "/memory", title: "Memory & Slot Diagnosis", desc: "32B cache-aligned MetisXSlot layout, Robin Hood displacement, and scope recycling.", icon: "◫", pastel: "pastel-mint" },
  { href: "/pareto", title: "Joint Wins & Ablation", desc: "Component ablation waterfall (A0->A5) and joint RAM + p95 latency win boundaries.", icon: "◈", pastel: "pastel-violet" },
  { href: "/compiler", title: "Compiler Simulator", desc: "Simulate symbol declarations through 32B slots, inline storage, and scope recycling.", icon: "⌘", pastel: "pastel-violet" },
  { href: "/scopes", title: "Scope Lifecycle", desc: "LIFO scope slot recycling tree and immediate physical memory deallocation.", icon: "◱", pastel: "pastel-sky" },
  { href: "/research", title: "Research & Paper", desc: "Two-phase research narrative, IEEE manuscript PDF, ablation, and audit log.", icon: "◎", pastel: "pastel-mint" },
  { href: "/docs", title: "Reproducibility Guide", desc: "Instructions for running ./reproduce_all.sh phase2 and verifying all results.", icon: "◧", pastel: "pastel-lemon" },
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
              "radial-gradient(500px circle at -5% -20%, rgba(16,185,129,0.16), transparent 55%), radial-gradient(500px circle at 105% -20%, rgba(99,102,241,0.20), transparent 55%), radial-gradient(500px circle at 60% 130%, rgba(14,165,233,0.16), transparent 55%)",
          }}
        />
        <div className="flex flex-col lg:flex-row lg:items-start justify-between gap-6">
          <div className="max-w-3xl">
            <div className="flex items-center gap-2 mb-3">
              <span className={`inline-flex items-center gap-1.5 px-2.5 py-1 rounded-full text-[11px] font-mono font-semibold border ${
                status?.prototypeOperational
                  ? "bg-emerald-50 text-emerald-700 border-emerald-200"
                  : "bg-slate-50 text-slate-500 border-slate-200"
              }`}>
                <span className={`h-1.5 w-1.5 rounded-full ${status?.prototypeOperational ? "bg-emerald-500" : "bg-slate-400"}`} />
                {statusError ? "Status unavailable" : status ? (status.prototypeOperational ? "METIS-X Phase II Validated" : "Not detected") : "Checking…"}
              </span>
              <span className="px-2.5 py-1 rounded-full text-[11px] font-mono font-semibold bg-indigo-50 text-indigo-700 border border-indigo-200">
                IEEE Paper & Artifact Ready
              </span>
            </div>
            <h1 className="text-2xl lg:text-3xl font-bold text-slate-900 tracking-tight">
              METIS-X
            </h1>
            <p className="text-sm font-medium text-slate-700 mt-1 italic">
              A Cache-Conscious Symbol Table Architecture for Resource-Constrained Embedded Toolchains
            </p>
            <p className="text-sm text-slate-600 mt-2 leading-relaxed">
              METIS-X resolves the tail-latency trade-off discovered in adaptive compression studies (Phase I) by eliminating dictionary reconstruction from the hot lookup path. It combines 32-byte cache-aligned slots, inline identifier storage ($\le 12$B), Robin Hood displacement, and LIFO scope slot recycling.
            </p>
          </div>

          <div className="grid grid-cols-3 gap-3 shrink-0 w-full lg:w-auto">
            <StatCell label="Joint Wins" value="Zephyr & ESP" />
            <StatCell label="Lookup Allocs" value="0 Proved" />
            <StatCell label="Zephyr RAM" value="-38.2%" />
          </div>
        </div>

        <div className="mt-5 flex flex-wrap gap-2 items-center">
          <RepBadge color="emerald" label="32B Flat Slot" />
          <RepBadge color="indigo" label="Inline Storage (≤12B)" />
          <RepBadge color="sky" label="Robin Hood Hashing" />
          <RepBadge color="teal" label="LIFO Scope Recycling" />
          <span className="mx-2 h-5 w-px bg-slate-200 self-center" />
          <span className="text-xs font-mono text-slate-500">
            Source: <code className="bg-slate-100 px-1.5 py-0.5 rounded text-slate-800">results/METIS_X_CANONICAL_DATASET.csv</code>
          </span>
        </div>
      </div>

      {/* KPI Grid */}
      <KpiGrid />

      {/* Central Research Question & Findings Banner */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <div className="panel p-6 rounded-2xl border border-slate-200 space-y-3">
          <h2 className="text-sm font-mono uppercase tracking-wider text-slate-500 font-semibold">
            Primary Research Question (Phase II)
          </h2>
          <blockquote className="text-sm text-slate-800 font-medium italic border-l-4 border-emerald-500 pl-4 py-1 bg-emerald-50/50 rounded-r-lg">
            “Can a cache-conscious flat open-addressing symbol table eliminate dictionary reconstruction overhead to achieve simultaneous physical heap and p95 latency reductions over embedded conventional baselines?”
          </blockquote>
          <p className="text-xs text-slate-600 leading-relaxed">
            Phase I demonstrated that front-coded block compression reduced physical heap by 20.2% on Zephyr but caused a 1.824× p95 latency regression. METIS-X eliminates decompression entirely from lookups, achieving simultaneous RAM and p95 speedups.
          </p>
        </div>

        <div className="panel p-6 rounded-2xl border border-slate-200 space-y-3">
          <h2 className="text-sm font-mono uppercase tracking-wider text-slate-500 font-semibold">
            Validated Research Claims
          </h2>
          <ul className="space-y-2 text-xs text-slate-700">
            <li className="flex items-start gap-2">
              <span className="text-emerald-500 font-bold">✓</span>
              <span><strong>Zephyr RTOS Win:</strong> −38.2% physical final heap (25.79 MB vs 41.74 MB) and −15.2% p95 latency (0.084 µs vs 0.119 µs).</span>
            </li>
            <li className="flex items-start gap-2">
              <span className="text-emerald-500 font-bold">✓</span>
              <span><strong>ESP-IDF Win:</strong> −17.5% physical final heap (35.20 MB vs 42.65 MB) and −69.6% p95 latency (0.069 µs vs 0.227 µs).</span>
            </li>
            <li className="flex items-start gap-2">
              <span className="text-emerald-500 font-bold">✓</span>
              <span><strong>Zero Hot-Path Allocations:</strong> 0 allocations across 2,863,367 lookups in real event streams.</span>
            </li>
            <li className="flex items-start gap-2">
              <span className="text-amber-500 font-bold">⚠</span>
              <span><strong>Arduino Trade-off:</strong> +2.6% heap due to fallback heap string allocations for names exceeding 12B.</span>
            </li>
          </ul>
        </div>
      </div>

      {/* Navigation Sections */}
      <div>
        <h2 className="text-sm font-mono uppercase tracking-wider text-slate-500 font-semibold mb-4">
          Research Explorer Modules
        </h2>
        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
          {SECTIONS.map((sec, idx) => (
            <Link key={idx} href={sec.href} className="panel card-hover p-5 rounded-2xl border border-slate-200 flex flex-col justify-between group">
              <div>
                <div className="flex items-center justify-between mb-3">
                  <span className="text-xl font-mono text-slate-400 group-hover:text-emerald-600 transition-colors">
                    {sec.icon}
                  </span>
                  <span className="text-xs font-mono text-slate-400 group-hover:translate-x-0.5 transition-transform">
                    →
                  </span>
                </div>
                <h3 className="text-sm font-bold text-slate-800 group-hover:text-emerald-700 transition-colors mb-1">
                  {sec.title}
                </h3>
                <p className="text-xs text-slate-600 leading-relaxed">
                  {sec.desc}
                </p>
              </div>
            </Link>
          ))}
        </div>
      </div>
    </div>
  );
}

function StatCell({ label, value }: { label: string; value: string }) {
  return (
    <div className="bg-slate-50/80 border border-slate-200/80 rounded-xl p-3 text-center">
      <div className="text-[10px] font-mono text-slate-500 uppercase tracking-wider">{label}</div>
      <div className="text-sm font-mono font-bold text-slate-800 mt-0.5">{value}</div>
    </div>
  );
}

function RepBadge({ color, label }: { color: string; label: string }) {
  const colorMap: Record<string, string> = {
    emerald: "bg-emerald-50 text-emerald-700 border-emerald-200",
    indigo: "bg-indigo-50 text-indigo-700 border-indigo-200",
    sky: "bg-sky-50 text-sky-700 border-sky-200",
    teal: "bg-teal-50 text-teal-700 border-teal-200",
  };
  return (
    <span className={`px-2.5 py-1 rounded-full text-[11px] font-mono font-semibold border ${colorMap[color] || "bg-slate-50 text-slate-700 border-slate-200"}`}>
      {label}
    </span>
  );
}
