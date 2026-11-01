// Adapted from LiteLDev/Levitate v0.1.0 (src/core/Format.cpp), LGPL-3.0-or-later.
#include "core/Format.h"

namespace gf::core {

namespace {
bool isDigit(char c) { return c >= '0' && c <= '9'; }
} // namespace

std::string formatMessage(std::string_view fmt, std::span<std::string const> args) {
    std::string out;
    out.reserve(fmt.size() + 16);
    std::size_t next = 0;
    for (std::size_t i = 0; i < fmt.size(); ++i) {
        char c = fmt[i];
        if (c == '{' && i + 1 < fmt.size() && fmt[i + 1] == '{') {
            out += '{';
            ++i;
            continue;
        }
        if (c == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
            out += '}';
            ++i;
            continue;
        }
        if (c == '{') {
            auto close = fmt.find('}', i);
            if (close != std::string_view::npos) {
                auto        inner  = fmt.substr(i + 1, close - i - 1);
                bool        digits = !inner.empty();
                std::size_t idx    = 0;
                for (char d : inner) {
                    if (!isDigit(d)) {
                        digits = false;
                        break;
                    }
                    idx = idx * 10 + std::size_t(d - '0');
                }
                if (inner.empty() && next < args.size()) {
                    out += args[next++];
                    i = close;
                    continue;
                }
                if (digits && idx < args.size()) {
                    out += args[idx];
                    i = close;
                    continue;
                }
            }
        }
        if (c == '%' && i + 1 < fmt.size()) {
            // Java's String.format subset used by TF's lang files: %s, %d, %1$s, %%.
            if (fmt[i + 1] == '%') {
                out += '%';
                ++i;
                continue;
            }
            if (fmt[i + 1] == 's' || fmt[i + 1] == 'd') {
                if (next < args.size()) {
                    out += args[next++];
                    ++i;
                    continue;
                }
            } else if (isDigit(fmt[i + 1])) {
                std::size_t j = i + 1, idx = 0;
                while (j < fmt.size() && isDigit(fmt[j])) idx = idx * 10 + std::size_t(fmt[j++] - '0');
                if (j + 1 < fmt.size() && fmt[j] == '$' && (fmt[j + 1] == 's' || fmt[j + 1] == 'd') && idx >= 1
                    && idx - 1 < args.size()) {
                    out += args[idx - 1];
                    i = j + 1;
                    continue;
                }
            }
        }
        out += c;
    }
    return out;
}

std::string compactCount(long long n) {
    if (n < 0) return "-" + compactCount(-n);
    if (n < 1000) return std::to_string(n);
    auto fmt1 = [](long long v, long long unit, char suffix) {
        long long   tenths = v * 10 / unit;
        std::string s      = std::to_string(tenths / 10);
        if (tenths < 100 && tenths % 10 != 0) s += "." + std::to_string(tenths % 10);
        return s + suffix;
    };
    if (n < 1000000) return fmt1(n, 1000, 'k');
    if (n < 1000000000) return fmt1(n, 1000000, 'M');
    return fmt1(n, 1000000000, 'G');
}

} // namespace gf::core
