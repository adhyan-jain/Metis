// Minimal correctness smoke test for all three symbol tables + the adaptive
// mechanism. Not a full unit-test framework (none is available in this
// toolchain) -- asserts + a PASS/FAIL summary, run from build.sh.
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include "../include/conventional_symbol_table.hpp"
#include "../include/interned_symbol_table.hpp"
#include "../include/budget_sym.hpp"
#include "../include/dataset_generators.hpp"
#include "../include/bench_metrics.hpp"
#include "../include/robinhood_symbol_table.hpp"
#include "../include/trie_symbol_table.hpp"
#include "../include/hash_functions.hpp"
#include "../include/metis_x.hpp"

using namespace budgetsym;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL: " #cond " (line " << __LINE__ << ")\n"; failures++; } } while (0)

void test_conventional() {
    ConventionalSymbolTable t(1 << 20);
    t.insert("x");
    t.insert("y");
    CHECK(t.lookup("x"));
    CHECK(t.lookup("y"));
    CHECK(!t.lookup("z"));
    t.enterScope();
    t.insert("x"); // shadow
    t.insert("local1");
    CHECK(t.lookup("local1"));
    auto rep = t.exitScope();
    CHECK(rep.symbolsReleased == 2);
    CHECK(!t.lookup("local1"));
    CHECK(t.lookup("x")); // outer x still visible
}

void test_interned() {
    InternedSymbolTable t(1 << 20);
    t.insert("counter");
    t.insert("counter"); // repeat -> shared pool entry
    CHECK(t.lookup("counter"));
    long long before = t.tracker().current();
    t.enterScope();
    t.insert("counter");
    t.insert("temp");
    CHECK(t.lookup("temp"));
    auto rep = t.exitScope();
    CHECK(rep.symbolsReleased == 2);
    CHECK(!t.lookup("temp"));
    CHECK(t.lookup("counter"));
    long long after = t.tracker().current();
    CHECK(after == before); // scope-local refs to "counter" reclaimed, pool entry itself untouched
}

void test_budgetsym_basic() {
    BudgetSym t(1 << 20);
    t.insert("i");
    t.insert("index");
    t.insert("temperatureThresholdValue");
    CHECK(t.lookup("i"));
    CHECK(t.lookup("index"));
    CHECK(t.lookup("temperatureThresholdValue"));
    CHECK(!t.lookup("doesNotExist"));
}

void test_budgetsym_scope_reclaim() {
    BudgetSym t(1 << 20);
    t.insert("global1");
    t.enterScope();
    for (int i = 0; i < 20; i++) t.insert("nested_local_" + std::to_string(i));
    long long peak = t.tracker().current();
    auto rep = t.exitScope();
    CHECK(rep.symbolsReleased == 20);
    CHECK(rep.bytesReclaimed > 0);
    CHECK(t.tracker().current() < peak);
    CHECK(t.lookup("global1"));
    CHECK(!t.lookup("nested_local_5"));
}

void test_budgetsym_compression_roundtrip() {
    BudgetSym t(1 << 20);
    PolicyConfig cfg = t.config();
    // Force a run of long, prefix-similar identifiers so they get compressed.
    std::vector<std::string> names = {
        "temperatureSensorReading", "temperatureSensorOffset", "temperatureSensorCalibration",
        "temperatureSensorMaxValue", "temperatureSensorMinValue"
    };
    for (auto& n : names) t.insert(n, 0, 0);
    for (auto& n : names) CHECK(t.lookup(n)); // every compressed entry must decode back correctly
    (void)cfg;
}

void test_budgetsym_promotion() {
    PolicyConfig cfg;
    cfg.hotAccessThreshold = 2;
    BudgetSym t(1 << 20, cfg);
    t.insert("veryLongPrefixSimilarIdentifierAlpha");
    t.insert("veryLongPrefixSimilarIdentifierBeta"); // should compress against the first
    auto repBefore = t.representationOf("veryLongPrefixSimilarIdentifierBeta");
    CHECK(repBefore == Representation::COMPRESSED_REP);
    t.lookup("veryLongPrefixSimilarIdentifierBeta");
    t.lookup("veryLongPrefixSimilarIdentifierBeta"); // crosses hotAccessThreshold=2
    auto repAfter = t.representationOf("veryLongPrefixSimilarIdentifierBeta");
    CHECK(repAfter == Representation::INTERNED_REP);
    CHECK(t.promotions() == 1);
    CHECK(t.lookup("veryLongPrefixSimilarIdentifierBeta")); // still findable after promotion
}

