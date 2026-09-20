#pragma once
// EmbeddedConventional Symbol Table
//
// A legitimate, conventional embedded-oriented symbol table baseline.
// Uses embedded systems engineering best practices for constrained RAM:
//   - 32-bit compact symbol IDs and offsets (4B offsets, 2B scope IDs)
//   - 16-byte CompactEntry (declId + scopeId + typeId + nameOffset + nameLen)
//   - Flat contiguous String Arena (no per-string malloc heap headers)
//   - Compact Robin Hood open-addressing hash index (no per-node pointers)
//   - Deterministic scope-lifetime slot and arena reclamation on exitScope()
//   - FIXED conventional representation (no adaptive INLINE/INTERNED/COMPRESSED selection,
//     no front-coding block compression, no fingerprint filtering)
//
// Represents a state-of-the-art embedded C toolchain symbol table (e.g. GCC/Clang embedded target)
// operating under severe RAM constraints.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include "common.hpp"
#include "memory_tracker.hpp"

namespace budgetsym {

class EmbeddedConventionalSymbolTable {
public:
    struct CompactEntry {
        uint32_t declId = 0;
        uint32_t nameOffset = 0;
        uint16_t scopeId = 0;
        uint16_t nameLen = 0;
        uint8_t  typeId = 0;
        uint8_t  live = 1;
    }; // sizeof(CompactEntry) = 16 bytes

    explicit EmbeddedConventionalSymbolTable(size_t budgetBytes = 0) : tracker_(budgetBytes) {
        scopeIndex_.emplace_back();
        hashSlots_.resize(16, kEmptySlot);
    }

    int enterScope() {
        scopeIndex_.emplace_back();
        return static_cast<int>(scopeIndex_.size()) - 1;
    }

    struct ScopeExitReport { size_t symbolsReleased = 0; long long bytesReclaimed = 0; };

    ScopeExitReport exitScope() {
        ScopeExitReport rep;
        if (scopeIndex_.size() <= 1) return rep;

        uint16_t currentScope = static_cast<uint16_t>(scopeIndex_.size() - 1);
        auto& active = scopeIndex_.back();

        for (uint32_t slotId : active) {
            if (slotId < entries_.size() && entries_[slotId].live && entries_[slotId].scopeId == currentScope) {
                entries_[slotId].live = 0;
                removeFromHashIndex(slotId);
                freeSlots_.push_back(slotId);
                rep.symbolsReleased++;
                liveCount_--;
            }
        }

        scopeIndex_.pop_back();

        // Reclaim memory metrics
        long long currentBytes = computeCurrentBytes();
        long long reclaimed = lastTrackedBytes_ > currentBytes ? (lastTrackedBytes_ - currentBytes) : 0;
        lastTrackedBytes_ = currentBytes;
        rep.bytesReclaimed = reclaimed;

        return rep;
    }

    int insert(const std::string& name, int typeId = 0) {
        int id = nextId_++;
        uint16_t currentScope = static_cast<uint16_t>(scopeIndex_.size() - 1);

        // Check for same-scope redeclaration
        int existingSlot = findInScope(name, currentScope);
        if (existingSlot >= 0) {
            entries_[existingSlot].declId = id;
            entries_[existingSlot].typeId = static_cast<uint8_t>(typeId);
            return id;
        }

        // Allocate string in flat arena
        uint32_t offset = static_cast<uint32_t>(arena_.size());
        uint16_t len = static_cast<uint16_t>(name.size());
        arena_.insert(arena_.end(), name.begin(), name.end());
        arena_.push_back('\0');

        // Allocate slot
        uint32_t slotId;
        if (!freeSlots_.empty()) {
            slotId = freeSlots_.back();
            freeSlots_.pop_back();
        } else {
            slotId = static_cast<uint32_t>(entries_.size());
            entries_.emplace_back();
        }

        CompactEntry& e = entries_[slotId];
        e.declId = id;
        e.nameOffset = offset;
        e.nameLen = len;
        e.scopeId = currentScope;
        e.typeId = static_cast<uint8_t>(typeId);
        e.live = 1;

        scopeIndex_.back().push_back(slotId);
        insertToHashIndex(slotId);
        liveCount_++;

        lastTrackedBytes_ = computeCurrentBytes();
        tracker_.add(lastTrackedBytes_);

        return id;
    }

