#include "keywords_internal.hpp"

#include "json.hpp"

#include <cctype>
#include <map>

#include "keywords_data.inc"  // generated: kKeywordsJson

namespace md {
namespace detail {
namespace {

std::vector<std::string> json_strings(const nlohmann::json& j) {
    std::vector<std::string> out;
    if (j.is_array())
        for (const auto& e : j)
            if (e.is_string()) out.push_back(e.get<std::string>());
    return out;
}

// Comment markers and lexer flags are not keyword data, so they stay here.
void apply_syntax(const std::string& name, LangProfile& p) {
    if (name == "python") { p.line_comment = "#"; p.python_like = true; }
    else if (name == "bash") { p.line_comment = "#"; p.python_like = true; }
    else if (name == "clike") { p.line_comment = "//"; p.block_comment = true; }
    else if (name == "yaml") { p.line_comment = "#"; p.python_like = true; }
    else if (name == "sql") { p.line_comment = "--"; p.block_comment = true; p.case_insensitive = true; }
}

const std::map<std::string, LangProfile>& profiles() {
    static const std::map<std::string, LangProfile> table = []() -> std::map<std::string, LangProfile> {
        std::map<std::string, LangProfile> t;
        // Any failure here (missing/corrupt data or an unexpected JSON shape)
        // degrades to an empty table: unknown languages then render as plain
        // code in the built-in colour instead of aborting the program.
        try {
            nlohmann::json j = nlohmann::json::parse(kKeywordsJson, nullptr, false);
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
                apply_syntax(name, p);

                t[name] = p;
                // Register every alias pointing at the same profile.
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
