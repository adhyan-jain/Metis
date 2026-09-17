"use client";

import React, { useState } from "react";

export interface ScatterPoint {
  x: number;
  y: number;
  series: string;
  label: string;
}

const SERIES_COLORS: Record<string, string> = {
  Conventional: "#94a3b8",
  Interned: "#818cf8",
  BudgetSymV1: "#fb7185",
  SymTabV2: "#14b8a6",
};

const WIDTH = 640;
const HEIGHT = 360;
const MARGIN = { top: 16, right: 16, bottom: 40, left: 64 };

export function ScatterChart({
  points,
  xLabel,
  yLabel,
  formatX,
  formatY,
}: {
  points: ScatterPoint[];
  xLabel: string;
  yLabel: string;
  formatX: (v: number) => string;
  formatY: (v: number) => string;
}) {
  const [hovered, setHovered] = useState<ScatterPoint | null>(null);

  if (points.length === 0) {
    return <div className="text-xs text-slate-400 py-12 text-center">No data available for this selection.</div>;
  }

  const xMax = Math.max(...points.map((p) => p.x)) * 1.1 || 1;
  const yMax = Math.max(...points.map((p) => p.y)) * 1.1 || 1;
  const innerW = WIDTH - MARGIN.left - MARGIN.right;
  const innerH = HEIGHT - MARGIN.top - MARGIN.bottom;

  const px = (x: number) => MARGIN.left + (x / xMax) * innerW;
  const py = (y: number) => MARGIN.top + innerH - (y / yMax) * innerH;

  const series = Array.from(new Set(points.map((p) => p.series)));
  const xTicks = 5;
  const yTicks = 5;

  return (
    <div className="w-full overflow-x-auto">
      <svg viewBox={`0 0 ${WIDTH} ${HEIGHT}`} className="w-full min-w-[480px]" role="img" aria-label={`Scatter plot of ${yLabel} vs ${xLabel}`}>
        {/* gridlines + y-axis ticks */}
        {Array.from({ length: yTicks + 1 }, (_, i) => {
          const val = (yMax / yTicks) * i;
          const y = py(val);
          return (
            <g key={`y-${i}`}>
              <line x1={MARGIN.left} y1={y} x2={WIDTH - MARGIN.right} y2={y} stroke="#e2e8f0" strokeWidth={1} />
              <text x={MARGIN.left - 8} y={y + 3} textAnchor="end" fontSize={9} fill="#64748b" fontFamily="monospace">
                {formatY(val)}
              </text>
            </g>
          );
        })}
        {/* x-axis ticks */}
        {Array.from({ length: xTicks + 1 }, (_, i) => {
          const val = (xMax / xTicks) * i;
          const x = px(val);
          return (
            <text key={`x-${i}`} x={x} y={HEIGHT - MARGIN.bottom + 16} textAnchor="middle" fontSize={9} fill="#64748b" fontFamily="monospace">
              {formatX(val)}
            </text>
          );
        })}
        {/* axis labels */}
        <text x={MARGIN.left + innerW / 2} y={HEIGHT - 6} textAnchor="middle" fontSize={10} fill="#334155" fontWeight={600}>
          {xLabel}
        </text>
        <text x={12} y={MARGIN.top + innerH / 2} textAnchor="middle" fontSize={10} fill="#334155" fontWeight={600} transform={`rotate(-90 12 ${MARGIN.top + innerH / 2})`}>
          {yLabel}
        </text>
        {/* points */}
        {points.map((p, i) => (
          <circle
            key={i}
            cx={px(p.x)}
            cy={py(p.y)}
            r={hovered === p ? 6 : 4}
            fill={SERIES_COLORS[p.series] ?? "#475569"}
            fillOpacity={0.85}
            stroke="white"
            strokeWidth={1}
            onMouseEnter={() => setHovered(p)}
            onMouseLeave={() => setHovered(null)}
          />
        ))}
        {/* tooltip */}
        {hovered && (
          <g transform={`translate(${Math.min(px(hovered.x) + 10, WIDTH - 170)}, ${Math.max(py(hovered.y) - 34, 4)})`}>
            <rect width={160} height={34} rx={6} fill="#0f172a" fillOpacity={0.92} />
            <text x={8} y={14} fontSize={9} fill="white" fontFamily="monospace">{hovered.label} · {hovered.series}</text>
            <text x={8} y={26} fontSize={9} fill="#cbd5e1" fontFamily="monospace">{formatX(hovered.x)} · {formatY(hovered.y)}</text>
          </g>
        )}
      </svg>
      <div className="flex flex-wrap gap-3 mt-2 px-2">
        {series.map((s) => (
          <div key={s} className="flex items-center gap-1.5 text-[10px] font-mono text-slate-600">
            <span className="w-2.5 h-2.5 rounded-full" style={{ background: SERIES_COLORS[s] ?? "#475569" }} />
            {s}
          </div>
        ))}
      </div>
    </div>
  );
}
