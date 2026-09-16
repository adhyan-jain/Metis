#!/usr/bin/env python3
"""Semantic Real-World Corpus Extractor & Characterization Engine (Phase 1)

Extracts semantic compiler events (DECLARE, USE, ENTER_SCOPE, EXIT_SCOPE)
from 15 candidate open-source C/C++ software repositories:
  1. FreeRTOS (Embedded RTOS Kernel)
  2. Arduino (Embedded Hardware Abstraction)
  3. Zephyr (Scalable Embedded OS)
  4. CPython (Interpreter / Language Core)
  5. Lua (Lightweight Embedded C Interpreter)
  6. SQLite (Embedded Relational Database Engine)
  7. FFmpeg (Multimedia / DSP Codec Engine)
  8. QEMU (Large System Emulator)
  9. mbedTLS (Embedded Security / Crypto Library)
 10. Redis (In-Memory Data Store)
 11. Nginx (Event-Driven Web Server)
 12. cJSON (Compact Data Format / JSON Library)
 13. protobuf-c (Serialization / Generated Code Runtime)
 14. ESP-IDF (Embedded IoT SDK)
 15. curl (Network Protocol Transfer Library)

Methodology:
  Identical token parsing, scope recovery ({, }), #define macro extraction,
  type token detection, and event emission across all repositories.

Outputs:
  - Event trace text files: data/corpus_events_<name>.txt
  - Characterization dataset: data/real_world_corpus_characterization.csv
"""

import math
import os
import re
import sys
import csv

C_KEYWORDS = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "inline", "int", "long", "register", "restrict", "return", "short",
    "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
    "unsigned", "void", "volatile", "while",
    # C99/C11 additions
    "_Bool", "_Complex", "_Imaginary", "_Generic", "_Noreturn", "_Static_assert",
    "_Thread_local", "bool", "true", "false",
    # C++ keywords
    "class", "namespace", "template", "typename", "public", "private", "protected",
    "virtual", "override", "final", "using", "try", "catch", "throw", "new", "delete",
    "operator", "this", "friend", "constexpr", "nullptr", "explicit", "mutable"
}

TYPE_QUALIFIERS = {
    "const", "volatile", "static", "extern", "register", "inline", "auto",
    "signed", "unsigned", "short", "long", "struct", "union", "enum", "typedef"
}

BUILTIN_TYPES = {
    "void", "char", "int", "float", "double", "bool", "_Bool",
    "int8_t", "int16_t", "int32_t", "int64_t",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "size_t", "ssize_t", "uintptr_t", "intptr_t", "ptrdiff_t",
    "BaseType_t", "UBaseType_t", "TickType_t", "TaskHandle_t", "QueueHandle_t"
}

TOK_IDENT = "IDENT"
TOK_KEYWORD = "KEYWORD"
TOK_PUNCT = "PUNCT"

TOKEN_RE = re.compile(r"""
    //.*?$ | /\*.*?\*/ |                           # Comments
    "(?:\\.|[^"\\])*" | '(?:\\.|[^'\\])*' |         # String / Char literals
    ^[ \t]*\#.*?$ |                                # Preprocessor lines
    [A-Za-z_][A-Za-z0-9_]* |                       # Identifiers / Keywords
    0x[0-9A-Fa-f]+|\d+(?:\.\d+)? |                # Numbers
    [{}()\[\];,=*&+:?~^|!<>/\.-]                    # Operators / Punctuation
""", re.VERBOSE | re.MULTILINE | re.DOTALL)


