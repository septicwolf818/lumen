#!/usr/bin/env python3
"""Regenerate data/keywords.json from the upstream languages-keywords README.

Usage:
    python3 tools/gen_keyword_data.py [README_URL_OR_PATH] [OUT_JSON]

Upstream: https://github.com/radosslav/languages-keywords
  Source: MIT, compiled data: CC-BY-4.0 (see NOTICE / README).

Upstream lists carry non-identifier tokens ("array()", "as?", "!in",
"@interface", "..."). This strips the decoration to identifiers our lexer can
match, and layers on the types/constants/builtins the highlighter uses.
"""
import json
import os
import re
import sys
import urllib.request

DEFAULT_URL = ("https://raw.githubusercontent.com/radosslav/languages-keywords/"
               "main/README.md")
# Committed upstream README snapshot used when the network is unavailable, so
# regeneration stays reproducible offline. Never needed at build or run time.
SNAPSHOT = os.path.join(os.path.dirname(__file__), "..", "data", "upstream-keywords.md")

# Upstream heading -> our profile name; the last matching section wins (so
# C++20 / Python 3.10 / Java 17 are used).
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
    "Fortran": "fortran",
    "Visual Basic": "vbnet",
    "MATLAB": "matlab",
    "Objective-C": "objc",
}

# Identifier-safe cleanup: strip "()" and leading/trailing decoration the
# lexer cannot tokenize; keep the bare word.
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
#   line:  single-line comment marker ("" = none)
#   bopen/bclose: multi-line comment delimiters ("" = none)
#   ci:    keywords are case-insensitive
#   hash:  '#'-based preprocessor/decorator highlighting
SYNTAX = {
    "python":     {"line": "#",  "bopen": "",    "bclose": "",    "ci": False, "hash": True},
    "ruby":       {"line": "#",  "bopen": "=begin", "bclose": "=end", "ci": False, "hash": True},
    "bash":       {"line": "#",  "bopen": "",    "bclose": "",    "ci": False, "hash": True},
    "yaml":       {"line": "#",  "bopen": "",    "bclose": "",    "ci": False, "hash": True},
    "r":          {"line": "#",  "bopen": "",    "bclose": "",    "ci": False, "hash": True},
    "elixir":     {"line": "#",  "bopen": "",    "bclose": "",    "ci": False, "hash": True},
    "perl":       {"line": "#",  "bopen": "",    "bclose": "",    "ci": False, "hash": True},
    "php":        {"line": "//", "bopen": "/*",  "bclose": "*/",  "ci": False, "hash": False},
    "cpp":        {"line": "//", "bopen": "/*",  "bclose": "*/",  "ci": False, "hash": False},
    "c":          {"line": "//", "bopen": "/*",  "bclose": "*/",  "ci": False, "hash": False},
    "csharp":     {"line": "//", "bopen": "/*",  "bclose": "*/",  "ci": False, "hash": False},
    "java":       {"line": "//", "bopen": "/*",  "bclose": "*/",  "ci": False, "hash": False},
    "javascript": {"line": "//", "bopen": "/*",  "bclose": "*/",  "ci": False, "hash": False},
    "typescript": {"line": "//", "bopen": "/*",  "bclose": "*/",  "ci": False, "hash": False},
    "go":         {"line": "//", "bopen": "/*",  "bclose": "*/",  "ci": False, "hash": False},
    "rust":       {"line": "//", "bopen": "/*",  "bclose": "*/",  "ci": False, "hash": False},
    "swift":      {"line": "//", "bopen": "/*",  "bclose": "*/",  "ci": False, "hash": False},
    "kotlin":     {"line": "//", "bopen": "/*",  "bclose": "*/",  "ci": False, "hash": False},
    "scala":      {"line": "//", "bopen": "/*",  "bclose": "*/",  "ci": False, "hash": False},
    "dart":       {"line": "//", "bopen": "/*",  "bclose": "*/",  "ci": False, "hash": False},
    "zig":        {"line": "//", "bopen": "",    "bclose": "",    "ci": False, "hash": False},
    "haskell":    {"line": "--", "bopen": "{-",  "bclose": "-}",  "ci": False, "hash": False},
    "clojure":    {"line": ";",  "bopen": "",    "bclose": "",    "ci": False, "hash": False},
    "html":       {"line": "",   "bopen": "<!--", "bclose": "-->", "ci": False, "hash": False},
    "css":        {"line": "",   "bopen": "/*",  "bclose": "*/",  "ci": False, "hash": False},
    "groovy":     {"line": "//", "bopen": "/*",  "bclose": "*/",  "ci": False, "hash": False},
    "powershell": {"line": "#",  "bopen": "<#",  "bclose": "#>",  "ci": True,  "hash": True},
    "gdscript":   {"line": "#",  "bopen": "",    "bclose": "",    "ci": False, "hash": True},
    "vbnet":      {"line": "'",  "bopen": "",    "bclose": "",    "ci": True,  "hash": False},
    "ocaml":      {"line": "",   "bopen": "(*",  "bclose": "*)",  "ci": False, "hash": False},
    "julia":      {"line": "#",  "bopen": "#=",  "bclose": "=#",  "ci": False, "hash": True},
    "fortran":    {"line": "!",  "bopen": "",    "bclose": "",    "ci": True,  "hash": False},
    "matlab":     {"line": "%",  "bopen": "%{",  "bclose": "%}",  "ci": False, "hash": False},
    "objc":       {"line": "//", "bopen": "/*",  "bclose": "*/",  "ci": False, "hash": False},
    "pascal":     {"line": "//", "bopen": "{",   "bclose": "}",   "ci": True,  "hash": False},
    "lisp":       {"line": ";",  "bopen": "#|",  "bclose": "|#",  "ci": True,  "hash": False},
    "assembly":   {"line": ";",  "bopen": "",    "bclose": "",    "ci": True,  "hash": False},
    "ada":        {"line": "--", "bopen": "",    "bclose": "",    "ci": True,  "hash": False},
    "prolog":     {"line": "%",  "bopen": "/*",  "bclose": "*/",  "ci": False, "hash": False},
    "fsharp":     {"line": "//", "bopen": "(*",  "bclose": "*)",  "ci": False, "hash": False},
    "nim":        {"line": "#",  "bopen": "#[",  "bclose": "]#",  "ci": True,  "hash": True},
    "erlang":     {"line": "%",  "bopen": "",    "bclose": "",    "ci": False, "hash": False},
    "lua":        {"line": "--", "bopen": "--[[", "bclose": "]]", "ci": False, "hash": False},
    "sql":        {"line": "--", "bopen": "/*",  "bclose": "*/",  "ci": True,  "hash": False},
    "json":       {"line": "",   "bopen": "",    "bclose": "",    "ci": False, "hash": False},
}

