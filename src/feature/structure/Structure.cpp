// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/TFStructureComponent.java
//   src/main/java/twilightforest/init/TFStructurePieceTypes.java
//   src/main/java/twilightforest/init/TFStructureProcessors.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Contract "structure": the piece framework (vanilla StructurePiece semantics, which TF's pieces
// extend) and the type / processor / marker registries. Piece-local coordinates map to the world
// through the orientation exactly as StructurePiece#getWorldX/Y/Z; every placement is clipped to
// the chunk box it is given (postProcess runs once per chunk a piece intersects).
#include "feature/structure/Structure.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <memory>
#include <shared_mutex>
#include <string_view>
#include <vector>

#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockProperty.h"

#include "feature/structure/Internal.h"
#include "mod/Log.h"

namespace gf::structure {

namespace {

struct Registries {
    std::shared_mutex                    mutex;
    std::map<std::string, Factory>       pieces;
    std::map<std::string, ProcessorFn>   processors;
    std::map<std::string, MarkerFn>      markers;
};
Registries& reg() {
    static Registries r;
    return r;
}

/// Vanilla StructurePiece#reorient (chests facing open air), with isSolidRender approximated by
/// full cube shapes.
bool solidRender(Block const& b) { return !b.isAir() && !blocks::isLiquid(b) && b.hasProperty(BlockProperty::CubeShaped); }

Orientation reorientChest(Region& region, BlockPos const& pos, Orientation current) {
    std::optional<Orientation> direction;
    bool                       ambiguous = false;
    for (auto d : {Orientation::North, Orientation::East, Orientation::South, Orientation::West}) {
        BlockPos     p{pos.x + stepX(d), pos.y, pos.z + stepZ(d)};
        Block const& at = region.getBlock(p);
        if (blocks::is(at, "minecraft:chest")) return current;
        if (solidRender(at)) {
            if (direction) {
                ambiguous = true;
                break;
            }
            direction = d;
        }
    }
    if (direction && !ambiguous) return opposite(*direction);
    Orientation d2 = current;
    auto        at = [&](Orientation d) -> Block const& { return region.getBlock(BlockPos{pos.x + stepX(d), pos.y, pos.z + stepZ(d)}); };
    if (solidRender(at(d2))) d2 = opposite(d2);
    if (solidRender(at(d2))) d2 = clockwise(d2);
    if (solidRender(at(d2))) d2 = opposite(d2);
    return d2;
}

} // namespace

// ---- the region's random ------------------------------------------------------------------------------

namespace {
struct RegionRandom {
    Region const* region = nullptr;
    int           cx = 0, cz = 0, dim = 0;
    std::int64_t  seed = 0;
    std::unique_ptr<JavaRandom> random;
};
/// The regions this thread decorates lately (a generator may interleave a few): each keeps its
/// own random until it drops out of the list.
thread_local std::vector<RegionRandom> tRegionRandoms;
constexpr std::size_t                 kRegionRandoms = 8;
} // namespace

RandomSource& regionRandom(Region& region) {
    // WorldGenRegion's random is made with the region (seeded at its center chunk) and shared by
    // everything decorating it
    auto& list = tRegionRandoms;
    int   cx = region.centerChunkX(), cz = region.centerChunkZ(), dim = region.dimension();
    auto  seed = region.seed();
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->region != &region || it->cx != cx || it->cz != cz || it->dim != dim || it->seed != seed) continue;
        if (it != list.begin()) std::rotate(list.begin(), it, it + 1); // most recent first
        return *list.front().random;
    }
    if (list.size() >= kRegionRandoms) list.pop_back();
    list.insert(list.begin(), RegionRandom{&region, cx, cz, dim, seed, std::make_unique<JavaRandom>(seed ^ pure::mthGetSeed(cx * 16, 0, cz * 16))});
    return *list.front().random;
}

void resetRegionRandom(Region const* region) {
    std::erase_if(tRegionRandoms, [&](RegionRandom const& r) { return region == nullptr || r.region == region; });
}

// ---- Piece --------------------------------------------------------------------------------------------

void Piece::addChildren(Piece&, std::vector<std::unique_ptr<Piece>>&, RandomSource&) {}

