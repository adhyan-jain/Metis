#pragma once
// Measured (not modeled) heap accounting for benchmark binaries.
//
// MemoryTracker (memory_tracker.hpp) sums hand-written per-entry cost
// constants. Those constants disagree with the real data layout (e.g.
// BudgetSym's Entry is 80 bytes but was charged 28-29, and several containers
// -- seen_, per-scope hash indexes, memo strings -- were never charged at
// all). This header replaces the global operator new/delete so a benchmark
// can measure the bytes a table *actually* has live on the heap, as reported
// by the allocator (malloc_usable_size: includes allocator size rounding,
// excludes the allocator's per-chunk header).
//
// USAGE: define BUDGETSYM_HEAP_COUNTER_IMPL in exactly ONE translation unit
// of a binary before including this header (every binary in this project is
// a single .cpp, so that is simply the main file).
//
// What is measured: every operator new allocation made by anything in the
// process between two snapshots. Callers must build input datasets BEFORE
// the baseline snapshot and keep only one table alive while measuring.
// Single-threaded use only. Sizes of the table objects themselves (if they
// live on the stack) are not heap and are not counted.
#include <cstddef>
#include <new>

namespace budgetsym {
namespace heap {

struct Counters {
    long long live = 0;        // bytes currently allocated through operator new
    long long peak = 0;        // max of live since the last resetPeak()
    long long allocations = 0; // cumulative operator new calls
};

Counters& counters();

inline long long liveBytes() { return counters().live; }
inline long long peakBytes() { return counters().peak; }
inline long long allocationCount() { return counters().allocations; }
inline void resetPeak() { counters().peak = counters().live; }

// RAII-style snapshot: values are relative to construction time.
class Scope {
public:
    Scope() : base_(liveBytes()), baseAllocs_(allocationCount()) { resetPeak(); }
    long long bytes() const { return liveBytes() - base_; }
    long long peakBytes() const { return heap::peakBytes() - base_; }
    long long allocations() const { return allocationCount() - baseAllocs_; }
private:
    long long base_;
    long long baseAllocs_;
};

} // namespace heap
} // namespace budgetsym

#ifdef BUDGETSYM_HEAP_COUNTER_IMPL
#include <cstdlib>
#if defined(__APPLE__)
#include <malloc/malloc.h>
#define BUDGETSYM_USABLE_SIZE(p) malloc_size(p)
#elif defined(_WIN32)
#include <malloc.h>
#define BUDGETSYM_USABLE_SIZE(p) _msize(p)
#else
#include <malloc.h>
#define BUDGETSYM_USABLE_SIZE(p) malloc_usable_size(p)
#endif

namespace budgetsym {
namespace heap {
Counters& counters() {
    static Counters c;
    return c;
}
namespace detail {
inline void* countedAlloc(std::size_t n) {
    if (n == 0) n = 1;
    void* p = std::malloc(n);
    if (!p) throw std::bad_alloc();
    Counters& c = counters();
    c.live += static_cast<long long>(BUDGETSYM_USABLE_SIZE(p));
    if (c.live > c.peak) c.peak = c.live;
    c.allocations++;
    return p;
}
inline void countedFree(void* p) {
    if (!p) return;
    counters().live -= static_cast<long long>(BUDGETSYM_USABLE_SIZE(p));
    std::free(p);
}
} // namespace detail
} // namespace heap
} // namespace budgetsym

void* operator new(std::size_t n) { return budgetsym::heap::detail::countedAlloc(n); }
void* operator new[](std::size_t n) { return budgetsym::heap::detail::countedAlloc(n); }
void operator delete(void* p) noexcept { budgetsym::heap::detail::countedFree(p); }
void operator delete[](void* p) noexcept { budgetsym::heap::detail::countedFree(p); }
void operator delete(void* p, std::size_t) noexcept { budgetsym::heap::detail::countedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { budgetsym::heap::detail::countedFree(p); }
#endif