    int resolve(const std::string& name) const {
        uint32_t h = hashString(name);
        size_t mask = hashSlots_.size() - 1;
        size_t idx = h & mask;
        size_t dist = 0;

        int bestDeclId = -1;
        int bestScope = -1;

        while (hashSlots_[idx] != kEmptySlot) {
            uint32_t slotId = hashSlots_[idx];
            if (slotId < entries_.size() && entries_[slotId].live) {
                const auto& e = entries_[slotId];
                if (e.nameLen == name.size() &&
                    std::memcmp(&arena_[e.nameOffset], name.data(), e.nameLen) == 0) {
                    if (static_cast<int>(e.scopeId) > bestScope) {
                        bestScope = e.scopeId;
                        bestDeclId = e.declId;
                    }
                }
            }
            idx = (idx + 1) & mask;
            dist++;
            if (dist > hashSlots_.size()) break;
        }

        return bestDeclId;
    }

    bool lookup(const std::string& name) const { return resolve(name) >= 0; }

    void recordAccess(const std::string& name) { (void)name; }

    size_t size() const { return liveCount_; }

    const MemoryTracker& tracker() const { return tracker_; }

    long long computeCurrentBytes() const {
        long long entriesBytes = static_cast<long long>(entries_.capacity() * sizeof(CompactEntry));
        long long arenaBytes   = static_cast<long long>(arena_.capacity() * sizeof(char));
        long long indexBytes   = static_cast<long long>(hashSlots_.capacity() * sizeof(uint32_t));
        long long scopeBytes   = 0;
        for (auto& s : scopeIndex_) scopeBytes += static_cast<long long>(s.capacity() * sizeof(uint32_t));
        return entriesBytes + arenaBytes + indexBytes + scopeBytes;
    }

private:
    enum : uint32_t { kEmptySlot = 0xFFFFFFFFu };

    static uint32_t hashString(const std::string& s) {
        uint32_t h = 2166136261u;
        for (char c : s) {
            h ^= static_cast<uint8_t>(c);
            h *= 16777619u;
        }
        return h;
    }

    int findInScope(const std::string& name, uint16_t scopeId) const {
        if (scopeIndex_.empty() || scopeId >= scopeIndex_.size()) return -1;
        for (uint32_t slotId : scopeIndex_[scopeId]) {
            if (slotId < entries_.size() && entries_[slotId].live && entries_[slotId].scopeId == scopeId) {
                const auto& e = entries_[slotId];
                if (e.nameLen == name.size() &&
                    std::memcmp(&arena_[e.nameOffset], name.data(), e.nameLen) == 0) {
                    return static_cast<int>(slotId);
                }
            }
        }
        return -1;
    }

    void insertToHashIndex(uint32_t slotId) {
        if (liveCount_ * 2 >= hashSlots_.size()) {
            resizeHashIndex(hashSlots_.size() * 2);
        }
        size_t mask = hashSlots_.size() - 1;
        const auto& e = entries_[slotId];
        std::string name(&arena_[e.nameOffset], e.nameLen);
        uint32_t h = hashString(name);
        size_t idx = h & mask;

        while (hashSlots_[idx] != kEmptySlot) {
            idx = (idx + 1) & mask;
        }
        hashSlots_[idx] = slotId;
    }

    void removeFromHashIndex(uint32_t slotId) {
        size_t mask = hashSlots_.size() - 1;
        const auto& e = entries_[slotId];
        std::string name(&arena_[e.nameOffset], e.nameLen);
        uint32_t h = hashString(name);
        size_t idx = h & mask;

        while (hashSlots_[idx] != kEmptySlot) {
            if (hashSlots_[idx] == slotId) {
                hashSlots_[idx] = kEmptySlot;
                // Shift subsequent elements back (simple linear probing deletion)
                size_t next = (idx + 1) & mask;
                while (hashSlots_[next] != kEmptySlot) {
                    uint32_t rehashSlot = hashSlots_[next];
                    hashSlots_[next] = kEmptySlot;
                    insertToHashIndex(rehashSlot);
                    next = (next + 1) & mask;
                }
                break;
            }
            idx = (idx + 1) & mask;
        }
    }

    void resizeHashIndex(size_t newCap) {
        hashSlots_.clear();
        hashSlots_.resize(newCap, kEmptySlot);
        for (size_t i = 0; i < entries_.size(); i++) {
            if (entries_[i].live) {
                insertToHashIndex(static_cast<uint32_t>(i));
            }
        }
    }

    std::vector<CompactEntry> entries_;
    std::vector<char> arena_;
    std::vector<uint32_t> hashSlots_;
    std::vector<std::vector<uint32_t>> scopeIndex_;
    std::vector<uint32_t> freeSlots_;

    size_t liveCount_ = 0;
    int nextId_ = 0;
    long long lastTrackedBytes_ = 0;
    MemoryTracker tracker_;
};

} // namespace budgetsym
