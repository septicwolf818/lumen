#!/usr/bin/env python3
"""Regenerate data/keywords.json from the upstream languages-keywords README.

Usage:
    python3 tools/gen_keyword_data.py [README_URL_OR_PATH] [OUT_JSON]

Upstream: https://github.com/radosslav/languages-keywords
  Source: MIT, compiled data: CC-BY-4.0 (see NOTICE / README).

The upstream lists carry some non-identifier tokens ("array()", "as?", "!in",
"@interface", "..."). This script strips the decoration so only things our
identifier-based lexer can match are kept, and layers on the types, constants
and builtins that our highlighter uses (which are not keywords).
"""
import json
import re
import sys
import urllib.request

DEFAULT_URL = ("https://raw.githubusercontent.com/radosslav/languages-keywords/"
               "main/README.md")

# Which upstream heading maps to our profile name; the last matching section
# wins (so C++20 / Python 3.10 / Java 17 are used).
SECTION_TO_PROFILE = {
    "C++": "cpp",
    "C": "c",
    "C#": "csharp",
    "Java": "java",
    "JavaScript": "javascript",
    "Go": "go",
    "Rust": "rust",
    "Python 3": "python",
    "Ruby": "ruby",
    "PHP": "php",
    "Kotlin": "kotlin",
    "Swift": "swift",
    "Scala": "scala",
    "Lua": "lua",
    "R": "r",
    "Dart": "dart",
    "Elixir": "elixir",
    "Erlang": "erlang",
}

# Identifier-safe cleanup: drop trailing "()" and leading/trailing decoration
# that our lexer cannot tokenize; keep the bare word.
def clean(token):
    t = token.strip()
    t = re.sub(r"\(\)$", "", t)          # array() -> array, die() -> die
    t = t.strip("@#!")                    # @interface -> interface (best effort)
    t = t.strip("?!")
    if not t:
        return None
    if t in {"...", "--", "=>", "=="}:
        return t
    if not re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", t):
        return None
    return t


# Language syntax metadata: comment markers / flags per profile.
SYNTAX = {
    "python":     {"line": "#", "block": False, "ci": False, "hash": True},
    "ruby":       {"line": "#", "block": False, "ci": False, "hash": True},
    "bash":       {"line": "#", "block": False, "ci": False, "hash": True},
    "yaml":       {"line": "#", "block": False, "ci": False, "hash": True},
    "r":          {"line": "#", "block": False, "ci": False, "hash": True},
    "elixir":     {"line": "#", "block": False, "ci": False, "hash": True},
    "perl":       {"line": "#", "block": False, "ci": False, "hash": True},
    "php":        {"line": "//", "block": True, "ci": False, "hash": False},
    "cpp":        {"line": "//", "block": True, "ci": False, "hash": False},
    "c":          {"line": "//", "block": True, "ci": False, "hash": False},
    "csharp":     {"line": "//", "block": True, "ci": False, "hash": False},
    "java":       {"line": "//", "block": True, "ci": False, "hash": False},
    "javascript": {"line": "//", "block": True, "ci": False, "hash": False},
    "typescript": {"line": "//", "block": True, "ci": False, "hash": False},
    "go":         {"line": "//", "block": True, "ci": False, "hash": False},
    "rust":       {"line": "//", "block": True, "ci": False, "hash": False},
    "swift":      {"line": "//", "block": True, "ci": False, "hash": False},
    "kotlin":     {"line": "//", "block": True, "ci": False, "hash": False},
    "scala":      {"line": "//", "block": True, "ci": False, "hash": False},
    "dart":       {"line": "//", "block": True, "ci": False, "hash": False},
    "erl":        {"line": "%", "block": False, "ci": False, "hash": False},
    "erlang":     {"line": "%", "block": False, "ci": False, "hash": False},
    "lua":        {"line": "--", "block": False, "ci": False, "hash": False},
    "sql":        {"line": "--", "block": True, "ci": True, "hash": False},
    "json":       {"line": "", "block": False, "ci": False, "hash": False},
}