# Extra per-language layers that are not keywords; keyword lists come upstream.
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
    "matlab": ["double","single","int8","int16","int32","int64","uint8","uint16","uint32",
               "uint64","logical","char","string","cell","struct","table","datetime",
               "duration","categorical"],
    "objc": ["id","Class","SEL","IMP","BOOL","NSInteger","NSUInteger","CGFloat","NSString",
             "NSObject","NSArray","NSDictionary","NSSet","NSNumber","instancetype","Method",
             "Ivar","Protocol"],
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
    "objc": ["nil","Nil","YES","NO","NULL","self","super"],
    "matlab": ["true","false","inf","Inf","NaN","pi","eps"],
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
    "fortran": ["fortran","f90","f95","f03"],
    "matlab": ["matlab","octave","m"],
    "objc": ["objc","objective-c","objectivec","m"],
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


# Languages absent upstream (or needing a markup profile), curated here.
# Keywords only; comment markers come from SYNTAX.
EXTRA_LANGUAGES = {
    "zig": {
        "aliases": ["zig"],
        "keywords": ["addrspace","align","allowzero","and","anyframe","anytype","asm",
                     "async","await","break","callconv","catch","comptime","const",
                     "continue","defer","else","enum","errdefer","error","export",
                     "extern","fn","for","if","inline","linksection","noalias","noinline",
                     "nosuspend","opaque","or","orelse","packed","pub","resume","return",
                     "struct","suspend","switch","test","threadlocal","try","union",
                     "unreachable","usingnamespace","var","volatile","while"],
        "types": ["i8","u8","i16","u16","i32","u32","i64","u64","i128","u128","isize",
                  "usize","c_char","c_short","c_ushort","c_int","c_uint","c_long",
                  "c_ulong","c_longlong","c_ulonglong","c_longdouble","f16","f32","f64",
                  "f80","f128","bool","anyopaque","void","noreturn","type","anyerror",
                  "comptime_int","comptime_float"],
        "constants": ["true","false","null","undefined"],
        "builtins": ["addrSpaceCast","addWithOverflow","alignCast","alignOf","as",
                     "atomicLoad","atomicRmw","atomicStore","bitCast","bitOffsetOf",
                     "bitSizeOf","branchHint","breakpoint","mulAdd","byteSwap","bitReverse",
                     "offsetOf","call","cDefine","cImport","cInclude","clz","cmpxchgStrong",
                     "cmpxchgWeak","compileError","compileLog","constCast","ctz","cUndef",
                     "cVaArg","cVaCopy","cVaEnd","cVaStart","divExact","divFloor","divTrunc",
                     "embedFile","enumFromInt","errorFromInt","errorName","errorReturnTrace",
                     "errorCast","export","extern","field","fieldParentPtr","FieldType",
                     "floatCast","floatFromInt","frameAddress","hasDecl","hasField","import",
                     "inComptime","intCast","intFromBool","intFromEnum","intFromError",
                     "intFromFloat","intFromPtr","max","memcpy","memset","min",
                     "wasmMemorySize","wasmMemoryGrow","mod","mulWithOverflow","panic",
                     "popCount","prefetch","ptrCast","ptrFromInt","rem","returnAddress",
                     "select","setEvalBranchQuota","setFloatMode","setRuntimeSafety",
                     "shlExact","shlWithOverflow","shrExact","shuffle","sizeOf","splat",
                     "reduce","src","sqrt","sin","cos","tan","exp","exp2","log","log2",
                     "log10","abs","floor","ceil","trunc","round","subWithOverflow",
                     "tagName","This","trap","truncate","Type","typeInfo","typeName",
                     "TypeOf","unionInit","Vector","volatileCast","workGroupId",
                     "workGroupSize","workItemId"],
    },
    "haskell": {
        "aliases": ["haskell","hs"],
        "keywords": ["case","class","data","default","deriving","do","else","foreign",
                     "if","import","in","infix","infixl","infixr","instance","let","module",
                     "newtype","of","then","type","where","_"],
        "types": ["Int","Integer","Float","Double","Bool","Char","String","Maybe","Either",
                  "IO","Ordering","Rational","Word","Show","Read","Eq","Ord","Enum",
                  "Bounded","Num","Functor","Monad"],
        "constants": ["True","False","Nothing","Just","Left","Right","LT","EQ","GT"],
        "builtins": ["map","filter","foldr","foldl","head","tail","length","reverse",
                     "concat","zip","unzip","take","drop","elem","notElem","null","sum",
                     "product","maximum","minimum","id","const","fst","snd","curry","uncurry",
                     "error","show","read","lines","words","unlines","unwords","iterate",
                     "repeat","replicate","mapM","mapM_","sequence","sequence_","fmap",
                     "return","pure","otherwise","undefined"],
    },
    "perl": {
        "aliases": ["perl","pl","pm"],
        "keywords": ["ADJUST","AUTOLOAD","BEGIN","CHECK","CORE","DESTROY","END","INIT",
                     "UNITCHECK","and","bless","break","catch","class","cmp","continue",
                     "default","defer","defined","do","else","elseif","elsif","eq","eval",
                     "evalbytes","exit","field","finally","for","foreach","format","ge",
                     "given","goto","gt","if","import","isa","last","le","local","lt",
                     "method","my","ne","next","no","not","or","our","package","redo","ref",
                     "require","return","state","sub","tie","tied","try","unless","until",
                     "untie","use","wantarray","when","while","x","xor"],
        "constants": ["undef","__FILE__","__LINE__","__PACKAGE__","__SUB__","__CLASS__",
                      "__DATA__","__END__"],
        "builtins": ["abs","accept","alarm","all","any","atan2","bind","binmode","caller",
                     "chdir","chmod","chomp","chop","chown","chr","chroot","close","closedir",
                     "connect","cos","crypt","dbmclose","dbmopen","delete","die","dump",
                     "each","eof","exec","exists","exp","fc","fcntl","fileno","flock","fork",
                     "formline","getc","getlogin","getpeername","getpgrp","getppid",
                     "getpriority","getpwnam","getgrnam","gethostbyname","getnetbyname",
                     "getprotobyname","getpwuid","getgrgid","getservbyname","gethostbyaddr",
                     "getnetbyaddr","getprotobynumber","getservbyport","getpwent","getgrent",
                     "gethostent","getnetent","getprotoent","getservent","setpwent","setgrent",
                     "sethostent","setnetent","setprotoent","setservent","endpwent","endgrent",
                     "endhostent","endnetent","endprotoent","endservent","getsockname",
                     "getsockopt","glob","gmtime","grep","hex","index","int","ioctl","join",
                     "keys","kill","lc","lcfirst","length","link","listen","localtime","lock",
                     "log","lstat","map","mkdir","msgctl","msgget","msgrcv","msgsnd","oct",
                     "open","opendir","ord","pack","pipe","pop","pos","print","printf",
                     "prototype","push","quotemeta","rand","read","readdir","readline",
                     "readlink","readpipe","recv","rename","reset","reverse","rewinddir",
                     "rindex","rmdir","say","scalar","seek","seekdir","select","semctl",
                     "semget","semop","send","setpgrp","setpriority","setsockopt","shift",
                     "shmctl","shmget","shmread","shmwrite","shutdown","sin","sleep","socket",
                     "socketpair","sort","splice","split","sprintf","sqrt","srand","stat",
                     "study","substr","symlink","syscall","sysopen","sysread","sysseek",
                     "system","syswrite","tell","telldir","time","times","truncate","uc",
                     "ucfirst","umask","unlink","unpack","unshift","utime","values","vec",
                     "wait","waitpid","warn","write"],
    },
    "clojure": {
        "aliases": ["clojure","clj","cljs"],
        "keywords": ["def","if","do","let","quote","var","fn","loop","recur","throw","try",
                     "monitor-enter","monitor-exit","new","set!","defn","defmacro","defmulti",
                     "defmethod","defprotocol","defrecord","deftype","defonce","declare",
                     "if-let","if-not","when","when-let","when-not","when-first","cond",
                     "condp","case","letfn","for","doseq","dotimes","while","catch","finally",
                     "ns","in-ns","require","import","use","refer","refer-clojure","and","or",
                     "not","binding","reify","proxy","doto","lazy-seq","delay","future",
                     "locking","with-open","with-local-vars","assert","comment","gen-class",
                     "memfn","sync","time"],
        "constants": ["true","false","nil"],
        "builtins": ["map","mapv","mapcat","filter","filterv","remove","reduce","reductions",
                     "apply","conj","concat","assoc","assoc-in","update","update-in","dissoc",
                     "get","get-in","keys","vals","count","first","second","ffirst","nfirst",
                     "rest","next","last","butlast","cons","seq","range","repeat","repeatedly",
                     "iterate","take","take-while","drop","drop-while","partition","interleave",
                     "interpose","flatten","distinct","sort","sort-by","reverse","shuffle",
                     "println","print","prn","pr-str","str","subs","format","inc","dec","max",
                     "min","merge","merge-with","zipmap","group-by","frequencies","identity",
                     "constantly","comp","partial","juxt","complement","every?","some","not-any?",
                     "not-every?","contains?","find","select-keys","into","vec","vector","set",
                     "hash-map","hash-set","list","lazy-cat","nth","nthnext","rand","rand-int",
                     "rand-nth","replace","split-at","split-with","trampoline","vary-meta",
                     "with-meta","meta","name","namespace","keyword","symbol","int","long",
                     "double","float","char","boolean","number?","string?","keyword?","symbol?",
                     "map?","vector?","set?","list?","seq?","fn?","nil?","true?","false?",
                     "zero?","pos?","neg?","even?","odd?","abs","quot","rem","mod","compare",
                     "type","class","instance?","satisfies?"],
    },
    "html": {
        "aliases": ["html","htm","xml"],
        "keywords": ["html","base","head","link","meta","style","title","body","address",
                     "article","aside","footer","header","h1","h2","h3","h4","h5","h6",
                     "hgroup","main","nav","section","search","blockquote","dd","div","dl",
                     "dt","figcaption","figure","hr","li","menu","ol","p","pre","ul","a",
                     "abbr","b","bdi","bdo","br","cite","code","data","dfn","em","i","kbd",
                     "mark","q","rp","rt","ruby","s","samp","small","span","strong","sub",
                     "sup","time","u","var","wbr","area","audio","img","map","track","video",
                     "embed","iframe","object","picture","source","svg","math","canvas",
                     "noscript","script","del","ins","caption","col","colgroup","table",
                     "tbody","td","tfoot","th","thead","tr","button","datalist","fieldset",
                     "form","input","label","legend","meter","optgroup","option","output",
                     "progress","select","selectedcontent","textarea","details","dialog",
                     "geolocation","summary","slot","template"],
        "constants": [],
        "builtins": [],
    },
    "css": {
        "aliases": ["css","scss","sass","less"],
        "keywords": ["accent-color","align-content","align-items","align-self","all",
                     "animation","animation-delay","animation-direction","animation-duration",
                     "animation-fill-mode","animation-iteration-count","animation-name",
                     "animation-play-state","animation-timing-function","appearance",
                     "aspect-ratio","backdrop-filter","backface-visibility","background",
                     "background-attachment","background-blend-mode","background-clip",
                     "background-color","background-image","background-origin",
                     "background-position","background-repeat","background-size","border",
                     "border-bottom","border-color","border-image","border-left",
                     "border-radius","border-right","border-spacing","border-style",
                     "border-top","border-width","bottom","box-shadow","box-sizing",
                     "break-after","break-before","break-inside","caption-side","caret-color",
                     "clear","clip-path","color","color-scheme","column-count","column-gap",
                     "column-rule","column-span","column-width","columns","contain","content",
                     "content-visibility","counter-increment","counter-reset","cursor",
                     "direction","display","empty-cells","filter","flex","flex-basis",
                     "flex-direction","flex-flow","flex-grow","flex-shrink","flex-wrap",
                     "float","font","font-family","font-feature-settings","font-kerning",
                     "font-size","font-size-adjust","font-stretch","font-style","font-variant",
                     "font-variation-settings","font-weight","gap","grid","grid-area",
                     "grid-auto-columns","grid-auto-flow","grid-auto-rows","grid-column",
                     "grid-column-end","grid-column-start","grid-row","grid-row-end",
                     "grid-row-start","grid-template","grid-template-areas",
                     "grid-template-columns","grid-template-rows","height","hyphens",
                     "image-rendering","inline-size","inset","isolation","justify-content",
                     "justify-items","justify-self","left","letter-spacing","line-height",
                     "list-style","list-style-image","list-style-position","list-style-type",
                     "margin","margin-bottom","margin-left","margin-right","margin-top","mask",
                     "max-height","max-width","min-height","min-width","mix-blend-mode",
                     "object-fit","object-position","opacity","order","orphans","outline",
                     "outline-color","outline-offset","outline-style","outline-width",
                     "overflow","overflow-wrap","overflow-x","overflow-y","overscroll-behavior",
                     "padding","padding-bottom","padding-left","padding-right","padding-top",
                     "page-break-after","page-break-before","page-break-inside","perspective",
                     "perspective-origin","place-content","place-items","place-self",
                     "pointer-events","position","quotes","resize","right","rotate","row-gap",
                     "scale","scroll-behavior","scroll-margin","scroll-padding",
                     "scroll-snap-align","scroll-snap-type","tab-size","table-layout",
                     "text-align","text-decoration","text-decoration-color",
                     "text-decoration-line","text-decoration-style","text-indent",
                     "text-overflow","text-shadow","text-transform","text-wrap","top",
                     "touch-action","transform","transform-origin","transform-style",
                     "transition","transition-delay","transition-duration","transition-property",
                     "transition-timing-function","translate","unicode-bidi","user-select",
                     "vertical-align","visibility","white-space","width","will-change",
                     "word-break","word-spacing","writing-mode","z-index","zoom",
                     # Border longhands / logical + logical box properties.
                     "border-collapse","border-bottom-color","border-bottom-style",
                     "border-bottom-width","border-top-color","border-top-style",
                     "border-top-width","border-left-color","border-left-style",
                     "border-left-width","border-right-color","border-right-style",
                     "border-right-width","border-block","border-block-color",
                     "border-block-style","border-block-width","border-block-start",
                     "border-block-end","border-inline","border-inline-color",
                     "border-inline-style","border-inline-width","border-inline-start",
                     "border-inline-end","border-start-start-radius","border-start-end-radius",
                     "border-end-start-radius","border-end-end-radius",
                     "border-top-left-radius","border-top-right-radius",
                     "border-bottom-left-radius","border-bottom-right-radius",
                     "border-image-outset","border-image-repeat","border-image-slice",
                     "border-image-source","border-image-width","border-shape",
                     "margin-block","margin-block-start","margin-block-end",
                     "margin-inline","margin-inline-start","margin-inline-end",
                     "padding-block","padding-block-start","padding-block-end",
                     "padding-inline","padding-inline-start","padding-inline-end",
                     "inset-block","inset-block-start","inset-block-end",
                     "inset-inline","inset-inline-start","inset-inline-end",
                     "block-size","min-block-size","max-block-size",
                     "min-inline-size","max-inline-size",
                     # Animation / anchoring / view transitions.
                     "animation-composition","animation-timeline","animation-range",
                     "animation-range-start","animation-range-end",
                     "anchor-name","anchor-scope","position-anchor","position-area",
                     "position-visibility","position-try-fallbacks","position-try-order",
                     "view-timeline","view-timeline-name","view-timeline-axis",
                     "view-transition-name","timeline-scope",
                     # Text / fonts.
                     "text-underline-offset","text-underline-position","text-emphasis",
                     "text-emphasis-style","text-emphasis-color","text-emphasis-position",
                     "text-orientation","text-size-adjust","text-rendering","text-justify",
                     "text-align-last","text-combine-upright","text-spacing-trim",
                     "white-space-collapse","word-wrap",
                     "font-synthesis","font-optical-sizing","font-variant-caps",
                     "font-variant-ligatures","font-variant-numeric",
                     "font-variant-east-asian","font-language-override","font-smooth",
                     "font-display","line-clamp","line-break","quotes","hyphenate-character",
                     "hyphenate-limit-chars",
                     # Overflow / scrolling.
                     "overflow-anchor","overflow-clip-margin","overflow-block",
                     "overflow-inline","overscroll-behavior-x","overscroll-behavior-y",
                     "overscroll-behavior-block","overscroll-behavior-inline",
                     "scroll-margin-top","scroll-margin-right","scroll-margin-bottom",
                     "scroll-margin-left","scroll-margin-block","scroll-margin-inline",
                     "scroll-padding-top","scroll-padding-right","scroll-padding-bottom",
                     "scroll-padding-left","scroll-padding-block","scroll-padding-inline",
                     "scrollbar-color","scrollbar-width","scrollbar-gutter",
                     "scroll-snap-stop","scroll-timeline","scroll-timeline-name",
                     "scroll-timeline-axis",
                     # Shapes / motion path / masking.
                     "shape-outside","shape-margin","shape-image-threshold",
                     "offset-path","offset-distance","offset-rotate","offset-anchor",
                     "offset-position","offset",
                     "mask-image","mask-mode","mask-repeat","mask-position","mask-clip",
                     "mask-origin","mask-size","mask-composite","mask-type","mask-border",
                     "mask-border-source","mask-border-mode","mask-border-slice",
                     "mask-border-width","mask-border-outset","mask-border-repeat",
                     # Misc modern properties.
                     "box-decoration-break","forced-color-adjust","print-color-adjust",
                     "color-interpolation","color-interpolation-filters","image-orientation",
                     "ime-mode","clip","overlay","ruby-align","ruby-position","speak",
                     "field-sizing","interpolate-size","reading-flow","reading-order",
                     "align-tracks","justify-tracks","contain-intrinsic-size",
                     "contain-intrinsic-width","contain-intrinsic-height",
                     "contain-intrinsic-block-size","contain-intrinsic-inline-size",
                     "container-type","container-name","counter-set","widows",
                     "grid-column-gap","grid-row-gap","background-position-x",
                     "background-position-y","background-repeat-x","background-repeat-y",
                     "background-position-block","background-position-inline",
                     "baseline-shift","baseline-source","break-after","break-before",
                     "break-inside","box-align","box-direction","box-flex","box-orient",
                     "box-pack","columns","column-fill",
                     "charset","import","namespace","layer","media","supports","container",
                     "keyframes","font-face","font-feature-values","font-palette-values",
                     "color-profile","counter-style","page","property","scope",
                     "starting-style","view-transition","position-try","function"],
        # Common CSS-wide keywords and frequently used values.
        "constants": ["inherit","initial","unset","revert","revert-layer","auto","none",
                      "normal","hidden","visible","collapse","block","inline","inline-block",
                      "flex","grid","table","contents","flow-root","list-item",
                      "inline-flex","inline-grid",
                      "static","relative","absolute","fixed","sticky",
                      "row","column","row-reverse","column-reverse","wrap","nowrap","wrap-reverse",
                      "center","justify","space-between","space-around","space-evenly",
                      "stretch","baseline",
                      "solid","dashed","dotted","double","groove","ridge","outset",
                      "thin","thick","transparent","currentColor","currentcolor",
                      "border-box","content-box","padding-box",
                      "underline","overline","line-through",
                      "uppercase","lowercase","capitalize","full-width",
                      "italic","oblique","bold","bolder","lighter",
                      "from","to","running","paused","infinite","alternate","forwards",
                      "backwards","both","ease","ease-in","ease-out","ease-in-out",
                      "step-start","step-end","no-repeat","repeat-x","repeat-y",
                      "round","space","cover","fill","portrait","landscape","scroll","clip",
                      "ellipsis","break-word","anywhere","pre","pre-wrap","pre-line",
                      "break-spaces","keep-all",
                      "pointer","default","move","grab","grabbing","not-allowed",
                      "wait","help","crosshair","zoom-in","zoom-out","alias","copy",
                      "ltr","rtl","vertical-rl","vertical-lr","horizontal-tb","sideways",
                      "sans-serif","serif","monospace","cursive","fantasy","system-ui",
                      "small-caps","small-caption","caption","icon","menu","message-box",
                      "status-bar","sub","super","middle","text-top","text-bottom",
                      "nonzero","evenodd","isolate","multiply","screen","darken",
                      "lighten","color-dodge","color-burn","hard-light","soft-light",
                      "difference","exclusion","saturation","luminosity"],
        "builtins": ["abs","acos","alpha","anchor","anchor-size","asin","atan","atan2",
                     "attr","blur","brightness","calc","calc-size","circle","clamp","color",
                     "color-mix","conic-gradient","contrast","contrast-color","cos","counter",
                     "counters","cross-fade","cubic-bezier","device-cmyk","drop-shadow",
                     "element","ellipse","env","exp","fit-content","grayscale","hsl","hsla",
                     "hue-rotate","hwb","hypot","image","image-set","inset","invert","lab",
                     "lch","light-dark","linear","linear-gradient","log","matrix","matrix3d",
                     "max","min","minmax","mod","oklab","oklch","opacity","paint","path",
                     "perspective","polygon","pow","progress","radial-gradient","random",
                     "ray","rect","rem","repeat","repeating-conic-gradient",
                     "repeating-linear-gradient","repeating-radial-gradient","rgb","rgba",
                     "rotate","rotate3d","rotatex","rotatey","rotatez","round","saturate",
                     "scale","scale3d","scalex","scaley","scalez","scroll","sepia","shape",
                     "sibling-count","sibling-index","sign","sin","skew","skewx","skewy",
                     "sqrt","steps","symbols","tan","translate","translate3d","translatex",
                     "translatey","translatez","url","var","view","xywh"],
    },
    "groovy": {
        "aliases": ["groovy","gvy"],
        "keywords": ["abstract","assert","break","case","catch","class","const","continue",
                     "default","do","else","enum","extends","final","finally","for","goto",
                     "if","implements","import","instanceof","interface","native","new",
                     "non-sealed","package","public","protected","private","return","static",
                     "strictfp","switch","synchronized","threadsafe","throw","throws",
                     "transient","try","while"],
        "types": ["int","long","double","float","boolean","char","byte","short","String",
                  "Object","List","Map","Set","def","BigDecimal","BigInteger"],
        "constants": ["true","false","null","this","super","it"],
    },
    "powershell": {
        "aliases": ["powershell","ps1","pwsh"],
        "keywords": ["begin","break","catch","class","clean","continue","data","define",
                     "do","dynamicparam","else","elseif","end","enum","exit","filter",
                     "finally","for","foreach","from","function","hidden","if","in","param",
                     "process","return","static","switch","throw","trap","try","until",
                     "using","var","while","workflow","inlinescript","parallel","sequence"],
        "constants": ["true","false","null"],
        "builtins": ["Write-Host","Write-Output","Write-Warning","Write-Error",
                     "Write-Verbose","Write-Debug","Get-Item","Set-Item","Remove-Item",
                     "New-Item","Copy-Item","Move-Item","Rename-Item","Get-ChildItem",
                     "Test-Path","Join-Path","Split-Path","Resolve-Path","Get-Content",
                     "Set-Content","Add-Content","Out-File","Out-Null","Get-Command",
                     "Get-Help","Get-Member","ForEach-Object","Where-Object","Select-Object",
                     "Sort-Object","Group-Object","Measure-Object","Select-String",
                     "Invoke-Command","Invoke-Expression","Start-Process","Read-Host"],
    },
    "gdscript": {
        "aliases": ["gdscript","gd"],
        "keywords": ["if","elif","else","for","while","match","when","break","continue",
                     "pass","return","class","class_name","extends","is","in","as","self",
                     "super","signal","func","static","const","enum","var","breakpoint",
                     "preload","await","yield","assert","void","and","or","not","true",
                     "false","null","PI","TAU","INF","NAN"],
        "types": ["bool","int","float","String","StringName","NodePath","Vector2",
                  "Vector2i","Rect2","Rect2i","Vector3","Vector3i","Vector4","Vector4i",
                  "Transform2D","Plane","Quaternion","AABB","Basis","Transform3D",
                  "Projection","Color","RID","Object","Array","Dictionary","Signal",
                  "Callable","PackedByteArray","PackedInt32Array","PackedInt64Array",
                  "PackedFloat32Array","PackedFloat64Array","PackedStringArray",
                  "PackedVector2Array","PackedVector3Array","PackedVector4Array",
                  "PackedColorArray"],
        "constants": ["true","false","null","PI","TAU","INF","NAN"],
    },
    "vbnet": {
        "aliases": ["vbnet","vb",".net","visualbasic"],
        "keywords": ["AddHandler","AddressOf","Alias","And","AndAlso","As","Boolean","ByRef",
                     "Byte","ByVal","Call","Case","Catch","CBool","CByte","CChar","CDate",
                     "CDbl","CDec","Char","CInt","Class","CLng","CObj","Const","Continue",
                     "CSByte","CShort","CSng","CStr","CType","CUInt","CULng","CUShort","Date",
                     "Decimal","Declare","Default","Delegate","Dim","DirectCast","Do","Double",
                     "Each","Else","ElseIf","End","EndIf","Enum","Erase","Error","Event","Exit",
                     "False","Finally","For","Friend","Function","Get","GetType",
                     "GetXMLNamespace","Global","GoSub","GoTo","Handles","If","Implements",
                     "Imports","In","Inherits","Integer","Interface","Is","IsNot","Let","Lib",
                     "Like","Long","Loop","Me","Mod","Module","MustInherit","MustOverride",
                     "MyBase","MyClass","NameOf","Namespace","Narrowing","New","Next","Not",
                     "Nothing","NotInheritable","NotOverridable","Object","Of","On","Operator",
                     "Option","Optional","Or","OrElse","Out","Overloads","Overridable",
                     "Overrides","ParamArray","Partial","Private","Property","Protected",
                     "Public","RaiseEvent","ReadOnly","ReDim","REM","RemoveHandler","Resume",
                     "Return","SByte","Select","Set","Shadows","Shared","Short","Single",
                     "Static","Step","Stop","String","Structure","Sub","SyncLock","Then",
                     "Throw","To","True","Try","TryCast","TypeOf","UInteger","ULong","UShort",
                     "Using","Variant","Wend","When","While","Widening","With","WithEvents",
                     "WriteOnly","Xor"],
        "types": ["Boolean","Byte","Char","Date","Decimal","Double","Integer","Long",
                  "Object","SByte","Short","Single","String","UInteger","ULong","UShort"],
        "constants": ["True","False","Nothing"],
    },
    "ocaml": {
        "aliases": ["ocaml","ml","mli"],
        "keywords": ["and","as","assert","asr","begin","class","constraint","do","done",
                     "downto","else","end","exception","external","false","for","fun",
                     "function","functor","if","in","include","inherit","initializer","land",
                     "lazy","let","lor","lsl","lsr","lxor","match","method","mod","module",
                     "mutable","new","nonrec","object","of","open","or","private","rec","sig",
                     "struct","then","to","true","try","type","val","virtual","when","while",
                     "with"],
        "types": ["int","float","bool","char","string","bytes","unit","list","array",
                  "option","result","ref"],
        "constants": ["true","false","None","Some","Ok","Error"],
    },
    "julia": {
        "aliases": ["julia","jl"],
        "keywords": ["baremodule","begin","break","catch","const","continue","do","else",
                     "elseif","end","export","false","finally","for","function","global","if",
                     "import","let","local","macro","module","quote","return","struct","true",
                     "try","using","while","abstract","mutable","primitive","type","where",
                     "in","isa"],
        "types": ["Int","Int8","Int16","Int32","Int64","UInt","Float16","Float32","Float64",
                  "Bool","Char","String","Symbol","Nothing","Missing","Any","Complex",
                  "Rational","Vector","Matrix","Tuple","NamedTuple","Dict","Set"],
        "constants": ["true","false","nothing","missing","Inf","NaN"],
    },
    "pascal": {
        "aliases": ["pascal","delphi","pas","fpc"],
        "keywords": ["absolute","abstract","alias","and","array","as","asm","assembler",
                     "at","automated","begin","case","cdecl","class","const","constructor",
                     "contains","default","deprecated","destructor","dispid","dispinterface",
                     "div","do","downto","dynamic","else","end","except","export","exports",
                     "external","far","file","finalization","finally","for","forward",
                     "function","goto","if","implementation","implements","in","index",
                     "inherited","initialization","inline","interface","is","label","library",
                     "message","mod","near","nil","nodefault","not","object","of","on","or",
                     "out","overload","override","packed","pascal","platform","private",
                     "procedure","program","property","protected","public","published","raise",
                     "read","readonly","record","register","reintroduce","repeat",
                     "resourcestring","safecall","sealed","set","shl","shr","static","stdcall",
                     "stored","strict","string","then","threadvar","to","try","type","unit",
                     "until","uses","var","varargs","virtual","while","with","write",
                     "writeonly","xor"],
        "types": ["Integer","Cardinal","Boolean","Byte","Word","LongInt","Int64","Single",
                  "Double","Extended","Currency","Char","WideChar","AnsiChar","ShortInt",
                  "SmallInt","UInt64","NativeInt","NativeUInt","Real","Real48","Comp",
                  "ShortString","AnsiString","WideString","UnicodeString","String","Pointer",
                  "PChar","PWideChar","TObject","Variant","OleVariant","TDateTime"],
        "constants": ["True","False","nil"],
    },
    "lisp": {
        "aliases": ["lisp","commonlisp","cl","elisp","scheme","racket"],
        "keywords": ["block","catch","eval-when","flet","function","go","if","labels","let",
                     "let*","load-time-value","locally","macrolet","multiple-value-call",
                     "multiple-value-prog1","progn","progv","quote","return-from","setq",
                     "symbol-macrolet","tagbody","the","throw","unwind-protect","defun",
                     "defmacro","defvar","defparameter","defconstant","defclass","defmethod",
                     "defgeneric","defstruct","deftype","define-condition","declaim","declare",
                     "defpackage","in-package","lambda","cond","when","unless","case","ecase",
                     "ccase","typecase","etypecase","ctypecase","and","or","loop","do","do*",
                     "dolist","dotimes","return","setf","psetf","push","pop","pushnew","incf",
                     "decf","prog","prog1","prog2","handler-case","handler-bind","ignore-errors",
                     "restart-case","multiple-value-bind","destructuring-bind","check-type",
                     "with-open-file","with-slots","with-accessors"],
        "types": ["t","nil","integer","fixnum","bignum","float","double-float","single-float",
                  "rational","complex","character","string","symbol","cons","list","vector",
                  "array","hash-table","function","stream","pathname","package"],
        "constants": ["t","nil","pi"],
        "builtins": ["car","cdr","caar","cadr","cddr","cons","list","append","length","reverse",
                     "mapcar","mapc","reduce","remove","remove-if","find","position","count",
                     "sort","format","print","prin1","princ","terpri","read","apply","funcall",
                     "values","first","second","third","rest","last","member","assoc","elt",
                     "aref","gethash","make-hash-table","error","assert","coerce","typecase",
                     "zerop","evenp","oddp","abs","max","min","expt","sqrt","exp","log","sin",
                     "cos","tan","char","char-code","code-char","string-upcase",
                     "string-downcase","concatenate","subseq","search","substitute","copy-seq",
                     "copy-list","every","some","intern","find-symbol","gensym","identity",
                     "complement","eval","load","compile","open","close","warn","make-list",
                     "make-array","vector","list-length","adjoin","union","intersection"],
    },
    "assembly": {
        "aliases": ["asm","assembly","nasm","gas","x86","x86-64"],
        "keywords": ["mov","movabs","lea","push","pop","add","sub","adc","sbb","mul","imul",
                     "div","idiv","inc","dec","neg","cmp","test","and","or","xor","not","shl",
                     "shr","sal","sar","rol","ror","jmp","je","jne","jz","jnz","jg","jge","jl",
                     "jle","ja","jae","jb","jbe","call","ret","leave","enter","nop","hlt",
                     "int","int3","syscall","sysenter","cpuid","rdtsc","xchg","movzx","movsx",
                     "movsxd","cmove","cmovne","setz","setnz","cld","std","cli","sti","cdq",
                     "cqo","rep","repe","repne","movs","stos","lods","scas","cmps","bt","bts",
                     "btr","btc","bsf","bsr","bswap","popcnt","xadd","cmpxchg",
                     "section","segment","global","extern","export","bits","org","align","db",
                     "dw","dd","dq","resb","resw","resd","resq","equ","times","include","macro",
                     "endmacro","struc","endstruc","label"],
        "types": ["rax","rbx","rcx","rdx","rsi","rdi","rbp","rsp","r8","r9","r10","r11",
                  "r12","r13","r14","r15","eax","ebx","ecx","edx","esi","edi","ebp","esp",
                  "ax","bx","cx","dx","al","bl","cl","dl","ah","bh","ch","dh","rip","rflags",
                  "eflags","cs","ds","es","fs","gs","ss","xmm0","xmm1","xmm2","ymm0","ymm1",
                  "zmm0","mm0","cr0","cr3","dr0"],
        "constants": [],
    },
    "ada": {
        "aliases": ["ada","adb","ads"],
        "keywords": ["abort","abs","abstract","accept","access","aliased","all","and","array",
                     "at","begin","body","case","constant","declare","delay","delta","digits",
                     "do","else","elsif","end","entry","exception","exit","for","function",
                     "generic","goto","if","in","interface","is","limited","loop","mod","new",
                     "not","null","of","or","others","out","overriding","package","pragma",
                     "private","procedure","protected","raise","range","record","rem","renames",
                     "requeue","return","reverse","select","separate","some","subtype",
                     "synchronized","tagged","task","terminate","then","type","until","use",
                     "when","while","with","xor"],
        "types": ["Integer","Natural","Positive","Float","Long_Float","Boolean","Character",
                  "String","Duration","Ada"],
        "constants": ["True","False"],
    },
    "prolog": {
        "aliases": ["prolog","plg"],
        "keywords": ["is","mod","div","rem","not","fail","true","false","repeat","call",
                     "catch","throw","once","forall","findall","bagof","setof","assert",
                     "asserta","assertz","retract","retractall","clause","abolish","dynamic",
                     "discontiguous","multifile","module","use_module","ensure_loaded",
                     "include","initialization","op","meta_predicate","public","between","succ",
                     "current_predicate","predicate_property","set_prolog_flag",
                     "module_transparent","thread_local","volatile"],
        "types": ["integer","float","rational","number","atom","string","atomic","variable",
                  "var","compound","callable","list","boolean","character","stream","term"],
        "constants": ["true","false","fail","nl","end_of_file"],
    },
    "fsharp": {
        "aliases": ["fsharp","fs","fsx","f#"],
        "keywords": ["abstract","and","as","assert","base","begin","class","default","delegate",
                     "do","done","downcast","downto","elif","else","end","exception","extern",
                     "false","finally","fixed","for","fun","function","global","if","in",
                     "inherit","inline","interface","internal","lazy","let","match","member",
                     "module","mutable","namespace","new","not","null","of","open","or",
                     "override","private","public","rec","return","select","static","struct",
                     "then","to","true","try","type","upcast","use","val","void","when","while",
                     "with","yield"],
        "types": ["int","int8","int16","int32","int64","uint8","uint16","uint32","uint64",
                  "float","float32","double","decimal","bool","char","string","byte","sbyte",
                  "unit","obj","list","array","seq","option","Result","Map","Set"],
        "constants": ["true","false","None","Some","Ok","Error"],
    },
    "nim": {
        "aliases": ["nim","nimble","nims"],
        "keywords": ["addr","and","as","asm","bind","block","break","case","cast","concept",
                     "const","continue","converter","defer","discard","distinct","div","do",
                     "elif","else","end","enum","except","export","finally","for","from","func",
                     "generic","if","import","in","include","interface","is","isnot","iterator",
                     "let","macro","method","mixin","mod","nil","not","notin","object","of",
                     "or","out","proc","ptr","raise","ref","return","shl","shr","static",
                     "template","try","tuple","type","using","var","when","while","with","xor",
                     "yield"],
        "types": ["int","int8","int16","int32","int64","uint","uint8","uint16","uint32",
                  "uint64","float","float32","float64","bool","char","string","cstring","seq",
                  "array","set","tuple","object","ref","ptr","pointer","openArray","varargs"],
        "constants": ["true","false","nil"],
    },
}


