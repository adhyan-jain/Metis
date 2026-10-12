// SymTab V2 research-gate harness.
//
// Identical semantic traces, identical API (enter/exit/declare/lookup), a single
// physical allocator-counting measurement boundary (operator new override, usable
// size), randomized implementation order, warm-up, paired repetitions.
//
// Modes:
//   check  <corpus>              differential correctness + scope-exit lifetime safety
//   mem    <corpus>              per-TU peak/final physical heap (deterministic)
//   time   <corpus> <reps>       aggregate ns/event + per-op latency quantiles
//   synth  <reps>                controlled synthetic sweep (clearly NOT source-derived)
//   ident                        V2 ablation-copy identity self-test
#define BUDGETSYM_HEAP_COUNTER_IMPL
#include "heap_counter.hpp"

#include <absl/container/flat_hash_map.h>
#include <x86intrin.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <glob.h>

#include "historical/conventional_symbol_table.hpp"
#include "historical/interned_symbol_table.hpp"
#include "historical/symtab_v2.hpp"
#include "symtab_v2_abl.hpp"
#include "hash_functions.hpp"

using namespace budgetsym;

// ------------------------------------------------------------------ trace model
struct Ev { uint8_t k; uint32_t n; };  // k: 0 ENTER 1 EXIT 2 DECL 3 USE 4 NEGPROBE(derived)
using TU = std::vector<Ev>;
static std::vector<std::string> g_names;

struct Workload {
    std::string name;
    std::vector<TU> tus;
    std::vector<std::string> tuFiles;
};

static std::vector<std::string> listGlob(const std::string& pat) {
    glob_t g; std::vector<std::string> r;
    if (glob(pat.c_str(), 0, nullptr, &g) == 0) for (size_t i = 0; i < g.gl_pathc; i++) r.push_back(g.gl_pathv[i]);
    globfree(&g); return r;
}

static std::unordered_map<std::string, uint32_t> g_nameIdx;
static uint32_t internName(const std::string& s) {
    auto it = g_nameIdx.find(s);
    if (it != g_nameIdx.end()) return it->second;
    uint32_t i = g_names.size(); g_names.push_back(s); g_nameIdx.emplace(s, i); return i;
}

// probeEvery: one derived negative probe per N uses (0 = none). Probe name is a real identifier that is
// declared LATER in the same TU and is verified invisible at the probe point (reference visibility count).
static void addProbes(TU& tu, int probeEvery, std::mt19937_64& rng) {
    if (probeEvery <= 0) return;
    std::vector<std::pair<size_t, uint32_t>> decls;
    for (size_t i = 0; i < tu.size(); i++) if (tu[i].k == 2) decls.push_back({i, tu[i].n});
    if (decls.empty()) return;
    std::unordered_map<uint32_t, int> vis;
    std::vector<std::vector<uint32_t>> stack(1);
    TU out; out.reserve(tu.size() + tu.size() / (probeEvery * 2));
    size_t di = 0; int useCnt = 0;
    for (size_t i = 0; i < tu.size(); i++) {
        const Ev& e = tu[i];
        if (e.k == 0) stack.emplace_back();
        else if (e.k == 1) { for (uint32_t n : stack.back()) vis[n]--; stack.pop_back(); }
        else if (e.k == 2) { vis[e.n]++; stack.back().push_back(e.n); di++; }
        out.push_back(e);
        if (e.k == 3 && ++useCnt % probeEvery == 0 && di < decls.size()) {
            for (int tries = 0; tries < 8; tries++) {
                size_t j = di + rng() % (decls.size() - di);
                uint32_t cand = decls[j].second;
                auto it = vis.find(cand);
                if (it == vis.end() || it->second <= 0) { out.push_back({4, cand}); break; }
            }
        }
    }
    tu.swap(out);
}

