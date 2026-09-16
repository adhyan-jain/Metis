// SymTabV2 Architectural Memory Decomposition & Counterfactual Diagnostic Engine
// (CLAUDE_RESEARCH.md & Architectural Memory Investigation)

#define BUDGETSYM_HEAP_COUNTER_IMPL
#include "../include/heap_counter.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "../include/symtab_v2.hpp"
#include "../include/conventional_symbol_table.hpp"
#include "../include/interned_symbol_table.hpp"
#include "../include/hires_timer.hpp"

using namespace budgetsym;
using namespace budgetsym::v2;

static void print_title(const std::string& t) { std::cout << t << "\n"; }

struct Event {
    enum Kind { ENTER_SCOPE, EXIT_SCOPE, DECLARE, USE } kind;
    std::string symbol;
    int depth = 0;
};

static std::vector<Event> loadEvents(const std::string& filepath) {
    std::vector<Event> events;
    std::ifstream in(filepath);
    if (!in) return events;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string tag;
        iss >> tag;
        Event ev;
        if (tag == "ENTER_SCOPE") {
            ev.kind = Event::ENTER_SCOPE;
            iss >> ev.depth;
        } else if (tag == "EXIT_SCOPE") {
            ev.kind = Event::EXIT_SCOPE;
            iss >> ev.depth;
        } else if (tag == "DECLARE") {
            ev.kind = Event::DECLARE;
            iss >> ev.symbol >> ev.depth;
        } else if (tag == "USE") {
            ev.kind = Event::USE;
            iss >> ev.symbol >> ev.depth;
        }
        events.push_back(ev);
    }
    return events;
}

// Inspector for SymTabV2 memory breakdown
template <typename HashFn = FnvHash>
class SymTabV2Inspector : public SymTabV2<HashFn> {
public:
    using Base = SymTabV2<HashFn>;
    using Base::Base;

    struct DetailedBreakdown {
        long long packedEntryBytes = 0;
        long long parallelVectorBytes = 0; // declIdOf + poolIndexOf + compressedRefOf + freeSlots
        long long scopeIndexBytes = 0;
        long long stringPoolBytes = 0;    // pool_ + poolRefCount_
        long long poolLookupMapBytes = 0; // poolLookup_
        long long everSeenSetBytes = 0;   // everSeenRep_
        long long blockStorageBytes = 0;  // blocks_ + member strings
        long long totalMeasuredHeap = 0;
        size_t totalEvents = 0;
        size_t declarations = 0;
        size_t uses = 0;
        size_t maxLiveSymbols = 0;
        size_t finalLiveSymbols = 0;
        size_t peakSlotCount = 0;
        size_t freedSlotsCount = 0;
        size_t poolUniqueStrings = 0;
        size_t everSeenUniqueStrings = 0;
        size_t totalBlocks = 0;
        size_t totalCompressedMembers = 0;
        size_t rawCompressedStringBytes = 0;
        size_t frontCodedStringBytes = 0;
        size_t activeScopesCount = 0;
    };

