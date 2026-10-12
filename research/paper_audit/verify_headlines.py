import json, math
import numpy as np, pandas as pd
R = '/home/adhyan/Desktop/Compiler/'
S = '/tmp/'

print("=== V1. METIS-X harness-overhead decomposition (Zephyr, ESP-IDF) ===")
c = pd.read_csv(R + 'results/METIS_X_CANONICAL_DATASET.csv')
a = pd.read_csv(R + 'results/metis_x_ablation.csv')
for w in ['Zephyr', 'ESP-IDF']:
    cc = c[c.workload == w].set_index('implementation').physical_final_heap_bytes
    aa = a[a.corpus == w].set_index('ablation_level').final_heap_bytes
    pairs = {'EmbeddedConventional': 'A3_EmbeddedConventional', 'MetisX': 'A5_FullMetisX', 'ConventionalHost': 'A0_ConventionalHost'}
    off = {k: cc[k] - aa[v] for k, v in pairs.items()}
    print(w, 'canonical(MB):', {k: round(cc[k] / 1e6, 2) for k in pairs},
          '| ablation(MB):', {k: round(aa[v] / 1e6, 2) for k, v in pairs.items()})
    print('   implied constant offset (canonical - ablation) MB:', {k: round(x / 1e6, 2) for k, x in off.items()})
    print('   canonical  MetisX vs EmbConv: %+.1f%% ; vs ConvHost: %+.1f%%' % (
        100 * (cc['MetisX'] / cc['EmbeddedConventional'] - 1), 100 * (cc['MetisX'] / cc['ConventionalHost'] - 1)))
    print('   ablation   MetisX vs EmbConv: %+.1f%% ; vs ConvHost: %+.1f%%' % (
        100 * (aa['A5_FullMetisX'] / aa['A3_EmbeddedConventional'] - 1), 100 * (aa['A5_FullMetisX'] / aa['A0_ConventionalHost'] - 1)))
print("canonical, all workloads: METIS-X vs ConventionalHost / EmbeddedConventional (final heap, p95)")
for w, g in c.groupby('workload'):
    g = g.set_index('implementation')
    f = lambda col, b: 100 * (g.loc['MetisX', col] / g.loc[b, col] - 1)
    print('  %-9s vs ConvHost: mem %+6.1f%% p95 %+6.1f%% | vs EmbConv: mem %+6.1f%% p95 %+6.1f%%' % (
        w, f('physical_final_heap_bytes', 'ConventionalHost'), f('lookup_p95_us', 'ConventionalHost'),
        f('physical_final_heap_bytes', 'EmbeddedConventional'), f('lookup_p95_us', 'EmbeddedConventional')))
print("repetition counts in canonical:", c.groupby('workload').repetition.first().to_dict(), "(one row per impl; no per-rep dispersion stored)")

print("\n=== V2. 26-corpus SymTab variants vs Conventional (data/real_world_benchmark.csv) ===")
r = pd.read_csv(R + 'data/real_world_benchmark.csv')
rows = []
for w, g in r.groupby('corpus'):
    g = g.set_index('implementation')
    mem = lambda i: g.loc[i, 'measured_final_heap_bytes'] / g.loc['Conventional', 'measured_final_heap_bytes']
    rows.append(dict(corpus=w, mem_v2=mem('SymTabV2'), mem_v3=mem('SymTabV3'), mem_v4=mem('SymTabV4'), mem_int=mem('Interned'),
        mem_hs=mem('Conventional-HeapString'),
        p95_v3=g.loc['SymTabV3', 'lookup_p95_us'] / g.loc['Conventional', 'lookup_p95_us'],
        conv_p95_sd=g.loc['Conventional', 'lookup_p95_stddev_us'] / g.loc['Conventional', 'lookup_p95_us'],
        reps=g.loc['Conventional', 'timing_reps']))
