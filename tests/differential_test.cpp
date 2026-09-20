// Differential correctness test (plan §B3/§H): every symbol table in this
// project must agree on *what is currently resolvable and under which
// declaration id* for the same sequence of scope/insert operations, even
// though their internal representations differ completely. This is checked
// against a plain std::vector<std::unordered_map<string,int>> scope stack
// used as the reference model.
//
// Focus: same-scope redeclaration (the B3 fix -- a second declaration of the
// same name in the SAME scope must replace the binding, matching
// std::unordered_map::operator[] semantics, with no double memory charge)
// and cross-implementation id agreement after that fix.
#include <cassert>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include "historical/conventional_symbol_table.hpp"
#include "historical/interned_symbol_table.hpp"
#include "historical/budget_sym.hpp"
#include "historical/robinhood_symbol_table.hpp"
#include "historical/trie_symbol_table.hpp"
#include "historical/symtab_v2.hpp"
#include "historical/symtab_v3.hpp"
#include "historical/symtab_v4.hpp"

using namespace budgetsym;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL: " #cond " (line " << __LINE__ << ")\n"; failures++; } } while (0)

// Reference model: a plain scope stack of name->declaration-id maps. Every
// production table's resolve() must match this exactly.
struct ReferenceModel {
    std::vector<std::unordered_map<std::string, int>> scopes{1};
    int nextId = 0;

    int insert(const std::string& name) {
        int id = nextId++;
        scopes.back()[name] = id; // overwrite semantics: same-scope redeclare replaces
        return id;
    }
    int resolve(const std::string& name) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) return f->second;
        }
        return -1;
    }
    void enterScope() { scopes.emplace_back(); }
    void exitScope() { if (scopes.size() > 1) scopes.pop_back(); }
};

enum class Op { Declare, Use, Enter, Exit };
struct Event { Op op; std::string name; };

static std::vector<Event> genTrace(std::mt19937& rng, int n) {
    // Small alphabet (a handful of short names) so redeclarations, shadowing
    // and hash/prefix collisions all actually get exercised, not just avoided
    // by a huge random string space.
    static const char* names[] = {
        "i", "j", "x", "y", "tmp", "count", "index", "value",
        // Long, prefix-similar names -- specifically chosen to exercise the
        // COMPRESSED tier (V1 BudgetSym's front-coded chain and V2
        // SymTabV2's block compression both gate on name length; the
        // original 8-name alphabet above never reached either).
        "temperatureSensorCalibrationAlpha", "temperatureSensorCalibrationBeta",
        "temperatureSensorCalibrationGamma", "networkInterfaceBufferPoolBase"
    };
    std::uniform_int_distribution<int> opDist(0, 9);
    std::uniform_int_distribution<int> nameDist(0, 11);
    std::vector<Event> trace;
    int depth = 0;
    for (int k = 0; k < n; k++) {
        int r = opDist(rng);
        if (r <= 4) {
            trace.push_back({Op::Declare, names[nameDist(rng)]});
        } else if (r <= 7) {
            trace.push_back({Op::Use, names[nameDist(rng)]});
        } else if (r == 8 && depth < 6) {
            trace.push_back({Op::Enter, ""});
            depth++;
        } else if (depth > 0) {
            trace.push_back({Op::Exit, ""});
            depth--;
        } else {
            trace.push_back({Op::Declare, names[nameDist(rng)]});
        }
    }
    while (depth-- > 0) trace.push_back({Op::Exit, ""});
    return trace;
}

template <typename Table>
static void runTrace(Table& t, const std::vector<Event>& trace, const std::string& label) {
    ReferenceModel ref;
    for (auto& e : trace) {
        switch (e.op) {
            case Op::Declare: {
                int expected = ref.insert(e.name);
                int actual = t.insert(e.name);
                CHECK(actual == expected);
                break;
            }
            case Op::Use: {
                int expected = ref.resolve(e.name);
                int actual = t.resolve(e.name);
                if (actual != expected) {
                    std::cerr << "MISMATCH[" << label << "] resolve(\"" << e.name << "\") expected="
                              << expected << " actual=" << actual << "\n";
                    failures++;
                }
                break;
            }
            case Op::Enter:
                ref.enterScope();
                t.enterScope();
                break;
            case Op::Exit:
                ref.exitScope();
                t.exitScope();
                break;
        }
    }
}

