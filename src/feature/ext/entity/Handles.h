#pragma once

// Contract extension of F8 (rule R18: new files only, folded into the W0 headers by A8 in W2b).
// What the entity and projectile contracts lack for registrations that come and go, and for the
// projectile impacts upstream's events change (NeoForge's ProjectileImpactEvent):
//
//   gf::entity::ext     a handle for a behaviour registration and its removal. Registrations of one
//                       type add up (gf::entity::registerType too): every hook of every registration
//                       runs, in registration order (hurt: each sees the amount the one before left,
//                       the first refusal cancels; interact: the first that used it wins; save: one
//                       document every registration writes its own keys into, load: every
//                       registration reads it: write your own keys, never assign the whole
//                       document). Because registrations add up, a module that registers in
//                       install() registers again when the mod is enabled again: register once
//                       per process, or use addBehaviour and removeBehaviour in uninstall().
//   gf::projectile::ext Projectile#setOwner (a parried projectile belongs to the blocker), and
//                       ProjectileImpactEvent#setCanceled for the hit being processed (parrying,
//                       perfect dodge): the projectile neither deals its hit nor bounces off.
//
//     auto h = gf::entity::ext::addBehaviour("minecraft:wolf", {.tick = ...});
//     gf::entity::ext::removeBehaviour(h);
//
//     // inside a gf::combat::onIncoming handler that parries an upstream projectile (EntityEvents#onParryProjectile)
//     gf::projectile::ext::setVelocity(*d.direct, look * speed);
//     gf::projectile::ext::setOwner(*d.direct, &player);
//     gf::projectile::ext::cancelImpact(*d.direct);

#include <cstdint>
#include <string>

#include "feature/entity/Entity.h"

class Actor;

namespace gf::entity::ext {

/// A behaviour registration (0: none).
using BehaviourHandle = std::uint64_t;
/// gf::entity::registerType with a handle for removeBehaviour.
[[nodiscard]] BehaviourHandle addBehaviour(std::string const& entityId, Behaviour behaviour);
/// Removes one registration (false: unknown or removed already). A hook that removes its own
/// registration finishes; the registration's other hooks do not run any more.
bool removeBehaviour(BehaviourHandle handle);

} // namespace gf::entity::ext

namespace gf::projectile::ext {

/// Projectile#setOwner: the projectile's shooter from now on (nullptr: none); its damage names the
/// new owner. Projectile#leftOwner stays as it was (Java's deflection keeps it).
void setOwner(Actor& projectile, Actor* owner);
/// ProjectileImpactEvent#setCanceled for the hit the projectile is processing now (call it from a
/// handler that runs inside the hit: gf::combat::onIncoming, Spec::onHitEntity / onHitBlock): the
/// hit is over without its outcome. An arrow is neither removed nor deflected (no REVERSE), a
/// throwable is not removed whatever its onHitEntity / onHitBlock answers; the projectile flies on
/// with its velocity (as ext::setVelocity set it: a parry's AIM_DEFLECT). No effect outside a hit.
void cancelImpact(Actor& projectile);

} // namespace gf::projectile::ext
