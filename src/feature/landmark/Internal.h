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
//                    the worldgen stages (Starts; the pieces at their decoration step), placement in
//                    loaded chunks, locate
//   Protection.cpp   ProgressionEvents (break / place / interact / multi-place / entity damage),
//                    protection packets, boxes and particles, the structure protection sync
//   Hints.cpp        hint books per player and the hint monster
//   Commands.cpp     /twilightforest center | info | conquer | genbook | locate | place
//   Module.cpp       install / uninstall, world lifecycle, the server tick, structureSpecialSpawns
//   TestStarts.cpp   the selftests' start removal (ext/landmark/LandmarkTest.h)

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/jrand/JRand.h"
#include "core/landmark/Clearance.h"
#include "core/landmark/Decoration.h"
#include "core/landmark/Placement.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/landmark/Landmark.h"
#include "feature/spawn/Spawn.h"
#include "feature/worldgen/Worldgen.h"

namespace gf::worldgen::ext {
struct DecorationContext;
}

class AABB;
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
    /// KnightStrongholdStructure.KnightStructureStart#getBoundingBox: the start's box ends one
    /// below its generation point (min(maxY, stub y - 1))
    bool topBelowStart     = false;
};

/// The adjustForTerrain override of a structure class: WorldUtil#adjustForTerrain(context, x, z,
/// radiusFromCenterChunk, gridLength) + offset, no clamp (LichTowerStructure 32, 4;
/// HedgeMazeStructure 24, 4; NagaCourtyardStructure 40, 4, + 2).
struct TerrainOverride {
    int radius = 0, gridLength = 0, offset = 0;
};

/// A descriptor with everything TF's structure record holds.
struct Info {
    Descriptor                         d;
    Traits                             traits;
    core::landmark::DecorationConfig   decoration;
    core::landmark::TerrainAdjustment  adjustment = core::landmark::TerrainAdjustment::None;
    /// The structure class's adjustForTerrain override (nullopt: DecorationClearance#adjustForTerrain).
    std::optional<TerrainOverride>     terrainOverride;
    bool                               centerInChunk = true;
    std::string                        type;         ///< the structure type id
    std::string                        biomes;       ///< "#tag" or one biome id
    std::string                        step;         ///< GenerationStep.Decoration name
    std::string                        structureSet; ///< the structure set placing it ("" none)
    std::string                        hintMob;      ///< hint_creature.hint_mob
    std::string                        hintBook;     ///< "lichtower" (twilightforest.book.<name>)
    int                                hintPages = 0;
    /// Where ChunkGenerator#applyBiomeDecoration seeds it (set when it joins the store,
    /// assignSlot): the worldgen's Structure registry slot (worldgen::ext::structureSlot), or for
    /// a key the registry does not hold (the gf_test fixtures) past the end of its step.
    core::landmark::decoration::Slot   slot;
    bool                               inRegistry = false; ///< the worldgen's Structure registry holds it
    Json                               raw;          ///< the record as loaded
};

/// A structure set (TF's worldgen/structure_set).
struct StructureSet {
    std::string                              id;
    core::landmark::placement::Placement     placement;
    std::vector<std::pair<std::string, int>> structures; ///< (structure key, weight)
};

/// Loads every descriptor and structure set (mod data dir), once per process; the lookups below load
/// them on first use too. Entries are only ever added, so their pointers stay valid.
void loadDescriptors();
[[nodiscard]] Info const* info(std::string const& key);
/// Every descriptor key, sorted.
[[nodiscard]] std::vector<std::string> descriptorKeys();
[[nodiscard]] StructureSet const* structureSet(std::string const& id);
[[nodiscard]] std::vector<StructureSet const*> structureSets();
/// Adds a descriptor at run time (selftest fixtures; an existing key is kept, info() pointers stay
/// valid). Not for W1 units.
void registerInfo(Info info);
/// Fills `info.d` and the parsed fields from a TF structure record. `fixedClearance`: the
/// DecorationConfig a Java class hard-codes (CampStructure, FallenTrunkStructure), used when the
/// record has no decoration_clearance; it stays out of the record (Descriptor::extra.gf only).
[[nodiscard]] Info parseRecord(std::string const& key, Json const& record, Json const* fixedClearance = nullptr);
/// "twilightforest:lich_tower" from "lich_tower" or the full id.
[[nodiscard]] std::string normalizeKey(std::string const& id);
/// A resource location's (namespace, path); "minecraft" without a namespace, as ResourceLocation.
[[nodiscard]] inline std::pair<std::string, std::string> splitKey(std::string const& key) {
    auto colon = key.find(':');
    if (colon == std::string::npos) return {"minecraft", key};
    return {key.substr(0, colon), key.substr(colon + 1)};
}

