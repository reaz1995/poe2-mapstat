// Colour theme loaded from a small YAML file (see themes/*.yaml).
#pragma once
#include <cstdint>
#include <string>

struct Theme {
    std::string name = "Dracula";
    std::string author;
    bool dark = true;                  // dark title bar and scrollbars
    std::string fontUi = "Segoe UI";   // all text
    std::string fontMono = "Consolas"; // big timers
    // colours as 0xRRGGBB, defaults = built-in Dracula
    uint32_t bg = 0x282a36, card = 0x21222c, darkest = 0x191a21, line = 0x44475a, stripe = 0x252632;
    uint32_t fg = 0xf8f8f2, comment = 0x6272a4;
    uint32_t cyan = 0x8be9fd, green = 0x50fa7b, orange = 0xffb86c, pink = 0xff79c6, purple = 0xbd93f9,
             red = 0xff5555, yellow = 0xf1fa8c;
};

// Parses YAML text over the defaults in `t`. On error returns false and sets `err` ("line N: ...").
// Unknown keys are ignored and listed in `warn`.
bool parseTheme(const std::string& text, Theme& t, std::string& err, std::string& warn);
