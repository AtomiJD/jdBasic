#!/usr/bin/env python
"""
Scans src/ and embedded/ for all register_native("NAME", ...) calls and
regenerates both syntax-highlighting artifacts:

  1. src/natives_list.h
     Used by the built-in console editor to colour known native functions.

  2. syntaxes/jdbasic.tmLanguage.json
     TextMate grammar used by the VS Code extension. This file is kept in the
     jdBasic repository; the extension's installed copy must be updated
     separately (or via `code --install-extension` / marketplace update).

The script preserves the top-level structure of the existing tmLanguage file
and only rewrites three `match` regexes:
  - "entity.name.type.class.jdbasic" -> all dotted namespaces (AI, GUI, ...)
  - "support.function.jdbasic" (top-level)   -> all non-dotted native names
  - "support.function.jdbasic" (dotted-member) -> all suffixes of dotted names

Run from the project root:
    python tools/update_syntax_highlighting.py
"""

import json
import os
import re
import sys


SRC_DIR = "src"
# The board ports register their own builtins outside src/, and they are
# written on a desktop like everything else, so the editor should colour
# TOUCH and SD.MOUNT even on a machine that has neither.
SCAN_DIRS = ["src", "embedded"]
NATIVES_LIST_H = os.path.join(SRC_DIR, "natives_list.h")
TMLANG_FILE = os.path.join("syntaxes", "jdbasic.tmLanguage.json")


def collect_natives():
    """Returns a sorted list of all unique names passed to register_native."""
    names = set()
    rx = re.compile(r'register_native\("([A-Z][A-Z0-9._$]*)"')
    for base in SCAN_DIRS:
        if not os.path.isdir(base):
            continue
        for root, _, files in os.walk(base):
            for fn in files:
                if fn.endswith((".cpp", ".h")):
                    path = os.path.join(root, fn)
                    with open(path, "r", encoding="utf-8", errors="ignore") as f:
                        for line in f:
                            for m in rx.findall(line):
                                names.add(m)
    return sorted(names)


def split_natives(names):
    """
    Splits the native list into three buckets for the TextMate grammar:

      top_level   : names without a dot (e.g. MATMUL, LEN, IOTA)
      prefixes    : first component of dotted names (AI, GUI, GFX, ...)
      suffixes    : component after the dot (LOAD_LLM, BUTTON, ...)
      all_parts   : union of all three — used by the internal editor
    """
    top_level = set()
    prefixes = set()
    suffixes = set()
    for n in names:
        if "." in n:
            parts = n.split(".")
            if len(parts) >= 1:
                prefixes.add(parts[0])
            if len(parts) >= 2:
                suffixes.add(parts[1])
        else:
            top_level.add(n)
    all_parts = top_level | prefixes | suffixes
    return {
        "top_level": sorted(top_level),
        "prefixes":  sorted(prefixes),
        "suffixes":  sorted(suffixes),
        "all_parts": sorted(all_parts),
    }


