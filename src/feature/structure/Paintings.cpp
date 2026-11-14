// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/util/entities/EntityUtil.java (tryHangPainting, getPaintingOfSize,
//     getPaintingsOfSizeOrSmaller, checkValidPaintingPosition, getEntitiesInAABB)
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichBossRoom.java (placePainting)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Paintings of structures (the old lich tower's walls, the camps' and the lich tower revamp's
// painting markers). The spot check is upstream's: the painting's box must not collide with blocks or
// overlap another hanging entity (during generation: the paintings queued so far, which are what
// upstream finds among a ProtoChunk's entities). A live region also asks the game, which only hangs a
// painting that has a wall behind it.
#include "feature/structure/Region.h"

#include <algorithm>
#include <cctype>

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/tags/Tags.h"

namespace gf::structure::paintings {

namespace {

// minecraft:painting_variant of 1.21.1, sorted by id (a data-driven registry loads in id order)
std::vector<Variant> const& builtinRegistry() {
    static std::vector<Variant> const v{
        {"minecraft:alban", 1, 1},     {"minecraft:aztec", 1, 1},       {"minecraft:aztec2", 1, 1},      {"minecraft:backyard", 3, 4},
        {"minecraft:baroque", 2, 2},   {"minecraft:bomb", 1, 1},        {"minecraft:bouquet", 3, 3},     {"minecraft:burning_skull", 4, 4},
        {"minecraft:bust", 2, 2},      {"minecraft:cavebird", 3, 3},    {"minecraft:changing", 4, 2},    {"minecraft:cotan", 3, 3},
        {"minecraft:courbet", 2, 1},   {"minecraft:creebet", 2, 1},     {"minecraft:donkey_kong", 4, 3}, {"minecraft:earth", 2, 2},
        {"minecraft:endboss", 3, 3},   {"minecraft:fern", 3, 3},        {"minecraft:fighters", 4, 2},    {"minecraft:finding", 4, 2},
        {"minecraft:fire", 2, 2},      {"minecraft:graham", 1, 2},      {"minecraft:humble", 2, 2},      {"minecraft:kebab", 1, 1},
        {"minecraft:lowmist", 4, 2},   {"minecraft:match", 2, 2},       {"minecraft:meditative", 1, 1},  {"minecraft:orb", 4, 4},
        {"minecraft:owlemons", 3, 3},  {"minecraft:passage", 4, 2},     {"minecraft:pigscene", 4, 4},    {"minecraft:plant", 1, 1},
        {"minecraft:pointer", 4, 4},   {"minecraft:pond", 3, 4},        {"minecraft:pool", 2, 1},        {"minecraft:prairie_ride", 1, 2},
        {"minecraft:sea", 2, 1},       {"minecraft:skeleton", 4, 3},    {"minecraft:skull_and_roses", 2, 2}, {"minecraft:stage", 2, 2},
        {"minecraft:sunflowers", 3, 3}, {"minecraft:sunset", 2, 1},     {"minecraft:tides", 3, 3},       {"minecraft:unpacked", 4, 4},
        {"minecraft:void", 2, 2},      {"minecraft:wanderer", 1, 2},    {"minecraft:wasteland", 1, 1},   {"minecraft:water", 2, 2},
        {"minecraft:wind", 2, 2},      {"minecraft:wither", 2, 2},
    };
    return v;
}

// tag contents in their files' order (vanilla data / upstream's datagen output), used when the mod's
// data tree has no copy of the tag
std::vector<std::string> builtinTag(std::string const& id) {
    static std::vector<std::string> const placeable{
        "kebab",    "aztec",    "alban",           "aztec2",      "bomb",       "plant",    "wasteland",  "pool",         "courbet",
        "sea",      "sunset",   "creebet",         "wanderer",    "graham",     "match",    "bust",       "stage",        "void",
        "skull_and_roses", "wither", "fighters",   "pointer",     "pigscene",   "burning_skull", "skeleton", "donkey_kong", "baroque",
        "humble",   "meditative", "prairie_ride",  "unpacked",    "backyard",   "bouquet",  "cavebird",   "changing",     "cotan",
        "endboss",  "fern",     "finding",         "lowmist",     "orb",        "owlemons", "passage",    "pond",         "sunflowers",
        "tides"};
    static std::vector<std::string> const towerBoss{
        "kebab",    "aztec",    "alban",           "aztec2",      "bomb",       "plant",    "wasteland",  "pool",         "courbet",
        "sea",      "sunset",   "creebet",         "wanderer",    "graham",     "match",    "bust",       "stage",        "void",
        "skull_and_roses", "wither", "fighters",   "pointer",     "pigscene",   "burning_skull", "skeleton", "donkey_kong", "baroque",
        "meditative", "prairie_ride", "backyard",  "bouquet",     "cavebird",   "changing", "cotan",      "endboss",      "fern",
        "finding",  "lowmist",  "orb",             "owlemons",    "passage",    "pond",     "sunflowers", "tides"};
    static std::vector<std::string> const tower{
        "kebab",    "aztec",    "alban",           "aztec2",      "bomb",       "plant",    "wasteland",  "pool",         "courbet",
        "sea",      "sunset",   "creebet",         "wanderer",    "graham",     "match",    "bust",       "stage",        "void",
        "skull_and_roses", "wither", "fighters",   "pointer",     "pigscene",   "burning_skull", "skeleton", "donkey_kong", "earth",
        "wind",     "water",    "fire",            "baroque",     "meditative", "prairie_ride", "unpacked", "backyard",   "bouquet",
        "cavebird", "changing", "cotan",           "endboss",     "fern",       "finding",  "lowmist",    "orb",          "owlemons",
        "passage",  "pond",     "sunflowers",      "tides"};
    std::vector<std::string> const* list = nullptr;
    if (id == "minecraft:placeable") list = &placeable;
    else if (id == "gloamingforest:tower_boss_paintings") list = &towerBoss;
    else if (id == "gloamingforest:tower_paintings") list = &tower;
    std::vector<std::string> out;
    if (list)
        for (auto const& s : *list) out.push_back("minecraft:" + s);
    return out;
}

/// A tag file's values, nested tags resolved, in order (`depth` stops cycles).
void tagValues(std::string const& id, std::vector<std::string>& out, int depth) {
    if (depth > 8) return;
    if (auto doc = data::load("tags/painting_variant", id); doc && doc->contains("values")) {
        for (auto const& v : (*doc)["values"]) {
            std::string value = v.is_string() ? v.get<std::string>() : v.is_object() ? v.value("id", std::string{}) : std::string{};
            if (value.empty()) continue;
            if (value[0] == '#') tagValues(value.substr(1), out, depth + 1);
            else out.push_back(value.find(':') == std::string::npos ? "minecraft:" + value : value);
        }
        return;
    }
    auto builtin = builtinTag(id);
    if (!builtin.empty()) {
        out.insert(out.end(), builtin.begin(), builtin.end());
        return;
    }
    // gf::tags knows the members (sorted, not in file order)
    for (auto const& m : tags::members(tags::Registry::PaintingVariant, id)) out.push_back(m);
}

bool isNumeric(std::string const& s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); }); }