# Extra per-language layers that are not keywords. Kept intentionally small and
# maintained here; the keyword lists come from upstream.
TYPES = {
    "cpp": ["void","bool","char","char8_t","char16_t","char32_t","short","int","long",
            "float","double","signed","unsigned","size_t","ssize_t","wchar_t",
            "uint8_t","uint16_t","uint32_t","uint64_t","int8_t","int16_t","int32_t",
            "int64_t","auto","string","wstring","string_view"],
    "c": ["void","char","short","int","long","float","double","signed","unsigned",
          "size_t","ssize_t","int8_t","int16_t","int32_t","int64_t","uint8_t",
          "uint16_t","uint32_t","uint64_t","bool","FILE"],
    "csharp": ["void","bool","byte","sbyte","char","short","ushort","int","uint","long",
               "ulong","float","double","decimal","string","object","var","dynamic"],
    "java": ["void","boolean","byte","char","short","int","long","float","double",
             "String","Object","var","Integer","Long","Double","Boolean"],
    "javascript": ["var","let","const","function","class","Number","String","Boolean",
                   "Array","Object","Promise","Map","Set","Symbol","BigInt"],
    "typescript": ["number","string","boolean","any","void","never","unknown","object",
                   "symbol","bigint","undefined"],
    "go": ["bool","byte","rune","string","int","int8","int16","int32","int64","uint",
           "uint8","uint16","uint32","uint64","uintptr","float32","float64",
           "complex64","complex128","error","any"],
    "rust": ["bool","char","str","i8","i16","i32","i64","i128","isize","u8","u16","u32",
             "u64","u128","usize","f32","f64","String","Vec","Option","Result","Box"],
    "swift": ["Int","Int8","Int16","Int32","Int64","UInt","UInt8","UInt16","UInt32",
              "UInt64","Float","Double","Bool","String","Character","Array","Dictionary",
              "Set","Optional","Any","AnyObject"],
    "kotlin": ["Int","Long","Short","Byte","Float","Double","Boolean","Char","String",
               "Unit","Any","Nothing","List","Map","Set","Array"],
    "scala": ["Int","Long","Short","Byte","Float","Double","Boolean","Char","String",
              "Unit","Any","AnyRef","Nothing","Null","List","Map","Set","Option"],
    "python": ["int","float","str","bool","bytes","list","dict","set","tuple","object",
               "complex","frozenset","bytearray"],
    "ruby": ["Integer","Float","String","Symbol","Array","Hash","Range","Proc","NilClass",
             "TrueClass","FalseClass"],
    "php": ["int","float","string","bool","array","object","callable","iterable","void",
            "mixed","null"],
    "r": ["numeric","integer","character","logical","complex","list","vector","data.frame",
          "matrix"],
    "dart": ["int","double","num","String","bool","List","Map","Set","Object","dynamic",
             "void","Future","Stream"],
    "elixir": ["integer","float","atom","binary","list","tuple","map"],
}

CONSTANTS = {
    "cpp": ["true","false","nullptr","NULL"],
    "c": ["true","false","NULL"],
    "csharp": ["true","false","null"],
    "java": ["true","false","null"],
    "javascript": ["true","false","null","undefined","NaN","Infinity"],
    "typescript": ["true","false","null","undefined","NaN","Infinity"],
    "go": ["true","false","nil","iota"],
    "rust": ["true","false","None","Some","Ok","Err","self","Self"],
    "swift": ["true","false","nil","self","Self"],
    "kotlin": ["true","false","null","this","super","it"],
    "scala": ["true","false","null","None","Some","Nil"],
    "python": ["None","True","False","self","cls","NotImplemented","Ellipsis"],
    "ruby": ["true","false","nil","self"],
    "php": ["true","false","null","this"],
    "r": ["TRUE","FALSE","NULL","NA","NaN","Inf","NULL"],
    "dart": ["true","false","null","this","super"],
    "elixir": ["true","false","nil"],
    "json": ["true","false","null"],
    "yaml": ["true","false","null","yes","no","on","off"],
    "sql": ["null","true","false"],
    "lua": ["true","false","nil"],
    "erlang": ["true","false"],
}

BUILTINS = {
    "python": ["print","len","range","enumerate","zip","map","filter","sum","min","max",
               "abs","open","isinstance","input","sorted","reversed","type","super",
               "getattr","setattr","hasattr","format","repr","iter","next","id","hash",
               "any","all","round","divmod","pow"],
    "ruby": ["puts","print","p","require","require_relative","attr_accessor","attr_reader",
             "attr_writer","raise","loop","gets","lambda","proc","yield","send"],
    "php": ["echo","print","printf","count","array_map","array_filter","array_push",
            "strlen","substr","str_replace","explode","implode","isset","empty","unset",
            "var_dump","json_encode","json_decode"],
    "javascript": ["console","require","parseInt","parseFloat","isNaN","setTimeout",
                   "setInterval","fetch","JSON","Math","Object","Array","Promise"],
    "typescript": ["console","require","parseInt","parseFloat","JSON","Math","Promise"],
    "go": ["make","new","len","cap","append","copy","delete","panic","recover","print",
           "println","close","complex","real","imag"],
    "rust": ["println","print","format","vec","panic","assert","assert_eq","assert_ne",
             "write","writeln","drop","clone"],
    "swift": ["print","dump","fatalError","assert","precondition","readLine","zip",
              "stride","repeatElement"],
    "kotlin": ["println","print","readLine","listOf","mapOf","setOf","mutableListOf",
               "mutableMapOf","arrayOf","lazy","require","check","TODO"],
    "scala": ["println","print","printf","require","assert","List","Map","Set","Seq",
              "Vector","Range"],
    "c": ["printf","fprintf","sprintf","snprintf","malloc","free","calloc","realloc",
          "strlen","strcpy","strcmp","memcpy","memset","fopen","fclose","fread","fwrite"],
    "cpp": ["std","printf","malloc","free","memcpy","memset","strlen","size","begin","end"],
    "sql": ["count","sum","avg","min","max","coalesce","now","lower","upper","length",
            "substring","trim","cast","greatest","least"],
    "bash": ["echo","printf","read","cd","pwd","test","exit","export","unset","set",
             "shift","eval","exec","source","trap","wait","kill","sleep","ls","cat","grep",
             "sed","awk","find","mkdir","rm","cp","mv","chmod","curl","wget"],
}