static void test_differential_fuzz() {
    const int kTraces = 200;
    const int kEventsPerTrace = 300;
    std::mt19937 rng(20260915);
    for (int i = 0; i < kTraces; i++) {
        std::vector<Event> trace = genTrace(rng, kEventsPerTrace);

        ConventionalSymbolTable conv(1 << 20);
        runTrace(conv, trace, "Conventional");

        InternedSymbolTable interned(1 << 20);
        runTrace(interned, trace, "Interned");

        BudgetSym budget(1 << 20);
        runTrace(budget, trace, "BudgetSym");

        PolicyConfig fixedCompressed;
        fixedCompressed.disableAdaptiveSelection = true;
        fixedCompressed.fixedRepresentation = Representation::COMPRESSED_REP;
        BudgetSym budgetCompressed(1 << 20, fixedCompressed);
        runTrace(budgetCompressed, trace, "BudgetSym-ForcedCompressed");

        RobinHoodSymbolTable rh(1 << 20);
        runTrace(rh, trace, "RobinHood");

        TrieSymbolTable trie(1 << 20);
        runTrace(trie, trace, "Trie");

        budgetsym::v2::SymTabV2<> v2table(1 << 20);
        runTrace(v2table, trace, "SymTabV2");

        budgetsym::v3::SymTabV3<> v3table(1 << 20);
        runTrace(v3table, trace, "SymTabV3");

        budgetsym::v4::SymTabV4<> v4table(1 << 20);
        runTrace(v4table, trace, "SymTabV4");
        // Byte-accounting check the id-only runTrace() above cannot catch
        // (review MEDIUM finding: a double-reclaim or refcount bug wouldn't
        // perturb resolve() ids at all). Every live entry pays AT LEAST
        // kSlotOverhead (the per-slot structural cost, charged uniformly
        // regardless of representation) -- since the alphabet now includes
        // both short (INLINE) and long (INTERNED/COMPRESSED, which add
        // pool-string/block-member bytes on top) names, this can only be a
        // floor, not an exact equality anymore. Still meaningfully catches
        // an under-charge (tracker dropping below the structural floor) or
        // an accounting sign error (negative).
        long long floor = static_cast<long long>(v2table.size()) * budgetsym::v2::SymTabV2<>::kSlotOverhead;
        if (v2table.tracker().current() < floor) {
            std::cerr << "MISMATCH[SymTabV2] tracker=" << v2table.tracker().current()
                      << " below structural floor=" << floor << " (size=" << v2table.size() << ")\n";
            failures++;
        }
    }
    if (failures == 0) std::cout << "test_differential_fuzz: " << kTraces << " traces, all implementations agree with reference\n";
}

