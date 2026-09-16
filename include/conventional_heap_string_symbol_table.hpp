#pragma once
// Analytical baseline -- NOT a replacement for ConventionalSymbolTable.
//
// Identical scope-stack-of-maps organization to ConventionalSymbolTable, but
// the map key is a HeapString instead of std::string: every identifier's
// characters are unconditionally placed in a separately new[]'d buffer,
// regardless of length. This removes libstdc++'s ~15-byte SSO buffer as a
// variable in the comparison, answering: "how much of Conventional's
// real-world win is specifically the SSO fast path, versus the underlying
// hash-map organization itself?"
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "common.hpp"
#include "memory_tracker.hpp"

namespace budgetsym {

struct HeapString {
    std::unique_ptr<char[]> data;
    uint32_t len = 0;

    HeapString() = default;
    explicit HeapString(const std::string& s) : data(new char[s.size() + 1]), len(static_cast<uint32_t>(s.size())) {
        std::memcpy(data.get(), s.data(), s.size());
        data[s.size()] = '\0';
    }
    HeapString(const HeapString& other) : data(new char[other.len + 1]), len(other.len) {
        std::memcpy(data.get(), other.data.get(), other.len + 1);
    }
    HeapString& operator=(const HeapString& other) {
        if (this != &other) {
            data.reset(new char[other.len + 1]);
            len = other.len;
            std::memcpy(data.get(), other.data.get(), other.len + 1);
        }
        return *this;
    }
    HeapString(HeapString&&) = default;
    HeapString& operator=(HeapString&&) = default;

    bool operator==(const HeapString& o) const {
        return len == o.len && std::memcmp(data.get(), o.data.get(), len) == 0;
    }
};

struct HeapStringHash {
    size_t operator()(const HeapString& s) const {
        // Same FNV-1a-style mixing as the rest of the project's hash_functions.hpp,
        // applied over the heap buffer instead of std::string's internal storage.
        uint64_t h = 1469598103934665603ULL;
        for (uint32_t i = 0; i < s.len; i++) {
            h ^= static_cast<uint8_t>(s.data[i]);
            h *= 1099511628211ULL;
        }
        return static_cast<size_t>(h);
    }
};

class ConventionalHeapStringSymbolTable {
public:
    explicit ConventionalHeapStringSymbolTable(size_t budgetBytes = 0) : tracker_(budgetBytes) {
        scopeMaps_.emplace_back();
    }

    int enterScope() {
        scopeMaps_.emplace_back();
        return static_cast<int>(scopeMaps_.size()) - 1;
    }

    struct ScopeExitReport { size_t symbolsReleased = 0; long long bytesReclaimed = 0; };

    ScopeExitReport exitScope() {
        ScopeExitReport rep;
        if (scopeMaps_.size() <= 1) return rep;
        for (auto& kv : scopeMaps_.back()) {
            long long cost = entryCost(kv.first.len);
            tracker_.reclaim(cost);
            rep.bytesReclaimed += cost;
            rep.symbolsReleased++;
        }
        scopeMaps_.pop_back();
        return rep;
    }

    int insert(const std::string& name, int typeId = 0) {
        int id = nextId_++;
        SymbolMeta meta;
        meta.id = id;
        meta.scopeId = static_cast<int>(scopeMaps_.size()) - 1;
        meta.typeId = typeId;
        meta.representation = Representation::INLINE_REP;

        HeapString key(name);
        auto& scope = scopeMaps_.back();
        auto existing = scope.find(key);
        if (existing != scope.end()) {
            existing->second = meta;
            return id;
        }
        long long cost = entryCost(key.len);
        scope.emplace(std::move(key), meta);
        tracker_.add(cost);
        return id;
    }

    int resolve(const std::string& name) const {
        HeapString key(name);
        for (auto it = scopeMaps_.rbegin(); it != scopeMaps_.rend(); ++it) {
            auto f = it->find(key);
            if (f != it->end()) return f->second.id;
        }
        return -1;
    }

    bool lookup(const std::string& name) const { return resolve(name) >= 0; }

    void recordAccess(const std::string& name) {
        HeapString key(name);
        for (auto it = scopeMaps_.rbegin(); it != scopeMaps_.rend(); ++it) {
            auto f = it->find(key);
            if (f != it->end()) { f->second.accessCount++; return; }
        }
    }

    size_t size() const {
        size_t n = 0;
        for (auto& m : scopeMaps_) n += m.size();
        return n;
    }

    const MemoryTracker& tracker() const { return tracker_; }

    // Cost model mirrors ConventionalSymbolTable::entryCost exactly, except
    // the string payload is unconditionally a heap allocation (len+1 bytes)
    // instead of "0 extra bytes for names <=15B via SSO". sizeof(HeapString)
    // (pointer + uint32_t, ~16B with padding) replaces sizeof(std::string)
    // (32B) as the map-node-resident control block -- HeapString is smaller
    // than std::string's control block, but unlike std::string it NEVER
    // avoids the heap allocation, which is the entire point of this baseline.
    static long long entryCost(uint32_t len) {
        return static_cast<long long>(sizeof(HeapString) + len + 1 + kMetaOverhead);
    }

    static const long long kMetaOverhead = 32;

private:
    std::vector<std::unordered_map<HeapString, SymbolMeta, HeapStringHash>> scopeMaps_;
    int nextId_ = 0;
    MemoryTracker tracker_;
};

} // namespace budgetsym
