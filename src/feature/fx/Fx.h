#pragma once

// Contract "rules+fx", effects by id (header owned by F0, implemented by F10; rule R10).
//
// Every sound event and particle has exactly one owner (docs/spec/owners.tsv) and is played by id
// through here; a missing one is silent (one warning per id). Sound events are TF's names
// ("entity.twilightforest.naga.hiss"), particles TF's ids ("twilightforest:firefly").
//
//     gf::fx::sound(dim, "block.twilightforest.portal.woosh", pos, 1.0f, 1.0f);
//     gf::fx::particle(dim, "twilightforest:firefly", pos);

#include <string>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"

class Dimension;
class Player;

namespace gf::fx {

/// Emits `count` particles of `particleId` at `pos` (velocity `velocity`, extra `params` for
/// parametrised particles: colour, scale, ...), visible to the players near it.
void particle(Dimension& dimension, std::string const& particleId, Vec3 const& pos, Vec3 const& velocity = {}, int count = 1, nlohmann::json const& params = {});
/// Plays sound event `event` at `pos` for the players near it.
void sound(Dimension& dimension, std::string const& event, Vec3 const& pos, float volume = 1.0f, float pitch = 1.0f);
/// Plays `event` to one player only (at the player).
void soundTo(Player& player, std::string const& event, float volume = 1.0f, float pitch = 1.0f);

} // namespace gf::fx
