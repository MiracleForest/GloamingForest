// Contract "spawn" (F5): the Twilight Forest's spawn engine. Per-entity placement rules
// (SpawnPlacements), per-biome tables (registered by U1, else the imported biome JSON), special
// spawns (onSpecialSpawn: NeoForge's PotentialSpawns event, where F6 answers TF's
// EntityEvents#structureSpecialSpawns with the landmark starts' controlled spawns and the
// structures' spawn overrides of ChunkGenerator#getMobsAt), the chunk-generation spawns
// (NaturalSpawner#spawnMobsForChunkGeneration) and the natural spawner of the dimension
// (NaturalSpawner#spawnForChunk with Java's global and per-player mob caps), all behaviour
// compatible with Minecraft 1.21.1.
#include "feature/spawn/Spawn.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/utility/optional_ref.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/entity/components/PersistentComponent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/Weather.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/BrightnessPair.h"
#include "mc/world/level/chunk/ChunkState.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/phys/AABB.h"

#include "core/jrand/Mth.h"
#include "feature/dimension/World.h"
#include "feature/features/Internal.h"
#include "feature/spawn/Internal.h"
#include "feature/tags/Tags.h"
#include "feature/worldgen/Internal.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

#pragma clang fp contract(off)

namespace gf::spawn {

using features::detail::fieldObj;

using Json = nlohmann::json;
namespace kit = features::kit;

namespace {

struct Placement {
    PlacementRule rule = PlacementRule::OnGround;
    PredicateFn   predicate;
};

struct State {
    std::shared_mutex                                   mutex; ///< guards every member below
    std::unordered_map<std::string, Placement>          placements;
    std::unordered_map<std::string, worldgen::Heightmap> heightmaps; ///< SpawnPlacements#getHeightmapType
    std::unordered_map<std::string, ext::SpawnBehaviour> behaviours; ///< the mob's own spawn answers
    std::unordered_map<std::string, Table>              tables;     ///< registered by units (setBiomeSpawns)
    std::unordered_map<std::string, Table>              jsonTables; ///< read from the imported biome JSON (per world)
    /// onSpecialSpawn's functions, in registration order: replaced as a whole on registration, so a
    /// reader copies the pointer only
    std::shared_ptr<std::vector<SpecialSpawnFn> const>  special = std::make_shared<std::vector<SpecialSpawnFn> const>();
};
State& state() {
    static State s;
    return s;
}

/// Java's MobCategory (enum order = NaturalSpawner.SPAWNING_CATEGORIES): max per chunk, friendly,
/// persistent, despawn distance.
struct Category {
    char const* name;
    int         max;
    bool        friendly;
    bool        persistent;
    int         despawnDistance;
};
constexpr Category kCategories[] = {
    {"monster", 70, false, false, 128},
    {"creature", 10, true, true, 128},
    {"ambient", 15, true, false, 128},
    {"axolotls", 5, true, false, 128},
    {"underground_water_creature", 5, true, false, 128},
    {"water_creature", 5, true, false, 128},
    {"water_ambient", 20, true, false, 64},
};
std::vector<SpawnEntry> entriesOf(Json const& list) {
    std::vector<SpawnEntry> out;
    if (!list.is_array()) return out;
    for (auto const& e : list)
        out.push_back({features::detail::normalizeId(e.value("type", std::string())), e.value("weight", 1), e.value("minCount", 1), e.value("maxCount", 1)});
    return out;
}

/// MobSpawnSettings.SpawnerData equality (a record: type, weight, minCount, maxCount).
bool sameEntry(SpawnEntry const& a, SpawnEntry const& b) {
    return a.entityId == b.entityId && a.weight == b.weight && a.minCount == b.minCount && a.maxCount == b.maxCount;
}

/// WeightedRandomList#getRandom.
SpawnEntry const* pick(std::vector<SpawnEntry> const& list, RandomSource& random) {
    int total = 0;
    for (auto const& e : list) total += e.weight;
    if (total <= 0) return nullptr;
    int i = random.nextInt(total);
    for (auto const& e : list) {
        i -= e.weight;
        if (i < 0) return &e;
    }
    return nullptr;
}

Placement placementOf(std::string const& id) {
    std::shared_lock lock(state().mutex);
    auto             it = state().placements.find(id);
    return it == state().placements.end() ? Placement{} : it->second;
}

worldgen::Heightmap heightmapTypeOf(std::string const& id) {
    std::shared_lock lock(state().mutex);
    auto             it = state().heightmaps.find(id);
    return it == state().heightmaps.end() ? worldgen::Heightmap::MotionBlockingNoLeaves : it->second;
}

ext::SpawnBehaviour behaviourOf(std::string const& id) {
    std::shared_lock lock(state().mutex);
    auto             it = state().behaviours.find(id);
    return it == state().behaviours.end() ? ext::SpawnBehaviour{} : it->second;
}

/// NaturalSpawner#isValidEmptySpawnBlock.
bool emptySpawnBlock(Block const& b) {
    return !kit::blocksMotion(b) && !kit::isFluid(b) && b.getTypeName() != "minecraft:powder_snow";
}

/// Whether the block's collision shape is the full cube (BlockState#isCollisionShapeFullBlock);
/// in-memory regions take every block that blocks motion as a cube.
bool fullCube(worldgen::Region& region, BlockPos const& pos, Block const& block) {
    if (!kit::blocksMotion(block)) return false;
    auto* src = kit::blockSource(region);
    if (!src) return true;
    AABB shape = block.getBlockType().getCollisionShape(block, *src, pos, {});
    return shape.min.x <= pos.x + 1e-4F && shape.min.y <= pos.y + 1e-4F && shape.min.z <= pos.z + 1e-4F && shape.max.x >= pos.x + 1 - 1e-4F
        && shape.max.y >= pos.y + 1 - 1e-4F && shape.max.z >= pos.z + 1 - 1e-4F;
}

/// BlockBehaviour#isPathfindable(state, LAND): not a full collision cube, with the overrides of
/// the blocks a spawn lands on (fences, walls and closed gates / doors / trapdoors are not; snow
/// layers below 5 are).
bool pathfindableLand(worldgen::Region& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    auto        id = kit::javaId(b);
    if (id == "minecraft:snow") {
        auto layers = features::detail::javaProperty(b, "layers");
        return layers && std::atoi(layers->c_str()) < 5;
    }
    if (kit::hasTag(b, "minecraft:fences") || kit::hasTag(b, "minecraft:walls")) return false;
    if (kit::hasTag(b, "minecraft:fence_gates") || kit::hasTag(b, "minecraft:doors") || kit::hasTag(b, "minecraft:trapdoors")) {
        auto open = features::detail::javaProperty(b, "open");
        return open && *open == "true";
    }
    return !fullCube(region, pos, b);
}

/// SpawnPlacements#isSpawnPositionOk for `rule` through a Region.
bool positionOk(PlacementRule rule, worldgen::Region& region, BlockPos const& pos) {
    switch (rule) {
    case PlacementRule::NoRestrictions:
        return true;
    case PlacementRule::InWater:
        return kit::isWater(region.getBlock(pos)) && !kit::blocksMotion(region.getBlock({pos.x, pos.y + 1, pos.z}));
    case PlacementRule::InLava:
        return kit::isLava(region.getBlock(pos));
    case PlacementRule::OnGround: {
        auto const& below = region.getBlock({pos.x, pos.y - 1, pos.z});
        auto const& id    = below.getTypeName();
        if (!kit::blocksMotion(below) || id == "minecraft:bedrock" || id == "minecraft:barrier") return false;
        return emptySpawnBlock(region.getBlock(pos)) && emptySpawnBlock(region.getBlock({pos.x, pos.y + 1, pos.z}));
    }
    }
    return false;
}

/// An entity box: EntityType#getSpawnAABB / EntityDimensions#makeBoundingBox at feet (x, y, z)
/// (half the float width, the float height).
struct SpawnBox {
    double x0, y0, z0, x1, y1, z1;
};
SpawnBox spawnBox(double x, double y, double z, double width, double height) {
    float f = static_cast<float>(width) / 2.0F;
    float h = static_cast<float>(height);
    return {x - static_cast<double>(f), y, z - static_cast<double>(f), x + static_cast<double>(f), y + static_cast<double>(h), z + static_cast<double>(f)};
}

/// LevelReader#containsAnyLiquid(box): a block with a fluid (water or lava, a waterlogged block's
/// liquid layer included) in [floor(min), ceil(max)) on every axis.
bool containsAnyLiquid(worldgen::Region& region, SpawnBox const& b) {
    int   i = core::mth::floor(b.x0), j = core::mth::ceil(b.x1);
    int   k = core::mth::floor(b.y0), l = core::mth::ceil(b.y1);
    int   i1 = core::mth::floor(b.z0), j1 = core::mth::ceil(b.z1);
    auto* src = kit::blockSource(region);
    for (int x = i; x < j; ++x)
        for (int y = k; y < l; ++y)
            for (int z = i1; z < j1; ++z) {
                BlockPos p{x, y, z};
                if (kit::isFluid(region.getBlock(p))) return true;
                if (src && y >= src->getMinHeight() && y < src->getMaxHeight() && kit::isFluid(src->getExtraBlock(p))) return true;
            }
    return false;
}

/// The water categories, whose usual mobs are WaterAnimals (or Axolotls): Mob#checkSpawnObstruction
/// without the liquid check.
bool waterCategory(std::string_view category) {
    return category == "water_creature" || category == "water_ambient" || category == "underground_water_creature" || category == "axolotls";
}

/// LevelReader#noCollision(EntityType#getSpawnAABB(x, y, z)) against blocks (the collision
/// shapes through a BlockSource, cubes in memory).
bool noCollision(worldgen::Region& region, double x, double y, double z, double width, double height) {
    auto   box = spawnBox(x, y, z, width, height);
    double ax0 = box.x0, ax1 = box.x1, ay0 = box.y0, ay1 = box.y1, az0 = box.z0, az1 = box.z1;
    int    x0 = core::mth::floor(ax0), x1 = core::mth::ceil(ax1) - 1;
    int    y0 = core::mth::floor(ay0), y1 = core::mth::ceil(ay1) - 1;
    int    z0 = core::mth::floor(az0), z1 = core::mth::ceil(az1) - 1;
    auto*  src = kit::blockSource(region);
    for (int bx = x0; bx <= x1; ++bx)
        for (int by = y0; by <= y1; ++by)
            for (int bz = z0; bz <= z1; ++bz) {
                BlockPos    p{bx, by, bz};
                auto const& b = region.getBlock(p);
                if (!kit::blocksMotion(b)) continue;
                if (!src) return false;
                AABB s = b.getBlockType().getCollisionShape(b, *src, p, {});
                if (s.max.x > ax0 && s.min.x < ax1 && s.max.y > ay0 && s.min.y < ay1 && s.max.z > az0 && s.min.z < az1) return false;
            }
    return true;
}

/// DimensionType#brightness for the Twilight Forest's ambient light (0.01).
float brightness(int light) {
    float f  = static_cast<float>(light) / 15.0F;
    float f1 = f / (4.0F - 3.0F * f);
    return f1 + 0.01F * (1.0F - f1); // Mth.lerp(ambientLight, f1, 1.0F)
}

/// Level#getSkyDarken of the Twilight Forest (fixed time 13000): Level#updateSkyBrightness with
/// the dimension's rain and thunder levels.
int twilightSkyDarken(BlockSource& region) {
    double rain = 0.0, thunder = 0.0;
    if (auto const& w = region.getDimension().mWeather.get()) {
        rain    = static_cast<double>(w->mRainLevel);
        thunder = static_cast<double>(w->mLightningLevel) * rain;
    }
    // DimensionType#timeOfDay(13000) and the sky brightness factor
    double d0  = 13000.0 / 24000.0 - 0.25;
    d0        -= std::floor(d0);
    double d1  = 0.5 - std::cos(d0 * 3.141592653589793) / 2.0;
    float  t   = static_cast<float>(d0 * 2.0 + d1) / 3.0F;
    double d2  = 0.5 + 2.0 * std::clamp(static_cast<double>(std::cos(t * 6.2831855F)), -0.25, 0.25);
    double wr  = 1.0 - rain * 5.0 / 16.0;
    double wt  = 1.0 - thunder * 5.0 / 16.0;
    return static_cast<int>((1.0 - d2 * wr * wt) * 11.0);
}

/// LevelReader#getMaxLocalRawBrightness at `pos`: the engine's light through a BlockSource (the
/// natural spawner); during generation (no light yet, WorldGenRegion#getSkyDarken 0) the sky light
/// is estimated from the blocks above (leaves and water dim it by one, opaque blocks block it).
int rawBrightness(worldgen::Region& region, BlockPos const& pos, bool generation) {
    if (!generation)
        if (auto* src = kit::blockSource(region)) {
            auto pair = src->getBrightnessPair(pos);
            int  sky  = static_cast<int>(pair.sky->mValue) - twilightSkyDarken(*src);
            return std::max(sky, static_cast<int>(pair.block->mValue));
        }
    int sky = 15;
    for (int y = pos.y; y < region.maxY() && sky > 0; ++y) {
        auto const& b = region.getBlock({pos.x, y, pos.z});
        if (b.isAir()) continue;
        if (kit::isLeaves(b) || kit::isFluid(b)) --sky;
        else if (kit::blocksMotion(b)) sky = 0;
    }
    return std::max(sky, 0);
}

/// Mob#checkSpawnRules of a mob without its own override: PathfinderMob#getWalkTargetValue >= 0
/// with the walk target of its class (Monster: 0.5 - light, Animal: 10 on grass else light - 0.5,
/// Mob / PathfinderMob / WaterAnimal: 0), by default the one its spawn category implies.
bool defaultSpawnRules(worldgen::Region& region, BlockPos const& pos, ext::WalkTarget walkTarget, std::string_view category, bool generation) {
    if (walkTarget == ext::WalkTarget::ByCategory)
        walkTarget = category == "monster" ? ext::WalkTarget::Monster : category == "creature" ? ext::WalkTarget::Animal : ext::WalkTarget::Zero;
    switch (walkTarget) {
    case ext::WalkTarget::Monster:
        return 0.5F - brightness(rawBrightness(region, pos, generation)) >= 0.0F;
    case ext::WalkTarget::Animal:
        if (kit::javaId(region.getBlock({pos.x, pos.y - 1, pos.z})) == "minecraft:grass_block") return true;
        return brightness(rawBrightness(region, pos, generation)) - 0.5F >= 0.0F;
    default:
        return true;
    }
}

/// Entity#blocksBuilding: living mobs and players, boats and minecarts.
bool blocksBuilding(Actor const& a) {
    if (!a.isAlive()) return false;
    return a.hasCategory(ActorCategory::Mob) || a.hasCategory(ActorCategory::Player) || a.hasCategory(ActorCategory::Boat)
        || a.hasCategory(ActorCategory::Minecart);
}

/// Mob#checkSpawnObstruction of a mob without its own override at feet (x, y, z): Mob's
/// `!containsAnyLiquid(box) && isUnobstructed(this)` or WaterAnimal's `isUnobstructed(this)`, by
/// default the one its spawn category implies. `entities` is the level the entity check reads;
/// nullptr during chunk generation, where Java's WorldGenRegion#getEntities is always empty.
bool defaultObstruction(
    worldgen::Region&          region,
    BlockSource*               entities,
    ext::SpawnBehaviour const& mob,
    std::string_view           category,
    double                     x,
    double                     y,
    double                     z
) {
    auto obstruction = mob.obstruction;
    if (obstruction == ext::Obstruction::ByCategory)
        obstruction = waterCategory(category) ? ext::Obstruction::EntitiesOnly : ext::Obstruction::LiquidAndEntities;
    auto box = spawnBox(x, y, z, mob.width, mob.height);
    if (obstruction == ext::Obstruction::LiquidAndEntities && containsAnyLiquid(region, box)) return false;
    if (!entities) return true;
    // CollisionGetter#isUnobstructed: an entity that blocks building whose box overlaps the mob's
    AABB aabb{Vec3{static_cast<float>(box.x0), static_cast<float>(box.y0), static_cast<float>(box.z0)},
              Vec3{static_cast<float>(box.x1), static_cast<float>(box.y1), static_cast<float>(box.z1)}};
    for (Actor* a : entities->fetchEntities(static_cast<Actor const*>(nullptr), aabb, false, false))
        if (blocksBuilding(*a)) return false;
    return true;
}


/// A spawn chosen on a generation thread, done on the server thread.
struct Queued {
    int         dimension;
    float       x, y, z;
    std::string entityId;
    int         chunkX, chunkZ;
};
std::mutex& queueMutex() {
    static std::mutex m;
    return m;
}
std::deque<Queued>& queue() {
    static std::deque<Queued> q;
    return q;
}

/// Mob#isPersistenceRequired / requiresCustomPersistence: Bedrock's persistent component or a name.
bool persistent(Actor& a) { return a.getEntityContext().hasComponent<PersistentComponent>() || !a.getNameTag().empty(); }

} // namespace

std::string detail::bedrockEntityId(std::string_view javaId) {
    // Java 1.21.1 entity ids whose Bedrock entity has another name
    static std::unordered_map<std::string_view, std::string_view> const kRenames{
        {"minecraft:zombified_piglin", "minecraft:zombie_pigman"},
        {"minecraft:evoker", "minecraft:evocation_illager"},
        {"minecraft:end_crystal", "minecraft:ender_crystal"},
        {"minecraft:experience_orb", "minecraft:xp_orb"},
        {"minecraft:eye_of_ender", "minecraft:eye_of_ender_signal"},
        {"minecraft:firework_rocket", "minecraft:fireworks_rocket"},
        {"minecraft:potion", "minecraft:splash_potion"},
        {"minecraft:experience_bottle", "minecraft:xp_bottle"},
        {"minecraft:furnace_minecart", "minecraft:minecart"},
        {"minecraft:spawner_minecart", "minecraft:minecart"},
        {"minecraft:fishing_bobber", "minecraft:fishing_hook"},
        {"minecraft:trident", "minecraft:thrown_trident"},
        {"minecraft:glow_item_frame", "minecraft:glow_frame"},
        {"minecraft:item_frame", "minecraft:frame"},
    };
    std::string id(javaId);
    if (!id.empty() && id.find(':') == std::string::npos) id = "minecraft:" + id;
    if (auto it = kRenames.find(id); it != kRenames.end()) return std::string(it->second);
    return id;
}

// ==== contract ========================================================================================

void registerPlacement(std::string const& entityId, PlacementRule rule, PredicateFn predicate) {
    std::unique_lock lock(state().mutex);
    state().placements[features::detail::normalizeId(entityId)] = {rule, std::move(predicate)};
}

Actor* spawnMob(BlockSource& region, std::string const& entityId, Vec3 const& pos, SpawnReason reason, InitFn init) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    std::string id      = detail::bedrockEntityId(entityId);
    bool        natural = reason == SpawnReason::Natural || reason == SpawnReason::ChunkGeneration;
    bool        spawner = reason == SpawnReason::Spawner;
    Mob*        mob     = level->getSpawner().spawnMob(region, ActorDefinitionIdentifier(id), nullptr, pos, natural, true, spawner);
    // a natural spawn makes Bedrock check its own spawn rules again (a spider's brightness and
    // room, ...): the caller already checked Java's (SpawnPlacements, checkSpawnRules), so the
    // spawn stands as any other
    if (!mob && natural) mob = level->getSpawner().spawnMob(region, ActorDefinitionIdentifier(id), nullptr, pos, false, true, spawner);
    if (!mob) {
        features::detail::warnOnce("spawn " + id, "cannot spawn " + id + (id != entityId ? " (" + entityId + ")" : std::string{}) +
                                                      " (unknown entity, or Bedrock refused the spawn)");
        return nullptr;
    }
    if (init) init(*mob);
    return mob;
}

