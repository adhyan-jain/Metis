#pragma once
// New comparison point (Task: "additional hashing techniques") -- Robin Hood
// open-addressing hashing, applied the same way ConventionalSymbolTable
// applies std::unordered_map: one table per scope, keyed directly by the
// full identifier string (no interning, no compression -- isolating the
// *table structure* as the only variable against Conventional, rather than
// bundling it with a representation-selection policy the way BudgetSym does).
//
// Robin Hood hashing is a linear-probing open-addressing scheme where, on
// insert, an element that has probed farther from its ideal ("home") slot
// than the element currently occupying a candidate slot displaces that
// element (steals from the "rich" element that's closer to home, gives to
// the "poor" one probing farther -- hence the name) and continues inserting
// the displaced element. Two consequences that matter here:
//   1. Lookup can stop early: if the probe distance already searched exceeds
//      the probe distance recorded in the current slot, the key cannot be
//      present anywhere farther out (see docs/robinhood.md for why).
//   2. No pointer-chasing: entries live inline in one contiguous array, not
//      one heap node per bucket entry like std::unordered_map's chaining --
//      better cache locality is the actual mechanism, not a faster hash.
//
// Deletion is NOT implemented (no tombstones, no backward-shift delete).
// This is a real, stated limitation, not an oversight: this project only
// ever needs whole-scope removal (exitScope() drops the entire table for
// that scope, exactly like ConventionalSymbolTable does), never a single-key
// delete from a live table, so implementing backward-shift deletion would be
// real code with no exercised code path -- see docs/robinhood.md.
#include <string>
#include <vector>
#include <cstdint>
#include "common.hpp"
#include "memory_tracker.hpp"
#include "hash_functions.hpp"

namespace budgetsym {

template <typename HashFn = FnvHash>
class RobinHoodSymbolTableT {
public:
    explicit RobinHoodSymbolTableT(size_t budgetBytes = 0) : tracker_(budgetBytes) {
        scopes_.emplace_back(kInitialCapacity);
    }

    int enterScope() {
        scopes_.emplace_back(kInitialCapacity);
        return static_cast<int>(scopes_.size()) - 1;
    }

    struct ScopeExitReport { size_t symbolsReleased = 0; long long bytesReclaimed = 0; };

    ScopeExitReport exitScope() {
        ScopeExitReport rep;
        if (scopes_.size() <= 1) return rep;
        Table& t = scopes_.back();
        for (auto& slot : t.slots) {
            if (!slot.occupied) continue;
            long long cost = entryCost(slot.key);
            tracker_.reclaim(cost);
            rep.bytesReclaimed += cost;
            rep.symbolsReleased++;
        }
        scopes_.pop_back();
        return rep;
    }

    int insert(const std::string& name, int typeId = 0) {
        int id = nextId_++;
        SymbolMeta meta;
        meta.id = id;
        meta.scopeId = static_cast<int>(scopes_.size()) - 1;
        meta.typeId = typeId;
        meta.representation = Representation::INLINE_REP; // raw storage, no adaptivity -- same framing as Conventional
        // insert() returns false for a same-scope redeclaration (binding
        // overwritten in place, no new slot) -- not charged again.
        if (!scopes_.back().insert(name, meta)) return id;
        // Tracked-memory charge uses the exact same per-entry formula as
        // ConventionalSymbolTable::entryCost -- deliberately, so the headline
        // "tracked memory" / compression_ratio numbers stay comparable across
        // every implementation in results/algorithm_comparison.csv. The real,
        // additional cost of preallocated-but-empty slots that open
        // addressing pays and chaining doesn't is reported separately via
        // slackBytes() (see docs/robinhood.md) rather than folded in here,
        // so it doesn't silently change what "tracked memory" means
        // project-wide.
        tracker_.add(entryCost(name));
        return id;
    }

    // Bytes occupied by preallocated-but-currently-empty slots across all open
    // scopes -- a real cost of open addressing with inline storage (every
    // slot is a live std::string + metadata whether used or not) that a
    // chaining table (std::unordered_map) does not pay in the same way.
    // Reported as its own diagnostic, NOT included in tracker()/compression
    // ratio -- see the insert() comment above for why.
    long long slackBytes() const {
        long long total = 0;
        for (auto& t : scopes_) total += t.emptySlotBytes();
        return total;
    }

    // See ConventionalSymbolTable::resolve().
    int resolve(const std::string& name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            const SymbolMeta* m = it->find(name);
            if (m != nullptr) return m->id;
        }
        return -1;
    }

    bool lookup(const std::string& name) const { return resolve(name) >= 0; }