// Review-2 addition: per-entry memoized COMPRESSED reconstruction (Phase 1).
// Uses representationOf() rather than lookup() to exercise reconstructFull()
// in isolation -- lookup() also goes through the Phase 2 LRU cache (see
// test_budgetsym_lookup_cache_hit_rate below), which would otherwise short-
// circuit repeat queries before they ever reach the memoized chain walk.
void test_budgetsym_memo_reconstruction() {
    BudgetSym t(1 << 20);
    // Force all three into the same COMPRESSED chain.
    t.insert("temperatureSensorReadingAlpha", 0, 0);
    t.insert("temperatureSensorReadingBeta", 0, 0);
    t.insert("temperatureSensorReadingGamma", 0, 0);
    for (int i = 0; i < 5; i++) {
        CHECK(t.representationOf("temperatureSensorReadingGamma") == Representation::COMPRESSED_REP);
    }
    auto stats = t.statistics();
    CHECK(stats.memoColdLookups == 1);
    CHECK(stats.memoHits == 4);
}

// Review-2 addition: LRU lookup cache (Phase 2).
void test_budgetsym_lookup_cache_hit_rate() {
    BudgetSym t(1 << 20);
    for (int i = 0; i < 10; i++) t.insert("cachedSymbol" + std::to_string(i));
    for (int i = 0; i < 20; i++) CHECK(t.lookup("cachedSymbol0")); // 1 cold miss + 19 hits
    auto stats = t.statistics();
    CHECK(stats.lookupCache.hitRate > 0.9);
}

void test_budgetsym_lookup_cache_invalidation_on_scope_exit() {
    BudgetSym t(1 << 20);
    t.enterScope();
    t.insert("scopedTemp");
    CHECK(t.lookup("scopedTemp"));  // populates the cache
    CHECK(t.statistics().lookupCache.hits + t.statistics().lookupCache.misses > 0);
    t.exitScope();
    CHECK(!t.lookup("scopedTemp")); // must not resurrect a reclaimed symbol via a stale cache entry
}

// Regression test for the cache-capacity fix (docs/latency.md): a hot set
// larger than the cache's old 64-entry capacity used to get a 0% hit rate
// under a cyclic access pattern (LRU thrashing is a hard cliff, not a
// gradual falloff -- confirmed empirically before choosing 256). This
// asserts the fix stays in effect rather than silently regressing if the
// capacity is ever lowered again.
void test_lookup_cache_handles_hot_set_larger_than_old_capacity() {
    PolicyConfig cfg;
    cfg.disableMLThresholdPrediction = true;
    BudgetSym t(64ull * 1024 * 1024, cfg);
    std::vector<std::string> hotNames;
    for (int i = 0; i < 150; i++) {
        hotNames.push_back("hotPathVariableAccessedFrequentlyInLoopBody" + std::to_string(i));
    }
    for (auto& n : hotNames) t.insert(n);
    for (int r = 0; r < 10; r++) {
        for (auto& n : hotNames) t.lookup(n);
    }
    // Theoretical ceiling for this access pattern is one unavoidable cold
    // miss per symbol (150 misses out of 1500 total lookups = 90% hit
    // rate); at the old 64-entry capacity this measured 0%.
    CHECK(t.statistics().lookupCache.hitRate > 0.85);
}

// Review-2 addition: WorkloadProfiler (Phase 3).
void test_workload_profiler_mean_name_length() {
    BudgetSym t(1 << 20);
    // 120 distinct 10-char identifiers -> expected meanNameLength == 10.0.
    for (int i = 0; i < 120; i++) {
        std::string n = "sym" + std::string(7 - std::to_string(i).size(), '0') + std::to_string(i);
        t.insert(n); // "sym" + 7 digits = 10 chars, always distinct
    }
    CHECK(t.profiler().isComplete());
    WorkloadFeatures f = t.profiler().extract();
    CHECK(std::abs(f.meanNameLength - 10.0) < 1.0); // within 10%
}

// Review-2 addition: ML-driven threshold prediction (Phase 4).
void test_threshold_predictor_aggressive_compression_for_high_prefix_workload() {
    WorkloadProfiler profiler;
    for (int i = 0; i < 100; i++) {
        profiler.observe("temperature" + std::to_string(i), 0, false);
    }
    CHECK(profiler.isComplete());
    WorkloadFeatures f = profiler.extract();
    PolicyConfig predicted = ThresholdPredictor::predict(f);
    CHECK(predicted.compressMinLen <= 8);
    CHECK(predicted.prefixSimilarityMinShared <= 3);
}