void setBiomeSpawns(std::string const& biomeId, Table table) {
    std::unique_lock lock(state().mutex);
    state().tables[features::detail::normalizeId(biomeId)] = std::move(table);
}

void onSpecialSpawn(SpecialSpawnFn fn) {
    std::unique_lock lock(state().mutex);
    auto             list = std::make_shared<std::vector<SpecialSpawnFn>>(*state().special);
    list->push_back(std::move(fn));
    state().special = std::move(list);
}

namespace ext {

void setPlacementHeightmap(std::string const& entityId, worldgen::Heightmap heightmap) {
    std::unique_lock lock(state().mutex);
    state().heightmaps[features::detail::normalizeId(entityId)] = heightmap;
}

void registerSpawnBehaviour(std::string const& entityId, SpawnBehaviour behaviour) {
    std::unique_lock lock(state().mutex);
    state().behaviours[features::detail::normalizeId(entityId)] = std::move(behaviour);
}

} // namespace ext

// ==== tables, special spawns ===========================================================================

namespace detail {

Table tableOf(std::string const& biomeId) {
    {
        std::shared_lock lock(state().mutex);
        if (auto it = state().tables.find(biomeId); it != state().tables.end()) return it->second;
        if (auto it = state().jsonTables.find(biomeId); it != state().jsonTables.end()) return it->second;
    }
    // built without the lock (the imported data is immutable), then published once
    Table       t;
    auto const& biomes = features::detail::data().biomes;
    if (auto it = biomes.find(biomeId); it != biomes.end()) {
        t.creatureProbability = it->second.value("creature_spawn_probability", 0.1F);
        for (auto const& [category, list] : fieldObj(it->second, "spawners").items()) t.byCategory[category] = entriesOf(list);
    }
    std::unique_lock lock(state().mutex);
    if (auto it = state().tables.find(biomeId); it != state().tables.end()) return it->second; // registered meanwhile
    return state().jsonTables.try_emplace(biomeId, std::move(t)).first->second;
}

std::vector<SpawnEntry> spawnListAt(BlockSource& region, BlockPos const& pos, std::string const& category, std::string const& biome) {
    std::shared_ptr<std::vector<SpecialSpawnFn> const> special;
    {
        std::shared_lock lock(state().mutex);
        special = state().special;
    }
    for (auto const& fn : *special)
        if (auto list = fn(region, pos, category)) return *list;
    auto t  = tableOf(biome);
    auto it = t.byCategory.find(category);
    return it == t.byCategory.end() ? std::vector<SpawnEntry>{} : it->second;
}

bool spawnRulesDefault(worldgen::Region& region, BlockPos const& pos, ext::WalkTarget walkTarget, std::string_view category, bool generation) {
    return defaultSpawnRules(region, pos, walkTarget, category, generation);
}

bool obstructionDefault(
    worldgen::Region&          region,
    BlockSource*               entities,
    ext::SpawnBehaviour const& mob,
    std::string_view           category,
    double                     x,
    double                     y,
    double                     z
) {
    return defaultObstruction(region, entities, mob, category, x, y, z);
}

bool biomeHasTag(std::string const& biome, std::string const& tag) {
    // gf::tags (F9): TF's biome tags, the units' fragments and the vanilla bridge, which holds the
    // 1.21.1 tags the spawner reads (BiomeTags.REDUCED_WATER_AMBIENT_SPAWNS =
    // minecraft:reduce_water_ambient_spawns: [#minecraft:is_river], is_river: [river, frozen_river])
    return tags::has(tags::Registry::Biome, tag, biome);
}

int worldSurfaceY(BlockSource& region, int x, int z) {
    int minY = region.getMinHeight(), maxY = region.getMaxHeight();
    auto const* lc = region.getChunk(x >> 4, z >> 4);
    if (!lc) return minY - 1;
    // every block above the highest non-air sub-chunk is air: scan down from its top
    int top = std::min(maxY, minY + lc->getSubChunkCountUpToHighestNonAirBlock() * 16);
    for (int y = top - 1; y >= minY; --y)
        if (!region.getBlock(BlockPos{x, y, z}).isAir()) return y;
    return minY - 1;
}

} // namespace detail

