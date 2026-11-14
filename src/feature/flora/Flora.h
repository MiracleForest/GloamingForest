#pragma once

// U6 flora (unit-internal header): upstream's small plants, berry bushes, critters and jars.
//
//   Util.cpp      block / item / sound helpers shared by the unit's files
//   Bushes.cpp    berry, dark tower and oreberry bushes (TFBushBlock family)
//   Plants.cpp    patches, mayapple, fiddlehead, mushgloom, fallen leaves, roots, torchberries,
//                 liveroots, huge lily pads / water lilies, berries as food, essence berry, the
//                 survival rules of every flora block and the world generation survival sweep
//   Critters.cpp  firefly, cicada, moonworm (CritterBlock): placement, support, squishing,
//                 capture in a mason jar, worn on the head (cicada sound), dispensers
//   Jars.cpp      firefly / cicada / mason jars (lids, release, mason jar storage) and the firefly
//                 particle spawner
//   Features.cpp  upstream's 8 flora feature types
//   Module.cpp    the module and the config section "flora"
//
// Directions are Bedrock FacingIDs: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east.

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "nlohmann/json.hpp"

#include "core/GlobalPos.h"
#include "core/jrand/JRand.h"
#include "feature/common/Interactions.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class ItemStack;
class Level;
class Player;

namespace gf::worldgen {
class Region;
}