t = pd.DataFrame(rows)
print('n corpora', len(t), '| timing_reps values:', sorted(t.reps.unique()))
for k in ['mem_v2', 'mem_v3', 'mem_v4', 'mem_int', 'mem_hs']:
    print('  %-7s <1.0 in %d/%d; min %.3f median %.3f max %.3f' % (k, (t[k] < 1).sum(), len(t), t[k].min(), t[k].median(), t[k].max()))
print('  p95_v3 <=1.25 in %d/%d ; <1.0 in %d' % ((t.p95_v3 <= 1.25).sum(), len(t), (t.p95_v3 < 1).sum()))
print('  joint gate (mem<1 & p95<=1.25):', int(((t.mem_v3 < 1) & (t.p95_v3 <= 1.25)).sum()))
print('  rel. stddev of Conventional p95 across timing reps: median %.1f%% max %.1f%%' % (100 * t.conv_p95_sd.median(), 100 * t.conv_p95_sd.max()))
t.to_csv(S + 'v2_ratios.csv', index=False)

print("\n=== V3. Zephyr symtab: fairness-audit table + tournament geomeans ===")
fa = json.load(open(R + 'results/fairness_audit_results.json'))
for w in fa:
    m = w['methods']; stock = m['B_Stock_Zephyr']['whole_image_flash']
    out = ['%s=%d(%.2f%%)' % (k[:11], v['whole_image_flash'], 100 * (stock - v['whole_image_flash']) / stock)
           for k, v in m.items() if k.startswith(('C_', 'D_', 'E_'))]
    print(w['workload'], 'N=%s stock=%d' % (w['num_symbols'], stock), out)

bl = json.load(open(R + 'research/metis_arch/results/baselines_relinked.json'))
n = pd.read_csv(R + 'research/metis_final_tournament/results/normalized_results.csv')
mm = pd.read_csv(R + 'research/metis_arch/results/normalized_results.csv')
workloads = ['hello_world_mps2', 'shell_module_mps2', 'echo_server_mps2', 'hello_world_nrf52840']
def gm(xs): return math.exp(sum(math.log(x) for x in xs) / len(xs))
def img(tag, w):
    for df in (n, mm):
        x = df[(df.workload == w) & (df.tag == tag)]
        if len(x): return int(x.whole_image.iloc[0])
    raise KeyError((tag, w))
best_exist = {w: min(bl[w][k]['whole_image_bin'] for k in ['kallsyms_m256', 'kallsyms_m16', 'cand_direct', 'cand_cp16']) for w in workloads}
print('best existing (relinked):', best_exist)
def saving(tag, base): return [100 * (base[w] - img(tag, w)) / base[w] for w in workloads]
def gmean_sav(s): return 100 * (1 - gm([1 - x / 100 for x in s]))
for tag in ['RAW32_B16_TOK', 'B1_B16_TOK', 'B1_B16_FC16', 'BLPR_B32', 'ALOB_B32']:
    s = saving(tag, best_exist); print('  vs best existing  %-14s' % tag, ['%.2f' % x for x in s], 'geomean %.2f%%' % gmean_sav(s))
hard_tags = ['B1_B16_TOK', 'B2_B16_TOK', 'B1_B16_KALL', 'B2_B16_KALL']
hard = {w: min(img(t_, w) for t_ in hard_tags if (t_ in set(n.tag) | set(mm.tag))) for w in workloads}
print('address-hardened control (conventional names + compact address index):', hard)
for tag in ['B1_B16_FC16', 'BLPR_B32']:
    s = saving(tag, hard); print('  vs hardened       %-14s' % tag, ['%.2f' % x for x in s], 'geomean %.2f%%' % gmean_sav(s))
tot = [best_exist[w] - img('B1_B16_FC16', w) for w in workloads]
addr = [best_exist[w] - hard[w] for w in workloads]
print('  B1_B16_FC16 bytes saved vs best existing:', tot, '| attained by address index alone:', addr)
print('  share explained by address index alone (sum over workloads): %.0f%%' % (100 * sum(addr) / sum(tot)))