// End-to-end: BudgetSym itself reconfigures cfg_ once the 100-symbol
// profiling window fills, without any explicit predictor call from the caller.
void test_budgetsym_applies_predicted_thresholds_after_profiling_window() {
    BudgetSym t(1 << 20);
    PolicyConfig before = t.config();
    for (int i = 0; i < 100; i++) t.insert("temperature" + std::to_string(i));
    CHECK(t.profiler().isComplete());
    PolicyConfig after = t.config();
    CHECK(after.compressMinLen != before.compressMinLen || after.prefixSimilarityMinShared != before.prefixSimilarityMinShared);
}

// Review-2 fix: disableMLThresholdPrediction actually holds cfg_ fixed past
// the 100-symbol profiling window (needed so grid_search_main.cpp's swept
// configs, and ablation_main.cpp's NoAccessFrequency variant, aren't silently
// overwritten mid-run by the Phase 4 auto-ML-override).
void test_disable_ml_threshold_prediction_holds_config_fixed() {
    PolicyConfig cfg;
    cfg.hotAccessThreshold = 999; // deliberately distinctive, unlikely to be a predicted value
    cfg.disableMLThresholdPrediction = true;
    BudgetSym t(1 << 20, cfg);
    for (int i = 0; i < 150; i++) t.insert("sym" + std::to_string(i));
    CHECK(t.profiler().isComplete());
    CHECK(t.config().hotAccessThreshold == 999);
}

void test_budgetsym_memory_pressure_selects_compressed() {
    PolicyConfig cfg;
    cfg.highPressureThreshold = 0.0; // force "always high pressure" for this test
    cfg.compressMinLen = 5;
    BudgetSym t(1, cfg); // tiny budget -> pressure() clamps to 1.0 immediately
    int id = t.insert("longIdentifierName", 0, 0);
    (void)id;
    CHECK(t.representationOf("longIdentifierName") == Representation::COMPRESSED_REP);
}

// Review-2 additions: dataset_generators.hpp / bench_metrics.hpp were
// factored out of benchmark_main.cpp so grid_search/multiseed/corpus_bench
// can reuse them -- these checks guard the determinism the whole sweep
// infrastructure depends on.
void test_dataset_generators_deterministic() {
    Dataset a = genUniformRandom("x", 50, 4, 10, 7, 1 << 20);
    Dataset b = genUniformRandom("x", 50, 4, 10, 7, 1 << 20);
    CHECK(a.identifiers == b.identifiers); // same seed -> identical output
    CHECK(a.identifiers.size() == 50);

    // genHighPrefixSimilarity ignores its seed param by design (kept
    // byte-identical to Review-1 benchmark_main.cpp); the seeded sibling
    // must actually vary so multiseed.exe's 30 seeds aren't degenerate.
    Dataset p1 = genHighPrefixSimilaritySeeded(20, 1, 1 << 20);
    Dataset p2 = genHighPrefixSimilaritySeeded(20, 2, 1 << 20);
    CHECK(p1.identifiers != p2.identifiers);
    CHECK(p1.identifiers.size() == 20);
}

void test_bench_metrics_compression_ratio() {
    HiResTimer timer;
    Dataset ds = genUniformRandom("y", 30, 6, 14, 3, 1 << 20);
    ConventionalSymbolTable conv(ds.budgetBytes);
    Metrics convM = runOne(timer, "Conventional", ds, conv, 0);
    BudgetSym budget(ds.budgetBytes);
    Metrics budgetM = runOne(timer, "BudgetSym", ds, budget, convM.memory_bytes);
    CHECK(budgetM.compression_ratio > 0.0);
    CHECK(budgetM.symbols == ds.identifiers.size() || budgetM.symbols <= ds.identifiers.size());
}

