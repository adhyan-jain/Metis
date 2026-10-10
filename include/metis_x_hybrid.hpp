#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
#include <iostream>

namespace budgetsym {
namespace metisx_hybrid {

struct FnvHash {
    static uint64_t hash(const std::string& s) {
        uint64_t h = 14695981039346656037ull;
        for (char c : s) {
            h ^= static_cast<uint8_t>(c);
            h *= 1099511628211ull;
        }
        return h;
    }
};

struct MetisXHybridSlot {
    int32_t  declId        = -1; // 4
    uint32_t hashCache     = 0;  // 4
    uint32_t frameIndex    = 0;  // 4
    uint16_t scopeId       = 0;  // 2
    uint16_t nameLen       = 0;  // 2
    uint8_t  probeDistance = 0;  // 1
    uint8_t  typeId        = 0;  // 1
    uint16_t pad           = 0;  // 2
    char     inlineBytes[12] = {0}; // 12
    // Total 32 bytes

    bool occupied() const { return declId >= 0; }
    void clear() { declId = -1; nameLen = 0; }
};

static_assert(sizeof(MetisXHybridSlot) == 32, "Slot must be exactly 32 bytes");

class MetisXHybridTable {
public:
    static constexpr uint8_t kMaxProbe = 250;
    static constexpr uint16_t kMaxScopeDepth = 65535;

    MetisXHybridTable(size_t initialCapacity = 16) {
        capacity_ = initialCapacity;
        slots_.resize(capacity_);
        scopeFrames_.push_back(std::vector<uint32_t>());
        scopeArenaBounds_.push_back(0);
    }

    void enterScope() {
        if (scopeFrames_.size() <= kMaxScopeDepth) {
            scopeFrames_.push_back(std::vector<uint32_t>());
            scopeArenaBounds_.push_back(arena_.size());
        }
    }

    void exitScope() {
        if (scopeFrames_.size() <= 1) return;
        uint16_t currentScope = static_cast<uint16_t>(scopeFrames_.size() - 1);
        std::vector<uint32_t>& frame = scopeFrames_.back();

        while (!frame.empty()) {
            uint32_t slotIdx = frame.back();
            frame.pop_back();

            if (slotIdx >= slots_.size()) continue;
            MetisXHybridSlot& s = slots_[slotIdx];
            if (!s.occupied() || s.scopeId != currentScope) continue;

            s.clear();
            backwardShift(slotIdx);
            liveCount_--;
        }
        scopeFrames_.pop_back();
        
        arena_.resize(scopeArenaBounds_.back());
        scopeArenaBounds_.pop_back();
    }

    int insert(const std::string& name, int typeId = 0) {
        if (name.size() > 65535) return -1;
        if (typeId < 0 || typeId > 255) return -1;
        if (scopeFrames_.empty() || scopeFrames_.size() > kMaxScopeDepth) return -1;

        int id = nextId_++;
        uint16_t currentScope = static_cast<uint16_t>(scopeFrames_.size() - 1);
        uint16_t nameLen16 = static_cast<uint16_t>(name.size());
        
        uint64_t h64 = FnvHash::hash(name);
        uint32_t h = static_cast<uint32_t>(h64 ^ (h64 >> 32));

        {
            size_t mask = capacity_ - 1;
            size_t idx  = h & mask;
            uint8_t dist = 0;
            while (slots_[idx].occupied()) {
                MetisXHybridSlot& s = slots_[idx];
                if (s.probeDistance < dist) break;
                if (s.hashCache == h && s.scopeId == currentScope &&
                    s.nameLen == nameLen16 && nameMatch(s, name.data(), nameLen16)) {
                    s.declId = id;
                    s.typeId = static_cast<uint8_t>(typeId);
                    return id;
                }
                idx = (idx + 1) & mask;
                dist++;
                if (dist > kMaxProbe) break;
            }
        }

        if (liveCount_ * 100 >= capacity_ * 75) {
            rehash(capacity_ * 2);
        }

        MetisXHybridSlot toInsert;
        toInsert.declId = id;
        toInsert.hashCache = h;
        toInsert.scopeId = currentScope;
        toInsert.nameLen = nameLen16;
        toInsert.typeId = static_cast<uint8_t>(typeId);
        toInsert.probeDistance = 0;

        if (nameLen16 <= 12) {
            std::memcpy(toInsert.inlineBytes, name.data(), nameLen16);
        } else {
            uint32_t offset = static_cast<uint32_t>(arena_.size());
            if (offset + (nameLen16 - 8) > 0xFFFFFFFF) return -1;
            std::memcpy(toInsert.inlineBytes, name.data(), 8);
            std::memcpy(toInsert.inlineBytes + 8, &offset, 4);
            arena_.insert(arena_.end(), name.begin() + 8, name.end());
        }

        int res = insertSlotInternal(toInsert);
        if (res >= 0) {
            liveCount_++;
            uint32_t finalIdx = static_cast<uint32_t>(res);
            slots_[finalIdx].frameIndex = static_cast<uint32_t>(scopeFrames_.back().size());
            scopeFrames_.back().push_back(finalIdx);
            return id;
        }
        return -1;
    }

