// Adapted from LiteLDev/Levitate v0.1.0 (src/core/common/Protocol.cpp), LGPL-3.0-or-later.
#include "core/Protocol.h"

namespace gf::core {

std::string encodePayload(nlohmann::json const& payload) {
    if (payload.is_null()) return "{}";
    return payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::optional<nlohmann::json> decodePayload(std::string_view text) {
    auto j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded()) return std::nullopt;
    return j;
}

bool validChannel(std::string_view channel) {
    if (channel.empty() || channel.size() > 64) return false;
    for (char c : channel) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) return false;
    }
    return channel.front() != '.' && channel.back() != '.' && channel.find('.') != std::string_view::npos;
}

std::string_view channelUnit(std::string_view channel) {
    auto dot = channel.find('.');
    return dot == std::string_view::npos ? std::string_view{} : channel.substr(0, dot);
}

} // namespace gf::core
