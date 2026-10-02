#include "ui.hpp"
#include "markdown.hpp"
#include "util.hpp"
#include <algorithm>

namespace {

// SGR sequences for UiStyle. Only bold/dim and basic colours 31-36 (black and
// white are never used), which the Linux console renders; keeps every line
// compatible with TERM=linux.
// Returns an empty string under NO_COLOR, so write_styled emits plain text.
std::string sgr(int style) {
    if (util::no_color()) return "";
    std::string s;
    if (style & STYLE_BOLD) s += "\033[1m";
    if (style & STYLE_DIM) s += "\033[2m";
    if (style & STYLE_RED) s += "\033[31m";
    if (style & STYLE_GREEN) s += "\033[32m";
    if (style & STYLE_YELLOW) s += "\033[33m";
    if (style & STYLE_BLUE) s += "\033[34m";
    if (style & STYLE_MAGENTA) s += "\033[35m";
    if (style & STYLE_CYAN) s += "\033[36m";
    if (style & STYLE_UNDERLINE) s += "\033[4m";
    if (style & STYLE_REVERSE) s += "\033[7m";
    return s;
}

void write_styled(Terminal& t, int style, const std::string& text) {
    if (text.empty()) return;
    std::string seq = sgr(style);
    if (seq.empty()) {
        t.write(text);
        return;
    }
    t.write(seq);
    t.write(text);
    t.reset_attrs();
}

// Cut to at most n characters (UTF-8 codepoints) without splitting a
// sequence. Truncation must count characters, not bytes: wrap() limits
// lines to n display columns, and a line of n characters can easily be
// more than n bytes - cutting at byte n would drop characters that fit.
std::string utf8_prefix(const std::string& s, size_t n) {
    size_t i = 0;
    size_t count = 0;
    while (i < s.size() && count < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        if (i + len > s.size()) len = 1;
        i += len;
        ++count;
    }
    return s.substr(0, i);
}

// Number of characters (UTF-8 codepoints) in s.
size_t utf8_len(const std::string& s) {
    size_t i = 0;
    size_t count = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        if (i + len > s.size()) len = 1;
        i += len;
        ++count;
    }
    return count;
}

// Scrollback cap.
constexpr size_t kMaxLines = 5000;

constexpr uint64_t kMarqueeMs = 500;

struct StatusChar {
    std::string bytes;
    int style;
};

size_t utf8_seq_len(unsigned char c) {
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

void push_status_chars(std::vector<StatusChar>& out, const std::string& s, int style) {
    size_t i = 0;
    while (i < s.size()) {
        size_t len = utf8_seq_len(static_cast<unsigned char>(s[i]));
        if (i + len > s.size()) len = 1;
        out.push_back({s.substr(i, len), style});
        i += len;
    }
}

std::string sanitize_text(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    size_t col = 0;
    size_t i = 0;
    while (i < in.size()) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (c == '\n') {
            out.push_back('\n');
            col = 0;
            ++i;
            continue;
        }
        if (c == '\t') {
            size_t pad = 4 - (col % 4);
            out.append(pad, ' ');
            col += pad;
            ++i;
            continue;
        }
        if (c < 0x20 || c == 0x7F) {
            ++i;
            continue;
        }
        size_t len = utf8_seq_len(c);
        if (i + len > in.size()) len = 1;
        out.append(in, i, len);
        col += 1;
        i += len;
    }
    return out;
}

