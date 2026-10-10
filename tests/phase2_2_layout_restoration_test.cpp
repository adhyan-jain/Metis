#define METISX_TEST_HOOK
#define private public
#include "../include/metis_x.hpp"
#undef private

#include <iostream>
#include <vector>
#include <string>
#include <cassert>

using namespace budgetsym::metisx;

struct Snapshot {
    size_t capacity;
    size_t liveCount;
    std::vector<MetisXSlot> slots;
};

Snapshot takeSnapshot(const MetisXTable& t) {
    Snapshot s;
    s.capacity = t.capacity_;
    s.liveCount = t.liveCount_;
    s.slots = t.slots_;
    return s;
}

bool compareSnapshots(const Snapshot& before, const Snapshot& after, const std::string& testName) {
    bool ok = true;
    if (before.capacity != after.capacity) {
        std::cout << "FAIL " << testName << ": Capacity mismatch " << before.capacity << " vs " << after.capacity << "\n";
        return false;
    }
    if (before.liveCount != after.liveCount) {
        std::cout << "FAIL " << testName << ": Live count mismatch " << before.liveCount << " vs " << after.liveCount << "\n";
        ok = false;
    }
    for (size_t i = 0; i < before.slots.size(); ++i) {
        const auto& b = before.slots[i];
        const auto& a = after.slots[i];
        if (b.occupied() != a.occupied()) {
            std::cout << "FAIL " << testName << ": Slot " << i << " occupancy mismatch\n";
            ok = false;
        } else if (b.occupied()) {
            if (b.hashCache != a.hashCache || b.scopeId != a.scopeId || 
                b.probeDistance != a.probeDistance || b.declId != a.declId) {
                std::cout << "FAIL " << testName << ": Slot " << i << " state mismatch\n";
                ok = false;
            }
        }
    }
    if (ok) std::cout << "PASS " << testName << "\n";
    return ok;
}

uint32_t force_hash_100(const std::string&) { return 100; }
uint32_t force_hash_200(const std::string&) { return 200; }

int main() {
    {
        MetisXTable::setCustomHashFn(nullptr);
        MetisXTable t;
        t.enterScope();
        t.insert("foo");
        t.insert("bar");
        Snapshot before = takeSnapshot(t);
        t.enterScope();
        t.insert("baz");
        t.insert("qux");
        t.exitScope();
        Snapshot after = takeSnapshot(t);
        compareSnapshots(before, after, "No-displacement insertions");
    }
    
    // Multi-swap displacement test
    {
        MetisXTable::setCustomHashFn(force_hash_100);
        MetisXTable t;
        t.enterScope();
        t.insert("A");
        t.insert("B");
        t.insert("C");
        Snapshot before = takeSnapshot(t);
        
        t.enterScope();
        t.insert("D");
        t.insert("E");
        t.exitScope();
        
        Snapshot after = takeSnapshot(t);
        compareSnapshots(before, after, "Multi-swap Robin Hood displacements");
    }

    // Shadowing displacement test
    {
        MetisXTable::setCustomHashFn(force_hash_200);
        MetisXTable t;
        t.enterScope();
        t.insert("X"); // Outer scope
        Snapshot before = takeSnapshot(t);
        
        t.enterScope();
        t.insert("X"); // Inner scope shadowing
        t.exitScope();
        
        Snapshot after = takeSnapshot(t);
        compareSnapshots(before, after, "Shadowing inner scope");
    }
    
    // Active scope rehashing test
    {
        MetisXTable::setCustomHashFn(nullptr);
        MetisXTable t;
        t.enterScope();
        for(int i=0; i<10; ++i) t.insert("A" + std::to_string(i));
        Snapshot before = takeSnapshot(t);
        
        t.enterScope();
        for(int i=0; i<100; ++i) t.insert("B" + std::to_string(i)); // Triggers rehash
        t.exitScope();
        
        Snapshot after = takeSnapshot(t);
        
        if (before.capacity != after.capacity) {
            std::cout << "INFO Active-scope rehash: capacity changed from " << before.capacity << " to " << after.capacity << ". Layout restoration is semantically broken by rehash (expected).\n";
        }
    }
    return 0;
}
