#define METISX_TEST_HOOK

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "metis_x.hpp"

using namespace budgetsym::metisx;

// ---- Test-only Hash Collision Hook -----------------------------------------
static uint32_t force_constant_hash(const std::string& /*name*/) {
    return 0x12345678u; // All distinct keys hash to the exact same home bucket
}

// ---- Test 1: Genuine Probe Distance Overflow & Resource Bound --------------
void test_probe_distance_overflow() {
    std::cout << "== [Test 1] Genuine Probe Distance Overflow & Resource Bound Verification ==" << std::endl;

    // Enable custom test hash seam
    MetisXTable::setCustomHashFn(force_constant_hash);

    MetisXTable table;
    int s1 = table.enterScope(); // Enter Scope 1 for colliding entries
    assert(s1 == 1);

    std::cout << "  Inserting 251 distinct keys sharing identical 32-bit hash (0x12345678)..." << std::endl;

    std::vector<int> accepted_ids;
    for (int i = 0; i < 251; ++i) {
        std::string key_name = "identical_hash_key_" + std::to_string(i);
        int id = table.insert(key_name, 0);
        assert(id == i); // Must succeed with distinct declIds 0..250
        accepted_ids.push_back(id);
    }

    auto st_initial = table.stats();
    size_t cap_initial = st_initial.capacity;
    size_t size_initial = st_initial.liveCount;
    std::cout << "  Table loaded: 251 items, Capacity: " << cap_initial 
              << ", Avg Probe Distance: " << st_initial.avgProbeDistance << std::endl;

    assert(size_initial == 251);
    assert(cap_initial == 512);

    // Verify all 251 inserted keys resolve correctly before rejection attempts
    for (int i = 0; i < 251; ++i) {
        std::string key_name = "identical_hash_key_" + std::to_string(i);
        assert(table.resolve(key_name) == accepted_ids[i]);
    }

    std::cout << "  Attempting 150 additional distinct keys with identical hash (probe overflow failure path)..." << std::endl;

    for (int attempt = 251; attempt < 401; ++attempt) {
        std::string new_key = "identical_hash_key_" + std::to_string(attempt);
        // Valid string length (24 bytes), valid typeId (0), valid scope depth (1).
        // Reaches dryRunCanInsert() pre-flight failure!
        int res = table.insert(new_key, 0);
        assert(res == -1); // Must fail probe pre-check

        // Verify invariant after EVERY single rejection
        assert(table.stats().capacity == cap_initial); // Capacity MUST NOT grow!
        assert(table.size() == size_initial);           // Size MUST NOT change!
    }

    std::cout << "  150 rejection attempts completed. Capacity after attempts: " 
              << table.stats().capacity << " (expected " << cap_initial << ") [PASS]" << std::endl;
    std::cout << "  Live count after attempts: " << table.size() << " (expected " << size_initial << ") [PASS]" << std::endl;

    assert(table.stats().capacity == cap_initial);
    assert(table.size() == size_initial);

    // Verify all 251 original keys STILL resolve to their exact original declIds
    for (int i = 0; i < 251; ++i) {
        std::string key_name = "identical_hash_key_" + std::to_string(i);
        int res = table.resolve(key_name);
        assert(res == accepted_ids[i]);
    }
    std::cout << "  All 251 original keys verified 100% resolvable after 150 failure retries [PASS]" << std::endl;

    // Reset custom hash hook to default FNV-1a hash
    MetisXTable::setCustomHashFn(nullptr);

    // Verify subsequent valid non-colliding insertion succeeds
    int id_valid = table.insert("normal_valid_key_after_collisions", 0);
    assert(id_valid >= 0);
    assert(table.resolve("normal_valid_key_after_collisions") == id_valid);
    std::cout << "  Subsequent valid non-colliding insertion succeeded [PASS]" << std::endl;

    // Exit Scope 1: verify all 252 items (251 colliding + 1 normal) are released cleanly
    auto rep = table.exitScope();
    assert(rep.symbolsReleased == 252);
    assert(table.size() == 0);
    std::cout << "  Scope 1 exit released 252 symbols cleanly [PASS]" << std::endl;
}