int map_md_style(int st) {
    int r = STYLE_NORMAL;
    if (st & md::BOLD) r |= STYLE_BOLD;
    if (st & md::ITALIC) r |= STYLE_YELLOW;
    if (st & md::STRIKE) r |= STYLE_DIM;
    if (st & md::CODE) r |= STYLE_REVERSE;
    if (st & md::HEADING) r |= STYLE_BOLD | STYLE_CYAN;
    if (st & md::LINK) r |= STYLE_UNDERLINE | STYLE_BLUE;
    if (st & md::QUOTE) r |= STYLE_DIM;
    if (st & md::MARKER) r |= STYLE_DIM;

    // Code block syntax colours. Each token category gets its own attribute so
    // the block reads like a real editor on a plain Linux console. CODE_BLOCK is
    // the guaranteed base colour: if the keyword data fails to load, spans carry
    // only CODE_BLOCK and code still renders in cyan with no syntax colouring.
    if (st & md::CODE_BLOCK) r |= STYLE_CYAN;  // default / fallback code text
    if (st & md::SYN_KEYWORD) r |= STYLE_BOLD | STYLE_BLUE;
    if (st & md::SYN_CONSTANT) r |= STYLE_BOLD | STYLE_MAGENTA;
    if (st & md::SYN_TYPE) r |= STYLE_YELLOW;
    if (st & md::SYN_BUILTIN) r |= STYLE_CYAN;
    if (st & md::SYN_FUNCTION) r |= STYLE_BOLD | STYLE_CYAN;
    if (st & md::SYN_VARIABLE) r |= STYLE_CYAN;
    if (st & md::SYN_STRING) r |= STYLE_GREEN;
    if (st & md::SYN_NUMBER) r |= STYLE_MAGENTA;
    if (st & md::SYN_COMMENT) r |= STYLE_DIM;
    if (st & md::SYN_OPERATOR) r |= STYLE_RED;
    if (st & md::SYN_PREPROC) r |= STYLE_BOLD | STYLE_GREEN;
    if (st & md::SYN_ATTRIBUTE) r |= STYLE_YELLOW;
    return r;
}

}  // namespace

UI::UI(Terminal& term) : term_(term) {}

void UI::set_status(const std::string& model, const std::string& session, const std::string& cwd) {
    status_model_ = model;
    status_session_ = session;
    status_cwd_ = cwd;
    marquee_offset_ = 0;
}

int UI::viewport_rows() const {
    // 1 status + 1 sep + 1 input = 3 reserved
    return std::max(1, term_.rows() - 3);
}

void UI::push_lines(std::vector<DisplayLine> lines) {
    for (auto& l : lines) lines_.push_back(std::move(l));
    while (lines_.size() > kMaxLines) {
        lines_.pop_front();
        if (stream_base_ > 0) --stream_base_;  // indices shift with the pop
    }
}

void UI::wrap_and_push(const std::string& text, int style) {
    push_lines(wrap_spans({{text, style}}, std::max(10, term_.cols())));
}

std::vector<DisplayLine> UI::wrap_spans(const std::vector<Span>& spans, int width) const {
    std::vector<DisplayLine> out;
    if (width < 1) width = 1;

    struct Token {
        std::string text;
        int style;
        bool nl = false;
    };
    std::vector<Token> tokens;
    size_t tcol = 0;
    for (const auto& sp : spans) {
        size_t i = 0;
        while (i < sp.text.size()) {
            unsigned char c = static_cast<unsigned char>(sp.text[i]);
            if (c == '\n') {
                tokens.push_back({"", sp.style, true});
                tcol = 0;
                ++i;
                continue;
            }
            if (c == '\t') {
                size_t pad = 4 - (tcol % 4);  // expand to the next tab stop
                tokens.push_back({std::string(pad, ' '), sp.style, false});
                tcol += pad;
                ++i;
                continue;
            }
            if (c == ' ') {
                size_t j = i;
                while (j < sp.text.size() && sp.text[j] == ' ') ++j;
                tokens.push_back({std::string(j - i, ' '), sp.style, false});
                tcol += j - i;
                i = j;
                continue;
            }
            if (c < 0x20 || c == 0x7F) {  // controls never reach the terminal
                ++i;
                continue;
            }
            size_t len = utf8_seq_len(c);
            if (i + len > sp.text.size()) len = 1;
            tokens.push_back({sp.text.substr(i, len), sp.style, false});
            ++tcol;
            i += len;
        }
    }

    DisplayLine cur;
    int col = 0;
    int pending = 0;
    auto append = [&](const std::string& t, int style) {
        if (t.empty()) return;
        if (!cur.spans.empty() && cur.spans.back().style == style)
            cur.spans.back().text += t;
        else
            cur.spans.push_back({t, style});
    };
    auto newline = [&]() {
        out.push_back(std::move(cur));
        cur = DisplayLine{};
        col = 0;
        pending = 0;
    };
    auto emit_pending = [&]() {
        if (pending > 0 && col > 0) {
            append(std::string(static_cast<size_t>(pending), ' '), STYLE_NORMAL);
            col += pending;
        }
        pending = 0;
    };

    for (auto& t : tokens) {
        if (t.nl) {
            newline();
            continue;
        }
        if (t.text.find_first_not_of(' ') == std::string::npos) {
            pending += static_cast<int>(utf8_len(t.text));
            continue;
        }
        int len = static_cast<int>(utf8_len(t.text));
        if (col > 0 && col + pending + len > width) {
            newline();
            pending = 0;
        }
        emit_pending();
        if (col + len <= width) {
            append(t.text, t.style);
            col += len;
            continue;
        }
        size_t k = 0;
        while (k < t.text.size()) {
            if (col >= width) newline();
            size_t l = utf8_seq_len(static_cast<unsigned char>(t.text[k]));
            if (k + l > t.text.size()) l = 1;
            append(t.text.substr(k, l), t.style);
            ++col;
            k += l;
        }
    }
    out.push_back(std::move(cur));
    return out;
}

