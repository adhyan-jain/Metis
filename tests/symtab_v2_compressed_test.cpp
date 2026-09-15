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

// Forced hash-collision test for P0-2 (fingerprint completion): a constant
// hash function makes EVERY string collide on both ScopeIndex's 32-bit
// fingerprint AND BlockMember's 8-bit fp8 (both derived from the same
// HashFn::hash() output -- see symtab_v2.hpp's fingerprint()/fingerprint8()),
// so both cheap rejection checks are maximally defeated for every candidate.
// Correctness must still hold: the final decode-and-compare in nameEquals()
// is the only real arbiter, and fp8 is a rejection-only fast path that must
// never cause a false negative (reject a real match) or a false positive
// (accept a wrong match) -- only skip or not skip the expensive decode.
struct ConstantHash {
    static uint64_t hash(const std::string&) { return 0xABCDEFABCDEFULL; }
};

static void test_forced_hash_collision_still_resolves_correctly() {
    PolicyConfigV2 cfg;
    cfg.compressMinLen = 10; // force these into the COMPRESSED tier, where fp8 lives
    SymTabV2<ConstantHash> t(1 << 20, cfg);

    std::vector<std::string> names = {
        "temperatureSensorCalibrationAlpha", "networkInterfaceBufferPoolBeta",
        "compilerSymbolTableEntryGamma", "userAuthenticationTokenDelta",
        "moduleConfigParameterEpsilon"
    };
    std::vector<int> ids;
    for (auto& n : names) ids.push_back(t.insert(n));

    // Every one of these collides on BOTH fingerprints (same constant hash),
    // yet each must still resolve to its OWN id, not a colliding neighbor's,
    // and an absent name sharing the same fingerprints must still report -1.
    for (size_t i = 0; i < names.size(); i++) {
        CHECK(t.resolve(names[i]) == ids[i]);
    }
    CHECK(t.resolve("thisNameWasNeverInserted12345") == -1);

    if (failures == 0) std::cout << "test_forced_hash_collision_still_resolves_correctly: passed\n";
}

// P0.1 architecture audit gap (docs/v2_architecture.md, section 10, item 1):
// SymTabV2 had no NAMED shadowing/nested-scope/absent-symbol regression test
// of its own -- only transitive coverage via the differential fuzz harness.
// This targets SymTabV2 directly, on names long enough to hit the COMPRESSED
// tier (the representation whose fingerprint-assisted lookup path this file
// is otherwise dedicated to validating), so shadowing is checked against the
// same fp8/reconstruction code path, not just the INLINE/short-name fast path
// a shorter test name would hit.
static void test_shadowing_nested_scope_and_absent_symbol() {
    PolicyConfigV2 cfg;
    cfg.compressMinLen = 10;
    cfg.blockSize = 5;
    cfg.anchorInterval = 3;

    SymTabV2<> t(1 << 20, cfg);
    int outer = t.insert("temperatureSensorCalibration");
    CHECK(t.resolve("temperatureSensorCalibration") == outer);
    CHECK(t.representationOf("temperatureSensorCalibration") == Rep::COMPRESSED_REP);

    // Absent symbol before any shadowing is introduced.
    CHECK(t.resolve("networkInterfaceBufferPool") == -1);

    t.enterScope();
    // Nested scope, no shadowing yet: outer binding must still resolve.
    CHECK(t.resolve("temperatureSensorCalibration") == outer);

    int inner = t.insert("temperatureSensorCalibration"); // shadows outer
    CHECK(inner != outer);
    CHECK(t.resolve("temperatureSensorCalibration") == inner); // inner wins
    CHECK(t.resolve("temperatureSensorCalibration") == inner); // repeated resolve stays correct (no stale state)

    // A second, deeper nested scope: the shadowed outer binding must remain
    // invisible while inner is live.
    t.enterScope();
    CHECK(t.resolve("temperatureSensorCalibration") == inner);
    CHECK(t.resolve("thisNameWasNeverDeclaredAnywhere") == -1); // absent symbol, nested
    t.exitScope();

    t.exitScope(); // inner scope exits; shadow is removed
    CHECK(t.resolve("temperatureSensorCalibration") == outer); // outer binding visible again
    CHECK(t.resolve("thisNameWasNeverDeclaredAnywhere") == -1);

    if (failures == 0) std::cout << "test_shadowing_nested_scope_and_absent_symbol: passed\n";
}

