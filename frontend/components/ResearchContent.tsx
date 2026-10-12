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

// Zephyr runtime-symbolization gap-fill (research/paper_final). Numbers are copied from
// research/paper_final/results/analysis_exploratory/per_workload.csv (image = allocated flash sections, GCC 14.3;
// the frozen flat-binary metric is 4 KiB-quantised on RISC-V). "saved" = best compact scheme vs best existing scheme.
const ZEPHYR_STUDY = {
  status: "Pre-registered gates NOT met — no paper was written",
  summary:
    "Ten existing Zephyr sample workloads (6 on Cortex-M3, 4 on RISC-V RV32; 137–2,337 symbols) were relinked to a fixed point with 14 symbol-table schemes (nine workloads usable; one baseline oscillated on net_ptp instead of converging). Compact address indexing explains about 71% (pooled; 68–75% for N ≥ 985) of the 1.7–3.1% whole-image saving of the best compact scheme over the best existing scheme. After equalising the address index, the proposed name coding adds 0.4–0.8% of the image for N ≥ 710 and nothing at N = 137. The 70%-share gate and the FSST-within-1% gate failed. No sample reached 3,000 symbols. Sizes only: no latency, energy or runtime-performance claim.",
  rows: [
    { w: "hello_world (RV32)", n: 137, saved: "408 B (1.67%)", share: ">100% (names cost more)", resid: "-0.64%" },
    { w: "net_sockets_socketpair (M3)", n: 710, saved: "2,568 B (3.06%)", share: "73%", resid: "0.83%" },
    { w: "net_midi2 (RV32)", n: 985, saved: "3,546 B (2.10%)", share: "68%", resid: "0.68%" },
    { w: "net_sockets_http_get (M3)", n: 1061, saved: "3,768 B (2.67%)", share: "70%", resid: "0.81%" },
    { w: "net_stats (M3)", n: 1321, saved: "4,624 B (2.23%)", share: "70%", resid: "0.68%" },
    { w: "net_ethernet_lldp (M3)", n: 1422, saved: "4,980 B (2.21%)", share: "71%", resid: "0.66%" },
    { w: "net_ethernet_bridge (RV32)", n: 1445, saved: "4,720 B (1.68%)", share: "75%", resid: "0.43%" },
    { w: "net_cloud_mqtt_azure (RV32)", n: 2148, saved: "7,718 B (1.98%)", share: "69%", resid: "0.62%" },
    { w: "net_prometheus (M3)", n: 2337, saved: "8,440 B (2.16%)", share: "70%", resid: "0.65%" },
  ],
  caveat:
    "Earlier METIS-X / SymTabV3 memory figures elsewhere on this site depend on harness and baseline choice (a 20.2 MB unique-symbol set inside the measured scope, and two different classes both named EmbeddedConventional); see research/paper_audit and research/paper_final/EXPERIMENT_LOG.md. Local, unpublished research — not peer reviewed.",
};

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

      {/* Zephyr runtime symbolization gap-fill */}
      <div className="panel p-6 rounded-2xl border border-slate-200 space-y-3">
        <h3 className="font-mono text-sm font-bold text-slate-900 uppercase tracking-wider">
          Zephyr Runtime Symbolization &mdash; Gap-Fill Outcome
        </h3>
        <p className="text-xs font-bold text-rose-700">{ZEPHYR_STUDY.status}</p>
        <a
          href="/Metis_Zephyr_Symbolization_Report.pdf"
          target="_blank"
          rel="noreferrer"
          className="inline-block px-3 py-1.5 rounded-lg text-xs font-mono font-bold bg-slate-800 text-white hover:bg-slate-900"
        >
          Download negative-result report (PDF)
        </a>
        <p className="text-xs text-slate-600 leading-relaxed">{ZEPHYR_STUDY.summary}</p>
        <div className="overflow-x-auto">
          <table className="w-full text-xs text-left">
            <thead>
              <tr className="text-slate-500 border-b border-slate-200">
                <th className="py-1 pr-3">Workload</th>
                <th className="py-1 pr-3">N</th>
                <th className="py-1 pr-3">Saved vs best existing</th>
                <th className="py-1 pr-3">Address-index share</th>
                <th className="py-1">Residual name coding</th>
              </tr>
            </thead>
            <tbody>
              {ZEPHYR_STUDY.rows.map((r) => (
                <tr key={r.w} className="border-b border-slate-100 text-slate-700">
                  <td className="py-1 pr-3 font-mono">{r.w}</td>
                  <td className="py-1 pr-3">{r.n}</td>
                  <td className="py-1 pr-3">{r.saved}</td>
                  <td className="py-1 pr-3">{r.share}</td>
                  <td className="py-1">{r.resid}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        <p className="text-[11px] text-slate-500 leading-relaxed">{ZEPHYR_STUDY.caveat}</p>
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