namespace gf::feature::flora {

using Json = nlohmann::json;
using core::GlobalPos;
using core::jrand::RandomSource;

/// Config section "flora" (docs/spec/config.md): TFConfig's client cicada switches.
struct FloraConfig {
    int  version              = 1;
    bool silentCicadas        = false; ///< client.silentCicadas
    bool silentCicadasOnHead  = false; ///< client.silentCicadasOnHead
};
[[nodiscard]] FloraConfig const& config();

namespace ids {
inline constexpr char MossPatch[]        = "gloamingforest:moss_patch";
inline constexpr char CloverPatch[]      = "gloamingforest:clover_patch";
inline constexpr char Mayapple[]         = "gloamingforest:mayapple";
inline constexpr char Fiddlehead[]       = "gloamingforest:fiddlehead";
inline constexpr char Mushgloom[]        = "gloamingforest:mushgloom";
inline constexpr char FallenLeaves[]     = "gloamingforest:fallen_leaves";
inline constexpr char Root[]             = "gloamingforest:root";
inline constexpr char LiverootBlock[]    = "gloamingforest:liveroot_block";
inline constexpr char Liveroot[]         = "gloamingforest:liveroot";
inline constexpr char RootStrand[]       = "gloamingforest:root_strand";
inline constexpr char TorchberryPlant[]  = "gloamingforest:torchberry_plant";
inline constexpr char Torchberries[]     = "gloamingforest:torchberries";
inline constexpr char HugeLilyPad[]      = "gloamingforest:huge_lily_pad";
inline constexpr char HugeWaterLily[]    = "gloamingforest:huge_water_lily";
inline constexpr char Firefly[]          = "gloamingforest:firefly";
inline constexpr char Cicada[]           = "gloamingforest:cicada";
inline constexpr char Moonworm[]         = "gloamingforest:moonworm";
inline constexpr char FireflyJar[]       = "gloamingforest:firefly_jar";
inline constexpr char CicadaJar[]        = "gloamingforest:cicada_jar";
inline constexpr char MasonJar[]         = "gloamingforest:mason_jar";
inline constexpr char FireflySpawner[]   = "gloamingforest:firefly_particle_spawner";
inline constexpr char EssenceBerry[]     = "gloamingforest:essence_berry";
inline constexpr char UberousSoil[]      = "gloamingforest:uberous_soil";
} // namespace ids

namespace st {
inline constexpr char Age[]       = "gloamingforest:age";
inline constexpr char Layers[]    = "gloamingforest:layers";
inline constexpr char Facing[]    = "gloamingforest:facing";
inline constexpr char Piece[]     = "gloamingforest:piece";
inline constexpr char Berries[]   = "gloamingforest:has_torchberries";
inline constexpr char Radius[]    = "gloamingforest:particle_radius";
inline constexpr char North[]     = "gloamingforest:north";
inline constexpr char South[]     = "gloamingforest:south";
inline constexpr char West[]      = "gloamingforest:west";
inline constexpr char East[]      = "gloamingforest:east";
} // namespace st

// ---- directions -------------------------------------------------------------------------------------

enum Dir : int { Down = 0, Up = 1, North = 2, South = 3, West = 4, East = 5 };
inline constexpr std::array<int, 6> kDx{0, 0, 0, 0, -1, 1};
inline constexpr std::array<int, 6> kDy{-1, 1, 0, 0, 0, 0};
inline constexpr std::array<int, 6> kDz{0, 0, -1, 1, 0, 0};
[[nodiscard]] constexpr int opposite(int d) { return d ^ 1; }
[[nodiscard]] char const*   dirName(int d);
[[nodiscard]] int           dirFromName(std::string_view name); ///< -1: none
[[nodiscard]] BlockPos      relative(BlockPos const& p, int d, int n = 1);
[[nodiscard]] BlockPos      offset(BlockPos const& p, int dx, int dy, int dz);

// ---- blocks ---------------------------------------------------------------------------------------

[[nodiscard]] Block const* blockAt(BlockSource& region, BlockPos const& pos);
[[nodiscard]] std::string  idAt(BlockSource& region, BlockPos const& pos);
[[nodiscard]] std::string  stateOf(Block const& block, std::string const& name);
[[nodiscard]] int          intState(Block const& block, std::string const& name, int fallback = -1);
[[nodiscard]] bool         boolState(Block const& block, std::string const& name);
/// The block with some states changed (nullptr: unknown state / value).
[[nodiscard]] Block const* withStates(Block const& block, Json const& states);
bool                       setStates(BlockSource& region, BlockPos const& pos, Json const& states);
bool                       place(BlockSource& region, BlockPos const& pos, std::string const& id, Json const& states = Json::object());
/// Java Block.isFaceSturdy(FULL) / (CENTER) of face `face` of the block at `pos`.
[[nodiscard]] bool sturdy(BlockSource& region, BlockPos const& pos, int face);
[[nodiscard]] bool sturdyCenter(BlockSource& region, BlockPos const& pos, int face);
/// Java BlockState#canBeReplaced.
[[nodiscard]] bool replaceable(BlockSource& region, BlockPos const& pos);
[[nodiscard]] bool isAir(BlockSource& region, BlockPos const& pos);
/// Water at `pos` (a water block or a block's water layer): Java getFluidState(pos) is water.
[[nodiscard]] bool hasWater(BlockSource& region, BlockPos const& pos);
/// Any fluid at `pos` (water / lava, liquid layer included).
[[nodiscard]] bool hasFluid(BlockSource& region, BlockPos const& pos);
/// A block tag (Java's, without '#') of the block at `pos`.
[[nodiscard]] bool tagged(BlockSource& region, BlockPos const& pos, std::string const& tag);
[[nodiscard]] bool tagged(Block const& block, std::string const& tag);
/// Java Level#getRawBrightness(pos, 0): max(sky light, block light).
[[nodiscard]] int rawBrightness(BlockSource& region, BlockPos const& pos);
[[nodiscard]] int blockLight(BlockSource& region, BlockPos const& pos);
/// Java Level.destroyBlock(pos, drop): drops (the loot contract's table) and the break effect.
void destroyBlock(BlockSource& region, BlockPos const& pos, bool drops);
/// Removes the block, leaving its water (Java's removeBlock puts the fluid back).
void removeBlock(BlockSource& region, BlockPos const& pos);

// ---- items and players --------------------------------------------------------------------------------

[[nodiscard]] std::optional<ItemStack> makeItem(std::string_view itemId, int count = 1);
/// Block.popResource: the stack as an item entity at the block centre.
void popResource(BlockSource& region, BlockPos const& pos, ItemStack const& stack);
/// An item entity at an exact position (Java's new ItemEntity(level, x, y, z, stack)).
void spawnItemAt(BlockSource& region, float x, float y, float z, ItemStack const& stack);
/// ItemHandlerHelper.giveItemToPlayer: into the inventory, the rest at the player's feet.
void give(Player& player, ItemStack const& stack);
/// One of the held stack used (not in creative).
void consumeHeld(Player& player, ItemStack const& contextItem, int count = 1);
/// Bedrock repeats a use-on while the button is held and simulated players report every use as a
/// repeat: true when `player` used `pos` less than 5 ticks ago, so a click acts once.
[[nodiscard]] bool repeatedUse(Player& player, GlobalPos const& pos, bool firstEvent);
/// The action bar message of `player`.
void actionBar(Player& player, std::string const& text);

// ---- effects ---------------------------------------------------------------------------------------

/// Plays an upstream / vanilla sound event at the block centre (gf::fx) for the players near it.
void sound(BlockSource& region, BlockPos const& pos, std::string const& event, float volume = 1.f, float pitch = 1.f);
/// A Bedrock vanilla sound by name at the block centre (PlaySoundPacket).
void vanillaSound(BlockSource& region, BlockPos const& pos, std::string const& name, float volume = 1.f, float pitch = 1.f);
/// A Bedrock particle effect by name (vanilla or the RP's) at a world position.
void particleAt(BlockSource& region, float x, float y, float z, std::string const& effect);
/// The block's break effect (Java levelEvent 2001).
void breakEffect(BlockSource& region, BlockPos const& pos, Block const& block);
/// Bone meal used on a block (Java levelEvent 1505): particles and sound.
void boneMealEffect(BlockSource& region, BlockPos const& pos);
/// The random source of interactions (Java's level.random): one per thread.
[[nodiscard]] RandomSource& levelRandom();
/// The server ticks since the module started.
[[nodiscard]] std::int64_t now();
/// Runs `fn` after `ticks` server ticks (Java scheduleTick).
void schedule(int ticks, std::function<void()> fn);

// ---- survival (canSurvive of every flora block) -----------------------------------------------------

/// Java BlockState#canSurvive of a flora block (or a block U6 places by state: trollvidr / trollber
/// hang like roots) at `pos`; nullopt for blocks without a rule here.
[[nodiscard]] std::optional<bool> survives(BlockSource& region, BlockPos const& pos, Block const& block);
/// The same for a world generation region (features); nullopt for blocks without a rule here.
[[nodiscard]] std::optional<bool> survivesIn(worldgen::Region& region, BlockPos const& pos, Block const& block);

// ---- parts -----------------------------------------------------------------------------------------

/// The fallen leaves item used on a block that did not take the click (TFItems FALLEN_LEAVES useOn / use).
[[nodiscard]] interactions::Result useFallenLeavesOn(interactions::UseOnContext const& c);

void installBushes();
void installPlants();
void installCritters();
void installJars();
void registerFeatures();
void tick();
/// Per server tick: the cicadas worn on heads.
void tickCritters(Level& level);

/// Bushes (selftests).
namespace bushes {
enum class Kind { Overworld, DarkTower, Oreberry, Essence };
[[nodiscard]] bool isBush(std::string const& blockId);
[[nodiscard]] Kind kindOf(std::string const& blockId);
/// TFBushBlock#randomTick (+ BerryBushBlock / dark tower / oreberry overrides) with `random`.
void randomTick(BlockSource& region, BlockPos const& pos, RandomSource& random);
/// The berry loot table of a bush ("gloamingforest:blocks/raspberry_bush_berries").
[[nodiscard]] std::string berryTable(std::string const& blockId);
[[nodiscard]] bool canGrowAt(BlockSource& region, BlockPos const& pos, Block const& block);
/// TFBushBlock#canSurvive (+ OreBerryBlock's light limit).
[[nodiscard]] bool canSurvive(BlockSource& region, BlockPos const& pos, Block const& block);
/// BerryBushBlock#performBonemeal.
void performBonemeal(BlockSource& region, BlockPos const& pos, RandomSource& random);
} // namespace bushes

/// Jars (selftests).
namespace jars {
/// The lid item of the jar at `pos` (its default when none was stored).
[[nodiscard]] std::string lidAt(BlockSource& region, BlockPos const& pos);
/// JarBlockEntity.REGISTERED_LOG_LIDS (pumpkin only during Halloween week).
[[nodiscard]] bool isLid(std::string const& itemId);
[[nodiscard]] std::string defaultLid(std::string const& jarId);
/// The mason jar's stored stack (empty when none).
[[nodiscard]] ItemStack contents(BlockSource& region, BlockPos const& pos);
/// Comparator output of a mason jar (lerpDiscrete(count / max stack, 0, 15)).
[[nodiscard]] int analog(ItemStack const& stored);
} // namespace jars

/// Critters (selftests).
namespace critters {
[[nodiscard]] bool isCritter(std::string const& blockId);
/// CritterBlock.canSurvive(level, pos, facing).
[[nodiscard]] bool canSurvive(BlockSource& region, BlockPos const& pos, int facing);
/// CritterBlock#getStateForPlacement: the facing for a click on `face` (nullopt: no support).
[[nodiscard]] std::optional<int> placementFacing(BlockSource& region, BlockPos const& pos, int face, Actor const* placer);
/// CritterBlock#entityInside for a projectile / falling block (true: squished).
bool squish(BlockSource& region, BlockPos const& pos, Actor* projectile, Player* owner);
/// Squished bugs so far (selftests).
[[nodiscard]] std::uint64_t squished();
} // namespace critters

} // namespace gf::feature::flora