    DetailedBreakdown inspect(const std::vector<Event>& events) {
        DetailedBreakdown d;
        d.totalEvents = events.size();

        heap::Scope hs;
        size_t currentLive = 0;

        for (const auto& ev : events) {
            if (ev.kind == Event::ENTER_SCOPE) {
                this->enterScope();
            } else if (ev.kind == Event::EXIT_SCOPE) {
                auto rep = this->exitScope();
                if (currentLive >= rep.symbolsReleased) {
                    currentLive -= rep.symbolsReleased;
                } else {
                    currentLive = 0;
                }
            } else if (ev.kind == Event::DECLARE) {
                d.declarations++;
                currentLive++;
                if (currentLive > d.maxLiveSymbols) d.maxLiveSymbols = currentLive;
                this->insert(ev.symbol);
            } else if (ev.kind == Event::USE) {
                d.uses++;
                this->resolve(ev.symbol);
            }
        }

        d.totalMeasuredHeap = hs.peakBytes();
        d.finalLiveSymbols = this->size();
        d.peakSlotCount = this->peakSlotCount();
        d.freedSlotsCount = this->freeSlots_.size();
        d.activeScopesCount = this->scopes_.size();

        // 1. PackedEntry vector
        d.packedEntryBytes = static_cast<long long>(this->entries_.capacity() * sizeof(PackedEntry));

        // 2. Parallel vectors
        d.parallelVectorBytes = static_cast<long long>(
            this->declIdOf_.capacity() * sizeof(int) +
            this->poolIndexOf_.capacity() * sizeof(uint32_t) +
            this->compressedRefOf_.capacity() * sizeof(CompressedRef) +
            this->freeSlots_.capacity() * sizeof(uint32_t)
        );

        // 3. ScopeIndex across all open/retained scopes
        for (const auto& sc : this->scopes_) {
            d.scopeIndexBytes += sc.index.byteFootprint();
            d.scopeIndexBytes += static_cast<long long>(sc.liveSlots.capacity() * sizeof(uint32_t));
        }

        // 4. String pool
        d.poolUniqueStrings = this->pool_.size();
        d.stringPoolBytes = static_cast<long long>(
            this->pool_.capacity() * sizeof(std::string) +
            this->poolRefCount_.capacity() * sizeof(uint32_t)
        );
        for (const auto& str : this->pool_) {
            if (str.capacity() > 15) {
                d.stringPoolBytes += static_cast<long long>(str.capacity() + 1);
            }
        }

        // 5. Pool lookup map
        d.poolLookupMapBytes = static_cast<long long>(
            this->poolLookup_.bucket_count() * sizeof(void*) +
            this->poolLookup_.size() * (sizeof(std::string) + sizeof(uint32_t) + 32)
        );
        for (const auto& kv : this->poolLookup_) {
            if (kv.first.capacity() > 15) {
                d.poolLookupMapBytes += static_cast<long long>(kv.first.capacity() + 1);
            }
        }

        // 6. EverSeenRep set
        d.everSeenUniqueStrings = this->everSeenRep_.size();
        d.everSeenSetBytes = static_cast<long long>(
            this->everSeenRep_.bucket_count() * sizeof(void*) +
            this->everSeenRep_.size() * (sizeof(std::string) + 32)
        );
        for (const auto& str : this->everSeenRep_) {
            if (str.capacity() > 15) {
                d.everSeenSetBytes += static_cast<long long>(str.capacity() + 1);
            }
        }

        // 7. Block storage
        d.totalBlocks = this->blocks_.size();
        d.blockStorageBytes = static_cast<long long>(this->blocks_.capacity() * sizeof(Block));
        for (const auto& blk : this->blocks_) {
            d.blockStorageBytes += static_cast<long long>(blk.members.capacity() * sizeof(BlockMember));
            for (const auto& m : blk.members) {
                d.totalCompressedMembers++;
                if (m.suffix.capacity() > 15) {
                    d.blockStorageBytes += static_cast<long long>(m.suffix.capacity() + 1);
                }
                d.frontCodedStringBytes += m.suffix.size();
            }
        }

        return d;
    }
};

static long long measureConventionalPeakHeap(const std::vector<Event>& events) {
    heap::Scope hs;
    ConventionalSymbolTable t(0);
    for (const auto& ev : events) {
        if (ev.kind == Event::ENTER_SCOPE) t.enterScope();
        else if (ev.kind == Event::EXIT_SCOPE) t.exitScope();
        else if (ev.kind == Event::DECLARE) t.insert(ev.symbol);
        else if (ev.kind == Event::USE) t.resolve(ev.symbol);
    }
    return hs.peakBytes();
}

static long long measureInternedPeakHeap(const std::vector<Event>& events) {
    heap::Scope hs;
    InternedSymbolTable t(0);
    for (const auto& ev : events) {
        if (ev.kind == Event::ENTER_SCOPE) t.enterScope();
        else if (ev.kind == Event::EXIT_SCOPE) t.exitScope();
        else if (ev.kind == Event::DECLARE) t.insert(ev.symbol);
        else if (ev.kind == Event::USE) t.resolve(ev.symbol);
    }
    return hs.peakBytes();
}