// ---- generators (Starts.cpp) -------------------------------------------------------------------------

/// The generator of `key` (nullptr: none). A shared copy: setGenerator / removeGenerator may run on
/// another thread meanwhile.
[[nodiscard]] std::shared_ptr<Generator const> generatorFor(std::string const& key);
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
    /// The piece's saved tag as read back from the world DB while the record's pieces are not in
    /// memory (a type without a factory yet): written again unchanged, so a later factory still finds
    /// it. Null while the pieces are live (their tag is saved from the piece itself).
    Json        saved;
};

/// A start with its pieces.
struct Record {
    Start                                          start;
    int                                            chunkX = 0, chunkZ = 0; ///< the start chunk
    bool                                           forced = false;         ///< made by forceStart / generateAt
    /// Made by generateAt (/twilightforest place, saplings): Java's /place structure and sapling
    /// growth place the pieces without storing a start, so LandmarkUtil, locate, protection and the
    /// controlled spawns never see it. forceStart's starts (selftests) are full starts.
    bool                                           placed = false;
    std::vector<PieceInfo>                         pieces;
    std::vector<std::shared_ptr<structure::Piece>> livePieces; ///< empty when not loadable / reserved
    std::atomic<bool>                              reviveTried{false}; ///< reviveRecord ran (once per record)
};
using RecordPtr = std::shared_ptr<Record>;

/// A copy of a record with every field (its live pieces shared, not copied: their saves and
/// postProcess calls lock per piece, whichever record reaches them): Record holds an atomic and is not
/// copyable as a whole. Kept next to Record: a field added there is added here.
[[nodiscard]] RecordPtr cloneRecord(Record const& r);

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
/// The world generation of the record store: clearRecords (a world closed or loaded) bumps it, and
/// a start made for an older generation is dropped instead of stored.
[[nodiscard]] std::uint64_t recordWorld();
/// Bumped whenever a record, a generator or a terraformer changes (caches compare it).
[[nodiscard]] std::uint64_t recordsVersion();
void eraseRecord(int dim, int chunkX, int chunkZ, std::string const& key);
/// Marks a record changed (persisted on the next flush).
void markDirty(Record const& record);
/// Writes changed records to the world DB (server thread).
void flushRecords();
/// Reads every record of the world (server thread, world ready).
void loadRecords();
/// Drops the cache (world closed).
void clearRecords();
/// The record as persisted. `stale` (when given) is set when a piece was in a postProcess on another
/// thread and its snapshot from before that postProcess was written: flushRecords writes it again later.
[[nodiscard]] Json recordToJson(Record const& r, bool* stale = nullptr);
[[nodiscard]] RecordPtr recordFromJson(Json const& j);
[[nodiscard]] std::string recordDbKey(int dim, int chunkX, int chunkZ);