// ---- regression test for a real bug found during the merge that added ----
// hash_functions.hpp/RobinHood/Trie on top of the Review-2 lookup cache: the
// LRU lookup cache is keyed by (hash, name) with no scope awareness, so a
// nested-scope declaration that shadows an outer one with the SAME name was
// resolving lookup()'s cache fast path to the stale OUTER entry -- proven by
// forcing the outer "x" to COMPRESSED (promotable) and the shadowing inner
// "x" to INTERNED (not promotable: isRepeat is true for the second "x", so
// decide() takes the exact-repeat branch regardless of any other setting),
// then checking that repeated lookups of "x" from inside the inner scope do
// NOT trigger a promotion -- a promotion firing here can only mean lookup()
// resolved to the (wrong) outer COMPRESSED entry instead of the live inner
// one. Fixed by invalidating the lookup cache's entry for a hash whenever a
// new declaration inserts under that hash (see budget_sym.hpp's insert()).
void test_lookup_cache_respects_scope_shadowing() {
    PolicyConfig cfg;
    cfg.compressMinLen = 1;           // make even "x" eligible for COMPRESSED
    cfg.highPressureThreshold = 0.0;  // force "always high pressure"
    cfg.hotAccessThreshold = 2;       // promote quickly if the bug is present
    cfg.disableMLThresholdPrediction = true; // keep cfg fixed for this test

    BudgetSym t(1 << 20, cfg);
    t.insert("x"); // outer x: isRepeat=false, longId, highPressure -> COMPRESSED_REP
    CHECK(t.representationOf("x") == Representation::COMPRESSED_REP);
    CHECK(t.lookup("x")); // populates the lookup cache for hash("x") -> outer id

    t.enterScope();
    t.insert("x"); // inner x: isRepeat=true -> INTERNED_REP, shadows outer
    CHECK(t.representationOf("x") == Representation::INTERNED_REP);

    t.lookup("x");
    t.lookup("x");
    t.lookup("x");
    // If lookup() incorrectly served the stale cached OUTER (COMPRESSED) id,
    // these three accesses would cross hotAccessThreshold and promote it.
    CHECK(t.promotions() == 0);
}

// ---- new correctness tests: hash-function backends -----------------------

void test_budgetsym_hash_backends_agree() {
    BudgetSymMurmur3 tm(1 << 20);
    BudgetSymDjb2 td(1 << 20);
    tm.insert("i"); tm.insert("index"); tm.insert("temperatureThresholdValue");
    td.insert("i"); td.insert("index"); td.insert("temperatureThresholdValue");
    CHECK(tm.lookup("i") && tm.lookup("index") && tm.lookup("temperatureThresholdValue"));
    CHECK(!tm.lookup("doesNotExist"));
    CHECK(td.lookup("i") && td.lookup("index") && td.lookup("temperatureThresholdValue"));
    CHECK(!td.lookup("doesNotExist"));
}

void test_murmur3_deterministic_and_differs_from_fnv() {
    CHECK(Murmur3Hash::hash("abc") == Murmur3Hash::hash("abc"));
    bool anyDiffer = false;
    for (const std::string& s : {std::string("abc"), std::string("identifier"), std::string("x")}) {
        if (Murmur3Hash::hash(s) != FnvHash::hash(s)) anyDiffer = true;
    }
    CHECK(anyDiffer);
}

// ---- new correctness tests: RobinHood open addressing ---------------------

void test_robinhood_basic_and_scope_reclaim() {
    RobinHoodSymbolTable t(1 << 20);
    t.insert("x");
    t.insert("y");
    CHECK(t.lookup("x") && t.lookup("y"));
    CHECK(!t.lookup("z"));
    t.enterScope();
    t.insert("local1");
    CHECK(t.lookup("local1"));
    auto rep = t.exitScope();
    CHECK(rep.symbolsReleased == 1);
    CHECK(!t.lookup("local1"));
    CHECK(t.lookup("x"));
}

void test_robinhood_survives_resize() {
    RobinHoodSymbolTable t(1 << 20);
    for (int i = 0; i < 300; i++) t.insert("sym_" + std::to_string(i));
    for (int i = 0; i < 300; i++) CHECK(t.lookup("sym_" + std::to_string(i)));
    CHECK(!t.lookup("sym_not_present"));
    CHECK(t.size() == 300);
}

// ---- new correctness tests: Trie storage -----------------------------------

void test_trie_basic_and_prefix_is_not_a_match() {
    TrieSymbolTable t(1 << 20);
    t.insert("temperatureSensorReading");
    t.insert("temperatureSensorOffset");
    CHECK(t.lookup("temperatureSensorReading"));
    CHECK(t.lookup("temperatureSensorOffset"));
    CHECK(!t.lookup("temperatureSensor"));
    CHECK(!t.lookup("temperatureSensorReadingX"));
}

void test_trie_scope_reclaim_and_shared_prefix_survives() {
    TrieSymbolTable t(1 << 20);
    t.insert("shared_global");
    t.enterScope();
    t.insert("shared_global");
    t.insert("local1");
    CHECK(t.lookup("local1"));
    auto rep = t.exitScope();
    CHECK(rep.symbolsReleased == 2);
    CHECK(!t.lookup("local1"));
    CHECK(t.lookup("shared_global"));
}

