#include "input.hpp"
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <cstring>

InputReader::InputReader() = default;

void InputReader::push_byte(unsigned char c) {
    buf_.push_back(static_cast<char>(c));
}

std::optional<KeyEvent> InputReader::poll() {
    // Read whatever is available
    unsigned char tmp[64];
    for (;;) {
        ssize_t n = ::read(STDIN_FILENO, tmp, sizeof(tmp));
        if (n > 0) {
            buf_.append(reinterpret_cast<char*>(tmp), static_cast<size_t>(n));
        } else {
            break;
        }
    }
    return try_parse();
}

std::optional<KeyEvent> InputReader::read(int timeout_ms) {
    for (;;) {
        auto ev = try_parse();
        if (ev) return ev;

        struct pollfd pfd{};
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        int pr = ::poll(&pfd, 1, timeout_ms);
        if (pr < 0) {
            if (errno == EINTR) continue;
            return std::nullopt;
        }
        if (pr == 0) return std::nullopt;  // timeout

        unsigned char tmp[64];
        ssize_t n = ::read(STDIN_FILENO, tmp, sizeof(tmp));
        if (n > 0) {
            buf_.append(reinterpret_cast<char*>(tmp), static_cast<size_t>(n));
        } else if (n < 0 && errno != EINTR && errno != EAGAIN) {
            return std::nullopt;
        }
        // if timeout_ms >= 0 and we already waited, loop once more to parse
        if (timeout_ms == 0) return try_parse();
    }
}

std::optional<KeyEvent> InputReader::try_parse() {
    if (buf_.empty()) return std::nullopt;

    unsigned char c = static_cast<unsigned char>(buf_[0]);

    // Ctrl keys (ASCII 1-26) when not part of escape
    if (c < 32 && c != 0x1b && c != '\t' && c != '\n' && c != '\r') {
        KeyEvent ev;
        ev.raw = buf_.substr(0, 1);
        buf_.erase(0, 1);
        switch (c) {
            case 3:  ev.key = Key::CtrlC; break;
            case 4:  ev.key = Key::CtrlD; break;
            case 12: ev.key = Key::CtrlL; break;
            case 14: ev.key = Key::CtrlN; break;
            case 17: ev.key = Key::CtrlQ; break;
            case 19: ev.key = Key::CtrlS; break;
            case 26: ev.key = Key::CtrlZ; break;
            case 8:  // Ctrl-H often = Backspace
            case 127: ev.key = Key::Backspace; break;
            default:
                ev.key = Key::Unknown;
                break;
        }
        return ev;
    }

    if (c == '\r' || c == '\n') {
        KeyEvent ev;
        ev.key = Key::Enter;
        ev.raw = buf_.substr(0, 1);
        buf_.erase(0, 1);
        // swallow CR LF pair
        if (c == '\r' && !buf_.empty() && buf_[0] == '\n') buf_.erase(0, 1);
        return ev;
    }

    if (c == '\t') {
        KeyEvent ev;
        ev.key = Key::Tab;
        ev.raw = buf_.substr(0, 1);
        buf_.erase(0, 1);
        return ev;
    }

    if (c == 0x7f || c == 0x08) {
        KeyEvent ev;
        ev.key = Key::Backspace;
        ev.raw = buf_.substr(0, 1);
        buf_.erase(0, 1);
        return ev;
    }

    if (c == 0x1b) {
        return parse_escape();
    }

    // Printable ASCII (and start of UTF-8 – we treat multi-byte as single Char events
    // by consuming one UTF-8 character)
    if (c >= 32) {
        size_t len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        if (buf_.size() < len) return std::nullopt;  // wait for more

        KeyEvent ev;
        ev.key = Key::Char;
        ev.raw = buf_.substr(0, len);
        // simplistic codepoint (enough for display / insert)
        if (len == 1) {
            ev.codepoint = c;
        } else {
            // decode roughly
            uint32_t cp = 0;
            if (len == 2) {
                cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(buf_[1]) & 0x3F);
            } else if (len == 3) {
                cp = ((c & 0x0F) << 12)
                   | ((static_cast<unsigned char>(buf_[1]) & 0x3F) << 6)
                   | (static_cast<unsigned char>(buf_[2]) & 0x3F);
            } else {
                cp = ((c & 0x07) << 18)
                   | ((static_cast<unsigned char>(buf_[1]) & 0x3F) << 12)
                   | ((static_cast<unsigned char>(buf_[2]) & 0x3F) << 6)
                   | (static_cast<unsigned char>(buf_[3]) & 0x3F);
            }
            ev.codepoint = cp;
        }
        buf_.erase(0, len);
        return ev;
    }

    // unknown control
    KeyEvent ev;
    ev.key = Key::Unknown;
    ev.raw = buf_.substr(0, 1);
    buf_.erase(0, 1);
    return ev;
}

