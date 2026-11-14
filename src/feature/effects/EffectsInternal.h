#pragma once

// Internals of the effect runtime shared with F9's client part and selftests.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "feature/effects/Effects.h"

class Actor;

namespace gf::effects::internal {

/// A vanilla instance of a carrier's effect that the carrier replaced (restored when the custom
/// effect ends while it would still run).
struct Displaced {
    int  amplifier = 0;
    int  ticks     = 0; ///< remaining
    bool visible   = true;
    bool ambient   = false;
};

/// One active custom effect of an actor.
struct Active {
    std::string id;
    int         amplifier = 0;
    int         ticks     = 0; ///< remaining
    /// MobEffectInstance#hiddenEffect: weaker, longer applications kept behind this one, next first
    /// (amplifier, remaining ticks); they resume when the one before ends.
    std::vector<std::pair<int, int>> hidden;
    std::optional<Displaced>         displaced;
    /// Read back from its persistence tag and not yet matched with the carrier the actor kept (the
    /// tag may be up to kWriteInterval ticks old).
    bool reloaded = false;
};

/// How often (ticks) the persistence tags are rewritten while nothing else changes.
inline constexpr int kWriteInterval = 20;

/// The custom effects of an actor (loaded from its persistence tags when not in memory yet).
[[nodiscard]] std::vector<Active> activeOf(Actor const& actor);
/// The spec of a registered custom effect.
[[nodiscard]] std::optional<Spec> specOf(std::string const& effectId);
/// The Bedrock effect id of a vanilla effect id ("minecraft:slowness" -> 2), nullopt for others.
[[nodiscard]] std::optional<unsigned> vanillaId(std::string const& effectId);
/// Runs one tick of the custom effects of every tracked actor (the module does it every tick).
void tickAll();
/// The persistence tag prefix on the actor ("gf_fx|<id>|<amplifier>|<ticks>|<hidden>|<displaced>").
inline constexpr char kTagPrefix[] = "gf_fx|";
/// Forgets the in-memory state of an actor (selftests: the next query reads its tags again).
void forget(Actor const& actor);
/// Ends every custom effect of an actor (milk, /effect clear: the removeAllEffects hook).
void clearAll(Actor& actor);
/// Removes a registered effect type (selftests).
void unregisterType(std::string const& effectId);
/// Whether the actor's instance of a carrier effect is the one the custom effect `active` put there
/// (its amplifier, its remaining time and the particle visibility of `spec`), not a vanilla one.
[[nodiscard]] bool carrierIsOurs(Actor const& actor, Spec const& spec, Active const& active);
/// The resource pack texture of a custom effect's icon (ext::setIcon, else
/// "textures/<namespace>/mob_effect/<path>", where F1 imports upstream's textures/mob_effect/*.png).
[[nodiscard]] std::string iconOf(std::string const& effectId);

} // namespace gf::effects::internal