// P0-3 (V2 research-rewrite plan): hot/cold promotion. A COMPRESSED entry
// resolved past cfg.hotAccessThreshold must be promoted to INTERNED in
// place, without disturbing its own declaration id, its neighbors in the
// same block, or any name that never crossed the threshold.
static void test_promotion_on_hot_access() {
    PolicyConfigV2 cfg;
    cfg.compressMinLen = 10;
    cfg.blockSize = 5;
    cfg.anchorInterval = 3;
    cfg.hotAccessThreshold = 3;

    SymTabV2<> t(1 << 20, cfg);
    std::vector<std::string> names;
    static const char* prefixes[] = {"temperatureSensorCalibration", "networkInterfaceBufferPool"};
    for (int p = 0; p < 2; p++) for (int i = 0; i < 5; i++) names.push_back(std::string(prefixes[p]) + std::to_string(i));

    std::vector<int> ids;
    for (auto& n : names) ids.push_back(t.insert(n));
    for (auto& n : names) CHECK(t.representationOf(n) == Rep::COMPRESSED_REP);
    CHECK(t.promotions() == 0);

    // Resolve only names[2] enough times to cross the threshold. Its own
    // block neighbors (names[0],[1],[3],[4], all still COMPRESSED) and every
    // other name (a second, separate block) must be undisturbed.
    const std::string& hot = names[2];
    for (size_t i = 0; i < cfg.hotAccessThreshold; i++) {
        int r = t.resolve(hot);
        CHECK(r == ids[2]); // declaration id must never change across promotion
    }
    CHECK(t.representationOf(hot) == Rep::INTERNED_REP);
    CHECK(t.promotions() == 1);

    for (size_t i = 0; i < names.size(); i++) {
        if (i == 2) continue;
        CHECK(t.representationOf(names[i]) == Rep::COMPRESSED_REP); // untouched
        CHECK(t.resolve(names[i]) == ids[i]); // still round-trips correctly
    }
    CHECK(t.resolve(hot) == ids[2]); // still resolves correctly as INTERNED too

    if (failures == 0) std::cout << "test_promotion_on_hot_access: passed\n";
}

// P0-4: cold demotion. A promoted (COMPRESSED->INTERNED) entry that goes
// idle for cfg.coldIdleEpochs resolve()-epochs must be demoted back to
// COMPRESSED by an explicit runMaintenance() call -- never automatically,
// and never before the idle threshold is reached.
static void test_demotion_on_idle_after_maintenance() {
    PolicyConfigV2 cfg;
    cfg.compressMinLen = 10;
    cfg.blockSize = 5;
    cfg.anchorInterval = 3;
    cfg.hotAccessThreshold = 2;
    cfg.coldIdleEpochs = 5;

    SymTabV2<> t(1 << 20, cfg);
    std::vector<std::string> names;
    static const char* prefixes[] = {"temperatureSensorCalibration", "networkInterfaceBufferPool"};
    for (int p = 0; p < 2; p++) for (int i = 0; i < 5; i++) names.push_back(std::string(prefixes[p]) + std::to_string(i));
    std::vector<int> ids;
    for (auto& n : names) ids.push_back(t.insert(n));

    const std::string& hot = names[2];
    for (size_t i = 0; i < cfg.hotAccessThreshold; i++) t.resolve(hot);
    CHECK(t.representationOf(hot) == Rep::INTERNED_REP);
    CHECK(t.promotions() == 1);

    // Not yet idle long enough: maintenance must not demote it.
    CHECK(t.runMaintenance() == 0);
    CHECK(t.representationOf(hot) == Rep::INTERNED_REP);

    // Advance the epoch clock past coldIdleEpochs via unrelated lookups
    // (each resolve() call -- hit or miss -- advances SymTabV2::epoch_;
    // deliberately using a miss here so `hot`'s own lastAccessEpoch is not
    // refreshed by these calls).
    for (size_t i = 0; i < cfg.coldIdleEpochs; i++) t.resolve("someNameThatWasNeverDeclared");

    CHECK(t.runMaintenance() == 1);
    CHECK(t.demotions() == 1);
    CHECK(t.representationOf(hot) == Rep::COMPRESSED_REP);
    // Declaration id and correctness must survive the round trip.
    CHECK(t.resolve(hot) == ids[2]);
    // Neighbors and the rest of the table must be untouched by the sweep.
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 2) continue;
        CHECK(t.resolve(names[i]) == ids[i]);
    }
    // A second maintenance sweep right away (epoch barely advanced by the
    // resolve() calls just above) must not re-demote or double-count.
    size_t demotionsBefore = t.demotions();
    t.runMaintenance();
    CHECK(t.demotions() == demotionsBefore);

    if (failures == 0) std::cout << "test_demotion_on_idle_after_maintenance: passed\n";
}

