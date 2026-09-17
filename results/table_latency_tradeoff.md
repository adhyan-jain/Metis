### Table 5: Real-World Cold Lookup p95 Latency Ratio vs Conventional (Gate: <= 1.25x)

| Corpus | Conventional (us) | SymTabV3 Ratio | SymTabV4 Ratio | V3 Pass? | V4 Pass? |
|---|---|---|---|---|---|
| Arduino | 0.197 | 1.40x | 1.44x | FAIL | FAIL |
| CPython | 0.172 | 1.28x | 1.98x | FAIL | FAIL |
| Clang | 0.171 | 2.19x | 2.25x | FAIL | FAIL |
| ESP-IDF | 0.252 | 1.77x | 1.89x | FAIL | FAIL |
| Eigen | 0.257 | 0.65x | 0.75x | PASS | PASS |
| FFmpeg | 0.216 | 0.79x | 0.97x | PASS | PASS |
| FreeRTOS | 0.201 | 2.54x | 2.84x | FAIL | FAIL |
| LLVM | 0.156 | 3.02x | 3.08x | FAIL | FAIL |
| Lua | 0.209 | 0.67x | 0.66x | PASS | PASS |
| Nginx | 0.328 | 0.56x | 0.61x | PASS | PASS |
| QEMU | 0.231 | 1.27x | 1.60x | FAIL | FAIL |
| Qt6 | 0.207 | 1.65x | 1.54x | FAIL | FAIL |
| Redis | 0.173 | 2.05x | 1.74x | FAIL | FAIL |
| SQLite | 0.218 | 0.82x | 0.71x | PASS | PASS |
| Zephyr | 0.389 | 1.22x | 1.43x | PASS | FAIL |
| cJSON | 0.147 | 2.84x | 2.99x | FAIL | FAIL |
| curl | 0.265 | 0.99x | 0.92x | PASS | PASS |
| mbedTLS | 0.246 | 2.47x | 2.59x | FAIL | FAIL |
| protobuf-c | 0.182 | 2.19x | 2.32x | FAIL | FAIL |
| protobuf-generated-cpp | 0.191 | 2.36x | 2.21x | FAIL | FAIL |

**Latency Gate Pass Rate (p95 <= 1.25x Conventional)**: V3 = 7/20, V4 = 6/20
