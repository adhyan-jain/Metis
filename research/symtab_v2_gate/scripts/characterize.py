#!/usr/bin/env python3
"""Per-corpus semantic characterization of libclang traces + semantic validity checks."""
import csv, glob, os, sys, math, collections
out = []
for corpus in sys.argv[1:]:
    skip = {int(x['tu']) for x in csv.DictReader(open(f"traces/{corpus}/_stats.csv")) if int(x['errors']) > 0}
    nd = nu = nredecl = nshadow = unresolved = 0; lens = []; uniq = set(); depths = []; per_tu_unique = []
    maxd = 0
    cnt = collections.Counter()
    for i, f in enumerate(sorted(glob.glob(f"traces/{corpus}/tu*.trace"))):
        if int(os.path.basename(f)[2:6]) in skip: continue
        stack = [dict()]; tu_names = set()
        for line in open(f):
            p = line.split()
            if p[0] == 'E': stack.append(dict())
            elif p[0] == 'X': stack.pop()
            elif p[0] == 'D':
                n = p[1]; nd += 1; lens.append(len(n)); uniq.add(n); tu_names.add(n); cnt[n] += 1
                if n in stack[-1]: nredecl += 1
                elif any(n in s for s in stack[:-1]): nshadow += 1
                stack[-1][n] = 1; depths.append(len(stack) - 1); maxd = max(maxd, len(stack) - 1)
            else:
                n = p[1]; nu += 1; cnt[n] += 1
                if not any(n in s for s in stack): unresolved += 1
        per_tu_unique.append(len(tu_names))
    ml = sum(lens) / len(lens)
    tot = sum(cnt.values()); gi = sorted(cnt.values()); n = len(gi)
    gini = (2 * sum((k + 1) * c for k, c in enumerate(gi)) / (n * tot)) - (n + 1) / n
    out.append((corpus, nd, nu, len(uniq), round(ml, 2), maxd, round(sum(depths)/len(depths), 2), nredecl, nshadow, unresolved, round(gini, 3), len(per_tu_unique)))
print("corpus,decls,uses,unique_names,mean_len,max_depth,mean_decl_depth,same_scope_redecl,shadowing_decls,unresolved_uses,gini,tus")
for o in out: print(",".join(map(str, o)))
