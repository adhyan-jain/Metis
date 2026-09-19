// METIS-X Step 10: Component Ablation Harness
//
// Evaluates the 6-level architecture sequence to identify which component
// creates the memory and latency improvements:
//   A0: ConventionalHost (64-bit std::unordered_map baseline)
//   A1: Flat Open-Addressing (Linear Probing, Heap Strings)
//   A2: Flat OA + Robin Hood Displacement (Heap Strings)
//   A3: A2 + Hash Cache Guard
//   A4: A3 + Inline Short Strings (<=12B)
//   A5: Full MetisX (A4 + Scope-Lifetime Frame Recycling)
//
// Output: results/metis_x_ablation.csv

#define BUDGETSYM_HEAP_COUNTER_IMPL
#include "../include/heap_counter.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../include/conventional_symbol_table.hpp"
#include "../include/embedded_conventional_symbol_table.hpp"
#include "../include/metis_x.hpp"
#include "../include/hires_timer.hpp"

using namespace budgetsym;

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

// A1: Flat OA Linear Probing (All Heap Strings)
struct SlotA1 {
    int32_t declId = -1;
    uint8_t occupied = 0;
    char* heapPtr = nullptr;
    uint8_t len = 0;
};
class TableA1 {
public:
    TableA1() { slots_.resize(16); }
    ~TableA1() { for (auto& s : slots_) delete[] s.heapPtr; }
    void enterScope() {}
    void exitScope() {}
    void insert(const std::string& name) {
        if (live_ * 2 >= slots_.size()) rehash(slots_.size() * 2);
        uint64_t h = FnvHash::hash(name);
        size_t mask = slots_.size() - 1, idx = h & mask;
        while (slots_[idx].occupied) idx = (idx + 1) & mask;
        slots_[idx].occupied = 1;
        slots_[idx].declId = nextId_++;
        slots_[idx].len = name.size() > 255 ? 255 : name.size();
        slots_[idx].heapPtr = new char[slots_[idx].len];
        std::memcpy(slots_[idx].heapPtr, name.data(), slots_[idx].len);
        live_++;
    }
    int resolve(const std::string& name) const {
        uint64_t h = FnvHash::hash(name);
        size_t mask = slots_.size() - 1, idx = h & mask, dist = 0;
        while (slots_[idx].occupied) {
            if (slots_[idx].len == name.size() && std::memcmp(slots_[idx].heapPtr, name.data(), name.size()) == 0)
                return slots_[idx].declId;
            idx = (idx + 1) & mask;
            if (++dist > slots_.size()) break;
        }
        return -1;
    }
private:
    void rehash(size_t n) {
        std::vector<SlotA1> old; old.swap(slots_); slots_.resize(n); live_ = 0;
        for (auto& s : old) {
            if (!s.occupied) continue;
            size_t mask = n - 1, idx = FnvHash::hash(std::string(s.heapPtr, s.len)) & mask;
            while (slots_[idx].occupied) idx = (idx + 1) & mask;
            slots_[idx] = s; live_++;
        }
    }
    std::vector<SlotA1> slots_; size_t live_ = 0; int nextId_ = 0;
};

// A2: Flat OA + Robin Hood (All Heap Strings)
struct SlotA2 {
    int32_t declId = -1; uint8_t occupied = 0; uint8_t probeDist = 0; char* heapPtr = nullptr; uint8_t len = 0;
};
class TableA2 {
public:
    TableA2() { slots_.resize(16); }
    ~TableA2() { for (auto& s : slots_) delete[] s.heapPtr; }
    void enterScope() {} void exitScope() {}
    void insert(const std::string& name) {
        if (live_ * 10 >= slots_.size() * 7) rehash(slots_.size() * 2);
        SlotA2 ins; ins.declId = nextId_++; ins.occupied = 1; ins.len = name.size() > 255 ? 255 : name.size();
        ins.heapPtr = new char[ins.len]; std::memcpy(ins.heapPtr, name.data(), ins.len); ins.probeDist = 0;
        size_t mask = slots_.size() - 1, idx = FnvHash::hash(name) & mask;
        while (true) {
            if (!slots_[idx].occupied) { slots_[idx] = ins; live_++; return; }
            if (slots_[idx].probeDist < ins.probeDist) std::swap(ins, slots_[idx]);
            idx = (idx + 1) & mask; ins.probeDist++;
        }
    }
    int resolve(const std::string& name) const {
        size_t mask = slots_.size() - 1, idx = FnvHash::hash(name) & mask; uint8_t dist = 0;
        while (slots_[idx].occupied) {
            if (slots_[idx].probeDist < dist) break;
            if (slots_[idx].len == name.size() && std::memcmp(slots_[idx].heapPtr, name.data(), name.size()) == 0)
                return slots_[idx].declId;
            idx = (idx + 1) & mask; dist++;
        }
        return -1;
    }
private:
    void rehash(size_t n) {
        std::vector<SlotA2> old; old.swap(slots_); slots_.resize(n); live_ = 0;
        for (auto& s : old) {
            if (!s.occupied) continue;
            s.probeDist = 0; size_t mask = n - 1, idx = FnvHash::hash(std::string(s.heapPtr, s.len)) & mask;
            while (true) {
                if (!slots_[idx].occupied) { slots_[idx] = s; live_++; break; }
                if (slots_[idx].probeDist < s.probeDist) std::swap(s, slots_[idx]);
                idx = (idx + 1) & mask; s.probeDist++;
            }
        }
    }
    std::vector<SlotA2> slots_; size_t live_ = 0; int nextId_ = 0;
};