std::vector<DisplayLine> UI::render_markdown(const std::string& text, int width) const {
    auto lines = md::render(sanitize_text(text), std::max(10, width));
    std::vector<DisplayLine> out;
    out.reserve(lines.size());
    for (auto& l : lines) {
        DisplayLine dl;
        for (auto& sp : l) dl.spans.push_back({sp.text, map_md_style(sp.style)});
        out.push_back(std::move(dl));
    }
    return out;
}

void UI::append(const std::string& text, int style) {
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        std::string line = (nl == std::string::npos)
            ? text.substr(start)
            : text.substr(start, nl - start);
        wrap_and_push(line, style);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
}

void UI::append_markdown(const std::string& text) {
    push_lines(render_markdown(text, term_.cols()));
}

void UI::begin_stream(int style) {
    streaming_ = true;
    stream_style_ = style;
    stream_acc_.clear();
    lines_.push_back(DisplayLine{});
    // The stream block owns everything from this line on; stream_token()
    // pops back to here before re-wrapping the accumulated text.
    stream_base_ = lines_.size() - 1;
}

void UI::stream_token(const std::string& tok) {
    if (!streaming_) {
        begin_stream();
    }
    stream_acc_ += tok;
    // Render Markdown live so code fences are highlighted as they stream. The
    // parser is safe on partial input: an unclosed fence renders as a code
    // block and half-typed emphasis stays literal, so nothing jumps around.
    std::vector<DisplayLine> wrapped;
    if (markdown_ && stream_style_ != STYLE_DIM)
        wrapped = render_markdown(stream_acc_, std::max(10, term_.cols()));
    else
        wrapped = wrap_spans({{sanitize_text(stream_acc_), stream_style_}},
                             std::max(10, term_.cols()));
    while (lines_.size() > stream_base_) lines_.pop_back();
    for (auto& dl : wrapped) lines_.push_back(std::move(dl));
    if (lines_.empty()) lines_.push_back(DisplayLine{});
}

void UI::end_stream() {
    // Final render once the message is complete (closes any open fences cleanly).
    if (markdown_ && stream_style_ != STYLE_DIM && !stream_acc_.empty()) {
        auto rendered = render_markdown(stream_acc_, term_.cols());
        while (lines_.size() > stream_base_) lines_.pop_back();
        for (auto& dl : rendered) lines_.push_back(std::move(dl));
    }
    streaming_ = false;
    stream_acc_.clear();
}

void UI::clear_conversation() {
    lines_.clear();
    scroll_offset_ = 0;
    streaming_ = false;
    stream_acc_.clear();
    stream_base_ = 0;
}

void UI::scroll_up(int n) {
    int max_off = static_cast<int>(lines_.size()) - viewport_rows();
    if (max_off < 0) max_off = 0;
    scroll_offset_ = std::min(static_cast<size_t>(max_off), scroll_offset_ + static_cast<size_t>(n));
}

void UI::scroll_down(int n) {
    if (scroll_offset_ <= static_cast<size_t>(n)) scroll_offset_ = 0;
    else scroll_offset_ -= static_cast<size_t>(n);
}

void UI::scroll_page_up() { scroll_up(viewport_rows()); }
void UI::scroll_page_down() { scroll_down(viewport_rows()); }