Json Piece::save() const {
    Json j  = Json::object();
    j["id"] = typeId;
    j["BB"] = boxToJson(bbox);
    j["O"]  = hasOrientation(orientation) ? data2D(orientation) : -1;
    j["GD"] = genDepth;
    j["rotation"] = rotationIndex(rotation);
    j["mirror"]   = static_cast<int>(mirror);
    return j;
}

void Piece::load(Json const& data) {
    if (data.contains("id") && data["id"].is_string()) typeId = data["id"].get<std::string>();
    if (data.contains("BB")) bbox = boxFromJson(data["BB"]);
    genDepth = data.value("GD", genDepth);
    int o    = data.value("O", -1);
    // StructurePiece(type, tag): setOrientation(O == -1 ? null : from2DDataValue(O))
    if (o < 0) {
        orientation = kNoOrientation;
        rotation    = Rotation::None;
        mirror      = Mirror::None;
    } else {
        orientation = from2D(o);
        switch (orientation) {
        case Orientation::South:
            mirror   = Mirror::LeftRight;
            rotation = Rotation::None;
            break;
        case Orientation::West:
            mirror   = Mirror::LeftRight;
            rotation = Rotation::Clockwise90;
            break;
        case Orientation::East:
            mirror   = Mirror::None;
            rotation = Rotation::Clockwise90;
            break;
        default:
            mirror   = Mirror::None;
            rotation = Rotation::None;
        }
    }
    if (data.contains("rotation")) rotation = rotationFromIndex(data["rotation"].get<int>());
    if (data.contains("mirror")) mirror = static_cast<Mirror>(std::clamp(data["mirror"].get<int>(), 0, 2));
}

int Piece::worldX(int x, int z) const {
    if (!hasOrientation(orientation)) return x;
    switch (orientation) {
    case Orientation::North:
    case Orientation::South:
        return bbox.minX + x;
    case Orientation::West:
        return bbox.maxX - z;
    case Orientation::East:
        return bbox.minX + z;
    }
    return x;
}

int Piece::worldY(int y) const { return hasOrientation(orientation) ? y + bbox.minY : y; }

int Piece::worldZ(int x, int z) const {
    if (!hasOrientation(orientation)) return z;
    switch (orientation) {
    case Orientation::North:
        return bbox.maxZ - z;
    case Orientation::South:
        return bbox.minZ + z;
    case Orientation::West:
    case Orientation::East:
        return bbox.minZ + x;
    }
    return z;
}

BlockPos Piece::getWorldPos(int x, int y, int z) const { return BlockPos{worldX(x, z), worldY(y), worldZ(x, z)}; }

// StructurePiece#getLocatorPosition: the center of the box.
BlockPos Piece::getLocatorPosition() const { return boxCenter(bbox); }

Block const& Piece::oriented(Block const& block) const {
    Block const* b = &block;
    if (mirror != Mirror::None) b = &blocks::mirror(*b, mirror);
    if (rotation != Rotation::None) b = &blocks::rotate(*b, rotation);
    return *b;
}

void Piece::placeBlock(Region& region, Block const& block, int x, int y, int z, BlockBox const& chunkBox) {
    BlockPos pos = getWorldPos(x, y, z);
    if (!chunkBox.contains(pos.x, pos.y, pos.z)) return;
    region.setBlock(pos, oriented(block), 2);
}

void Piece::placeBlock(Region& region, std::string const& blockId, Json const& states, int x, int y, int z, BlockBox const& chunkBox) {
    auto const* b = blocks::fromJava(blockId, states);
    if (!b) return;
    placeBlock(region, *b, x, y, z, chunkBox);
    // what the Bedrock block cannot carry: Java's waterlogged (the liquid layer) and the plant of
    // a potted_* block (the flower pot's block actor)
    bool waterlogged = false;
    if (states.is_object() && states.contains("waterlogged")) {
        auto const& v = states["waterlogged"];
        waterlogged   = v.is_boolean() ? v.get<bool>() : v.is_string() && v.get<std::string>() == "true";
    }
    auto plant = blocks::pottedPlant(blockId);
    if (!waterlogged && plant.empty()) return;
    BlockPos pos = getWorldPos(x, y, z);
    if (!chunkBox.contains(pos.x, pos.y, pos.z) || blocks::id(region.getBlock(pos)) != blocks::id(*b)) return; // not placed
    if (waterlogged) liquids::setWaterlogged(region, pos, true);
    if (!plant.empty()) blockentities::flowerPot(region, pos, plant);
}