def write_natives_list_h(all_parts):
    lines = []
    lines.append("// Auto-generated list of all native function name parts.")
    lines.append("// Used by the editor for syntax highlighting.")
    lines.append("// Do not edit by hand - regenerate via")
    lines.append("//   python tools/update_syntax_highlighting.py")
    lines.append("#pragma once")
    lines.append("#include <string>")
    lines.append("#include <unordered_set>")
    lines.append("")
    lines.append("inline const std::unordered_set<std::string>& native_names() {")
    lines.append("    static const std::unordered_set<std::string> ns = {")
    row = []
    for p in all_parts:
        row.append('"' + p + '"')
        if len(row) == 4:
            lines.append("        " + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("        " + ", ".join(row))
    lines.append("    };")
    lines.append("    return ns;")
    lines.append("}")
    with open(NATIVES_LIST_H, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote {} ({} entries)".format(NATIVES_LIST_H, len(all_parts)))


def escape_for_regex(name):
    # TextMate regexes need '$' escaped
    return name.replace("$", "\\$")


def build_top_level_regex(top_level):
    # Sort longer names first so prefixes don't win (e.g. REGEX_MATCH before REGEX)
    sorted_names = sorted(top_level, key=lambda s: (-len(s), s))
    alt = "|".join(escape_for_regex(n) for n in sorted_names)
    return r"(?i)\b(" + alt + r")\b"


def build_prefix_regex(prefixes):
    alt = "|".join(escape_for_regex(p) for p in sorted(prefixes))
    return r"(?i)\b(" + alt + r")\b"


def build_suffix_regex(suffixes):
    # Longer suffixes first so they match greedily
    sorted_names = sorted(suffixes, key=lambda s: (-len(s), s))
    alt = "|".join(escape_for_regex(n) for n in sorted_names)
    return r"(?i)\.(" + alt + r")\b"


def update_tmlanguage(buckets):
    if not os.path.exists(TMLANG_FILE):
        # Grammar lives in the VS Code extension, not in this repo.
        # Create a minimal syntaxes/ dir so the user can drop it into the ext.
        os.makedirs(os.path.dirname(TMLANG_FILE), exist_ok=True)
        print("NOTE: {} did not exist yet - will create a fresh copy.".format(TMLANG_FILE))
        data = make_fresh_grammar(buckets)
        with open(TMLANG_FILE, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=4, ensure_ascii=False)
        print("wrote {}".format(TMLANG_FILE))
        return

    with open(TMLANG_FILE, "r", encoding="utf-8") as f:
        data = json.load(f)

    top_re    = build_top_level_regex(buckets["top_level"])
    prefix_re = build_prefix_regex(buckets["prefixes"])
    suffix_re = build_suffix_regex(buckets["suffixes"])

    # Walk the patterns and rewrite the three we care about.
    # Match by the 'name' and, for the two support.function entries, by
    # whether the match starts with a dot.
    rewritten = {"top_level": False, "prefix": False, "dot_suffix": False}

    def rewrite_pattern(pat):
        name = pat.get("name", "")
        match = pat.get("match", "")
        if name == "entity.name.type.class.jdbasic":
            pat["match"] = prefix_re
            rewritten["prefix"] = True
        elif name == "support.function.jdbasic":
            if match.startswith("(?i)\\.") or match.startswith("(?i)\\\\."):
                pat["match"] = suffix_re
                rewritten["dot_suffix"] = True
            else:
                pat["match"] = top_re
                rewritten["top_level"] = True

    for pat in data.get("patterns", []):
        rewrite_pattern(pat)
        # Some patterns may nest; walk one level down just in case.
        for child in pat.get("patterns", []) or []:
            rewrite_pattern(child)

    # If any bucket wasn't found in the existing file, append a new pattern.
    patterns = data.setdefault("patterns", [])
    if not rewritten["top_level"]:
        patterns.append({
            "comment": "Auto-generated: non-dotted native functions.",
            "name": "support.function.jdbasic",
            "match": top_re,
        })
    if not rewritten["prefix"]:
        patterns.append({
            "comment": "Auto-generated: namespace prefixes of dotted natives.",
            "name": "entity.name.type.class.jdbasic",
            "match": prefix_re,
        })
    if not rewritten["dot_suffix"]:
        patterns.append({
            "comment": "Auto-generated: suffixes of dotted native functions.",
            "name": "support.function.jdbasic",
            "match": suffix_re,
        })

    with open(TMLANG_FILE, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=4, ensure_ascii=False)
    print("updated {}".format(TMLANG_FILE))
    print("  top-level natives : {}".format(len(buckets["top_level"])))
    print("  namespace prefixes: {}".format(len(buckets["prefixes"])))
    print("  dotted suffixes   : {}".format(len(buckets["suffixes"])))


def make_fresh_grammar(buckets):
    return {
        "$schema": "https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json",
        "name": "jdbasic",
        "scopeName": "source.jdbasic",
        "patterns": [
            {
                "name": "comment.line.jdbasic",
                "patterns": [
                    {"name": "comment.line.rem.jdbasic",   "begin": "(?i)\\b(REM)\\b", "end": "(?=$)"},
                    {"name": "comment.line.quote.jdbasic", "begin": "'",                "end": "(?=$)"},
                ],
            },
            {"name": "string.quoted.double.jdbasic", "begin": "\"", "end": "\""},
            {
                "name": "keyword.control.jdbasic",
                "match": r"(?i)\b(IF|THEN|ELSE|ELSEIF|ENDIF|FOR|TO|STEP|NEXT|EACH|IN|DO|LOOP|WHILE|UNTIL|SWITCH|CASE|DEFAULT|ENDSWITCH|SUB|ENDSUB|FUNC|ENDFUNC|RETURN|GOTO|CALL|EXITFUNC|EXITDO|EXITFOR|TRY|CATCH|FINALLY|ENDTRY|THROW|AWAIT|ASYNC|DIM|AS|LET|TYPE|ENDTYPE|ENUM|ENDENUM|THIS|REACT|IMPORT|EXPORT|MODULE|DECLARE|OPTION|STOP|RESUME)\b",
            },
            {
                "name": "storage.type.jdbasic",
                "match": r"(?i)\b(INTEGER|DOUBLE|STRING|MAP|ARRAY|TENSOR|JSON|DATE|BOOLEAN|BOOL|BYTE|CHAR|INT16|INT32|INT64|FLOAT16|FLOAT32|FLOAT64|OBJECT)\b",
            },
            {
                "name": "constant.language.jdbasic",
                "match": r"(?i)\b(TRUE|FALSE|PI|E|VBNEWLINE|NONE|NULL)\b",
            },
            {"name": "entity.name.type.class.jdbasic", "match": build_prefix_regex(buckets["prefixes"])},
            {"name": "support.function.jdbasic",        "match": build_top_level_regex(buckets["top_level"])},
            {"name": "support.function.jdbasic",        "match": build_suffix_regex(buckets["suffixes"])},
            {
                "name": "keyword.operator.jdbasic",
                "match": r"(?i)(\+|-|\*|/|\\|MOD|<>|<=|>=|<|>|=|\^|AND|OR|NOT|XOR|BAND|BOR|BXOR|ANDALSO|ORELSE|IN|->|@)\b",
            },
            {"name": "constant.numeric.jdbasic", "match": r"\b([0-9]+(\.[0-9]*)?|\.[0-9]+)\b"},
            {
                "comment": "User-defined function calls (a word followed by a parenthesis).",
                "match":   r"\b([a-zA-Z_][a-zA-Z0-9_\$]*)(?=\()",
                "name":    "entity.name.function.jdbasic",
            },
            {"name": "variable.other.string.jdbasic", "match": r"\b([a-zA-Z_][a-zA-Z0-9_]*)\$"},
            {"name": "variable.other.jdbasic",        "match": r"\b([a-zA-Z_][a-zA-Z0-9_]*)\b"},
        ],
    }


def main():
    if not os.path.isdir(SRC_DIR):
        sys.stderr.write("error: must be run from jdBasic project root (no 'src/' directory here)\n")
        sys.exit(1)

    names = collect_natives()
    print("found {} unique native function names".format(len(names)))

    buckets = split_natives(names)
    write_natives_list_h(buckets["all_parts"])
    update_tmlanguage(buckets)


if __name__ == "__main__":
    main()
