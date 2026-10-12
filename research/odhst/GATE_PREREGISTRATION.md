# ODHST pilot gate — pre-registered before any ODHST measurement (2026-10-11)

Denominator: whole-image flash bytes (text+rodata+data, as in results/fairness_audit_closure_results.json).
"Best existing fair method" per workload = min whole-image flash over {Kallsyms M=256, Kallsyms M=16,
Candidate K=1, Candidate K=16} from the frozen closure results (stock excluded as it is dominated).
Saving% = (best_existing - odhst) / best_existing.
Gate: (1) >=5% saving on >=3 workloads incl. echo_server; (2) no remaining workload >1% worse;
(3) 0 static RAM, same API, all tested symbols correct, <=15 record reconstructions at B=16;
(4) savings measured on full firmware including ID table, checkpoints, dictionary, decoder, adapter, alignment.
Offline payload size is only a lower bound on feature bytes and is never used to declare a winner,
but a payload-only lower bound that already fails the gate is sufficient to declare NO-GO for that workload.