static Workload loadCorpus(const std::string& corpus, int probeEvery) {
    Workload w; w.name = corpus;
    std::string dir = "traces/" + corpus + "/";
    std::vector<int> bad;
    { std::ifstream st(dir + "_stats.csv"); std::string l; std::getline(st, l);
      while (std::getline(st, l)) { std::stringstream ss(l); std::string tu, f, d, u, e, md, sy, er;
        std::getline(ss, tu, ','); std::getline(ss, f, ','); std::getline(ss, d, ','); std::getline(ss, u, ',');
        std::getline(ss, e, ','); std::getline(ss, md, ','); std::getline(ss, sy, ','); std::getline(ss, er, ',');
        if (std::stoi(er) > 0) bad.push_back(std::stoi(tu)); } }
    std::mt19937_64 rng(12345);
    for (auto& f : listGlob(dir + "tu*.trace")) {
        int idx = std::stoi(f.substr(f.size() - 10, 4));
        if (std::find(bad.begin(), bad.end(), idx) != bad.end()) continue;
        std::ifstream in(f); std::string line; TU tu;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            char c = line[0];
            if (c == 'E') tu.push_back({0, 0});
            else if (c == 'X') tu.push_back({1, 0});
            else {
                size_t sp = line.find(' ', 2);
                std::string nm = line.substr(2, sp - 2);
                if (c == 'U' && nm.compare(0, 10, "__builtin_") == 0) continue;  // compiler builtin table, not symtab
                tu.push_back({(uint8_t)(c == 'D' ? 2 : 3), internName(nm)});
            }
        }
        addProbes(tu, probeEvery, rng);
        w.tus.push_back(std::move(tu)); w.tuFiles.push_back(f);
    }
    return w;
}

// ------------------------------------------------------------------ implementations
struct ConvT {
    ConventionalSymbolTable t{0};
    void enter() { t.enterScope(); } void exit() { t.exitScope(); }
    int declare(const std::string& s) { return t.insert(s); }
    int lookup(const std::string& s) { return t.resolve(s); }
};
struct InternT {
    InternedSymbolTable t{0};
    void enter() { t.enterScope(); } void exit() { t.exitScope(); }
    int declare(const std::string& s) { return t.insert(s); }
    int lookup(const std::string& s) { return t.resolve(s); }
};
struct AbslT {
    std::vector<absl::flat_hash_map<std::string, int>> sc{1};
    int next = 0;
    void enter() { sc.emplace_back(); } void exit() { if (sc.size() > 1) sc.pop_back(); }
    int declare(const std::string& s) { int id = next++; sc.back()[s] = id; return id; }
    int lookup(const std::string& s) { for (size_t i = sc.size(); i-- > 0;) { auto f = sc[i].find(s); if (f != sc[i].end()) return f->second; } return -1; }
};
// Compact embedded/Clang-style: one global identifier table (names interned once in a byte arena),
// per-identifier shadow chain, LIFO undo stack for scope exit. O(1) lookup independent of scope depth.
struct ScopedIdT {
    struct Id { uint32_t hash, off; uint16_t len; int32_t top; };
    struct Node { int32_t declId; int32_t prev; uint32_t idIdx; uint32_t depth; };
    std::vector<uint32_t> slots = std::vector<uint32_t>(64, UINT32_MAX);
    std::vector<char> arena; std::vector<Id> ids; std::vector<Node> nodes; std::vector<uint32_t> marks;
    int next = 0;
    static uint32_t h32(const std::string& s) { uint64_t h = FnvHash::hash(s); return (uint32_t)(h ^ (h >> 32)); }
    void grow() {
        std::vector<uint32_t> ns(slots.size() * 2, UINT32_MAX); size_t m = ns.size() - 1;
        for (uint32_t i = 0; i < ids.size(); i++) { size_t p = ids[i].hash & m; while (ns[p] != UINT32_MAX) p = (p + 1) & m; ns[p] = i; }
        slots.swap(ns);
    }
    uint32_t find(const std::string& s, uint32_t h) const {
        size_t m = slots.size() - 1, p = h & m;
        for (;;) { uint32_t i = slots[p]; if (i == UINT32_MAX) return UINT32_MAX;
            const Id& d = ids[i]; if (d.hash == h && d.len == s.size() && memcmp(&arena[d.off], s.data(), s.size()) == 0) return i;
            p = (p + 1) & m; }
    }
    void enter() { marks.push_back((uint32_t)nodes.size()); }
    void exit() { if (marks.empty()) return; uint32_t m = marks.back(); marks.pop_back();
        while (nodes.size() > m) { ids[nodes.back().idIdx].top = nodes.back().prev; nodes.pop_back(); } }
    int declare(const std::string& s) {
        uint32_t h = h32(s), i = find(s, h);
        if (i == UINT32_MAX) {
            if ((ids.size() + 1) * 10 > slots.size() * 7) grow();
            i = ids.size(); ids.push_back({h, (uint32_t)arena.size(), (uint16_t)s.size(), -1});
            arena.insert(arena.end(), s.begin(), s.end());
            size_t m = slots.size() - 1, p = h & m; while (slots[p] != UINT32_MAX) p = (p + 1) & m; slots[p] = i;
        }
        int id = next++; uint32_t depth = marks.size();
        int32_t top = ids[i].top;
        if (top >= 0 && nodes[top].depth == depth) { nodes[top].declId = id; return id; }
        nodes.push_back({id, top, i, depth}); ids[i].top = (int32_t)nodes.size() - 1; return id;
    }
    int lookup(const std::string& s) {
        uint32_t i = find(s, h32(s)); if (i == UINT32_MAX || ids[i].top < 0) return -1; return nodes[ids[i].top].declId;
    }
};

