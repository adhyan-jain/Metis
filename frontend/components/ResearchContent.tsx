"use client";

import React from "react";

const SECTIONS = [
  {
    title: "Problem Statement",
    body: "Embedded compilation environments operate under tight memory constraints, whereas conventional compiler symbol tables incur substantial string pointer, metadata, and hash table bucket overheads (~64B+ per entry). A memory-bounded embedded compiler cannot afford peak memory bloat, yet cannot pay uniform front-coding lookup overhead on every identifier hit.",
  },
  {
    title: "Research Motivation",
    body: "Neither extreme — uniform uncompressed tables (fast, memory-heavy) nor uniform string interning/compression (compact, lookup-costly) — idealizes resource-bounded compilation. SymTab V2 resolves this via a dynamic, 3-tier per-symbol decision policy (INLINE, INTERNED, COMPRESSED) combined with scope slot recycling and 1-byte hash fingerprints.",
  },
  {
    title: "Baseline Positioning",
    body: "SymTab V2 is evaluated against Conventional hash tables (unordered_map<string, Symbol>), Interned string pools (refcounted shared string dictionary), and BudgetSym V1. All baselines are measured under identical workload runners across synthetic and production codebases.",
  },
  {
    title: "Research Contributions",
    body: "1) Unified 3-tier representation policy adapting to string length, scope nesting, and prefix similarity. 2) Scope slot recycling for non-nested scope lifetime reclamation. 3) 1-byte hash fingerprints accelerating cold lookups. 4) Empirical Pareto evaluation under formal latency bounds (1.10x, 1.25x, 1.50x, 2.00x).",
  },
  {
    title: "Empirical Results Summary",
    body: "On the large synthetic workload (N=20,000 symbols), SymTab V2 achieves 360.7 kB mean heap memory, delivering 69.89% heap savings vs BudgetSym V1 (1,197.7 kB) and 52.89% savings vs Interned (765.6 kB). This synthetic-workload result does not generalize to real-world corpora: on real-world C/C++ codebases, SymTab V2 measures 1.87x-6.52x MORE physical heap than Conventional (see the Memory Diagnosis section on the Memory page). On Zephyr RTOS (703k declarations), SymTab V2 manages peak heap memory at 84.7 MB while maintaining cold p50 lookup latency at 0.098 µs.",
  },
  {
    title: "Policy Selection & ML Evaluation",
    body: "Evaluated Hand-Designed Workload Heuristic against ML models (Decision Tree, ExtraTrees, Ridge Classifier) across 4 latency bounds. Heavyweight ML models were rejected due to high inference latency overhead (~57 ms for ExtraTrees) and LOWO constraint violations on random strings. The Hand-Designed Workload Heuristic was selected for deployment, achieving 0% constraint violations on all held-out real-world corpora.",
  },
  {
    title: "Operational Boundaries & Scope Caveats",
    body: "SymTab V2 incurs a fixed directory allocation overhead (~8 kB). On tiny workloads (N=100 symbols), Conventional uses 2.1 kB vs SymTab V2's 8.3 kB. SymTab V2 requires N ≥ 200 symbols or active scope nesting to achieve net memory reduction.",
  },
  {
    title: "Completed Validation Pipeline",
    body: "100% of planned research validation is complete: 30-seed multi-seed statistical validation (results/statistical_summary.csv), ablation isolations (results/ablation.csv), production real-world corpus extraction (results/corpus_benchmark.csv), and Pareto ML evaluations (results/ml_comparison.csv).",
  },
];

const FACULTY_QA = [
  {
    q: "Does SymTab V2 universally outperform Conventional or Interned symbol tables?",
    a: "No. Claim boundaries are strictly grounded: Conventional is superior on tiny workloads (N < 200) due to V2's fixed ~8 kB directory overhead. On Zephyr RTOS, Interned achieves slightly lower heap (81.5 MB vs V2's 84.7 MB) because Zephyr contains a high ratio of global declarations with low scope nesting depth. More significantly, across real-world C/C++ corpora (FreeRTOS, Arduino, Lua, CPython, ESP-IDF, Zephyr), SymTab V2 measures 1.87x-6.52x MORE physical heap than Conventional -- the opposite of its synthetic-workload result -- because V2's everSeenRep_ and poolLookup_ registries are never pruned on scope exit while Conventional deallocates scope maps immediately (see docs/v2_real_world_memory_diagnosis.md and the Memory page's diagnosis section). V2's synthetic-workload advantage is real but does not currently transfer to production codebases; the memory tradeoff exists and is under active architectural redesign.",
  },
  {
    q: "How does 1-byte hash fingerprinting accelerate lookups?",
    a: "1-byte hash fingerprints store a fast 8-bit hash header alongside entry descriptors. During lookup, fingerprint mismatches immediately reject non-matching candidate entries, bypassing expensive front-coded string chain decompression. This provides cold p50 lookup latencies of 0.089 µs on Arduino and 0.098 µs on Zephyr.",
  },
  {
    q: "Why was the Hand-Designed Workload Heuristic chosen over ML models for policy selection?",
    a: "ML models like ExtraTrees incurred high inference latency overheads (~57 ms) and exhibited high constraint violation rates (22%–55%) during LOWO cross-validation due to out-of-distribution synthetic random strings. The Hand-Designed Workload Heuristic operates in sub-microsecond time (0.25–0.43 µs) and achieved 0% constraint violations across all held-out real-world corpora.",
  },
];

export function ResearchContent() {
  return (
    <div className="space-y-8">
      <div className="panel p-6 rounded-2xl space-y-2">
        <h1 className="text-xl font-bold text-slate-900">Research Methodology &amp; Positioning</h1>
        <p className="text-sm text-slate-500 max-w-3xl leading-relaxed">
          SymTab V2 positions adaptive symbol representation within memory-bounded embedded compilers.
          All claims are empirically validated against authoritative multi-seed and corpus benchmark results.
        </p>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        {SECTIONS.map((s) => (
          <div key={s.title} className="panel rounded-2xl p-5">
            <h3 className="text-sm font-bold text-slate-900 mb-2">{s.title}</h3>
            <p className="text-xs text-slate-600 leading-relaxed">{s.body}</p>
          </div>
        ))}
      </div>

      <div className="panel p-6 rounded-2xl space-y-4">
        <h3 className="font-mono text-sm font-bold text-slate-900">Research Audit Q&amp;A</h3>
        <div className="space-y-3">
          {FACULTY_QA.map((qa, i) => (
            <div key={i} className="panel-soft p-4 rounded-xl space-y-1">
              <h4 className="font-mono text-xs font-bold text-indigo-700">Q: {qa.q}</h4>
              <p className="text-xs text-slate-600 leading-relaxed">{qa.a}</p>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
