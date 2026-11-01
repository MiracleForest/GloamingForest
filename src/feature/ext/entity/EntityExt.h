#pragma once

// Contract extension of F8 (rule R18: new files only, folded into the W0 headers by A8 in W2b).
// What the entity / projectile / boss contracts lack for TF's bosses and mobs:
//
//   gf::entity::ext     TFPart hits (Java's attackEntityFromPart), part management, the
//                       NoClipMoveControl wanted position, ITFCharger / IBreathAttacker state and
//                       the breath attack, IHostileMount / YetiThrowAttachment state.
//   gf::projectile::ext the owner and velocity of a simulated projectile.
//   gf::boss::ext       ServerTFBossBar#updateStyle (the Lich's colour changes), the boss bar
//                       flags, the death time, the 27-slot loot buffer, the "bosses" config values
//                       the multiplayer loot functions read, and the alpha yeti trophy's rare roar.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "mc/deps/core/math/Vec3.h"

#include "feature/entity/Entity.h"

class Actor;
class ItemStack;
class Mob;
class Player;

namespace gf::entity::ext {

/// The part id of the hit being forwarded from a part to its parent ("" when the current damage
/// did not come through a part): read it in Behaviour::hurt (Hydra heads, Naga segments).
[[nodiscard]] std::string const& hurtPart();
/// The parts of a mob, in addPart order.
[[nodiscard]] std::vector<PartHandle> parts(Mob& mob);
/// Removes one part actor (a Hydra head that died).
void removePart(PartHandle const& part);
/// TFPart#setSize: resizes the part's hitbox.
void setPartSize(PartHandle const& part, PartSize size);
/// The parent of a part actor (nullptr: not a part, or the parent is gone).
[[nodiscard]] Actor* parentOf(Actor const& part);

/// MoveControl#setWantedPosition for the NoClip controller (Java's MoveControl.Operation.MOVE_TO).
void setWantedPosition(Mob& mob, Vec3 const& pos, double speed);
/// The wanted position (nullopt: the controller is idle).
[[nodiscard]] std::optional<Vec3> wantedPosition(Mob const& mob);

/// ITFCharger#isCharging, IBreathAttacker#isBreathing (set by the charge / breath goals).
[[nodiscard]] bool isCharging(Actor const& mob);
[[nodiscard]] bool isBreathing(Actor const& mob);
/// IBreathAttacker#doBreathAttack of an entity type; without one the breath deals 2 damage of the
/// goal's damage type (TF's fire beetle and winter wolf numbers).
using BreathAttackFn = std::function<void(Mob& breather, Actor& target)>;
void onBreathAttack(std::string const& entityId, BreathAttackFn fn);
/// Runs the breath attack of `breather` on `target` (used by goals::breath).
void doBreathAttack(Mob& breather, Actor& target, std::string const& damageType);

/// HostileMountEvents#isRidingUnfriendly: `actor` rides a mob that grabbed it.
[[nodiscard]] bool isRidingUnfriendly(Actor const& actor);
/// HostileMountEvents#hostileDismount: the rider may leave (the mob lets go).
void hostileDismount(Actor& rider);
/// YetiThrowAttachment: thrown by a hostile mount and still in the air.
[[nodiscard]] bool thrown(Player const& player);
/// YetiThrowAttachment#getThrowCooldown (200 ticks after a throw).
[[nodiscard]] int throwCooldown(Player const& player);
/// The mob that threw the player (nullptr: none or gone).
[[nodiscard]] Actor* thrower(Player const& player);

} // namespace gf::entity::ext

namespace gf::projectile::ext {

/// Whether `actor` is a projectile simulated by gf::projectile.
[[nodiscard]] bool isProjectile(Actor const& actor);
/// The shooter of a projectile (nullptr: none or gone).
[[nodiscard]] Actor* owner(Actor const& projectile);
[[nodiscard]] Vec3 velocity(Actor const& projectile);
void               setVelocity(Actor& projectile, Vec3 const& velocity);

} // namespace gf::projectile::ext

namespace gf::boss::ext {

/// BossEvent.BossBarOverlay: 0 progress, 1..4 notched 6 / 10 / 12 / 20.
enum class Overlay { Progress = 0, Notched6, Notched10, Notched12, Notched20 };
/// ServerTFBossBar#updateStyle: a new colour / overlay (allowLerp false: the client snaps).
void setBarStyle(Mob& boss, unsigned rgb, Overlay overlay = Overlay::Progress, bool allowLerp = true);
/// BossEvent#setDarkenScreen / setCreateWorldFog at runtime.
void setBarFlags(Mob& boss, bool darken, bool fog);
/// The current colour of a boss's bar (0xRRGGBB; nullopt: not a registered boss).
[[nodiscard]] std::optional<unsigned> barColor(Mob& boss);
/// The nearest of the 8 Bedrock BossBarColor values (0 pink .. 7 white) to `rgb`.
[[nodiscard]] int nearestBarColor(unsigned rgb);

/// LivingEntity#deathTime of a dying boss (0: alive).
[[nodiscard]] int deathTime(Mob const& boss);

/// IBossLootBuffer: the 27 slots that go into the death chest (empty stacks for free slots).
[[nodiscard]] std::vector<ItemStack> lootBuffer(Mob& boss);
void                                 setLootBufferSlot(Mob& boss, int slot, ItemStack const& stack);

/// TFConfig.bossDropChests and multiplayerFightAdjuster (config section "bosses").
[[nodiscard]] bool dropChests();
[[nodiscard]] bool adjustsLootRolls();
[[nodiscard]] bool adjustsHealth();

/// The rare sound of a trophy (the alpha yeti roars once in 50): {sound, 1 / chance}.
struct RareSound {
    std::string sound;
    int         oneIn = 0;
};
[[nodiscard]] std::optional<RareSound> trophyRareSound(std::string const& blockId);

} // namespace gf::boss::ext
