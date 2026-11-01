// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Contract "synth" (F3): a small JSON reader for patches, tracks and render jobs.
#include "core/synth/Json.h"

#include <charconv>
#include <cstdint>
#include <cstdio>

namespace gf::core::synth {

namespace {

class Parser {
public:
    explicit Parser(std::string_view t) : mText(t) {}

    Json parseDocument() {
        skipSpace();
        Json v = parseValue(0);
        skipSpace();
        if (mPos != mText.size()) error("trailing characters");
        return v;
    }

private:
    [[noreturn]] void error(char const* what) const {
        int line = 1, col = 1;
        for (std::size_t i = 0; i < mPos && i < mText.size(); ++i) {
            if (mText[i] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
        throw JsonError(
            "JSON " + std::string(what) + " at line " + std::to_string(line) + ", column " + std::to_string(col)
        );
    }

    void skipSpace() {
        while (mPos < mText.size()) {
            char c = mText[mPos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++mPos;
            } else if (c == '/' && mPos + 1 < mText.size() && mText[mPos + 1] == '/') {
                // line comments are tolerated in hand-written recipes
                while (mPos < mText.size() && mText[mPos] != '\n') ++mPos;
            } else {
                break;
            }
        }
    }

    char peek() const { return mPos < mText.size() ? mText[mPos] : '\0'; }

    Json parseValue(int depth) {
        if (depth > 200) error("nesting too deep");
        char c = peek();
        if (c == '{') return parseObject(depth);
        if (c == '[') return parseArray(depth);
        if (c == '"') return Json(parseString());
        if (c == 't' && mText.substr(mPos, 4) == "true") {
            mPos += 4;
            return Json(true);
        }
        if (c == 'f' && mText.substr(mPos, 5) == "false") {
            mPos += 5;
            return Json(false);
        }
        if (c == 'n' && mText.substr(mPos, 4) == "null") {
            mPos += 4;
            return Json();
        }
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        error("unexpected character");
    }

    Json parseNumber() {
        std::size_t start = mPos;
        if (peek() == '-') ++mPos;
        while (mPos < mText.size()) {
            char c = mText[mPos];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                ++mPos;
            } else {
                break;
            }
        }
        double      v     = 0;
        char const* first = mText.data() + start;
        char const* last  = mText.data() + mPos;
        auto        res   = std::from_chars(first, last, v);
        if (res.ec != std::errc() || res.ptr != last) {
            mPos = start;
            error("bad number");
        }
        return Json(v);
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += char(cp);
        } else if (cp < 0x800) {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
    }

    unsigned parseHex4() {
        if (mPos + 4 > mText.size()) error("bad \\u escape");
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            char c   = mText[mPos++];
            v      <<= 4;
            if (c >= '0' && c <= '9') v |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
            else error("bad \\u escape");
        }
        return v;
    }

    std::string parseString() {
        ++mPos; // opening quote
        std::string out;
        while (true) {
            if (mPos >= mText.size()) error("unterminated string");
            char c = mText[mPos++];
            if (c == '"') break;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (mPos >= mText.size()) error("unterminated string");
            char e = mText[mPos++];
            switch (e) {
            case '"':
                out += '"';
                break;
            case '\\':
                out += '\\';
                break;
            case '/':
                out += '/';
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u': {
                unsigned cp = parseHex4();
                if (cp >= 0xD800 && cp < 0xDC00 && mPos + 1 < mText.size() && mText[mPos] == '\\'
                    && mText[mPos + 1] == 'u') {
                    mPos        += 2;
                    unsigned lo  = parseHex4();
                    cp           = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                appendUtf8(out, cp);
                break;
            }
            default:
                error("bad escape");
            }
        }
        return out;
    }

    Json parseArray(int depth) {
        ++mPos;
        Json arr = Json::array();
        skipSpace();
        if (peek() == ']') {
            ++mPos;
            return arr;
        }
        while (true) {
            skipSpace();
            arr.push(parseValue(depth + 1));
            skipSpace();
            char c = peek();
            if (c == ',') {
                ++mPos;
                skipSpace();
                if (peek() == ']') { // trailing comma tolerated
                    ++mPos;
                    break;
                }
                continue;
            }
            if (c == ']') {
                ++mPos;
                break;
            }
            error("expected ',' or ']'");
        }
        return arr;
    }

