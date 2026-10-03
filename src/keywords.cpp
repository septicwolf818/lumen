#include "keywords_internal.hpp"

#include "json.hpp"

#include <cctype>
#include <map>
#include <string>

#include "keywords_data.inc"  // generated: kKeywordsJsonChunks[]

namespace md {
namespace detail {
namespace {

// The embedded JSON is chunked to stay within the compiler's portable string
// literal length; join the chunks once for parsing.
std::string keywords_json() {
    std::string s;
    for (const char* const* p = kKeywordsJsonChunks; p && *p; ++p) s += *p;
    return s;
}

std::vector<std::string> json_strings(const nlohmann::json& j) {
    std::vector<std::string> out;
    if (j.is_array())
        for (const auto& e : j)
            if (e.is_string()) out.push_back(e.get<std::string>());
    return out;
}

// True if any list holds a hyphenated entry (e.g. CSS "background-color"),
// meaning '-' must be treated as an identifier character for this profile.
bool has_hyphenated(const LangProfile& p) {
    auto any = [](const std::vector<std::string>& v) {
        for (const auto& s : v)
            if (s.find('-') != std::string::npos) return true;
        return false;
    };
    return any(p.keywords) || any(p.types) || any(p.constants) || any(p.builtins);
}

// Comment markers and lexer flags come from the data's optional "syntax"
// object, so adding a language needs no code change.
void apply_syntax(const nlohmann::json& v, LangProfile& p) {
    if (!v.contains("syntax") || !v["syntax"].is_object()) return;
    const auto& s = v["syntax"];
    if (s.contains("line_comment") && s["line_comment"].is_string())
        p.line_comment = s["line_comment"].get<std::string>();
    if (s.contains("block_open") && s["block_open"].is_string())
        p.block_comment_open = s["block_open"].get<std::string>();
    if (s.contains("block_close") && s["block_close"].is_string())
        p.block_comment_close = s["block_close"].get<std::string>();
    if (s.contains("case_insensitive") && s["case_insensitive"].is_boolean())
        p.case_insensitive = s["case_insensitive"].get<bool>();
    if (s.contains("hash") && s["hash"].is_boolean())
        p.python_like = s["hash"].get<bool>();
}

const std::map<std::string, LangProfile>& profiles() {
    static const std::map<std::string, LangProfile> table = []() -> std::map<std::string, LangProfile> {
        std::map<std::string, LangProfile> t;
        // Any failure (missing/corrupt data, unexpected shape) degrades to an
        // empty table: unknown languages render as plain code, never abort.
        try {
            nlohmann::json j = nlohmann::json::parse(keywords_json(), nullptr, false);
            if (j.is_discarded() || !j.is_object() || !j.contains("languages") ||
                !j["languages"].is_object())
                return t;

            for (auto it = j["languages"].begin(); it != j["languages"].end(); ++it) {
                if (!it.value().is_object()) continue;  // skip malformed entries
                const std::string& name = it.key();
                const nlohmann::json& v = it.value();

                LangProfile p;
                p.keywords = json_strings(v.value("keywords", nlohmann::json::array()));
                p.types = json_strings(v.value("types", nlohmann::json::array()));
                p.constants = json_strings(v.value("constants", nlohmann::json::array()));
                p.builtins = json_strings(v.value("builtins", nlohmann::json::array()));
                apply_syntax(v, p);
                p.hyphen_idents = has_hyphenated(p);

                t[name] = p;
                for (const auto& a : json_strings(v.value("aliases", nlohmann::json::array())))
                    t[a] = p;
            }
        } catch (...) {
            return {};
        }
        return t;
    }();
    return table;
}

}  // namespace

const LangProfile* lookup_language(const std::string& name) {
    const auto& t = profiles();
    auto it = t.find(name);
    return it == t.end() ? nullptr : &it->second;
}

}  // namespace detail
}  // namespace md
