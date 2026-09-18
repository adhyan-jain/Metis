#!/usr/bin/env bash
# One-command demo launcher: builds the C++ core (including analyze.exe, the
# web dashboard's real backend engine) if needed, installs frontend deps if
# needed, then starts the dashboard. Run from the repo root: ./start.sh
set -e

if [ ! -f analyze.exe ]; then
  echo "== analyze.exe not found -- building C++ core first =="
  ./build.sh
fi

cd frontend
if [ ! -d node_modules ]; then
  echo "== Installing frontend dependencies =="
  npm install
fi

echo ""
echo "== Starting Metis dashboard on http://localhost:3000 =="
npm run dev
