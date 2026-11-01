// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/util/LandmarkStructure.java
//   src/main/java/twilightforest/world/components/structures/util/ProgressionStructure.java
//   src/main/java/twilightforest/world/components/structures/util/ConquerableStructure.java
//   src/main/java/twilightforest/world/components/structures/start/TFStructureStart.java
//   src/main/java/twilightforest/util/landmarks/LandmarkUtil.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// Internals of the landmark unit (F6). Not a contract: only src/feature/landmark/**,
// src/client/landmark/** and the unit's own extension (src/feature/ext/landmark/) include this.
//
// Pieces of the unit:
//   Descriptors.cpp  TF's structure records (assets/data/landmarks/*.json) and structure sets
//   Starts.cpp       the persistent start store (gf:landmark:start:<dim>:<cx>:<cz>), start creation,
//                    the worldgen stages (Starts, Pieces), placement in loaded chunks, locate
//   Protection.cpp   ProgressionEvents (break / place / interact / multi-place / entity damage),
//                    protection packets, boxes and particles, the structure protection sync
//   Hints.cpp        hint books per player and the hint monster
//   Commands.cpp     /twilightforest center | info | conquer | genbook | locate | place
//   Module.cpp       install / uninstall, world lifecycle, the server tick

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/landmark/Clearance.h"
#include "core/landmark/Placement.h"
#include "feature/landmark/Landmark.h"

class Actor;
class BlockPos;
class BlockSource;
class ItemStack;
class Player;