// ==== chunk generation spawns ===========================================================================

namespace detail {

int spawnForChunkGeneration(worldgen::Region& region, std::string const& biomeId, int chunkX, int chunkZ, RandomSource& random) {
    Table t  = tableOf(biomeId);
    auto  it = t.byCategory.find("creature");
    if (it == t.byCategory.end() || it->second.empty()) return 0;
    auto const& list = it->second;
    int         i    = chunkX * 16, j = chunkZ * 16;
    // SpawnPlacements#checkSpawnRules gets levelAccessor.getRandom() (the WorldGenRegion's random),
    // never the spawn sequence random
    std::unique_ptr<RandomSource> regionRandom;
    std::vector<Queued>           out;
    while (random.nextFloat() < t.creatureProbability) {
        auto const* data = pick(list, random);
        if (!data) continue;
        int k  = data->minCount + random.nextInt(1 + data->maxCount - data->minCount);
        int l  = i + random.nextInt(16);
        int i1 = j + random.nextInt(16);
        int j1 = l, k1 = i1;
        auto placement = placementOf(data->entityId);
        auto heightmap = heightmapTypeOf(data->entityId);
        auto mob       = behaviourOf(data->entityId);
        for (int l1 = 0; l1 < k; ++l1) {
            bool flag = false;
            for (int i2 = 0; !flag && i2 < 4; ++i2) {
                // NaturalSpawner#getTopNonCollidingPos: the entity's heightmap (the Twilight Forest
                // has no ceiling), then SpawnPlacementType#adjustSpawnPosition (ON_GROUND: one down
                // when the block below is pathfindable)
                int      top = region.height(heightmap, l, i1);
                BlockPos pos{l, top, i1};
                if (placement.rule == PlacementRule::OnGround && pathfindableLand(region, {l, top - 1, i1})) pos.y = top - 1;
                if (positionOk(placement.rule, region, pos)) {
                    auto   f  = static_cast<float>(mob.width); // EntityType#getWidth
                    double d0 = core::mth::clamp(static_cast<double>(l), static_cast<double>(i) + f, static_cast<double>(i) + 16.0 - f);
                    double d1 = core::mth::clamp(static_cast<double>(i1), static_cast<double>(j) + f, static_cast<double>(j) + 16.0 - f);
                    // Java's `continue` skips the position update below: the next try reuses (l, i1)
                    if (!noCollision(region, d0, static_cast<double>(pos.y), d1, mob.width, mob.height)) continue;
                    BlockPos at{core::mth::floor(d0), pos.y, core::mth::floor(d1)};
                    if (placement.predicate) {
                        auto* src = kit::blockSource(region);
                        if (!regionRandom) regionRandom = worldgen::detail::worldGenRegionRandom(region.seed(), region.centerChunkX(), region.centerChunkZ());
                        if (!src || !placement.predicate(*src, data->entityId, at, SpawnReason::ChunkGeneration, *regionRandom)) continue;
                    }
                    (void)random.nextFloat(); // the yaw (Entity#moveTo)
                    // EventHooks#checkSpawnPosition: Mob#checkSpawnRules, then #checkSpawnObstruction
                    // (the WorldGenRegion holds no entities: the mobs this pass placed never obstruct)
                    bool rules = mob.checkSpawnRules ? mob.checkSpawnRules(region, at, SpawnReason::ChunkGeneration)
                                                     : defaultSpawnRules(region, at, mob.walkTarget, "creature", true);
                    bool clear = rules
                              && (mob.checkSpawnObstruction ? mob.checkSpawnObstruction(region, d0, static_cast<double>(pos.y), d1)
                                                            : defaultObstruction(region, nullptr, mob, "creature", d0, static_cast<double>(pos.y), d1));
                    if (clear) {
                        out.push_back({region.dimension(), static_cast<float>(d0), static_cast<float>(pos.y), static_cast<float>(d1), data->entityId, chunkX, chunkZ});
                        flag = true;
                    }
                }
                l += random.nextInt(5) - random.nextInt(5);
                for (i1 += random.nextInt(5) - random.nextInt(5); l < i || l >= i + 16 || i1 < j || i1 >= j + 16;
                     i1 = k1 + random.nextInt(5) - random.nextInt(5)) {
                    l = j1 + random.nextInt(5) - random.nextInt(5);
                }
            }
        }
    }
    int queued = static_cast<int>(out.size());
    if (out.empty() || region.dimension() < 0) return queued; // an in-memory region: nothing to spawn into
    std::lock_guard lock(queueMutex());
    for (auto& q : out) queue().push_back(std::move(q));
    return queued;
}

} // namespace detail

