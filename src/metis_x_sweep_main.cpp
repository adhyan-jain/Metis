// METIS-X Step 8: Controlled Parameter Sweep (Development Workloads Only)
//
// Evaluates parameter configurations on FreeRTOS and Arduino ONLY.
// HELD-OUT WORKLOADS (Zephyr, ESP-IDF) ARE STRICTOR UNTOUCHED DURING THIS SWEEP.
//
// Grid:
//   kInlineCap: {8, 12, 16, 24}
//   maxLoadFactor: {0.60, 0.70, 0.80}
//
// Output: results/metis_x_parameter_sweep.csv

#define BUDGETSYM_HEAP_COUNTER_IMPL
#include "../include/heap_counter.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../include/embedded_conventional_symbol_table.hpp"
#include "../include/hash_functions.hpp"
#include "../include/hires_timer.hpp"

using namespace budgetsym;

// Custom templated slot & table for parameter exploration
template<size_t INLINE_CAP>
struct SweepSlot {
    int32_t  declId        = -1;
    uint32_t hashCache     = 0;
    uint16_t scopeId       = 0;
    uint8_t  nameLen       = 0;
    uint8_t  probeDistance = 0;
    uint8_t  repFlags      = 0;
    uint8_t  typeId        = 0;
    uint16_t _pad          = 0;

    union NameStorage {
        char  inlineBytes[INLINE_CAP];
        char* heapPtr;
        NameStorage() { std::memset(inlineBytes, 0, sizeof(inlineBytes)); }
    } name;

    bool occupied() const { return nameLen > 0; }

    char* setName(const char* data, uint8_t len) {
        nameLen = len;
        if (static_cast<size_t>(len) <= INLINE_CAP) {
            repFlags = 0;
            std::memcpy(name.inlineBytes, data, len);
            return nullptr;
        }
        repFlags = 1; // REP_HEAP
        char* p = new char[len];
        std::memcpy(p, data, len);
        std::memcpy(name.inlineBytes, &p, sizeof(char*));
        return p;
    }

    void clear() {
        if (occupied() && (repFlags & 1)) {
            char* p;
            std::memcpy(&p, name.inlineBytes, sizeof(char*));
            delete[] p;
        }
        declId = -1; hashCache = 0; scopeId = 0; nameLen = 0;
        probeDistance = 0; repFlags = 0; typeId = 0; _pad = 0;
        std::memset(name.inlineBytes, 0, INLINE_CAP);
    }
};

template<size_t INLINE_CAP>
class SweepTable {
public:
    SweepTable(double maxLoadFactor = 0.70) : loadFactorMax_(maxLoadFactor) {
        slots_.resize(16);
        capacity_ = 16;
        scopeFrames_.emplace_back();
    }

    ~SweepTable() {
        for (auto& s : slots_) {
            if (s.occupied() && (s.repFlags & 1)) {
                char* p;
                std::memcpy(&p, s.name.inlineBytes, sizeof(char*));
                delete[] p;
            }
        }
    }

    int enterScope() { scopeFrames_.emplace_back(); return scopeFrames_.size() - 1; }

    void exitScope() {
        if (scopeFrames_.size() <= 1) return;
        uint16_t cur = scopeFrames_.size() - 1;
        for (uint32_t idx : scopeFrames_.back()) {
            if (idx < slots_.size() && slots_[idx].occupied() && slots_[idx].scopeId == cur) {
                slots_[idx].clear();
                backwardShift(idx);
                liveCount_--;
            }
        }
        scopeFrames_.pop_back();
    }