    Json parseObject(int depth) {
        ++mPos;
        Json obj = Json::object();
        skipSpace();
        if (peek() == '}') {
            ++mPos;
            return obj;
        }
        while (true) {
            skipSpace();
            if (peek() != '"') error("expected a key");
            std::string key = parseString();
            skipSpace();
            if (peek() != ':') error("expected ':'");
            ++mPos;
            skipSpace();
            obj.set(std::move(key), parseValue(depth + 1));
            skipSpace();
            char c = peek();
            if (c == ',') {
                ++mPos;
                skipSpace();
                if (peek() == '}') {
                    ++mPos;
                    break;
                }
                continue;
            }
            if (c == '}') {
                ++mPos;
                break;
            }
            error("expected ',' or '}'");
        }
        return obj;
    }

    std::string_view mText;
    std::size_t      mPos = 0;
};

Json const& nullJson() {
    static Json const n;
    return n;
}

void dumpString(std::string& out, std::string const& s) {
    out += '"';
    for (char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (std::uint8_t(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", unsigned(std::uint8_t(c)));
                out += buf;
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

void dumpValue(std::string& out, Json const& v) {
    switch (v.type()) {
    case Json::Type::Null:
        out += "null";
        break;
    case Json::Type::Bool:
        out += v.boolean() ? "true" : "false";
        break;
    case Json::Type::Number: {
        char buf[64];
        auto res = std::to_chars(buf, buf + sizeof(buf), v.number());
        out.append(buf, res.ptr);
        break;
    }
    case Json::Type::String:
        dumpString(out, v.string());
        break;
    case Json::Type::Array: {
        out        += '[';
        bool first  = true;
        for (auto const& x : v.items()) {
            if (!first) out += ',';
            first = false;
            dumpValue(out, x);
        }
        out += ']';
        break;
    }
    case Json::Type::Object: {
        out        += '{';
        bool first  = true;
        for (auto const& [k, x] : v.members()) {
            if (!first) out += ',';
            first = false;
            dumpString(out, k);
            out += ':';
            dumpValue(out, x);
        }
        out += '}';
        break;
    }
    }
}

} // namespace

Json Json::parse(std::string_view text) {
    // a UTF-8 byte order mark is skipped
    if (text.size() >= 3 && std::uint8_t(text[0]) == 0xEF && std::uint8_t(text[1]) == 0xBB
        && std::uint8_t(text[2]) == 0xBF) {
        text.remove_prefix(3);
    }
    return Parser(text).parseDocument();
}

double Json::number() const {
    if (mType == Type::Number) return mNumber;
    if (mType == Type::Bool) return mBool ? 1.0 : 0.0;
    throw JsonError("JSON value is not a number");
}

bool Json::boolean() const {
    if (mType == Type::Bool) return mBool;
    if (mType == Type::Number) return mNumber != 0;
    throw JsonError("JSON value is not a boolean");
}

std::string const& Json::string() const {
    if (mType != Type::String) throw JsonError("JSON value is not a string");
    return mString;
}

std::size_t Json::size() const {
    if (mType == Type::Array) return mArray.size();
    if (mType == Type::Object) return mObject.size();
    return 0;
}

Json const& Json::operator[](std::size_t i) const {
    if (mType != Type::Array || i >= mArray.size()) return nullJson();
    return mArray[i];
}

void Json::push(Json v) {
    if (mType == Type::Null) mType = Type::Array;
    mArray.push_back(std::move(v));
}

Json const* Json::find(std::string_view key) const {
    if (mType != Type::Object) return nullptr;
    for (auto const& [k, v] : mObject) {
        if (k == key) return &v;
    }
    return nullptr;
}

Json const& Json::operator[](std::string_view key) const {
    auto const* v = find(key);
    return v ? *v : nullJson();
}

void Json::set(std::string key, Json v) {
    if (mType == Type::Null) mType = Type::Object;
    for (auto& [k, x] : mObject) {
        if (k == key) {
            x = std::move(v);
            return;
        }
    }
    mObject.emplace_back(std::move(key), std::move(v));
}

double Json::num(std::string_view key, double def) const {
    auto const* v = find(key);
    return v && (v->isNumber() || v->isBool()) ? v->number() : def;
}

std::string Json::str(std::string_view key, std::string_view def) const {
    auto const* v = find(key);
    return v && v->isString() ? v->string() : std::string(def);
}

bool Json::flag(std::string_view key, bool def) const {
    auto const* v = find(key);
    return v && (v->isBool() || v->isNumber()) ? v->boolean() : def;
}

std::string Json::dump() const {
    std::string out;
    dumpValue(out, *this);
    return out;
}

} // namespace gf::core::synth