void UI::set_input(const std::string& s) {
    input_ = s;
    cursor_ = s.size();
}

void UI::set_input(const std::string& s, size_t cursor) {
    input_ = s;
    cursor_ = std::min(cursor, s.size());
}

void UI::input_insert(uint32_t cp) {
    std::string u = utf8_from_cp(cp);
    input_.insert(cursor_, u);
    cursor_ += u.size();
}

void UI::input_backspace() {
    if (cursor_ == 0) return;
    // delete one UTF-8 char before cursor
    size_t i = cursor_;
    do { --i; } while (i > 0 && (static_cast<unsigned char>(input_[i]) & 0xC0) == 0x80);
    input_.erase(i, cursor_ - i);
    cursor_ = i;
}

void UI::input_delete() {
    if (cursor_ >= input_.size()) return;
    size_t i = cursor_;
    size_t len = 1;
    unsigned char c = static_cast<unsigned char>(input_[i]);
    if ((c & 0xE0) == 0xC0) len = 2;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xF8) == 0xF0) len = 4;
    if (i + len > input_.size()) len = input_.size() - i;
    input_.erase(i, len);
}

void UI::input_left() {
    if (cursor_ == 0) return;
    do { --cursor_; } while (cursor_ > 0 && (static_cast<unsigned char>(input_[cursor_]) & 0xC0) == 0x80);
}

void UI::input_right() {
    if (cursor_ >= input_.size()) return;
    unsigned char c = static_cast<unsigned char>(input_[cursor_]);
    size_t len = 1;
    if ((c & 0xE0) == 0xC0) len = 2;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xF8) == 0xF0) len = 4;
    cursor_ = std::min(input_.size(), cursor_ + len);
}

void UI::input_home() { cursor_ = 0; }
void UI::input_end() { cursor_ = input_.size(); }

void UI::input_history_up() {
    if (history_.empty()) return;
    if (history_idx_ < 0) {
        history_stash_ = input_;
        history_idx_ = static_cast<int>(history_.size()) - 1;
    } else if (history_idx_ > 0) {
        --history_idx_;
    }
    input_ = history_[static_cast<size_t>(history_idx_)];
    cursor_ = input_.size();
}

void UI::input_history_down() {
    if (history_idx_ < 0) return;
    if (history_idx_ + 1 >= static_cast<int>(history_.size())) {
        history_idx_ = -1;
        input_ = history_stash_;
    } else {
        ++history_idx_;
        input_ = history_[static_cast<size_t>(history_idx_)];
    }
    cursor_ = input_.size();
}

void UI::push_history(const std::string& s) {
    if (s.empty()) return;
    if (!history_.empty() && history_.back() == s) return;
    history_.push_back(s);
    if (history_.size() > 100) history_.erase(history_.begin());
    history_idx_ = -1;
}

