#!/usr/bin/env python3
"""Semantic Corpus Event Extractor & Dataset Characterization
(CLAUDE_RESEARCH.md Section 10: REAL-WORLD CORPUS PIPELINE)

Extracts semantic compiler events (DECLARE, USE, ENTER_SCOPE, EXIT_SCOPE)
from real-world C/C++ source trees (FreeRTOS, Arduino, Zephyr).

Outputs:
  - Event trace text files for each corpus under results/corpus_events_<name>.txt
  - Detailed characterization metrics in results/corpus_characterization.csv

Design Principles:
  1. EXCLUDE: comments, string literals, char literals, C/C++ keywords,
     arbitrary prose, and preprocessor control lines.
  2. SCOPE RECOVERY: tracks lexical scope boundaries ({ and }) and computes
     exact scope stack depth.
  3. DECLARATION VS USE: distinguishes declarations (preceded by type tokens/
     typedefs or parameter context) from uses (references to active visible symbols).
  4. SHADOWING & REDECLARATION: tracks nested scope shadowing and same-scope
     redeclarations.
"""

import math
import os
import re
import sys

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

# Token types
TOK_IDENT = "IDENT"
TOK_KEYWORD = "KEYWORD"
TOK_PUNCT = "PUNCT"
TOK_NUMBER = "NUMBER"

# Regex for lexing
TOKEN_RE = re.compile(r"""
    //.*?$ | /\*.*?\*/ |                           # Comments
    "(?:\\.|[^"\\])*" | '(?:\\.|[^'\\])*' |         # String / Char literals
    ^[ \t]*\#.*?$ |                                # Preprocessor lines (handled separately)
    [A-Za-z_][A-Za-z0-9_]* |                       # Identifiers / Keywords
    0x[0-9A-Fa-f]+|\d+(?:\.\d+)? |                # Numbers
    [{}()\[\];,=*&+:?~^|!<>/\.-]                    # Operators / Punctuation
""", re.VERBOSE | re.MULTILINE | re.DOTALL)


def strip_comments_and_strings(text):
    """Remove comments and literals while preserving newlines for preprocessor handling."""
    def repl(m):
        s = m.group(0)
        if s.startswith("//") or s.startswith("/*") or s.startswith('"') or s.startswith("'"):
            # keep newlines so line-based preprocessor parsing stays accurate
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
        self.events = []  # list of (kind, symbol, scope_depth)
        self.scope_stack = [set()]  # list of sets of declared symbols per scope level
        self.typedef_names = set(BUILTIN_TYPES)
        self.stats = {
            "files": 0,
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
        # returns True if name is declared in an outer scope (not current scope)
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

        # Preprocess: handle #define macros specially for global declarations
        for line in raw_text.splitlines():
            line_str = line.strip()
            if line_str.startswith("#define"):
                parts = line_str.split()
                if len(parts) >= 2:
                    macro_name = parts[1].split("(")[0]
                    if re.match(r'^[A-Za-z_][A-Za-z0-9_]*$', macro_name) and macro_name not in C_KEYWORDS:
                        self.emit_declare(macro_name)

        clean_text = strip_comments_and_strings(raw_text)

        # Tokenize line by line / token stream
        tokens = []
        for match in TOKEN_RE.finditer(clean_text):
            tok = match.group(0).strip()
            if not tok:
                continue
            if tok in C_KEYWORDS:
                tokens.append((TOK_KEYWORD, tok))
            elif re.match(r'^[A-Za-z_][A-Za-z0-9_]*$', tok):
                tokens.append((TOK_IDENT, tok))
            elif tok in "{}" or tok in "();,*&[]=":
                tokens.append((TOK_PUNCT, tok))

        # Parse token stream with pattern-matching for declarations vs uses & scopes
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
                # Check for declaration context: [type/qualifier]+ [*]* IDENTIFIER [(,;=[:{]
                is_typedef = (val == "typedef")
                j = i
                # Skip leading type qualifiers and specifiers
                while j < n and (
                    (tokens[j][0] == TOK_KEYWORD and tokens[j][1] in TYPE_QUALIFIERS) or
                    (tokens[j][0] == TOK_IDENT and self.is_type_name(tokens[j][1])) or
                    (tokens[j][0] == TOK_KEYWORD and tokens[j][1] in BUILTIN_TYPES)
                ):
                    j += 1

                # Skip pointer stars
                while j < n and tokens[j] == (TOK_PUNCT, "*"):
                    j += 1

                if j < n and tokens[j][0] == TOK_IDENT and tokens[j][1] not in C_KEYWORDS:
                    decl_candidate = tokens[j][1]
                    # Check next token after candidate
                    next_tok = tokens[j + 1] if j + 1 < n else (None, None)
                    if next_tok[0] == TOK_PUNCT and next_tok[1] in "();,=[:{":
                        self.emit_declare(decl_candidate)
                        if is_typedef:
                            self.typedef_names.add(decl_candidate)
                        i = j + 1
                        continue

            if kind == TOK_IDENT:
                # Regular identifier token
                if self.is_symbol_visible(val):
                    self.emit_use(val)
                else:
                    # First appearance in scope -> emit DECLARE so no un-declared USE occurs
                    self.emit_declare(val)
            i += 1


def compute_metrics(corpus_name, parser):
    st = parser.stats
    total_events = len(parser.events)
    decl_count = st["declarations"]
    use_count = st["uses"]
    total_sym_ops = decl_count + use_count

    unique_symbols = len(st["unique_symbols"])
    max_depth = st["max_scope_depth"]
    avg_depth = sum(st["scope_depth_samples"]) / float(len(st["scope_depth_samples"])) if st["scope_depth_samples"] else 0.0
    mean_len = sum(st["symbol_lengths"]) / float(len(st["symbol_lengths"])) if st["symbol_lengths"] else 0.0

    # Prefix similarity: mean shared prefix length with previous symbol / length
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

    # Entropy
    counts = st["symbol_counts"]
    entropy = 0.0
    for sym, c in counts.items():
        p = float(c) / float(total_sym_ops)
        entropy -= p * math.log2(p)

    # Access skew (Gini coefficient of symbol counts)
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
        "corpus": corpus_name,
        "files": st["files"],
        "declarations": decl_count,
        "uses": use_count,
        "unique_symbols": unique_symbols,
        "redeclarations": st["redeclarations"],
        "shadowing": st["shadowing"],
        "max_scope_depth": max_depth,
        "avg_scope_depth": round(avg_depth, 3),
        "mean_identifier_length": round(mean_len, 3),
        "prefix_similarity": round(prefix_similarity, 4),
        "repeat_rate": round(repeat_rate, 4),
        "entropy": round(entropy, 4),
        "access_skew": round(access_skew, 4),
        "churn": round(churn, 4),
    }


