// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/TFTeleporter.java
//   src/main/java/twilightforest/world/NoReturnTeleporter.java
//   src/main/java/twilightforest/world/TeleporterCache.java
//   src/main/java/twilightforest/util/Restriction.java
//   src/main/java/twilightforest/util/iterators/DiagonalSpiralIterator.java
//   src/main/java/twilightforest/util/iterators/XZQuadrantIterator.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).

#include "feature/dimension/Teleporter.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <mutex>
#include <random>
#include <set>
#include <unordered_map>

#include "nlohmann/json.hpp"

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/chunk/SubChunk.h"
#include "mc/world/level/chunk/SubChunkStorage.h"
#include "mc/world/level/material/Material.h"
#include "mc/world/phys/AABB.h"

#include "core/landmark/Landmark.h"
#include "feature/dimension/DimensionConfig.h"
#include "feature/dimension/Internal.h"
#include "feature/dimension/World.h"
#include "feature/landmark/Landmark.h"
#include "feature/progression/Progression.h"
#include "feature/worldgen/Worldgen.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Main.h"

namespace gf::feature::dimension {

namespace {

// Java's Direction.from2DDataValue: 0 south, 1 west, 2 north, 3 east.
constexpr int kDir2D[4][2] = {
    {0,  1 },
    {-1, 0 },
    {0,  -1},
    {1,  0 }
};
// Direction.Plane.HORIZONTAL: north, east, south, west.
constexpr int kHorizontal[4][2] = {
    {0,  -1},
    {1,  0 },
    {0,  1 },
    {-1, 0 }
};

BlockPos offset(BlockPos const& p, int dx, int dy, int dz) { return BlockPos{p.x + dx, p.y + dy, p.z + dz}; }
BlockPos below(BlockPos const& p) { return offset(p, 0, -1, 0); }
BlockPos above(BlockPos const& p) { return offset(p, 0, 1, 0); }

bool inHeight(BlockSource& r, int y) { return y >= r.getMinHeight() && y < r.getMaxHeight(); }

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
int nextInt(int bound) { return std::uniform_int_distribution<int>(0, bound - 1)(rng()); }

} // namespace

// ---- block predicates (Java BlockState semantics on Bedrock blocks) ----------------------------------------------

bool sturdyTop(BlockSource& region, BlockPos const& pos) {
    if (!inHeight(region, pos.y)) return false;
    return region.getBlock(pos).canProvideFullSupport(1); // Facing::Up
}

bool canFormPortal(Block const& block) {
    return blockTagged("twilightforest:portal/fluid", block.getTypeName())
        || (isPortal(block) && game::intState(block, kOneWayState).value_or(0) != 0);
}

namespace {

bool isGrassOrDirt(Block const& b) { return blockTagged("twilightforest:portal/edge", b.getTypeName()); }
bool isNatureBlock(Block const& b) { return blockTagged("twilightforest:portal/decoration", b.getTypeName()); }

/// Java isEmptyBlock / isAir (out of the build range: void air).
bool isEmpty(BlockSource& r, BlockPos const& p) { return !inHeight(r, p.y) || r.getBlock(p).isAir(); }

/// Java BlockState#canBeReplaced (out of the build range: void air, replaceable).
bool canBeReplaced(BlockSource& r, BlockPos const& p) {
    if (!inHeight(r, p.y)) return true;
    auto const& b = r.getBlock(p);
    return b.isAir() || b.getBlockType().canBeBuiltOver(b, r, p);
}

bool cannotReplace(Block const& b) { return blockTagged("minecraft:features_cannot_replace", b.getTypeName()); }
bool isDirt(Block const& b) { return blockTagged("minecraft:dirt", b.getTypeName()); }
bool isSolid(Block const& b) { return b._isSolid(); }
bool isLiquid(Block const& b) { return b.getMaterial().mLiquid; }

/// The block's collision box in world space (empty AABB: no collision).
AABB collisionBox(BlockSource& r, BlockPos const& p) {
    if (!inHeight(r, p.y)) return AABB{};
    auto const& b     = r.getBlock(p);
    AABB        shape = b.getBlockType().getCollisionShape(b, r, p, {});
    float       vol   = (shape.max.x - shape.min.x) * (shape.max.y - shape.min.y) * (shape.max.z - shape.min.z);
    if (!(vol > 0.0f)) return AABB{};
    // the engine answers in world space; be safe with a local (0..1) answer too
    bool local = shape.max.x <= 1.0001f && shape.min.x >= -0.0001f && (p.x < -1 || p.x > 1);
    if (local) shape = AABB{shape.min + Vec3(p.x, p.y, p.z), shape.max + Vec3(p.x, p.y, p.z)};
    return shape;
}

bool collisionEmpty(BlockSource& r, BlockPos const& p) {
    AABB s = collisionBox(r, p);
    return !(s.max.x > s.min.x && s.max.y > s.min.y && s.max.z > s.min.z);
}

/// Java Level#noCollision(AABB) for blocks.
bool noCollision(BlockSource& r, AABB const& box) {
    int x0 = static_cast<int>(std::floor(box.min.x)), x1 = static_cast<int>(std::floor(box.max.x - 1e-7f));
    int y0 = static_cast<int>(std::floor(box.min.y)), y1 = static_cast<int>(std::floor(box.max.y - 1e-7f));
    int z0 = static_cast<int>(std::floor(box.min.z)), z1 = static_cast<int>(std::floor(box.max.z - 1e-7f));
    for (int y = y0 - 1; y <= y1; ++y) // one below: fences and walls reach 1.5 high
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                AABB s = collisionBox(r, BlockPos{x, y, z});
                if (!(s.max.x > s.min.x)) continue;
                bool hit = s.min.x < box.max.x && s.max.x > box.min.x && s.min.y < box.max.y && s.max.y > box.min.y
                        && s.min.z < box.max.z && s.max.z > box.min.z;
                if (hit) return false;
            }
    return true;
}

std::string currentDimName(BlockSource& r) { return dimensionName(static_cast<int>(r.getDimensionId())); }

long long gameTime() {
    auto level = ll::service::getLevel();
    return level ? static_cast<long long>(level->getTime()) : 0;
}

} // namespace