def syntax_entry(name):
    s = SYNTAX.get(name, {"line": "", "bopen": "", "bclose": "", "ci": False, "hash": False})
    return {
        "line_comment": s["line"],
        "block_open": s["bopen"],
        "block_close": s["bclose"],
        "case_insensitive": s["ci"],
        "hash": s["hash"],
    }


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
            entry["syntax"] = syntax_entry(profile)
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
        entry = {"aliases": ALIASES[name], "keywords": [], "syntax": syntax_entry(name)}
        if name in CONSTANTS:
            entry["constants"] = CONSTANTS[name]
        if name in BUILTINS:
            entry["builtins"] = BUILTINS[name]
        languages[name] = entry

    # Curated languages not available upstream.
    for name, data in EXTRA_LANGUAGES.items():
        entry = {
            "aliases": data.get("aliases", [name]),
            "keywords": data.get("keywords", []),
            "syntax": syntax_entry(name),
        }
        for key in ("types", "constants", "builtins"):
            if data.get(key):
                entry[key] = data[key]
        languages[name] = entry

    # JSX/TSX/Svelte/Vue are JS/TS-family for highlighting purposes.
    for alias in ("jsx", "tsx", "svelte", "vue", "typescript", "ts"):
        if "javascript" in languages:
            languages["javascript"]["aliases"].append(alias)

    return {
        "_comment": ("Language keyword data for syntax highlighting. Keywords "
                     "compiled from the MIT/CC-BY-4.0 languages-keywords project "
                     "(https://github.com/radosslav/languages-keywords). Types, "
                     "constants and builtins are maintained in "
                     "tools/gen_keyword_data.py."),
        "version": 2,
        "languages": languages,
    }