// ============================================================================
// METIS-X correctness tests (Phase II)
// ============================================================================

void test_metisx_basic() {
    budgetsym::metisx::MetisXTable t;
    t.insert("x");
    t.insert("y");
    CHECK(t.lookup("x"));
    CHECK(t.lookup("y"));
    CHECK(!t.lookup("z"));
    CHECK(t.size() == 2);
}

void test_metisx_scope_reclaim() {
    budgetsym::metisx::MetisXTable t;
    t.insert("global");
    t.enterScope();
    t.insert("local1");
    t.insert("local2");
    CHECK(t.lookup("local1"));
    CHECK(t.lookup("global"));
    auto rep = t.exitScope();
    CHECK(rep.symbolsReleased == 2);
    CHECK(!t.lookup("local1"));
    CHECK(!t.lookup("local2"));
    CHECK(t.lookup("global")); // global scope still alive
}

void test_metisx_shadowing() {
    budgetsym::metisx::MetisXTable t;
    int outer = t.insert("x");
    t.enterScope();
    int inner = t.insert("x"); // shadow
    CHECK(inner != outer);
    CHECK(t.resolve("x") == inner);  // inner binding visible
    t.exitScope();
    CHECK(t.resolve("x") == outer);  // outer restored
}

void test_metisx_same_scope_redeclaration() {
    budgetsym::metisx::MetisXTable t;
    int first  = t.insert("counter");
    int second = t.insert("counter"); // same scope redeclaration
    // resolve() should return the latest id
    CHECK(t.resolve("counter") == second);
    (void)first;
}

void test_metisx_long_name_heap_path() {
    // Names longer than kInlineCap (12) must be stored on heap
    budgetsym::metisx::MetisXTable t;
    std::string longName(32, 'a'); // "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    t.insert(longName);
    CHECK(t.lookup(longName));
    CHECK(!t.lookup(std::string(32, 'b')));
    // Verify slot was correctly stored
    auto st = t.stats();
    CHECK(st.heapNames == 1);
    CHECK(st.inlineNames == 0);
}

void test_metisx_many_symbols() {
    // Insert enough symbols to force at least one rehash (> kInitialSlots * 0.7)
    budgetsym::metisx::MetisXTable t;
    const int N = 200;
    for (int i = 0; i < N; i++) {
        t.insert("sym_" + std::to_string(i));
    }
    CHECK(t.size() == static_cast<size_t>(N));
    for (int i = 0; i < N; i++) {
        CHECK(t.lookup("sym_" + std::to_string(i)));
    }
    CHECK(!t.lookup("sym_9999"));
    // Scope reclaim across many symbols
    t.enterScope();
    for (int i = 0; i < 50; i++) t.insert("inner_" + std::to_string(i));
    auto rep = t.exitScope();
    CHECK(rep.symbolsReleased == 50);
    CHECK(t.size() == static_cast<size_t>(N));
}

int main() {
    test_conventional();
    test_interned();
    test_budgetsym_basic();
    test_budgetsym_scope_reclaim();
    test_budgetsym_compression_roundtrip();
    test_budgetsym_promotion();
    test_budgetsym_memo_reconstruction();
    test_budgetsym_lookup_cache_hit_rate();
    test_budgetsym_lookup_cache_invalidation_on_scope_exit();
    test_lookup_cache_handles_hot_set_larger_than_old_capacity();
    test_workload_profiler_mean_name_length();
    test_threshold_predictor_aggressive_compression_for_high_prefix_workload();
    test_budgetsym_applies_predicted_thresholds_after_profiling_window();
    test_disable_ml_threshold_prediction_holds_config_fixed();
    test_budgetsym_memory_pressure_selects_compressed();
    test_dataset_generators_deterministic();
    test_bench_metrics_compression_ratio();

    test_lookup_cache_respects_scope_shadowing();
    test_budgetsym_hash_backends_agree();
    test_murmur3_deterministic_and_differs_from_fnv();
    test_robinhood_basic_and_scope_reclaim();
    test_robinhood_survives_resize();
    test_trie_basic_and_prefix_is_not_a_match();
    test_trie_scope_reclaim_and_shared_prefix_survives();

    // ---- METIS-X correctness tests (Phase II) --------------------------------
    test_metisx_basic();
    test_metisx_scope_reclaim();
    test_metisx_shadowing();
    test_metisx_same_scope_redeclaration();
    test_metisx_long_name_heap_path();
    test_metisx_many_symbols();

    if (failures == 0) {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    } else {
        std::cout << failures << " TEST(S) FAILED\n";
        return 1;
    }
}
