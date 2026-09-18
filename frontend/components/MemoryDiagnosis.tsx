"use client";

import React from "react";

export function MemoryDiagnosis() {
  return (
    <div className="space-y-6">
      {/* Overview Banner */}
      <div className="panel p-6 rounded-2xl border border-slate-200 space-y-3">
        <h3 className="font-mono text-base font-semibold text-slate-900">
          Physical Memory Allocation Mechanics &amp; Boundary Explanations
        </h3>
        <p className="text-xs text-slate-600 leading-relaxed">
          The memory efficiency of compiler symbol tables is governed by the physical allocator footprint of node pointers, container metadata, and string storage. Below is the architectural breakdown of why performance differs across representation strategies.
        </p>
      </div>

      {/* 4 Architecture Breakdown Cards */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
        {/* 1. Conventional / SSO */}
        <div className="panel p-5 rounded-2xl border border-slate-200 space-y-3">
          <div className="flex items-center justify-between">
            <span className="font-mono text-xs font-bold uppercase tracking-wider text-slate-800">
              1. Conventional / SSO Baseline
            </span>
            <span className="px-2 py-0.5 rounded text-[10px] font-mono font-semibold bg-slate-100 text-slate-700">
              Optimal for L ≤ 15B
            </span>
          </div>
          <p className="text-xs text-slate-600 leading-relaxed">
            In standard 64-bit libstdc++/libc++, <code className="font-mono bg-slate-100 px-1 rounded">std::string</code> employs Short String Optimization (SSO). Characters of length <code className="font-mono">L ≤ 15</code> bytes reside inside the internal inline buffer.
          </p>
          <div className="panel-soft p-3 rounded-xl font-mono text-[11px] text-slate-700 space-y-1">
            <div>Secondary Heap Cost: <strong className="text-teal-700">0 bytes</strong> (SSO)</div>
            <div>Break-Even Live Duplication: <strong className="text-rose-700">k_breakeven &lt; 0</strong> (No positive solution)</div>
          </div>
          <p className="text-[11px] text-slate-500">
            Because real host software codebases contain 59%–76% short identifiers and low live duplication (k &lt; 1.5), Conventional hash tables are mathematically optimal for host compilation.
          </p>
        </div>

        {/* 2. String Interning */}
        <div className="panel p-5 rounded-2xl border border-indigo-200 bg-indigo-50/20 space-y-3">
          <div className="flex items-center justify-between">
            <span className="font-mono text-xs font-bold uppercase tracking-wider text-indigo-900">
              2. Global String Interning
            </span>
            <span className="px-2 py-0.5 rounded text-[10px] font-mono font-semibold bg-indigo-100 text-indigo-800">
              Deduplication Baseline
            </span>
          </div>
          <p className="text-xs text-slate-600 leading-relaxed">
            String interning stores exactly one canonical copy of each unique identifier name in a global pool and references it by a 4-byte pool index.
          </p>
          <div className="panel-soft p-3 rounded-xl font-mono text-[11px] text-slate-700 space-y-1">
            <div>Fixed Pool Overhead: <strong className="text-indigo-700">C_pool(L) ≈ 2L + 86 bytes</strong></div>
            <div>Index Slot Cost: <strong className="text-indigo-700">C_idx ≈ 28 bytes</strong></div>
          </div>
          <p className="text-[11px] text-slate-500">
            Interning eliminates duplicate character storage, but the hash-table node overhead (S_pool_node ≈ 68B) causes interning to lose on memory unless identifier length L &gt; 15B and duplication k &gt; 3.33.
          </p>
        </div>

        {/* 3. SymTabV3 */}
        <div className="panel p-5 rounded-2xl border border-teal-200 bg-teal-50/30 space-y-3">
          <div className="flex items-center justify-between">
            <span className="font-mono text-xs font-bold uppercase tracking-wider text-teal-900">
              3. SymTabV3 (Proposed Architecture)
            </span>
            <span className="px-2 py-0.5 rounded text-[10px] font-mono font-bold bg-teal-100 text-teal-800">
              -20.2% Final Heap on Zephyr
            </span>
          </div>
          <p className="text-xs text-slate-600 leading-relaxed">
            Dynamically partitions symbols into 3 tiers: <strong>Inline</strong> (≤12B inside slot), <strong>Interned</strong> (pool index), and front-coded <strong>Compressed</strong> (blocks of B=32, anchor interval A=8).
          </p>
          <div className="panel-soft p-3 rounded-xl font-mono text-[11px] text-slate-700 space-y-1">
            <div>Zephyr Final Heap: <strong className="text-teal-700">53.39 MB vs 66.91 MB (-13.52 MB)</strong></div>
            <div>Zephyr Peak Heap: <strong className="text-teal-700">57.89 MB vs 76.37 MB (-24.2%)</strong></div>
            <div>Latency Trade-off: <strong className="text-amber-700">1.824× p95 latency (0.228 µs vs 0.125 µs)</strong></div>
          </div>
          <p className="text-[11px] text-slate-500">
            Scope slot recycling immediately returns exited lexical scope entries to a LIFO free-list, preventing heap fragmentation across deep call stacks.
          </p>
        </div>

        {/* 4. SymTabV4 Negative Result */}
        <div className="panel p-5 rounded-2xl border border-rose-200 bg-rose-50/20 space-y-3">
          <div className="flex items-center justify-between">
            <span className="font-mono text-xs font-bold uppercase tracking-wider text-rose-900">
              4. SymTabV4 (Negative Result)
            </span>
            <span className="px-2 py-0.5 rounded text-[10px] font-mono font-bold bg-rose-100 text-rose-800">
              Disclosed Negative Result
            </span>
          </div>
          <p className="text-xs text-slate-600 leading-relaxed">
            Tested whether shrinking the core slot from 32B to 24B (CoreEntry) and moving metadata (access counts, timestamps) to a sparse side table (HotMetaTable) improves density.
          </p>
          <div className="panel-soft p-3 rounded-xl font-mono text-[11px] text-slate-700 space-y-1">
            <div>Core Inline Savings: <strong className="text-teal-700">+8 bytes / slot</strong></div>
            <div>Side-Table Marginal Overhead: <strong className="text-rose-700">~34–57 bytes / entry + T_table</strong></div>
          </div>
          <p className="text-[11px] text-slate-500">
            Empirical outcome: V4 lost to V3 on 19/20 real software corpora and lost to Conventional on 20/20 corpora because side-table container overheads exceed scalar field savings on real identifier distributions.
          </p>
        </div>
      </div>
    </div>
  );
}