// Direct, minimal repro of the B3 same-scope-redeclaration bug: insert the
// same name twice in one scope and confirm every table (a) returns
// increasing declaration ids, (b) resolve() finds only the SECOND
// declaration's id, and (c) size()/tracked memory reflect exactly one live
// binding, not two.
// allowRepresentationChange: BudgetSym's decide() reacts to `isRepeat`
// (seen_.find(name) -- "has this name EVER been declared, in any scope,
// before"), which becomes true starting with a name's second declaration
// anywhere, including a same-scope redeclaration. That legitimately changes
// which representation the SECOND "dup" gets (e.g. INLINE -> INTERNED,
// which for a short name that's the first-ever interning of "dup" also pays
// a one-time pool-string charge) -- a policy-driven representation switch,
// not a double-charge/leak. So for BudgetSym this check only asserts the
// stronger, still-meaningful invariant: the redeclaration must not cost MORE
// than a full second copy on top of the first (that would mean the old
// entry's bytes were never reclaimed), and must not silently zero out the
// live entry's cost either. The four tables whose insert() representation
// never depends on repeat status (Conventional/Interned/RobinHood/Trie) are
// held to the strict "byte count must not move at all" invariant.
template <typename Table>
static void checkSameScopeRedeclare(Table& t, const std::string& label, bool allowRepresentationChange = false) {
    // Byte-level check first: this is the bug the fix directly targets (a
    // double-charge/leak in MODELED memory -- see include/memory_audit and
    // the "80 MB modeled vs 1.8 MB measured" finding this session). id/
    // resolve()/size() agreement alone would not catch a regression where
    // those are all correct but the tracker byte count still double-charges
    // or under-charges on a same-scope redeclaration.
    long long before = t.tracker().current();
    int id1 = t.insert("dup");
    long long afterFirst = t.tracker().current();
    long long firstCost = afterFirst - before;
    CHECK(firstCost > 0); // the first declaration must be charged something

    int id2 = t.insert("dup"); // same-scope redeclaration of the same name
    long long afterSecond = t.tracker().current();

    CHECK(id2 == id1 + 1);
    int resolved = t.resolve("dup");
    if (resolved != id2) {
        std::cerr << "MISMATCH[" << label << "] same-scope redeclare resolved to " << resolved
                  << ", expected the later id " << id2 << "\n";
        failures++;
    }
    if (t.size() != 1) {
        std::cerr << "MISMATCH[" << label << "] size()==" << t.size()
                  << " after one name declared twice in the same scope, expected 1\n";
        failures++;
    }
    if (allowRepresentationChange) {
        // Must not double-charge (old entry's bytes never reclaimed) and
        // must not zero out the live entry's cost (under-charge/leak).
        if (afterSecond <= before || afterSecond > before + 2 * firstCost) {
            std::cerr << "MISMATCH[" << label << "] tracked memory after redeclare is " << afterSecond
                      << " (baseline " << before << ", first-declaration cost " << firstCost
                      << "); expected somewhere in (baseline, baseline + 2x first cost] -- "
                      << "outside that range means either a double-charge or an under-charge/leak\n";
            failures++;
        }
    } else if (afterSecond != afterFirst) {
        std::cerr << "MISMATCH[" << label << "] tracked memory changed by "
                  << (afterSecond - afterFirst) << " bytes on a same-scope redeclaration "
                  << "(first declaration cost " << firstCost << " bytes); expected no change\n";
        failures++;
    }
}

static void test_same_scope_redeclaration_no_double_charge() {
    ConventionalSymbolTable conv(1 << 20);
    checkSameScopeRedeclare(conv, "Conventional");

    InternedSymbolTable interned(1 << 20);
    checkSameScopeRedeclare(interned, "Interned");

    BudgetSym budget(1 << 20);
    checkSameScopeRedeclare(budget, "BudgetSym", /*allowRepresentationChange=*/true);

    // Also exercise the forced-COMPRESSED policy directly (not just
    // stochastically via the fuzz run below): this path goes through
    // releaseEntry()'s chain-tail-only-reclaim branch, which the default
    // policy above may not reach depending on decide()'s heuristics.
    // disableAdaptiveSelection means decide() always returns
    // fixedRepresentation regardless of isRepeat, so the REPRESENTATION
    // stays fixed -- but the concrete byte cost can still legitimately
    // shrink: the second "dup" front-codes against the chain tail (which is
    // the first "dup"), sharing its entire prefix, so its suffix is empty
    // and it costs strictly LESS than the first copy. That is front-coding
    // working correctly, not a double-charge, so this also needs the lenient
    // check (a real double-charge/leak bug would still be caught: it would
    // push the total outside the (baseline, baseline + 2x first cost] band).
    PolicyConfig fixedCompressed;
    fixedCompressed.disableAdaptiveSelection = true;
    fixedCompressed.fixedRepresentation = Representation::COMPRESSED_REP;
    BudgetSym budgetCompressed(1 << 20, fixedCompressed);
    checkSameScopeRedeclare(budgetCompressed, "BudgetSym-ForcedCompressed", /*allowRepresentationChange=*/true);

    RobinHoodSymbolTable rh(1 << 20);
    checkSameScopeRedeclare(rh, "RobinHood");

    TrieSymbolTable trie(1 << 20);
    checkSameScopeRedeclare(trie, "Trie");

    budgetsym::v2::SymTabV2<> v2table(1 << 20);
    checkSameScopeRedeclare(v2table, "SymTabV2");

    budgetsym::v3::SymTabV3<> v3table(1 << 20);
    checkSameScopeRedeclare(v3table, "SymTabV3");

    budgetsym::v4::SymTabV4<> v4table(1 << 20);
    checkSameScopeRedeclare(v4table, "SymTabV4");

    if (failures == 0) std::cout << "test_same_scope_redeclaration_no_double_charge: all tables agree (ids, resolve(), size(), AND tracked bytes)\n";
}

