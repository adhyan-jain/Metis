# URCM gate specification (transcribed verbatim from the task brief, 2026-10-11)

Disclosure: the criteria below were fixed by the task brief before any measurement. This file was written to disk (and hashed) AFTER the
feasibility screen (`feasibility_screen.py`) had run, because the brief orders the screen first. No criterion was changed after seeing results.

To advance, unified URCM must:
1. zero unexplained lookup mismatches;
2. preserve both function-symbol and source-line lookup capabilities of the baseline;
3. add no static RAM (report stack/scratch);
4. bound sequential event reconstruction to <= B-1 events per B-sized block;
5. reduce combined deployed diagnostic-metadata footprint by >= 10% on >= 3 of 4 workloads (vs the best separate-table baseline);
6. improve whole-image flash >= 0.5% geomean, <= 0.25% regression on any one workload;
7. stay ahead after all decoder/adapter/dictionary/restart/alignment/link costs;
8. have a defensible novelty distinction from the closest prior art.

Screen rule: if a strict optimistic bound on sharing cannot produce the criterion-5 saving, stop before implementing the full architecture.
Workloads: hello_world/mps2_an385, shell_module/mps2_an385, echo_server/mps2_an385, hello_world/nrf52840dk.