template <class Cfg> struct V2T {   // original unmodified header
    v2::SymTabV2<> t{0, Cfg::get()};
    void enter() { t.enterScope(); } void exit() { t.exitScope(); }
    int declare(const std::string& s) { return t.insert(s); }
    int lookup(const std::string& s) { return t.resolve(s); }
};
template <class Cfg> struct V2AblT { // ablation copy
    v2abl::SymTabV2<> t{0, Cfg::get()};
    void enter() { t.enterScope(); } void exit() { t.exitScope(); }
    int declare(const std::string& s) { return t.insert(s); }
    int lookup(const std::string& s) { return t.resolve(s); }
};
#define CFG(NAME, NS, BODY) struct NAME { static NS::PolicyConfigV2 get() { NS::PolicyConfigV2 c; BODY; return c; } };
CFG(CfgDefault, v2, (void)c)
CFG(CfgNoFP, v2, c.disableFingerprints = true)
CFG(CfgNoReclaim, v2, c.disableScopeReclamation = true)
CFG(CfgAllInterned, v2, c.inlineMaxLen = 0; c.compressMinLen = SIZE_MAX)         // adaptive policy removed -> interned only
CFG(CfgNoCompress, v2, c.compressMinLen = SIZE_MAX)                              // inline(<12)+interned
CFG(CfgCompressFirst, v2, c.inlineMaxLen = 0; c.compressMinLen = 0)              // compress unless repeat
CFG(AblDefault, v2abl, (void)c)
CFG(AblNoSlotRecycle, v2abl, c.disableSlotRecycle = true)
CFG(AblNoSeenMap, v2abl, c.omitSeenMap = true)
CFG(AblNoSeenNoCompress, v2abl, c.omitSeenMap = true; c.compressMinLen = SIZE_MAX)
CFG(AblNoSeenAllInterned, v2abl, c.omitSeenMap = true; c.inlineMaxLen = 0; c.compressMinLen = SIZE_MAX)

// ------------------------------------------------------------------ replay
template <class T> static inline void apply(T& t, const Ev& e, int* sink) {
    switch (e.k) {
        case 0: t.enter(); break;
        case 1: t.exit(); break;
        case 2: *sink ^= t.declare(g_names[e.n]); break;
        default: *sink ^= t.lookup(g_names[e.n]); break;
    }
}

static inline uint64_t rd() { unsigned a; _mm_lfence(); uint64_t t = __rdtscp(&a); _mm_lfence(); return t; }