def load_upstream(src):
    """Return the upstream README text.

    Order: an explicit path/URL, then the network, then the committed snapshot.
    A failure to fetch never empties the data - we fall back to the snapshot
    and, if even that is missing, abort without touching the output file.
    """
    if src and not src.startswith("http"):
        return open(src, encoding="utf-8").read()

    url = src or DEFAULT_URL
    try:
        with urllib.request.urlopen(url, timeout=30) as r:
            return r.read().decode("utf-8")
    except Exception as e:  # network down, rate-limited, repo gone, ...
        print(f"note: could not fetch {url} ({e}); using snapshot", file=sys.stderr)

    if os.path.exists(SNAPSHOT):
        return open(SNAPSHOT, encoding="utf-8").read()
    raise SystemExit("error: no upstream source and no snapshot available")


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else None
    out = sys.argv[2] if len(sys.argv) > 2 else "data/keywords.json"
    text = load_upstream(src)
    upstream = parse_upstream(text)
    data = build(upstream)

    total = sum(len(v.get("keywords", [])) for v in data["languages"].values())
    if not data["languages"] or total == 0:
        raise SystemExit("error: parsed no languages; refusing to overwrite " + out)
    with open(out, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print(f"wrote {out}: {len(data['languages'])} profiles, {total} keywords")


if __name__ == "__main__":
    main()