def strip_preprocessor_else_branches(text):
    """Drop #else..#endif alternate-branch code, keeping only the #if/#ifdef
    branch. Without this, generated code with idioms like
        #ifdef X
          if (a) {
        #else
          if (b) {
        #endif
            body();
          }
    causes the brace-counting scope tracker to see two '{' opens but only
    one matching '}', since both branches are tokenized additively. That
    imbalance compounds across files when a single parser instance is
    reused for a whole corpus."""
    out_lines = []
    stack = []  # True while inside an "else" branch to be dropped at this nesting level
    directive_re = re.compile(r'^\s*#\s*(if|ifdef|ifndef|else|elif|endif)\b')

    raw_lines = text.split("\n")
    i = 0
    while i < len(raw_lines):
        line = raw_lines[i]
        if line.lstrip().startswith("#"):
            # Merge backslash-continued directive lines (e.g. the common
            # "#if defined(FOO) && \" / "    defined(BAR)" idiom) into one
            # logical line before matching, so the continuation line is
            # recognized as part of the directive instead of leaking through
            # as ordinary code (which would inject spurious identifier
            # tokens from the continuation text). All physical lines in the
            # group are blanked together if it is a directive.
            j = i
            while raw_lines[j].endswith("\\") and j + 1 < len(raw_lines):
                j += 1
            full = " ".join(raw_lines[i:j + 1])
            m = directive_re.match(full)
            if m:
                kw = m.group(1)
                if kw in ("if", "ifdef", "ifndef"):
                    stack.append(False)
                elif kw in ("else", "elif") and stack:
                    stack[-1] = True
                elif kw == "endif" and stack:
                    stack.pop()
                for _ in range(i, j + 1):
                    out_lines.append("")
                i = j + 1
                continue
        if any(stack):
            out_lines.append("")
        else:
            out_lines.append(line)
        i += 1
    return "\n".join(out_lines)


def merge_qualified_identifiers(tokens):
    """Collapse IDENT '::' IDENT chains (a::b::c) into one identifier token
    so mean length / prefix-similarity reflect the real qualified name
    instead of its shortest segment. Namespaced generated code (protobuf,
    gRPC) makes this matter a lot; hand-written code rarely uses '::'."""
    merged = []
    i = 0
    n = len(tokens)
    while i < n:
        kind, val = tokens[i]
        if kind == TOK_IDENT:
            parts = [val]
            j = i + 1
            while (j + 1 < n and tokens[j] == (TOK_PUNCT, ":") and tokens[j + 1] == (TOK_PUNCT, ":")):
                j += 2
                if j < n and tokens[j][0] == TOK_IDENT:
                    parts.append(tokens[j][1])
                    j += 1
                else:
                    break
            if len(parts) > 1:
                merged.append((TOK_IDENT, "::".join(parts)))
                i = j
                continue
        merged.append((kind, val))
        i += 1
    return merged


def strip_comments_and_strings(text):
    def repl(m):
        s = m.group(0)
        if s.startswith("//") or s.startswith("/*") or s.startswith('"') or s.startswith("'"):
            return "\n" * s.count("\n")
        return s
    pattern = re.compile(
        r'//.*?$|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
        re.MULTILINE | re.DOTALL
    )
    return pattern.sub(repl, text)