struct Samples { std::vector<uint32_t> hit, miss, decl, enter, exit; };
template <class T> static void replayTimed(T& t, const TU& tu, Samples& s, int* sink) {
    for (const Ev& e : tu) {
        uint64_t a = rd(); int r = 0;
        switch (e.k) {
            case 0: t.enter(); break; case 1: t.exit(); break;
            case 2: r = t.declare(g_names[e.n]); break;
            default: r = t.lookup(g_names[e.n]); break;
        }
        uint64_t b = rd(); *sink ^= r;
        uint32_t d = (uint32_t)std::min<uint64_t>(b - a, UINT32_MAX);
        switch (e.k) { case 0: s.enter.push_back(d); break; case 1: s.exit.push_back(d); break;
                       case 2: s.decl.push_back(d); break; case 3: s.hit.push_back(d); break; default: s.miss.push_back(d); }
    }
}

// ------------------------------------------------------------------ registry
struct Impl {
    std::string name;
    std::function<void(const TU&, long long&, long long&, int*)> memRun;                // peak/final heap for one TU
    std::function<double(const Workload&, int*)> aggRun;                               // total wall ns for all TUs (no per-op timers)
    std::function<void(const Workload&, Samples&, int*)> timedRun;
    std::function<long long(const Workload&)> checkRun;                                 // mismatches vs reference
};

template <class T> static Impl makeImpl(const std::string& name) {
    Impl im; im.name = name;
    im.memRun = [](const TU& tu, long long& peak, long long& fin, int* sink) {
        heap::Scope hs;
        { T* t = new T(); for (const Ev& e : tu) apply(*t, e, sink); peak = hs.peakBytes(); fin = hs.bytes(); delete t; }
        long long leak = hs.bytes(); if (leak != 0) { static int warned = 0; if (!warned++) std::cerr << "LEAK? " << leak << " in " << typeid(T).name() << "\n"; }
    };
    im.aggRun = [](const Workload& w, int* sink) {
        auto a = std::chrono::steady_clock::now();
        for (const TU& tu : w.tus) { T* t = new T(); for (const Ev& e : tu) apply(*t, e, sink); delete t; }
        return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - a).count();
    };
    im.timedRun = [](const Workload& w, Samples& s, int* sink) {
        for (const TU& tu : w.tus) { T* t = new T(); replayTimed(*t, tu, s, sink); delete t; }
    };
    im.checkRun = [](const Workload& w) {
        long long bad = 0;
        for (const TU& tu : w.tus) {
            ConvT ref; T t;
            std::vector<std::vector<uint32_t>> declared(1);
            for (const Ev& e : tu) {
                if (e.k == 0) { ref.enter(); t.enter(); declared.emplace_back(); }
                else if (e.k == 1) {
                    ref.exit(); t.exit();
                    // lifetime safety: every name declared in the exited scope must now resolve exactly as in the reference
                    if (declared.size() > 1) { for (uint32_t n : declared.back()) if (ref.lookup(g_names[n]) != t.lookup(g_names[n])) bad++; declared.pop_back(); }
                } else if (e.k == 2) { int a = ref.declare(g_names[e.n]), b = t.declare(g_names[e.n]); if (a != b) bad++; declared.back().push_back(e.n); }
                else { if (ref.lookup(g_names[e.n]) != t.lookup(g_names[e.n])) bad++; }
            }
        }
        return bad;
    };
    return im;
}