Block const& Piece::getBlock(Region& region, int x, int y, int z, BlockBox const& chunkBox) const {
    BlockPos pos = getWorldPos(x, y, z);
    if (!chunkBox.contains(pos.x, pos.y, pos.z)) return blocks::air();
    return region.getBlock(pos);
}

void Piece::fill(Region& region, BlockBox const& chunkBox, int x0, int y0, int z0, int x1, int y1, int z1, Block const& edge, Block const& inside, bool keepAir) {
    for (int i = y0; i <= y1; i++)
        for (int j = x0; j <= x1; j++)
            for (int k = z0; k <= z1; k++) {
                if (keepAir && getBlock(region, j, i, k, chunkBox).isAir()) continue;
                if (i != y0 && i != y1 && j != x0 && j != x1 && k != z0 && k != z1) placeBlock(region, inside, j, i, k, chunkBox);
                else placeBlock(region, edge, j, i, k, chunkBox);
            }
}

void Piece::fillAir(Region& region, BlockBox const& chunkBox, int x0, int y0, int z0, int x1, int y1, int z1) {
    for (int i = y0; i <= y1; i++)
        for (int j = x0; j <= x1; j++)
            for (int k = z0; k <= z1; k++) placeBlock(region, blocks::air(), j, i, k, chunkBox);
}

void Piece::randomFill(Region& region, BlockBox const& chunkBox, int x0, int y0, int z0, int x1, int y1, int z1, bool alwaysReplace, RandomSource& random,
                       BlockSelector& selector) {
    for (int i = y0; i <= y1; i++)
        for (int j = x0; j <= x1; j++)
            for (int k = z0; k <= z1; k++) {
                if (alwaysReplace && getBlock(region, j, i, k, chunkBox).isAir()) continue;
                bool         wall = i == y0 || i == y1 || j == x0 || j == x1 || k == z0 || k == z1;
                Block const& next = selector.next(random, j, i, k, wall);
                placeBlock(region, next, j, i, k, chunkBox);
            }
}

void Piece::generateMaybeBox(Region& region, BlockBox const& chunkBox, RandomSource& random, float chance, int x0, int y0, int z0, int x1, int y1, int z1,
                             Block const& edge, Block const& inside, bool requireNonAir, bool requireSkylight) {
    for (int i = y0; i <= y1; i++)
        for (int j = x0; j <= x1; j++)
            for (int k = z0; k <= z1; k++) {
                if (random.nextFloat() > chance) continue;
                if (requireNonAir && getBlock(region, j, i, k, chunkBox).isAir()) continue;
                if (requireSkylight) {
                    // StructurePiece#isInterior: the block above lies below the ocean floor heightmap
                    BlockPos above = getWorldPos(j, i + 1, k);
                    if (!chunkBox.contains(above.x, above.y, above.z)) continue;
                    if (above.y >= region.height(worldgen::Heightmap::OceanFloorWg, above.x, above.z)) continue;
                }
                if (i != y0 && i != y1 && j != x0 && j != x1 && k != z0 && k != z1) placeBlock(region, inside, j, i, k, chunkBox);
                else placeBlock(region, edge, j, i, k, chunkBox);
            }
}

void Piece::maybeGenerateBlock(Region& region, BlockBox const& chunkBox, RandomSource& random, float chance, int x, int y, int z, Block const& block) {
    if (random.nextFloat() < chance) placeBlock(region, block, x, y, z, chunkBox);
}

void Piece::fillColumnDown(Region& region, Block const& block, int x, int y, int z, BlockBox const& chunkBox) {
    BlockPos pos = getWorldPos(x, y, z);
    if (!chunkBox.contains(pos.x, pos.y, pos.z)) return;
    while (blocks::replaceableByStructures(region.getBlock(pos)) && pos.y > region.minY() + 1) {
        region.setBlock(pos, block, 2);
        pos.y--;
    }
}