/// LandmarkUtil#locateNearestLandmarkStart (locateNearestMatchingLandmark with checkReady): the
/// valid #twilightforest:landmark start in the landmark center chunk nearest to chunk (cx, cz),
/// nothing while that center chunk is not loaded (Level#hasChunk). With `pos`, also an off-grid
/// start made by forceStart (the selftests' stand-in for a grid start; never a /twilightforest place
/// one) whose box contains it. Reserved sites (no generator yet, no pieces) are no valid starts.
/// Server thread (protection, the structure protection sync, /tf info, /tf conquer).
[[nodiscard]] RecordPtr nearestLandmark(int dim, int chunkX, int chunkZ, std::optional<GlobalPos> pos = std::nullopt);
/// LandmarkUtil#locateNearestLandmarkStart(level, structureKey, pos) for a structure key (no ready
/// check on the center chunk, as TF): the valid start of `key` referencing the nearest landmark
/// center chunk whose own start chunk is loaded (level.hasChunk(packedX, packedZ)); besides, only a
/// forceStart start of `key` (selftests) whose box covers the column. Server thread.
[[nodiscard]] RecordPtr nearestLandmarkOf(int dim, std::string const& key, int blockX, int blockZ);
/// Whether chunk (cx, cz) of `dim` is loaded on the server (Level#hasChunk; server thread).
[[nodiscard]] bool chunkLoaded(int dim, int chunkX, int chunkZ);

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
/// starts that begin there (ChunkGenerator#createStructures). Idempotent and never blocking the
/// server thread: while another thread evaluates the chunk, a generation thread waits for it
/// (bounded), but the server thread and a thread inside an evaluation (a layout asking about another
/// chunk) evaluate it themselves; the result is deterministic and the first start stored per key is
/// kept. True when the chunk's starts are known on return (false: this thread is evaluating that
/// very chunk, or the world was closed meanwhile).
bool ensureStartsIn(int dim, int chunkX, int chunkZ);
/// ensureStartsIn for every start chunk that can reach chunk (cx, cz) (chunks another thread is
/// evaluating last).
void ensureStartsAround(int dim, int chunkX, int chunkZ);
/// Whether the structure sets of start chunk (cx, cz) were evaluated (StructureCheck's "known").
[[nodiscard]] bool startsEvaluated(int dim, int chunkX, int chunkZ);
/// Records the server thread (called from the server tick): it never waits for a generation thread.
void noteServerThread();
/// Test seam: the dimension TF's structure sets place in instead of the Twilight Forest (nullopt:
/// the Twilight Forest), so the start evaluation runs while the dimension contract is a stub.
void setStructureDimOverride(std::optional<int> dim);
/// Test seam: marks start chunk (cx, cz) as being evaluated by another thread (busy) or releases it.
void setEvaluationBusy(int dim, int chunkX, int chunkZ, bool busy);
/// Test seam: forgets that start chunk (cx, cz) was evaluated (and the probes and the
/// ensureStartsAround runs that relied on it).
void forgetEvaluation(int dim, int chunkX, int chunkZ);
/// The structure of `set` that starts in start chunk (cx, cz) ("": none), as the Starts stage would
/// make it there (placement, weighted pick, layout and biome check), computed without storing a
/// start: /twilightforest locate's CHUNK_LOAD_NEEDED for a chunk nobody generated. Remembered per
/// world in memory only.
[[nodiscard]] std::string probeStart(int dim, StructureSet const& set, int chunkX, int chunkZ);
/// The registered terrain height provider (ext::setHeightProvider), copied (empty: none registered
/// yet): every height query goes through it; fetch it once for many samples.
[[nodiscard]] std::function<std::optional<int>(int dim, int x, int z, worldgen::Heightmap type)> heightProvider();
/// WorldUtil#adjustForTerrain(context, xInCenterChunk, zInCenterChunk, radiusFromCenterChunk,
/// gridLength) over `provider`'s WORLD_SURFACE_WG heights (nullopt: no provider, or no column).
[[nodiscard]] std::optional<int> adjustForTerrainAroundChunk(std::function<std::optional<int>(int, int, int, worldgen::Heightmap)> const& provider, int dim, int x,
                                                             int z, int radiusFromCenterChunk, int gridLength);