static std::vector<Impl> allImpls() {
    std::vector<Impl> v;
    v.push_back(makeImpl<ConvT>("Conventional"));
    v.push_back(makeImpl<InternT>("Interned"));
    v.push_back(makeImpl<AbslT>("AbslFlat"));
    v.push_back(makeImpl<ScopedIdT>("ScopedId"));
    v.push_back(makeImpl<V2T<CfgDefault>>("V2"));
    v.push_back(makeImpl<V2T<CfgNoFP>>("V2_noFingerprint"));
    v.push_back(makeImpl<V2T<CfgNoReclaim>>("V2_noReclaim"));
    v.push_back(makeImpl<V2T<CfgAllInterned>>("V2_fixedInterned"));
    v.push_back(makeImpl<V2T<CfgNoCompress>>("V2_noCompress"));
    v.push_back(makeImpl<V2T<CfgCompressFirst>>("V2_compressFirst"));
    v.push_back(makeImpl<V2AblT<AblDefault>>("V2abl_identity"));
    v.push_back(makeImpl<V2AblT<AblNoSlotRecycle>>("V2_noSlotRecycle"));
    v.push_back(makeImpl<V2AblT<AblNoSeenMap>>("V2_noSeenMap_diag"));
    v.push_back(makeImpl<V2AblT<AblNoSeenNoCompress>>("V2_noSeenMap_noCompress_diag"));
    v.push_back(makeImpl<V2AblT<AblNoSeenAllInterned>>("V2_noSeenMap_fixedInterned_diag"));
    return v;
}

// ------------------------------------------------------------------ statistics helpers
static double pct(std::vector<uint32_t>& v, double p) {
    if (v.empty()) return 0; size_t k = (size_t)(p * (v.size() - 1)); std::nth_element(v.begin(), v.begin() + k, v.end()); return v[k];
}
static double tscGHz() {
    auto a = std::chrono::steady_clock::now(); uint64_t c0 = rd();
    while (std::chrono::steady_clock::now() - a < std::chrono::milliseconds(300)) {}
    uint64_t c1 = rd(); double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - a).count();
    return (c1 - c0) / ns;
}
static double timerOverheadCycles() {
    std::vector<uint32_t> v; for (int i = 0; i < 200000; i++) { uint64_t a = rd(); uint64_t b = rd(); v.push_back(b - a); }
    return pct(v, 0.5);
}

static void printHeader() { std::cout << "mode,workload,impl,rep,metric,value\n"; }
static void row(const char* mode, const std::string& w, const std::string& impl, int rep, const std::string& m, double v) {
    std::cout << mode << "," << w << "," << impl << "," << rep << "," << m << "," << v << "\n";
}

// ------------------------------------------------------------------ synthetic generator
struct SynthParams { int L; int prefix; int depth; double dup; double zipf; int tus; int decls; int usesPer; int churn; };
static TU synthTU(const SynthParams& p, std::mt19937_64& rng) {
    // vocabulary: names share family prefixes of length p.prefix; total length ~ L
    const int FAM = 24;
    auto mk = [&](uint64_t id) {
        std::string pre; int fam = id % FAM;
        for (int i = 0; i < p.prefix; i++) pre.push_back('a' + (fam * 7 + i * 3) % 26);
        std::string suf; uint64_t x = id * 2654435761u + 12345;
        while ((int)(pre.size() + suf.size()) < p.L) { suf.push_back('a' + (x % 26)); x = x * 6364136223846793005ULL + 1442695040888963407ULL; x >>= 3; }
        return pre + suf + (char)('0' + (id / FAM) % 10) + (char)('a' + (id / FAM / 10) % 26) + (char)('a' + (id / FAM / 260) % 26);
    };
    TU tu; uint64_t fresh = 0; int curDepth = 0;
    std::vector<std::vector<uint32_t>> vis(1);   // per scope visible declared name indices (this TU)
    auto pickVisible = [&]() -> int64_t {
        size_t n = 0; for (auto& s : vis) n += s.size(); if (!n) return -1;
        size_t k;
        if (p.zipf > 0) { double u = std::uniform_real_distribution<>(0, 1)(rng); k = (size_t)(std::pow(u, 1.0 + p.zipf * 2) * n); if (k >= n) k = n - 1; }
        else k = rng() % n;
        for (auto& s : vis) { if (k < s.size()) return s[k]; k -= s.size(); }
        return -1;
    };
    auto declareFresh = [&](bool dupOk) {
        uint32_t id;
        int64_t cand = -1;
        if (dupOk && std::uniform_real_distribution<>(0, 1)(rng) < p.dup) cand = pickVisible();
        if (cand >= 0) id = (uint32_t)cand; else id = internName(mk(fresh++));
        tu.push_back({2, id}); vis.back().push_back(id);
    };
    int G = p.decls / 4;
    for (int i = 0; i < G; i++) declareFresh(false);
    int remaining = p.decls - G;
    while (remaining > 0) {
        if (curDepth < p.depth && (curDepth == 0 || rng() % 100 < 60)) { tu.push_back({0, 0}); vis.emplace_back(); curDepth++; }
        else if (curDepth > 0 && (int)(rng() % 100) < p.churn) { tu.push_back({1, 0}); vis.pop_back(); curDepth--; }
        int k = 1 + rng() % 3;
        for (int j = 0; j < k && remaining > 0; j++, remaining--) declareFresh(true);
        for (int u = 0; u < p.usesPer; u++) { int64_t v = pickVisible(); if (v >= 0) tu.push_back({3, (uint32_t)v}); }
    }
    while (curDepth-- > 0) tu.push_back({1, 0});
    return tu;
}