// P0-4: demotion is opt-in. cfg.coldIdleEpochs == 0 (the default) must keep
// promotion strictly one-directional, matching pre-P0-4 SymTabV2 behavior.
static void test_demotion_disabled_by_default() {
    PolicyConfigV2 cfg;
    cfg.compressMinLen = 10;
    cfg.hotAccessThreshold = 2;
    // cfg.coldIdleEpochs left at its default (0).

    SymTabV2<> t(1 << 20, cfg);
    int id = t.insert("temperatureSensorCalibration");
    for (size_t i = 0; i < cfg.hotAccessThreshold; i++) t.resolve("temperatureSensorCalibration");
    CHECK(t.representationOf("temperatureSensorCalibration") == Rep::INTERNED_REP);

    for (int i = 0; i < 10000; i++) t.resolve("someUnrelatedAbsentName");
    CHECK(t.runMaintenance() == 0); // disabled: must be an unconditional no-op
    CHECK(t.representationOf("temperatureSensorCalibration") == Rep::INTERNED_REP);
    CHECK(t.resolve("temperatureSensorCalibration") == id);

    if (failures == 0) std::cout << "test_demotion_disabled_by_default: passed\n";
}

// P0-4: an entry decide() natively routed to INTERNED (never went through
// COMPRESSED at all -- e.g. an exact repeat, per decide()'s repeat rule)
// must NEVER be demoted, even when idle past coldIdleEpochs. Demotion is
// gated on PackedEntry::wasPromoted specifically to protect this case.
static void test_naturally_interned_entry_never_demoted() {
    PolicyConfigV2 cfg;
    cfg.compressMinLen = 10;
    cfg.coldIdleEpochs = 3;

    SymTabV2<> t(1 << 20, cfg);
    // First occurrence goes COMPRESSED (len >= compressMinLen, never seen before).
    int first = t.insert("networkInterfaceBufferPool");
    CHECK(t.representationOf("networkInterfaceBufferPool") == Rep::COMPRESSED_REP);
    t.enterScope();
    // Exact repeat in a nested scope: decide()'s repeat rule routes this
    // occurrence to INTERNED directly -- NOT via maybePromote().
    int second = t.insert("networkInterfaceBufferPool");
    CHECK(second != first);
    CHECK(t.representationOf("networkInterfaceBufferPool") == Rep::INTERNED_REP);

    for (int i = 0; i < 100; i++) t.resolve("someUnrelatedAbsentName");
    CHECK(t.runMaintenance() == 0); // must find nothing eligible: wasPromoted is false
    CHECK(t.representationOf("networkInterfaceBufferPool") == Rep::INTERNED_REP);
    CHECK(t.resolve("networkInterfaceBufferPool") == second);

    if (failures == 0) std::cout << "test_naturally_interned_entry_never_demoted: passed\n";
}

