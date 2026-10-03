#pragma once

#include <string>
#include <vector>

// Bridge between the Markdown lexer and the embedded keyword data.
// keywords.cpp is the only unit that includes the generated keywords_data.inc,
// so markdown.cpp compiles without that header.

namespace md {
namespace detail {

struct LangProfile {
    std::vector<std::string> keywords;
    std::vector<std::string> types;
    std::vector<std::string> constants;  // booleans, None/null, ...
    std::vector<std::string> builtins;   // built-in function names
    std::string line_comment;            // starts a comment to end of line
    std::string block_comment_open;      // multi-line comment start, e.g. "/*"
    std::string block_comment_close;     // multi-line comment end, e.g. "*/"
    bool case_insensitive = false;
    bool python_like = false;            // '#': preprocessor/decorator
};

// Look up by name or alias (caller lower-cases). nullptr = unknown language.
const LangProfile* lookup_language(const std::string& name);

}  // namespace detail
}  // namespace md