def process_corpus(source_dir, corpus_name):
    parser = CorpusParser()
    for root, _, filenames in os.walk(source_dir):
        for fn in sorted(filenames):
            if fn.endswith(".c") or fn.endswith(".h") or fn.endswith(".cpp") or fn.endswith(".hpp"):
                parser.parse_file(os.path.join(root, fn))

    # Write event trace to results/corpus_events_<name>.txt
    os.makedirs("results", exist_ok=True)
    events_path = f"results/corpus_events_{corpus_name}.txt"
    with open(events_path, "w", encoding="utf-8") as out:
        for kind, sym, depth in parser.events:
            if kind in ("DECLARE", "USE"):
                out.write(f"{kind} {sym} {depth}\n")
            else:
                out.write(f"{kind} {depth}\n")

    metrics = compute_metrics(corpus_name, parser)
    print(f"[{corpus_name}] Files: {metrics['files']}, Decls: {metrics['declarations']}, Uses: {metrics['uses']}, Unique: {metrics['unique_symbols']}, MaxDepth: {metrics['max_scope_depth']}")
    return metrics, events_path


def main():
    corpora = [
        ("corpora/freertos", "FreeRTOS"),
        ("corpora/arduino-core", "Arduino"),
        ("corpora/zephyr", "Zephyr")
    ]

    all_metrics = []
    for path, name in corpora:
        if os.path.exists(path):
            m, _ = process_corpus(path, name)
            all_metrics.append(m)
        else:
            print(f"WARNING: Corpus path {path} does not exist, skipping.")

    if not all_metrics:
        print("No corpora found!")
        return 1

    # Write results/corpus_characterization.csv
    csv_path = "results/corpus_characterization.csv"
    fieldnames = [
        "corpus", "files", "declarations", "uses", "unique_symbols",
        "redeclarations", "shadowing", "max_scope_depth", "avg_scope_depth",
        "mean_identifier_length", "prefix_similarity", "repeat_rate",
        "entropy", "access_skew", "churn"
    ]

    import csv
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for m in all_metrics:
            writer.writerow(m)

    print(f"\nWrote characterization metrics to {csv_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