// ECC review C1 regression test (results/ecc_review.md finding C1):
// allocSlot()/insert() previously did not reset PackedEntry::wasPromoted or
// lastAccessEpoch when a freed slot was reused, so a decide()-native
// INTERNED entry that happened to recycle a slot PREVIOUSLY held by a
// promoted-then-released entry inherited stale demotion-eligibility state
// and could be wrongly demoted -- without ever itself being resolve()'d,
// let alone promoted. This test forces exactly that slot-reuse sequence:
//   promotion -> release (scope exit) -> free-list slot reuse
//   -> insertion of a natively-INTERNED symbol -> maintenance sweep
// and asserts the new symbol is untouched by demotion.
static void test_slot_reuse_does_not_inherit_promotion_state() {
    PolicyConfigV2 cfg;
    cfg.compressMinLen = 10;
    cfg.hotAccessThreshold = 2;
    cfg.coldIdleEpochs = 2; // deliberately small: easy for a stale epoch to appear "idle enough"

    SymTabV2<> t(1 << 20, cfg);
    // Seed the repeat-detection registry so a LATER occurrence of this name
    // decide()-natively routes to INTERNED (never touches COMPRESSED, never
    // goes through maybePromote()). Stays alive in the GLOBAL scope for the
    // whole test -- deliberately NOT released, so its slot (0) is never in
    // play for the reuse this test targets.
    t.insert("networkInterfaceBufferPool"); // slot 0, global scope, COMPRESSED

    t.enterScope(); // scope 1
    int hotId = t.insert("temperatureSensorCalibration"); // slot 1, COMPRESSED
    for (size_t i = 0; i < cfg.hotAccessThreshold; i++) t.resolve("temperatureSensorCalibration");
    CHECK(t.representationOf("temperatureSensorCalibration") == Rep::INTERNED_REP); // promoted
    CHECK(t.promotions() == 1);
    t.exitScope(); // releases slot 1 (the promoted entry) -> freeSlots_ = [1], LIFO

    // A DIFFERENT scope (not scope 1's, and not scope 0's -- scope 0 already
    // holds a live binding of this exact name, which would make the next
    // insert() a same-scope REDECLARATION of slot 0 instead of a fresh
    // allocSlot() call, missing the free-list reuse path entirely). Scope
    // 2's own index has no entry for this name, so insert() takes the
    // fresh-allocation path, and freeSlots_'s only entry (slot 1, the
    // promoted-then-released one) is exactly what gets reused.
    t.enterScope(); // scope 2
    // "networkInterfaceBufferPool" is a REPEAT of the seed above (globally,
    // via everSeenRep_), so decide() routes it natively to INTERNED -- this
    // occurrence never goes through maybePromote() at all.
    int y = t.insert("networkInterfaceBufferPool");
    CHECK(y != hotId);
    CHECK(t.representationOf("networkInterfaceBufferPool") == Rep::INTERNED_REP);

    // Advance the epoch clock well past coldIdleEpochs via unrelated
    // lookups. Pre-fix, the reused slot's STALE lastAccessEpoch (left over
    // from "temperatureSensorCalibration"'s promotion) combined with its
    // STALE wasPromoted=true would make this immediately demotion-eligible.
    for (int i = 0; i < 50; i++) t.resolve("someUnrelatedAbsentName");

    size_t demoted = t.runMaintenance();
    CHECK(demoted == 0); // the new symbol must NOT be swept up as demotion-eligible
    CHECK(t.demotions() == 0);
    CHECK(t.representationOf("networkInterfaceBufferPool") == Rep::INTERNED_REP); // untouched
    CHECK(t.resolve("networkInterfaceBufferPool") == y); // correctness preserved

    if (failures == 0) std::cout << "test_slot_reuse_does_not_inherit_promotion_state: passed\n";
}

int main() {
    test_round_trip_and_no_false_positives();
    test_whole_block_reclaim_to_zero();
    test_partial_block_redeclare_preserves_survivors();
    test_forced_hash_collision_still_resolves_correctly();
    test_shadowing_nested_scope_and_absent_symbol();
    test_promotion_on_hot_access();
    test_demotion_on_idle_after_maintenance();
    test_demotion_disabled_by_default();
    test_naturally_interned_entry_never_demoted();
    test_slot_reuse_does_not_inherit_promotion_state();

    if (failures == 0) {
        std::cout << "ALL SYMTAB_V2 COMPRESSED-TIER TESTS PASSED\n";
        return 0;
    }
    std::cout << failures << " SYMTAB_V2 COMPRESSED-TIER TEST(S) FAILED\n";
    return 1;
}