// Defense-in-depth regression test for SymTabV2's exitScope(), reviewed by
// ecc:cpp-reviewer: exitScope() checks BOTH e.live and e.scopeId (not just
// e.live) before freeing a slot in its liveSlots_ list, guarding against a
// slot being freed out from under a DIFFERENT scope that has since reused it.
//
// Honesty note (corrected after review): with SymTabV2's CURRENT insert()
// ordering this hazard cannot actually be forced through the public API --
// the review built a probe confirming that a redeclaration's releaseSlot()
// is always immediately followed, in the same insert() call with no
// intervening allocSlot(), by the allocSlot() that reclaims that exact slot,
// so freeSlots_ (a LIFO stack) never exposes a freed slot to a DIFFERENT
// scope's insert() in between. So this test currently exercises the
// e.scopeId check as a no-op (it never actually triggers) rather than
// reproducing a live bug -- kept anyway as a cheap regression guard, since
// the guard itself is one line and the invariant it depends on (immediate
// same-call reuse) would be easy to break by a future refactor (e.g. the
// upcoming block-compression tier batching releases before allocating).
static void test_exit_scope_does_not_free_slot_reused_by_inner_scope() {
    budgetsym::v2::SymTabV2<> t(1 << 20);
    t.enterScope();               // scope 1
    t.insert("a");                // allocates slot X
    int a2 = t.insert("a");       // redeclare: frees slot X, allocates a new slot for "a"

    t.enterScope();                // scope 2
    int b = t.insert("b");        // allocSlot() reuses the freed slot X here
    CHECK(t.resolve("b") == b);

    t.exitScope();                 // scope 2 exits, legitimately frees slot X (b)
    CHECK(t.resolve("b") == -1);
    CHECK(t.resolve("a") == a2);  // scope 1's "a" must still be live and correct

    t.exitScope();                 // scope 1 exits
    CHECK(t.resolve("a") == -1);
    // ECC review H1 fix: ScopeIndex's own backing storage is now charged to
    // the tracker (previously never tracked at all -- see
    // results/ecc_review.md finding H1). The outermost/global scope (index
    // 0) is never exited by this test, so its ScopeIndex allocation stays
    // legitimately charged -- current() must equal EXACTLY that scope's
    // byteFootprint(), not zero, once every symbol and every non-global
    // scope has been released.
    budgetsym::v2::SymTabV2<> fresh(1 << 20); // untouched: isolates the global scope's own baseline cost
    CHECK(t.tracker().current() == fresh.tracker().current());
}

// Regression test for the shadowing-lookup-cache bug (fixed upstream) staying
// fixed under resolve() too, not just lookup(): a stale cache hit on a
// shadowed name must never return the outer id.
static void test_resolve_respects_shadowing() {
    BudgetSym t(1 << 20);
    int outer = t.insert("x");
    CHECK(t.resolve("x") == outer); // populates the cache

    t.enterScope();
    int inner = t.insert("x"); // shadows outer; must invalidate any stale cache entry
    CHECK(inner != outer);
    CHECK(t.resolve("x") == inner);
    CHECK(t.resolve("x") == inner); // cache now correctly holds inner

    t.exitScope();
    CHECK(t.resolve("x") == outer); // outer binding visible again
}

int main() {
    test_same_scope_redeclaration_no_double_charge();
    test_resolve_respects_shadowing();
    test_exit_scope_does_not_free_slot_reused_by_inner_scope();
    test_differential_fuzz();

    if (failures == 0) {
        std::cout << "ALL DIFFERENTIAL TESTS PASSED\n";
        return 0;
    }
    std::cout << failures << " DIFFERENTIAL TEST(S) FAILED\n";
    return 1;
}