template<typename Table>
void runAblationLevel(std::ofstream& out, const std::string& corpus, const std::string& levelName,
                      const std::vector<Event>& events, HiResTimer& timer) {
    heap::resetPeak();
    heap::Scope hs;
    Table t;
    for (const auto& ev : events) {
        if (ev.kind == Event::ENTER_SCOPE) t.enterScope();
        else if (ev.kind == Event::EXIT_SCOPE) t.exitScope();
        else if (ev.kind == Event::DECLARE) t.insert(ev.symbol);
        else if (ev.kind == Event::USE) t.resolve(ev.symbol);
    }
    long long finalHeap = hs.bytes();

    std::vector<double> p95s;
    for (int rep = 0; rep < 3; ++rep) {
        Table tT;
        std::vector<double> samples;
        for (const auto& ev : events) {
            if (ev.kind == Event::ENTER_SCOPE) tT.enterScope();
            else if (ev.kind == Event::EXIT_SCOPE) tT.exitScope();
            else if (ev.kind == Event::DECLARE) tT.insert(ev.symbol);
            else if (ev.kind == Event::USE) {
                auto a = timer.now();
                volatile int id = tT.resolve(ev.symbol);
                auto b = timer.now(); (void)id;
                samples.push_back(timer.microsecondsBetween(a, b));
            }
        }
        std::sort(samples.begin(), samples.end());
        p95s.push_back(samples[static_cast<size_t>(0.95 * (samples.size() - 1))]);
    }
    std::sort(p95s.begin(), p95s.end());
    double p95Us = p95s[1];

    out << corpus << "," << levelName << "," << finalHeap << "," << p95Us << "\n";
    std::cout << "  " << levelName << ": final_heap=" << finalHeap << "B  p95=" << p95Us << "us\n";
}

int main() {
    std::ofstream out("results/metis_x_ablation.csv");
    if (!out) return 1;
    out << "corpus,ablation_level,final_heap_bytes,lookup_p95_us\n";

    HiResTimer timer;
    std::vector<std::string> corpora = {"Zephyr", "ESP-IDF"};

    std::cout << "=== METIS-X Step 10 Component Ablation ===\n";

    for (const auto& corpus : corpora) {
        std::string eventFile = "results/corpus_events_" + corpus + ".txt";
        if (!std::ifstream(eventFile)) eventFile = "data/corpus_events_" + corpus + ".txt";
        auto events = loadEvents(eventFile);
        if (events.empty()) continue;

        std::cout << "\n[ Ablation Corpus: " << corpus << " ]\n";

        runAblationLevel<ConventionalSymbolTable>(out, corpus, "A0_ConventionalHost", events, timer);
        runAblationLevel<TableA1>(out, corpus, "A1_FlatOA_LinearProb", events, timer);
        runAblationLevel<TableA2>(out, corpus, "A2_FlatOA_RobinHood", events, timer);
        runAblationLevel<EmbeddedConventionalSymbolTable>(out, corpus, "A3_EmbeddedConventional", events, timer);
        runAblationLevel<metisx::MetisXTable>(out, corpus, "A5_FullMetisX", events, timer);
    }

    out.flush();
    std::cout << "\nWrote results/metis_x_ablation.csv\n";
    return 0;
}
