#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>

namespace budgetsym {
namespace metisx_arena {

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

struct MetisXArenaSlot {
    int32_t  declId        = -1; // 0-3 (4B)
    uint32_t hashCache     = 0;  // 4-7 (4B)
    uint32_t frameIndex    = 0;  // 8-11 (4B)
    uint32_t arenaOffset   = 0xFFFFFFFF; // 12-15 (4B)
    
    uint16_t scopeId       = 0;  // 16-17 (2B)
    uint16_t nameLen       = 0;  // 18-19 (2B)
    
    uint8_t  probeDistance = 0;  // 20 (1B)
    uint8_t  typeId        = 0;  // 21 (1B)
    uint16_t pad1          = 0;  // 22-23 (2B)

    char     prefix[4]     = {0};// 24-27 (4B)
    uint32_t reserved      = 0;  // 28-31 (4B)

    bool occupied() const { return declId >= 0; }
    void clear() {
        declId = -1;
        nameLen = 0;
    }
};

static_assert(sizeof(MetisXArenaSlot) == 32, "Slot must be exactly 32 bytes");

class MetisXArenaTable {
public:
    static constexpr uint8_t kMaxProbe = 250;
    static constexpr uint16_t kMaxScopeDepth = 65535;

    MetisXArenaTable(size_t initialCapacity = 16) {
        capacity_ = initialCapacity;
        slots_.resize(capacity_);
        scopeFrames_.push_back(std::vector<uint32_t>());
    }

    void enterScope() {
        if (scopeFrames_.size() <= kMaxScopeDepth) {
            scopeFrames_.push_back(std::vector<uint32_t>());
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
            MetisXArenaSlot& s = slots_[slotIdx];
            if (!s.occupied() || s.scopeId != currentScope) continue;

            s.clear();
            backwardShift(slotIdx);
            liveCount_--;
        }
        scopeFrames_.pop_back();
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
                MetisXArenaSlot& s = slots_[idx];
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

        MetisXArenaSlot newSlot;
        newSlot.declId = id;
        newSlot.hashCache = h;
        newSlot.scopeId = currentScope;
        newSlot.nameLen = nameLen16;
        newSlot.typeId = static_cast<uint8_t>(typeId);
        newSlot.probeDistance = 0;

        uint16_t preLen = std::min<uint16_t>(4, nameLen16);
        std::memcpy(newSlot.prefix, name.data(), preLen);

        if (nameLen16 > 4) {
            uint32_t offset = static_cast<uint32_t>(arena_.size());
            if (offset + (nameLen16 - 4) > 0xFFFFFFFF) return -1; // Arena limit
            arena_.insert(arena_.end(), name.begin() + 4, name.end());
            newSlot.arenaOffset = offset;
        }

        int res = insertSlotInternal(newSlot);
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
            const MetisXArenaSlot& s = slots_[idx];
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

    size_t getArenaSize() const { return arena_.size(); }
    size_t getCapacity() const { return capacity_; }
    size_t getLiveCount() const { return liveCount_; }

private:
    bool nameMatch(const MetisXArenaSlot& s, const char* data, uint16_t len) const {
        uint16_t preLen = std::min<uint16_t>(4, len);
        if (std::memcmp(s.prefix, data, preLen) != 0) return false;
        if (len <= 4) return true;
        if (s.arenaOffset == 0xFFFFFFFF || s.arenaOffset + (len - 4) > arena_.size()) return false;
        return std::memcmp(&arena_[s.arenaOffset], data + 4, len - 4) == 0;
    }

    void updateSlotLocation(MetisXArenaSlot& s, size_t newIdx) {
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
            MetisXArenaSlot& nextSlot = slots_[next];
            if (!nextSlot.occupied() || nextSlot.probeDistance == 0) break;
            slots_[idx] = nextSlot;
            slots_[idx].probeDistance--;
            updateSlotLocation(slots_[idx], idx);
            nextSlot.clear();
            idx = next;
        }
    }

    int insertSlotInternal(MetisXArenaSlot src) {
        size_t mask = capacity_ - 1;
        size_t idx = src.hashCache & mask;
        
        while (true) {
            MetisXArenaSlot& cur = slots_[idx];
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
        std::vector<MetisXArenaSlot> oldSlots = std::move(slots_);
        slots_.assign(newCap, MetisXArenaSlot());
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
    std::vector<MetisXArenaSlot> slots_;
    std::vector<std::vector<uint32_t>> scopeFrames_;
    std::vector<char> arena_;
};

}
}
