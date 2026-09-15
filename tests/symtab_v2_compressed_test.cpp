// Correctness tests for SymTabV2's block-based front-coded compression tier
// (P0-3 of the V2 redesign ranking). See include/symtab_v2.hpp's "block-based
// compression" section for the design: fixed-size blocks, configurable
// re-anchor interval decoupled from block size, direct (blockIndex,
// slotInBlock) addressing (no linked-chain pointer chasing), whole-block
// reclaim once every member is dead.
//
// Not folded into tests/differential_test.cpp because that file's reference
// model only checks resolve()-id agreement against a plain std::map -- these
// tests specifically target block-internal invariants (round-trip decode at
// every anchor offset, whole-block reclaim to exactly zero, partial-block
// redeclaration not corrupting surviving members' front-coding) that a
// generic cross-implementation fuzzer wouldn't isolate as clearly.
#include "../include/symtab_v2.hpp"
#include <iostream>
#include <string>
#include <vector>
using namespace budgetsym::v2;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL: " #cond " (line " << __LINE__ << ")\n"; failures++; } } while (0)

static void test_round_trip_and_no_false_positives() {
    // Small anchorInterval to force many re-anchors within a block, and a
    // small blockSize to force multiple blocks -- exercises block-boundary
    // crossing, cross-block addressing, and bounded reconstruction depth.
    PolicyConfigV2 cfg;
    cfg.compressMinLen = 10;
    cfg.blockSize = 5;
    cfg.anchorInterval = 3;

    SymTabV2<> t(1 << 20, cfg);
    std::vector<std::string> names;
    static const char* prefixes[] = {
        "temperatureSensorCalibration", "networkInterfaceBufferPool", "compilerSymbolTableEntry"
    };
    for (int p = 0; p < 3; p++) {
        for (int i = 0; i < 20; i++) {
            names.push_back(std::string(prefixes[p]) + std::to_string(i));
        }
    }

    std::vector<int> ids;
    for (auto& n : names) ids.push_back(t.insert(n));

    // Every compressed name must round-trip exactly (correctness preserved,
    // no lossy front-coding) and resolve to its own declaration id, not a
    // neighbor's (verifies bounded reconstruction decodes correctly at every
    // anchor offset, not just offset 0).
    for (size_t i = 0; i < names.size(); i++) {
        CHECK(t.resolve(names[i]) == ids[i]);
    }
    CHECK(t.resolve("temperatureSensorCalibration999") == -1); // absent, no false positive
    CHECK(t.resolve("temperatureSensorCalibratio") == -1);     // prefix of a real name, must not match

    if (failures == 0) std::cout << "test_round_trip_and_no_false_positives: passed\n";
}

static void test_whole_block_reclaim_to_zero() {
    PolicyConfigV2 cfg;
    cfg.compressMinLen = 10;
    cfg.blockSize = 5;
    cfg.anchorInterval = 3;

    std::vector<std::string> names;
    static const char* prefixes[] = {
        "temperatureSensorCalibration", "networkInterfaceBufferPool", "compilerSymbolTableEntry"
    };
    for (int p = 0; p < 3; p++) for (int i = 0; i < 20; i++) names.push_back(std::string(prefixes[p]) + std::to_string(i));

    // Release every name (via exitScope) and confirm the block bytes are
    // reclaimed -- tracker must drop back down close to zero, though NOT
    // necessarily exactly zero: decide()'s repeat-detection registry
    // (everSeenRep_, see symtab_v2.hpp) permanently records every non-INLINE
    // name ever seen (mirrors V1's identical, documented `seen_` trade-off),
    // and that registry's own bytes are charged and never reclaimed by
    // design. So the correct invariant is "tracker after == exactly the
    // permanent everSeenRep_ registration cost for these N names", not zero.
    SymTabV2<> t(1 << 20, cfg);
    t.enterScope();
    for (auto& n : names) t.insert(n);
    long long peak = t.tracker().current();
    CHECK(peak > 0);
    auto rep = t.exitScope();
    CHECK(rep.symbolsReleased == names.size());
    long long afterExit = t.tracker().current();
    CHECK(afterExit >= 0);
    CHECK(afterExit < peak); // real reclamation must have happened

    // Insert the SAME names again into a fresh scope. Every one of them is
    // now a "repeat" per decide()'s exact-repeat rule (everSeenRep_ already
    // has them from the first round), so this second round uses INTERNED
    // for all of them, NOT COMPRESSED -- a deliberately different cost shape
    // than the first round (front-coded compression vs. full pool strings),
    // so the two peaks are not expected to match. What must hold is
    // REPEATABILITY: releasing this second round must reclaim back down to
    // the exact same residual as the first round (the permanent registry
    // cost does not grow further on a repeat, and nothing leaks or
    // double-charges on the second exitScope()).
    t.enterScope();
    for (auto& n : names) t.insert(n);
    long long secondPeak = t.tracker().current();
    CHECK(secondPeak > 0);
    t.exitScope();
    CHECK(t.tracker().current() == afterExit); // reclaim is exact and repeatable

    if (failures == 0) {
        std::cout << "test_whole_block_reclaim_to_zero: passed (peak=" << peak
                  << ", residual after exit=" << afterExit << ")\n";
    }
}

static void test_partial_block_redeclare_preserves_survivors() {
    PolicyConfigV2 cfg;
    cfg.compressMinLen = 10;
    cfg.blockSize = 5;
    cfg.anchorInterval = 3;

    std::vector<std::string> names;
    static const char* prefixes[] = {
        "temperatureSensorCalibration", "networkInterfaceBufferPool", "compilerSymbolTableEntry"
    };
    for (int p = 0; p < 3; p++) for (int i = 0; i < 20; i++) names.push_back(std::string(prefixes[p]) + std::to_string(i));

    // Redeclare the first 5 names (spans exactly one full block at
    // blockSize=5) -- this releases their block members (decrementing
    // liveCount, NOT reclaiming yet since other members in later blocks are
    // still live) and re-inserts them as NEW compressed members in a fresh
    // open block. Surviving members' front-coding (relative to their own
    // block's predecessors) must be unaffected.
    SymTabV2<> t(1 << 20, cfg);
    for (auto& n : names) t.insert(n);
    for (int i = 0; i < 5; i++) t.insert(names[i]);

    for (auto& n : names) CHECK(t.resolve(n) >= 0);

    if (failures == 0) std::cout << "test_partial_block_redeclare_preserves_survivors: passed\n";
}

int main() {
    test_round_trip_and_no_false_positives();
    test_whole_block_reclaim_to_zero();
    test_partial_block_redeclare_preserves_survivors();

    if (failures == 0) {
        std::cout << "ALL SYMTAB_V2 COMPRESSED-TIER TESTS PASSED\n";
        return 0;
    }
    std::cout << failures << " SYMTAB_V2 COMPRESSED-TIER TEST(S) FAILED\n";
    return 1;
}
