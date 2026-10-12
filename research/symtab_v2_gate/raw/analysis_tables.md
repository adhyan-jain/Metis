## Physical heap (allocator-measured, fresh table per TU; ratio = paired per-TU peak ratio, bootstrap 95% CI)

| corpus | TUs | impl | peak KiB (mean) | peak KiB (max TU) | final KiB (mean) | peak ratio vs Conv [95% CI] | peak ratio vs ScopedId |
|---|---|---|---|---|---|---|---|
| lua | 34 | Conventional | 76.2 | 265.2 | 74.5 | 1.000 [1.000,1.000] | 1.131 |
| lua | 34 | Interned | 167.2 | 553.1 | 164.2 | 2.210 [2.154,2.271] | 2.486 |
| lua | 34 | AbslFlat | 78.7 | 249.0 | 53.5 | 1.046 [0.987,1.108] | 1.178 |
| lua | 34 | ScopedId | 67.0 | 220.2 | 59.1 | 0.892 [0.865,0.919] | 1.000 |
| lua | 34 | V2 | 157.3 | 538.6 | 147.0 | 2.098 [2.017,2.182] | 2.362 |
| lua | 34 | V2_noFingerprint | 157.3 | 538.7 | 147.1 | 2.098 [2.017,2.182] | 2.363 |
| lua | 34 | V2_noReclaim | 275.8 | 1397.7 | 232.8 | 3.496 [3.300,3.719] | 3.935 |
| lua | 34 | V2_noSlotRecycle | 273.6 | 1324.3 | 225.6 | 3.469 [3.274,3.678] | 3.890 |
| lua | 34 | V2_fixedInterned | 225.3 | 770.5 | 220.8 | 2.978 [2.891,3.074] | 3.360 |
| lua | 34 | V2_noCompress | 167.8 | 557.5 | 158.1 | 2.248 [2.161,2.339] | 2.530 |
| lua | 34 | V2_compressFirst | 208.7 | 813.0 | 202.8 | 2.712 [2.640,2.791] | 3.058 |
| lua | 34 | V2_noSeenMap_diag | 114.3 | 404.9 | 96.1 | 1.527 [1.448,1.607] | 1.721 |
| lua | 34 | V2_noSeenMap_noCompress_diag | 124.2 | 423.9 | 107.2 | 1.669 [1.586,1.752] | 1.879 |
| lua | 34 | V2_noSeenMap_fixedInterned_diag | 175.6 | 598.3 | 169.9 | 2.330 [2.241,2.425] | 2.626 |
| cjson | 3 | Conventional | 85.0 | 102.2 | 84.1 | 1.000 [1.000,1.000] | 0.984 |
| cjson | 3 | Interned | 193.0 | 230.3 | 192.4 | 2.272 [2.253,2.282] | 2.234 |
| cjson | 3 | AbslFlat | 103.5 | 123.9 | 71.3 | 1.215 [1.204,1.229] | 1.195 |
| cjson | 3 | ScopedId | 86.9 | 107.6 | 73.5 | 1.018 [0.991,1.068] | 1.000 |
| cjson | 3 | V2 | 215.3 | 259.3 | 180.6 | 2.530 [2.501,2.573] | 2.488 |
| cjson | 3 | V2_noFingerprint | 215.4 | 259.3 | 180.7 | 2.531 [2.502,2.573] | 2.489 |
| cjson | 3 | V2_noReclaim | 321.4 | 385.0 | 275.3 | 3.786 [3.728,3.820] | 3.723 |
| cjson | 3 | V2_noSlotRecycle | 311.0 | 373.2 | 253.9 | 3.646 [3.585,3.703] | 3.585 |
| cjson | 3 | V2_fixedInterned | 308.2 | 369.4 | 285.1 | 3.623 [3.608,3.646] | 3.563 |
| cjson | 3 | V2_noCompress | 228.0 | 269.8 | 191.6 | 2.702 [2.640,2.804] | 2.658 |
| cjson | 3 | V2_compressFirst | 286.7 | 344.0 | 253.1 | 3.370 [3.345,3.413] | 3.313 |
| cjson | 3 | V2_noSeenMap_diag | 158.7 | 192.0 | 123.9 | 1.865 [1.837,1.905] | 1.833 |
| cjson | 3 | V2_noSeenMap_noCompress_diag | 171.3 | 202.0 | 134.8 | 2.034 [1.977,2.133] | 2.002 |
| cjson | 3 | V2_noSeenMap_fixedInterned_diag | 251.5 | 301.4 | 228.2 | 2.956 [2.940,2.979] | 2.907 |
| protobufc | 1 | Conventional | 54.9 | 54.9 | 51.8 | 1.000 [1.000,1.000] | 1.030 |
| protobufc | 1 | Interned | 126.3 | 126.3 | 125.0 | 2.299 [2.299,2.299] | 2.367 |
| protobufc | 1 | AbslFlat | 64.6 | 64.6 | 45.3 | 1.177 [1.177,1.177] | 1.212 |
| protobufc | 1 | ScopedId | 53.4 | 53.4 | 49.3 | 0.971 [0.971,0.971] | 1.000 |
| protobufc | 1 | V2 | 147.1 | 147.1 | 125.0 | 2.678 [2.678,2.678] | 2.757 |
| protobufc | 1 | V2_noFingerprint | 147.1 | 147.1 | 125.0 | 2.678 [2.678,2.678] | 2.757 |
| protobufc | 1 | V2_noReclaim | 215.5 | 215.5 | 187.1 | 3.923 [3.923,3.923] | 4.039 |
| protobufc | 1 | V2_noSlotRecycle | 199.4 | 199.4 | 169.0 | 3.630 [3.630,3.630] | 3.738 |
| protobufc | 1 | V2_fixedInterned | 194.2 | 194.2 | 177.3 | 3.535 [3.535,3.535] | 3.640 |
| protobufc | 1 | V2_noCompress | 154.8 | 154.8 | 131.1 | 2.819 [2.819,2.819] | 2.902 |
| protobufc | 1 | V2_compressFirst | 191.2 | 191.2 | 170.1 | 3.481 [3.481,3.481] | 3.584 |
| protobufc | 1 | V2_noSeenMap_diag | 110.9 | 110.9 | 89.7 | 2.019 [2.019,2.019] | 2.079 |
| protobufc | 1 | V2_noSeenMap_noCompress_diag | 118.7 | 118.7 | 95.8 | 2.162 [2.162,2.162] | 2.226 |
| protobufc | 1 | V2_noSeenMap_fixedInterned_diag | 157.7 | 157.7 | 141.8 | 2.870 [2.870,2.870] | 2.955 |
| freertos | 9 | Conventional | 58.9 | 173.2 | 57.9 | 1.000 [1.000,1.000] | 1.144 |
| freertos | 9 | Interned | 128.5 | 337.8 | 128.0 | 2.243 [2.125,2.344] | 2.549 |
| freertos | 9 | AbslFlat | 57.6 | 134.0 | 41.2 | 1.039 [0.894,1.191] | 1.176 |
| freertos | 9 | ScopedId | 49.7 | 120.1 | 43.6 | 0.884 [0.821,0.936] | 1.000 |
| freertos | 9 | V2 | 124.5 | 347.2 | 120.1 | 2.128 [1.977,2.296] | 2.417 |
| freertos | 9 | V2_noFingerprint | 124.6 | 347.3 | 120.2 | 2.129 [1.978,2.297] | 2.418 |
| freertos | 9 | V2_noReclaim | 178.5 | 457.8 | 169.7 | 3.103 [2.793,3.487] | 3.514 |
| freertos | 9 | V2_noSlotRecycle | 173.6 | 437.3 | 158.0 | 3.020 [2.648,3.456] | 3.430 |
| freertos | 9 | V2_fixedInterned | 173.3 | 495.7 | 168.6 | 2.935 [2.754,3.150] | 3.344 |
| freertos | 9 | V2_noCompress | 140.0 | 405.4 | 133.4 | 2.375 [2.213,2.546] | 2.700 |
| freertos | 9 | V2_compressFirst | 155.8 | 434.9 | 152.4 | 2.646 [2.469,2.852] | 3.008 |
| freertos | 9 | V2_noSeenMap_diag | 86.3 | 238.4 | 80.1 | 1.474 [1.324,1.643] | 1.672 |
| freertos | 9 | V2_noSeenMap_noCompress_diag | 101.6 | 296.7 | 93.3 | 1.717 [1.557,1.891] | 1.950 |
| freertos | 9 | V2_noSeenMap_fixedInterned_diag | 134.6 | 387.0 | 128.6 | 2.267 [2.080,2.489] | 2.581 |
| redisdeps | 12 | Conventional | 137.6 | 795.4 | 136.0 | 1.000 [1.000,1.000] | 1.122 |
| redisdeps | 12 | Interned | 280.8 | 1523.3 | 277.4 | 2.150 [2.052,2.260] | 2.393 |
| redisdeps | 12 | AbslFlat | 120.1 | 558.6 | 90.0 | 0.991 [0.880,1.102] | 1.097 |
| redisdeps | 12 | ScopedId | 120.8 | 684.1 | 102.5 | 0.904 [0.845,0.963] | 1.000 |
| redisdeps | 12 | V2 | 286.6 | 1584.8 | 273.9 | 2.126 [2.007,2.268] | 2.369 |
| redisdeps | 12 | V2_noFingerprint | 286.6 | 1584.8 | 273.9 | 2.126 [2.007,2.268] | 2.369 |
| redisdeps | 12 | V2_noReclaim | 471.4 | 3336.0 | 406.6 | 2.799 [2.498,3.144] | 3.167 |
| redisdeps | 12 | V2_noSlotRecycle | 448.0 | 3081.8 | 393.9 | 2.814 [2.446,3.232] | 3.167 |
| redisdeps | 12 | V2_fixedInterned | 408.4 | 2263.1 | 401.5 | 3.034 [2.891,3.201] | 3.388 |
| redisdeps | 12 | V2_noCompress | 352.7 | 2181.7 | 340.3 | 2.365 [2.209,2.537] | 2.640 |
| redisdeps | 12 | V2_compressFirst | 355.1 | 1960.3 | 346.6 | 2.674 [2.541,2.813] | 2.987 |
| redisdeps | 12 | V2_noSeenMap_diag | 200.2 | 1072.5 | 176.8 | 1.527 [1.405,1.658] | 1.701 |
| redisdeps | 12 | V2_noSeenMap_noCompress_diag | 259.8 | 1596.4 | 243.1 | 1.760 [1.621,1.913] | 1.962 |
| redisdeps | 12 | V2_noSeenMap_fixedInterned_diag | 315.2 | 1702.0 | 304.5 | 2.386 [2.236,2.550] | 2.662 |
| redis | 119 | Conventional | 407.7 | 1345.6 | 406.0 | 1.000 [1.000,1.000] | 1.215 |
| redis | 119 | Interned | 854.3 | 2607.0 | 850.6 | 2.131 [2.100,2.166] | 2.561 |
| redis | 119 | AbslFlat | 385.7 | 1079.6 | 280.6 | 0.973 [0.942,1.001] | 1.164 |
| redis | 119 | ScopedId | 333.6 | 900.4 | 297.9 | 0.840 [0.819,0.860] | 1.000 |
| redis | 119 | V2 | 909.9 | 3027.3 | 853.6 | 2.177 [2.138,2.217] | 2.629 |
| redis | 119 | V2_noFingerprint | 909.9 | 3027.3 | 853.6 | 2.177 [2.138,2.217] | 2.629 |
| redis | 119 | V2_noReclaim | 1322.3 | 6393.8 | 1206.2 | 3.223 [3.126,3.321] | 3.864 |
| redis | 119 | V2_noSlotRecycle | 1307.6 | 6022.3 | 1166.1 | 3.152 [3.051,3.257] | 3.796 |
| redis | 119 | V2_fixedInterned | 1257.8 | 4221.7 | 1231.6 | 3.053 [3.015,3.092] | 3.692 |
| redis | 119 | V2_noCompress | 1095.4 | 3876.3 | 1033.6 | 2.543 [2.476,2.607] | 3.070 |
| redis | 119 | V2_compressFirst | 1114.4 | 3894.4 | 1067.3 | 2.717 [2.679,2.755] | 3.281 |
| redis | 119 | V2_noSeenMap_diag | 651.3 | 2079.9 | 566.1 | 1.559 [1.524,1.596] | 1.879 |
| redis | 119 | V2_noSeenMap_noCompress_diag | 834.4 | 2928.5 | 746.1 | 1.919 [1.856,1.980] | 2.311 |
| redis | 119 | V2_noSeenMap_fixedInterned_diag | 992.8 | 3274.4 | 944.2 | 2.405 [2.365,2.445] | 2.903 |

