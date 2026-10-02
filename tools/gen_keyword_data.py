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
    "zig":        {"line": "//", "block": False, "ci": False, "hash": False},
    "haskell":    {"line": "--", "block": True, "ci": False, "hash": False},
    "perl":       {"line": "#", "block": False, "ci": False, "hash": True},
    "clojure":    {"line": ";", "block": False, "ci": False, "hash": False},
    "html":       {"line": "", "block": True, "ci": False, "hash": False},
    "css":        {"line": "", "block": True, "ci": False, "hash": False},
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


# Languages not present upstream (or needing a markup profile), curated here.
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
                     "charset","import","namespace","layer","media","supports","container",
                     "keyframes","font-face","font-feature-values","font-palette-values",
                     "color-profile","counter-style","page","property","scope",
                     "starting-style","view-transition","position-try","function"],
        "constants": [],
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

    # Curated languages not available upstream.
    for name, data in EXTRA_LANGUAGES.items():
        syn = SYNTAX.get(name, {"line": "", "block": False, "ci": False, "hash": False})
        entry = {
            "aliases": data.get("aliases", [name]),
            "keywords": data.get("keywords", []),
            "syntax": {
                "line_comment": syn["line"],
                "block_comment": syn["block"],
                "case_insensitive": syn["ci"],
                "hash": syn["hash"],
            },
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