    int resolve(const std::string& name) const {
        if (capacity_ == 0 || name.size() > 65535) return -1;
        uint64_t h64 = FnvHash::hash(name);
        uint32_t h = static_cast<uint32_t>(h64 ^ (h64 >> 32));
        uint16_t nameLen16 = static_cast<uint16_t>(name.size());

        size_t mask = capacity_ - 1;
        size_t idx = h & mask;
        uint8_t dist = 0;
        int bestId = -1;
        int16_t bestScope = -1;

        while (slots_[idx].occupied()) {
            const MetisXHybridSlot& s = slots_[idx];
            if (s.probeDistance < dist) break;
            if (s.hashCache == h && s.nameLen == nameLen16 && nameMatch(s, name.data(), nameLen16)) {
                if (static_cast<int16_t>(s.scopeId) > bestScope) {
                    bestScope = static_cast<int16_t>(s.scopeId);
                    bestId = s.declId;
                }
            }
            idx = (idx + 1) & mask;
            dist++;
            if (dist > kMaxProbe) break;
        }
        return bestId;
    }

private:
    bool nameMatch(const MetisXHybridSlot& s, const char* data, uint16_t len) const {
        if (len <= 12) {
            return std::memcmp(s.inlineBytes, data, len) == 0;
        }
        if (std::memcmp(s.inlineBytes, data, 8) != 0) return false;
        uint32_t offset;
        std::memcpy(&offset, s.inlineBytes + 8, 4);
        return std::memcmp(&arena_[offset], data + 8, len - 8) == 0;
    }

    void updateSlotLocation(MetisXHybridSlot& s, size_t newIdx) {
        if (s.scopeId < scopeFrames_.size()) {
            if (s.frameIndex < scopeFrames_[s.scopeId].size()) {
                scopeFrames_[s.scopeId][s.frameIndex] = static_cast<uint32_t>(newIdx);
            }
        }
    }

    void backwardShift(size_t startIdx) {
        size_t mask = capacity_ - 1;
        size_t idx  = startIdx & mask;
        while (true) {
            size_t next = (idx + 1) & mask;
            MetisXHybridSlot& nextSlot = slots_[next];
            if (!nextSlot.occupied() || nextSlot.probeDistance == 0) break;
            slots_[idx] = nextSlot;
            slots_[idx].probeDistance--;
            updateSlotLocation(slots_[idx], idx);
            nextSlot.clear();
            idx = next;
        }
    }

    int insertSlotInternal(MetisXHybridSlot src) {
        size_t mask = capacity_ - 1;
        size_t idx = src.hashCache & mask;
        
        while (true) {
            MetisXHybridSlot& cur = slots_[idx];
            if (!cur.occupied()) {
                slots_[idx] = src;
                updateSlotLocation(slots_[idx], idx);
                return static_cast<int>(idx);
            }
            if (cur.probeDistance < src.probeDistance) {
                std::swap(src, slots_[idx]);
                updateSlotLocation(slots_[idx], idx);
            }
            idx = (idx + 1) & mask;
            src.probeDistance++;
            if (src.probeDistance > kMaxProbe) return -1;
        }
    }

    void rehash(size_t newCap) {
        if (newCap < 16) newCap = 16;
        std::vector<MetisXHybridSlot> oldSlots = std::move(slots_);
        slots_.assign(newCap, MetisXHybridSlot());
        capacity_ = newCap;

        for (auto& s : oldSlots) {
            if (s.occupied()) {
                s.probeDistance = 0;
                insertSlotInternal(s);
            }
        }
    }

    size_t capacity_ = 0;
    size_t liveCount_ = 0;
    int nextId_ = 1;
    std::vector<MetisXHybridSlot> slots_;
    std::vector<std::vector<uint32_t>> scopeFrames_;
    std::vector<size_t> scopeArenaBounds_;
    std::vector<char> arena_;
};

}
}