/// Sets info.slot and info.inRegistry (a descriptor joining the store; the store lock not held).
/// ChunkGenerator#applyBiomeDecoration's index counts every Structure registry entry of the step,
/// vanilla's 34 included, in registry order: F5's registry (worldgen::ext::structureSlot) is the
/// one the decoration context seeds with, so a registered key takes its slot. A key it does not
/// hold (the gf_test fixtures) goes past the end of its step, after every registered structure and
/// the `unregisteredBefore` unregistered descriptors of that step already in the store, so no
/// registered structure shares its seed. (core/landmark/Decoration's own table of Minecraft's
/// structures serves the gtests; landmark.decoration compares it with F5's registry.) The slot is
/// kept: F5 loads its data, and so its registry, once per process (features::detail::data();
/// nothing calls reloadData), so it never moves under a loaded descriptor.
void assignSlot(Info& info, int unregisteredBefore);
/// The GenerationStep.Decoration ordinal of a descriptor's step (surface_structures, Structure's
/// default, when unknown).
[[nodiscard]] int stepOf(Info const& info);
/// The step and index applyBiomeDecoration seeds structure `key` with: its descriptor's slot, else
/// the worldgen's Structure registry slot, else past the end of surface_structures.
[[nodiscard]] core::landmark::decoration::Slot decorationSlot(std::string const& key);
/// The decoration steps some descriptor's structure decorates at (bit `step`; TF's: 3, 4 and 10):
/// the landmark.pieces structure step returns at once at the others.
[[nodiscard]] std::uint32_t decorationStepMask();
/// The random the pieces of `key` are placed with in chunk (cx, cz): WorldgenRandom(Xoroshiro)
/// setDecorationSeed(levelSeed, cx << 4, cz << 4), then setFeatureSeed(.., decorationSlot(key)).
[[nodiscard]] std::unique_ptr<core::jrand::WorldgenRandom> decorationRandom(std::int64_t levelSeed, int chunkX, int chunkZ, std::string const& key);
/// The landmark.pieces structure step (worldgen::ext::addStructureStep) for decoration step `d.step`
/// of chunk (cx, cz): the starts of `region`'s dimension reaching the chunk whose structure decorates
/// at that step, structure by structure in registry order, each structure's starts placed with
/// d.structureRandom(its index) (StructureStart#placeInChunk). Returns the number of starts placed.
int decorateStep(worldgen::Region& region, int chunkX, int chunkZ, worldgen::ext::DecorationContext const& d);
/// Creates a start of `key` with its center at block (x, y, z) (y: nullopt = TF's height rule)
/// for start chunk (cx, cz); nullptr when the generator made no pieces.
[[nodiscard]] RecordPtr createStart(Info const& info, int dim, int chunkX, int chunkZ, std::optional<GlobalPos> at, bool forced);
/// Rebuilds the pieces of a start that has none in memory (reserved before its generator, or read
/// back without loadable pieces) from its generator's deterministic layout; once per record.
void reviveRecord(RecordPtr const& record);
/// Places the parts of a record inside the loaded chunks of `region` (commands, saplings,
/// forceStart); returns the number of chunks placed.
int placeLoaded(Record const& record, BlockSource& region);

/// Adds the landmark.starts stage (Stage::Starts) and the landmark.pieces structure step
/// (worldgen::ext::addStructureStep, every decoration step) to the worldgen pipeline (once).
void registerWorldgenStages();

/// Whether an entity bounding box intersects AABB(block (x, y, z)).inflate(inflate): Level
/// #getEntitiesOfClass's test (LandmarkUtil#markStructureConquered's players within 32 blocks).
[[nodiscard]] bool playerBoxReaches(AABB const& box, int x, int y, int z, double inflate);

/// The start's protected piece boxes containing pos (TF: pieces whose box contains pos).
[[nodiscard]] std::vector<BlockBox> pieceBoxesAt(Record const& r, int x, int y, int z);
/// The controlled-spawn list index at a position (EntityEvents#getSpawnListIndexAt; -1: none).
[[nodiscard]] int spawnIndexAt(Record const& r, int x, int y, int z);
/// The pieces of a record with their type ids and tags (landmark::ext::startPieces).
[[nodiscard]] std::vector<gf::landmark::ext::StartPiece> piecesOf(Record const& r);
/// The structure type of TF's hollow hills (HollowHillStructure): their monsters spawn in the dome only.
inline constexpr char kHollowHillType[] = "twilightforest:hollow_hill";

// ---- spawns (Module.cpp) -----------------------------------------------------------------------------

