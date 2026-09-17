"use client";

import React, { useState } from "react";
import Link from "next/link";
import { usePathname } from "next/navigation";

const NAV_ITEMS = [
  { href: "/", label: "Dashboard" },
  { href: "/compiler", label: "Compiler" },
  { href: "/symbols", label: "Symbol Table" },
  { href: "/scopes", label: "Scopes" },
  { href: "/memory", label: "Memory" },
  { href: "/benchmarks", label: "Benchmarks" },
  { href: "/pareto", label: "Pareto" },
  { href: "/workload", label: "Workload" },
  { href: "/algorithms", label: "Algorithms" },
  { href: "/experiments", label: "Experiments" },
  { href: "/architecture", label: "Architecture" },
  { href: "/research", label: "Research" },
  { href: "/docs", label: "Docs" },
];

const GITHUB_URL = "https://github.com/shubhisingh1510/compiler";

// Minimal mark: three stacked bars of decreasing width, standing in for a
// compact symbol-table index -- deliberately not an "AI" glyph or robot icon.
function Logomark() {
  return (
    <svg width="20" height="20" viewBox="0 0 20 20" fill="none" aria-hidden="true">
      <rect x="2" y="3" width="16" height="3.2" rx="1" fill="#4f46e5" />
      <rect x="2" y="8.4" width="12" height="3.2" rx="1" fill="#0d9488" />
      <rect x="2" y="13.8" width="8" height="3.2" rx="1" fill="#c2410c" />
    </svg>
  );
}

export function Navbar() {
  const pathname = usePathname();
  const [mobileOpen, setMobileOpen] = useState(false);

  return (
    <header className="sticky top-0 z-50 bg-white/85 backdrop-blur border-b border-slate-200 px-4 lg:px-8">
      <div className="max-w-7xl mx-auto">
        <div className="flex items-center justify-between gap-4 py-3">
          {/* Branding */}
          <Link href="/" className="flex items-center gap-2.5 shrink-0">
            <div className="h-9 w-9 rounded-lg bg-gradient-to-br from-violet-50 via-white to-teal-50 border border-slate-200 shadow-sm flex items-center justify-center">
              <Logomark />
            </div>
            <div>
              <div className="flex items-center gap-2">
                <span className="font-mono font-bold text-[15px] text-slate-900 tracking-tight">
                  BUDGET-SYM
                </span>
                <span className="hidden sm:inline px-1.5 py-0.5 rounded-full text-[9px] font-mono font-semibold uppercase tracking-wider bg-teal-50 text-teal-700 border border-teal-200">
                  Prototype
                </span>
              </div>
              <p className="hidden md:block text-[11px] text-slate-500 leading-none mt-0.5">
                Memory-Constrained Compressed Symbol Table for Embedded Compilers
              </p>
            </div>
          </Link>

          {/* GitHub button */}
          <div className="flex items-center gap-2">
            <a
              href={GITHUB_URL}
              target="_blank"
              rel="noreferrer"
              className="hidden sm:flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-medium text-slate-700 border border-slate-200 hover:bg-slate-50 hover:border-slate-300 transition-colors"
            >
              <svg width="14" height="14" viewBox="0 0 16 16" fill="currentColor">
                <path d="M8 0C3.58 0 0 3.58 0 8a8 8 0 0 0 5.47 7.59c.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.01 8.01 0 0 0 16 8c0-4.42-3.58-8-8-8Z" />
              </svg>
              GitHub
            </a>
            <button
              onClick={() => setMobileOpen((v) => !v)}
              className="lg:hidden p-2 rounded-lg border border-slate-200 text-slate-600"
              aria-label="Toggle navigation"
            >
              <svg width="18" height="18" viewBox="0 0 20 20" fill="none">
                <path d="M3 5h14M3 10h14M3 15h14" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" />
              </svg>
            </button>
          </div>
        </div>

        {/* Nav links */}
        <nav
          className={`${mobileOpen ? "flex" : "hidden"} lg:flex flex-col lg:flex-row lg:items-center gap-0.5 lg:gap-1 pb-3 lg:pb-2 overflow-x-auto`}
        >
          {NAV_ITEMS.map((item) => {
            const isActive = pathname === item.href;
            return (
              <Link
                key={item.href}
                href={item.href}
                onClick={() => setMobileOpen(false)}
                className={`px-3 py-1.5 rounded-lg text-[13px] font-medium whitespace-nowrap transition-colors ${
                  isActive
                    ? "bg-indigo-50 text-indigo-700"
                    : "text-slate-600 hover:text-slate-900 hover:bg-slate-50"
                }`}
              >
                {item.label}
              </Link>
            );
          })}
        </nav>
      </div>
    </header>
  );
}