std::optional<KeyEvent> InputReader::parse_escape() {
    // Need at least ESC
    if (buf_.size() < 1) return std::nullopt;

    // Lone ESC (or ESC timeout) – if only ESC and no more data yet, wait a bit?
    // For simplicity: if we have only ESC, treat as Escape after a short wait
    // is handled by the caller polling. Here if more bytes arrive we parse CSI.
    if (buf_.size() == 1) {
        // Could be lone Escape – return it so UI can react
        KeyEvent ev;
        ev.key = Key::Escape;
        ev.raw = buf_;
        buf_.clear();
        return ev;
    }

    // ESC [ ...  (CSI)
    if (buf_[1] == '[') {
        // Linux console (TERM=linux) function keys send ESC [ [ A .. E for
        // F1..F5. Check before the CSI scan below, which would take the second
        // '[' as the final byte and leak 'A'..'E' into the input as a Char.
        if (buf_.size() > 2 && buf_[2] == '[') {
            if (buf_.size() < 4) return std::nullopt;  // wait for final byte
            KeyEvent ev;
            ev.raw = buf_.substr(0, 4);
            char f = buf_[3];
            if (f >= 'A' && f <= 'E') {
                ev.key = static_cast<Key>(static_cast<int>(Key::F1) + (f - 'A'));
            } else {
                ev.key = Key::Unknown;
            }
            buf_.erase(0, 4);
            return ev;
        }
        // Find final byte (0x40-0x7E)
        size_t i = 2;
        while (i < buf_.size()) {
            unsigned char ch = static_cast<unsigned char>(buf_[i]);
            if (ch >= 0x40 && ch <= 0x7E) {
                std::string seq = buf_.substr(0, i + 1);
                buf_.erase(0, i + 1);
                KeyEvent ev;
                ev.raw = seq;

                // Parse common sequences
                // ESC [ A/B/C/D
                if (seq.size() == 3) {
                    switch (seq[2]) {
                        case 'A': ev.key = Key::Up; return ev;
                        case 'B': ev.key = Key::Down; return ev;
                        case 'C': ev.key = Key::Right; return ev;
                        case 'D': ev.key = Key::Left; return ev;
                        case 'H': ev.key = Key::Home; return ev;
                        case 'F': ev.key = Key::End; return ev;
                        default: break;
                    }
                }
                // ESC [ n ~
                if (seq.back() == '~') {
                    std::string mid = seq.substr(2, seq.size() - 3);
                    if (mid == "1" || mid == "7") { ev.key = Key::Home; return ev; }
                    if (mid == "4" || mid == "8") { ev.key = Key::End; return ev; }
                    if (mid == "2") { ev.key = Key::Insert; return ev; }
                    if (mid == "3") { ev.key = Key::Delete; return ev; }
                    if (mid == "5") { ev.key = Key::PageUp; return ev; }
                    if (mid == "6") { ev.key = Key::PageDown; return ev; }
                    if (mid == "11") { ev.key = Key::F1; return ev; }
                    if (mid == "12") { ev.key = Key::F2; return ev; }
                    if (mid == "13") { ev.key = Key::F3; return ev; }
                    if (mid == "14") { ev.key = Key::F4; return ev; }
                    if (mid == "15") { ev.key = Key::F5; return ev; }
                    if (mid == "17") { ev.key = Key::F6; return ev; }
                    if (mid == "18") { ev.key = Key::F7; return ev; }
                    if (mid == "19") { ev.key = Key::F8; return ev; }
                    if (mid == "20") { ev.key = Key::F9; return ev; }
                    if (mid == "21") { ev.key = Key::F10; return ev; }
                    if (mid == "23") { ev.key = Key::F11; return ev; }
                    if (mid == "24") { ev.key = Key::F12; return ev; }
                }
                // ESC [ 1 ; 5 A  etc (modifiers) – ignore modifiers for now
                if (seq.find(';') != std::string::npos) {
                    char final = seq.back();
                    if (final == 'A') { ev.key = Key::Up; return ev; }
                    if (final == 'B') { ev.key = Key::Down; return ev; }
                    if (final == 'C') { ev.key = Key::Right; return ev; }
                    if (final == 'D') { ev.key = Key::Left; return ev; }
                    if (final == 'H') { ev.key = Key::Home; return ev; }
                    if (final == 'F') { ev.key = Key::End; return ev; }
                }
                ev.key = Key::Unknown;
                return ev;
            }
            ++i;
        }
        // incomplete CSI – wait
        if (buf_.size() > 16) {
            // give up
            KeyEvent ev;
            ev.key = Key::Escape;
            ev.raw = buf_.substr(0, 1);
            buf_.erase(0, 1);
            return ev;
        }
        return std::nullopt;
    }

    // ESC O P/Q/R/S  (SS3 – F1-F4 on some terminals)
    if (buf_[1] == 'O' && buf_.size() >= 3) {
        KeyEvent ev;
        ev.raw = buf_.substr(0, 3);
        switch (buf_[2]) {
            case 'P': ev.key = Key::F1; break;
            case 'Q': ev.key = Key::F2; break;
            case 'R': ev.key = Key::F3; break;
            case 'S': ev.key = Key::F4; break;
            case 'H': ev.key = Key::Home; break;
            case 'F': ev.key = Key::End; break;
            default:  ev.key = Key::Unknown; break;
        }
        buf_.erase(0, 3);
        return ev;
    }

    // Alt+key: ESC + char
    if (buf_.size() >= 2 && static_cast<unsigned char>(buf_[1]) >= 32) {
        KeyEvent ev;
        ev.key = Key::Char;
        ev.alt = true;
        ev.codepoint = static_cast<unsigned char>(buf_[1]);
        ev.raw = buf_.substr(0, 2);
        buf_.erase(0, 2);
        return ev;
    }

    // fallback
    KeyEvent ev;
    ev.key = Key::Escape;
    ev.raw = buf_.substr(0, 1);
    buf_.erase(0, 1);
    return ev;
}