/// EntityEvents#structureSpecialSpawns, the function F6 registers with gf::spawn::onSpecialSpawn:
/// the controlled list of a ControlledSpawns start reaching the position's chunk
/// (ext::potentialSpawns), else the spawn override of a start whose piece ("piece") or box ("full")
/// holds it, as SpawnEntries; nullopt: the biome's list.
[[nodiscard]] std::optional<std::vector<gf::spawn::SpawnEntry>> structureSpecialSpawns(BlockSource& region, BlockPos const& pos, std::string const& category);

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
/// ProgressionEvents#isPieceProtected for piece `index` of a record: Generator::pieceProtected
/// (asked at `at`, a position inside the piece) when the generator has one, else the piece's flag.
[[nodiscard]] bool pieceProtectedAt(Record const& record, std::size_t index, int x, int y, int z);
/// Test seam: protection boxes the server keeps alive (min corners and sizes).
[[nodiscard]] std::vector<BlockBox> liveProtectionBoxes();
/// Ids of the protection box entities alive (tests).
[[nodiscard]] std::size_t protectionBoxEntities();
/// Removes protection box entities the server does not track (a box saved with its chunk and loaded
/// again: TF's ProtectionBox is a client entity that is never saved). Returns how many.
std::size_t sweepStrayProtectionBoxes();
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
/// Spawns the hint mob of `info` at (x, y, z) holding the hint book for `player`, its main hand
/// dropped on death (setDropChance(MAINHAND, PRESERVE_ITEM_DROP_CHANCE_THRESHOLD)).
Actor* spawnHintCarrier(Info const& info, BlockSource& region, float x, float y, float z, Player const* player);
/// The main-hand drop chance of a mob (SlotDropChancesComponent; nullopt: none set).
[[nodiscard]] std::optional<float> mainHandDropChance(Actor& actor);
/// Rewrites a hint book (translatable_book) for `reader`'s language; true when it changed.
bool retranslateBook(ItemStack& book, Player const& reader);
/// Level#getGameTime: the persisted world tick (level.dat's currentTick). Level::getCurrentTick
/// returns that very field, which advances every tick and continues over a restart (landmark.hint_timer
/// and the persist tests check both).
[[nodiscard]] std::uint64_t gameTime();
void installHints();
void uninstallHints();
void resetHints();

// ---- shared helpers ----------------------------------------------------------------------------------

/// "Lich Tower": the structure's name (structure.<ns>.<path>) in the player's language (or the
/// default language without a player); the key itself when it has no translation, as Java shows it.
[[nodiscard]] std::string structureName(std::string const& key, Player const* player);
/// EntityType#getDescription().getString(): an entity's name ("minecraft:zombie" -> "Zombie") in the
/// player's language: TF's names from the mod language (entity.<ns>.<path>), else the game's own
/// (Bedrock's entity.<id>.name: vanilla's and the resource packs'), else the id's path, capitalized.
[[nodiscard]] std::string entityName(std::string const& id, Player const* player);
/// Spawns an entity by id: the spawn contract, or the engine's spawner while that is a stub.
Actor* spawnEntity(BlockSource& region, std::string const& id, float x, float y, float z, std::function<void(Actor&)> init = {});
/// Emits particle `id` at each point for the players near them (the fx contract, or the particle
/// packet while that is a stub).
void emitParticles(int dim, std::string const& id, std::vector<std::array<float, 3>> const& points);
/// Whether a contract stub was hit (core/Stub.h) since start.
[[nodiscard]] bool stubHit(std::string const& name);

// ---- commands (Commands.cpp) -------------------------------------------------------------------------

/// ResourceLocation#read of a command argument: "<ns>:<path>" ("minecraft" without a namespace);
/// nullopt for an invalid one (argument.id.invalid).
[[nodiscard]] std::optional<std::string> parseResourceLocation(std::string const& text);
/// Whether the Structure registry holds `key` (ResourceKeyArgument#getStructure): the worldgen's
/// structures (Minecraft's, TF's, the data packs') and the descriptors.
[[nodiscard]] bool structureRegistered(std::string const& key);

/// Registers the test fixture landmarks "gf_test:landmark_fixture" / "_open" (descriptor +
/// generator) when the gf_test fixture data is deployed (<mod>/data/gf_test: development builds;
/// GF_NO_TEST_PACKS=1 leaves it out). True when they are registered.
bool registerFixture();
/// Test seam: called with the random each fixture piece's postProcess receives and the piece's box,
/// before it places its blocks (nullptr: none; any thread). It may draw from the random.
void setFixtureProbe(std::function<void(worldgen::RandomSource& random, BlockBox const& pieceBox)> probe);
/// Whether a structure key is a gf_test fixture.
[[nodiscard]] inline bool isFixtureKey(std::string const& key) { return key.starts_with("gf_test:"); }

} // namespace gf::feature::landmark
