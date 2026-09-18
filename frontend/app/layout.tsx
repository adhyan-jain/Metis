import type { Metadata } from "next";
import { Geist, Geist_Mono } from "next/font/google";
import "./globals.css";
import { Navbar } from "../components/Navbar";
import { AnalysisProvider } from "../lib/AnalysisContext";

const geistSans = Geist({
  variable: "--font-geist-sans",
  subsets: ["latin"],
});

const geistMono = Geist_Mono({
  variable: "--font-geist-mono",
  subsets: ["latin"],
});

export const metadata: Metadata = {
  title: "Metis — Adaptive Symbol Table Research Dashboard",
  description:
    "An adaptive, scope-aware symbol table for memory-bounded embedded compilation. Live benchmark metrics, ablation study, and a real compiler-backed playground.",
};

export default function RootLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  return (
    <html lang="en" className={`${geistSans.variable} ${geistMono.variable} h-full antialiased`}>
      <body className="min-h-full flex flex-col bg-[var(--background)] text-slate-900 font-sans selection:bg-indigo-100 selection:text-indigo-900">
        <AnalysisProvider>
          <Navbar />
          <main className="flex-1 w-full max-w-7xl mx-auto px-4 lg:px-8 py-8">
            {children}
          </main>
          <footer className="border-t border-slate-200 bg-white/60 text-slate-400 py-6 px-4 text-center font-mono text-xs">
            <div className="max-w-7xl mx-auto flex flex-col sm:flex-row items-center justify-between gap-3">
              <div>Metis — Adaptive Symbol Table Research Prototype</div>
              <div className="flex items-center gap-3 text-[11px]">
                <span>Real data from results/*.csv</span>
                <span>·</span>
                <span>C++14 engine via /api/analyze</span>
              </div>
            </div>
          </footer>
        </AnalysisProvider>
      </body>
    </html>
  );
}
