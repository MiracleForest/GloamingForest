// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/monster/HostileWolf.java (isValidLightLevel)
//   src/main/java/twilightforest/entity/monster/SkeletonDruid.java (isValidLightLevel)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U17 (hostiles): the helpers the forest and hollow-hill hostiles share (ids, light, difficulty,
// equipment, the BP component groups the native goals switch, vanilla goals translated for the
// mobs whose TF goal stacks are native). Not a contract: only src/feature/hostiles uses it.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"

#include "core/jrand/JRand.h"
#include "feature/entity/Entity.h"
#include "feature/spawn/Spawn.h"

class Actor;
class BlockPos;
class BlockSource;
class ItemStack;
class Mob;
class Player;

namespace gf::feature::hostiles {

using Json = nlohmann::json;
namespace ge = gf::entity;

namespace id {
inline constexpr char HostileWolf[]  = "twilightforest:hostile_wolf";
inline constexpr char MistWolf[]     = "twilightforest:mist_wolf";
inline constexpr char WinterWolf[]   = "twilightforest:winter_wolf";
inline constexpr char KingSpider[]   = "twilightforest:king_spider";
inline constexpr char HedgeSpider[]  = "twilightforest:hedge_spider";
inline constexpr char SwarmSpider[]  = "twilightforest:swarm_spider";
inline constexpr char Druid[]        = "twilightforest:skeleton_druid";
inline constexpr char NatureBolt[]   = "twilightforest:nature_bolt";
inline constexpr char Redcap[]       = "twilightforest:redcap";
inline constexpr char RedcapSapper[] = "twilightforest:redcap_sapper";
inline constexpr char Wraith[]       = "twilightforest:wraith";
inline constexpr char FireBeetle[]   = "twilightforest:fire_beetle";
inline constexpr char SlimeBeetle[]  = "twilightforest:slime_beetle";
inline constexpr char SlimeBlob[]    = "twilightforest:slime_blob";
inline constexpr char PinchBeetle[]  = "twilightforest:pinch_beetle";
} // namespace id

// ---- actors ------------------------------------------------------------------------------------------

[[nodiscard]] std::int64_t uidOf(Actor const& actor);
[[nodiscard]] Actor*       actorById(std::int64_t uid);
[[nodiscard]] Mob*         asMob(Actor& actor);
[[nodiscard]] std::string  typeOf(Actor const& actor);
[[nodiscard]] bool         isType(Actor const* actor, char const* id);
/// A living mob or player (Java's LivingEntity; projectiles, items and the like are not).
[[nodiscard]] bool isLiving(Actor const& actor);
/// The mob's attack target (alive), nullptr when none.
[[nodiscard]] Actor* targetOf(Mob& mob);
/// The mob's goal random source (F8's java.util.Random per actor; seedable with ge::seedForTest).
[[nodiscard]] gf::core::jrand::RandomSource& randomOf(Actor& actor);
/// A random source for things Java draws from the level's random.
[[nodiscard]] gf::core::jrand::RandomSource& levelRandom();
[[nodiscard]] BlockPos blockPosOf(Actor const& actor);
[[nodiscard]] int      dimOf(Actor const& actor);
/// Entity#distanceToSqr(Vec3) from the feet.
[[nodiscard]] double distSqr(Actor const& a, Vec3 const& p);
[[nodiscard]] double distance(Actor const& a, Actor const& b);
/// Whether the native goal state of the actor holds goals (F8 sweeps the state of unloaded actors:
/// a mob that comes back needs its goals again).
[[nodiscard]] bool nativeGoalsPresent(Actor& actor);

// ---- world ----------------------------------------------------------------------------------------------

[[nodiscard]] bool mobGriefing();
[[nodiscard]] bool doEntityDrops();
/// 0 peaceful, 1 easy, 2 normal, 3 hard.
[[nodiscard]] int difficulty();
/// LevelReader#getMaxLocalRawBrightness: max(sky light - sky darken, block light).
[[nodiscard]] int maxLocalRawBrightness(BlockSource& region, BlockPos const& pos);
/// DimensionType#brightness of the Twilight Forest (ambient light 0.01) for a light level.
[[nodiscard]] float brightnessOf(int light);
/// Entity#getLightLevelDependentMagicValue (the brightness at the actor's eyes' block).
[[nodiscard]] float lightMagicValue(Actor& actor);
/// DifficultyInstance#getSpecialMultiplier (no inhabited time on Bedrock).
[[nodiscard]] float specialMultiplier();
/// The chunk's landmark (LegacyLandmarkPlacements#pickLandmarkForChunk) is the hedge maze (only in
/// the Twilight Forest).
[[nodiscard]] bool hedgeMazeChunk(BlockSource& region, BlockPos const& pos);
/// Monster#isDarkEnoughToSpawn.
[[nodiscard]] bool darkEnough(BlockSource& region, BlockPos const& pos, gf::core::jrand::RandomSource& random);
/// Monster#checkMonsterSpawnRules: not peaceful, dark enough unless a spawner places it.
[[nodiscard]] bool checkMonsterSpawnRules(BlockSource& region, BlockPos const& pos, gf::spawn::SpawnReason reason, gf::core::jrand::RandomSource& random);

// ---- effects and sounds ----------------------------------------------------------------------------------

enum Effect : unsigned { Speed = 1, Strength = 5, Regeneration = 10, Invisibility = 14, Blindness = 15, Poison = 19 };
/// LivingEntity#addEffect (`ticks` < 0: infinite).
void addEffect(Actor& actor, unsigned effect, int ticks, int amplifier = 0);
/// Entity#playSound at the actor (TF's sound events and Java vanilla events, through gf::fx).
void playSound(Actor& actor, std::string const& event, float volume, float pitch);
/// LivingEntity#getVoicePitch with `base` ((r1 - r2) x 0.2 + base).
[[nodiscard]] float voicePitch(Actor& actor, float base = 1.0f);

// ---- equipment --------------------------------------------------------------------------------------------

/// Sets the main hand / feet item and tells the clients that see the mob.
void setMainHand(Mob& mob, ItemStack const& stack);
void setFeet(Mob& mob, ItemStack const& stack);
[[nodiscard]] std::string mainHandId(Mob& mob);
/// Mob#populateDefaultEquipmentEnchantments on the main hand (chance 0.25) and the feet (0.5).
void enchantSpawnEquipment(Mob& mob);
/// Mob#dropCustomDeathLoot of the hand and feet with their drop chances (only when a player killed
/// it recently; damaged at random); the engine never drops them again.
void dropEquipment(Mob& mob, std::vector<ItemStack>& drops, float handChance, float feetChance, bool byPlayer, Actor* killer);

// ---- the BP's component groups -----------------------------------------------------------------------------

/// Runs a BP event on the actor.
bool fireEvent(Actor& actor, std::string const& event);
/// Switches the BP group gf:<group> on / off (events gf:<group>_on / _off); only on a change.
void setGroup(Actor& actor, std::string const& group, bool on);
/// The BP group's state as last set (nullopt: never set this session).
[[nodiscard]] std::optional<bool> groupState(Actor& actor, std::string const& group);
/// Sets a property only when it changed (the clients get one packet per change).
void setPropertyCached(Actor& actor, std::string const& name, ge::PropertyValue const& value);

// ---- spawning -----------------------------------------------------------------------------------------------

/// Spawns a mob (spawn reason as given; nullptr on failure).
Mob* spawnMob(BlockSource& region, std::string const& id, Vec3 const& pos, gf::spawn::SpawnReason reason, float yaw = 0.0f);
/// A non-mob actor by id (primed TNT).
Actor* spawnActor(BlockSource& region, std::string const& id, Vec3 const& pos);
/// The first finalizeSpawn of a mob runs once per mob (persisted in its data).
[[nodiscard]] bool firstSpawn(Actor& actor);

// ---- vanilla goals translated for the native goal stacks ------------------------------------------------------

/// The goals of a mob whose native goals run, active or not: the BP's melee / stroll group
/// (gf:vanilla_move) yields while a native goal with the MOVE flag runs (Java's flag lock).
class MoveLock {
public:
    explicit MoveLock(std::int64_t uid) : mUid(uid) {}
    void hold(bool on);
    [[nodiscard]] static bool held(std::int64_t uid);
    static void              forget(std::int64_t uid);

private:
    std::int64_t mUid;
    bool         mHeld = false;
};
/// Switches gf:vanilla_move from the locks of the mob (call every tick).
void syncVanillaMove(Mob& mob);

/// A goal of F8 whose MOVE flag keeps the BP's melee and stroll waiting while it runs.
class LockedGoal final : public ge::Goal {
public:
    LockedGoal(Mob& mob, ge::GoalPtr inner);
    bool canUse() override { return mInner->canUse(); }
    bool canContinueToUse() override { return mInner->canContinueToUse(); }
    bool isInterruptable() override { return mInner->isInterruptable(); }
    bool requiresUpdateEveryTick() override { return mInner->requiresUpdateEveryTick(); }
    void start() override;
    void stop() override;
    void tick() override { mInner->tick(); }

private:
    ge::GoalPtr mInner;
    MoveLock        mLock;
};

/// net.minecraft.world.entity.ai.goal.RangedAttackGoal.
class RangedAttackGoal : public ge::Goal {
public:
    using Attack = std::function<void(Mob& mob, Actor& target, float power)>;
    RangedAttackGoal(Mob& mob, double speed, int intervalMin, int intervalMax, float radius, Attack attack, std::function<bool(Mob&)> usable = {});
    bool canUse() override;
    bool canContinueToUse() override;
    void start() override;
    void stop() override;
    bool requiresUpdateEveryTick() override { return true; }
    void tick() override;

private:
    std::int64_t              mId;
    std::int64_t              mTarget = 0;
    double                    mSpeed;
    int                       mMin, mMax;
    float                     mRadius;
    Attack                    mAttack;
    std::function<bool(Mob&)> mUsable;
    int                       mAttackTime = -1;
    int                       mSeeTime    = 0;
    MoveLock                  mLock;
};

/// net.minecraft.world.entity.ai.goal.AvoidEntityGoal (players: NO_CREATIVE_OR_SPECTATOR).
class AvoidEntityGoal : public ge::Goal {
public:
    AvoidEntityGoal(Mob& mob, std::function<bool(Actor&)> avoid, float distance, double walk, double sprint);
    bool canUse() override;
    bool canContinueToUse() override;
    void start() override;
    void stop() override;
    void tick() override;

private:
    std::int64_t                 mId;
    std::function<bool(Actor&)>  mAvoid;
    float                        mDistance;
    double                       mWalk, mSprint;
    std::int64_t                 mToAvoid = 0;
    Vec3                         mDest{};
    MoveLock                     mLock;
};

} // namespace gf::feature::hostiles
