// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U16 (fauna): helpers shared by the unit's files (Common.cpp) and the install entry points.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mc/deps/core/math/Vec3.h"

#include "core/fauna/Fauna.h"
#include "core/jrand/JRand.h"

class Actor;
class BlockPos;
class BlockSource;
class Mob;
class Player;

namespace gf::feature::fauna {

inline constexpr char kBoar[]          = "twilightforest:boar";
inline constexpr char kBighorn[]       = "twilightforest:bighorn_sheep";
inline constexpr char kDeer[]          = "twilightforest:deer";
inline constexpr char kPenguin[]       = "twilightforest:penguin";
inline constexpr char kTinyBird[]      = "twilightforest:tiny_bird";
inline constexpr char kRaven[]         = "twilightforest:raven";
inline constexpr char kSquirrel[]      = "twilightforest:squirrel";
inline constexpr char kDwarfRabbit[]   = "twilightforest:dwarf_rabbit";
inline constexpr char kKobold[]        = "twilightforest:kobold";
inline constexpr char kMosquitoSwarm[] = "twilightforest:mosquito_swarm";

/// The ten mob ids of the unit.
[[nodiscard]] std::vector<std::string> const& mobIds();

/// The unit's random source for TF's gameplay draws (server thread).
[[nodiscard]] core::jrand::RandomSource& rng();
/// The difficulty of the level (0 peaceful .. 3 hard).
[[nodiscard]] int difficulty();
/// The actor's block position.
[[nodiscard]] BlockPos blockPosOf(Actor const& actor);
/// The Java biome id at the actor (TF's biomes in the Twilight Forest, Bedrock's names elsewhere).
[[nodiscard]] std::string biomeIdAt(Actor& actor);
/// Runs a BP event of the actor's entity definition.
bool fireEvent(Actor& actor, std::string const& event);
/// Entity#getEyeHeight of Java (0.85 x height of the type, golden eyeHeight) through the offsets
/// component (Bedrock's eye is 0.9 x height); every tick (the engine may recompute it).
void applyJavaEyeHeight(Actor& actor);
/// Whether F8's state of `actor` holds native goals (none after F8 swept the state of an unloaded actor).
[[nodiscard]] bool nativeGoalsPresent(Actor& actor);
[[nodiscard]] bool nativeTargetGoalsPresent(Actor& actor);
/// A fresh actor (not loaded from a save) whose finalizeSpawn-like set-up did not run yet: marks it.
[[nodiscard]] bool firstSetUp(Actor& actor);

// ---- colours and variants ----------------------------------------------------------------------------------

/// The actor's minecraft:color (Java's DyeColor id: the same index).
[[nodiscard]] int colorOf(Actor const& actor);
/// Sets the colour through the BP's set_color_<n> event.
void setColor(Actor& actor, int color);
/// minecraft:variant of the actor.
[[nodiscard]] int variantOf(Actor const& actor);
/// Sets the variant through the BP's set_variant_<n> and keeps the registry id in the save
/// (Java's "variant" string).
void setVariant(Actor& actor, int index);
/// The registry entries (TinyBirdVariant / DwarfRabbitVariant) in Bedrock index order.
[[nodiscard]] std::vector<core::fauna::Variant> const& variantsOf(std::string const& entityId);
/// The default variant index (tiny bird: red, dwarf rabbit: brown).
[[nodiscard]] int defaultVariant(std::string const& entityId);
/// Restores the saved variant string (readAdditionalSaveData: unknown ids keep the current one).
void restoreVariant(Actor& actor);
/// Bighorn shearing state (minecraft:is_sheared).
[[nodiscard]] bool isSheared(Actor const& actor);
/// The two nearest adult mobs of the same type within 4 blocks (the parents of a bred baby).
[[nodiscard]] std::vector<Actor*> nearbyParents(Actor& baby);

/// finalizeSpawn of the variant mobs (tiny bird, dwarf rabbit; a dwarf rabbit kit inherits), or the
/// saved variant of a loaded one.
void variantSpawn(Actor& self);
/// Bird#aiStep: don't fall as fast.
void birdSlowFall(Actor& self);

// ---- install ------------------------------------------------------------------------------------------------

void installAnimals();
void installBirds();
void installMonsters();
void installWorld();
void uninstallAnimals();
void uninstallMonsters();

// ---- behaviour entry points (selftests) ---------------------------------------------------------------------

/// Bighorn: Sheep#shear with the drops (1..3 wool of the colour); false when not ready.
bool shearBighorn(Actor& sheep, Player* player);
/// Bighorn: DyeItem#interactLivingEntity's colour change (true when it changed).
bool dyeBighorn(Actor& sheep, int color);
/// The bighorn's loot table (Bighorn#getDefaultLootTable).
[[nodiscard]] std::string bighornTable(Actor& sheep);
/// Deer#mobInteract / usePlayerItem with shika senbei: heals 4 and plays the eat sound.
void deerEatSenbei(Actor& deer);
/// FlyingBird: landed flag (DATA_BIRDFLAGS bit 0) and the take-off / landing.
[[nodiscard]] bool birdLanded(Actor& bird);
void               setBirdLanded(Actor& bird, bool landed, bool sound);
/// Kobold: the bread munching timers (EatingTimeLeft, TimeSinceBreadLastEaten).
struct KoboldTimers {
    int eatingTime       = 0;
    int lastEatenBread   = 0;
};
[[nodiscard]] KoboldTimers koboldTimers(Actor& kobold);
void                       setKoboldTimers(Actor& kobold, KoboldTimers timers);
/// Kobold#pickUpItem of a bread stack lying on the ground (true when it took it).
bool koboldPickUp(Mob& kobold, Actor& itemActor);
/// The parrot imitation sound of an entity type (the neoforge:parrot_imitations data map), "" if none.
[[nodiscard]] std::string parrotImitation(std::string const& entityId);
/// Penguin#canSpawn: the block below `pos` is in #twilightforest:penguins_spawnable_on.
[[nodiscard]] bool penguinCanSpawnOn(BlockSource& region, BlockPos const& pos);
/// The prey target goals a vanilla predator type gets (MiscEvents#addPrey): count.
[[nodiscard]] int preyGoalCount(std::string const& predatorId);

} // namespace gf::feature::fauna
