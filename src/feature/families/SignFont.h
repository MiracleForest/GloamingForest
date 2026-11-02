#pragma once

// Block families (F11): Java's sign line width, shared by the server (Signs.cpp: the cut of an
// edit's lines) and the client mod's sign editor (src/client/families/Screens.cpp: where typing
// stops), so both builds cut a line at the same place. Font.width over the default font's
// advances; a character outside ASCII counts 9 (the widest glyph of the fallback font). Lines are
// UTF-8. No game headers: both builds include it.

#include <cstddef>
#include <string>
#include <string_view>

namespace gf::feature::families::signfont {

/// The advance of a character in the default font (font pixels, the shadow column included).
[[nodiscard]] constexpr int charWidth(char32_t c) {
    switch (c) {
    case '!': case '\'': case ',': case '.': case ':': case ';': case '|': case 'i': return 2;
    case '`': case 'l': return 3;
    case ' ': case 'I': case '[': case ']': case 't': return 4;
    case '"': case '(': case ')': case '*': case '<': case '>': case 'f': case 'k': case '{': case '}': return 5;
    case '@': case '~': return 7;
    default: return c < 0x80 ? 6 : 9;
    }
}

/// The byte length of the UTF-8 sequence that starts with `lead`.
[[nodiscard]] constexpr std::size_t utf8Length(unsigned char lead) { return lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : 4; }

/// The code point at byte `i` of `s` (`len`: its byte length, cut at the end of `s`).
[[nodiscard]] inline char32_t decodeAt(std::string_view s, std::size_t i, std::size_t& len) {
    auto     lead = static_cast<unsigned char>(s[i]);
    len           = utf8Length(lead);
    char32_t cp   = len == 1 ? lead : len == 2 ? (lead & 0x1F) : len == 3 ? (lead & 0x0F) : (lead & 0x07);
    for (std::size_t k = 1; k < len && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    if (i + len > s.size()) len = s.size() - i;
    return cp;
}

/// Font.width of a line.
[[nodiscard]] inline int lineWidth(std::string_view s) {
    int w = 0;
    for (std::size_t i = 0; i < s.size();) {
        std::size_t len = 1;
        w += charWidth(decodeAt(s, i, len));
        i += len;
    }
    return w;
}

/// The line cut where SignEditScreen stops typing: whole characters while the width stays within
/// `maxWidth` and the bytes within `maxBytes`, up to a line break.
[[nodiscard]] inline std::string fitLine(std::string_view line, int maxWidth, std::size_t maxBytes) {
    std::string out;
    int         width = 0;
    for (std::size_t i = 0; i < line.size();) {
        auto        lead = static_cast<unsigned char>(line[i]);
        std::size_t len  = utf8Length(lead);
        if (lead == '\n' || lead == '\r' || i + len > line.size() || out.size() + len > maxBytes) break;
        std::size_t got = len;
        int         w   = charWidth(decodeAt(line, i, got));
        if (width + w > maxWidth) break;
        width += w;
        out.append(line.substr(i, len));
        i += len;
    }
    return out;
}

/// At most `maxBytes` of `s`, cut between characters (JSON documents need valid UTF-8).
[[nodiscard]] inline std::string utf8Prefix(std::string_view s, std::size_t maxBytes) {
    std::size_t end = 0;
    while (end < s.size()) {
        std::size_t len = utf8Length(static_cast<unsigned char>(s[end]));
        if (end + len > maxBytes || end + len > s.size()) break;
        end += len;
    }
    return std::string(s.substr(0, end));
}

} // namespace gf::feature::families::signfont