// ==== natural spawner ===================================================================================

namespace {

struct Spawner {
    core::jrand::LegacyRandom random{0};
    std::uint64_t             ticks = 0;

    std::unordered_map<std::string, std::string> categories; ///< entity id -> category (cache, server thread)

    /// The players of the tick (NaturalSpawner uses their feet positions; spectators are skipped).
    struct PlayerInfo {
        Vec3                       feet;
        std::map<std::string, int> counts; ///< LocalMobCapCalculator.MobCounts
    };
    std::vector<PlayerInfo> players;
    std::map<std::string, int> counts; ///< NaturalSpawner.SpawnState#mobCategoryCounts

    /// The category of an entity id: the first table listing it.
    std::string const& categoryOf(std::string const& id, std::vector<std::string> const& biomes) {
        if (auto it = categories.find(id); it != categories.end()) return it->second;
        std::string found;
        for (auto const& b : biomes) {
            for (auto const& [cat, list] : detail::tableOf(b).byCategory)
                for (auto const& e : list)
                    if (found.empty() && e.entityId == id) found = cat;
        }
        return categories.emplace(id, found).first->second;
    }

    /// ChunkMap#playerIsCloseEnoughForSpawning: within 128 blocks of the chunk's center (horizontal).
    static bool closeForSpawning(Vec3 const& p, int cx, int cz) {
        double dx = cx * 16 + 8 - static_cast<double>(p.x), dz = cz * 16 + 8 - static_cast<double>(p.z);
        return dx * dx + dz * dz < 16384.0;
    }

