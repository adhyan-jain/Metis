"use client";

import React from "react";

export interface StackedBarSegment {
  key: string;
  label: string;
  value: number;
  color: string;
}

export interface StackedBarGroup {
  label: string;
  segments: StackedBarSegment[];
}

export function StackedBarChart({ groups }: { groups: StackedBarGroup[] }) {
  if (groups.length === 0) {
    return <div className="text-xs text-slate-400 py-8 text-center">No representation data available.</div>;
  }

  const allKeys = Array.from(new Map(groups.flatMap((g) => g.segments).map((s) => [s.key, s])).values());

  return (
    <div className="space-y-3">
      {groups.map((g) => {
        const total = g.segments.reduce((sum, s) => sum + s.value, 0) || 1;
        return (
          <div key={g.label} className="flex items-center gap-3">
            <span className="w-24 shrink-0 text-[11px] font-mono text-slate-600 truncate">{g.label}</span>
            <div className="flex-1 h-6 rounded-md overflow-hidden flex border border-slate-200">
              {g.segments.map((s) => {
                const pct = (s.value / total) * 100;
                if (pct <= 0) return null;
                return (
                  <div
                    key={s.key}
                    style={{ width: `${pct}%`, background: s.color }}
                    className="h-full flex items-center justify-center"
                    title={`${s.label}: ${pct.toFixed(1)}%`}
                  >
                    {pct > 12 && <span className="text-[9px] font-mono font-semibold text-white">{pct.toFixed(0)}%</span>}
                  </div>
                );
              })}
            </div>
          </div>
        );
      })}
      <div className="flex flex-wrap gap-3 pt-1">
        {allKeys.map((s) => (
          <div key={s.key} className="flex items-center gap-1.5 text-[10px] font-mono text-slate-600">
            <span className="w-2.5 h-2.5 rounded-sm" style={{ background: s.color }} />
            {s.label}
          </div>
        ))}
      </div>
    </div>
  );
}