// ---- TFPortalBlock.recursivelyValidatePortal ---------------------------------------------------------------------------

bool recursivelyValidatePortal(BlockSource& region, BlockPos const& pos, PortalScan& checked, int& size, Block const& poolBlock) {
    int const maxSize = std::max(gf::config::get<DimensionConfig>().maxPortalSize, 4);
    if (++size > maxSize) return false;
    bool isPoolProbablyEnclosed = true;
    for (int i = 0; i < 4 && size <= maxSize; i++) {
        BlockPos positionCheck = offset(pos, kDir2D[i][0], 0, kDir2D[i][1]);
        if (checked.contains(positionCheck)) continue;
        if (!inHeight(region, positionCheck.y) || !region.hasChunksAt(positionCheck, 0, false)) return false;
        Block const& state = region.getBlock(positionCheck);
        if (&state == &poolBlock && sturdyTop(region, below(positionCheck))) {
            checked[positionCheck] = true;
            if (isPoolProbablyEnclosed) isPoolProbablyEnclosed = recursivelyValidatePortal(region, positionCheck, checked, size, poolBlock);
        } else if (isGrassOrDirt(state) && isNatureBlock(region.getBlock(above(positionCheck)))) {
            checked[positionCheck] = false;
        } else {
            return false;
        }
    }
    return isPoolProbablyEnclosed;
}