## Latency (host x86-64 i7-13620H P-core, powersave governor; ns after timer-overhead subtraction; mean over reps ± 95% CI over reps)

| corpus | impl | agg ns/event | hit p50 | hit p95 | hit p99 | hit p99.9 | hit max | miss p50 | miss p99 | decl p50 | decl p99 | exit p50 | exit p99 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| lua | Conventional | 129±13 | 93±12 | 253±27 | 351±39 | 735±467 | 12689±4413 | 203±22 | 512±57 | 158±21 | 493±60 | 62±10 | 204±25 |
| lua | Interned | 157±16 | 96±9 | 210±19 | 297±30 | 497±65 | 23501±17266 | 141±15 | 417±49 | 231±25 | 920±91 | 64±9 | 233±21 |
| lua | AbslFlat | 109±11 | 94±12 | 240±21 | 327±26 | 516±132 | 12249±4705 | 162±18 | 427±33 | 107±14 | 370±35 | 53±10 | 142±15 |
| lua | ScopedId | 58±5 | 53±7 | 116±14 | 169±22 | 296±47 | 11170±3517 | 77±10 | 250±35 | 76±10 | 383±48 | 25±6 | 83±10 |
| lua | V2 | 208±22 | 73±10 | 192±21 | 736±78 | 1266±128 | 13989±4139 | 119±14 | 303±34 | 294±35 | 1790±186 | 218±28 | 827±89 |
| lua | V2_noFingerprint | 194±18 | 67±9 | 199±20 | 696±67 | 1207±112 | 12767±4795 | 117±13 | 351±40 | 276±31 | 1689±143 | 203±24 | 769±66 |
| lua | V2_noReclaim | 168±16 | 70±8 | 197±17 | 707±56 | 1309±90 | 13558±3549 | 115±11 | 316±30 | 245±25 | 1923±156 | 43±6 | 109±13 |
| lua | V2_noSlotRecycle | 201±22 | 69±10 | 187±21 | 699±85 | 1251±156 | 55963±60330 | 111±15 | 289±31 | 280±36 | 1860±235 | 205±27 | 751±76 |
| lua | V2_fixedInterned | 239±20 | 72±11 | 169±21 | 242±33 | 427±95 | 163622±228929 | 112±14 | 298±40 | 394±50 | 1569±201 | 296±40 | 1229±141 |
| lua | V2_noCompress | 184±21 | 69±11 | 167±21 | 237±35 | 382±70 | 52140±79246 | 113±15 | 292±45 | 279±37 | 1469±188 | 206±30 | 760±86 |
| lua | V2_compressFirst | 385±41 | 106±12 | 642±64 | 979±97 | 2009±417 | 75135±74823 | 116±13 | 319±32 | 624±68 | 2014±189 | 498±54 | 2009±199 |
| cjson | Conventional | 112±10 | 98±9 | 242±20 | 349±27 | 507±42 | 3094±1481 | 196±17 | 440±36 | 160±17 | 457±35 | 44±8 | 189±16 |
| cjson | Interned | 153±16 | 102±9 | 198±16 | 265±20 | 363±28 | 5473±1527 | 149±14 | 302±27 | 236±25 | 815±83 | 46±8 | 185±17 |
| cjson | AbslFlat | 95±10 | 102±11 | 217±19 | 293±25 | 404±42 | 2595±2024 | 160±16 | 342±29 | 114±13 | 367±36 | 52±7 | 150±17 |
| cjson | ScopedId | 55±5 | 65±6 | 121±11 | 162±14 | 235±20 | 2125±1634 | 81±9 | 199±20 | 78±9 | 337±39 | 25±5 | 78±9 |
| cjson | V2 | 208±20 | 83±9 | 428±42 | 872±88 | 1509±154 | 4471±1478 | 115±12 | 251±26 | 253±28 | 2056±217 | 146±19 | 871±91 |
| cjson | V2_noFingerprint | 213±21 | 85±8 | 437±42 | 915±96 | 1645±188 | 5802±1500 | 123±12 | 301±44 | 259±27 | 2093±233 | 158±18 | 927±95 |
| cjson | V2_noReclaim | 185±17 | 86±9 | 299±31 | 892±87 | 1371±136 | 7999±4359 | 121±11 | 264±37 | 232±25 | 2262±206 | 45±7 | 91±10 |
| cjson | V2_noSlotRecycle | 207±19 | 84±9 | 427±43 | 892±91 | 1621±161 | 5414±1217 | 113±11 | 249±29 | 256±28 | 2086±203 | 156±19 | 900±92 |
| cjson | V2_fixedInterned | 253±24 | 83±8 | 165±14 | 214±18 | 286±30 | 4806±1831 | 112±11 | 254±24 | 362±42 | 1487±148 | 223±28 | 1121±103 |
| cjson | V2_noCompress | 192±18 | 85±9 | 173±15 | 230±21 | 313±41 | 2629±1821 | 116±11 | 267±39 | 261±29 | 1447±145 | 150±19 | 820±84 |
| cjson | V2_compressFirst | 418±35 | 122±11 | 695±61 | 1035±90 | 1640±171 | 6889±1735 | 127±12 | 305±53 | 636±60 | 2107±200 | 426±42 | 1881±157 |
| protobufc | Conventional | 117±7 | 98±6 | 285±23 | 396±34 | 518±51 | 4441±1629 | 222±13 | 442±47 | 155±9 | 429±33 | 42±4 | 200±16 |
| protobufc | Interned | 127±10 | 99±6 | 205±11 | 261±14 | 355±24 | 4722±2024 | 124±8 | 281±18 | 236±17 | 906±75 | 45±4 | 207±13 |
| protobufc | AbslFlat | 110±2 | 106±2 | 234±5 | 295±7 | 382±16 | 3822±2233 | 176±4 | 345±9 | 109±2 | 364±17 | 46±1 | 137±6 |
| protobufc | ScopedId | 61±10 | 61±2 | 117±3 | 144±5 | 193±20 | 1600±1506 | 65±2 | 158±11 | 77±4 | 388±41 | 25±2 | 72±4 |
| protobufc | V2 | 177±11 | 77±4 | 465±21 | 904±51 | 1351±109 | 3912±1076 | 112±5 | 233±12 | 252±17 | 2266±145 | 144±10 | 776±50 |
| protobufc | V2_noFingerprint | 187±14 | 80±7 | 493±39 | 943±78 | 1428±172 | 3776±1115 | 125±10 | 287±45 | 263±23 | 2360±149 | 152±14 | 830±67 |
| protobufc | V2_noReclaim | 163±10 | 82±6 | 464±35 | 983±84 | 1511±144 | 4518±1357 | 119±9 | 242±19 | 234±18 | 2370±159 | 46±4 | 88±8 |
| protobufc | V2_noSlotRecycle | 174±2 | 76±3 | 462±11 | 894±25 | 1371±67 | 4196±1198 | 110±3 | 232±9 | 245±9 | 2198±77 | 143±3 | 789±27 |
| protobufc | V2_fixedInterned | 177±4 | 77±2 | 169±5 | 220±8 | 286±21 | 3176±1551 | 112±3 | 230±10 | 368±10 | 1528±95 | 189±7 | 1213±37 |
| protobufc | V2_noCompress | 147±5 | 77±4 | 175±10 | 231±18 | 290±33 | 2733±1679 | 111±5 | 232±17 | 249±12 | 1350±87 | 139±8 | 740±36 |
| protobufc | V2_compressFirst | 299±18 | 126±7 | 610±34 | 978±52 | 1506±92 | 4825±1089 | 117±6 | 247±15 | 552±24 | 2249±130 | 301±17 | 1766±60 |
| freertos | Conventional | 140±9 | 110±8 | 274±18 | 383±26 | 541±38 | 7384±1309 | 205±15 | 498±37 | 184±16 | 510±35 | 50±6 | 248±20 |
| freertos | Interned | 178±14 | 105±9 | 206±16 | 277±24 | 409±51 | 5341±2063 | 146±13 | 351±37 | 279±27 | 1185±103 | 50±7 | 216±19 |
| freertos | AbslFlat | 117±9 | 105±9 | 243±14 | 317±17 | 438±24 | 6317±1390 | 168±14 | 376±21 | 126±12 | 427±27 | 53±7 | 201±16 |
| freertos | ScopedId | 66±5 | 68±6 | 125±11 | 170±23 | 249±37 | 5569±1430 | 89±8 | 262±34 | 84±8 | 447±40 | 22±4 | 80±8 |
| freertos | V2 | 292±23 | 94±8 | 609±53 | 942±80 | 1370±118 | 6693±926 | 122±10 | 325±31 | 339±31 | 2222±179 | 175±18 | 1872±158 |
| freertos | V2_noFingerprint | 307±21 | 101±8 | 646±48 | 1002±68 | 1463±102 | 8105±1337 | 136±10 | 399±46 | 361±28 | 2365±159 | 185±17 | 1980±147 |
| freertos | V2_noReclaim | 261±22 | 100±10 | 593±55 | 998±89 | 1458±110 | 6943±1258 | 129±11 | 335±34 | 338±35 | 2515±217 | 47±6 | 95±11 |
| freertos | V2_noSlotRecycle | 305±25 | 98±10 | 633±59 | 979±94 | 1348±122 | 7102±2712 | 124±11 | 323±33 | 355±33 | 2320±204 | 180±19 | 1927±173 |
| freertos | V2_fixedInterned | 299±23 | 93±8 | 181±14 | 244±22 | 390±61 | 7594±1842 | 126±10 | 338±29 | 478±46 | 1872±146 | 268±27 | 1608±123 |
| freertos | V2_noCompress | 256±16 | 92±7 | 185±12 | 246±18 | 368±37 | 4965±1865 | 124±8 | 337±30 | 344±29 | 1693±142 | 182±18 | 1411±109 |
| freertos | V2_compressFirst | 461±36 | 128±9 | 775±55 | 1123±75 | 1827±154 | 8739±1871 | 129±9 | 362±29 | 713±55 | 2284±158 | 433±34 | 2550±183 |
| redisdeps | Conventional | 139±15 | 108±12 | 244±25 | 358±37 | 542±54 | 16718±19784 | 233±25 | 620±65 | 185±21 | 547±58 | 68±10 | 236±24 |
| redisdeps | Interned | 201±22 | 110±11 | 223±22 | 327±44 | 1085±1180 | 43955±74976 | 185±19 | 635±72 | 276±31 | 900±131 | 73±10 | 282±27 |
| redisdeps | AbslFlat | 119±13 | 114±9 | 233±16 | 319±23 | 462±41 | 8654±1065 | 213±17 | 542±43 | 154±13 | 454±38 | 77±9 | 192±15 |
| redisdeps | ScopedId | 68±5 | 76±6 | 151±13 | 212±26 | 363±63 | 18676±22314 | 116±11 | 495±47 | 100±9 | 360±42 | 40±5 | 104±9 |
| redisdeps | V2 | 312±48 | 93±11 | 581±63 | 1046±113 | 1586±185 | 17493±18123 | 155±18 | 546±51 | 330±39 | 1863±191 | 210±27 | 1039±111 |
| redisdeps | V2_noFingerprint | 283±27 | 90±8 | 568±49 | 1016±86 | 1532±131 | 8156±840 | 162±15 | 628±53 | 316±31 | 1780±131 | 200±21 | 997±82 |
| redisdeps | V2_noReclaim | 279±33 | 94±11 | 594±65 | 1085±121 | 1677±199 | 40236±30821 | 159±18 | 562±44 | 297±36 | 2314±246 | 61±8 | 110±13 |
| redisdeps | V2_noSlotRecycle | 308±29 | 95±10 | 599±58 | 1091±103 | 1648±150 | 9546±1537 | 160±17 | 566±48 | 342±35 | 2020±191 | 215±23 | 1063±97 |
| redisdeps | V2_fixedInterned | 322±29 | 96±9 | 199±17 | 294±33 | 539±73 | 8438±955 | 159±13 | 558±43 | 475±48 | 1504±130 | 304±31 | 1507±132 |
| redisdeps | V2_noCompress | 284±31 | 93±11 | 194±22 | 270±33 | 466±72 | 8722±2503 | 156±19 | 535±44 | 328±42 | 1431±172 | 211±29 | 954±112 |
| redisdeps | V2_compressFirst | 545±138 | 118±15 | 755±98 | 1607±1156 | 2777±2054 | 90667±166822 | 162±21 | 1086±1138 | 720±90 | 2654±1397 | 527±66 | 3160±1408 |
| redis | Conventional | 137±14 | 111±6 | 258±13 | 427±29 | 838±250 | 371929±601487 | 243±16 | 794±99 | 199±13 | 612±45 | 79±6 | 249±11 |
| redis | Interned | 201±22 | 114±10 | 249±23 | 435±63 | 844±151 | 682560±897140 | 226±25 | 806±115 | 292±25 | 929±105 | 85±8 | 288±25 |
| redis | AbslFlat | 124±13 | 118±11 | 239±17 | 359±27 | 619±54 | 458809±722623 | 215±21 | 580±36 | 169±17 | 493±31 | 87±10 | 202±13 |
| redis | ScopedId | 63±6 | 75±6 | 148±12 | 226±23 | 457±52 | 45339±23708 | 124±11 | 508±35 | 105±9 | 319±27 | 43±5 | 107±8 |
| redis | V2 | 289±27 | 87±8 | 343±43 | 1015±99 | 1759±271 | 385000±491667 | 173±23 | 604±64 | 338±33 | 2109±220 | 215±21 | 1145±113 |
| redis | V2_noFingerprint | 299±19 | 89±7 | 371±30 | 1041±77 | 1745±124 | 82970±87731 | 194±21 | 724±61 | 344±29 | 2108±149 | 222±21 | 1154±82 |
| redis | V2_noReclaim | 268±20 | 89±7 | 344±27 | 1017±71 | 1667±127 | 223136±418122 | 173±15 | 581±46 | 310±25 | 2169±152 | 61±6 | 120±9 |
| redis | V2_noSlotRecycle | 298±28 | 89±9 | 347±35 | 1031±100 | 1710±159 | 531768±762814 | 169±20 | 579±38 | 346±37 | 2143±219 | 225±26 | 1163±115 |
| redis | V2_fixedInterned | 345±22 | 93±10 | 208±23 | 383±62 | 772±126 | 100765±149591 | 180±24 | 592±63 | 475±56 | 1501±175 | 319±38 | 1456±156 |
| redis | V2_noCompress | 287±19 | 97±6 | 220±10 | 380±19 | 831±64 | 55789±26624 | 193±10 | 629±23 | 377±22 | 1641±64 | 249±17 | 1072±41 |
| redis | V2_compressFirst | 478±57 | 112±10 | 729±57 | 1203±90 | 2292±519 | 432536±767056 | 195±20 | 637±42 | 757±69 | 2349±187 | 585±52 | 2619±193 |