int main() {
    std::cout << "==========================================================================\n";
    print_title("V2 PHYSICAL MEMORY DECOMPOSITION & COUNTERFACTUAL ANALYSIS");
    std::cout << "==========================================================================\n\n";

    std::vector<std::string> corpora = {"FreeRTOS", "Arduino", "Zephyr", "CPython", "Lua", "ESP-IDF"};

    for (const auto& corp : corpora) {
        std::string eventFile = "data/corpus_events_" + corp + ".txt";
        if (!std::ifstream(eventFile)) {
            eventFile = "results/corpus_events_" + corp + ".txt";
        }
        auto events = loadEvents(eventFile);
        if (events.empty()) continue;

        SymTabV2Inspector<> inspector(0);
        auto d = inspector.inspect(events);
        long long convPeak = measureConventionalPeakHeap(events);
        long long intPeak = measureInternedPeakHeap(events);

        std::cout << "==========================================================================\n";
        std::cout << "CORPUS: " << corp << "\n";
        std::cout << "Events: " << d.totalEvents << " | Decls: " << d.declarations 
                  << " | Peak Live: " << d.maxLiveSymbols << " | Final Live: " << d.finalLiveSymbols << "\n";
        std::cout << "--------------------------------------------------------------------------\n";
        std::cout << "MEASURED PEAK HEAP COMPARISON:\n";
        std::cout << "  Conventional Peak Heap: " << std::setw(12) << convPeak << " B (" 
                  << std::fixed << std::setprecision(2) << (convPeak / 1024.0 / 1024.0) << " MB)\n";
        std::cout << "  Interned Peak Heap:     " << std::setw(12) << intPeak << " B (" 
                  << std::fixed << std::setprecision(2) << (intPeak / 1024.0 / 1024.0) << " MB)\n";
        std::cout << "  SymTabV2 Peak Heap:     " << std::setw(12) << d.totalMeasuredHeap << " B (" 
                  << std::fixed << std::setprecision(2) << (d.totalMeasuredHeap / 1024.0 / 1024.0) << " MB)\n";
        std::cout << "  V2 vs Conventional Ratio: " << std::fixed << std::setprecision(2) 
                  << (static_cast<double>(d.totalMeasuredHeap) / convPeak) << "x\n";
        std::cout << "--------------------------------------------------------------------------\n";

        double heapMB = d.totalMeasuredHeap > 0 ? static_cast<double>(d.totalMeasuredHeap) : 1.0;

        std::cout << "\nV2 COMPONENT DECOMPOSITION:\n";
        std::cout << std::left << std::setw(34) << "Memory Component" 
                  << std::right << std::setw(14) << "Bytes" 
                  << std::setw(16) << "B / Decl Sym" 
                  << std::setw(16) << "B / Peak Live" 
                  << std::setw(12) << "% Heap" << "\n";
        std::cout << "--------------------------------------------------------------------------\n";

        auto printLine = [&](const std::string& name, long long bytes) {
            double pct = (bytes / heapMB) * 100.0;
            double bDecl = d.declarations > 0 ? static_cast<double>(bytes) / d.declarations : 0.0;
            double bLive = d.maxLiveSymbols > 0 ? static_cast<double>(bytes) / d.maxLiveSymbols : 0.0;
            std::cout << std::left << std::setw(34) << name 
                      << std::right << std::setw(14) << bytes 
                      << std::setw(16) << std::fixed << std::setprecision(1) << bDecl 
                      << std::setw(16) << std::fixed << std::setprecision(1) << bLive 
                      << std::setw(11) << std::fixed << std::setprecision(1) << pct << "%\n";
        };

        printLine("1. PackedEntry Storage (56B/slot)", d.packedEntryBytes);
        printLine("2. Parallel Metadata Vectors", d.parallelVectorBytes);
        printLine("3. ScopeIndex Open-Addr Table", d.scopeIndexBytes);
        printLine("4. String Pool (pool_)", d.stringPoolBytes);
        printLine("5. Pool Lookup Map (poolLookup_)", d.poolLookupMapBytes);
        printLine("6. EverSeen Set (everSeenRep_)", d.everSeenSetBytes);
        printLine("7. Block Compression Storage", d.blockStorageBytes);
        std::cout << "--------------------------------------------------------------------------\n";

        // Counterfactual Variant Calculations
        long long variantA_NoScopeIndex = d.totalMeasuredHeap - d.scopeIndexBytes;
        long long variantB_NoParallelVecs = d.totalMeasuredHeap - d.parallelVectorBytes;
        // Variant C: 16B PackedEntry instead of 56B -> saves (56 - 16) = 40B per slot
        long long entrySavings = static_cast<long long>(d.peakSlotCount * 40);
        long long variantC_16BEntry = d.totalMeasuredHeap - entrySavings;
        // Variant D: Scope-local arena / purge persistent maps on scope exit
        long long variantD_PurgedMaps = d.totalMeasuredHeap - (d.everSeenSetBytes + (d.poolLookupMapBytes / 2));
        // Variant E: No everSeenRep_ set
        long long variantE_NoEverSeen = d.totalMeasuredHeap - d.everSeenSetBytes;

        // Combined Ideal V2:
        // Peak Live * (16B Compact Entry + 16B ScopeIndex slot) + String Pool Unique Bytes
        long long combinedIdealV2 = static_cast<long long>(
            d.maxLiveSymbols * (16 + 16) + d.stringPoolBytes
        );

        std::cout << "\nCOUNTERFACTUAL DIAGNOSTIC VARIANTS (Hypothetical Peak Memory):\n";
        std::cout << std::left << std::setw(45) << "Variant Description" 
                  << std::right << std::setw(14) << "Heap Bytes" 
                  << std::setw(12) << "MB" 
                  << std::setw(16) << "vs Conventional" << "\n";
        std::cout << "--------------------------------------------------------------------------\n";
        auto printVariant = [&](const std::string& name, long long bytes) {
            double mb = bytes / 1024.0 / 1024.0;
            double ratio = static_cast<double>(bytes) / convPeak;
            std::cout << std::left << std::setw(45) << name 
                      << std::right << std::setw(14) << bytes 
                      << std::setw(12) << std::fixed << std::setprecision(2) << mb 
                      << std::setw(15) << std::fixed << std::setprecision(2) << ratio << "x\n";
        };

        printVariant("Baseline V2 (Measured)", d.totalMeasuredHeap);
        printVariant("Variant A: Exclude ScopeIndex Footprint", variantA_NoScopeIndex);
        printVariant("Variant B: Exclude Parallel Vectors", variantB_NoParallelVecs);
        printVariant("Variant C: Compact 16B PackedEntry Layout", variantC_16BEntry);
        printVariant("Variant D: Scope-Exit Map Purging", variantD_PurgedMaps);
        printVariant("Variant E: Exclude everSeenRep_ Set", variantE_NoEverSeen);
        printVariant("Combined Idealized V2 Architecture", combinedIdealV2);

        std::cout << "--------------------------------------------------------------------------\n";
        std::cout << "IDEALIZED TARGET FOOTPRINTS AT VARIOUS B/SYM GOALS:\n";
        for (int bSym : {32, 24, 16, 12, 8}) {
            long long targetBytes = static_cast<long long>(d.maxLiveSymbols * bSym);
            double ratio = static_cast<double>(targetBytes) / convPeak;
            std::cout << "  At " << std::setw(2) << bSym << " B/live-sym: " 
                      << std::setw(10) << targetBytes << " B (" 
                      << std::setw(6) << std::fixed << std::setprecision(2) << (targetBytes / 1024.0 / 1024.0) << " MB) -> "
                      << std::setw(5) << std::fixed << std::setprecision(2) << ratio << "x of Conventional\n";
        }
        std::cout << "\n\n";
    }

    return 0;
}
