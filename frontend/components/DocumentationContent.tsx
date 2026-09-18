"use client";

import React from "react";

function Code({ children }: { children: string }) {
  return (
    <pre className="panel-soft rounded-xl p-4 text-[11px] font-mono text-slate-700 overflow-x-auto leading-relaxed border border-slate-200">
      {children}
    </pre>
  );
}

function Section({ id, title, children }: { id: string; title: string; children: React.ReactNode }) {
  return (
    <section id={id} className="panel rounded-2xl p-6 space-y-3 scroll-mt-24 border border-slate-200">
      <h2 className="text-base font-bold text-slate-900">{title}</h2>
      {children}
    </section>
  );
}

const TOC = [
  ["reproducibility", "Master Reproduction Guide"],
  ["canonical-data", "Canonical Datasets"],
  ["architecture", "METIS Architecture"],
  ["methodology", "Physical Heap Methodology"],
  ["limitations", "Known Limitations & Boundaries"],
] as const;

export function DocumentationContent() {
  return (
    <div className="grid grid-cols-1 lg:grid-cols-12 gap-6">
      <aside className="lg:col-span-3">
        <nav className="panel rounded-2xl p-4 sticky top-24 space-y-1 border border-slate-200">
          <div className="text-[11px] font-mono uppercase tracking-wider text-slate-400 mb-2 px-2">On this page</div>
          {TOC.map(([id, label]) => (
            <a key={id} href={`#${id}`} className="block px-2 py-1.5 rounded-lg text-xs text-slate-600 hover:bg-slate-50 hover:text-indigo-700 font-mono">
              {label}
            </a>
          ))}
        </nav>
      </aside>

      <div className="lg:col-span-9 space-y-6">
        <Section id="reproducibility" title="Master Reproduction Guide">
          <p className="text-xs text-slate-600 leading-relaxed">
            The entire METIS research artifact — including correctness tests, differential fuzzing, multi-repetition benchmark runs, dataset reconciliation, publication figure regeneration, and LaTeX document compilation — is automated via a single command:
          </p>
          <Code>{`# Clone and enter the repository
git clone https://github.com/adhyan-jain/Metis.git
cd Metis

# Run end-to-end master reproduction pipeline
./reproduce_all.sh

# Start the interactive research dashboard
cd frontend
npm install
npm run dev`}</Code>
          <div className="text-xs text-slate-600 space-y-1 pt-2">
            <p><strong>Script Actions:</strong></p>
            <ol className="list-decimal list-inside space-y-1 text-[11px] text-slate-600">
              <li>Executes unit tests and differential fuzzing under AddressSanitizer/UBSan.</li>
              <li>Compiles all C++ benchmark harnesses (<code className="font-mono">bin/embedded_bench</code>, <code className="font-mono">bin/real_world_bench</code>).</li>
              <li>Runs multi-repetition embedded benchmarks ($R=3$) with physical allocator heap profiling.</li>
              <li>Reconciles the canonical dataset into <code className="font-mono">results/CANONICAL_FINAL_DATASET.csv</code>.</li>
              <li>Generates all 16 figures in <code className="font-mono">figures/</code>.</li>
              <li>Compiles the IEEE publication paper (<code className="font-mono">Metis_v2_IEEE.pdf</code>).</li>
            </ol>
          </div>
        </Section>

        <Section id="canonical-data" title="Canonical Datasets">
          <p className="text-xs text-slate-600 leading-relaxed">
            The single authoritative source of truth for all measurements is:
          </p>
          <Code>{`results/CANONICAL_FINAL_DATASET.csv (433 measured configurations)`}</Code>
          <p className="text-xs text-slate-600 leading-relaxed">
            Supporting CSV files include <code className="font-mono">results/embedded_benchmark.csv</code> (FreeRTOS, Arduino, Zephyr, ESP-IDF), <code className="font-mono">data/real_world_benchmark.csv</code> (26 host software corpora), and <code className="font-mono">results/synthetic_experiments_A_F.csv</code>.
          </p>
        </Section>

        <Section id="architecture" title="METIS Architecture">
          <p className="text-xs text-slate-600 leading-relaxed">
            METIS (<code className="font-mono">include/symtab_v3.hpp</code>) dynamically chooses per symbol between:
          </p>
          <ul className="list-disc list-inside text-xs text-slate-600 space-y-1">
            <li><strong>INLINE</strong>: ≤12 bytes directly stored inside the slot.</li>
            <li><strong>INTERNED</strong>: 4-byte pool index into a shared string dictionary.</li>
            <li><strong>COMPRESSED</strong>: Front-coded in blocks of B=32 with anchor interval A=8 and zero-allocation stack buffer decoding.</li>
          </ul>
        </Section>

        <Section id="methodology" title="Physical Heap Methodology">
          <p className="text-xs text-slate-600 leading-relaxed">
            All heap memory measurements override global <code className="font-mono">operator new</code> and <code className="font-mono">operator delete</code> via <code className="font-mono">include/heap_counter.hpp</code>, invoking <code className="font-mono">malloc_usable_size(p)</code> to record true physical RAM allocations including system alignment and chunk rounding overheads.
          </p>
        </Section>

        <Section id="limitations" title="Known Limitations &amp; Boundaries">
          <ul className="list-disc list-inside text-xs text-slate-600 space-y-1.5">
            <li><strong>Conventional SSO Optimal for Short Names</strong>: On host codebases dominated by short identifiers (L ≤ 15B), Conventional hash tables incur zero secondary heap bytes and remain optimal.</li>
            <li><strong>Tail-Latency Trade-off</strong>: Compressed representation decoding introduces prefix/suffix memory copying, resulting in a 1.824x p95 latency overhead on Zephyr and failing the 1.25x latency gate.</li>
            <li><strong>V4 Negative Result</strong>: Side-table container overheads exceed scalar field savings on real identifier distributions.</li>
          </ul>
        </Section>
      </div>
    </div>
  );
}
