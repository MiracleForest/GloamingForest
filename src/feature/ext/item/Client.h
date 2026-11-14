#pragma once

// Extension of contract "data" / "enchant+effects" by F9 (rule R18; to be folded by A8): the client
// mod's view of the synced player data. The server sends a companion its traits ("traits.sync"),
// custom effects ("effects.sync") and statistics ("stats.sync") on join and on every change; these
// read the local copy (client build only, client thread).
//
//     if (gf::traits::ext::client::get("gloamingforest:banished")) ...;

#ifdef GF_CLIENT

#include <map>
#include <string>
#include <vector>

namespace gf::traits::ext::client {
/// The local player's trait (false before the server sent it).
[[nodiscard]] bool get(std::string const& traitId);
} // namespace gf::traits::ext::client

namespace gf::effects::ext::client {
struct Active {
    std::string id;
    int         amplifier = 0;
    int         ticks     = 0; ///< remaining when the server sent it
};
/// The local player's custom effects.
[[nodiscard]] std::vector<Active> active();
/// The amplifier of a custom effect on the local player (-1: none).
[[nodiscard]] int amplifier(std::string const& effectId);
} // namespace gf::effects::ext::client

namespace gf::stats::ext::client {
/// The local player's statistics.
[[nodiscard]] std::map<std::string, long long> values();
} // namespace gf::stats::ext::client

#endif // GF_CLIENT
