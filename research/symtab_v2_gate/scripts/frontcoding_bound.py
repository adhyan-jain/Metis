#!/usr/bin/env python3
"""Upper bound on what ANY front-coding tier can save: per TU, distinct names sorted,
bytes saved = sum of LCP with predecessor (ideal, zero-overhead). Compared to the
physical peak heap of the compact ScopedId table and Conventional (from raw/mem_*.csv)."""
import csv, glob, os, statistics as st, sys
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
print("corpus,tus,mean_distinct_names,mean_name_bytes,ideal_frontcoding_saving_bytes,saving_pct_of_name_bytes,saving_pct_of_ScopedId_peak,saving_pct_of_Conv_peak")
for c in ["lua","cjson","protobufc","freertos","redisdeps","redis"]:
    bad={int(r['tu']) for r in csv.DictReader(open(f"traces/{c}/_stats.csv")) if int(r['errors'])>0}
    mem={}
    for r in csv.DictReader(open(f"raw/mem_{c}.csv")):
        mem.setdefault(r['impl'],[]).append(int(r['peak_heap']))
    saves=[];nb=[];nd=[]
    for f in sorted(glob.glob(f"traces/{c}/tu*.trace")):
        if int(os.path.basename(f)[2:6]) in bad: continue
        names=set()
        for l in open(f):
            if l[0]=='D': names.add(l.split()[1])
        s=sorted(names); sv=0
        for a,b in zip(s,s[1:]):
            k=0
            while k<min(len(a),len(b)) and a[k]==b[k]: k+=1
            sv+=k
        saves.append(sv); nb.append(sum(map(len,s))); nd.append(len(s))
    sc=st.mean(mem['ScopedId']); cv=st.mean(mem['Conventional'])
    print(f"{c},{len(saves)},{st.mean(nd):.0f},{st.mean(nb):.0f},{st.mean(saves):.0f},{100*st.mean(saves)/st.mean(nb):.1f},{100*st.mean(saves)/sc:.1f},{100*st.mean(saves)/cv:.1f}")
