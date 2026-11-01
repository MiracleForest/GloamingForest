#pragma once

// F10-internal parts of gf::combat (the death pipeline, loot and selftests use them).

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"

#include "feature/combat/Combat.h"

class ActorDamageSource;

namespace gf::combat {

/// A damage type (data/<ns>/damage_type/<path>.json, Java's DamageType).
struct DamageTypeSpec {
    std::string id;               ///< "twilightforest:hydra_bite"
    std::string messageId;        ///< "twilightforest.hydraBite" (death.attack.<messageId>)
    float       exhaustion = 0.f; ///< food exhaustion added to a hurt player
    std::string effects = "hurt"; ///< hurt / burning / freezing / ... / "twilightforest:pinch"
    std::string scaling = "when_caused_by_living_non_player";
    std::string deathMessageType = "default";
};

/// The damage type `id` (TF's 40 are known without data; data files override and add).
[[nodiscard]] DamageTypeSpec const* damageType(std::string const& id);
/// Every known damage type id, sorted.
[[nodiscard]] std::vector<std::string> damageTypeIds();
/// Whether damage type `id` is in damage type tag `tag` ("minecraft:bypasses_armor"): TF's own
/// tag memberships are built in, everything else is asked from gf::tags.
[[nodiscard]] bool damageTypeHasTag(std::string const& id, std::string const& tag);
/// The Bedrock cause that carries damage type `id` (armour, resistances, enchantments).
[[nodiscard]] SharedTypes::Legacy::ActorDamageCause causeFor(std::string const& id, bool hasAttacker);
/// The Java damage type id of a Bedrock source ("minecraft:player_attack", ...) for `victim`;
/// the TF type while gf::combat::hurt applies it.
[[nodiscard]] std::string typeOf(Actor& victim, ActorDamageSource const& source);
/// Attacker (responsible entity) and direct entity of a source (nullptr when none).
[[nodiscard]] std::pair<Actor*, Actor*> entitiesOf(ActorDamageSource const& source);
/// The TF death message of `victim` (Java's DamageSource#getLocalizedDeathMessage for the last
/// gf::combat::hurt that hit it this tick or the previous one): {key, params} with params[0] the
/// victim's name; an empty key when the last damage was not a TF one.
[[nodiscard]] std::pair<std::string, std::vector<std::string>> deathMessageFor(Actor& victim, std::string const& deadName);
/// The Bedrock display name of an entity for a death message ("Steve", "%entity.zombie.name").
[[nodiscard]] std::string nameParam(Actor& actor);

} // namespace gf::combat
