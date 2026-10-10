#define METISX_TEST_HOOK
#define private public
#include "../include/metis_x.hpp"
#undef private

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <chrono>

using namespace budgetsym::metisx;

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

void profileCorpus(const std::string& corpusName) {
    std::string filepath = "data/corpus_events_" + corpusName + ".txt";
    auto events = loadEvents(filepath);
    if (events.empty()) {
        std::cout << "Could not load " << filepath << "\n";
        return;
    }

    MetisXTable t;
    long long totalTimeNs = 0;
    long long exitTimeNs = 0;
    long long insertTimeNs = 0;
    long long lookupTimeNs = 0;
    long long enterTimeNs = 0;

    int numExits = 0;

    // Warm-up
    for (int rep = 0; rep < 3; ++rep) {
        MetisXTable w;
        for (const auto& ev : events) {
            if (ev.kind == Event::ENTER_SCOPE) w.enterScope();
            else if (ev.kind == Event::EXIT_SCOPE) w.exitScope();
            else if (ev.kind == Event::DECLARE) w.insert(ev.symbol);
            else if (ev.kind == Event::USE) w.resolve(ev.symbol);
        }
    }

    // Measured run
    auto tStart = std::chrono::high_resolution_clock::now();
    for (const auto& ev : events) {
        if (ev.kind == Event::ENTER_SCOPE) {
            auto start = std::chrono::high_resolution_clock::now();
            t.enterScope();
            enterTimeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - start).count();
        }
        else if (ev.kind == Event::EXIT_SCOPE) {
            auto start = std::chrono::high_resolution_clock::now();
            t.exitScope();
            exitTimeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - start).count();
            numExits++;
        }
        else if (ev.kind == Event::DECLARE) {
            auto start = std::chrono::high_resolution_clock::now();
            t.insert(ev.symbol);
            insertTimeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - start).count();
        }
        else if (ev.kind == Event::USE) {
            auto start = std::chrono::high_resolution_clock::now();
            volatile int id = t.resolve(ev.symbol);
            (void)id;
            lookupTimeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - start).count();
        }
    }
    auto tEnd = std::chrono::high_resolution_clock::now();
    totalTimeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(tEnd - tStart).count();

    double totalMs = totalTimeNs / 1000000.0;
    double exitMs = exitTimeNs / 1000000.0;
    double insertMs = insertTimeNs / 1000000.0;
    double lookupMs = lookupTimeNs / 1000000.0;

    std::cout << "--- Corpus: " << corpusName << " ---\n";
    std::cout << "Total loop time: " << totalMs << " ms\n";
    std::cout << "Insert time:     " << insertMs << " ms (" << (insertMs/totalMs)*100.0 << "%)\n";
    std::cout << "Lookup time:     " << lookupMs << " ms (" << (lookupMs/totalMs)*100.0 << "%)\n";
    std::cout << "ExitScope time:  " << exitMs << " ms (" << (exitMs/totalMs)*100.0 << "%)\n";
    std::cout << "Total exits:     " << numExits << "\n";
}

int main() {
    profileCorpus("FreeRTOS");
    profileCorpus("Arduino");
    profileCorpus("Zephyr");
    profileCorpus("ESP-IDF");
    return 0;
}
