// Blocker A diagnostic: replay ONE trace (data/corpus_events_Zephyr.txt) through the two
// different classes that are both called "EmbeddedConventionalSymbolTable" in the repository,
// with and without the std::unordered_set<std::string> uniqueSymbols object that
// src/metis_x_bench_main.cpp keeps inside the measured heap::Scope.
//
//   HIST = include/historical/embedded_conventional_symbol_table.hpp  (flat arena + RH index)
//   CAP  = class of the same name in research/capacity-architecture/src/capacity_proto_bench.cpp
//          (git: research/final-claim-battle), vector<unordered_map<string,int>> per scope
//
// Build: g++ -O2 -std=c++17 -I include research/paper_final/scripts/blockerA_diag.cpp -o <out>
#define BUDGETSYM_HEAP_COUNTER_IMPL
#include "heap_counter.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "historical/embedded_conventional_symbol_table.hpp"
#include "metis_x.hpp"

using namespace budgetsym;

class CapEC {
public:
    void enterScope() { scopes_.push_back({}); }
    void exitScope() { if (!scopes_.empty()) scopes_.pop_back(); }
    void insert(const std::string& n, int v) { if (scopes_.empty()) enterScope(); scopes_.back()[n] = v; }
    bool lookup(const std::string& n) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) if (it->find(n) != it->end()) return true;
        return false;
    }
private:
    std::vector<std::unordered_map<std::string, int>> scopes_;
};

struct Ev { int k; std::string s; };  // 0 enter 1 exit 2 declare 3 use

static std::vector<Ev> load(const std::string& p) {
    std::vector<Ev> v; std::ifstream f(p); std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        size_t s1 = line.find(' ');
        if (s1 == std::string::npos) continue;
        std::string cmd = line.substr(0, s1), rest = line.substr(s1 + 1);
        Ev e;
        if (cmd == "ENTER_SCOPE") e.k = 0; else if (cmd == "EXIT_SCOPE") e.k = 1;
        else if (cmd == "DECLARE" || cmd == "USE") {
            e.k = cmd == "DECLARE" ? 2 : 3;
            size_t s2 = rest.rfind(' '); e.s = s2 != std::string::npos ? rest.substr(0, s2) : rest;
        } else continue;
        v.push_back(std::move(e));
    }
    return v;
}

template <class T, bool Cap> static void run(const char* name, const std::vector<Ev>& ev, bool withUnique) {
    long long fin, pk; size_t nu = 0, openScopes = 0; long long depth = 0, maxd = 0;
    {
        heap::resetPeak(); heap::Scope hs;
        T t;
        std::unordered_set<std::string> u;
        for (auto& e : ev) {
            if (e.k == 0) { t.enterScope(); depth++; if (depth > maxd) maxd = depth; }
            else if (e.k == 1) { t.exitScope(); if (depth > 0) depth--; }
            else if (e.k == 2) { if (withUnique) u.insert(e.s); if constexpr (Cap) t.insert(e.s, 1); else t.insert(e.s); }
            else { if (withUnique) u.insert(e.s); if constexpr (Cap) t.lookup(e.s); else { t.resolve(e.s); t.recordAccess(e.s); } }
        }
        fin = hs.bytes(); pk = hs.peakBytes(); nu = u.size(); openScopes = depth;
    }
    std::cout << name << (withUnique ? " +uniqueSet" : " bare      ") << " final=" << fin << " peak=" << pk
              << " uniq=" << nu << " scopes_open_at_end=" << openScopes << " max_depth=" << maxd << "\n";
}

int main(int argc, char** argv) {
    auto ev = load(argc > 1 ? argv[1] : "data/corpus_events_Zephyr.txt");
    long long enter = 0, exit_ = 0, dec = 0, use = 0;
    for (auto& e : ev) (e.k == 0 ? enter : e.k == 1 ? exit_ : e.k == 2 ? dec : use)++;
    std::cout << "events=" << ev.size() << " enter=" << enter << " exit=" << exit_ << " declare=" << dec << " use=" << use << "\n";
    { heap::Scope hs; std::unordered_set<std::string> u; for (auto& e : ev) if (e.k >= 2) u.insert(e.s);
      std::cout << "uniqueSet alone: n=" << u.size() << " heap=" << hs.bytes() << "\n"; }
    run<EmbeddedConventionalSymbolTable, false>("HIST_EC", ev, false);
    run<EmbeddedConventionalSymbolTable, false>("HIST_EC", ev, true);
    run<CapEC, true>("CAP_EC ", ev, false);
    run<CapEC, true>("CAP_EC ", ev, true);
    run<metisx::MetisXTable, false>("METIS_X", ev, false);
    run<metisx::MetisXTable, false>("METIS_X", ev, true);
    return 0;
}
