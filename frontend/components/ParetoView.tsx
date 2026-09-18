"use client";

import React, { useState } from "react";

export function ParetoView() {
  const [selectedL, setSelectedL] = useState<number>(32);

  // Analytical break-even formula: k_breakeven = (2L + 86) / (L + 13) for L > 15
  const computeKBreakeven = (L: number) => {
    if (L <= 15) return null; // No positive solution
    return (2 * L + 86) / (L + 13);
  };

  const kBreak = computeKBreakeven(selectedL);

  return (
    <div className="space-y-6">
      {/* 1. Analytical Cost & Break-Even Boundary Model */}
      <div className="panel p-6 rounded-2xl border border-slate-200 space-y-4">
        <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-2 pb-3 border-b border-slate-200">
          <div>
            <h3 className="font-mono text-base font-semibold text-slate-900">
              Analytical Cost &amp; Break-Even Boundary Model
            </h3>
            <p className="text-xs text-slate-500 mt-1">
              Closed-form memory equation governing when adaptive representations overcome Conventional SSO storage
            </p>
          </div>
          <span className="px-2.5 py-1 rounded-full text-[10px] font-mono font-bold bg-indigo-50 text-indigo-700 border border-indigo-200 shrink-0">
            Implementation-Specific Cost Model
          </span>
        </div>

        <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
          <div className="space-y-3 text-xs text-slate-600 leading-relaxed">
            <p>
              By equating physical memory equations for Conventional host tables (<code className="font-mono bg-slate-100 px-1 rounded">M_conv(k, L)</code>) and Interned/Adaptive tables (<code className="font-mono bg-slate-100 px-1 rounded">M_int(k, L)</code>), we derive the theoretical break-even live duplication ratio <code className="font-mono font-bold">k_breakeven(L)</code>:
            </p>
            <div className="panel-soft p-4 rounded-xl border border-indigo-200 bg-indigo-50/30 text-center font-mono space-y-2">
              <div className="text-sm font-bold text-indigo-950">
                k_breakeven(L) = (2L + 86) / (L + 13) &emsp; [for L &gt; 15 bytes]
              </div>
              <div className="text-xs text-rose-700 font-semibold">
                No positive break-even ratio exists for L ≤ 15 bytes (SSO regime)
              </div>
            </div>
            <ul className="space-y-1.5 list-disc list-inside text-[11px] text-slate-600">
              <li>As <code className="font-mono">L → 15⁺</code>: <code className="font-mono">k_breakeven → 4.14</code></li>
              <li>At <code className="font-mono">L = 32 bytes</code>: <code className="font-mono">k_breakeven = 150 / 45 ≈ 3.33</code> (Confirmed by Synthetic Exp D2)</li>
              <li>As <code className="font-mono">L → ∞</code>: <code className="font-mono">k_breakeven → 2.00</code></li>
            </ul>
          </div>

          {/* Interactive Calculator */}
          <div className="panel-soft p-5 rounded-xl border border-slate-200 space-y-4">
            <h4 className="font-mono text-xs font-bold text-slate-800 uppercase tracking-wider">
              Interactive Break-Even Calculator
            </h4>
            <div>
              <div className="flex justify-between text-xs font-mono text-slate-600 mb-1">
                <span>Identifier Length (L):</span>
                <span className="font-bold text-teal-700">{selectedL} bytes</span>
              </div>
              <input
                type="range"
                min="4"
                max="64"
                value={selectedL}
                onChange={(e) => setSelectedL(Number(e.target.value))}
                className="w-full cursor-pointer accent-teal-600"
              />
              <div className="flex justify-between text-[10px] font-mono text-slate-400 mt-1">
                <span>4B (Short SSO)</span>
                <span>15B (SSO Cutoff)</span>
                <span>32B (Typical Long)</span>
                <span>64B (Mangled/Qualified)</span>
              </div>
            </div>

            <div className="p-3 rounded-lg border bg-white space-y-1 text-xs font-mono">
              <div className="text-slate-500 text-[11px]">Calculated Outcome for L = {selectedL}B:</div>
              {selectedL <= 15 ? (
                <div className="text-rose-600 font-bold">
                  Conventional Wins Structurally (0 heap bytes paid under SSO)
                </div>
              ) : (
                <div className="text-teal-700 font-bold">
                  Adaptive / Interned beats Conventional when duplication k &gt; {kBreak?.toFixed(2)}
                </div>
              )}
            </div>
          </div>
        </div>
      </div>

      {/* 2. Disclosed Negative Result Card: SymTabV4 */}
      <div className="panel p-6 rounded-2xl border border-rose-200 bg-rose-50/20 space-y-3">
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-2">
            <span className="font-mono text-sm font-bold text-slate-900">
              Disclosed Negative Result: SymTabV4 (Representation-Conditional Side Tables)
            </span>
            <span className="px-2 py-0.5 rounded text-[10px] font-mono font-bold bg-rose-100 text-rose-800 border border-rose-300">
              Negative Architectural Result
            </span>
          </div>
        </div>

        <p className="text-xs text-slate-600 leading-relaxed">
          To test whether eliminating hot/cold metadata from <strong>Inline</strong> entries improves byte density, SymTabV4 reduced the core slot from 32B to 24B (<code className="font-mono">CoreEntry</code>) and stored scalar fields in a sparse side table (<code className="font-mono">HotMetaTable</code>).
        </p>

        <div className="grid grid-cols-1 md:grid-cols-3 gap-3 font-mono text-xs pt-1">
          <div className="p-3 rounded-xl bg-white border border-slate-200">
            <span className="text-[10px] text-slate-400 block uppercase">Core Struct Savings</span>
            <span className="font-bold text-teal-700">+8 bytes per slot</span>
          </div>
          <div className="p-3 rounded-xl bg-white border border-slate-200">
            <span className="text-[10px] text-slate-400 block uppercase">Side-Table Entry Cost</span>
            <span className="font-bold text-rose-700">~34–57 bytes marginal + T_table</span>
          </div>
          <div className="p-3 rounded-xl bg-white border border-slate-200">
            <span className="text-[10px] text-slate-400 block uppercase">Empirical Benchmark Result</span>
            <span className="font-bold text-rose-700">Lost on 19/20 real corpora</span>
          </div>
        </div>

        <p className="text-[11px] text-slate-500 leading-relaxed">
          Because real software contains 10%–70% non-Inline symbols, the container allocation floor (<code className="font-mono">T_table</code>) consistently outweighs core scalar savings. This empirical negative result is preserved in the repository for scientific completeness.
        </p>
      </div>
    </div>
  );
}