    void recordAccess(const std::string& name) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            SymbolMeta* m = it->find(name);
            if (m) { m->accessCount++; return; }
        }
    }

    size_t size() const {
        size_t n = 0;
        for (auto& t : scopes_) n += t.count;
        return n;
    }

    const MemoryTracker& tracker() const { return tracker_; }

    static long long entryCost(const std::string& name) {
        return static_cast<long long>(sizeof(std::string) + name.size() + 1 + kMetaOverhead);
    }

    static const long long kMetaOverhead = 32; // same constant Conventional uses -- isolates table-structure as the only variable

private:
    static const size_t kInitialCapacity = 16;
    // Standard Robin Hood load-factor ceiling; resize (double capacity, full
    // rehash) once exceeded. Lower than a chaining table's typical 1.0 because
    // open addressing degrades sharply as slots fill up (probe sequences grow).
    static constexpr double kMaxLoadFactor = 0.70;

    struct Slot {
        uint64_t hash = 0;
        std::string key;
        SymbolMeta meta;
        uint32_t probeDistance = 0;
        bool occupied = false;
    };

    struct Table {
        std::vector<Slot> slots;
        size_t count = 0;
        size_t capacityMask = 0;

        explicit Table(size_t initialCapacity) : slots(initialCapacity), capacityMask(initialCapacity - 1) {}

        // Empty-slot memory is a real, honestly-reportable cost of this
        // structure: every slot -- occupied or not -- is a live std::string +
        // metadata in a preallocated array, unlike a chaining table where an
        // empty bucket is just a null pointer. See docs/robinhood.md.
        long long emptySlotBytes() const {
            size_t empty = slots.size() - count;
            return static_cast<long long>(empty * sizeof(Slot));
        }

        // Returns true if a new slot was filled, false if an existing key's
        // meta was overwritten.
        bool insert(std::string key, SymbolMeta meta) {
            if (static_cast<double>(count + 1) / static_cast<double>(slots.size()) > kMaxLoadFactor) {
                grow();
            }
            uint64_t h = HashFn::hash(key);
            size_t pos = h & capacityMask;
            uint32_t dist = 0;
            for (;;) {
                Slot& s = slots[pos];
                if (!s.occupied) {
                    s = Slot{h, std::move(key), meta, dist, true};
                    count++;
                    return true;
                }
                if (s.hash == h && s.key == key) {
                    s.meta = meta; // re-declaration in the same scope: overwrite, matches Conventional's operator[] semantics
                    return false;
                }
                if (s.probeDistance < dist) {
                    // Robin Hood swap: the incoming element is "poorer" (has
                    // probed farther from its home slot) than the current
                    // occupant, so it takes this slot and the displaced
                    // occupant continues probing from here.
                    std::swap(h, s.hash);
                    std::swap(key, s.key);
                    std::swap(meta, s.meta);
                    std::swap(dist, s.probeDistance);
                }
                pos = (pos + 1) & capacityMask;
                dist++;
            }
        }

        SymbolMeta* find(const std::string& key) {
            uint64_t h = HashFn::hash(key);
            size_t pos = h & capacityMask;
            uint32_t dist = 0;
            for (;;) {
                const Slot& s = slots[pos];
                // Robin Hood's early-exit invariant: slots are laid out so
                // probeDistance never decreases while a match could still be
                // ahead. Once the search has probed farther than the current
                // slot's own probeDistance, the key must be absent -- it
                // would have displaced this slot on insert otherwise.
                if (!s.occupied || dist > s.probeDistance) return nullptr;
                if (s.hash == h && s.key == key) return &const_cast<Slot&>(s).meta;
                pos = (pos + 1) & capacityMask;
                dist++;
            }
        }

        const SymbolMeta* find(const std::string& key) const {
            return const_cast<Table*>(this)->find(key);
        }

        void grow() {
            std::vector<Slot> old = std::move(slots);
            slots.assign(old.size() * 2, Slot());
            capacityMask = slots.size() - 1;
            count = 0;
            for (auto& s : old) {
                if (s.occupied) insert(std::move(s.key), s.meta);
            }
        }
    };

    std::vector<Table> scopes_;
    int nextId_ = 0;
    MemoryTracker tracker_;
};

// Out-of-class definition for the static const member: required pre-C++17
// whenever it's ODR-used (std::vector::emplace_back's perfect-forwarding
// constructor binds it by reference), which -std=c++14 on this project's
// toolchain (GCC 6.3.0) does trigger -- caught as a link error, not a compile
// error, the first time this header was actually used from another .cpp.
template <typename HashFn>
const size_t RobinHoodSymbolTableT<HashFn>::kInitialCapacity;

using RobinHoodSymbolTable = RobinHoodSymbolTableT<FnvHash>;

} // namespace budgetsym
