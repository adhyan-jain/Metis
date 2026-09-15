#pragma once
// New comparison point (Task: "additional compression/storage techniques") --
// a shared trie (prefix tree) as an alternative to BudgetSym's COMPRESSED
// front-coding.
//
// Front-coding (include/budget_sym.hpp) only shares a prefix with the ONE
// identifier inserted immediately before it in the COMPRESSED chain -- a
// deliberate choice mirroring a real compiler's source-order declaration
// stream (see budget_sym.hpp's insert(), "Similarity signal..."). A trie
// shares prefixes across EVERY identifier ever inserted, project-wide, not
// just an adjacent pair: inserting "temperatureSensorOffset" after
// "networkInterfaceBuffer" gets no sharing at all from front-coding (they
// don't share a prefix with each other), but a later "temperatureSensorMax"
// still reuses the "temperatureSensor" path nodes laid down by the first,
// however many unrelated identifiers were inserted in between. That is a
// strictly more powerful sharing model -- at a real, different cost: a
// dedicated node (with its own child map) per distinct character position
// actually branches, not just a shared-prefix-length byte and a suffix
// string, so identifiers that DON'T share prefixes with anything already in
// the trie can cost MORE than front-coding's raw suffix bytes would. See
// docs/trie.md for measured numbers either way.
//
// A second, structural difference worth calling out: trie lookup does not
// need BudgetSym's hash-then-reconstruct step at all. Walking the query
// string's characters down the trie IS the equality check -- if every
// character has a matching child and the walk ends exactly on a terminal
// node, the string is present; there is nothing to reconstruct-and-compare
// afterward. This is the trie's actual answer to
// docs/faculty_questions.md's "Does compression hurt lookup?" -- not "make
// reconstruction cheaper" (the cache in include/lru_cache.hpp does that) but
// "avoid needing reconstruction in the first place, structurally."
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include "common.hpp"
#include "memory_tracker.hpp"

namespace budgetsym {

class TrieSymbolTable {
public:
    explicit TrieSymbolTable(size_t budgetBytes = 0) : tracker_(budgetBytes) {
        nodes_.emplace_back(); // node 0 = root, parent = -1
        scopeMaps_.emplace_back();
    }

    int enterScope() {
        scopeMaps_.emplace_back();
        return static_cast<int>(scopeMaps_.size()) - 1;
    }

    struct ScopeExitReport { size_t symbolsReleased = 0; long long bytesReclaimed = 0; };

    ScopeExitReport exitScope() {
        ScopeExitReport rep;
        if (scopeMaps_.size() <= 1) return rep;
        for (auto& kv : scopeMaps_.back()) {
            long long freed = releaseTerminal(kv.first) + kIndexEntryOverhead;
            tracker_.reclaim(freed);
            rep.bytesReclaimed += freed;
            rep.symbolsReleased++;
        }
        scopeMaps_.pop_back();
        return rep;
    }

    int insert(const std::string& name, int typeId = 0) {
        int id = nextId_++;
        SymbolMeta meta;
        meta.id = id;
        meta.scopeId = static_cast<int>(scopeMaps_.size()) - 1;
        meta.typeId = typeId;
        meta.representation = Representation::COMPRESSED_REP; // trie is a compression strategy, same label family
        // walkOrCreatePath() below walks the trie exactly ONCE regardless of
        // which case this turns out to be (brand-new name, shadow of an
        // outer scope's binding, or a true same-scope redeclaration) --
        // earlier versions called findTerminal() (a full walk) and then,
        // on the non-redeclare paths, internTrie() (a second full walk of
        // the same characters), doubling per-insert cost project-wide.
        int termIdx = walkOrCreatePath(name);
        // Same-scope redeclaration replaces the binding in place: no extra
        // terminal reference and no extra charge (earlier versions leaked a
        // refcount here, so the terminal mark could never be reclaimed).
        auto f = scopeMaps_.back().find(termIdx);
        if (f != scopeMaps_.back().end()) {
            f->second = meta;
            return id;
        }
        markTerminal(termIdx);
        scopeMaps_.back().emplace(termIdx, meta);
        tracker_.add(kIndexEntryOverhead);
        return id;
    }

    // See ConventionalSymbolTable::resolve().
    int resolve(const std::string& name) const {
        int termIdx = findTerminal(name);
        if (termIdx < 0) return -1; // never inserted at all -> definitely absent
        for (auto it = scopeMaps_.rbegin(); it != scopeMaps_.rend(); ++it) {
            auto f = it->find(termIdx);
            if (f != it->end()) return f->second.id;
        }
        return -1;
    }