    /// LocalMobCapCalculator#canSpawn: a player near the chunk whose own count is below the cap.
    bool canSpawnLocal(Category const& cat, int cx, int cz) const {
        for (auto const& p : players) {
            if (!closeForSpawning(p.feet, cx, cz)) continue;
            auto it = p.counts.find(cat.name);
            if (it == p.counts.end() || it->second < cat.max) return true;
        }
        return false;
    }

    /// LocalMobCapCalculator#addMob (and SpawnState#afterSpawn's global count).
    void addMob(std::string const& cat, int cx, int cz) {
        ++counts[cat];
        for (auto& p : players)
            if (closeForSpawning(p.feet, cx, cz)) ++p.counts[cat];
    }

    /// NaturalSpawner#mobsAt (detail::spawnListAt).
    std::vector<SpawnEntry> mobsAt(BlockSource& region, BlockPos const& pos, std::string const& category, std::string const& biome) {
        return detail::spawnListAt(region, pos, category, biome);
    }

    void tick() {
        auto level = ll::service::getLevel();
        if (!level || !config::get<worldgen::WorldgenConfig>().naturalSpawning) return;
        auto dimType = world::twilight();
        if (!dimType) return;
        GameRuleId rule;
        rule.mValue = static_cast<int>(GameRules::GameRulesIndex::DoMobSpawning);
        if (!level->getGameRules().getBool(rule, true)) return;
        auto* region = game::regionOf(static_cast<int>(*dimType));
        if (!region) return;
        ++ticks;
        players.clear();
        region->getDimension().forEachPlayer([&](Player& p) {
            if (!p.isSpectator()) players.push_back({p.getFeetPos(), {}});
            return true;
        });
        if (players.empty()) return;
        features::detail::TagCacheScope tagPass; // the fence / wall / door tags of every spawn attempt of the tick
        // MinecraftServer#isSpawningMonsters: no monsters on Peaceful (ServerChunkCache#spawnEnemies)
        bool spawnEnemies = level->getDifficulty() != SharedTypes::Legacy::Difficulty::Peaceful;

        // spawnable chunks: within 8 chunks of a player (DistanceManager#getNaturalSpawnChunkCount)
        std::set<std::pair<int, int>> chunks;
        for (auto const& p : players) {
            int pcx = core::mth::floor(p.feet.x) >> 4, pcz = core::mth::floor(p.feet.z) >> 4;
            for (int dx = -8; dx <= 8; ++dx)
                for (int dz = -8; dz <= 8; ++dz)
                    if (auto* lc = region->getChunk(pcx + dx, pcz + dz); lc && lc->mLoadState->load() == ChunkState::Loaded)
                        chunks.emplace(pcx + dx, pcz + dz);
        }
        if (chunks.empty()) return;

        // NaturalSpawner#createState: the non-persistent mobs per category (globally and near each
        // player) in the loaded part of the dimension
        counts.clear();
        std::set<Actor const*> seen; // an entity near several players counts once
        auto                   possible = worldgen::ext::possibleBiomes();
        for (auto const& p : players) {
            AABB box{Vec3{p.feet.x - 160, static_cast<float>(region->getMinHeight()), p.feet.z - 160}, Vec3{p.feet.x + 160, static_cast<float>(region->getMaxHeight()), p.feet.z + 160}};
            for (Actor* a : region->fetchEntities(static_cast<Actor const*>(nullptr), box, false, false)) {
                if (a->isPlayer() || !seen.insert(a).second) continue;
                if (persistent(*a)) continue; // Mob#isPersistenceRequired / requiresCustomPersistence
                auto cat = categoryOf(a->getTypeName(), possible);
                if (cat.empty()) continue;
                auto feet = a->getFeetPos();
                addMob(cat, core::mth::floor(feet.x) >> 4, core::mth::floor(feet.z) >> 4);
            }
        }
        // NaturalSpawner#getFilteredSpawningCategories, once per tick (the global caps)
        bool                         spawnPersistent = ticks % 400 == 0;
        std::vector<Category const*> filtered;
        for (auto const& cat : kCategories) {
            if (!cat.friendly && !spawnEnemies) continue;
            if (cat.persistent && !spawnPersistent) continue;
            int cap = cat.max * static_cast<int>(chunks.size()) / 289; // MAGIC_NUMBER = 17 * 17
            if (counts[cat.name] < cap) filtered.push_back(&cat);
        }
        if (filtered.empty()) return;
        std::vector<std::pair<int, int>> order(chunks.begin(), chunks.end());
        for (std::size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[static_cast<std::size_t>(random.nextInt(static_cast<int>(i)))]);
        auto biomeSource = worldgen::detail::providers()->biomeSource;
        for (auto const& [cx, cz] : order) {
            // NaturalSpawner#spawnForChunk: SpawnState#canSpawnForCategoryLocal per category
            for (auto const* cat : filtered)
                if (canSpawnLocal(*cat, cx, cz)) spawnCategoryForChunk(*region, *cat, cx, cz, biomeSource);
        }
    }

