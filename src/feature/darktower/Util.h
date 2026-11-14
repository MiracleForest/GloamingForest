#pragma once

// U26 (darktower): world helpers shared by the unit's devices, mobs and structure (server thread).
// Rule R16: nothing here keeps Actor* / Player* / Block* across ticks.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/level/BlockPos.h"

#include "core/GlobalPos.h"
#include "core/darktower/Devices.h"
#include "core/jrand/JRand.h"

class AABB;
class Actor;
class Block;
class BlockSource;
class Level;
class Mob;
class Player;

namespace gf::feature::darktower {

using Json = nlohmann::json;
using core::GlobalPos;

inline constexpr char kNs[] = "gloamingforest:";

// ---- ids -------------------------------------------------------------------------------------------
namespace id {
inline constexpr char ReappearingBlock[]  = "gloamingforest:reappearing_block";
inline constexpr char VanishingBlock[]    = "gloamingforest:vanishing_block";
inline constexpr char UnbreakableVanish[] = "gloamingforest:unbreakable_vanishing_block";
inline constexpr char LockedVanish[]      = "gloamingforest:locked_vanishing_block";
inline constexpr char Builder[]           = "gloamingforest:carminite_builder";
inline constexpr char BuiltBlock[]        = "gloamingforest:built_block";
inline constexpr char Antibuilder[]       = "gloamingforest:antibuilder";
inline constexpr char Antibuilt[]         = "gloamingforest:antibuilt_block";
inline constexpr char Reactor[]           = "gloamingforest:carminite_reactor";
inline constexpr char ReactorDebris[]     = "gloamingforest:reactor_debris";
inline constexpr char FakeGold[]          = "gloamingforest:fake_gold";
inline constexpr char FakeDiamond[]       = "gloamingforest:fake_diamond";
inline constexpr char Slider[]            = "gloamingforest:slider";
inline constexpr char GhastTrap[]         = "gloamingforest:ghast_trap";
inline constexpr char InfestedTowerwood[] = "gloamingforest:infested_towerwood";
inline constexpr char Towerwood[]         = "gloamingforest:towerwood";
inline constexpr char TowerKey[]          = "gloamingforest:tower_key";
inline constexpr char UrGhast[]           = "gloamingforest:ur_ghast";
inline constexpr char Ghastguard[]        = "gloamingforest:carminite_ghastguard";
inline constexpr char Ghastling[]         = "gloamingforest:carminite_ghastling";
inline constexpr char Golem[]             = "gloamingforest:carminite_golem";
inline constexpr char Broodling[]         = "gloamingforest:carminite_broodling";
inline constexpr char Borer[]             = "gloamingforest:towerwood_borer";
inline constexpr char SlideEntity[]       = "gloamingforest:slider";
} // namespace id

// ---- level ---------------------------------------------------------------------------------------
[[nodiscard]] Level*        level();
[[nodiscard]] std::uint64_t now();
/// The unit's level random (Java's level.getRandom() for the devices).
[[nodiscard]] core::jrand::RandomSource& random();
[[nodiscard]] int dimOf(BlockSource& region);

// ---- blocks --------------------------------------------------------------------------------------
[[nodiscard]] std::string idAt(BlockSource& region, BlockPos const& pos);
[[nodiscard]] bool        boolState(Block const& block, char const* state);
[[nodiscard]] bool        boolStateAt(BlockSource& region, BlockPos const& pos, char const* state);
[[nodiscard]] std::string textState(Block const& block, char const* state);
/// Sets `blockId` with `states` (Java's setBlockAndUpdate).
bool set(BlockSource& region, BlockPos const& pos, std::string const& blockId, Json const& states = Json::object());
/// Level#removeBlock (no drops, no break effects).
void remove(BlockSource& region, BlockPos const& pos);
/// Whether the block change being dispatched now (blockevents::onRemoved) comes from set() /
/// remove() above: Java's setBlock / removeBlock, which drop nothing (no spawnAfterBreak).
[[nodiscard]] bool quietEdit(BlockSource& region, BlockPos const& pos);
/// Level#destroyBlock(pos, drops): break effects, drops when asked.
void destroy(BlockSource& region, BlockPos const& pos, bool drops);
/// Level event 2001 (break particles and sound) of `block` at `pos`.
void breakEffect(BlockSource& region, BlockPos const& pos, Block const& block);
[[nodiscard]] bool isAir(BlockSource& region, BlockPos const& pos);
[[nodiscard]] bool blocksMotion(BlockSource& region, BlockPos const& pos);
/// The block has a non-empty collision shape (Java: !getCollisionShape(level, pos).isEmpty()).
[[nodiscard]] bool hasCollision(BlockSource& region, BlockPos const& pos);
[[nodiscard]] bool unbreakable(Block const& block);
[[nodiscard]] bool loaded(BlockSource& region, BlockPos const& pos, int radius = 0);
/// Java's hasNeighborSignal.
[[nodiscard]] bool powered(BlockSource& region, BlockPos const& pos);
/// The mobGriefing / doTileDrops game rules (true without a level).
[[nodiscard]] bool mobGriefing();
[[nodiscard]] bool doTileDrops();
[[nodiscard]] BlockPos offset(BlockPos const& p, core::darktower::Facing f);
[[nodiscard]] Vec3     center(BlockPos const& p);

// ---- effects ---------------------------------------------------------------------------------------
void sound(BlockSource& region, BlockPos const& pos, char const* event, float volume, float pitch);
void soundAt(BlockSource& region, Vec3 const& pos, char const* event, float volume, float pitch);
void particle(BlockSource& region, char const* id, Vec3 const& pos, Vec3 const& velocity = {}, int count = 1, Json const& params = {});
/// DustParticleOptions.REDSTONE at a position.
void redstoneDust(BlockSource& region, Vec3 const& pos);
/// RedstoneOreBlock#spawnParticles (upstream's sparkle) around an active device.
void sparkle(BlockSource& region, BlockPos const& pos);
/// The chance that ClientLevel#animateTick picks the block at `pos` in one tick for the nearest
/// player (667 random positions within 16 blocks, 667 within 32: about 2.1 % within 16, 0.24 %
/// within 32, 0 beyond). Block animateTick effects (sparkles) are rolled with it.
[[nodiscard]] double animateTickChance(BlockSource& region, BlockPos const& pos);
/// Effects only the vanilla clients get (the companions draw them themselves, src/client/darktower):
/// a resource pack particle effect / redstone dust (Bedrock's legacy RedDust) for the non-companion
/// players within 32 blocks.
void vanillaParticle(BlockSource& region, char const* effect, Vec3 const& pos);
void vanillaRedDust(BlockSource& region, Vec3 const& pos);
/// The block states of `block` as JSON (booleans, integers and strings).
[[nodiscard]] Json blockStates(Block const& block);

// ---- actors ----------------------------------------------------------------------------------------
[[nodiscard]] std::int64_t uidOf(Actor const& actor);
[[nodiscard]] Actor*       actorById(std::int64_t uid);
[[nodiscard]] Mob*         asMob(Actor& actor);
[[nodiscard]] std::string  typeOf(Actor const& actor);
/// Players of a dimension within `radius` (alive), nearest first.
[[nodiscard]] std::vector<Player*> playersNear(int dim, Vec3 const& c, double radius, bool spectators = false);
/// Level#getNearestPlayer(x, y, z, range, false).
[[nodiscard]] Player* nearestPlayer(int dim, Vec3 const& c, double range);
/// Actors (players included) whose box intersects `box`.
[[nodiscard]] std::vector<Actor*> actorsIn(BlockSource& region, AABB const& box);
void setMotion(Actor& actor, Vec3 const& motion);
void push(Actor& actor, Vec3 const& delta);
/// Spawns a mob by id (Spawner#spawnMob); nullptr on failure.
Mob* spawnMob(BlockSource& region, std::string const& entityId, Vec3 const& at, float yaw = 0.0f);
/// A visual-only lightning bolt (LightningBolt#setVisualOnly: no fire, no damage).
void visualLightning(BlockSource& region, Vec3 const& at);
/// A visual-only bolt at t (client-only actor for the players near, plus the thunder).
void visualLightning(Actor& source, Vec3 const& at);
/// Removes the client-only bolts after their second (runtime tick).
void tickVisualLightning();
/// Mob#serverAiStep: the goal and target selectors run in full on the first two ticks and then every
/// other tick ((tickCount + id) % 2 == 0; the phase is per entity here); in between only goals that
/// override requiresUpdateEveryTick tick. The argument is the entity's tick count, starting at 1.
[[nodiscard]] inline bool goalTick(int tickCount) { return tickCount <= 1 || tickCount % 2 == 0; }
[[nodiscard]] int deathTime(Actor const& actor);
[[nodiscard]] bool dying(Actor const& actor);

} // namespace gf::feature::darktower
