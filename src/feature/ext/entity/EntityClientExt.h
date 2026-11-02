#pragma once

// Contract extension of F8 (rule R18: new files only, folded into the W0 headers by A8 in W2b),
// client mod only: the readers of what F8 sends to companion clients, for the client renderers of
// the W1 units (breath and charge flags, kobold panic, Hydra head names, Naga / Hydra part lists,
// the death animations of the bosses, thrown players). Thread-safe (the net handlers write on the
// client thread, renderers may read on the render thread). Implemented in
// src/client/entity/EntityClient.cpp; the server build has none of these.

#ifdef GF_CLIENT

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace gf::client::entity {

/// data(actor, key) as gf::entity::sync last sent it ("entity.data"); null when never synced.
/// `uid` is the actor's ActorUniqueID (Actor::getOrCreateUniqueID on the client).
[[nodiscard]] nlohmann::json data(std::int64_t uid, std::string const& key);
/// A TFDataSerializers.STRING_LIST value (gf::entity::ext::setStringList).
[[nodiscard]] std::vector<std::string> stringList(std::int64_t uid, std::string const& key);

/// One part of a multipart mob (UpdateTFMultipartPacket, "entity.multipart").
struct Part {
    std::string  id;
    std::int64_t uid       = 0; ///< the part actor's unique id
    std::int64_t runtimeId = 0;
    int          index     = 0;
    float        width     = 1.0f;
    float        height    = 1.0f;
};
[[nodiscard]] std::vector<Part> parts(std::int64_t parentUid);
/// The parent of a part actor (0: not a known part).
[[nodiscard]] std::int64_t partParent(std::int64_t partUid);

/// LivingEntity#deathTime of a dying boss: the value of its UpdateDeathTimePacket ("boss.death_time",
/// sent when the player starts seeing a dying boss), counted on at 20 ticks a second; 0 when none.
[[nodiscard]] int deathTime(std::int64_t uid);

/// YetiThrowAttachment of a player (UpdateThrownPacket, "projectile.thrown").
struct Thrown {
    bool         thrown   = false;
    std::int64_t thrower  = 0;
    int          cooldown = 0;
};
[[nodiscard]] std::optional<Thrown> thrown(std::int64_t playerUid);

/// The local player rides a hostile mount (ClientGameEvents#removeHostileMountHealth).
[[nodiscard]] bool ridingUnfriendly();

/// The TFBossBarPacket state of a boss's bar ("boss.bar"; ClientTFBossBar).
struct BossBar {
    unsigned    rgb      = 0xFFFFFF;
    int         overlay  = 0;
    bool        darken   = false;
    bool        fog      = false;
    float       progress = 1.0f;
    std::string title;
};
[[nodiscard]] std::optional<BossBar> bossBar(std::int64_t uid);

} // namespace gf::client::entity

#endif