class CorpusParser:
    def __init__(self):
        self.reset()

    def reset(self):
        self.events = []
        self.scope_stack = [set()]
        self.typedef_names = set(BUILTIN_TYPES)
        self.stats = {
            "files": 0,
            "loc": 0,
            "declarations": 0,
            "uses": 0,
            "unique_symbols": set(),
            "redeclarations": 0,
            "shadowing": 0,
            "max_scope_depth": 0,
            "scope_depth_samples": [],
            "symbol_lengths": [],
            "symbol_sequence": [],
            "symbol_counts": {},
            "enter_scope_count": 0,
            "exit_scope_count": 0,
        }

    def current_depth(self):
        return len(self.scope_stack) - 1

    def is_type_name(self, token):
        return token in BUILTIN_TYPES or token in self.typedef_names

    def is_symbol_visible(self, name):
        for scope_set in reversed(self.scope_stack):
            if name in scope_set:
                return True
        return False

    def is_symbol_shadowing(self, name):
        if len(self.scope_stack) < 2:
            return False
        for scope_set in self.scope_stack[:-1]:
            if name in scope_set:
                return True
        return False

    def emit_declare(self, name):
        depth = self.current_depth()
        is_same_scope_redecl = name in self.scope_stack[-1]
        is_shadow = self.is_symbol_shadowing(name)

        if is_same_scope_redecl:
            self.stats["redeclarations"] += 1
        elif is_shadow:
            self.stats["shadowing"] += 1

        self.scope_stack[-1].add(name)
        self.events.append(("DECLARE", name, depth))

        self.stats["declarations"] += 1
        self.stats["unique_symbols"].add(name)
        self.stats["scope_depth_samples"].append(depth)
        self.stats["symbol_lengths"].append(len(name))
        self.stats["symbol_sequence"].append(name)
        self.stats["symbol_counts"][name] = self.stats["symbol_counts"].get(name, 0) + 1

    def emit_use(self, name):
        depth = self.current_depth()
        self.events.append(("USE", name, depth))

        self.stats["uses"] += 1
        self.stats["scope_depth_samples"].append(depth)
        self.stats["symbol_lengths"].append(len(name))
        self.stats["symbol_sequence"].append(name)
        self.stats["symbol_counts"][name] = self.stats["symbol_counts"].get(name, 0) + 1

    def emit_enter_scope(self):
        self.scope_stack.append(set())
        depth = self.current_depth()
        if depth > self.stats["max_scope_depth"]:
            self.stats["max_scope_depth"] = depth
        self.events.append(("ENTER_SCOPE", "", depth))
        self.stats["enter_scope_count"] += 1

    def emit_exit_scope(self):
        if len(self.scope_stack) > 1:
            self.scope_stack.pop()
        depth = self.current_depth()
        self.events.append(("EXIT_SCOPE", "", depth))
        self.stats["exit_scope_count"] += 1

    def parse_file(self, file_path):
        self.stats["files"] += 1
        try:
            with open(file_path, "r", encoding="utf-8", errors="replace") as f:
                raw_text = f.read()
        except OSError:
            return

        self.stats["loc"] += raw_text.count("\n") + 1

        for line in raw_text.splitlines():
            line_str = line.strip()
            if line_str.startswith("#define"):
                parts = line_str.split()
                if len(parts) >= 2:
                    macro_name = parts[1].split("(")[0]
                    if re.match(r'^[A-Za-z_][A-Za-z0-9_]*$', macro_name) and macro_name not in C_KEYWORDS:
                        self.emit_declare(macro_name)

        clean_text = strip_comments_and_strings(raw_text)
        clean_text = strip_preprocessor_else_branches(clean_text)

        tokens = []
        for match in TOKEN_RE.finditer(clean_text):
            tok = match.group(0).strip()
            if not tok:
                continue
            if tok in C_KEYWORDS:
                tokens.append((TOK_KEYWORD, tok))
            elif re.match(r'^[A-Za-z_][A-Za-z0-9_]*$', tok):
                tokens.append((TOK_IDENT, tok))
            elif tok in "{}" or tok in "();,*&[]=:":
                tokens.append((TOK_PUNCT, tok))

        tokens = merge_qualified_identifiers(tokens)

        i = 0
        n = len(tokens)
        while i < n:
            kind, val = tokens[i]

            if kind == TOK_PUNCT:
                if val == "{":
                    self.emit_enter_scope()
                elif val == "}":
                    self.emit_exit_scope()
                i += 1
                continue

            if kind == TOK_KEYWORD or (kind == TOK_IDENT and self.is_type_name(val)):
                is_typedef = (val == "typedef")
                j = i
                while j < n and (
                    (tokens[j][0] == TOK_KEYWORD and tokens[j][1] in TYPE_QUALIFIERS) or
                    (tokens[j][0] == TOK_IDENT and self.is_type_name(tokens[j][1])) or
                    (tokens[j][0] == TOK_KEYWORD and tokens[j][1] in BUILTIN_TYPES)
                ):
                    j += 1

                while j < n and tokens[j] == (TOK_PUNCT, "*"):
                    j += 1

                if j < n and tokens[j][0] == TOK_IDENT and tokens[j][1] not in C_KEYWORDS:
                    decl_candidate = tokens[j][1]
                    next_tok = tokens[j + 1] if j + 1 < n else (None, None)
                    if next_tok[0] == TOK_PUNCT and next_tok[1] in "();,=[:{":
                        self.emit_declare(decl_candidate)
                        if is_typedef:
                            self.typedef_names.add(decl_candidate)
                        i = j + 1
                        continue

            if kind == TOK_IDENT:
                if self.is_symbol_visible(val):
                    self.emit_use(val)
                else:
                    self.emit_declare(val)
            i += 1


