#pragma once

// Internals of the effect runtime shared with F9's client part and selftests.

#include <optional>
#include <string>
#include <vector>

#include "feature/effects/Effects.h"

class Actor;

namespace gf::effects::internal {

/// One active custom effect of an actor.
struct Active {
    std::string id;
    int         amplifier = 0;
    int         ticks     = 0; ///< remaining
};

/// The custom effects of an actor (loaded from its persistence tags when not in memory yet).
[[nodiscard]] std::vector<Active> activeOf(Actor const& actor);
/// The spec of a registered custom effect.
[[nodiscard]] std::optional<Spec> specOf(std::string const& effectId);
/// The Bedrock effect id of a vanilla effect id ("minecraft:slowness" -> 2), nullopt for others.
[[nodiscard]] std::optional<unsigned> vanillaId(std::string const& effectId);
/// Runs one tick of the custom effects of every tracked actor (the module does it every tick).
void tickAll();
/// The persistence tag prefix on the actor ("gf_fx|<id>|<amplifier>|<ticks>").
inline constexpr char kTagPrefix[] = "gf_fx|";
/// Forgets the in-memory state of an actor (selftests: the next query reads its tags again).
void forget(Actor const& actor);
/// Ends every custom effect of an actor (milk, /effect clear: the removeAllEffects hook).
void clearAll(Actor& actor);
} // namespace gf::effects::internal
