#define BUDGETSYM_HEAP_COUNTER_IMPL
#include "../include/heap_counter.hpp"

#include "../include/metis_x.hpp"
#include "../include/metis_x_arena.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <iomanip>

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

static double pctile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    double idx = p * (v.size() - 1);
    size_t i = static_cast<size_t>(idx);
    double frac = idx - i;
    if (i + 1 < v.size()) return v[i] * (1.0 - frac) + v[i+1] * frac;
    return v[i];
}

template<typename TableType>
void runBenchmark(const std::string& name, const std::string& corpusName, const std::vector<Event>& events, int reps) {
    std::vector<double> rep_p50;
    std::vector<double> rep_p95;
    std::vector<long long> rep_peakHeap;
    std::vector<long long> rep_finalHeap;
    std::vector<long long> rep_allocs;
    std::vector<double> rep_insertTimeMs;

    for (int r = 0; r < reps; r++) {
        heap::Scope hscope;
        
        TableType table;
        std::vector<double> lookupLats;
        lookupLats.reserve(events.size() / 2);
        
        auto tInsertStart = std::chrono::high_resolution_clock::now();
        double insertTotalNs = 0;
        
        for (const auto& ev : events) {
            if (ev.kind == Event::ENTER_SCOPE) {
                table.enterScope();
            }
            else if (ev.kind == Event::EXIT_SCOPE) {
                table.exitScope();
            }
            else if (ev.kind == Event::DECLARE) {
                auto s = std::chrono::high_resolution_clock::now();
                table.insert(ev.symbol);
                auto e = std::chrono::high_resolution_clock::now();
                insertTotalNs += std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
            }
            else if (ev.kind == Event::USE) {
                auto s = std::chrono::high_resolution_clock::now();
                volatile int id = table.resolve(ev.symbol);
                (void)id;
                auto e = std::chrono::high_resolution_clock::now();
                double ns = std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
                lookupLats.push_back(ns / 1000.0); // convert to us
            }
        }
        
        rep_p50.push_back(pctile(lookupLats, 0.50));
        rep_p95.push_back(pctile(lookupLats, 0.95));
        rep_peakHeap.push_back(hscope.peakBytes());
        rep_finalHeap.push_back(hscope.bytes());
        rep_allocs.push_back(hscope.allocations());
        rep_insertTimeMs.push_back(insertTotalNs / 1000000.0);
    }

    double med_p50 = pctile(rep_p50, 0.50);
    double med_p95 = pctile(rep_p95, 0.50);
    long long med_peakHeap = rep_peakHeap[rep_peakHeap.size()/2];
    std::sort(rep_peakHeap.begin(), rep_peakHeap.end());
    med_peakHeap = rep_peakHeap[rep_peakHeap.size()/2];
    
    std::sort(rep_finalHeap.begin(), rep_finalHeap.end());
    long long med_finalHeap = rep_finalHeap[rep_finalHeap.size()/2];

    std::sort(rep_allocs.begin(), rep_allocs.end());
    long long med_allocs = rep_allocs[rep_allocs.size()/2];

    std::sort(rep_insertTimeMs.begin(), rep_insertTimeMs.end());
    double med_insertTimeMs = rep_insertTimeMs[rep_insertTimeMs.size()/2];
    
    std::cout << std::left << std::setw(15) << name 
              << " | Peak Heap: " << std::setw(10) << med_peakHeap 
              << " | Final Heap: " << std::setw(10) << med_finalHeap 
              << " | Allocs: " << std::setw(8) << med_allocs
              << " | p50 us: " << std::setw(6) << std::fixed << std::setprecision(3) << med_p50 
              << " | p95 us: " << std::setw(6) << med_p95 
              << " | Insert(ms): " << std::setw(6) << med_insertTimeMs
              << "\n";
}

int main(int argc, char** argv) {
    std::vector<std::string> corpuses = {"ESP-IDF", "Zephyr"};
    
    for (const auto& corpus : corpuses) {
        std::cout << "--- Corpus: " << corpus << " ---\n";
        auto events = loadEvents("data/corpus_events_" + corpus + ".txt");
        if (events.empty()) {
            std::cerr << "Failed to load " << corpus << "\n";
            continue;
        }
        
        runBenchmark<metisx::MetisXTable>("Control", corpus, events, 5);
        runBenchmark<metisx_arena::MetisXArenaTable>("CandidateD", corpus, events, 5);
        std::cout << "\n";
    }
    return 0;
}