/// Java String#split(":") (trailing empty strings removed).
std::vector<std::string> splitColon(std::string const& s) {
    std::vector<std::string> out;
    std::size_t              start = 0;
    while (true) {
        auto p = s.find(':', start);
        if (p == std::string::npos) break;
        out.push_back(s.substr(start, p - start));
        start = p + 1;
    }
    out.push_back(s.substr(start));
    while (out.size() > 1 && out.back().empty()) out.pop_back();
    return out;
}

Orientation counterClockwise(Orientation o) { return static_cast<Orientation>((static_cast<int>(o) + 3) & 3); }

} // namespace

std::vector<Variant> const& registry() { return builtinRegistry(); }

std::optional<Variant> byId(std::string_view id) {
    std::string full(id);
    if (full.find(':') == std::string::npos) full = "minecraft:" + full;
    for (auto const& v : registry())
        if (v.id == full) return v;
    return std::nullopt;
}

std::vector<Variant> tag(std::string const& tagId) {
    std::vector<std::string> ids;
    tagValues(tagId, ids, 0);
    std::vector<Variant> out;
    for (auto const& id : ids)
        if (auto v = byId(id)) out.push_back(*v);
    return out;
}

void boundingBox(BlockPos const& pos, Orientation direction, Variant const& variant, double (&out)[6]) {
    // Painting#calculateBoundingBox: the block's center moved 15/32 back towards the wall, then to
    // the painting's center (half a block for even sizes)
    auto   offset = [](int size) { return size % 2 == 0 ? 0.5 : 0.0; };
    double cx     = pos.x + 0.5 - stepX(direction) * 0.46875;
    double cy     = pos.y + 0.5;
    double cz     = pos.z + 0.5 - stepZ(direction) * 0.46875;
    Orientation ccw = counterClockwise(direction);
    cx += stepX(ccw) * offset(variant.width);
    cz += stepZ(ccw) * offset(variant.width);
    cy += offset(variant.height);
    bool   alongX = direction == Orientation::East || direction == Orientation::West;
    double dx     = alongX ? 0.0625 : variant.width;
    double dy     = variant.height;
    double dz     = alongX ? variant.width : 0.0625;
    out[0]        = cx - dx / 2;
    out[1]        = cy - dy / 2;
    out[2]        = cz - dz / 2;
    out[3]        = cx + dx / 2;
    out[4]        = cy + dy / 2;
    out[5]        = cz + dz / 2;
}

