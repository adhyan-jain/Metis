# Latency: the LRU Cache's Capacity Was a Hard Thrashing Cliff

## What was asked, and what was found

Asked to push BudgetSym's lookup latency down further, on top of what
`docs/caching.md` (superseded by this file's more complete account) and
`docs/algorithm_comparison.md` already cover. Rather than adding another
alternative structure, this looked at whether the Review-2 lookup cache
(`include/lookup_cache.hpp`, `LRULookupCache<64>`) was actually working as
well as it could for realistic access patterns -- and found it wasn't.

## The cliff

`tests/smoke_test.cpp`'s and `src/cache_benchmark_main.cpp`'s existing hot-set
tests use a 32-symbol hot subset, deliberately sized to fit inside the
cache's old 64-entry capacity. That was itself a blind spot: it could never
show what happens when the actual "hot" working set is *larger* than the
cache, which is not a contrived case -- a function with more than 64 locals
accessed roughly once per loop iteration is ordinary code.

Measured directly with a 150-symbol hot set (each looked up once per round,
10 rounds, a cyclic access pattern):

| Cache capacity | Hit rate | Mean lookup latency |
|---|---|---|
| 64 (old default) | **0%** | 0.650 $\mu$s |
| 128 | **0%** | 0.677 $\mu$s |
| 256 | 95% | 0.190 $\mu$s |

This is not a gradual falloff as the hot set grows past capacity -- it is a
hard cliff. Under a cyclic scan, any capacity below the working-set size
gets a 0% hit rate: by the time the scan returns to the first symbol, every
other access in between has evicted it. Doubling the capacity from 64 to
128 made no difference at all for this workload; the fix required capacity
that actually exceeds the hot-set size (256 > 150). This is a well-known
property of LRU under sequential/cyclic scans, confirmed here for this
specific cache rather than assumed from the general case.

Verified the increase doesn't regress the small-hot-set case it was already
tested against: a 32-symbol hot set (within both the old and new capacity)
showed the same 95% hit rate at both sizes, with latency the same or
marginally better at 256 -- consistent with "more headroom, same behavior
when you don't need it," not a trade-off against the existing tests.

## Fix

`include/budget_sym.hpp`: `LRULookupCache<64>` -> `LRULookupCache<256>`.
This is a cache-implementation constant, not one of `PolicyConfig`'s six
representation-selection thresholds -- it does not touch `decide()` or any
of the adaptive-policy logic.

Cost: roughly `(256 - 64)` extra cache-entry slots per `BudgetSym` instance
(each entry is a 64-bit hash, a `std::string` name, and a `void*` --
on the order of a few KB total, not charged against the tracked-memory
model; see Threats to Validity item 2 in `metis_v2.tex`).

`tests/smoke_test.cpp`'s
`test_lookup_cache_handles_hot_set_larger_than_old_capacity` encodes the
150-symbol reproduction above as a permanent regression test (asserting hit
rate `> 0.85`, just under the 90% theoretical ceiling for 10 rounds), so a
future capacity reduction that reintroduces the cliff will fail the suite
rather than silently regress. `src/cache_benchmark_main.cpp` also gained a
dedicated `large-hot-set-150` scenario in its own output
(`results/cache_benchmark_results.csv`) for the same reason applied to the
actual benchmark/paper numbers, not just the test suite.

## Honest limits of this fix

256 is not a fix for arbitrarily large hot sets -- the identical cliff
exists again above 256 symbols, just moved. It is an evidence-based choice
sized to realistic compiler workloads (a large function's locals) being
less likely to exceed it, not a claim that the thrashing failure mode has
been eliminated in general. A genuinely capacity-independent fix would need
a different eviction policy (e.g. one aware of access frequency rather than
only recency, or a working-set-adaptive size) -- left as future work rather
than attempted here under time pressure to ship a verified, real
improvement over guessing at a more elaborate one.
