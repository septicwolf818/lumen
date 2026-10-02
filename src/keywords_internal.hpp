#pragma once

#include <string>
#include <vector>

// Internal bridge between the Markdown lexer (markdown.cpp) and the embedded
// language keyword data (keywords.cpp). keywords.cpp is the only translation
// unit that includes the build-generated keywords_data.inc, so markdown.cpp
// compiles on its own and does not depend on the generated header.

namespace md {
namespace detail {

struct LangProfile {
    std::vector<std::string> keywords;
    std::vector<std::string> types;
    std::vector<std::string> constants;  // booleans, None/null, ...
    std::vector<std::string> builtins;   // built-in function names
    std::string line_comment;            // starts a comment to end of line
    bool block_comment = false;          // C-style /* ... */
    bool case_insensitive = false;
    bool python_like = false;            // '#': preprocessor/decorator
};

// Look up a language by name or alias (already lower-cased by the caller).
// Returns nullptr when the language is unknown (plain code block).
const LangProfile* lookup_language(const std::string& name);

}  // namespace detail
}  // namespace md
