#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

// Keyboard decoder for TERM=linux and VT100/xterm-style terminals.

enum class Key : int {
    None = 0,
    Char,           // printable / utf8 start – use codepoint
    Enter,
    Backspace,
    Tab,
    Escape,
    Up, Down, Left, Right,
    Home, End,
    PageUp, PageDown,
    Insert, Delete,
    CtrlC, CtrlD, CtrlL, CtrlN, CtrlS, CtrlQ, CtrlZ,
    CtrlEnter,      // often indistinguishable; treated as Enter on basic TTY
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Unknown
};

struct KeyEvent {
    Key key = Key::None;
    uint32_t codepoint = 0;   // for Key::Char
    bool ctrl = false;
    bool alt  = false;
    std::string raw;          // original bytes (debug)
};

class InputReader {
public:
    InputReader();

    // Return the next complete event, or nullopt if none is ready yet.
    std::optional<KeyEvent> poll();

    // Wait for an event. timeout_ms: 0 = non-blocking, -1 = forever.
    std::optional<KeyEvent> read(int timeout_ms = -1);

    void push_byte(unsigned char c);  // for testing

private:
    std::string buf_;
    bool eof_ = false;  // stdin reached EOF; read() paces instead of spinning
    std::optional<KeyEvent> try_parse();
    std::optional<KeyEvent> parse_escape();
};
