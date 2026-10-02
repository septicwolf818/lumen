#include "ui.hpp"
#include "util.hpp"
#include <algorithm>
#include <cstring>

UI::UI(Terminal& term) : term_(term) {}

void UI::set_status(const std::string& model, const std::string& session, const std::string& cwd) {
    status_model_ = model;
    status_session_ = session;
    status_cwd_ = cwd;
}

int UI::viewport_rows() const {
    // 1 status + 1 sep + 1 input = 3 reserved
    return std::max(1, term_.rows() - 3);
}

void UI::wrap_and_push(const std::string& text, int style) {
    auto wrapped = wrap(text, std::max(10, term_.cols() - 1));
    for (auto& w : wrapped) {
        lines_.push_back({w, style});
    }
    // cap memory
    const size_t max_lines = 5000;
    while (lines_.size() > max_lines) lines_.pop_front();
    if (scroll_offset_ == 0) {
        // stay at bottom
    }
}

std::vector<std::string> UI::wrap(const std::string& text, int width) const {
    std::vector<std::string> out;
    if (text.empty()) {
        out.push_back("");
        return out;
    }
    size_t i = 0;
    while (i < text.size()) {
        // take up to width columns (approx = bytes for ASCII)
        size_t take = 0;
        size_t cols = 0;
        while (i + take < text.size() && cols < static_cast<size_t>(width)) {
            unsigned char c = static_cast<unsigned char>(text[i + take]);
            if (c == '\n') break;
            size_t clen = 1;
            if ((c & 0xE0) == 0xC0) clen = 2;
            else if ((c & 0xF0) == 0xE0) clen = 3;
            else if ((c & 0xF8) == 0xF0) clen = 4;
            if (i + take + clen > text.size()) clen = 1;
            take += clen;
            cols += 1;  // treat as 1 column
        }
        out.push_back(text.substr(i, take));
        i += take;
        if (i < text.size() && text[i] == '\n') ++i;
    }
    if (out.empty()) out.push_back("");
    return out;
}

void UI::append(const std::string& text, int style) {
    // split on newlines first
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
    if (scroll_offset_ == 0) {
        // auto follow
    } else {
        // user scrolled up – keep offset roughly stable by adding to offset
        // (new lines at bottom don't move the view)
    }
}

void UI::append_raw(const std::string& text) {
    append(text, 0);
}

void UI::begin_stream() {
    streaming_ = true;
    stream_acc_.clear();
    lines_.push_back({"", 0});
}

void UI::stream_token(const std::string& tok) {
    if (!streaming_) {
        begin_stream();
    }
    stream_acc_ += tok;
    // rebuild last line(s) from stream_acc_
    // simple approach: pop last open lines and re-wrap
    // For efficiency on Pi: just append visible chars to last line until wrap
    if (lines_.empty()) lines_.push_back({"", 0});
    // naive: replace last line content and re-wrap stream_acc_
    // Count how many lines belong to current stream – keep it simple:
    // remove trailing empty/stream lines and re-push
    while (!lines_.empty() && lines_.back().text.empty() && lines_.size() > 1) {
        // keep at least one
        break;
    }
    // Better simple approach for streaming:
    auto wrapped = wrap(stream_acc_, std::max(10, term_.cols() - 1));
    // Find start of stream block: we tagged begin with one empty line; replace from there
    // Simplest robust: keep a stream_start index
    // For this version: pop lines that were added during stream and re-add
    // We store stream_line_count
    static thread_local size_t stream_base = 0;
    if (stream_acc_.size() == tok.size()) {
        // first token of this stream
        stream_base = lines_.size() > 0 ? lines_.size() - 1 : 0;
        if (lines_.empty()) {
            lines_.push_back({"", 0});
            stream_base = 0;
        }
    }
    while (lines_.size() > stream_base) lines_.pop_back();
    for (auto& w : wrapped) lines_.push_back({w, 0});
    if (lines_.empty()) lines_.push_back({"", 0});

    if (scroll_offset_ == 0) {
        // will redraw at bottom
    }
}

void UI::end_stream() {
    streaming_ = false;
    if (!stream_acc_.empty() && (lines_.empty() || lines_.back().text != stream_acc_)) {
        // ensure final
    }
    stream_acc_.clear();
}

void UI::clear_conversation() {
    lines_.clear();
    scroll_offset_ = 0;
    streaming_ = false;
    stream_acc_.clear();
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
void UI::scroll_home() {
    int max_off = static_cast<int>(lines_.size()) - viewport_rows();
    scroll_offset_ = max_off > 0 ? static_cast<size_t>(max_off) : 0;
}
void UI::scroll_end() { scroll_offset_ = 0; }
bool UI::at_bottom() const { return scroll_offset_ == 0; }

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

void UI::draw_status() {
    term_.move_cursor(1, 1);
    term_.clear_line();
    std::string s = "model: " + (status_model_.empty() ? "(none)" : status_model_);
    s += "  session: " + (status_session_.empty() ? "(none)" : status_session_);
    if (!status_ctx_.empty()) {
        s += "  ctx: " + status_ctx_;
    }
    if (!status_cwd_.empty()) {
        s += "  cwd: " + status_cwd_;
    }
    if (s.size() > static_cast<size_t>(term_.cols()))
        s = s.substr(0, static_cast<size_t>(term_.cols()));
    term_.write(s);
}

void UI::draw_viewport() {
    int rows = viewport_rows();
    int total = static_cast<int>(lines_.size());
    int start = total - rows - static_cast<int>(scroll_offset_);
    if (start < 0) start = 0;

    for (int i = 0; i < rows; ++i) {
        term_.move_cursor(2 + i, 1);
        term_.clear_line();
        int idx = start + i;
        if (idx >= 0 && idx < total) {
            const auto& dl = lines_[static_cast<size_t>(idx)];
            std::string t = dl.text;
            if (t.size() > static_cast<size_t>(term_.cols()))
                t = t.substr(0, static_cast<size_t>(term_.cols()));
            if (dl.style == 1) {
                term_.write("\033[2m");  // dim
                term_.write(t);
                term_.reset_attrs();
            } else {
                term_.write(t);
            }
        }
    }

    // separator / scroll indicator
    term_.move_cursor(term_.rows() - 1, 1);
    term_.clear_line();
    std::string ind;
    if (scroll_offset_ > 0) {
        ind = "[scroll: " + std::to_string(scroll_offset_) + " lines above bottom]";
    }
    if (queue_count_ > 0) {
        if (!ind.empty()) ind += "  ";
        ind += "[queued: " + std::to_string(queue_count_) + "]";
    }
    if (ind.empty()) ind = "---";
    if (static_cast<int>(ind.size()) > term_.cols()) ind = ind.substr(0, term_.cols());
    term_.write(ind);
}

void UI::draw_input() {
    term_.move_cursor(term_.rows(), 1);
    term_.clear_line();
    std::string prefix = "> ";
    term_.write(prefix);
    // show input, handle overflow simply: show tail around cursor
    int avail = term_.cols() - static_cast<int>(prefix.size()) - 1;
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
    // cursor already placed in draw_input
}

void UI::on_resize() {
    term_.update_size();
    // re-wrap would be ideal; for simplicity just redraw
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
    term_.write(question);
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

void UI::message(const std::string& msg) {
    last_message_ = msg;
    append(msg, 1);
}