    int insert(const std::string& name) {
        int id = nextId_++;
        uint16_t curScope = scopeFrames_.size() - 1;
        uint8_t nameLen8 = static_cast<uint8_t>(name.size() > 255 ? 255 : name.size());
        uint64_t h64 = FnvHash::hash(name);
        uint32_t h = static_cast<uint32_t>(h64 ^ (h64 >> 32));

        if (static_cast<double>(liveCount_ + 1) > static_cast<double>(capacity_) * loadFactorMax_) {
            rehash(capacity_ * 2);
        }

        SweepSlot<INLINE_CAP> toInsert;
        toInsert.declId = id; toInsert.hashCache = h; toInsert.scopeId = curScope;
        toInsert.setName(name.data(), nameLen8); toInsert.probeDistance = 0;

        size_t mask = capacity_ - 1;
        size_t idx = h & mask;

        while (true) {
            auto& cur = slots_[idx];
            if (!cur.occupied()) {
                slots_[idx] = toInsert;
                scopeFrames_.back().push_back(idx);
                liveCount_++;
                return id;
            }
            if (cur.probeDistance < toInsert.probeDistance) {
                std::swap(toInsert, slots_[idx]);
                scopeFrames_.back().push_back(idx);
                liveCount_++;
                reinsertDisplaced(toInsert, idx);
                return id;
            }
            idx = (idx + 1) & mask;
            toInsert.probeDistance++;
        }
    }

    int resolve(const std::string& name) const {
        uint8_t len8 = static_cast<uint8_t>(name.size() > 255 ? 255 : name.size());
        uint64_t h64 = FnvHash::hash(name);
        uint32_t h = static_cast<uint32_t>(h64 ^ (h64 >> 32));
        size_t mask = capacity_ - 1;
        size_t idx = h & mask;
        uint8_t dist = 0;
        int bestId = -1, bestScope = -1;

        while (slots_[idx].occupied()) {
            const auto& s = slots_[idx];
            if (s.probeDistance < dist) break;
            if (s.hashCache == h && s.nameLen == len8 && matchName(s, name.data(), len8)) {
                if (static_cast<int>(s.scopeId) > bestScope) {
                    bestScope = s.scopeId; bestId = s.declId;
                }
            }
            idx = (idx + 1) & mask;
            dist++;
        }
        return bestId;
    }

private:
    static bool matchName(const SweepSlot<INLINE_CAP>& s, const char* data, uint8_t len) {
        if (s.repFlags & 1) {
            char* p; std::memcpy(&p, s.name.inlineBytes, sizeof(char*));
            return std::memcmp(p, data, len) == 0;
        }
        return std::memcmp(s.name.inlineBytes, data, len) == 0;
    }

    void backwardShift(size_t startIdx) {
        size_t mask = capacity_ - 1;
        size_t idx = startIdx & mask;
        while (true) {
            size_t next = (idx + 1) & mask;
            if (!slots_[next].occupied() || slots_[next].probeDistance == 0) break;
            slots_[idx] = slots_[next];
            slots_[idx].probeDistance--;
            slots_[next].nameLen = 0;
            idx = next;
        }
    }

    void reinsertDisplaced(SweepSlot<INLINE_CAP> src, size_t startIdx) {
        size_t mask = capacity_ - 1;
        size_t idx = (startIdx + 1) & mask;
        src.probeDistance++;
        while (true) {
            if (!slots_[idx].occupied()) { slots_[idx] = src; return; }
            if (slots_[idx].probeDistance < src.probeDistance) std::swap(src, slots_[idx]);
            idx = (idx + 1) & mask;
            src.probeDistance++;
        }
    }

    void rehash(size_t newCap) {
        std::vector<SweepSlot<INLINE_CAP>> old; old.swap(slots_);
        slots_.resize(newCap); capacity_ = newCap; liveCount_ = 0;
        for (auto& s : old) { if (s.occupied()) reinsertRehash(s); }
    }

    void reinsertRehash(SweepSlot<INLINE_CAP> src) {
        size_t mask = capacity_ - 1;
        size_t idx = src.hashCache & mask;
        src.probeDistance = 0;
        while (true) {
            if (!slots_[idx].occupied()) { slots_[idx] = src; liveCount_++; return; }
            if (slots_[idx].probeDistance < src.probeDistance) std::swap(src, slots_[idx]);
            idx = (idx + 1) & mask;
            src.probeDistance++;
        }
    }

    std::vector<SweepSlot<INLINE_CAP>> slots_;
    std::vector<std::vector<uint32_t>> scopeFrames_;
    size_t capacity_ = 16;
    size_t liveCount_ = 0;
    int nextId_ = 0;
    double loadFactorMax_ = 0.70;
};