namespace gf::feature::landmark {

using gf::landmark::BlockBox;
using gf::landmark::ChunkPos;
using gf::landmark::Descriptor;
using gf::landmark::Generator;
using gf::landmark::GlobalPos;
using gf::landmark::Start;
using Json = nlohmann::json;

inline constexpr char kUnit[] = "landmark";

/// config/landmark.json (rule R13). TF has no landmark config keys (docs/spec/config.md); these
/// are GloamingForest's own switches for the Bedrock rendering of the protection feedback.
struct LandmarkConfig {
    int  version = 1;
    /// Show the protected boxes of a refused interaction as protection box entities to every player
    /// near it (TF draws them on each client within 64 blocks).
    bool protectionBoxes = true;
    /// Emit TF's 20 protection particles around the refused block.
    bool protectionParticles = true;
};
/// TF's sea level of the Twilight Forest (TFDimensionData.SEALEVEL).
inline constexpr int kTwilightSeaLevel = 0;
/// Bedrock's overworld sea level (other dimensions).
inline constexpr int kDefaultSeaLevel = 62;
/// Vanilla's structure reference range (chunks): a start reaches at most this far.
inline constexpr int kReferenceRange = 8;

// ---- descriptors (Descriptors.cpp) -------------------------------------------------------------------

/// The Java class family of a structure (which TF interfaces it implements).
struct Traits {
    bool landmark          = false; ///< LandmarkStructure (decoration clearance, grid center)
    bool landmarkTag       = false; ///< #twilightforest:landmark (LandmarkUtil#locateNearestLandmarkStart)
    bool conquerable       = false; ///< ConquerableStructure (TFStructureStart with the conquered flag)
    bool progression       = false; ///< ProgressionStructure (advancement lock + hints)
    bool controlledSpawns  = false; ///< ControlledSpawns (controlled_spawns lists)
    bool customDensity     = false; ///< CustomDensitySource (terraformer)
};

/// A descriptor with everything TF's structure record holds.
struct Info {
    Descriptor                         d;
    Traits                             traits;
    core::landmark::DecorationConfig   decoration;
    core::landmark::TerrainAdjustment  adjustment = core::landmark::TerrainAdjustment::None;
    bool                               centerInChunk = true;
    std::string                        type;         ///< the structure type id
    std::string                        biomes;       ///< "#tag" or one biome id
    std::string                        step;         ///< GenerationStep.Decoration name
    std::string                        structureSet; ///< the structure set placing it ("" none)
    std::string                        hintMob;      ///< hint_creature.hint_mob
    std::string                        hintBook;     ///< "lichtower" (twilightforest.book.<name>)
    int                                hintPages = 0;
    Json                               raw;          ///< the record as loaded
};

/// A structure set (TF's worldgen/structure_set).
struct StructureSet {
    std::string                              id;
    core::landmark::placement::Placement     placement;
    std::vector<std::pair<std::string, int>> structures; ///< (structure key, weight)
};

/// Loads every descriptor and structure set (mod data dir), replacing the current ones.
void loadDescriptors();
[[nodiscard]] Info const* info(std::string const& key);
/// Every descriptor key, sorted.
[[nodiscard]] std::vector<std::string> descriptorKeys();
[[nodiscard]] StructureSet const* structureSet(std::string const& id);
[[nodiscard]] std::vector<StructureSet const*> structureSets();
/// Adds a descriptor at run time (selftest fixtures; an existing key is kept, info() pointers stay
/// valid). Not for W1 units.
void registerInfo(Info info);
/// Fills `info.d` and the parsed fields from a TF structure record (+ its structure set).
[[nodiscard]] Info parseRecord(std::string const& key, Json const& record);
/// "twilightforest:lich_tower" from "lich_tower" or the full id.
[[nodiscard]] std::string normalizeKey(std::string const& id);

// ---- generators (Starts.cpp) -------------------------------------------------------------------------

[[nodiscard]] Generator const* generatorFor(std::string const& key);
void setGenerator(std::string const& key, Generator generator);
void removeGenerator(std::string const& key);
/// Every terraformer of a structure (Generator::terraform, then addTerraformer's), immutable.
using TerraformerList = std::shared_ptr<std::vector<std::function<double(Start const&, int, int, int)>> const>;
/// The terraformers of `key` (nullptr: none).
[[nodiscard]] TerraformerList terraformersFor(std::string const& key);
void addTerraformer(std::string const& key, std::function<double(Start const&, int, int, int)> fn);

// ---- starts (Starts.cpp) -----------------------------------------------------------------------------

/// What protection, spawning and clearance need of a piece (kept even when the piece itself cannot
/// be loaded back).
struct PieceInfo {
    BlockBox    box;
    bool        protectedPiece        = true; ///< ProgressionPiece#isComponentProtected
    int         spawnIndex            = -1;   ///< SpawnIndexProvider#getSpawnIndex (-1: none)
    bool        utilityAllowsFeatures = false;
    std::string type;                         ///< piece type id
};

/// A start with its pieces.
struct Record {
    Start                                          start;
    int                                            chunkX = 0, chunkZ = 0; ///< the start chunk
    bool                                           forced = false;         ///< made by forceStart / place
    std::vector<PieceInfo>                         pieces;
    std::vector<std::shared_ptr<structure::Piece>> livePieces; ///< empty when not loadable / reserved
    std::atomic<bool>                              reviveTried{false}; ///< reviveRecord ran (once per record)
};
using RecordPtr = std::shared_ptr<Record>;

/// The record of `key` started in chunk (cx, cz) of `dim`.
[[nodiscard]] RecordPtr findRecord(int dim, int chunkX, int chunkZ, std::string const& key);
/// Records whose start chunk is (cx, cz).
[[nodiscard]] std::vector<RecordPtr> recordsStartedIn(int dim, int chunkX, int chunkZ);
/// Records whose box intersects chunk (cx, cz).
[[nodiscard]] std::vector<RecordPtr> recordsIntersecting(int dim, int chunkX, int chunkZ);
/// Every record of `dim` (commands, tests).
[[nodiscard]] std::vector<RecordPtr> allRecords(int dim);
/// Stores a record (replacing one with the same start chunk and key); persisted on the next flush.
void putRecord(RecordPtr record);
void eraseRecord(int dim, int chunkX, int chunkZ, std::string const& key);
/// Marks a record changed (persisted on the next flush).
void markDirty(Record const& record);
/// Writes changed records to the world DB (server thread).
void flushRecords();
/// Reads every record of the world (server thread, world ready).
void loadRecords();
/// Drops the cache (world closed).
void clearRecords();
[[nodiscard]] Json recordToJson(Record const& r);
[[nodiscard]] RecordPtr recordFromJson(Json const& j);
[[nodiscard]] std::string recordDbKey(int dim, int chunkX, int chunkZ);

/// LandmarkUtil#locateNearestLandmarkStart: the #twilightforest:landmark start in the landmark
/// center chunk nearest to chunk (cx, cz), plus off-grid starts (forceStart / place) whose box
/// contains `pos` when given.
[[nodiscard]] RecordPtr nearestLandmark(int dim, int chunkX, int chunkZ, std::optional<GlobalPos> pos = std::nullopt);
/// LandmarkUtil#locateNearestLandmarkStart(level, structureKey, pos) for a structure key.
[[nodiscard]] RecordPtr nearestLandmarkOf(int dim, std::string const& key, int blockX, int blockZ);

/// The world seed the grid and placements use (the overworld seed, as TF).
[[nodiscard]] std::int64_t levelSeed();
/// The Twilight Forest's dimension id (nullopt: not registered).
[[nodiscard]] std::optional<int> twilightDim();
[[nodiscard]] int seaLevelOf(int dim);
/// The biome at a block of `dim` ("" unknown).
[[nodiscard]] std::string biomeAt(int dim, int x, int y, int z);
/// Whether biome `biomeId` matches a record's "biomes" ("#tag" or id).
[[nodiscard]] bool biomeMatches(std::string const& biomes, std::string const& biomeId);

/// Evaluates the structure sets for start chunk (cx, cz) of the Twilight Forest and stores the
/// starts that begin there (ChunkGenerator#createStructures). Idempotent.
void ensureStartsIn(int dim, int chunkX, int chunkZ);
/// ensureStartsIn for every start chunk that can reach chunk (cx, cz).
void ensureStartsAround(int dim, int chunkX, int chunkZ);
/// Creates a start of `key` with its center at block (x, y, z) (y: nullopt = TF's height rule)
/// for start chunk (cx, cz); nullptr when the generator made no pieces.
[[nodiscard]] RecordPtr createStart(Info const& info, int dim, int chunkX, int chunkZ, std::optional<GlobalPos> at, bool forced);
/// Rebuilds the pieces of a start that has none in memory (reserved before its generator, or read
/// back without loadable pieces) from its generator's deterministic layout; once per record.
void reviveRecord(RecordPtr const& record);
/// Places the parts of a record inside the loaded chunks of `region` (commands, saplings,
/// forceStart); returns the number of chunks placed.
int placeLoaded(Record const& record, BlockSource& region);

/// Adds the Starts and Pieces stages to the worldgen pipeline (once).
void registerWorldgenStages();

/// The start's protected piece boxes containing pos (TF: pieces whose box contains pos).
[[nodiscard]] std::vector<BlockBox> pieceBoxesAt(Record const& r, int x, int y, int z);
/// The controlled-spawn list index at a position (EntityEvents#getSpawnListIndexAt; -1: none).
[[nodiscard]] int spawnIndexAt(Record const& r, int x, int y, int z);

// ---- protection (Protection.cpp) ---------------------------------------------------------------------

/// The tfEnforcedProgression gamerule (TF's default, true, while no gamerule store is available).
[[nodiscard]] bool enforced();
/// PlayerHelper#playerHasRequiredAdvancements for a descriptor's lock.
[[nodiscard]] bool hasRequiredAdvancements(Player const& player, Info const& info);
/// ProgressionEvents#isAreaProtected; with `effects`, also shows the protection (packets, boxes,
/// particles) and tries a hint monster, as TF does.
bool isAreaProtected(Player& player, int dim, int x, int y, int z, bool effects);
/// The same query for a const player (no effects).
[[nodiscard]] bool isAreaProtectedQuery(Player const& player, int dim, int x, int y, int z);
/// ProgressionEvents#sendAreaProtectionPacket: companions within 64 blocks get
/// landmark.area_protection; every player near gets the protection boxes and particles.
void showAreaProtection(int dim, int x, int y, int z, std::vector<BlockBox> const& boxes);
/// The server tick part of ProgressionEvents (structure protection sync, box lifetimes).
void protectionTick(std::uint64_t tick);
void installProtection();
void uninstallProtection();
void resetProtection();
/// Test seam: forces the enforcement result (nullopt: the gamerule).
void setEnforcedOverride(std::optional<bool> value);
/// Test seam: protection boxes the server keeps alive (min corners and sizes).
[[nodiscard]] std::vector<BlockBox> liveProtectionBoxes();
/// Ids of the protection box entities alive (tests).
[[nodiscard]] std::size_t protectionBoxEntities();
/// The protection box entities with their positions (test diagnostics).
[[nodiscard]] std::string describeProtectionBoxes();

// ---- hints (Hints.cpp) -------------------------------------------------------------------------------

/// StructureHints#createHintBook for `player` (texts in the player's language). Empty when the
/// structure has no hint (nullopt).
[[nodiscard]] std::optional<ItemStack> createHintBook(Info const& info, Player const* player);
/// StructureHints.addBookInformationStatic(book, name, pages) for `player`.
[[nodiscard]] std::optional<ItemStack> makeBook(std::string const& name, int pages, Player const* player);
/// ProgressionStructure#trySpawnHintMonster (rate limited per structure, 20 attempts).
bool trySpawnHintMonster(Info const& info, Player& player, int x, int y, int z);
void resetHints();

// ---- shared helpers ----------------------------------------------------------------------------------

/// "Lich Tower": the structure's name (structure.<ns>.<path>) in the player's language (or the
/// default language without a player).
[[nodiscard]] std::string structureName(std::string const& key, Player const* player);
/// Spawns an entity by id: the spawn contract, or the engine's spawner while that is a stub.
Actor* spawnEntity(BlockSource& region, std::string const& id, float x, float y, float z, std::function<void(Actor&)> init = {});
/// Emits particle `id` at each point for the players near them (the fx contract, or the particle
/// packet while that is a stub).
void emitParticles(int dim, std::string const& id, std::vector<std::array<float, 3>> const& points);
/// Whether a contract stub was hit (core/Stub.h) since start.
[[nodiscard]] bool stubHit(std::string const& name);

/// Registers the test fixture landmark "gf_test:landmark_fixture" (descriptor + generator) when the
/// gf_test fixture pack is present (development builds).
void registerFixture();

} // namespace gf::feature::landmark
