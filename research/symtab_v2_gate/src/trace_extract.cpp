// Semantic symbol-table trace extractor built on libclang.
//
// Unlike scripts/extract_corpus_events.py (regex token stream where every unseen
// identifier becomes a DECLARE), this walks the real Clang AST:
//   DECLARE  : VarDecl/FunctionDecl/ParmVarDecl/TypedefDecl/EnumConstantDecl
//              emitted in the C ordinary-identifier namespace at its true lexical
//              scope (decl visible BEFORE its own initializer).
//   USE      : DeclRefExpr, and TypeRef that resolves to a typedef, emitted with
//              the *referenced* entity's name (so every USE resolves).
//   ENTER/EXIT: function-prototype scope, function body, compound stmt, for stmt.
//   One trace file per translation unit; the table is fresh per TU, like a compiler.
// Excluded on purpose (separate namespaces, not the ordinary-identifier table):
//   struct/union/enum tags, record members, labels, macros.
// Line format: "D name depth sys" | "U name depth sys" | "E" | "X"
#include <clang-c/Index.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct Ctx {
    FILE* out;
    int depth = 0;
    long decl = 0, use = 0, enter = 0, sysEvents = 0;
    int maxDepth = 0;
};

static std::string str(CXString s) {
    const char* c = clang_getCString(s);
    std::string r = c ? c : "";
    clang_disposeString(s);
    return r;
}
static bool inSystem(CXCursor c) { return clang_Location_isInSystemHeader(clang_getCursorLocation(c)) != 0; }
static bool hasFile(CXCursor c) {
    CXFile f; unsigned l, col, off;
    clang_getFileLocation(clang_getCursorLocation(c), &f, &l, &col, &off);
    return f != nullptr;
}

static CXChildVisitResult visit(CXCursor c, CXCursor, CXClientData d);

static void recurse(CXCursor c, Ctx* x) { clang_visitChildren(c, visit, x); }

static void enter(Ctx* x) { fprintf(x->out, "E\n"); x->depth++; x->enter++; if (x->depth > x->maxDepth) x->maxDepth = x->depth; }
static void leave(Ctx* x) { fprintf(x->out, "X\n"); x->depth--; }
static void declare(Ctx* x, CXCursor c, const std::string& n) {
    if (n.empty()) return;
    int sys = inSystem(c);
    fprintf(x->out, "D %s %d %d\n", n.c_str(), x->depth, sys);
    x->decl++; x->sysEvents += sys;
}
static void use(Ctx* x, CXCursor at, const std::string& n) {
    if (n.empty()) return;
    int sys = inSystem(at);
    fprintf(x->out, "U %s %d %d\n", n.c_str(), x->depth, sys);
    x->use++; x->sysEvents += sys;
}

static CXChildVisitResult visit(CXCursor c, CXCursor, CXClientData d) {
    Ctx* x = static_cast<Ctx*>(d);
    switch (clang_getCursorKind(c)) {
        case CXCursor_FunctionDecl: {
            std::string n = str(clang_getCursorSpelling(c));
            declare(x, c, n);
            bool scoped = clang_isCursorDefinition(c) || clang_Cursor_getNumArguments(c) > 0;
            if (scoped) enter(x);
            recurse(c, x);
            if (scoped) leave(x);
            return CXChildVisit_Continue;
        }
        case CXCursor_ParmDecl:
        case CXCursor_VarDecl:
        case CXCursor_TypedefDecl:
        case CXCursor_EnumConstantDecl:
            declare(x, c, str(clang_getCursorSpelling(c)));
            recurse(c, x);
            return CXChildVisit_Continue;
        case CXCursor_CompoundStmt:
        case CXCursor_ForStmt:
            enter(x); recurse(c, x); leave(x);
            return CXChildVisit_Continue;
        case CXCursor_DeclRefExpr: {
            CXCursor r = clang_getCursorReferenced(c);
            switch (clang_getCursorKind(r)) {
                case CXCursor_VarDecl: case CXCursor_ParmDecl: case CXCursor_FunctionDecl:
                case CXCursor_EnumConstantDecl:
                    if (hasFile(r)) use(x, c, str(clang_getCursorSpelling(r)));
                    break;
                default: break;
            }
            recurse(c, x);
            return CXChildVisit_Continue;
        }
        case CXCursor_TypeRef: {
            CXCursor r = clang_getCursorReferenced(c);
            if (clang_getCursorKind(r) == CXCursor_TypedefDecl && hasFile(r))
                use(x, c, str(clang_getCursorSpelling(r)));
            return CXChildVisit_Continue;
        }
        default:
            return CXChildVisit_Recurse;
    }
}

// usage: trace_extract <list> <outdir> ; list line: "<file>|<space separated clang args>"
int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage\n"); return 2; }
    std::ifstream in(argv[1]);
    std::string outdir = argv[2], line;
    CXIndex idx = clang_createIndex(0, 0);
    int n = 0;
    std::ofstream stats(outdir + "/_stats.csv");
    stats << "tu,file,decl,use,enter,max_depth,sys_events,errors\n";
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        size_t bar = line.find('|');
        std::string file = line.substr(0, bar);
        std::vector<std::string> a;
        if (bar != std::string::npos) { std::istringstream is(line.substr(bar + 1)); std::string t; while (is >> t) a.push_back(t); }
        std::vector<const char*> av;
        for (auto& s : a) av.push_back(s.c_str());
        CXTranslationUnit tu;
        if (clang_parseTranslationUnit2(idx, file.c_str(), av.data(), (int)av.size(), nullptr, 0,
                                        CXTranslationUnit_None, &tu) != CXError_Success) {
            fprintf(stderr, "parse failed: %s\n", file.c_str()); continue;
        }
        int errs = 0;
        for (unsigned i = 0; i < clang_getNumDiagnostics(tu); i++) {
            CXDiagnostic dg = clang_getDiagnostic(tu, i);
            if (clang_getDiagnosticSeverity(dg) >= CXDiagnostic_Error) errs++;
            clang_disposeDiagnostic(dg);
        }
        char path[512]; snprintf(path, sizeof path, "%s/tu%04d.trace", outdir.c_str(), n);
        Ctx x; x.out = fopen(path, "w");
        recurse(clang_getTranslationUnitCursor(tu), &x);
        fclose(x.out);
        stats << n << "," << file << "," << x.decl << "," << x.use << "," << x.enter << "," << x.maxDepth << "," << x.sysEvents << "," << errs << "\n";
        clang_disposeTranslationUnit(tu);
        n++;
    }
    clang_disposeIndex(idx);
    fprintf(stderr, "extracted %d TUs\n", n);
    return 0;
}