struct Event {
    enum Kind { ENTER_SCOPE, EXIT_SCOPE, DECLARE, USE } kind;
    std::string symbol;
};

static std::vector<Event> loadEvents(const std::string& filepath) {
    std::vector<Event> events;
    std::ifstream in(filepath);
    if (!in) return events;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string tag; iss >> tag;
        Event ev;
        if (tag == "ENTER_SCOPE") ev.kind = Event::ENTER_SCOPE;
        else if (tag == "EXIT_SCOPE") ev.kind = Event::EXIT_SCOPE;
        else if (tag == "DECLARE") { ev.kind = Event::DECLARE; iss >> ev.symbol; }
        else if (tag == "USE") { ev.kind = Event::USE; iss >> ev.symbol; }
        events.push_back(ev);
    }
    return events;
}

template<size_t INLINE_CAP>
void runSweepConfig(std::ofstream& out, const std::string& corpus,
                    const std::vector<Event>& events, double loadFactor, HiResTimer& timer) {
    heap::resetPeak();
    heap::Scope hs;
    SweepTable<INLINE_CAP> t(loadFactor);
    for (auto& ev : events) {
        if (ev.kind == Event::ENTER_SCOPE) t.enterScope();
        else if (ev.kind == Event::EXIT_SCOPE) t.exitScope();
        else if (ev.kind == Event::DECLARE) t.insert(ev.symbol);
        else if (ev.kind == Event::USE) t.resolve(ev.symbol);
    }
    long long finalHeap = hs.bytes();

    // Timing (3 reps)
    std::vector<double> p95s;
    for (int rep = 0; rep < 3; ++rep) {
        SweepTable<INLINE_CAP> tT(loadFactor);
        std::vector<double> samples;
        for (auto& ev : events) {
            if (ev.kind == Event::ENTER_SCOPE) tT.enterScope();
            else if (ev.kind == Event::EXIT_SCOPE) tT.exitScope();
            else if (ev.kind == Event::DECLARE) tT.insert(ev.symbol);
            else if (ev.kind == Event::USE) {
                auto a = timer.now();
                volatile int id = tT.resolve(ev.symbol);
                auto b = timer.now();
                (void)id;
                samples.push_back(timer.microsecondsBetween(a, b));
            }
        }
        std::sort(samples.begin(), samples.end());
        p95s.push_back(samples[static_cast<size_t>(0.95 * (samples.size() - 1))]);
    }
    std::sort(p95s.begin(), p95s.end());
    double p95Us = p95s[1];

    out << corpus << "," << INLINE_CAP << "," << loadFactor << "," << finalHeap << "," << p95Us << "\n";
    std::cout << "  cap=" << INLINE_CAP << " load=" << loadFactor
              << " -> heap=" << finalHeap << "B  p95=" << p95Us << "us\n";
}

int main() {
    std::ofstream out("results/metis_x_parameter_sweep.csv");
    if (!out) return 1;
    out << "corpus,inline_cap,max_load_factor,final_heap_bytes,lookup_p95_us\n";

    HiResTimer timer;
    std::vector<std::string> devCorpora = {"FreeRTOS", "Arduino"}; // NO ZEPHYR / NO ESP-IDF

    std::cout << "=== METIS-X Step 8 Development Parameter Sweep ===\n";

    for (const auto& corpus : devCorpora) {
        std::string eventFile = "results/corpus_events_" + corpus + ".txt";
        if (!std::ifstream(eventFile)) eventFile = "data/corpus_events_" + corpus + ".txt";
        auto events = loadEvents(eventFile);
        if (events.empty()) continue;

        std::cout << "\n[ Development Corpus: " << corpus << " ]\n";

        for (double load : {0.60, 0.70, 0.80}) {
            runSweepConfig<8>(out, corpus, events, load, timer);
            runSweepConfig<12>(out, corpus, events, load, timer);
            runSweepConfig<16>(out, corpus, events, load, timer);
            runSweepConfig<24>(out, corpus, events, load, timer);
        }
    }

    out.flush();
    std::cout << "\nWrote results/metis_x_parameter_sweep.csv\n";
    return 0;
}
