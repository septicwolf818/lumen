#pragma once

#include <string>
#include <vector>

// A small, self-contained Markdown subset parser and renderer.
//
// It parses into a block/inline tree and renders it to a list of styled visual
// lines that fit a given width. Styles are semantic; the UI maps them onto the
// attributes the Linux console supports. Rendering is deliberately plain: no
// HTML, no reference links, single-level lists, table columns may be trimmed.
//
// Supported blocks:  paragraph, ATX heading, fenced code, blockquote,
//                    unordered/ordered list, table (GFM), thematic break.
// Supported inlines: text, emphasis, strong, strikethrough, code span,
//                    link/image, autolink, hard break.

namespace md {

enum Style : int {
    NONE          = 0,
    BOLD          = 1 << 0,
    ITALIC        = 1 << 1,
    STRIKE        = 1 << 2,
    CODE          = 1 << 3,   // inline code
    CODE_BLOCK    = 1 << 4,
    HEADING       = 1 << 5,
    LINK          = 1 << 6,
    QUOTE         = 1 << 7,
    MARKER        = 1 << 8,   // bullets, table borders, rules
    SYN_KEYWORD   = 1 << 9,
    SYN_STRING    = 1 << 10,
    SYN_COMMENT   = 1 << 11,
    SYN_NUMBER    = 1 << 12,
    SYN_FUNCTION  = 1 << 13,
    SYN_TYPE      = 1 << 14,
    SYN_CONSTANT  = 1 << 15,  // built-in constants / booleans / None
    SYN_BUILTIN   = 1 << 16,  // language built-in functions
    SYN_VARIABLE  = 1 << 17,  // variables / parameters
    SYN_OPERATOR  = 1 << 18,  // operators and punctuation
    SYN_PREPROC   = 1 << 19,  // preprocessor directives (#include, ...)
    SYN_ATTRIBUTE = 1 << 20,  // decorators, annotations
};

struct Span {
    std::string text;
    int style = NONE;
};

using Line  = std::vector<Span>;  // one rendered visual line
using Lines = std::vector<Line>;

struct Inline {
    enum Kind { Text, Emph, Strong, Strike, Code, Link, Break } kind = Text;
    std::string text;                  // literal, or link/image label
    std::string url;                   // Link only
    std::vector<Inline> children;      // emphasis/strong/strike/link
};

struct TableCell {
    std::vector<Inline> inlines;
    int align = 0;                     // 0 left, 1 center, 2 right
};

struct TableRow {
    std::vector<TableCell> cells;
};

struct Block;

struct ListItem {
    std::vector<Block> blocks;
};

struct Block {
    enum Kind { Paragraph, Heading, Code, Quote, BulletList, OrderedList, Table, Rule } kind = Paragraph;
    int level = 0;                     // heading level, or ordered list start
    std::string lang;                  // code fence info string
    std::string text;                  // code block literal
    std::vector<Inline> inlines;       // paragraph / heading
    std::vector<Block> children;       // quote
    std::vector<ListItem> items;       // list
    std::vector<TableRow> rows;        // table; rows[0] is the header
};

std::vector<Block> parse(const std::string& src);
Lines render(const std::vector<Block>& blocks, int width);
Lines render(const std::string& src, int width);

}  // namespace md
