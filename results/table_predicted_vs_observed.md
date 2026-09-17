### Table 6: Predicted vs Observed Break-Even Boundaries

| Workload / Regime | Parameter | Analytical Prediction | Observed Empirical Boundary | Agreement Status |
|---|---|---|---|---|
| Short Names (L <= 15B) | Duplication k | No positive solution (Interned/V3/V4 never beat Conventional) | 0/20 real corpora beat Conventional (Int +21% to +86%, V3 +5% to +40%) | **VERIFIED AGREE** |
| Long Names (L = 32B) | Break-even k | k > (2L+86)/(L+13) = 150/45 = 3.33 | Interned beats Conventional at k >= 3 (peak) / k >= 4 (final) | **VERIFIED AGREE** |
| V4 Side-Table Overhead | Representation Mix | V4 beats V3 iff k_inline * 8B > k_hot * (34-57B) + T_table | V4 loses to V3 on 19/20 corpora and synthetic 90/10 & 70/30 mixes | **VERIFIED AGREE** |