# Aliases per profile.
ALIASES = {
    "cpp": ["cpp","c++","cc","cxx","hpp","hxx"],
    "c": ["c","h"],
    "csharp": ["cs","csharp"],
    "java": ["java"],
    "javascript": ["js","javascript","node"],
    "typescript": ["ts","typescript"],
    "go": ["go","golang"],
    "rust": ["rust","rs"],
    "python": ["python","py","python3"],
    "ruby": ["ruby","rb"],
    "php": ["php"],
    "kotlin": ["kotlin","kt","kts"],
    "swift": ["swift"],
    "scala": ["scala"],
    "lua": ["lua"],
    "r": ["r"],
    "dart": ["dart"],
    "elixir": ["elixir","ex","exs"],
    "erlang": ["erlang","erl"],
    "bash": ["bash","sh","shell","zsh"],
    "sql": ["sql"],
    "json": ["json"],
    "yaml": ["yaml","yml"],
}


def parse_upstream(text):
    """Return {profile: [keywords]} from the README markdown."""
    sections = re.split(r"\n### ", text)
    out = {}
    for sec in sections[1:]:
        lines = sec.split("\n")
        name = lines[0].split(" (")[0].strip()
        profile = SECTION_TO_PROFILE.get(name)
        if not profile:
            continue
        kws = []
        for ln in lines[1:]:
            if ln.startswith("|"):
                for cell in ln.strip().strip("|").split("|"):
                    c = clean(cell)
                    if c:
                        kws.append(c)
            elif ln.startswith("####"):
                break
        seen, ordered = set(), []
        for k in kws:
            if k not in seen:
                seen.add(k)
                ordered.append(k)
        out[profile] = ordered  # last section for a profile wins
    return out


def build(upstream):
    languages = {}
    for profile, keywords in upstream.items():
        entry = {"aliases": ALIASES.get(profile, [profile]), "keywords": keywords}
        if profile in TYPES:
            entry["types"] = TYPES[profile]
        if profile in CONSTANTS:
            entry["constants"] = CONSTANTS[profile]
        if profile in BUILTINS:
            entry["builtins"] = BUILTINS[profile]
        syn = SYNTAX.get(profile)
        if syn:
            entry["syntax"] = {
                "line_comment": syn["line"],
                "block_comment": syn["block"],
                "case_insensitive": syn["ci"],
                "hash": syn["hash"],
            }
        languages[profile] = entry

    # Aliases that don't map to an upstream section (shell, json, yaml, sql).
    extra = {
        "bash": {"#{keywords}": []},
        "json": {"#{keywords}": []},
        "yaml": {"#{keywords}": []},
        "sql": {"#{keywords}": []},
    }
    for name, tmpl in extra.items():
        if name in languages:
            continue
        entry = {"aliases": ALIASES[name], "keywords": [], "syntax": {
            "line_comment": SYNTAX[name]["line"], "block_comment": SYNTAX[name]["block"],
            "case_insensitive": SYNTAX[name]["ci"], "hash": SYNTAX[name]["hash"]}}
        if name in CONSTANTS:
            entry["constants"] = CONSTANTS[name]
        if name in BUILTINS:
            entry["builtins"] = BUILTINS[name]
        languages[name] = entry

    return {
        "_comment": ("Language keyword data for syntax highlighting. Keywords "
                     "compiled from the MIT/CC-BY-4.0 languages-keywords project "
                     "(https://github.com/radosslav/languages-keywords). Types, "
                     "constants and builtins are maintained in "
                     "tools/gen_keyword_data.py."),
        "version": 2,
        "languages": languages,
    }


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_URL
    out = sys.argv[2] if len(sys.argv) > 2 else "data/keywords.json"
    if src.startswith("http"):
        with urllib.request.urlopen(src, timeout=30) as r:
            text = r.read().decode("utf-8")
    else:
        text = open(src, encoding="utf-8").read()
    upstream = parse_upstream(text)
    data = build(upstream)
    with open(out, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print(f"wrote {out}: {len(data['languages'])} profiles, "
          f"{sum(len(v.get('keywords', [])) for v in data['languages'].values())} keywords")


if __name__ == "__main__":
    main()