## Paired per-rep differences (V2 − other; negative = V2 faster). Same trace, same rep, randomized order.

| corpus | metric | other | mean Δ ns | 95% CI | verdict |
|---|---|---|---|---|---|
| lua | agg_ns_per_event | Conventional | +79.2 | ±18.4 | V2 slower |
| lua | hit_p50_ns | Conventional | -20.3 | ±10.6 | V2 faster |
| lua | hit_p99_ns | Conventional | +384.8 | ±72.2 | V2 slower |
| lua | miss_p50_ns | Conventional | -84.1 | ±19.0 | V2 faster |
| lua | decl_p50_ns | Conventional | +136.4 | ±31.8 | V2 slower |
| lua | exit_p50_ns | Conventional | +156.2 | ±25.3 | V2 slower |
| lua | agg_ns_per_event | ScopedId | +149.4 | ±21.2 | V2 slower |
| lua | hit_p50_ns | ScopedId | +20.0 | ±11.5 | V2 slower |
| lua | hit_p99_ns | ScopedId | +566.9 | ±86.1 | V2 slower |
| lua | miss_p50_ns | ScopedId | +41.5 | ±17.2 | V2 slower |
| lua | decl_p50_ns | ScopedId | +217.9 | ±36.6 | V2 slower |
| lua | exit_p50_ns | ScopedId | +192.9 | ±28.7 | V2 slower |
| lua | agg_ns_per_event | AbslFlat | +98.3 | ±22.1 | V2 slower |
| lua | hit_p50_ns | AbslFlat | -20.9 | ±14.1 | V2 faster |
| lua | hit_p99_ns | AbslFlat | +408.9 | ±83.8 | V2 slower |
| lua | miss_p50_ns | AbslFlat | -43.2 | ±20.9 | V2 faster |
| lua | decl_p50_ns | AbslFlat | +186.9 | ±36.2 | V2 slower |
| lua | exit_p50_ns | AbslFlat | +165.5 | ±28.9 | V2 slower |
| cjson | agg_ns_per_event | Conventional | +96.2 | ±14.1 | V2 slower |
| cjson | hit_p50_ns | Conventional | -15.1 | ±6.4 | V2 faster |
| cjson | hit_p99_ns | Conventional | +522.7 | ±71.8 | V2 slower |
| cjson | miss_p50_ns | Conventional | -80.2 | ±12.5 | V2 faster |
| cjson | decl_p50_ns | Conventional | +92.5 | ±19.4 | V2 slower |
| cjson | exit_p50_ns | Conventional | +101.4 | ±14.0 | V2 slower |
| cjson | agg_ns_per_event | ScopedId | +153.2 | ±17.3 | V2 slower |
| cjson | hit_p50_ns | ScopedId | +18.4 | ±5.4 | V2 slower |
| cjson | hit_p99_ns | ScopedId | +709.7 | ±79.9 | V2 slower |
| cjson | miss_p50_ns | ScopedId | +34.5 | ±7.7 | V2 slower |
| cjson | decl_p50_ns | ScopedId | +174.5 | ±22.2 | V2 slower |
| cjson | exit_p50_ns | ScopedId | +120.4 | ±15.6 | V2 slower |
| cjson | agg_ns_per_event | AbslFlat | +112.4 | ±14.3 | V2 slower |
| cjson | hit_p50_ns | AbslFlat | -18.7 | ±6.6 | V2 faster |
| cjson | hit_p99_ns | AbslFlat | +579.0 | ±72.7 | V2 slower |
| cjson | miss_p50_ns | AbslFlat | -44.8 | ±10.6 | V2 faster |
| cjson | decl_p50_ns | AbslFlat | +138.6 | ±18.7 | V2 slower |
| cjson | exit_p50_ns | AbslFlat | +93.9 | ±13.3 | V2 slower |
| protobufc | agg_ns_per_event | Conventional | +60.3 | ±12.9 | V2 slower |
| protobufc | hit_p50_ns | Conventional | -21.1 | ±6.8 | V2 faster |
| protobufc | hit_p99_ns | Conventional | +508.4 | ±59.0 | V2 slower |
| protobufc | miss_p50_ns | Conventional | -109.9 | ±14.0 | V2 faster |
| protobufc | decl_p50_ns | Conventional | +97.6 | ±17.6 | V2 slower |
| protobufc | exit_p50_ns | Conventional | +102.0 | ±10.4 | V2 slower |
| protobufc | agg_ns_per_event | ScopedId | +116.4 | ±14.9 | V2 slower |
| protobufc | hit_p50_ns | ScopedId | +16.3 | ±4.4 | V2 slower |
| protobufc | hit_p99_ns | ScopedId | +760.3 | ±49.8 | V2 slower |
| protobufc | miss_p50_ns | ScopedId | +47.3 | ±5.1 | V2 slower |
| protobufc | decl_p50_ns | ScopedId | +175.3 | ±17.6 | V2 slower |
| protobufc | exit_p50_ns | ScopedId | +119.7 | ±9.1 | V2 slower |
| protobufc | agg_ns_per_event | AbslFlat | +67.5 | ±10.8 | V2 slower |
| protobufc | hit_p50_ns | AbslFlat | -29.0 | ±4.3 | V2 faster |
| protobufc | hit_p99_ns | AbslFlat | +609.3 | ±51.0 | V2 slower |
| protobufc | miss_p50_ns | AbslFlat | -64.0 | ±5.2 | V2 faster |
| protobufc | decl_p50_ns | AbslFlat | +142.9 | ±16.5 | V2 slower |
| protobufc | exit_p50_ns | AbslFlat | +98.7 | ±9.5 | V2 slower |
| freertos | agg_ns_per_event | Conventional | +152.4 | ±18.2 | V2 slower |
| freertos | hit_p50_ns | Conventional | -16.7 | ±6.1 | V2 faster |
| freertos | hit_p99_ns | Conventional | +558.9 | ±63.0 | V2 slower |
| freertos | miss_p50_ns | Conventional | -83.7 | ±11.2 | V2 faster |
| freertos | decl_p50_ns | Conventional | +155.3 | ±21.9 | V2 slower |
| freertos | exit_p50_ns | Conventional | +125.6 | ±14.4 | V2 slower |
| freertos | agg_ns_per_event | ScopedId | +226.2 | ±19.6 | V2 slower |
| freertos | hit_p50_ns | ScopedId | +26.0 | ±5.8 | V2 slower |
| freertos | hit_p99_ns | ScopedId | +772.6 | ±71.6 | V2 slower |
| freertos | miss_p50_ns | ScopedId | +32.6 | ±6.0 | V2 slower |
| freertos | decl_p50_ns | ScopedId | +254.9 | ±25.2 | V2 slower |
| freertos | exit_p50_ns | ScopedId | +153.1 | ±14.9 | V2 slower |
| freertos | agg_ns_per_event | AbslFlat | +174.9 | ±16.9 | V2 slower |
| freertos | hit_p50_ns | AbslFlat | -11.7 | ±5.6 | V2 faster |
| freertos | hit_p99_ns | AbslFlat | +624.8 | ±68.2 | V2 slower |
| freertos | miss_p50_ns | AbslFlat | -46.7 | ±8.6 | V2 faster |
| freertos | decl_p50_ns | AbslFlat | +213.0 | ±23.1 | V2 slower |
| freertos | exit_p50_ns | AbslFlat | +122.7 | ±13.4 | V2 slower |
| redisdeps | agg_ns_per_event | Conventional | +172.7 | ±38.8 | V2 slower |
| redisdeps | hit_p50_ns | Conventional | -15.2 | ±9.3 | V2 faster |
| redisdeps | hit_p99_ns | Conventional | +688.0 | ±94.7 | V2 slower |
| redisdeps | miss_p50_ns | Conventional | -77.8 | ±17.8 | V2 faster |
| redisdeps | decl_p50_ns | Conventional | +145.0 | ±29.2 | V2 slower |
| redisdeps | exit_p50_ns | Conventional | +142.0 | ±21.5 | V2 slower |
| redisdeps | agg_ns_per_event | ScopedId | +243.3 | ±46.5 | V2 slower |
| redisdeps | hit_p50_ns | ScopedId | +17.0 | ±8.6 | V2 slower |
| redisdeps | hit_p99_ns | ScopedId | +833.9 | ±103.2 | V2 slower |
| redisdeps | miss_p50_ns | ScopedId | +39.5 | ±15.1 | V2 slower |
| redisdeps | decl_p50_ns | ScopedId | +230.3 | ±34.9 | V2 slower |
| redisdeps | exit_p50_ns | ScopedId | +169.9 | ±24.5 | V2 slower |
| redisdeps | agg_ns_per_event | AbslFlat | +192.3 | ±46.6 | V2 slower |
| redisdeps | hit_p50_ns | AbslFlat | -21.6 | ±13.4 | V2 faster |
| redisdeps | hit_p99_ns | AbslFlat | +727.6 | ±114.5 | V2 slower |
| redisdeps | miss_p50_ns | AbslFlat | -58.0 | ±24.8 | V2 faster |
| redisdeps | decl_p50_ns | AbslFlat | +175.8 | ±40.1 | V2 slower |
| redisdeps | exit_p50_ns | AbslFlat | +132.7 | ±27.3 | V2 slower |
| redis | agg_ns_per_event | Conventional | +151.7 | ±24.8 | V2 slower |
| redis | hit_p50_ns | Conventional | -24.4 | ±10.0 | V2 faster |
| redis | hit_p99_ns | Conventional | +588.1 | ±105.9 | V2 slower |
| redis | miss_p50_ns | Conventional | -70.5 | ±22.6 | V2 faster |
| redis | decl_p50_ns | Conventional | +139.2 | ±31.9 | V2 slower |
| redis | exit_p50_ns | Conventional | +136.0 | ±20.1 | V2 slower |
| redis | agg_ns_per_event | ScopedId | +226.0 | ±25.0 | V2 slower |
| redis | hit_p50_ns | ScopedId | +11.5 | ±5.7 | V2 slower |
| redis | hit_p99_ns | ScopedId | +789.6 | ±87.4 | V2 slower |
| redis | miss_p50_ns | ScopedId | +48.0 | ±17.9 | V2 slower |
| redis | decl_p50_ns | ScopedId | +233.6 | ±27.5 | V2 slower |
| redis | exit_p50_ns | ScopedId | +172.2 | ±17.7 | V2 slower |
| redis | agg_ns_per_event | AbslFlat | +164.8 | ±26.6 | V2 slower |
| redis | hit_p50_ns | AbslFlat | -31.4 | ±12.9 | V2 faster |
| redis | hit_p99_ns | AbslFlat | +656.1 | ±91.8 | V2 slower |
| redis | miss_p50_ns | AbslFlat | -42.9 | ±22.7 | V2 faster |
| redis | decl_p50_ns | AbslFlat | +169.7 | ±32.0 | V2 slower |
| redis | exit_p50_ns | AbslFlat | +127.9 | ±20.2 | V2 slower |