    bool lookup(const std::string& name) const { return resolve(name) >= 0; }

    void recordAccess(const std::string& name) {
        int termIdx = findTerminal(name);
        if (termIdx < 0) return;
        for (auto it = scopeMaps_.rbegin(); it != scopeMaps_.rend(); ++it) {
            auto f = it->find(termIdx);
            if (f != it->end()) { f->second.accessCount++; return; }
        }
    }

    size_t size() const {
        size_t n = 0;
        for (auto& m : scopeMaps_) n += m.size();
        return n;
    }

    const MemoryTracker& tracker() const { return tracker_; }

    static const long long kIndexEntryOverhead = 28;       // SymbolMeta + 4-byte terminal-node index, per scope-map slot
    static const long long kNewNodeOverhead = 24;           // one unordered_map<char,int> child entry + TrieNode bookkeeping
    static const long long kTerminalMarkOverhead = 8;       // refcount + terminal flag, charged once when a node first becomes terminal

private:
    struct TrieNode {
        std::unordered_map<char, int> children;
        int parent = -1;
        char charFromParent = '\0';
        bool terminal = false;
        int refCount = 0;
    };

    // Walks/creates the path for `name`, charging kNewNodeOverhead for every
    // node actually created (i.e. NOT reusing an existing branch) -- this is
    // the trie's actual compression mechanism: a name whose full prefix
    // already exists character-for-character pays nothing here, regardless
    // of when or in what order that prefix was laid down. Does NOT touch the
    // terminal flag or refcount: insert() decides separately (via
    // markTerminal() below) whether this call represents a genuinely new
    // scope binding or a same-scope redeclaration, so this walk can be
    // shared by both cases instead of happening twice.
    int walkOrCreatePath(const std::string& name) {
        int cur = 0; // root
        for (char c : name) {
            auto& children = nodes_[cur].children;
            auto it = children.find(c);
            if (it != children.end()) {
                cur = it->second;
                continue;
            }
            TrieNode node;
            node.parent = cur;
            node.charFromParent = c;
            nodes_.push_back(node);
            int newIdx = static_cast<int>(nodes_.size()) - 1;
            nodes_[cur].children[c] = newIdx;
            tracker_.add(kNewNodeOverhead);
            cur = newIdx;
        }
        return cur;
    }

    // Marks node `idx` terminal (charging kTerminalMarkOverhead the first
    // time only) and bumps its refcount. Called from insert() exactly once
    // per genuinely new scope binding -- not for a same-scope redeclaration,
    // which reuses the existing terminal/refcount untouched.
    void markTerminal(int idx) {
        TrieNode& n = nodes_[idx];
        if (!n.terminal) {
            n.terminal = true;
            tracker_.add(kTerminalMarkOverhead);
        }
        n.refCount++;
    }

    int findTerminal(const std::string& name) const {
        int cur = 0;
        for (char c : name) {
            auto it = nodes_[cur].children.find(c);
            if (it == nodes_[cur].children.end()) return -1;
            cur = it->second;
        }
        return nodes_[cur].terminal ? cur : -1;
    }

    std::string reconstruct(int idx) const {
        std::string s;
        while (idx > 0) {
            s.push_back(nodes_[idx].charFromParent);
            idx = nodes_[idx].parent;
        }
        std::reverse(s.begin(), s.end());
        return s;
    }

    // Only the terminal mark's own cost is reclaimed when the last reference
    // drops -- the path nodes leading to it are NOT removed, even if no
    // other identifier shares any part of that path, because a node may
    // still be a non-terminal waypoint for other still-live identifiers
    // (removing it would require confirming no descendant is reachable from
    // anywhere else, which this implementation does not attempt). This is
    // the trie's version of the same documented trade-off
    // InternedSymbolTable's pool (docs/future_work.md item... pool slots
    // not compacted) and BudgetSym's COMPRESSED chain interior nodes
    // (docs/future_work.md item 1) already make -- stated, not hidden.
    long long releaseTerminal(int termIdx) {
        TrieNode& n = nodes_[termIdx];
        n.refCount--;
        if (n.refCount == 0 && n.terminal) {
            n.terminal = false;
            return kTerminalMarkOverhead;
        }
        return 0;
    }

    std::vector<TrieNode> nodes_;
    std::vector<std::unordered_map<int, SymbolMeta>> scopeMaps_;
    int nextId_ = 0;
    MemoryTracker tracker_;
};

} // namespace budgetsym
