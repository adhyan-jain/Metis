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
  { href: "/benchmarks", title: "Embedded & Host Benchmarks", desc: "Physical allocator-measured heap and multi-repetition lookup latencies.", icon: "▥", pastel: "pastel-peach" },
  { href: "/workload", title: "26-Corpus Workload Explorer", desc: "Characterization, representation distributions, and front-coded reconstruction statistics.", icon: "▤", pastel: "pastel-pink" },
  { href: "/memory", title: "Memory Diagnosis", desc: "SSO short-string zero-allocation boundary, interning overhead, and V3/V4 layouts.", icon: "◫", pastel: "pastel-mint" },
  { href: "/pareto", title: "Break-Even & Trade-offs", desc: "Analytical break-even curve k_breakeven = (2L+86)/(L+13) and V4 negative result.", icon: "◈", pastel: "pastel-violet" },
  { href: "/compiler", title: "Compiler Simulator", desc: "Simulate symbol declarations through 3-tier routing and scope recycling.", icon: "⌘", pastel: "pastel-violet" },
  { href: "/scopes", title: "Scope Lifecycle", desc: "LIFO scope slot recycling tree and immediate memory deallocation.", icon: "◱", pastel: "pastel-sky" },
  { href: "/research", title: "Research & Paper", desc: "Research question, scientific boundaries, IEEE manuscript download, and audit log.", icon: "◎", pastel: "pastel-mint" },
  { href: "/docs", title: "Reproducibility Guide", desc: "Instructions for running ./reproduce_all.sh and verifying all results.", icon: "◧", pastel: "pastel-lemon" },
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
          <div className="max-w-3xl">
            <div className="flex items-center gap-2 mb-3">
              <span className={`inline-flex items-center gap-1.5 px-2.5 py-1 rounded-full text-[11px] font-mono font-semibold border ${
                status?.prototypeOperational
                  ? "bg-teal-50 text-teal-700 border-teal-200"
                  : "bg-slate-50 text-slate-500 border-slate-200"
              }`}>
                <span className={`h-1.5 w-1.5 rounded-full ${status?.prototypeOperational ? "bg-teal-500" : "bg-slate-400"}`} />
                {statusError ? "Status unavailable" : status ? (status.prototypeOperational ? "Research Freeze Complete" : "Not detected") : "Checking…"}
              </span>
              <span className="px-2.5 py-1 rounded-full text-[11px] font-mono font-semibold bg-indigo-50 text-indigo-700 border border-indigo-200">
                IEEE Conference Paper Ready
              </span>
            </div>
            <h1 className="text-2xl lg:text-3xl font-bold text-slate-900 tracking-tight">
              METIS
            </h1>
            <p className="text-sm font-medium text-slate-700 mt-1 italic">
              Evaluating the Memory/Latency Boundary of Adaptive Symbol-Table Name Representations in Embedded Workloads
            </p>
            <p className="text-sm text-slate-600 mt-2 leading-relaxed">
              METIS is a research compiler symbol-table architecture that investigates the fundamental trade-offs between physical heap memory footprint and lookup latency under embedded constraints. It dynamically routes identifiers between <strong>INLINE</strong>, <strong>INTERNED</strong>, and <strong>COMPRESSED</strong> representations.
            </p>
          </div>

          <div className="grid grid-cols-3 gap-3 shrink-0 w-full lg:w-auto">
            <StatCell label="Workloads" value="4 Embedded" />
            <StatCell label="Corpora" value="26 Host" />
            <StatCell label="Dataset Rows" value="433 Canonical" />
          </div>
        </div>

        <div className="mt-5 flex flex-wrap gap-2 items-center">
          <RepBadge color="teal" label="INLINE (≤12B)" />
          <RepBadge color="indigo" label="INTERNED (Pool)" />
          <RepBadge color="amber" label="COMPRESSED (Front-Coded)" />
          <span className="mx-2 h-5 w-px bg-slate-200 self-center" />
          <span className="text-xs font-mono text-slate-500">
            Authoritative Source: <code className="bg-slate-100 px-1.5 py-0.5 rounded text-slate-800">results/CANONICAL_FINAL_DATASET.csv</code>
          </span>
        </div>
      </div>

      {/* KPI Grid */}
      <KpiGrid />

      {/* Central Research Question & Findings Banner */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <div className="panel p-6 rounded-2xl border border-slate-200 space-y-3">
          <h2 className="text-sm font-mono uppercase tracking-wider text-slate-500 font-semibold">
            Central Research Question
          </h2>
          <blockquote className="border-l-4 border-teal-500 pl-4 py-1 text-slate-800 italic text-sm leading-relaxed">
            &ldquo;Under what workload conditions can adaptive symbol-table name representations overcome the structural memory efficiency of an embedded conventional hash-table baseline while satisfying a latency constraint?&rdquo;
          </blockquote>
          <p className="text-xs text-slate-600 leading-relaxed">
            Standard 64-bit systems utilize Short String Optimization (SSO), storing small identifiers (&le; 15 bytes) without heap allocation. METIS models and experimentally establishes exactly where adaptive representations beat conventional structures and where they do not.
          </p>
        </div>

        <div className="panel p-6 rounded-2xl border border-slate-200 space-y-3">
          <h2 className="text-sm font-mono uppercase tracking-wider text-slate-500 font-semibold">
            Key Empirical Conclusions
          </h2>
          <ul className="text-xs text-slate-600 space-y-2 list-disc list-inside">
            <li><strong>Short Names (L &le; 15B)</strong>: Conventional SSO hash tables remain structurally optimal (no positive break-even duplication ratio exists).</li>
            <li><strong>Zephyr Memory Victory</strong>: SymTabV3 reduces final heap by <strong>20.2% (13.52 MB)</strong> and peak heap by <strong>24.2%</strong> on 228k unique symbols.</li>
            <li><strong>Tail-Latency Trade-off</strong>: Zephyr p95 lookup latency is <strong>1.824&times;</strong> over EmbeddedConventional (0.228 &mu;s vs 0.125 &mu;s), <strong>failing the 1.25&times; gate</strong> due to front-coded decode copy work.</li>
            <li><strong>Negative Result (SymTabV4)</strong>: Side-table container overheads exceed scalar field savings on real identifier mixes.</li>
          </ul>
        </div>
      </div>

      {/* Canonical Embedded Workload Summary Table */}
      <div className="panel p-6 rounded-2xl border border-slate-200 space-y-4">
        <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-2">
          <div>
            <h2 className="text-base font-bold text-slate-900">
              Canonical Embedded Evaluation Summary
            </h2>
            <p className="text-xs text-slate-500 mt-0.5">
              Physical allocator measurements (<code className="font-mono">malloc_usable_size</code>) across 4 embedded workloads
            </p>
          </div>
          <Link
            href="/benchmarks"
            className="text-xs font-mono text-teal-700 hover:text-teal-800 font-semibold flex items-center gap-1"
          >
            View Full Benchmark Suite &rarr;
          </Link>
        </div>

        <div className="overflow-x-auto">
          <table className="w-full text-left text-xs font-mono">
            <thead>
              <tr className="border-b border-slate-200 text-slate-500 bg-slate-50">
                <th className="p-2.5">Workload</th>
                <th className="p-2.5 text-right">Unique Syms</th>
                <th className="p-2.5 text-right">EmbConv Final Heap</th>
                <th className="p-2.5 text-right">SymTabV3 Final Heap</th>
                <th className="p-2.5 text-right">RAM Delta</th>
                <th className="p-2.5 text-right">EmbConv p95</th>
                <th className="p-2.5 text-right">SymTabV3 p95</th>
                <th className="p-2.5 text-right">p95 Ratio</th>
                <th className="p-2.5 text-center">Latency Gate</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-slate-100">
              <tr className="hover:bg-slate-50/50">
                <td className="p-2.5 font-bold text-slate-800">FreeRTOS</td>
                <td className="p-2.5 text-right">10,386</td>
                <td className="p-2.5 text-right">3.89 MB</td>
                <td className="p-2.5 text-right">4.72 MB</td>
                <td className="p-2.5 text-right text-red-600 font-semibold">+21.2% (Conv wins)</td>
                <td className="p-2.5 text-right">0.094 &mu;s</td>
                <td className="p-2.5 text-right">0.277 &mu;s</td>
                <td className="p-2.5 text-right">2.95&times;</td>
                <td className="p-2.5 text-center"><span className="px-2 py-0.5 rounded text-[10px] bg-red-50 text-red-700 border border-red-200">FAIL</span></td>
              </tr>
              <tr className="hover:bg-slate-50/50">
                <td className="p-2.5 font-bold text-slate-800">Arduino</td>
                <td className="p-2.5 text-right">11,000</td>
                <td className="p-2.5 text-right">2.27 MB</td>
                <td className="p-2.5 text-right">2.99 MB</td>
                <td className="p-2.5 text-right text-red-600 font-semibold">+31.7% (Conv wins)</td>
                <td className="p-2.5 text-right">0.089 &mu;s</td>
                <td className="p-2.5 text-right">0.171 &mu;s</td>
                <td className="p-2.5 text-right">1.92&times;</td>
                <td className="p-2.5 text-center"><span className="px-2 py-0.5 rounded text-[10px] bg-red-50 text-red-700 border border-red-200">FAIL</span></td>
              </tr>
              <tr className="bg-teal-50/40 hover:bg-teal-50/60 font-semibold">
                <td className="p-2.5 text-teal-900 font-bold">Zephyr</td>
                <td className="p-2.5 text-right text-teal-900">228,739</td>
                <td className="p-2.5 text-right text-teal-900">66.91 MB</td>
                <td className="p-2.5 text-right text-teal-900">53.39 MB</td>
                <td className="p-2.5 text-right text-teal-700 font-bold">-20.2% (-13.52 MB)</td>
                <td className="p-2.5 text-right text-teal-900">0.125 &mu;s</td>
                <td className="p-2.5 text-right text-teal-900">0.228 &mu;s</td>
                <td className="p-2.5 text-right text-amber-700">1.824&times;</td>
                <td className="p-2.5 text-center"><span className="px-2 py-0.5 rounded text-[10px] bg-amber-50 text-amber-700 border border-amber-200">FAIL (1.25&times;)</span></td>
              </tr>
              <tr className="hover:bg-slate-50/50">
                <td className="p-2.5 font-bold text-slate-800">ESP-IDF</td>
                <td className="p-2.5 text-right">231,075</td>
                <td className="p-2.5 text-right">67.82 MB</td>
                <td className="p-2.5 text-right">81.91 MB</td>
                <td className="p-2.5 text-right text-red-600 font-semibold">+20.8% (Conv wins)</td>
                <td className="p-2.5 text-right">0.236 &mu;s</td>
                <td className="p-2.5 text-right">0.297 &mu;s</td>
                <td className="p-2.5 text-right">1.26&times;</td>
                <td className="p-2.5 text-center"><span className="px-2 py-0.5 rounded text-[10px] bg-red-50 text-red-700 border border-red-200">FAIL</span></td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>

      {/* Section cards */}
      <div>
        <h2 className="text-sm font-semibold text-slate-900 mb-3">Explore Research Visualizations</h2>
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
