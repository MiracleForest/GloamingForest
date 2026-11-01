// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// F7 internals shared by the files of src/feature/structure (not a contract: other units use
// Structure.h and bases/*.h only, rule R2). Server thread unless noted; generation may call the
// placement code from worker threads, so the registries are filled at load / world ready and
// read-only afterwards.

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
    std::vector<std::uint16_t> cells;       ///< palette index per block, y-major, then z, then x
    std::vector<Marker>       markers;
    std::vector<Connector>    connectors;
    std::vector<EntityEntry>  entities;
    [[nodiscard]] int index(int x, int y, int z) const { return (y * sz + z) * sx + x; }
};

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
/// A mob spawner (vanilla mob_spawner) for `entityId` with optional spawner data (Delay,
/// MinSpawnDelay, MaxSpawnDelay, SpawnCount, MaxNearbyEntities, RequiredPlayerRange, SpawnRange).
void spawner(Region& region, BlockPos const& pos, std::string const& entityId, Json const& data);
/// Sign text (lines 0..3).
void sign(Region& region, BlockPos const& pos, std::vector<std::string> const& lines);
/// Spawns an entity at a world position (template entities); false when it could not (live
/// regions) or when the id is empty. Paintings go through paintings::tryHang.
bool entity(Region& region, double x, double y, double z, std::string const& entityId, Json const& data);
/// Hangs painting `motive` ("kebab") at `pos` facing `direction`; with a BlockSource the result is
/// the game's, during generation the painting is queued (the caller checked the spot) and true.
bool painting(Region& region, BlockPos const& pos, Orientation direction, std::string const& motive, Json const& box);
/// Whether a queued (not yet spawned) painting of dimension `dim` overlaps `box` ([minX, minY,
/// minZ, maxX, maxY, maxZ] doubles): the ProtoChunk entities TF's painting check sees.
[[nodiscard]] bool pendingPaintingIntersects(int dim, double const (&box)[6]);
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
/// SpawnerProcessor entity_width_max: the scale spawned mobs of the spawner at `pos` get.
void spawnerScale(int dim, BlockPos const& pos, std::string const& entityId, float scale);
/// Applies a generic.scale-like size factor to a live mob (Bedrock bounding box data).
bool applyScale(Actor& actor, float scale);
/// Runs the deferred work whose chunks are loaded (server tick; every job of a chunk at once,
/// chunks checked every few ticks).
void flush();
[[nodiscard]] std::size_t pending();
/// A selftest job (kind "gf_test_mark": writes `nonce` under gf:structure:test_mark when run).
void queueTestMark(int dim, BlockPos const& pos, std::string const& nonce);
void load();
void save();
void installEvents();
void uninstallEvents();
} // namespace blockentities

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
/// An entity (a painting so far, or any in a live world) overlaps the block at `pos`.
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
/// Forgets the region random of this thread (a new region object starts its own).
void resetRegionRandom();

namespace outlines {
/// Removes the outline tick listener (clear_display, module uninstall, world closed).
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