    /// NaturalSpawner#isRightDistanceToPlayerAndSpawnPoint.
    bool rightDistance(BlockSource& region, BlockPos const& pos, double d2, int cx, int cz) const {
        if (d2 <= 576.0) return false;
        auto level = ll::service::getLevel();
        if (level) {
            // the shared spawn of the world (the Twilight Forest's level data derives the overworld's)
            auto const& spawn = level->getSharedSpawnPos();
            // Bedrock keeps y 32767 while the spawn height is not resolved: compare horizontally then
            double sx = spawn.x + 0.5 - (pos.x + 0.5), sy = spawn.y >= 32767 ? 0.0 : spawn.y + 0.5 - pos.y, sz = spawn.z + 0.5 - (pos.z + 0.5);
            if (sx * sx + sy * sy + sz * sz < 576.0) return false; // closerToCenterThan(.., 24.0)
        }
        if ((pos.x >> 4) == cx && (pos.z >> 4) == cz) return true;
        auto const* lc = region.getChunk(pos.x >> 4, pos.z >> 4); // Level#isNaturalSpawningAllowed
        return lc && lc->mLoadState->load() == ChunkState::Loaded;
    }

    /// NaturalSpawner#spawnCategoryForChunk / #spawnCategoryForPosition.
    void spawnCategoryForChunk(BlockSource& region, Category const& cat, int cx, int cz, worldgen::BiomeSourceFn const& biomeSource) {
        worldgen::detail::SourceRegionAdapter view(
            region,
            cx,
            cz,
            1,
            region.getMinHeight(),
            region.getMaxHeight(),
            worldgen::seed(),
            static_cast<int>(region.getDimensionId()),
            biomeSource
        );
        // getRandomPosWithin: up to ChunkAccess#getHeight(WORLD_SURFACE) + 1
        int x = cx * 16 + random.nextInt(16), z = cz * 16 + random.nextInt(16);
        int top = detail::worldSurfaceY(region, x, z) + 1;
        int y   = random.nextInt(top - region.getMinHeight() + 1) + region.getMinHeight(); // Mth.randomBetweenInclusive
        if (y < region.getMinHeight() + 1) return;
        BlockPos start{x, y, z};
        // BlockState#isRedstoneConductor: a full collision cube, except glass and leaves (never)
        auto const& startBlock = view.getBlock(start);
        if (fullCube(view, start, startBlock) && !kit::isLeaves(startBlock) && kit::javaId(startBlock).find("glass") == std::string::npos) return;
        int j = 0;
        for (int k = 0; k < 3; ++k) {
            int               l = x, i1 = z;
            std::optional<SpawnEntry> data;
            int               k1 = core::mth::ceil(static_cast<double>(random.nextFloat() * 4.0F));
            int               l1 = 0;
            ext::SpawnBehaviour mob;
            for (int i2 = 0; i2 < k1; ++i2) {
                l += random.nextInt(6) - random.nextInt(6);
                i1 += random.nextInt(6) - random.nextInt(6);
                BlockPos pos{l, y, i1};
                double   d0 = l + 0.5, d1 = i1 + 0.5;
                // Level#getNearestPlayer(d0, y, d1, -1, false): the nearest non-spectator
                double d2 = -1.0;
                for (auto const& pl : players) {
                    double d = core::mth::square(pl.feet.x - d0) + core::mth::square(pl.feet.y - y) + core::mth::square(pl.feet.z - d1);
                    if (d2 < 0.0 || d < d2) d2 = d;
                }
                if (d2 < 0.0 || !rightDistance(region, pos, d2, cx, cz)) continue;
                if (!data) {
                    // getRandomSpawnMobAt (REDUCED_WATER_AMBIENT_SPAWNS biomes keep 2% of their fish)
                    auto biome = view.biome(pos);
                    if (std::string_view(cat.name) == "water_ambient" && detail::biomeHasTag(biome, "minecraft:reduce_water_ambient_spawns")
                        && random.nextFloat() < 0.98F)
                        break;
                    auto mobs   = mobsAt(region, pos, cat.name, biome);
                    auto picked = pick(mobs, random);
                    if (!picked) break;
                    data = *picked;
                    mob  = behaviourOf(data->entityId);
                    k1   = data->minCount + random.nextInt(1 + data->maxCount - data->minCount);
                }
                // isValidSpawnPostitionForType
                if (d2 > static_cast<double>(cat.despawnDistance) * cat.despawnDistance && !cat.persistent) continue; // canSpawnFarFromPlayer
                // canSpawnMobAt: the entry is still in the list at this position
                auto here = mobsAt(region, pos, cat.name, view.biome(pos));
                if (std::none_of(here.begin(), here.end(), [&](SpawnEntry const& e) { return sameEntry(e, *data); })) continue;
                auto placement = placementOf(data->entityId);
                if (!positionOk(placement.rule, view, pos)) continue;
                // the Level random, as Java's NaturalSpawner passes level.random to the predicate
                if (placement.predicate && !placement.predicate(region, data->entityId, pos, SpawnReason::Natural, random)) continue;
                if (!noCollision(view, d0, static_cast<double>(y), d1, mob.width, mob.height)) continue;
                (void)random.nextFloat(); // the yaw (Entity#moveTo)
                // isValidPositionForMob: EventHooks#checkSpawnPosition (checkSpawnRules, checkSpawnObstruction)
                bool rules = mob.checkSpawnRules ? mob.checkSpawnRules(view, pos, SpawnReason::Natural)
                                                 : defaultSpawnRules(view, pos, mob.walkTarget, cat.name, false);
                if (!rules) continue;
                bool clear = mob.checkSpawnObstruction ? mob.checkSpawnObstruction(view, d0, y, d1)
                                                       : defaultObstruction(view, &region, mob, cat.name, d0, y, d1);
                if (!clear) continue;
                if (spawnMob(region, data->entityId, Vec3{static_cast<float>(d0), static_cast<float>(y), static_cast<float>(d1)}, SpawnReason::Natural)) {
                    ++j;
                    ++l1;
                    addMob(cat.name, l >> 4, i1 >> 4); // SpawnState#afterSpawn
                    if (j >= mob.maxClusterSize) return;
                    if (mob.maxGroupSizeReached && mob.maxGroupSizeReached(l1)) break;
                }
            }
        }
    }
};

Spawner& spawner() {
    static Spawner s;
    return s;
}

/// Spawns queued chunk-generation mobs whose chunks are loaded (server thread).
void drainQueue() {
    std::deque<Queued> later;
    std::deque<Queued> now;
    {
        std::lock_guard lock(queueMutex());
        now.swap(queue());
    }
    for (auto& q : now) {
        auto* region = game::regionOf(q.dimension);
        if (!region) continue;
        auto* lc = region->getChunk(q.chunkX, q.chunkZ);
        if (!lc || lc->mLoadState->load() != ChunkState::Loaded) {
            later.push_back(std::move(q));
            continue;
        }
        spawnMob(*region, q.entityId, Vec3{q.x, q.y, q.z}, SpawnReason::ChunkGeneration);
    }
    if (later.empty()) return;
    std::lock_guard lock(queueMutex());
    while (later.size() > 4096) later.pop_front(); // chunks that never load again
    for (auto& q : later) queue().push_back(std::move(q));
}

} // namespace

class SpawnModule {
public:
    void install() {
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            drainQueue();
            spawner().tick();
        });
        static bool once = false;
        if (!once) {
            once = true;
            game::db::onWorldClosed([] {
                std::lock_guard lock(queueMutex());
                queue().clear();
                spawner().categories.clear();
                std::unique_lock tables(state().mutex);
                state().jsonTables.clear();
            });
        }
    }
    void uninstall() { ll::event::EventBus::getInstance().removeListener(mTick); }

private:
    ll::event::ListenerPtr mTick;
};

} // namespace gf::spawn

GF_REGISTER_MODULE_ORDERED(gf::spawn::SpawnModule, gf::ModuleOrder::Contracts);
