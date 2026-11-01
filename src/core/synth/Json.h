// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Contract "synth" (F3): a small JSON reader for patches, tracks and render jobs.
#pragma once

// Self-contained so that the synthesizer CLI (tools/sounds/cli) compiles with nothing but the C++
// standard library. Objects keep their key order; numbers are doubles (parsed exactly with
// std::from_chars).

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gf::core::synth {

struct JsonError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(double v) : mType(Type::Number), mNumber(v) {}
    Json(int v) : mType(Type::Number), mNumber(v) {}
    Json(bool v) : mType(Type::Bool), mBool(v) {}
    Json(std::string v) : mType(Type::String), mString(std::move(v)) {}
    Json(char const* v) : mType(Type::String), mString(v) {}

    static Json array() {
        Json j;
        j.mType = Type::Array;
        return j;
    }
    static Json object() {
        Json j;
        j.mType = Type::Object;
        return j;
    }

    /// Parses `text`; throws JsonError with line and column on bad input.
    static Json parse(std::string_view text);

    [[nodiscard]] Type type() const { return mType; }
    [[nodiscard]] bool isNull() const { return mType == Type::Null; }
    [[nodiscard]] bool isBool() const { return mType == Type::Bool; }
    [[nodiscard]] bool isNumber() const { return mType == Type::Number; }
    [[nodiscard]] bool isString() const { return mType == Type::String; }
    [[nodiscard]] bool isArray() const { return mType == Type::Array; }
    [[nodiscard]] bool isObject() const { return mType == Type::Object; }

    [[nodiscard]] double             number() const;
    [[nodiscard]] bool               boolean() const;
    [[nodiscard]] std::string const& string() const;

    // arrays
    [[nodiscard]] std::size_t       size() const;
    [[nodiscard]] Json const&       operator[](std::size_t i) const;
    [[nodiscard]] std::vector<Json> const& items() const { return mArray; }
    void                                   push(Json v);

    // objects
    [[nodiscard]] bool        has(std::string_view key) const { return find(key) != nullptr; }
    [[nodiscard]] Json const* find(std::string_view key) const;
    /// The member `key`, or a null value when absent.
    [[nodiscard]] Json const& operator[](std::string_view key) const;
    [[nodiscard]] std::vector<std::pair<std::string, Json>> const& members() const { return mObject; }
    void                                                           set(std::string key, Json v);

    // typed lookups with defaults
    [[nodiscard]] double      num(std::string_view key, double def) const;
    [[nodiscard]] std::string str(std::string_view key, std::string_view def) const;
    [[nodiscard]] bool        flag(std::string_view key, bool def) const;

    /// Compact serialization (numbers with up to 17 significant digits).
    [[nodiscard]] std::string dump() const;

private:
    Type                                    mType   = Type::Null;
    bool                                    mBool   = false;
    double                                  mNumber = 0;
    std::string                             mString;
    std::vector<Json>                       mArray;
    std::vector<std::pair<std::string, Json>> mObject;
};

} // namespace gf::core::synth
