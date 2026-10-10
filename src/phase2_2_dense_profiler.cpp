#define METISX_TEST_HOOK
#define private public
#include "../include/metis_x.hpp"
#undef private

#include <iostream>
#include <vector>
#include <string>
#include <chrono>

using namespace budgetsym::metisx;

int main() {
    MetisXTable t;
    long long totalInsertNs = 0;
    long long totalExitNs = 0;
    
    // Warm-up
    for (int rep = 0; rep < 5; ++rep) {
        MetisXTable w;
        w.enterScope();
        for (int i=0; i<10000; ++i) w.insert("S" + std::to_string(i));
        w.exitScope();
    }
    
    // Measurement
    for (int rep = 0; rep < 100; ++rep) {
        t.enterScope();
        
        auto start = std::chrono::high_resolution_clock::now();
        for (int i=0; i<10000; ++i) {
            t.insert("S" + std::to_string(rep) + "_" + std::to_string(i));
        }
        auto end = std::chrono::high_resolution_clock::now();
        totalInsertNs += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        
        start = std::chrono::high_resolution_clock::now();
        t.exitScope();
        end = std::chrono::high_resolution_clock::now();
        totalExitNs += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    }
    
    double insMs = totalInsertNs / 1000000.0;
    double extMs = totalExitNs / 1000000.0;
    
    std::cout << "--- Synthetic Dense Scopes (100 repetitions of 10k declarations) ---\n";
    std::cout << "Insert Time: " << insMs << " ms\n";
    std::cout << "Exit Time:   " << extMs << " ms\n";
    std::cout << "Exit is " << (extMs / (insMs + extMs))*100.0 << "% of mutative work\n";
    return 0;
}