def compute_metrics(corpus_name, category, parser, limitations):
    st = parser.stats
    total_events = len(parser.events)
    decl_count = st["declarations"]
    use_count = st["uses"]
    total_sym_ops = decl_count + use_count

    unique_symbols = len(st["unique_symbols"])
    max_depth = st["max_scope_depth"]
    avg_depth = sum(st["scope_depth_samples"]) / float(len(st["scope_depth_samples"])) if st["scope_depth_samples"] else 0.0
    mean_len = sum(st["symbol_lengths"]) / float(len(st["symbol_lengths"])) if st["symbol_lengths"] else 0.0

    seq = st["symbol_sequence"]
    prefix_sim_sum = 0.0
    for k in range(1, len(seq)):
        s1, s2 = seq[k - 1], seq[k]
        common = 0
        min_l = min(len(s1), len(s2))
        while common < min_l and s1[common] == s2[common]:
            common += 1
        prefix_sim_sum += float(common) / float(len(s2)) if len(s2) > 0 else 0.0
    prefix_similarity = prefix_sim_sum / float(len(seq) - 1) if len(seq) > 1 else 0.0

    repeat_rate = float(use_count) / float(total_sym_ops) if total_sym_ops > 0 else 0.0

    counts = st["symbol_counts"]
    entropy = 0.0
    for sym, c in counts.items():
        p = float(c) / float(total_sym_ops)
        entropy -= p * math.log2(p)

    sorted_counts = sorted(counts.values())
    if sorted_counts:
        n_syms = len(sorted_counts)
        num_sum = sum((idx + 1) * count for idx, count in enumerate(sorted_counts))
        den_sum = n_syms * sum(sorted_counts)
        access_skew = (2.0 * num_sum / den_sum) - (float(n_syms + 1) / float(n_syms)) if den_sum > 0 else 0.0
    else:
        access_skew = 0.0

    churn = float(st["enter_scope_count"]) / float(total_events) if total_events > 0 else 0.0

    return {
        "repository": corpus_name,
        "category": category,
        "files": st["files"],
        "loc": st["loc"],
        "events": total_events,
        "declarations": decl_count,
        "uses": use_count,
        "unique_identifiers": unique_symbols,
        "redeclarations": st["redeclarations"],
        "shadowing": st["shadowing"],
        "max_scope_depth": max_depth,
        "avg_scope_depth": round(avg_depth, 3),
        "mean_identifier_length": round(mean_len, 3),
        "prefix_similarity": round(prefix_similarity, 4),
        "repeat_rate": round(repeat_rate, 4),
        "entropy": round(entropy, 4),
        "access_skew_gini": round(access_skew, 4),
        "scope_churn": round(churn, 4),
        "extraction_limitations": limitations
    }


def process_corpus(source_dir, corpus_name, category, limitations, max_files=None):
    parser = CorpusParser()
    file_count = 0
    for root, _, filenames in os.walk(source_dir):
        # Exclude tests or third-party vendor dirs if deep inside, but parse main code
        for fn in sorted(filenames):
            if fn.endswith(".c") or fn.endswith(".h") or fn.endswith(".cpp") or fn.endswith(".hpp"):
                full_path = os.path.join(root, fn)
                parser.parse_file(full_path)
                file_count += 1
                if max_files and file_count >= max_files:
                    break
        if max_files and file_count >= max_files:
            break

    os.makedirs("data", exist_ok=True)
    events_path = f"data/corpus_events_{corpus_name}.txt"
    with open(events_path, "w", encoding="utf-8") as out:
        for kind, sym, depth in parser.events:
            if kind in ("DECLARE", "USE"):
                out.write(f"{kind} {sym} {depth}\n")
            else:
                out.write(f"{kind} {depth}\n")

    # Also keep a copy under results/ if needed for backward compatibility
    os.makedirs("results", exist_ok=True)
    results_events_path = f"results/corpus_events_{corpus_name}.txt"
    with open(results_events_path, "w", encoding="utf-8") as out:
        for kind, sym, depth in parser.events:
            if kind in ("DECLARE", "USE"):
                out.write(f"{kind} {sym} {depth}\n")
            else:
                out.write(f"{kind} {depth}\n")

    metrics = compute_metrics(corpus_name, category, parser, limitations)
    print(f"[{corpus_name:<12}] Files: {metrics['files']:<5} LOC: {metrics['loc']:<8} Events: {metrics['events']:<8} Decls: {metrics['declarations']:<7} Unique: {metrics['unique_identifiers']:<6} PrefixSim: {metrics['prefix_similarity']:<6} Churn: {metrics['scope_churn']:<6}")
    return metrics


