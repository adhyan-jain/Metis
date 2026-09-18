### Table 5: Real-World Cold Lookup p95 Latency Ratio vs Conventional (Gate: <= 1.25x)

| Corpus | Conventional (us) | SymTabV3 Ratio | SymTabV4 Ratio | V3 Pass? | V4 Pass? |
|---|---|---|---|---|---|
| Arduino | 0.347 | 0.61x | 1.04x | PASS | PASS |
| CMSIS | 0.242 | 0.97x | 1.95x | PASS | FAIL |
| CPython | 0.259 | 0.95x | 1.33x | PASS | FAIL |
| Clang | 0.203 | 1.96x | 2.61x | FAIL | FAIL |
| ESP-IDF | 0.392 | 0.88x | 1.40x | PASS | FAIL |
| Eigen | 0.232 | 0.83x | 1.09x | PASS | PASS |
| FFmpeg | 0.276 | 0.82x | 1.11x | PASS | PASS |
| FreeRTOS | 0.217 | 1.62x | 3.47x | FAIL | FAIL |
| LLVM | 0.215 | 1.32x | 2.21x | FAIL | FAIL |
| LVGL | 0.924 | 0.38x | 0.59x | PASS | PASS |
| Lua | 0.239 | 0.69x | 0.74x | PASS | PASS |
| MbedTLS2 | 0.231 | 1.28x | 2.47x | FAIL | FAIL |
| Nginx | 0.396 | 0.66x | 0.67x | PASS | PASS |
| OpenThread | 0.285 | 0.90x | 1.35x | PASS | FAIL |
| QEMU | 0.275 | 1.49x | 1.82x | FAIL | FAIL |
| Qt6 | 0.287 | 0.90x | 1.34x | PASS | FAIL |
| Redis | 0.294 | 0.88x | 1.32x | PASS | FAIL |
| SQLite | 0.290 | 0.70x | 0.83x | PASS | PASS |
| TinyUSB | 0.276 | 1.20x | 2.23x | PASS | FAIL |
| Zephyr | 0.449 | 0.89x | 1.33x | PASS | FAIL |
| cJSON | 0.283 | 0.92x | 1.83x | PASS | FAIL |
| curl | 0.363 | 1.20x | 0.95x | PASS | PASS |
| mbedTLS | 0.387 | 0.99x | 1.72x | PASS | FAIL |
| nanopb | 0.384 | 0.47x | 0.43x | PASS | PASS |
| protobuf-c | 0.221 | 1.08x | 2.24x | PASS | FAIL |
| protobuf-generated-cpp | 0.182 | 1.98x | 3.34x | FAIL | FAIL |

**Latency Gate Pass Rate (p95 <= 1.25x Conventional)**: V3 = 20/26, V4 = 9/26