bool tryHang(Region& region, BlockPos const& pos, Orientation direction, std::optional<Variant> const& variant) {
    if (!variant) return false;
    double box[6];
    boundingBox(pos, direction, *variant, box);
    // EntityUtil.checkValidPaintingPosition: world.noCollision(painting, box) - the blocks whose
    // cells overlap the box (strictly) must not stop movement
    for (int x = pure::mthFloor(box[0]); x < pure::mthCeil(box[3]); x++)
        for (int y = pure::mthFloor(box[1]); y < pure::mthCeil(box[4]); y++)
            for (int z = pure::mthFloor(box[2]); z < pure::mthCeil(box[5]); z++)
                if (blocks::blocksMotion(region.getBlock(BlockPos{x, y, z}))) return false;
    // ... and no hanging entity in it: EntityUtil.getEntitiesInAABB reads the entities of
    // ProtoChunks only, the queued paintings here. A loaded chunk is an ImposterProtoChunk without
    // entities, so in a live world upstream sees none of the paintings that hang already either (the
    // game then refuses an overlapping one, which drops as an item).
    if (blockentities::pendingPaintingIntersects(region.dimension(), box)) return false;
    auto motive = variant->id.substr(variant->id.find(':') + 1);
    return blockentities::painting(region, pos, direction, motive, Json::array({box[0], box[1], box[2], box[3], box[4], box[5]}));
}

bool occupied(Region& region, BlockPos const& pos) {
    // EntityUtil.getEntitiesInAABB(world, new AABB(pos)) is not empty: the ProtoChunk entities
    // (structure entities: the queued paintings); a live world's chunks show upstream none
    double box[6] = {static_cast<double>(pos.x), static_cast<double>(pos.y), static_cast<double>(pos.z),
                     pos.x + 1.0,                pos.y + 1.0,                pos.z + 1.0};
    return blockentities::pendingPaintingIntersects(region.dimension(), box);
}

std::optional<Variant> ofSize(RandomSource& random, int width, int height, bool exactMeasurements) {
    std::vector<Variant> valid;
    for (auto const& v : registry()) {
        if (exactMeasurements ? (v.width == width && v.height == height) : (v.width >= width || v.height >= height)) valid.push_back(v);
    }
    if (valid.empty()) return std::nullopt;
    return valid[static_cast<std::size_t>(random.nextInt(static_cast<int>(valid.size())))];
}

std::vector<Variant> ofSizeOrSmaller(std::string const& tagId, int width, int height) {
    std::vector<Variant> out;
    for (auto const& v : tag(tagId))
        if (v.width <= width && v.height <= height) out.push_back(v);
    return out;
}

bool place(BlockPos const& pos, RandomSource& random, int limitTries, std::string const& tagId, Region& region, int width, int height,
           Orientation direction) {
    int  maxArea  = width * height;
    auto fitting  = ofSizeOrSmaller(tagId, width, height);
    // Util.shuffle
    for (int i = static_cast<int>(fitting.size()); i > 1; i--)
        std::swap(fitting[static_cast<std::size_t>(i - 1)], fitting[static_cast<std::size_t>(random.nextInt(i))]);
    for (auto const& v : fitting) {
        int area = v.width * v.height;
        if (random.nextInt(maxArea) <= area) {
            if (tryHang(region, pos, direction, v) || limitTries-- <= 0) return true;
        }
    }
    return false;
}

bool placeLabelled(std::string const& label, BlockPos const& pos, Region& region, RandomSource& random, BlockBox const& chunkBounds, Rotation rotation,
                   int limitTries, int rarityFactor, std::string const& tagId) {
    if (!chunkBounds.contains(pos.x, pos.y, pos.z) || random.nextInt(rarityFactor) != 0) return false;
    auto params = splitColon(label);
    if (params.size() != 3) return false;
    // DirectionUtil.fromStringOrElse(params[0], null); a vertical direction is refused
    auto dir = blocks::parseDirection(params[0]);
    if (!dir || !isNumeric(params[1]) || !isNumeric(params[2])) return false;
    Orientation turned = rotateDir(*dir, rotation);
    return place(pos, random, limitTries, tagId, region, std::stoi(params[1]), std::stoi(params[2]), turned);
}

} // namespace gf::structure::paintings
