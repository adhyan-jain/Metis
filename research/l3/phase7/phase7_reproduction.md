# Phase 7: Artifact Reproduction Guide

**Date:** October 10, 2026
**Repository:** `adhyan-jain/Metis`

This guide explains how to reproduce the canonical dataset and the derived paper tables for the METIS-X systems paper.

## 1. Trace Assets
The evaluation relies on compiler AST event traces extracted from embedded codebases:
- `data/corpus_events_Zephyr.txt`
- `data/corpus_events_ESP-IDF.txt`
Ensure these files are present and match the hashes from Phase 2.

## 2. Benchmark Execution
The canonical dataset `results/metis_x_ablation.csv` was generated using the Phase 2 embedded benchmark harness.
To re-run the benchmark and generate new CSV data:
```bash
make clean
make metis_x_bench
taskset -c 0 ./metis_x_bench 5 results/metis_x_ablation_new.csv
```
*(Note: Hardware jitter may cause minor variations in $p_{95}$ latency on different execution hosts.)*

## 3. Paper Table Generation
The headline table in the manuscript (`table_1_results.tex`) is deterministically generated from the canonical CSV using a Python script.
To regenerate the LaTeX table:
```bash
python3 research/l3/phase7/generate_tables.py
```
This script reads `results/metis_x_ablation.csv`, converts bytes to MB, and formats the output into `research/l3/phase7/table_1_results.tex`.

## 4. Manuscript Compilation
To compile the manuscript draft into a PDF:
```bash
cd research/l3/phase7/
pdflatex metis_x_paper.tex
bibtex metis_x_paper
pdflatex metis_x_paper.tex
pdflatex metis_x_paper.tex
```
Ensure you have a standard TeX Live distribution with the `acmart` document class installed.
