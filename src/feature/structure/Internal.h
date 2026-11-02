// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// F7 internals shared by the files of src/feature/structure (not a contract: other units use
// Structure.h and bases/*.h only, rule R2). Server thread unless noted; generation may call the
// placement code from worker threads, so the registries are filled at load / world ready and
// read-only afterwards.

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/world/level/BlockPos.h"

#include "feature/structure/Structure.h"
#include "feature/structure/pure/JavaRandom.h"

class Actor;
class Block;
class BlockSource;
class Vec3;

namespace gf::structure {

using pure::JavaRandom;

// ---- Java <-> Bedrock blocks (BlockStates.cpp) -----------------------------------------------------
namespace blocks {

/// minecraft:air.
[[nodiscard]] Block const& air();
/// The Bedrock type name of a block ("minecraft:stone_stairs" for Java's cobblestone stairs).
[[nodiscard]] std::string const& id(Block const& block);
/// The Bedrock id Java id `javaId` maps to ("" when the block does not exist on Bedrock).
[[nodiscard]] std::string bedrockId(std::string_view javaId);
/// A Java block state ("minecraft:oak_stairs", {"facing": "east", "half": "top"}) as a Bedrock
/// block (nullptr: unknown id; one warning per id). Unknown properties are ignored.
[[nodiscard]] Block const* fromJava(std::string_view javaId, Json const& props = Json::object());
/// fromJava or air.
[[nodiscard]] Block const& java(std::string_view javaId, Json const& props = Json::object());
/// The Bedrock plant id of a Java "minecraft:potted_<plant>" id ("" for any other id or when the
/// plant does not exist on Bedrock).
[[nodiscard]] std::string pottedPlant(std::string_view javaId);
/// Whether the block stops movement (Material#blocksMotion: has a collision shape).
[[nodiscard]] bool blocksMotion(Block const& block);
/// Whether `id` names a block of the block registry (no warning when it does not).
[[nodiscard]] bool isRegisteredBlock(std::string_view id);
/// A Java block state written as {"Name": id, "Properties": {...}} (BlockState.CODEC).
[[nodiscard]] Block const* fromJavaState(Json const& state);
/// Whether `block` is Java block `javaId` (by Bedrock id).
[[nodiscard]] bool is(Block const& block, std::string_view javaId);
/// Applies Java-named properties to a Bedrock block (BlockState#setValue); unknown ones are ignored.
[[nodiscard]] Block const& withJava(Block const& block, Json const& props);
/// One Java property of a Bedrock block ("facing" -> "north", "half" -> "top"), nullopt when absent.
[[nodiscard]] std::optional<std::string> javaProperty(Block const& block, std::string_view name);
/// FeaturePlacers.transferAllStateKeys: `target`'s default with every state `from` shares.
[[nodiscard]] Block const& transferStates(Block const& from, Block const& target);
/// BlockState#rotate / #mirror.
[[nodiscard]] Block const& rotate(Block const& block, Rotation rotation);
[[nodiscard]] Block const& mirror(Block const& block, Mirror mirror);

[[nodiscard]] bool isAir(Block const& block);
[[nodiscard]] bool isLiquid(Block const& block);
[[nodiscard]] bool isWater(Block const& block);
/// BlockState#canBeReplaced (air, liquids, replaceable plants, single snow layers, ...).
[[nodiscard]] bool canBeReplaced(Block const& block);
/// FeatureLogic.worldGenReplaceable (canBeReplaced or #twilightforest:worldgen_replaceables, and
/// not #minecraft:features_cannot_replace).
[[nodiscard]] bool worldGenReplaceable(Block const& block);
/// StructurePiece#isReplaceableByStructures.
[[nodiscard]] bool replaceableByStructures(Block const& block);
/// A block tag check by Java tag id ("minecraft:dirt"): gf::tags when it knows the tag, else the
/// vanilla membership built into the kit.
[[nodiscard]] bool inTag(Block const& block, std::string_view tag);
/// Fence / wall / slab / stairs (SoftReplaceProcessor#isFullBlock is the negation).
[[nodiscard]] bool isPartial(Block const& block);
/// Java Direction names <-> horizontal orientation.
[[nodiscard]] std::optional<Orientation> parseDirection(std::string_view name);
[[nodiscard]] char const*                directionName(Orientation o);
/// Drops every cached block (block types are registered per world on the client).
void clearCaches();
/// The Java ids of the rename table whose Bedrock target does not resolve (selftest report).
[[nodiscard]] std::vector<std::string> unresolvedRenames();

} // namespace blocks

// ---- orientation maths ----------------------------------------------------------------------------
/// A null Java orientation (StructurePiece#orientation == null): piece-local == world coordinates.
inline constexpr auto kNoOrientation = static_cast<Orientation>(-1);
[[nodiscard]] inline bool hasOrientation(Orientation o) { return static_cast<int>(o) >= 0 && static_cast<int>(o) <= 3; }
[[nodiscard]] Orientation rotateDir(Orientation dir, Rotation rotation);
[[nodiscard]] Orientation opposite(Orientation dir);
[[nodiscard]] Orientation clockwise(Orientation dir);
[[nodiscard]] int         stepX(Orientation dir);
[[nodiscard]] int         stepZ(Orientation dir);
/// Direction#get2DDataValue (S 0, W 1, N 2, E 3).
[[nodiscard]] int         data2D(Orientation dir);
[[nodiscard]] Orientation from2D(int value);
/// Rotation#getRotated.
[[nodiscard]] Rotation    addRotation(Rotation a, Rotation b);
[[nodiscard]] Rotation    rotationFromIndex(int index);
[[nodiscard]] int         rotationIndex(Rotation rotation);
/// RotationUtil.getRelativeRotation.
[[nodiscard]] Rotation    relativeRotation(Orientation from, Orientation to);
/// StructureTemplate.transform(pos, mirror, rotation, pivot).
[[nodiscard]] BlockPos    transformPos(BlockPos const& pos, Mirror mirror, Rotation rotation, BlockPos const& pivot);
/// StructureTemplate.getBoundingBox(start, rotation, pivot, mirror, size).
[[nodiscard]] BlockBox    templateBox(BlockPos const& start, Rotation rotation, BlockPos const& pivot, Mirror mirror, int sx, int sy, int sz);
/// BoundingBox#getCenter.
[[nodiscard]] BlockPos    boxCenter(BlockBox const& box);
[[nodiscard]] BlockBox    moved(BlockBox box, int dx, int dy, int dz);
[[nodiscard]] std::optional<BlockBox> intersection(BlockBox const& a, BlockBox const& b);
[[nodiscard]] Json        boxToJson(BlockBox const& box);
[[nodiscard]] BlockBox    boxFromJson(Json const& j);

// ---- templates (Templates.cpp) ------------------------------------------------------------------------
namespace templates {

/// A jigsaw connector of a template ("jigsaw" entries).
struct Connector {
    BlockPos    pos{0, 0, 0};
    std::string front = "north"; ///< FrontAndTop front
    std::string top   = "up";    ///< FrontAndTop top
    std::string pool  = "minecraft:empty";
    std::string name;
    std::string target;
    int         priority = 0;
};
/// A data marker ("markers" entries): `marker` "data" carries TF's metadata label in data.metadata;
/// any other marker is block entity data attached to the block at `pos`.
struct Marker {
    BlockPos    pos{0, 0, 0};
    std::string marker;
    Json        data = Json::object();
    [[nodiscard]] bool        isData() const { return marker == "data"; }
    [[nodiscard]] std::string label() const;
};
struct EntityEntry {
    double      x = 0, y = 0, z = 0;
    std::string id;
    Json        data = Json::object();
};
/// A parsed template with its palette resolved to Bedrock blocks.
struct Parsed {
    Template const*           base = nullptr;
    int                       sx = 0, sy = 0, sz = 0;
    std::vector<Block const*> palette;      ///< nullptr: unknown block (skipped)
    std::vector<Json>         paletteJson;  ///< the Java states
    /// Java "waterlogged" of each palette state: -1 no such property, 0 false, 1 true.
    std::vector<std::int8_t>  paletteWaterlogged;
    /// The plant of a Java minecraft:potted_* state (Bedrock id; "" for every other state).
    std::vector<std::string>  pottedPlant;
    /// The list StructureTemplate#addToLists puts each palette state in (javaList).
    std::vector<std::int8_t>  paletteList;
    std::vector<std::uint16_t> cells;       ///< palette index per block, y-major, then z, then x
    std::vector<Marker>       markers;
    std::vector<Connector>    connectors;
    std::vector<EntityEntry>  entities;
    [[nodiscard]] int index(int x, int y, int z) const { return (y * sz + z) * sx + x; }
};

/// StructureTemplate#addToLists for a Java state without block entity data (TemplateOrder.cpp): 0
/// a full collision block, 1 any other shape, 2 a block with a block entity (its data is saved
/// with the template). placeInWorld processes and places a template's blocks list by list, each
/// sorted by y, then x, then z (StructureTemplate#buildInfoList).
[[nodiscard]] int javaList(Json const& javaState);
/// Parses and validates a template document (errors listed; empty: valid).
[[nodiscard]] std::vector<std::string> validate(Json const& doc);
/// Registers an in-memory template (selftest fixtures, /tf structure_util capture); replaces one
/// with the same id. Returns false (with `error`) when the document is invalid.
bool add(Json const& doc, std::string* error = nullptr);
/// The parsed form of template `id` (nullptr when missing / invalid).
[[nodiscard]] Parsed const* parsed(std::string const& id);
/// Every template id known (files and in-memory), sorted.
[[nodiscard]] std::vector<std::string> ids();
/// (Re)scans `<mod>/structures/**/*.gft.json` and `<mod>/data/gf/structure/templates/**`.
void scan();

/// Where one placement happens.
struct Placement {
    BlockPos         offset{0, 0, 0}; ///< template origin in the world (templatePosition)
    BlockPos         pivotPos{0, 0, 0}; ///< "pos" of placeInWorld (structure bottom center)
    TemplateSettings settings;
    BlockPos         rotationPivot{0, 0, 0};
    int              flags = 2;
    bool             knownShape = false;
};
/// One block a placement produced, in world coordinates.
struct Placed {
    BlockPos     pos;
    Block const* block = nullptr;
    Json         data;
    int          waterlogged = -1; ///< the Java state's waterlogged (-1: the block has no such property)
    std::string  pottedPlant;      ///< flower pot content (Bedrock plant id)
};
/// StructureTemplate#placeInWorld for a GloamingForest template: processes every block, places
/// the ones inside settings.clip, applies their block entity markers and spawns the entities.
/// `dataMarkers` receives the TF data markers (world position + label) inside the clip.
bool placeInWorld(Parsed const& t, Region& region, Placement const& placement, RandomSource& random,
                  std::vector<std::pair<BlockPos, std::string>>* dataMarkers = nullptr, std::vector<Placed>* placedOut = nullptr);
/// StructureTemplate.calculateRelativePosition(settings, pos) (rotation, mirror, pivot).
[[nodiscard]] BlockPos relative(Placement const& placement, BlockPos const& local);
/// The yaw StructureTemplate#placeEntities gives an entity facing `yaw` in the template:
/// Entity#rotate(rotation) + Entity#mirror(mirror) - yaw.
[[nodiscard]] float turnedYaw(float yaw, Rotation rotation, Mirror mirror);
/// A template painting's facing after the placement: HangingEntity#rotate, then #mirror (which
/// flips the turned direction when the mirror crosses its axis).
[[nodiscard]] Orientation turnedPaintingFacing(Orientation facing, Rotation rotation, Mirror mirror);
/// StructureTemplate#fillFromWorld with entities: the blocks of `box` (Bedrock ids and states) and
/// its entities but players (paintings by the block they cover, facing and variant; the others
/// by position, rotation and their saved actor data) as a template document `id`. `skipped` counts the entities that
/// reach outside the box (left out). (Commands.cpp, /twilightforest structure_util capture.)
[[nodiscard]] Json capture(BlockSource& source, BlockBox const& box, std::string const& id, int& skipped);

} // namespace templates

// ---- processors (Processors.cpp) ------------------------------------------------------------------
namespace processors {
/// A processor instance: type id + its JSON config ({"processor_type": ..., ...}).
struct Instance {
    std::string type;
    Json        config = Json::object();
};
/// Expands TemplateSettings::processors entries: inline JSON ("{...}"), processor list ids
/// (worldgen/processor_list, in-memory lists) or bare processor type ids.
[[nodiscard]] std::vector<Instance> expand(std::vector<std::string> const& entries);
/// Runs one processor; nullopt drops the block.
[[nodiscard]] std::optional<BlockInfo> run(Instance const& p, Region& region, BlockInfo const& original, BlockInfo const& current,
                                           TemplateSettings const& settings, BlockPos const& pivot);
/// Registers an in-memory processor list ("gf_test:..."), as worldgen/processor_list JSON.
void addList(std::string const& id, Json const& list);
void registerBuiltins();
/// StructurePlaceSettings#getRandom(pos) during a placement: a fresh position-seeded
/// LegacyRandomSource, or the placement's shared one when TemplateSettings::seed is set.
[[nodiscard]] RandomSource& randomAt(BlockPos const& pos);
/// The structure "pivot" of the running placement (StructureProcessor's centerBottom argument).
[[nodiscard]] BlockPos currentPivot();
/// The template offset of the running placement (StructureProcessor's offset argument).
[[nodiscard]] BlockPos currentOffset();
void                   setCurrentOffset(BlockPos const& offset);
} // namespace processors

// ---- registries (Piece.cpp) -------------------------------------------------------------------------
[[nodiscard]] ProcessorFn const* processorFor(std::string const& typeId);
[[nodiscard]] MarkerFn const*    markerFor(std::string const& id);
[[nodiscard]] std::vector<std::string> pieceTypes();
[[nodiscard]] std::vector<std::string> processorTypes();
[[nodiscard]] std::vector<std::string> markerTypes();

// ---- TF template marker handlers (Markers.cpp) ---------------------------------------------------------
namespace markers {
/// A TemplateMarkerHandler (typed JSON: block_placement, drying_rack, loot, painting, rotation,
/// handler_switch). Returns whether it handled the label.
bool handle(Json const& handler, std::string const& label, BlockPos const& pos, Region& region, RandomSource& random,
            BlockBox const& chunkBounds, Rotation rotation);
/// A template_marker_handler_list by id (data/twilightforest/twilight/template_marker_handler_list).
[[nodiscard]] Json const* list(std::string const& id);
void                      addList(std::string const& id, Json const& list);
/// BlockStateProvider#getState for the vanilla providers TF's data uses.
[[nodiscard]] Block const* provide(Json const& provider, RandomSource& random, BlockPos const& pos);
void                       registerBuiltins();
/// TemplateMarkerHandler.parseRange.
[[nodiscard]] int parseRange(std::string const& label, RandomSource& random, int defaultMin, int defaultMax);
} // namespace markers

// ---- block entities and deferred work (BlockEntities.cpp) -------------------------------------------
namespace blockentities {
/// The BlockSource behind `region` when it is one (selftests, commands, generateAt).
[[nodiscard]] BlockSource* sourceOf(Region& region);
/// A chest-like container with a loot table at `pos` (gf::loot::placeContainer, deferred to the
/// server thread when the region has no BlockSource).
void container(Region& region, BlockPos const& pos, std::string const& blockId, std::optional<Orientation> facing, std::string const& tableId,
               std::int64_t seed);
/// Sets the loot table of an existing container at `pos` (custom containers: gf::inventory).
void lootTable(Region& region, BlockPos const& pos, std::string const& tableId, std::int64_t seed);
/// Places a mob spawner (vanilla mob_spawner, TF's setSpawner / placeSpawner) for `entityId` with
/// optional spawner data (Delay, MinSpawnDelay, MaxSpawnDelay, SpawnCount, MaxNearbyEntities,
/// RequiredPlayerRange, SpawnRange; entity_scale and spawn_rules from spawner_processor).
void spawner(Region& region, BlockPos const& pos, std::string const& entityId, Json const& data);
/// A template's spawner block entity data for the block the template put at `pos` (the block
/// stays, as StructureTemplate loads the data into that block's own block entity): a vanilla
/// spawner is set up as spawner() does; any other block (TF's sinister spawner, whose block entity
/// is F8's) keeps the data in its BlockData "structure"."spawner" record
/// ({"entity", "data": {the BaseSpawner fields}, "scale"?, "spawn_rules"?}); air takes nothing.
void spawnerData(Region& region, BlockPos const& pos, std::string const& entityId, Json const& data);
/// Sign text (lines 0..3).
void sign(Region& region, BlockPos const& pos, std::vector<std::string> const& lines);
/// Spawns a template entity at a world position (StructureTemplate#placeEntities); false when the
/// id is empty. `data`: "Rotation" [yaw, pitch] (already turned by the placement) and "nbt" (a
/// Bedrock actor tag in SNBT, as capture writes it: the entity is loaded from it with its position,
/// rotation and identity replaced; without it a new entity of the type is spawned). Paintings go
/// through paintings::tryHang.
bool entity(Region& region, double x, double y, double z, std::string const& entityId, Json const& data);
/// Hangs painting `motive` ("kebab") at `pos` (the block it covers) facing `direction` once the
/// caller ran TF's spot check (true, as EntityUtil.tryHangPainting): at once in a live region, when
/// the chunk is loaded during generation. A painting the game refuses (no wall behind it) drops as
/// a painting item at the painting's center, as TF's does on its next survival check. False for an
/// unknown motive.
bool painting(Region& region, BlockPos const& pos, Orientation direction, std::string const& motive, Json const& box);
/// Whether a queued (not yet spawned) painting of dimension `dim` overlaps `box` ([minX, minY,
/// minZ, maxX, maxY, maxZ] doubles): the ProtoChunk entities TF's painting check sees. Thread safe
/// (a per-chunk index of the queued paintings' boxes behind a shared lock).
[[nodiscard]] bool pendingPaintingIntersects(int dim, double const (&box)[6]);
/// A painting hanging in the world, as a template stores it.
struct HungPainting {
    std::string variant; ///< "minecraft:kebab"
    Orientation facing = Orientation::South;
    BlockPos    block{0, 0, 0}; ///< the block it covers (Java's HangingEntity#pos)
};
[[nodiscard]] std::optional<HungPainting> describePainting(Actor& actor);
/// The box Bedrock gives a painting of `motive` facing `direction` placed from `placement`
/// (selftests: which block the engine's placement position is).
[[nodiscard]] std::optional<std::array<double, 6>> paintingBoxFor(BlockPos const& placement, Orientation direction, std::string const& motive);
/// The placement position spawnPainting hands Bedrock for a painting covering `covered`.
[[nodiscard]] BlockPos paintingPlacement(BlockPos const& covered, Orientation direction);
/// UpdateMarkingProcessor / ProtoChunk#markPosForPostprocessing: the block at `pos` takes its shape
/// from its neighbours once its chunk is loaded and the structure stands (queued in its chunk's
/// list of marked positions, live regions included; nothing for scratch regions).
void updateShape(Region& region, BlockPos const& pos);
/// The spawner data F7 keeps for the spawner at `pos` ({"entity", "scale"?, "spawn_rules"?,
/// "data"?}).
[[nodiscard]] std::optional<Json> spawnerRecord(BlockSource& source, BlockPos const& pos);
/// Drops that record (its spawner spawns another mob now: Java replaced the whole SpawnData).
void forgetSpawnerRecord(BlockSource& source, BlockPos const& pos);
/// A flower pot's plant (FlowerPotBlockActor PlantBlock).
void flowerPot(Region& region, BlockPos const& pos, std::string const& plantId);
/// Water in the liquid layer at `pos` of a generation region, put there once the chunk is loaded
/// and only while the block there is still `blockId`.
void waterlog(Region& region, BlockPos const& pos, std::string const& blockId);
/// DryingRackBlockEntity#fillFromLootTable: rolls `tableId` now and stores the last stack.
void fillRack(Region& region, BlockPos const& pos, std::string const& tableId, std::int64_t seed);
/// The loot table F7 gave the container at `pos` (count_loot unpacks it), nullopt when none.
struct LootRecord {
    std::string  table;
    std::int64_t seed = 0;
};
[[nodiscard]] std::optional<LootRecord> lootRecord(int dim, BlockPos const& pos);
void                                    forgetLootRecord(int dim, BlockPos const& pos);
/// Runs the deferred work whose chunks are loaded (server tick; every job of a chunk at once, then
/// its shape updates; chunks checked every few ticks).
void flush();
/// Queued jobs plus marked positions.
[[nodiscard]] std::size_t pending();
/// A selftest job (kind "gf_test_mark": writes `nonce` under gf:structure:test_mark when run).
void queueTestMark(int dim, BlockPos const& pos, std::string const& nonce);
void load();
void save();
/// The hook that gives the kit's paintings Java's survival check (HangingActor::$wouldSurvive).
void install();
void uninstall();
/// HangingEntity#survives for the painting `actor` (the kit's check; nullopt: not a painting).
[[nodiscard]] std::optional<bool> javaSurvives(Actor& actor);
} // namespace blockentities

// ---- spawners placed by structures (Spawners.cpp) ------------------------------------------------------
namespace spawners {
/// The spawner block whose spawn is under way at `at` for mob `entityId`: the nearest vanilla
/// spawner around `at` (within its SpawnRange, one block up or down) whose mob is `entityId`,
/// preferring the one whose delay ran out (the one spawning now).
[[nodiscard]] std::optional<BlockPos> firingSpawner(BlockSource& source, Vec3 const& at, std::string const& entityId);
/// Applies a generic.scale-like size factor to a live mob now (Bedrock bounding box data).
bool applyScale(Actor& actor, float scale);
/// Gives a mob the generic.scale `scale` for good: the factor and the size it gave are kept on the
/// mob (actor tags, saved with it) and the factor goes back on every part of the size the game sets
/// from the mob's definition again (loading, growing up). The first application waits a couple of
/// ticks (the mob is set up in the level first).
void scaleMob(Actor& mob, float scale);
/// The generic.scale scaleMob gave `mob` (nullopt: none).
[[nodiscard]] std::optional<float> scaleOf(Actor const& mob);
/// The scale of the mob's bounding box data (what clients draw it at; 1 when unset).
[[nodiscard]] std::optional<float> dataScaleOf(Actor& mob);
/// Spawns the SpawningMobEvent veto (custom spawn rules) and SpawnedMobEvent scale listeners, the
/// actor-load hook and the size checks run from tick().
void install();
void uninstall();
/// Server tick: pending scales, size checks of scaled mobs.
void tick();
/// Forgets the watched mobs (world closed; the client opens many worlds in one process).
void clear();
/// Spawns vetoed by custom spawn rules so far (selftests).
[[nodiscard]] int vetoes();
/// Holds the periodic size checks of scaled mobs (selftests measure the game's own size reset).
void holdSizeChecks(bool held);
} // namespace spawners

// ---- liquids (Liquids.cpp) ---------------------------------------------------------------------------
namespace liquids {
/// The TemplateSettings / PlaceSettings::processors entry standing for Java's
/// setLiquidSettings(LiquidSettings.IGNORE_WATERLOGGING) (processors::expand skips it).
inline constexpr char kIgnoreWaterlogging[] = "gf:liquid_settings/ignore_waterlogging";
[[nodiscard]] bool ignoresWaterlogging(std::vector<std::string> const& processors);
/// Java's SimpleWaterloggedBlock / LiquidBlockContainer, by Bedrock block (the families whose Java
/// state has a "waterlogged" property).
[[nodiscard]] bool waterloggable(Block const& block);
/// Blocks whose Java fluid state is always a water source (seagrass, kelp).
[[nodiscard]] bool alwaysWater(Block const& block);
/// A water source block (minecraft:water / flowing_water at liquid_depth 0).
[[nodiscard]] bool isWaterSource(Block const& block);
/// A lava source block (minecraft:lava / flowing_lava at liquid_depth 0).
[[nodiscard]] bool isLavaSource(Block const& block);
/// Drops the per-block cache (block types are registered per world on the client).
void clearCaches();
/// Level#getFluidState(pos) is a water source: the block layer or the liquid layer.
[[nodiscard]] bool waterSourceAt(Region& region, BlockPos const& pos);
/// Puts water into / takes it out of the liquid layer at `pos` (live regions at once,
/// generation regions when the chunk is loaded).
void setWaterlogged(Region& region, BlockPos const& pos, bool waterlogged);
/// Places `block` as Java places a state whose waterlogged is `waterlogged` (the liquid layer
/// holds water exactly when it is true).
bool setBlock(Region& region, BlockPos const& pos, Block const& block, int flags, bool waterlogged);
} // namespace liquids

// ---- paintings (Paintings.cpp) -------------------------------------------------------------------------
namespace paintings {
/// A painting variant of 1.21.1 (width x height in blocks).
struct Variant {
    std::string id; ///< "minecraft:kebab"
    int         width = 1, height = 1;
};
/// The painting_variant registry (data-driven: sorted by id).
[[nodiscard]] std::vector<Variant> const& registry();
[[nodiscard]] std::optional<Variant>      byId(std::string_view id);
/// The members of a painting_variant tag in the tag's order ("minecraft:placeable").
[[nodiscard]] std::vector<Variant> tag(std::string const& tagId);
/// Painting#calculateBoundingBox: [minX, minY, minZ, maxX, maxY, maxZ].
void boundingBox(BlockPos const& pos, Orientation direction, Variant const& variant, double (&out)[6]);
/// EntityUtil.tryHangPainting.
bool tryHang(Region& region, BlockPos const& pos, Orientation direction, std::optional<Variant> const& variant);
/// EntityUtil.getEntitiesInAABB(new AABB(pos)) is not empty: a queued painting (the ProtoChunk
/// entities TF reads; in a live world TF sees none) overlaps the block at `pos`.
[[nodiscard]] bool occupied(Region& region, BlockPos const& pos);
/// EntityUtil.getPaintingOfSize.
[[nodiscard]] std::optional<Variant> ofSize(RandomSource& random, int width, int height, bool exactMeasurements);
/// EntityUtil.getPaintingsOfSizeOrSmaller.
[[nodiscard]] std::vector<Variant> ofSizeOrSmaller(std::string const& tagId, int width, int height);
/// LichBossRoom.placePainting(pos, random, limitTries, paintings, level, width, height, dir).
bool place(BlockPos const& pos, RandomSource& random, int limitTries, std::string const& tagId, Region& region, int width, int height,
           Orientation direction);
/// LichBossRoom.placePainting(label, pos, level, random, chunkBounds, rotation, limitTries, rarityFactor, paintings).
bool placeLabelled(std::string const& label, BlockPos const& pos, Region& region, RandomSource& random, BlockBox const& chunkBounds, Rotation rotation,
                   int limitTries, int rarityFactor, std::string const& tagId);
} // namespace paintings

/// WorldGenRegion#getRandom: a random of the region being decorated, separate from the piece's
/// random (TF draws wall selectors, canopy trees and planter trees from it). Seeded from the
/// region's seed and center chunk; the same region keeps drawing from the same random.
[[nodiscard]] RandomSource& regionRandom(Region& region);
/// Forgets this thread's random of `region` (a new region object at that address starts its own;
/// nullptr: all of them).
void resetRegionRandom(Region const* region = nullptr);

namespace outlines {
/// Draws the dev command outlines and chunk labels (server tick, from the module's tick listener;
/// returns at once while nothing is displayed) and drops the expired ones.
void tick();
/// Clears every outline and label (clear_display, module uninstall, world closed).
void shutdown();
} // namespace outlines

// ---- data (Data.cpp) --------------------------------------------------------------------------------
namespace data {
/// The mod directory's data root (`<mod>/data`).
[[nodiscard]] std::string root();
/// Loads every JSON under data/<ns>/<dir>/ into id -> document ("twilightforest:camp/tent").
[[nodiscard]] std::map<std::string, Json> loadDir(std::string const& dir);
/// A data document data/<ns>/<dir>/<path>.json by id.
[[nodiscard]] std::optional<Json> load(std::string const& dir, std::string const& id);
/// (Re)loads the twilight/* registries F7 consumes (palettes, stalactites, speleothem configs,
/// template definitions, marker handler lists).
void reload();
} // namespace data

// ---- jigsaw pools (Jigsaw.cpp) --------------------------------------------------------------------------
namespace pools {
struct Entry {
    std::string templateId;
    Json        instance; ///< TemplatePoolInstance (weight or object)
    int         weight = 1;
};
/// The pool `poolId` built from the template definitions (sorted by template id; nullptr: none).
[[nodiscard]] std::vector<Entry> const* get(std::string const& poolId);
/// Adds an in-memory template definition (templateId -> {pool: instance}).
void addDefinition(std::string const& templateId, Json const& definition);
void rebuild(std::map<std::string, Json> const& definitions);
[[nodiscard]] std::size_t definitionCount();
/// WeightedRandomList#getRandom.
[[nodiscard]] Entry const* random(std::vector<Entry> const& pool, RandomSource& random);
} // namespace pools

// ---- palettes / speleothems data (Palettes.cpp, Speleothems.cpp) ------------------------------------
namespace palettes {
void load(std::map<std::string, Json> const& docs);
[[nodiscard]] std::vector<std::string> ids();
/// WoodPalette#modifyBlockWithType: `block` in palette `target` turned into palette `replacement`
/// (nullptr when the block is not in `target`).
[[nodiscard]] Block const* swizzle(std::string const& target, std::string const& replacement, Block const& block);
} // namespace palettes
namespace selectors {
[[nodiscard]] std::vector<std::string> ids();
} // namespace selectors
namespace speleothems {
void load(std::map<std::string, Json> const& varieties, std::map<std::string, Json> const& settings);
[[nodiscard]] std::vector<std::string> ids();
struct Lattice {
    float spacing = 3.5f, xOffset = 0, zOffset = 0, xSpacing = 0, zSpacing = 0;
};
[[nodiscard]] Lattice lattice(std::string const& configId);
/// The lattice positions inside `bounds` at `y` (RectangleLatticeIterator zipped pair).
[[nodiscard]] std::vector<BlockPos> latticePositions(Lattice const& lattice, BlockBox const& bounds, int y);
[[nodiscard]] bool shouldDoAStalactite(std::string const& configId, RandomSource& random);
[[nodiscard]] bool shouldDoAStalagmite(std::string const& configId, RandomSource& random);
/// BlockSpikeFeature.startSpike with the config's stalactite / stalagmite pick.
bool spike(Region& region, BlockPos const& pos, std::string const& configId, RandomSource& random, bool hanging, int forcedMaxHeight);
} // namespace speleothems

// ---- misc -------------------------------------------------------------------------------------------------
/// Logs `message` once per key (missing ids and the like).
void warnOnce(std::string const& key, std::string const& message);
/// The TF dimension's sea level (noise settings: 0).
inline constexpr int kSeaLevel = 0;

} // namespace gf::structure