// ---- Test 2: Identifier Length Boundary (12B, 13B, 65535B, 65536B) ---------
void test_identifier_length_boundary() {
    std::cout << "\n== [Test 2] Identifier Length Boundary Verification ==" << std::endl;

    MetisXTable table;

    std::string name12 = "123456789012";              // 12B (Inline)
    std::string name13 = "1234567890123";             // 13B (Heap)
    std::string name65535(65535, 'k');                 // 65,535B (Max representable)
    std::string name65536_a = name65535 + "A";        // 65,536B (Oversized A)
    std::string name65536_b = name65535 + "B";        // 65,536B (Oversized B)

    int id12 = table.insert(name12, 1);
    assert(id12 >= 0);
    assert(table.resolve(name12) == id12);

    int id13 = table.insert(name13, 2);
    assert(id13 >= 0);
    assert(table.resolve(name13) == id13);

    int id65535 = table.insert(name65535, 3);
    assert(id65535 >= 0);
    assert(table.resolve(name65535) == id65535);

    // Oversized names must be deterministically rejected with -1
    int id65536_a = table.insert(name65536_a, 4);
    int id65536_b = table.insert(name65536_b, 5);

    std::cout << "  insert(12B): id=" << id12 << " [PASS]" << std::endl;
    std::cout << "  insert(13B): id=" << id13 << " [PASS]" << std::endl;
    std::cout << "  insert(65535B): id=" << id65535 << " [PASS]" << std::endl;
    std::cout << "  insert(65536B A): id=" << id65536_a << " (expected -1) [PASS]" << std::endl;
    std::cout << "  insert(65536B B): id=" << id65536_b << " (expected -1) [PASS]" << std::endl;

    assert(id65536_a == -1);
    assert(id65536_b == -1);
    assert(table.resolve(name65536_a) == -1);
    assert(table.resolve(name65536_b) == -1);
}

// ---- Test 3: Scope Depth Boundary (65534, 65535, 65536) -------------------
void test_scope_depth_boundary() {
    std::cout << "\n== [Test 3] Scope Depth Boundary Verification ==" << std::endl;

    MetisXTable table;

    // Outer scope 0
    int id0 = table.insert("root_var", 0);
    assert(id0 >= 0);

    // Enter scopes up to depth 65,534 (total 65,535 frames)
    for (int i = 1; i < 65535; ++i) {
        int s = table.enterScope();
        assert(s == i);
    }

    // Insert at depth 65,534
    int id_deep = table.insert("deep_var", 10);
    assert(id_deep >= 0);
    assert(table.resolve("deep_var") == id_deep);

    // Attempt to enter 65,536th scope -> must be rejected with -1
    int s_overflow = table.enterScope();
    std::cout << "  enterScope() at limit 65,535: returned " << s_overflow << " (expected -1) [PASS]" << std::endl;
    assert(s_overflow == -1);

    // Exit deep scope
    auto rep = table.exitScope();
    assert(rep.symbolsReleased == 1);
    assert(table.lookup("deep_var") == false);

    // Root scope exit test (must be safe no-op)
    for (int i = 1; i < 65535; ++i) {
        table.exitScope();
    }
    auto root_rep = table.exitScope(); // Attempts to exit root scope
    assert(root_rep.symbolsReleased == 0);
    assert(table.lookup("root_var") == true);
    std::cout << "  Root scope protection verified [PASS]" << std::endl;
}

// ---- Test 4: Type ID Contract Verification ----------------------------------
void test_type_id_contract() {
    std::cout << "\n== [Test 4] Type ID Contract Verification ==" << std::endl;

    MetisXTable table;

    int id0   = table.insert("var_type_0", 0);
    int id255 = table.insert("var_type_255", 255);
    int id256 = table.insert("var_type_256", 256);   // Outside [0..255] -> Rejected (-1)
    int idneg = table.insert("var_type_neg", -1);    // Negative -> Rejected (-1)

    std::cout << "  insert(typeId=0):   id=" << id0   << " [PASS]" << std::endl;
    std::cout << "  insert(typeId=255): id=" << id255 << " [PASS]" << std::endl;
    std::cout << "  insert(typeId=256): id=" << id256 << " (expected -1) [PASS]" << std::endl;
    std::cout << "  insert(typeId=-1):  id=" << idneg << " (expected -1) [PASS]" << std::endl;

    assert(id0 >= 0);
    assert(id255 >= 0);
    assert(id256 == -1);
    assert(idneg == -1);
}

int main() {
    std::cout << "=================================================================" << std::endl;
    std::cout << "  METIS-X Phase 1.1 Correctness Fix Verification Suite           " << std::endl;
    std::cout << "=================================================================" << std::endl;

    test_probe_distance_overflow();
    test_identifier_length_boundary();
    test_scope_depth_boundary();
    test_type_id_contract();

    std::cout << "\n=================================================================" << std::endl;
    std::cout << "  ALL PHASE 1.1 VERIFICATION TESTS PASSED SUCCESSFULLY!          " << std::endl;
    std::cout << "=================================================================" << std::endl;
    return 0;
}
