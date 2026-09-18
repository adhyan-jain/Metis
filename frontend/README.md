# Metis Frontend Dashboard & Live Simulator

This is the Next.js 16 + React 19 + Tailwind CSS web dashboard and client-side simulator for **Metis** (an adaptive scope-aware symbol table for embedded compilers).

## Getting Started

### 1. Install Dependencies
```bash
npm install
```

### 2. Run Development Server
```bash
npm run dev
```

Open [http://localhost:3000](http://localhost:3000) (or `http://localhost:3001` if port 3000 is occupied) in your browser.

### 3. Production Build
```bash
npm run build
npm run start
```

## Features

- **📊 Benchmark Metrics Dashboard**: Interactive bar charts and latency breakdown tables across 8 synthetic compiler datasets.
- **🔬 Ablation Study**: 4-variant mechanism isolation analysis (Scope Stack, Access Frequency, Adaptive Policy).
- **⚡ Live Policy Simulator**: Real-time client-side JS simulator of `decide()` and `maybePromote()` policies with editable C source code presets and policy sliders.
- **📚 Architecture & Theory**: In-depth theoretical explanations, overhead math, decision flowchart, and faculty Q&A defense material.