namespace teleporter {

// ---- TeleporterCache ------------------------------------------------------------------------------------------------------

namespace {

constexpr char kCacheKey[] = "gf:dimension:teleporter_cache";

std::int64_t columnKey(int x, int z) { return (static_cast<std::int64_t>(x) << 32) ^ static_cast<std::uint32_t>(z); }

struct Cache {
    std::mutex                                                                               mutex;
    std::unordered_map<std::string, std::unordered_map<std::int64_t, std::pair<std::pair<int, int>, PortalPosition>>> dest;
    bool                                                                                     dirty = false;
};
Cache& cache() {
    static Cache c;
    return c;
}

} // namespace

void cacheLoad() {
    auto& c = cache();
    std::lock_guard lock(c.mutex);
    c.dest.clear();
    c.dirty = false;
    auto doc = game::db::get(kCacheKey);
    if (!doc || !doc->is_object() || !(*doc)["dest"].is_array()) return;
    for (auto const& d : (*doc)["dest"]) {
        if (!d.is_object() || !d["name"].is_string() || !d["links"].is_array()) continue;
        auto& m = c.dest[d["name"].get<std::string>()];
        for (auto const& link : d["links"]) {
            try {
                int            x = link.at("column").at("x").get<int>();
                int            z = link.at("column").at("z").get<int>();
                auto const&    p = link.at("portal").at("pos");
                PortalPosition pp{BlockPos{p.at(0).get<int>(), p.at(1).get<int>(), p.at(2).get<int>()}, link.at("portal").at("time").get<long long>()};
                m[columnKey(x, z)] = {{x, z}, pp};
            } catch (...) {
                log().warn("dimension: skipped an unreadable teleporter cache entry");
            }
        }
    }
}

void cacheSave() {
    auto& c = cache();
    std::lock_guard lock(c.mutex);
    if (!c.dirty) return;
    // TeleporterCache#save: {"dest": [{"name": dim, "links": [{"column": {x, z}, "portal": {time, pos}}]}]}
    nlohmann::json dcc = nlohmann::json::array();
    for (auto const& [name, map] : c.dest) {
        nlohmann::json links = nlohmann::json::array();
        for (auto const& [k, entry] : map) {
            auto const& [column, pp] = entry;
            links.push_back({
                {"column", {{"x", column.first}, {"z", column.second}}},
                {"portal", {{"time", pp.lastUpdateTime}, {"pos", {pp.pos.x, pp.pos.y, pp.pos.z}}}}
            });
        }
        dcc.push_back({{"name", name}, {"links", std::move(links)}});
    }
    game::db::put(kCacheKey, {{"dest", std::move(dcc)}});
    c.dirty = false;
}

void cacheClear() {
    auto& c = cache();
    std::lock_guard lock(c.mutex);
    c.dest.clear();
    c.dirty = false;
}

std::optional<PortalPosition> cacheGet(std::string const& dimension, int x, int z) {
    auto& c = cache();
    std::lock_guard lock(c.mutex);
    auto it = c.dest.find(dimension);
    if (it == c.dest.end()) return std::nullopt;
    auto e = it->second.find(columnKey(x, z));
    if (e == it->second.end()) return std::nullopt;
    return e->second.second;
}

void cacheAdd(std::string const& dimension, int x, int z, PortalPosition const& position) {
    auto& c = cache();
    std::lock_guard lock(c.mutex);
    c.dest[dimension][columnKey(x, z)] = {{x, z}, position};
    c.dirty                            = true;
}

void cacheRemove(std::string const& dimension, int x, int z) {
    auto& c = cache();
    std::lock_guard lock(c.mutex);
    auto it = c.dest.find(dimension);
    if (it == c.dest.end()) return;
    it->second.erase(columnKey(x, z));
    c.dirty = true;
}

// ---- traveller ------------------------------------------------------------------------------------------------------------

Traveller capture(Actor& actor) {
    Traveller t;
    t.id       = idOf(actor);
    t.isPlayer = actor.isPlayer();
    t.name     = t.isPlayer ? static_cast<Player&>(actor).getRealName() : actor.getTypeName();
    t.srcDim   = static_cast<int>(actor.getDimensionId());
    t.srcExact = actor.getFeetPos();
    t.srcPos   = game::blockPosOf(t.srcExact);
    t.yaw      = actor.getRotation().y;
    t.pitch    = actor.getRotation().x;
    // Entity#getBbWidth / getBbHeight (safePosInColumn)
    AABB const& box = actor.getAABB();
    if (box.max.x > box.min.x && box.max.y > box.min.y) {
        t.width  = box.max.x - box.min.x;
        t.height = box.max.y - box.min.y;
    }
    if (auto* region = game::regionOf(t.srcDim)) {
        // TFTeleporter#placeInExistingPortal (source side): the portal blocks around the entity
        PortalScan blocks;
        blocks[t.srcPos] = true;
        int size         = 0;
        recursivelyValidatePortal(*region, t.srcPos, blocks, size, region->getBlock(t.srcPos));
        for (auto const& [p, isPool] : blocks)
            if (isPool) t.srcPortalBlocks.push_back(p);
        // cacheNewPortalCoords: the source portal to come back to
        t.srcExit = getPortalPosition(*region, t.srcPos);
    }
    return t;
}

// ---- TFTeleporter -----------------------------------------------------------------------------------------------------------

namespace {

/// getScanHeight: min(top block, the chunk's highest non-empty section + 15).
int scanHeight(BlockSource& world, int x, int z) {
    int   worldHeight = world.getMaxHeight() - 1;
    int   minY        = world.getMinHeight();
    auto* chunk       = world.getChunk(x >> 4, z >> 4);
    int   sections    = chunk ? chunk->getSubChunkCountUpToHighestNonAirBlock() : 0;
    int   highest     = sections > 0 ? minY + (sections - 1) * 16 : minY;
    return std::min(worldHeight, highest + 15);
}

double distSqr(BlockPos const& a, BlockPos const& b) {
    double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

/// Calls fn(pos) for every portal block in the loaded chunks of the column box, skipping the
/// sub-chunks whose palette has no portal block.
void forEachPortalBlock(BlockSource& world, int x0, int z0, int x1, int z1, std::function<void(BlockPos const&)> const& fn) {
    int                         minY = world.getMinHeight(), maxY = world.getMaxHeight();
    std::set<BlockPos, PosLess> seen;
    for (int cx = x0 >> 4; cx <= (x1 >> 4); ++cx) {
        for (int cz = z0 >> 4; cz <= (z1 >> 4); ++cz) {
            if (!world.hasChunksAt(BlockPos{cx * 16, minY, cz * 16}, 0, false)) continue;
            auto* chunk = world.getChunk(cx, cz);
            if (!chunk) continue;
            auto& subChunks = *chunk->mSubChunks;
            for (std::size_t i = 0; i < subChunks.size(); ++i) {
                auto& sub     = subChunks[i];
                auto const& storage = sub.mBlocks[0];
                if (!storage) continue;
                if (!storage->hasAnyElementMatchingFilterInPalette([](Block const& b) { return isPortal(b); })) continue;
                // the i-th sub-chunk from the bottom; its own index is y >> 4 (scan both if they differ)
                int bases[2] = {minY + static_cast<int>(i) * 16, static_cast<int>(sub.mAbsoluteIndex) * 16};
                for (int b = 0; b < 2; ++b) {
                    if (b == 1 && bases[1] == bases[0]) break;
                    int baseY = bases[b];
                    for (int y = std::max(baseY, minY); y < std::min(baseY + 16, maxY); ++y)
                        for (int lz = 0; lz < 16; ++lz) {
                            int z = cz * 16 + lz;
                            if (z < z0 || z > z1) continue;
                            for (int lx = 0; lx < 16; ++lx) {
                                int x = cx * 16 + lx;
                                if (x < x0 || x > x1) continue;
                                BlockPos p{x, y, z};
                                if (isPortal(world.getBlock(p)) && seen.insert(p).second) fn(p);
                            }
                        }
                }
            }
        }
    }
}

bool isPortalAt(BlockSource& world, BlockPos const& pos) { return inHeight(world, pos.y) && isPortal(world.getBlock(pos)); }

/// getBoundaryPositions + checkAdjacent: the walkable blocks next to the portal.
void checkAdjacent(BlockSource& world, BlockPos const& pos, std::map<BlockPos, bool, PosLess>& checked, std::vector<BlockPos>& result) {
    for (auto const& d : kHorizontal) {
        BlockPos off = offset(pos, d[0], 0, d[1]);
        if (checked.contains(off)) continue;
        checked[off] = true;
        if (isPortalAt(world, off)) {
            checkAdjacent(world, off, checked, result);
        } else if (inHeight(world, off.y) && world.getBlock(off).canProvideFullSupport(1) && collisionEmpty(world, above(off))) {
            result.push_back(off);
        }
    }
}

std::vector<BlockPos> boundaryPositions(BlockSource& world, BlockPos const& start) {
    std::map<BlockPos, bool, PosLess> checked;
    std::vector<BlockPos>             result;
    checked[start] = true;
    checkAdjacent(world, start, checked, result);
    return result;
}

double yFactor(BlockSource& world) { return static_cast<int>(world.getDimensionId()) == 0 ? 2.0 : 0.5; }

using Predicate = std::function<bool(BlockPos const&)>;

std::optional<BlockPos> findPortalCoords(BlockSource& world, Vec3 const& loc, Predicate const& predicate, bool makePortalInAir = false) {
    double yF      = yFactor(world);
    int    entityX = static_cast<int>(std::floor(loc.x));
    int    entityZ = static_cast<int>(std::floor(loc.z));
    int    minY    = world.getMinHeight();

    double                  spotWeight = -1.0;
    std::optional<BlockPos> spot;
    int const               range = 16;
    for (int rx = entityX - range; rx <= entityX + range; rx++) {
        double xWeight = (rx + 0.5) - loc.x;
        for (int rz = entityZ - range; rz <= entityZ + range; rz++) {
            double zWeight = (rz + 0.5) - loc.z;
            for (int ry = scanHeight(world, rx, rz); ry >= minY; ry--) {
                BlockPos pos{rx, ry, rz};
                if (!makePortalInAir && !isEmpty(world, pos)) continue;
                if (makePortalInAir) {
                    while (ry > minY && isEmpty(world, BlockPos{rx, ry - 1, rz}) && predicate(BlockPos{rx, ry - 1, rz})) ry--;
                    pos = BlockPos{rx, ry, rz};
                } else {
                    // Java leaves `pos` on the first non-empty block below the air (the "in ground" pos)
                    while (ry > minY && isEmpty(world, (pos = BlockPos{rx, ry - 1, rz}))) ry--;
                }
                double yWeight    = (ry + 0.5) - loc.y * yF;
                double rPosWeight = xWeight * xWeight + yWeight * yWeight + zWeight * zWeight;
                if (spotWeight < 0.0 || rPosWeight < spotWeight) {
                    if (predicate(pos)) {
                        spotWeight = rPosWeight;
                        spot       = pos;
                    }
                }
            }
        }
    }
    return spot;
}

/// The 4x4x6 site test of isIdealForPortal / isOkayForPortal / isOkayForFallbackPortal.
template <class Layer0>
bool siteOk(BlockSource& world, BlockPos const& pos, Layer0&& layer0) {
    for (int potentialZ = 0; potentialZ < 4; potentialZ++)
        for (int potentialX = 0; potentialX < 4; potentialX++)
            for (int potentialY = 0; potentialY < 6; potentialY++) {
                BlockPos tPos = offset(pos, potentialX - 1, potentialY, potentialZ - 1);
                if (!inHeight(world, tPos.y)) {
                    if (potentialY == 0 && !layer0(nullptr, tPos)) return false; // void air
                    continue;
                }
                Block const& state = world.getBlock(tPos);
                if (cannotReplace(state)) return false;
                if (potentialY == 0 && !layer0(&state, tPos)) return false;
                if (potentialY >= 1 && !canBeReplaced(world, tPos)) return false;
            }
    return true;
}

bool isIdealForPortal(BlockSource& world, BlockPos const& pos) {
    return siteOk(world, pos, [](Block const* s, BlockPos const&) { return s && isDirt(*s); });
}
bool isOkayForPortal(BlockSource& world, BlockPos const& pos) {
    return siteOk(world, pos, [](Block const* s, BlockPos const&) { return s && (isSolid(*s) || isLiquid(*s)); });
}
bool isOkayForFallbackPortal(BlockSource& world, BlockPos const& pos) {
    return siteOk(world, pos, [](Block const*, BlockPos const&) { return true; });
}

/// A random member of portal/generated_decoration (nullptr: none resolves, no decoration).
Block const* randNatureBlock() {
    auto members = blockTagMembers("twilightforest:portal/generated_decoration");
    std::vector<Block const*> blocks;
    for (auto const& id : members)
        if (auto const* b = game::blockWith(id)) blocks.push_back(b);
    if (!blocks.empty()) return blocks[nextInt(static_cast<int>(blocks.size()))];
    return game::blockWith("minecraft:short_grass");
}

void cacheNewPortalCoords(Traveller const& t, BlockPos const& pos) {
    // src/dest is backwards logic because we're caching the opposite direction
    if (!t.srcExit) return;
    auto           src = dimensionName(t.srcDim);
    PortalPosition back{*t.srcExit, gameTime()};
    log().debug("dimension: caching the way back to {} {} {}", t.srcExit->x, t.srcExit->y, t.srcExit->z);
    cacheAdd(src, pos.x, pos.z, back);
    cacheAdd(src, pos.x, pos.z + 1, back);
    cacheAdd(src, pos.x + 1, pos.z, back);
    cacheAdd(src, pos.x + 1, pos.z + 1, back);
}

// ---- isSafeAround ---------------------------------------------------------------------------------------------------------------

/// Restriction#isBiomeSafeFor: the biome's restriction (TF data twilight/restrictions/<biome>.json,
/// imported into the mod's data directory) lists advancements the player must have.
std::vector<std::string> restrictionAdvancements(std::string const& biomeId) {
    static std::mutex                                                mutex;
    static std::unordered_map<std::string, std::vector<std::string>> cacheMap;
    std::lock_guard                                                  lock(mutex);
    if (auto it = cacheMap.find(biomeId); it != cacheMap.end()) return it->second;
    std::vector<std::string> advancements;
    auto                     colon = biomeId.find(':');
    std::string              ns    = colon == std::string::npos ? "minecraft" : biomeId.substr(0, colon);
    std::string              path  = colon == std::string::npos ? biomeId : biomeId.substr(colon + 1);
    auto file = Mod::getInstance().getSelf().getModDir() / "data" / ns / "twilight" / "restrictions" / (path + ".json");
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) {
        try {
            std::ifstream in(file);
            auto          doc = nlohmann::json::parse(in);
            for (auto const& a : doc.at("advancements")) advancements.push_back(a.get<std::string>());
        } catch (...) {
            log().warn("dimension: unreadable restriction {}", file.string());
        }
    }
    cacheMap[biomeId] = advancements;
    return advancements;
}

bool biomeUnsafe(BlockSource&, BlockPos const& pos, Player* entity) {
    if (!entity) return false; // Restriction.getRestrictionForBiome: players only
    auto biome = worldgen::biomeAt(pos);
    if (biome.empty()) return false;
    for (auto const& adv : restrictionAdvancements(biome))
        if (!progression::has(*entity, adv)) return true;
    return false;
}

bool isOutsideBorder(BlockPos const& pos) {
    constexpr int kBorder = 29999984; // Bedrock has no world border: the vanilla limits
    return pos.x < -kBorder || pos.x >= kBorder || pos.z < -kBorder || pos.z >= kBorder;
}

bool posOverlapsRestrictedStructureChunk(BlockSource& world, BlockPos const& pos) {
    return !landmark::startsIntersecting(static_cast<int>(world.getDimensionId()), landmark::ChunkPos{pos.x >> 4, pos.z >> 4}).empty();
}

bool isUnsafe(BlockSource& world, BlockPos const& pos, Player* entity, bool checkProgression) {
    if (!world::isTwilight(world.getDimensionId())) return false;
    if (isOutsideBorder(pos)) return true;
    if (checkProgression && biomeUnsafe(world, pos, entity)) return true;
    return posOverlapsRestrictedStructureChunk(world, pos);
}

/// scanIntoSafeBiomes: a 9x9 grid of biome cells around `pos` (landmark centers), each searched
/// in a 17x17 chunk grid.
std::optional<BlockPos> scanIntoSafeBiomes(BlockSource& level, BlockPos const& pos, Player* entity, bool checkProgression) {
    // DiagonalSpiralIterator(pos.x >> 4, pos.z >> 4, false, 128, 16, getNearestCenterXZ)
    int const xConstant = pos.x >> 4, zConstant = pos.z >> 4, radius = 128 / 16, spacing = 16;
    int       offsetN = 0, advance = 0, side = 0; // side: 0 north, 1 east, 2 south, 3 west (clockwise)
    constexpr int kStep[4][2] = {
        {0,  -1},
        {1,  0 },
        {0,  1 },
        {-1, 0 }
    };
    while (offsetN <= radius * 2) {
        int u = offsetN - advance, v = advance;
        int cw = (side + 1) & 3;
        int cx = xConstant + (kStep[side][0] * u + kStep[cw][0] * v) * spacing;
        int cz = zConstant + (kStep[side][1] * u + kStep[cw][1] * v) * spacing;
        // advancePos
        if (offsetN == 0) {
            offsetN = 1;
            advance = 0;
        } else {
            advance++;
            int advanceLimit = std::min(offsetN, radius + 1);
            if (advance >= advanceLimit) {
                side = (side + 1) & 3;
                if (side == 0) offsetN++;
                advance = 0;
                if (offsetN > radius) advance = offsetN - radius;
            }
        }
        auto     center = core::landmark::nearestCenter(cx, cz);
        BlockPos biomeCenter{center.x, 0, center.z};
        if (checkProgression && biomeUnsafe(level, biomeCenter, entity)) continue;
        // XZQuadrantIterator(center.x, center.z, true, 128, 16, (x, z) -> (x, 4, z))
        int const qRadius = 128;
        int       qx = 0, qz = 1, cardinal = 0;
        while (qx + 1 <= qRadius) {
            int gx = 0, gz = 0;
            switch (cardinal) {
            case 0:
                cardinal++, gx = qz * 16, gz = -qx * 16;
                break;
            case 1:
                cardinal++, gx = -qx * 16, gz = -qz * 16;
                break;
            case 2:
                cardinal++, gx = -qz * 16, gz = qx * 16;
                break;
            default:
                cardinal = 0, gx = qx * 16, gz = qz * 16;
                if (qx + 1 <= qRadius) {
                    if (qz < qRadius) qz++;
                    else qz = 1, qx++;
                }
                break;
            }
            BlockPos posInBiome{biomeCenter.x + gx, 4, biomeCenter.z + gz};
            if (isSafeAround(level, posInBiome, entity, checkProgression)) return posInBiome;
        }
    }
    return std::nullopt;
}

} // namespace

bool isSafeAround(BlockSource& region, BlockPos const& pos, Player* entity, bool checkProgression) {
    if (isUnsafe(region, pos, entity, checkProgression)) return false;
    for (auto const& d : kHorizontal)
        if (isUnsafe(region, offset(pos, d[0] * 16, 0, d[1] * 16), entity, checkProgression)) return false;
    return true;
}

int heightmap(BlockSource& region, int x, int z) {
    if (!region.hasChunksAt(BlockPos{x, region.getMinHeight(), z}, 0, false)) return region.getMinHeight();
    return region.getAboveTopSolidBlock(x, z, true, false);
}

bool areaLoaded(BlockSource& region, BlockPos const& pos, int radius) {
    BlockPos p{pos.x, std::clamp(pos.y, static_cast<int>(region.getMinHeight()), region.getMaxHeight() - 1), pos.z};
    return region.hasChunksAt(p, radius, false);
}

double horizontalScale(int dest) {
    double scale = world::coordinateScale();
    return world::isTwilight(DimensionType(dest)) ? 1.0 / scale : scale;
}

std::vector<BlockPos> portalBlocksNear(BlockSource& region, BlockPos const& center, int radius) {
    std::vector<BlockPos> out;
    forEachPortalBlock(region, center.x - radius, center.z - radius, center.x + radius, center.z + radius, [&](BlockPos const& p) {
        if (std::abs(p.y - center.y) <= radius) out.push_back(p);
    });
    return out;
}

std::vector<BlockPos> portalBlocksInChunk(BlockSource& region, int cx, int cz) {
    std::vector<BlockPos> out;
    forEachPortalBlock(region, cx * 16, cz * 16, cx * 16 + 15, cz * 16 + 15, [&](BlockPos const& p) { out.push_back(p); });
    return out;
}

std::optional<BlockPos> getPortalPosition(BlockSource& region, BlockPos const& pos) {
    int const radius = 200; // scan radius up to 200
    double    best   = std::numeric_limits<double>::max();
    std::optional<BlockPos> result;
    forEachPortalBlock(region, pos.x - radius, pos.z - radius, pos.x + radius, pos.z + radius, [&](BlockPos const& p) {
        // only the lowest block of a run of portal blocks counts (Java scans each column top-down
        // from getScanHeight, which is above every non-empty section)
        if (isPortalAt(region, below(p))) return;
        double d = static_cast<float>(distSqr(p, pos));
        // Java keeps the first strictly nearer one in its x, z, top-down scan order
        bool better = d < best
                   || (d == best && result && std::tie(p.x, p.z) < std::tie(result->x, result->z))
                   || (d == best && result && p.x == result->x && p.z == result->z && p.y > result->y);
        if (better) {
            best   = d;
            result = p;
        }
    });
    return result;
}

std::optional<Vec3> placeInExistingPortal(BlockSource& dest, Traveller const& t, BlockPos const& pos) {
    bool                    flag = true;
    std::optional<BlockPos> blockpos;
    auto                    destName = currentDimName(dest);
    // the column of the entity in the source dimension
    if (auto cached = cacheGet(destName, t.srcPos.x, t.srcPos.z)) {
        blockpos = cached->pos;
        cacheAdd(destName, t.srcPos.x, t.srcPos.z, PortalPosition{cached->pos, gameTime()}); // lastUpdateTime
        flag = false;
        log().debug("dimension: using the teleporter cache, validating {} {} {}", blockpos->x, blockpos->y, blockpos->z);
        if (!isPortalAt(dest, *blockpos)) {
            log().debug("dimension: the cached portal is gone, recreating");
            blockpos.reset();
            cacheRemove(destName, t.srcPos.x, t.srcPos.z);
        }
    } else {
        blockpos = getPortalPosition(dest, pos);
    }
    if (!blockpos) return std::nullopt;
    if (flag) {
        for (auto const& b : t.srcPortalBlocks) cacheAdd(destName, b.x, b.z, PortalPosition{*blockpos, gameTime()});
    }
    auto     portalBorder = boundaryPositions(dest, *blockpos);
    BlockPos borderPos    = portalBorder.empty() ? *blockpos : portalBorder[nextInt(static_cast<int>(portalBorder.size()))];
    Vec3     at{borderPos.x + 0.5f, borderPos.y + 1.0f, borderPos.z + 0.5f};
    return safePosInColumn(dest, t, at);
}

BlockPos safeColumn(BlockSource& dest, Traveller const& t, BlockPos const& pos) {
    bool checkProgression = landmark::enforcedProgression();
    auto* entity          = playerById(t.id);
    if (isSafeAround(dest, pos, entity, checkProgression)) {
        log().debug("dimension: portal destination looks safe");
        return pos;
    }
    log().debug("dimension: portal destination looks unsafe, rerouting");
    if (auto newPos = scanIntoSafeBiomes(dest, pos, entity, checkProgression)) return *newPos;
    log().warn("dimension: did not find a safe portal spot");
    return pos;
}

Vec3 safePosInColumn(BlockSource& region, Traveller const& t, Vec3 const& pos) {
    float hw = t.width / 2.0f;
    AABB  box{Vec3{pos.x - hw, pos.y, pos.z - hw}, Vec3{pos.x + hw, pos.y + t.height, pos.z + hw}};
    if (noCollision(region, box)) return pos;
    // Java Math.round: floor(x + 0.5) (std::lround rounds the halves of negative values away from zero)
    auto javaRound = [](double v) { return static_cast<int>(std::floor(v + 0.5)); };
    int  height    = heightmap(region, javaRound(pos.x), javaRound(pos.z));
    return Vec3{pos.x, static_cast<float>(height), pos.z};
}

namespace {
SiteObserver& siteObserver() {
    static SiteObserver o;
    return o;
}
} // namespace

void setSiteObserver(SiteObserver fn) { siteObserver() = std::move(fn); }

BlockPos makePortalAt(BlockSource& world, BlockPos const& pos, bool locked) {
    if (auto const& observer = siteObserver()) observer(world, pos);
    auto const* grass = game::blockWith("minecraft:grass_block");
    auto const* dirt  = game::blockWith("minecraft:dirt");
    auto const* air   = game::blockWith("minecraft:air");
    auto const* portal = portalBlock(locked || !gf::config::get<DimensionConfig>().shouldReturnPortalBeUsable);
    if (!grass || !dirt || !air || !portal) {
        log().error("dimension: cannot build a portal (block {} missing)", portal ? "grass/dirt" : kPortalBlock);
        return pos;
    }
    auto set = [&](BlockPos const& p, Block const& b) {
        if (inHeight(world, p.y)) game::setBlock(world, p, b);
    };
    // grass all around it
    for (auto const& [dx, dz] : std::initializer_list<std::pair<int, int>>{
             {-1, -1}, {0, -1}, {1, -1}, {2, -1}, {-1, 0}, {2, 0}, {-1, 1}, {2, 1}, {-1, 2}, {0, 2}, {1, 2}, {2, 2}
         })
        set(offset(pos, dx, 0, dz), *grass);
    // dirt under it
    for (auto const& p : {below(pos), below(offset(pos, 1, 0, 0)), below(offset(pos, 0, 0, 1)), below(offset(pos, 1, 0, 1))}) {
        if (!inHeight(world, p.y)) continue;
        Block const& state = world.getBlock(p);
        if (isDirt(state) || canBeReplaced(world, p) || state.isAir()) set(p, *dirt);
    }
    // portal in it
    for (auto const& p : {pos, offset(pos, 1, 0, 0), offset(pos, 0, 0, 1), offset(pos, 1, 0, 1)}) set(p, *portal);
    // meh, let's just make a bunch of air over it for 4 squares (Level#removeBlock: a block with a
    // fluid leaves the fluid's own block, so water stays water)
    for (int dx = -1; dx <= 2; dx++)
        for (int dz = -1; dz <= 2; dz++)
            for (int dy = 1; dy <= 5; dy++) {
                BlockPos p = offset(pos, dx, dy, dz);
                if (!inHeight(world, p.y)) continue;
                Block const& b = world.getBlock(p);
                if (b.isAir() || isLiquid(b)) continue;
                Block const& extra = world.getExtraBlock(p);
                if (!extra.isAir() && isLiquid(extra)) {
                    Block const* fluid = &extra; // a waterlogged block: its water takes its place
                    world.setExtraBlock(p, *air, 3);
                    set(p, *fluid);
                } else {
                    set(p, *air);
                }
            }
    // finally, "nature decorations"!
    for (auto const& [dx, dz] : std::initializer_list<std::pair<int, int>>{
             {-1, -1}, {0, -1}, {1, -1}, {2, -1}, {-1, 0}, {2, 0}, {-1, 1}, {2, 1}, {-1, 2}, {0, 2}, {1, 2}, {2, 2}
         })
        if (auto const* deco = randNatureBlock()) set(offset(pos, dx, 1, dz), *deco);
    // (the portal index hears of the new portal blocks through gf::blockevents' onPlaced)
    log().info("dimension: built a {}portal at {} {} {} in {}", locked ? "one-way " : "", pos.x, pos.y, pos.z, currentDimName(world));
    return pos;
}

BlockPos makePortal(BlockSource& world, Traveller const& t, Vec3 const& pos, bool locked) {
    // (the area is loaded by the travelling player before this runs: loadSurroundingArea)
    auto const& name = t.name;
    if (auto spot = findPortalCoords(world, pos, [&](BlockPos const& p) { return isPortalAt(world, p); })) {
        log().debug("dimension: found an existing portal for {} at {} {} {}", name, spot->x, spot->y, spot->z);
        cacheNewPortalCoords(t, *spot);
        return *spot;
    }
    if (auto spot = findPortalCoords(world, pos, [&](BlockPos const& p) { return isIdealForPortal(world, p); })) {
        log().debug("dimension: found an ideal portal spot for {}", name);
        cacheNewPortalCoords(t, makePortalAt(world, *spot, locked));
        return *spot;
    }
    log().debug("dimension: did not find an ideal portal spot, shooting for an okay one for {}", name);
    if (auto spot = findPortalCoords(world, pos, [&](BlockPos const& p) { return isOkayForPortal(world, p); })) {
        cacheNewPortalCoords(t, makePortalAt(world, *spot, locked));
        return *spot;
    }
    log().debug("dimension: did not even find an okay portal spot, making a fallback one for {}", name);
    if (auto spot = findPortalCoords(world, pos, [&](BlockPos const& p) { return isOkayForFallbackPortal(world, p); }, true)) {
        cacheNewPortalCoords(t, makePortalAt(world, *spot, locked));
        return *spot;
    }
    log().debug("dimension: did not even find a fallback portal spot, making a random one for {}", name);
    double   scale = horizontalScale(static_cast<int>(world.getDimensionId()));
    BlockPos scaled{
        static_cast<int>(std::floor(t.srcExact.x * scale)),
        static_cast<int>(std::floor(t.srcExact.y)),
        static_cast<int>(std::floor(t.srcExact.z * scale))
    };
    BlockPos spot{scaled.x, heightmap(world, scaled.x, scaled.z), scaled.z};
    cacheNewPortalCoords(t, makePortalAt(world, spot, locked));
    return spot;
}

Vec3 noReturnPlacePosition(BlockSource& level, Traveller const& t, Vec3 const& pos) {
    auto top = [&](BlockPos const& s) { return safePosInColumn(level, t, Vec3{s.x + 0.5f, s.y + 1.5f, s.z + 0.5f}); };
    if (auto spot = findPortalCoords(level, pos, [&](BlockPos const& p) { return isPortalAt(level, p); })) return top(*spot);
    if (auto spot = findPortalCoords(level, pos, [&](BlockPos const& p) { return isIdealForPortal(level, p); })) return top(*spot);
    if (auto spot = findPortalCoords(level, pos, [&](BlockPos const& p) { return isOkayForPortal(level, p); })) return top(*spot);
    // + 2 to make it above bedrock
    double scale = horizontalScale(static_cast<int>(level.getDimensionId()));
    return safePosInColumn(
        level,
        t,
        Vec3{
            static_cast<float>(t.srcExact.x * scale),
            static_cast<float>(t.srcExact.y * yFactor(level) + 2),
            static_cast<float>(t.srcExact.z * scale)
        }
    );
}

} // namespace teleporter

} // namespace gf::feature::dimension