// ------------------------------------------------------------------ main
int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "";
    auto impls = allImpls();
    if (mode == "ident") {
        Workload w = loadCorpus("lua", 0);
        long long diff = 0; int sink = 0; size_t checked = 0;
        Impl* a = nullptr; Impl* b = nullptr;
        for (auto& i : impls) { if (i.name == "V2") a = &i; if (i.name == "V2abl_identity") b = &i; }
        double maxRel = 0;
        for (auto& tu : w.tus) { long long p1, f1, p2, f2; a->memRun(tu, p1, f1, &sink); b->memRun(tu, p2, f2, &sink);
            maxRel = std::max(maxRel, std::fabs((double)(p2 - p1)) / p1); maxRel = std::max(maxRel, std::fabs((double)(f2 - f1)) / std::max(1LL, f1)); if (p1 != p2) diff++; checked++; }
        std::cout << "identity-check TUs=" << checked << " bytewise-differing=" << diff << " max_relative_diff=" << maxRel << " (size-class noise from larger config object)\n";
        return maxRel > 0.002;
    }
    if (mode == "check") {
        Workload w = loadCorpus(argv[2], 8);
        printHeader();
        for (auto& i : impls) row("check", w.name, i.name, 0, "mismatches", (double)i.checkRun(w));
        return 0;
    }
    if (mode == "mem") {
        int probe = argc > 3 ? std::atoi(argv[3]) : 8;
        Workload w = loadCorpus(argv[2], probe);
        std::cout << "workload,tu,file,impl,events,decls,uses,peak_heap,final_heap\n";
        int sink = 0;
        for (size_t ti = 0; ti < w.tus.size(); ti++) {
            size_t d = 0, u = 0; for (auto& e : w.tus[ti]) { d += e.k == 2; u += e.k == 3; }
            for (auto& i : impls) { long long p, f; i.memRun(w.tus[ti], p, f, &sink);
                std::cout << w.name << "," << ti << "," << w.tuFiles[ti] << "," << i.name << "," << w.tus[ti].size() << "," << d << "," << u << "," << p << "," << f << "\n"; }
        }
        return sink == 12345;
    }
    if (mode == "time") {
        int reps = std::atoi(argv[3]); int probe = argc > 4 ? std::atoi(argv[4]) : 8;
        Workload w = loadCorpus(argv[2], probe);
        double ghz = tscGHz(), ovh = timerOverheadCycles();
        std::cerr << "TSC GHz=" << ghz << " timer overhead cycles=" << ovh << " TUs=" << w.tus.size() << "\n";
        printHeader();
        size_t nev = 0; for (auto& tu : w.tus) nev += tu.size();
        row("meta", w.name, "-", 0, "tsc_ghz", ghz); row("meta", w.name, "-", 0, "timer_overhead_cycles", ovh); row("meta", w.name, "-", 0, "events", (double)nev);
        std::mt19937_64 rng(2024); int sink = 0;
        std::vector<int> order(impls.size()); for (size_t i = 0; i < order.size(); i++) order[i] = i;
        for (auto& i : impls) { i.aggRun(w, &sink); }   // warm-up (discarded)
        for (int rep = 0; rep < reps; rep++) {
            std::shuffle(order.begin(), order.end(), rng);
            for (int oi : order) {
                Impl& im = impls[oi];
                double ns = im.aggRun(w, &sink);
                row("time", w.name, im.name, rep, "agg_ns_per_event", ns / nev);
                Samples s; im.timedRun(w, s, &sink);
                auto emit = [&](const char* cat, std::vector<uint32_t>& v) {
                    if (v.empty()) return;
                    auto ns_ = [&](double c) { c -= ovh; return std::max(0.0, c) / ghz; };
                    row("time", w.name, im.name, rep, std::string(cat) + "_n", (double)v.size());
                    row("time", w.name, im.name, rep, std::string(cat) + "_p50_ns", ns_(pct(v, 0.50)));
                    row("time", w.name, im.name, rep, std::string(cat) + "_p95_ns", ns_(pct(v, 0.95)));
                    row("time", w.name, im.name, rep, std::string(cat) + "_p99_ns", ns_(pct(v, 0.99)));
                    row("time", w.name, im.name, rep, std::string(cat) + "_p999_ns", ns_(pct(v, 0.999)));
                    row("time", w.name, im.name, rep, std::string(cat) + "_max_ns", ns_(*std::max_element(v.begin(), v.end())));
                    double sum = 0; for (auto x : v) sum += std::max(0.0, x - ovh); row("time", w.name, im.name, rep, std::string(cat) + "_mean_ns", sum / v.size() / ghz);
                };
                emit("hit", s.hit); emit("miss", s.miss); emit("decl", s.decl); emit("enter", s.enter); emit("exit", s.exit);
            }
        }
        return sink == 12345;
    }
    if (mode == "synth") {
        int reps = std::atoi(argv[2]); int sink = 0;
        std::cout << "L,prefix,depth,dup,zipf,churn,impl,rep,peak_heap_mean,final_heap_mean,agg_ns_per_event,events,unique_names\n";
        std::mt19937_64 rng(777);
        for (int L : {6, 10, 16, 24, 40})
        for (int prefix : {0, 4, 8})
        for (int depth : {2, 8, 24})
        for (double dup : {0.0, 0.3, 0.7})
        for (double zipf : {0.0, 1.0}) {
            if (prefix >= L) continue;
            SynthParams p{L, prefix, depth, dup, zipf, 6, 3000, 2, 25};
            Workload w; w.name = "synth";
            for (int t = 0; t < p.tus; t++) w.tus.push_back(synthTU(p, rng));
            size_t nev = 0; std::unordered_map<uint32_t, int> uq; for (auto& tu : w.tus) { nev += tu.size(); for (auto& e : tu) if (e.k == 2) uq[e.n]++; }
            std::vector<int> order(impls.size()); for (size_t i = 0; i < order.size(); i++) order[i] = i;
            for (int rep = 0; rep < reps; rep++) {
                std::shuffle(order.begin(), order.end(), rng);
                for (int oi : order) {
                    Impl& im = impls[oi];
                    double pk = 0, fn = 0;
                    for (auto& tu : w.tus) { long long a, b; im.memRun(tu, a, b, &sink); pk += a; fn += b; }
                    double ns = im.aggRun(w, &sink);
                    std::cout << L << "," << prefix << "," << depth << "," << dup << "," << zipf << "," << p.churn << "," << im.name << "," << rep << ","
                              << pk / w.tus.size() << "," << fn / w.tus.size() << "," << ns / nev << "," << nev << "," << uq.size() << "\n";
                }
            }
        }
        return sink == 12345;
    }
    std::cerr << "usage: harness {ident|check <c>|mem <c> [probeEvery]|time <c> <reps> [probeEvery]|synth <reps>}\n";
    return 2;
}