std::string UI::utf8_from_cp(uint32_t cp) {
    std::string r;
    if (cp < 0x80) {
        r += static_cast<char>(cp);
    } else if (cp < 0x800) {
        r += static_cast<char>(0xC0 | (cp >> 6));
        r += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        r += static_cast<char>(0xE0 | (cp >> 12));
        r += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        r += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        r += static_cast<char>(0xF0 | (cp >> 18));
        r += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        r += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        r += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return r;
}

std::vector<std::pair<std::string, int>> UI::status_parts() const {
    std::vector<std::pair<std::string, int>> parts;
    auto kv = [&parts](const std::string& label, const std::string& value) {
        if (!parts.empty()) parts.push_back({"  ", STYLE_NORMAL});
        parts.push_back({label, STYLE_BOLD | STYLE_CYAN});
        parts.push_back({value, STYLE_NORMAL});
    };
    kv("model:", status_model_.empty() ? "(none)" : status_model_);
    kv("session:", status_session_.empty() ? "(none)" : status_session_);
    if (!status_ctx_.empty()) kv("ctx:", status_ctx_);
    if (!status_cwd_.empty()) kv("cwd:", status_cwd_);
    return parts;
}

void UI::draw_status() {
    term_.move_cursor(1, 1);
    term_.clear_line();

    auto parts = status_parts();
    const int cols = std::max(1, term_.cols());

    size_t total = 0;
    for (const auto& p : parts) total += utf8_len(p.first);

    if (total <= static_cast<size_t>(cols)) {
        marquee_offset_ = 0;
        int avail = cols;
        for (auto& p : parts) {
            if (avail <= 0) break;
            std::string t = utf8_prefix(p.first, static_cast<size_t>(avail));
            write_styled(term_, p.second, t);
            avail -= static_cast<int>(utf8_len(t));
        }
        return;
    }

    std::vector<StatusChar> chars;
    for (const auto& p : parts) push_status_chars(chars, p.first, p.second);
    push_status_chars(chars, "   ", STYLE_DIM);

    const size_t period = chars.size();
    if (period == 0) return;
    marquee_offset_ %= period;

    size_t shown = 0;
    size_t idx = marquee_offset_;
    while (shown < static_cast<size_t>(cols)) {
        const int style = chars[idx].style;
        std::string run;
        while (shown < static_cast<size_t>(cols) && chars[idx].style == style) {
            run += chars[idx].bytes;
            idx = (idx + 1) % period;
            ++shown;
        }
        write_styled(term_, style, run);
    }
}

void UI::tick_marquee() {
    size_t total = 0;
    for (const auto& p : status_parts()) total += utf8_len(p.first);

    if (total <= static_cast<size_t>(std::max(1, term_.cols()))) {
        if (marquee_offset_ != 0) {
            marquee_offset_ = 0;
            redraw();
        }
        return;
    }

    uint64_t now = util::steady_ms();
    if (now - marquee_last_ms_ < kMarqueeMs) return;
    marquee_last_ms_ = now;
    marquee_offset_ += 1;
    redraw();
}

// First conversation line and row count of the window currently shown.
// Shared by the live drawing and the exit dump so both show the same screen.
void UI::viewport_window(int& start, int& rows) const {
    rows = viewport_rows();
    int total = static_cast<int>(lines_.size());
    start = total - rows - static_cast<int>(scroll_offset_);
    if (start < 0) start = 0;
}

void UI::draw_viewport() {
    int start, rows;
    viewport_window(start, rows);
    int total = static_cast<int>(lines_.size());

    for (int i = 0; i < rows; ++i) {
        term_.move_cursor(2 + i, 1);
        term_.clear_line();
        int idx = start + i;
        if (idx >= 0 && idx < total) {
            const auto& dl = lines_[static_cast<size_t>(idx)];
            // Cut by character (UTF-8 codepoints); lines are wrapped to the
            // current width, so this only trims after a shrink.
            int avail = term_.cols();
            for (const auto& sp : dl.spans) {
                if (avail <= 0) break;
                std::string t = utf8_prefix(sp.text, static_cast<size_t>(avail));
                write_styled(term_, sp.style, t);
                avail -= static_cast<int>(utf8_len(t));
            }
        }
    }

    // separator / scroll / spinner indicator
    term_.move_cursor(term_.rows() - 1, 1);
    term_.clear_line();
    std::string ind;
    if (working_depth_ > 0) {
        static const char kFrames[] = "|/-\\";
        ind = std::string(1, kFrames[spinner_frame_]);
    }
    if (scroll_offset_ > 0) {
        if (!ind.empty()) ind += "  ";
        ind += "[scroll: " + std::to_string(scroll_offset_) + " lines above bottom]";
    }
    if (queue_count_ > 0) {
        if (!ind.empty()) ind += "  ";
        ind += "[queued: " + std::to_string(queue_count_) + "]";
    }
    if (ind.empty()) ind = "---";
    ind = utf8_prefix(ind, static_cast<size_t>(term_.cols()));
    write_styled(term_, STYLE_DIM, ind);
}

void UI::draw_input() {
    term_.move_cursor(term_.rows(), 1);
    term_.clear_line();
    std::string prefix = "> ";
    write_styled(term_, STYLE_BOLD | STYLE_GREEN, prefix);
    // Show a window of the input around the cursor when it overflows.
    int avail = term_.cols() - static_cast<int>(prefix.size());
    if (avail < 4) avail = 4;
    std::string shown = input_;
    size_t vis_cursor = cursor_;
    if (static_cast<int>(shown.size()) > avail) {
        // window around cursor
        size_t start = 0;
        if (cursor_ + 1 > static_cast<size_t>(avail))
            start = cursor_ + 1 - static_cast<size_t>(avail);
        shown = input_.substr(start, static_cast<size_t>(avail));
        vis_cursor = cursor_ - start;
    }
    term_.write(shown);
    // place cursor
    term_.move_cursor(term_.rows(), static_cast<int>(prefix.size()) + static_cast<int>(vis_cursor) + 1);
}

void UI::redraw() {
    term_.hide_cursor();
    draw_status();
    draw_viewport();
    draw_input();
    term_.show_cursor();
}

// Reprint the conversation window sequentially so it stays readable after
// the app exits: inside the alt screen it is discarded with the buffer; on a
// terminal that ignored the alt screen it rebuilds a clean last frame (the
// status / separator / input rows are dropped). Only sequences the plain
// Linux console supports are used (home, erase display, SGR).
void UI::dump_screen() {
    int start, rows;
    viewport_window(start, rows);
    int total = static_cast<int>(lines_.size());
    int width = std::max(10, term_.cols());
    term_.clear_screen();
    term_.move_cursor(1, 1);
    for (int i = 0; i < rows; ++i) {
        int idx = start + i;
        if (idx >= total) break;
        const auto& dl = lines_[static_cast<size_t>(idx)];
        // Re-wrap to the current width: lines were wrapped when appended and
        // the window may have shrunk since (on_resize does not re-wrap).
        for (auto& wl : wrap_spans(dl.spans, width)) {
            for (const auto& sp : wl.spans) write_styled(term_, sp.style, sp.text);
            term_.write("\r\n");
        }
    }
    // Cursor left at column 1 of the row after the last printed line.
}

void UI::farewell() {
    // Leading newline: after leave_alt_screen() the cursor sits right behind
    // the old shell prompt, so the message must start on its own row.
    term_.write("\r\n");
    write_styled(term_, STYLE_BOLD | STYLE_GREEN, "lumen: Bye!");
    term_.write(" Session was ");
    write_styled(term_, STYLE_BOLD,
                 status_session_.empty() ? "(none)" : status_session_);
    term_.write("\r\n");
}

void UI::on_resize() {
    term_.update_size();
    redraw();
}

bool UI::confirm(InputReader& in, const std::string& prompt) {
    // Raw mode has OPOST off, so a '\n' would not return to column 1 and a
    // multi-line prompt would staircase across the screen. Put everything
    // except the last line into the scrollback and keep the question on the
    // input row.
    auto lines = util::split_lines(prompt);
    std::string question = lines.empty() ? prompt : lines.back();
    if (!lines.empty()) lines.pop_back();
    for (const auto& l : lines) append(l, 1);
    if (!lines.empty()) redraw();
    term_.move_cursor(term_.rows(), 1);
    term_.clear_line();
    write_styled(term_, STYLE_BOLD | STYLE_YELLOW, question);  // must stand out
    term_.show_cursor();
    for (;;) {
        if (Terminal::got_sigterm.load()) return false;
        auto ev = in.read(200);
        if (!ev) continue;
        if (ev->key == Key::Char) {
            char c = static_cast<char>(ev->codepoint);
            if (c == 'y' || c == 'Y') return true;
            if (c == 'n' || c == 'N') return false;
        }
        if (ev->key == Key::Enter) return false;  // default N
        if (ev->key == Key::CtrlC || ev->key == Key::Escape) return false;
        if (ev->key == Key::CtrlQ) return false;  // must never be swallowed
    }
}

void UI::message(const std::string& msg, int style) {
    append(msg, style);
}

void UI::set_working(bool w) {
    bool was = working_depth_ > 0;
    working_depth_ += w ? 1 : -1;
    if (working_depth_ < 0) working_depth_ = 0;
    if (was != (working_depth_ > 0)) redraw();  // spinner appears / clears
}

void UI::tick_spinner() {
    if (working_depth_ <= 0) return;
    uint64_t now = util::steady_ms();
    if (now - spinner_last_ms_ < 300) return;
    spinner_last_ms_ = now;
    spinner_frame_ = (spinner_frame_ + 1) % 4;
    redraw();
}
