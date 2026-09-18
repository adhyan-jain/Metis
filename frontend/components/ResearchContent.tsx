"use client";

import React from "react";

const SECTIONS = [
  {
    title: "Central Research Question",
    body: "Under what workload conditions can adaptive symbol-table name representations overcome the structural memory efficiency of an embedded conventional hash-table baseline while satisfying a latency constraint?",
  },
  {
    title: "Key Contributions",
    body: "1) Adaptive 3-tier architecture (SymTabV3) with zero-allocation stack buffer front-code decoding; 2) Closed-form analytical cost and break-even model (k_breakeven = (2L+86)/(L+13)); 3) Physical memory advantage on Zephyr RTOS (-20.2% final heap, -13.52 MB); 4) Quantified tail-latency trade-off (1.824x p95 latency ratio on Zephyr); 5) Disclosed negative architectural result for SymTabV4 side tables.",
  },
  {
    title: "The Short-String (SSO) Regime",
    body: "In standard 64-bit systems, Short String Optimization stores identifiers up to 15 bytes directly inside the string structure without secondary heap allocations. The analytical break-even model proves no positive duplication ratio k exists for L ≤ 15B, confirming Conventional hash tables are optimal for short identifiers.",
  },
  {
    title: "The Long-Identifier Regime",
    body: "For identifiers longer than the SSO threshold (L > 15B), Conventional pays dynamic heap allocations. Interning and adaptive compression beat Conventional when duplication k > (2L+86)/(L+13). At L = 32B, break-even occurs at k ≥ 3.33 (empirically confirmed by Synthetic Experiment D2).",
  },
  {
    title: "Tail-Latency Trade-off & Gate Failure",
    body: "On Zephyr RTOS (2.6M events, 228k unique symbols), SymTabV3 achieves 53.39 MB final heap (vs 66.91 MB for EmbeddedConventional), saving 20.2% RAM. However, p95 latency increases from 0.125 µs to 0.228 µs (1.824x), failing the 1.25x p95 latency gate due to measured front-coded prefix/suffix memory copy operations.",
  },
  {
    title: "Disclosed Negative Result (SymTabV4)",
    body: "SymTabV4 evaluated representation-conditional metadata, reducing core entries to 24B and moving scalar fields to a sparse side table. Across real software codebases, side-table container overheads (T_table) and marginal entry costs (~34–57B) exceed the 8B core savings, causing V4 to lose to V3 on 19/20 real software corpora.",
  },
];

const RESEARCH_QA = [
  {
    q: "Does METIS claim universal memory superiority across all compiler workloads?",
    a: "No. METIS explicitly establishes workload boundaries: Conventional hash tables with Short String Optimization (SSO) remain structurally optimal for short identifiers (L ≤ 15B) and low duplication (k < 1.5). Adaptive representations become advantageous only at larger scales with longer names and higher duplication (e.g. Zephyr RTOS).",
  },
  {
    q: "Why did SymTabV3 fail the 1.25x p95 latency gate on Zephyr?",
    a: "Reconstructing front-coded compressed members requires copying prefix and suffix byte slices across block members to anchor strings (mean depth 3.31 steps). Even with zero dynamic allocations using stack buffers, executing multiple memory copy operations introduces reconstruction overhead that raises tail latency to 1.824x over uncompressed hash tables.",
  },
  {
    q: "What is the primary scientific value of the SymTabV4 evaluation?",
    a: "SymTabV4 provides empirical negative evidence showing that sparse side-table container overheads erode scalar struct savings when workloads contain realistic representation mixes (10%–70% non-inline symbols). Disclosing this prevents future redundant compiler optimizations.",
  },
];

export function ResearchContent() {
  return (
    <div className="space-y-8">
      {/* Header */}
      <div className="panel p-6 rounded-2xl border border-slate-200 space-y-3">
        <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-3">
          <div>
            <h1 className="text-xl font-bold text-slate-900">METIS Research Methodology &amp; Paper</h1>
            <p className="text-xs text-slate-500 mt-1">
              Evaluating the Memory/Latency Boundary of Adaptive Symbol-Table Name Representations in Embedded Workloads
            </p>
          </div>
          <a
            href="/Metis_v2_IEEE.pdf"
            target="_blank"
            rel="noreferrer"
            className="inline-flex items-center gap-2 px-4 py-2 rounded-xl text-xs font-mono font-bold bg-indigo-600 text-white hover:bg-indigo-700 transition-colors shadow-sm shrink-0"
          >
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4" />
              <polyline points="7 10 12 15 17 10" />
              <line x1="12" y1="15" x2="12" y2="3" />
            </svg>
            Download IEEE PDF
          </a>
        </div>
      </div>

      {/* Sections Grid */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        {SECTIONS.map((s) => (
          <div key={s.title} className="panel rounded-2xl p-5 border border-slate-200">
            <h3 className="text-sm font-bold text-slate-900 mb-2">{s.title}</h3>
            <p className="text-xs text-slate-600 leading-relaxed">{s.body}</p>
          </div>
        ))}
      </div>

      {/* Research Q&A */}
      <div className="panel p-6 rounded-2xl border border-slate-200 space-y-4">
        <h3 className="font-mono text-sm font-bold text-slate-900 uppercase tracking-wider">
          Scientific Audit &amp; Peer Review Q&amp;A
        </h3>
        <div className="space-y-3">
          {RESEARCH_QA.map((qa, i) => (
            <div key={i} className="panel-soft p-4 rounded-xl space-y-1">
              <h4 className="font-mono text-xs font-bold text-indigo-800">Q: {qa.q}</h4>
              <p className="text-xs text-slate-600 leading-relaxed">{qa.a}</p>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