def main():
    candidates = [
        ("corpora/freertos", "FreeRTOS", "Embedded RTOS", "Heuristic C parser; macro definitions extracted; inline assembly ignored."),
        ("corpora/arduino-core", "Arduino", "Embedded Hardware Abstraction", "C++ headers and AVR C sources; template metaprogramming simplified."),
        ("corpora/zephyr", "Zephyr", "Scalable RTOS / Microkernel", "Large driver and kernel tree; Kconfig generated macros included."),
        ("corpora/cpython", "CPython", "Compiler / Interpreter C Core", "Python VM, object model, and AST parser C source tree."),
        ("corpora/lua", "Lua", "Lightweight Embedded Interpreter", "Compact single-pass C interpreter engine and VM opcode handling."),
        ("corpora/sqlite", "SQLite", "Embedded Relational Database Engine", "Amalgamated and core modular database execution engine C code."),
        ("corpora/ffmpeg", "FFmpeg", "Multimedia Codec / DSP Engine", "Large C codec, filter, and DSP library with heavy prefix conventions."),
        ("corpora/qemu", "QEMU", "Large System Emulator", "Virtualization and hardware peripheral emulation C codebase."),
        ("corpora/mbedtls", "mbedTLS", "Embedded Security / Crypto Library", "Cryptographic primitives and TLS protocol state machines in C."),
        ("corpora/redis", "Redis", "In-Memory Data Store", "Event-driven C server with custom data structures and RESP protocol."),
        ("corpora/nginx", "Nginx", "Event-Driven Web Server", "Asynchronous HTTP/mail proxy server core in ANSI C."),
        ("corpora/cjson", "cJSON", "Compact Data Format / JSON Library", "Single-file lightweight C JSON parser and formatter library."),
        ("corpora/protobuf-c", "protobuf-c", "Serialization / Generated Code Runtime", "Protocol buffers C runtime library and code generator interface."),
        ("corpora/esp-idf", "ESP-IDF", "Embedded IoT SDK", "Espressif ESP32 hardware abstraction and FreeRTOS wrapper SDK."),
        ("corpora/curl", "curl", "Network Protocol Transfer Library", "Multiprotocol file transfer library and CLI tool in C."),
        ("corpora/protobuf-generated-cpp-sample", "protobuf-generated-cpp", "Schema-Generated RPC/Serialization Code", "protoc --cpp_out generated C++ from a systematic 1-in-12 sample (611 of 7,325) of real-world googleapis.com .proto schemas; full corpus (1.8GB/14,650 files) caused OOM in the pure-Python extractor on this machine, so a deterministic, non-cherry-picked subsample was used. Long namespaced/qualified identifiers by construction."),
        ("/usr/include/llvm", "LLVM", "Compiler Infrastructure / Template-Heavy C++", "System-installed llvm-libs dev headers (unmodified upstream distribution package), full directory, no file selection."),
        ("/usr/include/clang", "Clang", "Compiler Frontend / AST-Heavy C++", "System-installed clang dev headers (unmodified upstream distribution package), full directory, no file selection."),
        ("/usr/include/qt6", "Qt6", "GUI Framework / Meta-Object System C++", "System-installed qt6-base dev headers (unmodified upstream distribution package), full directory, no file selection."),
        ("corpora/eigen", "Eigen", "Template-Metaprogramming Linear Algebra C++", "Shallow git clone of the official Eigen repository (gitlab.com/libeigen/eigen), full directory, no file selection."),
    ]

    print("==========================================================================")
    print("PHASE 1: Semantic Real-World Corpus Characterization (15 Candidates)")
    print("==========================================================================")

    all_metrics = []
    for path, name, category, limitations in candidates:
        if os.path.exists(path):
            m = process_corpus(path, name, category, limitations)
            all_metrics.append(m)
        else:
            print(f"WARNING: Corpus path {path} does not exist, skipping.")

    if not all_metrics:
        print("ERROR: No candidate corpora found!")
        return 1

    csv_path = "data/real_world_corpus_characterization.csv"
    fieldnames = [
        "repository", "category", "files", "loc", "events", "declarations", "uses",
        "unique_identifiers", "redeclarations", "shadowing", "max_scope_depth",
        "avg_scope_depth", "mean_identifier_length", "prefix_similarity",
        "repeat_rate", "entropy", "access_skew_gini", "scope_churn", "extraction_limitations"
    ]

    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for m in all_metrics:
            writer.writerow(m)

    print("==========================================================================")
    print(f"Phase 1 Characterization Complete! Output written to {csv_path}")
    print("==========================================================================")
    return 0


if __name__ == "__main__":
    sys.exit(main())
