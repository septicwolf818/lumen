#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

// Keyboard input decoder for Linux TTY / VT100 / xterm-ish sequences.
// Designed to work with TERM=linux (limited sequences) and normal SSH terminals.

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

    // Non-blocking-ish: reads available bytes and returns next event if ready.
    // Returns nullopt if no complete event yet.
    std::optional<KeyEvent> poll();

    // Block until an event (or timeout_ms). timeout_ms=0 = non-block, -1 = forever
    std::optional<KeyEvent> read(int timeout_ms = -1);

    void push_byte(unsigned char c);  // for testing

private:
    std::string buf_;
    bool eof_ = false;  // stdin reached EOF; read() paces instead of spinning
    std::optional<KeyEvent> try_parse();
    std::optional<KeyEvent> parse_escape();
};