bool Piece::placeChest(Region& region, BlockBox const& chunkBox, RandomSource& random, int x, int y, int z, std::string const& blockId,
                       std::string const& tableId) {
    // StructurePiece#createChest(level, box, random, pos, lootTable, state): nothing when a chest
    // stands there already; a default chest (no state given) faces away from the walls (reorient),
    // any other block keeps its default facing
    BlockPos pos = getWorldPos(x, y, z);
    if (!chunkBox.contains(pos.x, pos.y, pos.z)) return false;
    if (blocks::is(region.getBlock(pos), "minecraft:chest")) return false;
    bool                       defaultChest = blockId.empty() || blockId == "minecraft:chest";
    std::string                id           = defaultChest ? std::string("minecraft:chest") : blockId;
    std::optional<Orientation> facing;
    if (defaultChest) facing = reorientChest(region, pos, Orientation::North);
    blockentities::container(region, pos, id, facing, tableId, random.nextLong());
    return true;
}

bool Piece::placeSpawner(Region& region, BlockBox const& chunkBox, int x, int y, int z, std::string const& entityId) {
    BlockPos pos = getWorldPos(x, y, z);
    if (!chunkBox.contains(pos.x, pos.y, pos.z)) return false;
    // a boss spawner is a block of its own (TF places TFBlocks.*_BOSS_SPAWNER with placeBlock): an
    // id the block registry knows is placed as that block, any other id is the mob of a spawner.
    // Bedrock's block ids that are also mob ids stay mobs.
    static constexpr std::string_view kMobsNamedLikeBlocks[] = {"minecraft:slime", "minecraft:tnt"};
    bool mobId = std::find(std::begin(kMobsNamedLikeBlocks), std::end(kMobsNamedLikeBlocks), entityId) != std::end(kMobsNamedLikeBlocks);
    if (!mobId && blocks::isRegisteredBlock(entityId)) {
        if (auto const* b = blocks::fromJava(entityId)) {
            region.setBlock(pos, *b, 2);
            return true;
        }
    }
    blockentities::spawner(region, pos, entityId, Json::object());
    return true;
}

bool Piece::placeTemplate(Region& region, BlockBox const& chunkBox, std::string const& templateId, BlockPos const& origin, TemplateSettings const& settings) {
    TemplateSettings s = settings;
    BlockBox         clip = chunkBox;
    if (s.clip) {
        auto both = intersection(*s.clip, chunkBox);
        if (!both) return false;
        clip = *both;
    }
    s.clip = clip;
    return templates::place(templateId, region, getWorldPos(origin.x, origin.y, origin.z), s);
}

// ---- registries ---------------------------------------------------------------------------------------

void registerPieceType(std::string const& typeId, Factory factory) {
    std::unique_lock lock(reg().mutex);
    reg().pieces[typeId] = std::move(factory);
}

std::unique_ptr<Piece> loadPiece(Json const& saved) {
    std::string id = saved.value("id", std::string{});
    Factory     factory;
    {
        std::shared_lock lock(reg().mutex);
        auto             it = reg().pieces.find(id);
        if (it != reg().pieces.end()) factory = it->second;
    }
    if (!factory) {
        warnOnce("piece:" + id, "structure: unknown piece type '" + id + "' (piece skipped)");
        return nullptr;
    }
    return factory(saved);
}

void registerProcessor(std::string const& typeId, ProcessorFn fn) {
    std::unique_lock lock(reg().mutex);
    reg().processors[typeId] = std::move(fn);
}

void registerMarkerHandler(std::string const& id, MarkerFn fn) {
    std::unique_lock lock(reg().mutex);
    reg().markers[id] = std::move(fn);
}

ProcessorFn const* processorFor(std::string const& typeId) {
    std::shared_lock lock(reg().mutex);
    auto             it = reg().processors.find(typeId);
    return it == reg().processors.end() ? nullptr : &it->second;
}

MarkerFn const* markerFor(std::string const& id) {
    std::shared_lock lock(reg().mutex);
    auto             it = reg().markers.find(id);
    return it == reg().markers.end() ? nullptr : &it->second;
}

namespace {
template <class M>
std::vector<std::string> keysOf(M const& m) {
    std::vector<std::string> out;
    for (auto const& [k, v] : m) out.push_back(k);
    return out;
}
} // namespace

std::vector<std::string> pieceTypes() {
    std::shared_lock lock(reg().mutex);
    return keysOf(reg().pieces);
}
std::vector<std::string> processorTypes() {
    std::shared_lock lock(reg().mutex);
    return keysOf(reg().processors);
}
std::vector<std::string> markerTypes() {
    std::shared_lock lock(reg().mutex);
    return keysOf(reg().markers);
}

} // namespace gf::structure
