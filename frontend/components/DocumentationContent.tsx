"use client";

import React from "react";

function Code({ children }: { children: string }) {
  return (
    <pre className="panel-soft rounded-xl p-4 text-[11px] font-mono text-slate-700 overflow-x-auto leading-relaxed">
      {children}
    </pre>
  );
}

function Section({ id, title, children }: { id: string; title: string; children: React.ReactNode }) {
  return (
    <section id={id} className="panel rounded-2xl p-6 space-y-3 scroll-mt-24">
      <h2 className="text-base font-bold text-slate-900">{title}</h2>
      {children}
    </section>
  );
}

const TOC = [
  ["getting-started", "Getting Started"],
  ["architecture", "Architecture"],
  ["api", "API"],
  ["representations", "Symbol Representations"],
  ["memory-model", "Memory Model"],
  ["methodology", "Benchmark Methodology"],
  ["experiments", "Experiments"],
  ["limitations", "Limitations"],
] as const;

export function DocumentationContent() {
  return (
    <div className="grid grid-cols-1 lg:grid-cols-12 gap-6">
      <aside className="lg:col-span-3">
        <nav className="panel rounded-2xl p-4 sticky top-24 space-y-1">
          <div className="text-[11px] font-mono uppercase tracking-wider text-slate-400 mb-2 px-2">On this page</div>
          {TOC.map(([id, label]) => (
            <a key={id} href={`#${id}`} className="block px-2 py-1.5 rounded-lg text-xs text-slate-600 hover:bg-slate-50 hover:text-indigo-700">
              {label}
            </a>
          ))}
        </nav>
      </aside>

      <div className="lg:col-span-9 space-y-6">
        <Section id="getting-started" title="Getting Started">
          <p className="text-xs text-slate-600 leading-relaxed">Build the C++ core and run the complete research suite:</p>
          <Code>{`# Reproduce complete research pipeline from repo root
./run_research_experiments.sh

# Or start the web application
cd frontend
npm install
npm run dev                    # http://localhost:3000`}</Code>
        </Section>

        <Section id="architecture" title="Architecture">
          <p className="text-xs text-slate-600 leading-relaxed">
            Source → extractor → event stream → <code className="bg-slate-100 px-1 rounded">src/analyze_main.cpp</code> → SymTab V2 engine (<code className="bg-slate-100 px-1 rounded">include/budget_sym.hpp</code>) → web dashboard. Full interactive diagram on the{" "}
            <a href="/architecture" className="text-indigo-600 hover:underline">Architecture page</a>.
          </p>
        </Section>

        <Section id="api" title="API">
          <p className="text-xs text-slate-600 leading-relaxed">All API endpoints are Next.js route handlers serving live research CSVs and compiled backend binaries.</p>
          <div className="space-y-3">
            <ApiRow method="POST" path="/api/analyze" desc="Body: { code, budgetBytes?, config? }. Runs code snippet through analyze.exe." />
            <ApiRow method="GET" path="/api/benchmarks" desc="Reads results/statistical_summary.csv fresh from disk." />
            <ApiRow method="GET" path="/api/experiments" desc="Reads results/ablation.csv fresh from disk." />
            <ApiRow method="GET" path="/api/status" desc="System health and CSV status checks." />
          </div>
        </Section>

        <Section id="representations" title="Symbol Representations">
          <p className="text-xs text-slate-600 leading-relaxed">
            INLINE (short, low pressure), INTERNED (exact repeats + default fallback), COMPRESSED
            (front-coded against previous entry with 1-byte hash fingerprints). Detailed decision logic on the{" "}
            <a href="/architecture" className="text-indigo-600 hover:underline">Architecture page</a>.
          </p>
        </Section>

        <Section id="memory-model" title="Memory Model">
          <p className="text-xs text-slate-600 leading-relaxed">
            Physical heap memory is measured via custom OS heap hooks during benchmark runs. Deterministic modeled bytes represent pure symbol entry data structures. Both metrics are explicitly separated in all result tables and documentation.
          </p>
        </Section>

        <Section id="methodology" title="Benchmark Methodology">
          <p className="text-xs text-slate-600 leading-relaxed">
            Evaluated across 8 synthetic datasets (N=30 seeds each) and 3 real-world production codebases (FreeRTOS, Arduino Core, Zephyr RTOS) comparing Conventional, Interned, BudgetSym V1, and SymTab V2.
          </p>
        </Section>

        <Section id="experiments" title="Experiments">
          <p className="text-xs text-slate-600 leading-relaxed">
            Ablation study systematically isolates scope slot recycling, front-coded block compression, 1-byte hash fingerprints, and adaptive policy selection on shared fixed workloads — see the{" "}
            <a href="/experiments" className="text-indigo-600 hover:underline">Experiments page</a>.
          </p>
        </Section>

        <Section id="limitations" title="Limitations">
          <ul className="text-xs text-slate-600 leading-relaxed list-disc pl-4 space-y-1">
            <li>Fixed directory allocation overhead (~8 kB) makes Conventional symbol tables superior on tiny workloads (N &lt; 200 symbols).</li>
            <li>On Zephyr RTOS (703k declarations, low scope depth), string interning achieves slightly lower peak heap (81.5 MB vs V2&apos;s 84.7 MB).</li>
            <li>Front-coded block decompression adds cold lookup latency on synthetic random long string workloads with high prefix similarity.</li>
            <li>Heavyweight ML models were rejected for policy selection due to high inference latency overhead (~57 ms) and OOD constraint violations; Hand-Designed Workload Heuristic is deployed.</li>
          </ul>
        </Section>
      </div>
    </div>
  );
}

function ApiRow({ method, path, desc }: { method: string; path: string; desc: string }) {
  return (
    <div className="flex flex-col sm:flex-row sm:items-start gap-2 text-xs">
      <div className="flex items-center gap-2 shrink-0 w-full sm:w-56">
        <span className="font-mono text-[10px] font-bold px-1.5 py-0.5 rounded bg-indigo-50 text-indigo-700 border border-indigo-200">{method}</span>
        <span className="font-mono text-slate-800">{path}</span>
      </div>
      <span className="text-slate-500">{desc}</span>
    </div>
  );
}
