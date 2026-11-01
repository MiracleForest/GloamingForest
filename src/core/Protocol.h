// Adapted from LiteLDev/Levitate v0.1.0 (src/core/common/Protocol.h), LGPL-3.0-or-later.
#pragma once

// Companion protocol envelope, independent of the game.
//
// Every message is a channel name plus a JSON payload. The handshake is:
//   client -> server  "net.hello"   {"protocol": N, "mod": "<mod name>"}
//   server -> client  "net.welcome" {"protocol": N, "accepted": true|false}
// A server marks a player as companion only after an accepted hello. The server never sends
// anything to a player that is not a companion, so vanilla clients never see these packets.

#include <optional>
#include <string>
#include <string_view>

#include "nlohmann/json.hpp"

namespace gf::core {

/// Bumped whenever a message changes incompatibly.
inline constexpr int kProtocolVersion = 1;
/// The oldest client protocol this build still accepts.
inline constexpr int kMinProtocolVersion = 1;

[[nodiscard]] constexpr bool protocolAccepted(int clientVersion) {
    return clientVersion >= kMinProtocolVersion && clientVersion <= kProtocolVersion;
}

struct Envelope {
    std::string    channel;
    nlohmann::json payload;
};

/// Payload text of a packet ("{}" for null payloads).
[[nodiscard]] std::string encodePayload(nlohmann::json const& payload);
/// nullopt when the payload is not valid JSON.
[[nodiscard]] std::optional<nlohmann::json> decodePayload(std::string_view text);

/// Channels are "<unit>.<name>" (lowercase, digits, '_', '.'); at most 64 characters.
[[nodiscard]] bool validChannel(std::string_view channel);
/// The unit part of a channel ("uncraftmaps" for "uncraftmaps.magic_map"; "" if none).
[[nodiscard]] std::string_view channelUnit(std::string_view channel);

} // namespace gf::core
