# LLEXT compact-ID gate (frozen 2026-10-11, before any LLEXT measurement)
Denominator: total symbol-binding bytes = base-firmware export data (names/SLIDs/index/tables) + all shipped extension bytes
that exist only for symbol binding (import/export names or IDs, symtab/strtab entries used for binding, relocation entries that reference symbols, manifest/fingerprint) -- for the
SAME bundle, same toolchain, same loader contract. Reduction % = (SLID_total - compact_total)/SLID_total, compact_total includes loader logic delta, manifest, fingerprint, fallback path, alignment.
GO requires BOTH: (1) a credible research distinction after prior-art review; (2) >=5% reduction on >=3 realistic workloads/bundles with no weakening of loader compatibility/correctness (incompatible ABI rejected at load time).
Toy-only modules => no general claims. Failing either => NO-GO and stop; no new architecture in this run.
