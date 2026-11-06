// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/feature/templates/TemplateFeature.java
//   src/main/java/twilightforest/world/components/feature/templates/DruidHutFeature.java
//   src/main/java/twilightforest/world/components/feature/templates/SimpleWellFeature.java
//   src/main/java/twilightforest/world/components/feature/templates/FancyWellFeature.java
//   src/main/java/twilightforest/world/components/feature/templates/GroveRuinsFeature.java
//   src/main/java/twilightforest/world/components/feature/templates/StoneCircleFeature.java
//   src/main/java/twilightforest/world/components/feature/templates/GraveyardFeature.java
//   src/main/java/twilightforest/world/components/feature/config/SwizzleConfig.java
//   src/main/java/twilightforest/util/features/FeatureLogic.java (isBlockOk / isBlockNotOk / isAreaClear)
//   src/main/java/twilightforest/loot/TFLootTables.java (generateLootContainer / generateChestContents)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// TF's template features (docs/spec/questcamps.md 4): the druid hut (and its basement), the simple
// and fancy wells, the grove ruins and the stone circle (TemplateFeature, drawing from the region's
// random), and the graveyard (its own snapping and the feature random). Templates are U19's original
// designs (assets/structures/questcamps/feature/**).

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>
#include <mutex>
#include <utility>

#include "core/questcamps/Questcamps.h"
#include "feature/ext/questcamps/W0Internals.h"
#include "feature/ext/structure/Paintings.h"
#include "feature/ext/structure/Placement.h"
#include "feature/ext/structure/SharedRandom.h"
#include "feature/features/Features.h"
#include "feature/questcamps/Questcamps.h"
#include "mod/Log.h"

namespace gf::feature::questcamps {

namespace st   = gf::structure;
namespace cq   = gf::core::questcamps;
namespace data = gf::feature::item::data;
using st::Mirror;
using st::Orientation;
using st::Rotation;

namespace {

thread_local FeatureReport tReport;

// ---- helpers -------------------------------------------------------------------------------------------------

/// The template standing in for TF's "twilightforest:feature/<path>" (no template definition: the
/// gloamingforest namespace, like the Naga courtyard's templates).
std::string tid(std::string const& path) { return "gloamingforest:feature/" + path; }

cq::Rot toCore(Rotation r) { return static_cast<cq::Rot>(st::rotationIndex(r)); }
cq::Mir toCore(Mirror m) { return static_cast<cq::Mir>(static_cast<int>(m)); }

/// Mirror#mirror(Direction): LEFT_RIGHT swaps north and south, FRONT_BACK east and west.
Orientation mirrorDir(Mirror m, Orientation d) {
    if (m == Mirror::LeftRight && (d == Orientation::North || d == Orientation::South)) return st::opposite(d);
    if (m == Mirror::FrontBack && (d == Orientation::East || d == Orientation::West)) return st::opposite(d);
    return d;
}
/// rotation.rotate(mirror.mirror(d)).
Orientation turned(Rotation r, Mirror m, Orientation d) { return st::rotateDir(mirrorDir(m, d), r); }
BlockPos    relative(BlockPos p, Orientation d, int n = 1) { return BlockPos{p.x + st::stepX(d) * n, p.y, p.z + st::stepZ(d) * n}; }

/// StructureTemplate#getZeroPositionWithTransform(pos, mirror, rotation) with the template's size.
BlockPos zeroPosition(BlockPos const& pos, Mirror mirror, Rotation rotation, int sizeX, int sizeZ) {
    --sizeX;
    --sizeZ;
    int i = mirror == Mirror::FrontBack ? sizeX : 0;
    int j = mirror == Mirror::LeftRight ? sizeZ : 0;
    switch (rotation) {
    case Rotation::Counterclockwise90: return BlockPos{pos.x + j, pos.y, pos.z + sizeX - i};
    case Rotation::Clockwise90: return BlockPos{pos.x + sizeZ - j, pos.y, pos.z + i};
    case Rotation::Clockwise180: return BlockPos{pos.x + sizeX - i, pos.y, pos.z + sizeZ - j};
    default: return BlockPos{pos.x + i, pos.y, pos.z + j};
    }
}

/// StructureTemplate#getSize(rotation).
cq::Vec3i templateSize(st::templates::Template const& t, Rotation r) { return cq::rotatedSize(cq::Vec3i{t.sizeX, t.sizeY, t.sizeZ}, toCore(r)); }

/// FeatureLogic.isBlockNotOk: liquids, bedrock, giant blocks, #twilightforest:clouds, hardened dark leaves.
bool blockNotOk(Block const& b) {
    if (st::blocks::isLiquid(b)) return true;
    auto const& id = st::blocks::id(b);
    if (id == "minecraft:bedrock" || id == "twilightforest:hardened_dark_leaves") return true;
    if (id == "twilightforest:giant_cobblestone" || id == "twilightforest:giant_log" || id == "twilightforest:giant_leaves" || id == "twilightforest:giant_obsidian")
        return true;
    return st::blocks::inTag(b, "twilightforest:clouds");
}
/// FeatureLogic.isBlockOk: BlockState#isSolid.
bool blockOk(Block const& b) { return st::blocks::blocksMotion(b); }

/// offsetToAverageGroundLevel: the column scan, the deviation limit, the averaged y and the
/// clearance test (`graveyard`: Math.round and FeatureLogic.isAreaClear; else (int)(mean + 0.5) and
/// every block canBeReplaced).
bool offsetToAverageGroundLevel(st::Region& region, BlockPos& start, cq::Vec3i size, bool graveyard) {
    auto fail = [](std::string why) {
        tReport.failure = std::move(why);
        return false;
    };
    std::vector<int> heights;
    heights.reserve(static_cast<std::size_t>(std::max(0, size.x * size.z)));
    for (int dx = 0; dx < size.x; dx++) {
        for (int dz = 0; dz < size.z; dz++) {
            int x = start.x + dx, z = start.z + dz;
            int y = region.height(gf::worldgen::Heightmap::MotionBlockingNoLeaves, x, z);
            while (y >= 0) {
                auto const& b = region.getBlock(BlockPos{x, y, z});
                if (blockNotOk(b)) return fail("a liquid / bedrock / giant / cloud block at " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z));
                if (blockOk(b)) break;
                y--;
            }
            if (y < 0) return fail("no ground above y 0 at " + std::to_string(x) + " " + std::to_string(z));
            heights.push_back(y);
        }
    }
    auto ground = cq::averageGround(heights, graveyard);
    if (!ground) return fail("the ground is too uneven (deviation above 2)");
    start.y = ground->baseY;
    BlockPos min{start.x, start.y + ground->maxY - ground->baseY + 1, start.z};
    BlockPos max{start.x + size.x, start.y + size.y, start.z + size.z};
    for (int x = min.x; x <= max.x; x++)
        for (int y = min.y; y <= max.y; y++)
            for (int z = min.z; z <= max.z; z++) {
                auto const& b = region.getBlock(BlockPos{x, y, z});
                if (graveyard) {
                    if (!st::blocks::canBeReplaced(b) && blockOk(b) && !st::blocks::isLiquid(b)) return fail("not clear: " + st::blocks::id(b) + " at " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z));
                } else if (!st::blocks::canBeReplaced(b)) {
                    return fail("not clear: " + st::blocks::id(b) + " at " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z));
                }
            }
    return true;
}

/// TFLootTables.generateLootContainer(world, pos, state, flags, table): the block, then the table
/// with the seed generateChestContents uses (world seed * x + y ^ z).
void lootContainer(st::Region& region, BlockPos const& pos, Block const& state, std::string const& table) {
    region.setBlock(pos, state, 16 | 2);
    std::int64_t seed = static_cast<std::int64_t>(static_cast<std::uint64_t>(region.seed()) * static_cast<std::uint64_t>(static_cast<std::int64_t>(pos.x)) +
                                                  static_cast<std::uint64_t>(static_cast<std::int64_t>(pos.y))) ^
                        static_cast<std::int64_t>(pos.z);
    st::blockentities::lootTable(region, pos, table, seed);
}

std::string dirName(Orientation d) { return st::blocks::directionName(d); }

// ---- SwizzleConfig ---------------------------------------------------------------------------------------------

/// The members of a twilight:wood_palettes tag ("#twilightforest:common") in file order.
std::mutex&                                       paletteMutex() {
    static std::mutex m;
    return m;
}
std::map<std::string, std::vector<std::string>>& paletteCache() {
    static std::map<std::string, std::vector<std::string>> cache;
    return cache;
}
std::vector<std::string> paletteTag(std::string tag) {
    if (!tag.empty() && tag[0] == '#') tag.erase(0, 1);
    std::lock_guard lock(paletteMutex());
    auto&           cache = paletteCache();
    if (auto it = cache.find(tag); it != cache.end()) return it->second;
    std::vector<std::string> out;
    for (auto const& f : data::list("tags/twilight/wood_palettes")) {
        if (f.id() != tag) continue;
        auto doc = data::read(f.path);
        for (auto const& v : doc.value("values", Json::array())) {
            if (v.is_string()) out.push_back(v.get<std::string>());
            else if (v.is_object() && v.contains("id")) out.push_back(v["id"].get<std::string>());
        }
    }
    cache[tag] = out;
    return out;
}

/// SwizzleConfig#buildAddProcessors: the preprocessing rules (one StateTransfiguringProcessor), then
/// one WoodPaletteSwizzle per target palette with a weighted palette tag and a random member of it.
void buildAddProcessors(std::vector<std::string>& processors, st::RandomSource& random, Json const& config) {
    auto rules = config.value("preprocessing_rules", Json::array());
    if (!rules.empty()) processors.push_back(Json{{"processor_type", "twilightforest:state_transfiguring"}, {"rules", rules}}.dump());
    auto choices = config.value("palette_choices", Json::array());
    int  total   = 0;
    for (auto const& c : choices) total += c.value("weight", 1);
    for (auto const& target : paletteTag(config.value("target_palettes", std::string{}))) {
        if (total <= 0) break;
        // WeightedRandomList#getRandom: nextInt(total), then HolderSet#getRandomElement: nextInt(size)
        int         roll = random.nextInt(total);
        std::string chosenTag;
        for (auto const& c : choices) {
            roll -= c.value("weight", 1);
            if (roll < 0) {
                chosenTag = c.value("data", std::string{});
                break;
            }
        }
        auto members = paletteTag(chosenTag);
        if (members.empty()) continue;
        auto replacement = members[static_cast<std::size_t>(random.nextInt(static_cast<int>(members.size())))];
        processors.push_back(
            Json{{"processor_type", "twilightforest:wood_swizzle"}, {"target_palette", target}, {"replacement_palette", replacement}}.dump());
    }
}

// ---- TemplateFeature -------------------------------------------------------------------------------------------

struct TemplateFeature {
    std::function<std::string(st::RandomSource&)>                                                               getTemplate;
    std::function<void(std::vector<std::string>&, st::RandomSource&)>                                           modifySettings;
    std::function<void(st::Region&, BlockPos const&, std::string const&, Rotation, Mirror, st::RandomSource&)> processMarkers;
    std::function<void(st::Region&, st::RandomSource&, Rotation, Mirror, st::TemplateSettings&, BlockPos const&)> postPlacement;
    int                                                                                                         yLevelOffset = 0;
};

/// Places a template and hands its data markers (Java's filterBlocks of STRUCTURE_BLOCK DATA) on.
void placeWithMarkers(TemplateFeature const& f, std::string const& id, st::Region& region, BlockPos const& placementPos, st::TemplateSettings const& settings,
                      st::RandomSource& random) {
    st::ext::placeTemplate(id, region, placementPos, placementPos, settings, random, 2);
    for (auto const& [pos, label] : st::ext::filterDataMarkers(id, placementPos, settings, random)) {
        tReport.markers.push_back(label);
        if (f.processMarkers) f.processMarkers(region, pos, label, settings.rotation, settings.mirror, random);
    }
}

/// TemplateFeature#place.
bool placeTemplateFeature(TemplateFeature const& f, st::Region& region, BlockPos const& origin, st::RandomSource& random) {
    tReport  = FeatureReport{};
    auto id  = f.getTemplate(random);
    auto const* t = st::templates::get(id);
    if (!t) return false;
    Rotation rotation = st::rotationFromIndex(random.nextInt(4));
    Mirror   mirror   = static_cast<Mirror>(random.nextInt(3));
    int      cx = origin.x >> 4, cz = origin.z >> 4;
    st::BlockBox mask{cx << 4, region.minY(), cz << 4, (cx << 4) + 15, region.maxY(), (cz << 4) + 15};
    auto         size = templateSize(*t, rotation);
    if (size.x >= 16 || size.z >= 16) {
        st::warnOnce("questcamps-size:" + id, "questcamps: template " + id + " is wider than 15 blocks; the feature is skipped");
        return false;
    }
    int      dx = random.nextInt(16 - size.x);
    int      dz = random.nextInt(16 - size.z);
    BlockPos start{(cx << 4) + dx, origin.y, (cz << 4) + dz};
    if (!offsetToAverageGroundLevel(region, start, size, false)) return false;
    start.y += f.yLevelOffset;
    BlockPos             placementPos = zeroPosition(start, mirror, rotation, t->sizeX, t->sizeZ);
    st::TemplateSettings settings;
    settings.rotation = rotation;
    settings.mirror   = mirror;
    settings.clip     = mask;
    if (f.modifySettings) f.modifySettings(settings.processors, random);
    tReport.templateId   = id;
    tReport.placementPos = placementPos;
    tReport.rotation     = rotation;
    tReport.mirror       = mirror;
    placeWithMarkers(f, id, region, placementPos, settings, random);
    if (f.postPlacement) f.postPlacement(region, random, rotation, mirror, settings, placementPos);
    return true;
}

// ---- druid hut -------------------------------------------------------------------------------------------------

void druidMarkers(st::Region& region, BlockPos const& pos, std::string const& s, Rotation rotation, Mirror mirror, st::RandomSource& random) {
    if (s == "spawner") {
        // removeBlock and the spawner both succeed inside the region: a skeleton druid spawner
        region.setBlock(pos, st::blocks::air(), 3);
        st::blockentities::spawner(region, pos, "twilightforest:skeleton_druid", Json::object());
        return;
    }
    if (s.starts_with("loot") && s.size() >= 6) {
        region.setBlock(pos, st::blocks::air(), 3);
        bool        trapped = s.back() == 'T';
        std::string type    = "single";
        if (s[5] == 'L') type = mirror != Mirror::None ? "right" : "left";
        else if (s[5] == 'R') type = mirror != Mirror::None ? "left" : "right";
        Orientation d = s[4] == 'W' ? Orientation::West : s[4] == 'E' ? Orientation::East : s[4] == 'S' ? Orientation::South : Orientation::North;
        auto const& chest = st::blocks::java(trapped ? "minecraft:trapped_chest" : "minecraft:chest",
                                             Json{{"facing", dirName(turned(rotation, mirror, d))}, {"type", type}, {"waterlogged", "false"}});
        lootContainer(region, pos, chest, s.back() == 'J' ? "twilightforest:chests/hut_junk" : "twilightforest:chests/basement");
        tReport.chests++;
        return;
    }
    if (s.starts_with("barrel")) {
        region.setBlock(pos, st::blocks::air(), 3);
        char        c = s.size() > 6 ? s[6] : 'U';
        std::string facing;
        switch (c) {
        case 'D': facing = "down"; break;
        case 'W': facing = dirName(turned(rotation, mirror, Orientation::West)); break;
        case 'E': facing = dirName(turned(rotation, mirror, Orientation::East)); break;
        case 'N': facing = dirName(turned(rotation, mirror, Orientation::North)); break;
        case 'S': facing = dirName(turned(rotation, mirror, Orientation::South)); break;
        default: facing = "up"; break;
        }
        lootContainer(region, pos, st::blocks::java("minecraft:barrel", Json{{"facing", facing}, {"open", "false"}}), "twilightforest:chests/hut_junk");
        tReport.chests++;
        return;
    }
    if (s.starts_with("painting") && s.size() >= 9) {
        region.setBlock(pos, st::blocks::air(), 3);
        char        c = s[8];
        Orientation d = turned(rotation, mirror, c == 'W' ? Orientation::West : c == 'E' ? Orientation::East : c == 'S' ? Orientation::South : Orientation::North);
        int         width = s.size() >= 10 && std::isdigit(static_cast<unsigned char>(s[9])) ? s[9] - '0' : 1;
        BlockPos    hang  = mirror != Mirror::None ? relative(pos, st::clockwise(d)) : pos;
        (void)st::ext::tryHangPainting(region, hang, d, st::ext::getPaintingOfSize(random, width, width == 2 || width == 4 ? 2 : 1, true));
    }
}

TemplateFeature druidHut(Json const& config) {
    TemplateFeature f;
    f.getTemplate = [](st::RandomSource& random) {
        static char const* const kHuts[] = {"druid_hut/druid_hut", "druid_hut/druid_sideways", "druid_hut/druid_doubledeck"};
        return tid(kHuts[random.nextInt(3)]);
    };
    f.modifySettings = [config](std::vector<std::string>& p, st::RandomSource& random) { buildAddProcessors(p, random, config); };
    f.processMarkers = druidMarkers;
    f.postPlacement  = [config, f2 = TemplateFeature{.processMarkers = druidMarkers}](st::Region& region, st::RandomSource& random, Rotation rotation, Mirror mirror,
                                                                                    st::TemplateSettings& settings, BlockPos const& placementPos) {
        if (!random.nextBoolean()) return;
        static char const* const kBasements[] = {"basement_study", "basement_shelves", "basement_gallery"};
        std::string              base         = kBasements[random.nextInt(3)];
        std::string              id           = tid("druid_hut/" + base + (random.nextBoolean() ? "_trap" : ""));
        if (!st::templates::get(id)) return;
        BlockPos pos{placementPos.x, placementPos.y - 12, placementPos.z};
        pos = relative(pos, turned(rotation, mirror, Orientation::North));
        pos = relative(pos, turned(rotation, mirror, Orientation::East));
        settings.processors.clear();
        buildAddProcessors(settings.processors, random, config);
        settings.processors.push_back("twilightforest:cobble_variants");
        settings.processors.push_back("twilightforest:stone_brick_variants");
        tReport.basement = id;
        placeWithMarkers(f2, id, region, pos, settings, random);
    };
    return f;
}

// ---- wells -----------------------------------------------------------------------------------------------------

void simpleWellMarkers(st::Region& region, BlockPos const& pos, std::string const& s, Rotation rotation, Mirror mirror, st::RandomSource& random) {
    if (!s.starts_with("loot")) return;
    if (random.nextBoolean()) {
        region.setBlock(pos, st::blocks::java(random.nextBoolean() ? "minecraft:cobblestone" : "minecraft:mossy_cobblestone"), 16 | 2);
        return;
    }
    region.setBlock(pos, st::blocks::air(), 3);
    char        c   = s.size() > 4 ? s[4] : 'N';
    Orientation dir = turned(rotation, mirror, c == 'W' ? Orientation::West : c == 'E' ? Orientation::East : c == 'S' ? Orientation::South : Orientation::North);
    lootContainer(region, pos, st::blocks::java("minecraft:barrel", Json{{"facing", dirName(dir)}, {"open", "false"}}), "twilightforest:well");
    tReport.chests++;
    if (random.nextBoolean()) return;
    region.setBlock(relative(pos, dir), st::blocks::java("minecraft:hopper", Json{{"facing", dirName(st::opposite(dir))}, {"enabled", "true"}}), 16 | 2);
}

void fancyWellMarkers(st::Region& region, BlockPos const& pos, std::string const& s, Rotation, Mirror, st::RandomSource&) {
    if (!s.starts_with("loot")) return;
    region.setBlock(pos, st::blocks::air(), 3);
    lootContainer(region, pos, st::blocks::java("minecraft:barrel", Json{{"facing", "up"}, {"open", "false"}}), "twilightforest:fancy_well");
    tReport.chests++;
}

TemplateFeature well(Json const& config, bool fancy) {
    TemplateFeature f;
    std::string     top    = tid(fancy ? "well/fancy_well_top" : "well/simple_well_top");
    std::string     bottom = tid(fancy ? "well/fancy_well_bottom" : "well/simple_well_bottom");
    f.getTemplate          = [top](st::RandomSource&) { return top; };
    f.yLevelOffset         = fancy ? 0 : 1;
    f.modifySettings       = [config](std::vector<std::string>& p, st::RandomSource& random) { buildAddProcessors(p, random, config); };
    f.processMarkers       = fancy ? fancyWellMarkers : simpleWellMarkers;
    f.postPlacement        = [bottom, markers = f.processMarkers](st::Region& region, st::RandomSource& random, Rotation, Mirror, st::TemplateSettings& settings,
                                                           BlockPos const& placementPos) {
        auto const* t = st::templates::get(bottom);
        if (!t) return;
        BlockPos pos{placementPos.x, placementPos.y - t->sizeY, placementPos.z};
        settings.processors.push_back("twilightforest:smart_grass");
        placeWithMarkers(TemplateFeature{.processMarkers = markers}, bottom, region, pos, settings, random);
    };
    return f;
}

// ---- ruins -----------------------------------------------------------------------------------------------------

TemplateFeature groveRuins() {
    TemplateFeature f;
    f.getTemplate    = [](st::RandomSource& random) { return tid(random.nextBoolean() ? "ruins/grove_pillar" : "ruins/grove_arch"); };
    f.modifySettings = [](std::vector<std::string>& p, st::RandomSource&) { p.push_back("twilightforest:stone_brick_variants"); };
    return f;
}

TemplateFeature stoneCircle() {
    TemplateFeature f;
    f.getTemplate    = [](st::RandomSource&) { return tid("ruins/stone_circle"); };
    f.modifySettings = [](std::vector<std::string>& p, st::RandomSource&) { p.push_back("twilightforest:cobble_variants"); };
    return f;
}

} // namespace

FeatureReport& lastFeatureReport() { return tReport; }

bool placeDruidHut(st::Region& region, BlockPos const& origin, st::RandomSource& random, Json const& config) {
    return placeTemplateFeature(druidHut(config), region, origin, random);
}
bool placeSimpleWell(st::Region& region, BlockPos const& origin, st::RandomSource& random, Json const& config) {
    return placeTemplateFeature(well(config, false), region, origin, random);
}
bool placeFancyWell(st::Region& region, BlockPos const& origin, st::RandomSource& random, Json const& config) {
    return placeTemplateFeature(well(config, true), region, origin, random);
}
bool placeGroveRuins(st::Region& region, BlockPos const& origin, st::RandomSource& random) { return placeTemplateFeature(groveRuins(), region, origin, random); }
bool placeStoneCircle(st::Region& region, BlockPos const& origin, st::RandomSource& random) { return placeTemplateFeature(stoneCircle(), region, origin, random); }

// ---- graveyard ---------------------------------------------------------------------------------------------------

/// GraveyardFeature#place (docs/spec/questcamps.md 4.2).
bool placeGraveyard(st::Region& region, BlockPos const& pos, st::RandomSource& rand) {
    tReport           = FeatureReport{};
    constexpr int flags = 16 | 2 | 1;
    auto const*   base  = st::templates::get(tid("graveyard/graveyard"));
    auto const*   trap  = st::templates::get(tid("graveyard/grave_trap"));
    static char const* const kGraves[] = {"graveyard/grave_full", "graveyard/grave_upper", "graveyard/grave_lower"};
    if (!base || !trap) return false;
    for (auto const* g : kGraves)
        if (!st::templates::get(tid(g))) return false;
    auto const* full = st::templates::get(tid(kGraves[0]));

    Rotation rotation = st::rotationFromIndex(rand.nextInt(4));
    Mirror   mirror   = static_cast<Mirror>(rand.nextInt(4) % 3);
    auto     transformedSize      = templateSize(*base, rotation);
    auto     transformedGraveSize = templateSize(*full, rotation);

    int          ox = pos.x - 8, oz = pos.z - 8;
    int          ccx = ox >> 4, ccz = oz >> 4;
    int          ecx = (ox + transformedSize.x) >> 4, ecz = (oz + transformedSize.z) >> 4;
    st::BlockBox mask{(ccx << 4) + 8, 0, (ccz << 4) + 8, (ecx << 4) + 15 + 8, 255, (ecz << 4) + 15 + 8};
    st::TemplateSettings settings;
    settings.rotation = rotation;
    settings.mirror   = mirror;
    settings.clip     = mask;

    BlockPos startPos{(ccx << 4) + 8, pos.y - 1, (ccz << 4) + 8};
    if (!offsetToAverageGroundLevel(region, startPos, transformedSize, true)) return false;

    auto     grid = cq::graveGrid(transformedSize, transformedGraveSize, toCore(rotation), toCore(mirror));
    BlockPos zero = zeroPosition(startPos, mirror, rotation, base->sizeX, base->sizeZ);
    BlockPos placementPos{zero.x + 1, zero.y - 1, zero.z};
    tReport.templateId   = tid("graveyard/graveyard");
    tReport.placementPos = placementPos;
    tReport.rotation     = rotation;
    tReport.mirror       = mirror;

    settings.processors.push_back("twilightforest:web"); // shared: the graves and the trap get it too
    std::vector<std::pair<BlockPos, std::string>> data;
    st::ext::placeTemplate(tid("graveyard/graveyard"), region, placementPos, placementPos, settings, rand, flags);
    for (auto& m : st::ext::filterDataMarkers(tid("graveyard/graveyard"), placementPos, settings, rand)) data.push_back(std::move(m));

    BlockPos start{startPos.x + grid.start.x, startPos.y + grid.start.y, startPos.z + grid.start.z};
    for (int x = 1; x <= grid.size.x - 1; x++)
        for (int z = 1; z <= grid.size.z - 1; z++) {
            BlockPos p{start.x + x, start.y, start.z + z};
            if (st::blocks::isAir(region.getBlock(p)) && rand.nextInt(12) == 0) region.setBlock(p, st::blocks::java("minecraft:cobweb"), flags);
        }

    BlockPos fixed{startPos.x + grid.fixed.x, startPos.y + grid.fixed.y, startPos.z + grid.fixed.z};
    auto     chestloc = cq::rotate(cq::Vec3i{rand.nextInt(2) - (mirror == Mirror::FrontBack ? 1 : 0), 1, 0}, toCore(rotation));
    auto     trapOff  = cq::rotate(cq::Vec3i{mirror == Mirror::FrontBack ? 1 : -1, 0, mirror == Mirror::LeftRight ? 1 : -1}, toCore(rotation));
    auto const& chestState =
        st::blocks::mirror(st::blocks::rotate(st::blocks::java("minecraft:trapped_chest", Json{{"facing", "west"}, {"type", "single"}, {"waterlogged", "false"}}), rotation),
                           mirror);
    for (auto const& [x, z] : grid.cells) {
        BlockPos placement{fixed.x + x, fixed.y - 2, fixed.z + z};
        int      kind  = rand.nextInt(3);
        auto     grave = tid(kGraves[kind]);
        st::ext::placeTemplate(grave, region, placement, placement, settings, rand, flags);
        for (auto& m : st::ext::filterDataMarkers(grave, placement, settings, rand)) data.push_back(std::move(m));
        tReport.graves++;
        if (kind != 0 || !rand.nextBoolean()) continue;
        if (rand.nextInt(3) == 0) {
            placement = BlockPos{placement.x + trapOff.x, placement.y + trapOff.y, placement.z + trapOff.z};
            st::ext::placeTemplate(tid("graveyard/grave_trap"), region, placement, placement, settings, rand, flags);
        }
        // TF's quirk: the trap's markers are filtered at the base's placement position, placed or not
        for (auto& m : st::ext::filterDataMarkers(tid("graveyard/grave_trap"), placementPos, settings, rand)) data.push_back(std::move(m));
        BlockPos chest{placement.x + chestloc.x, placement.y + chestloc.y, placement.z + chestloc.z};
        if (region.setBlock(chest, chestState, flags)) {
            std::int64_t seed = static_cast<std::int64_t>(static_cast<std::uint64_t>(region.seed()) * static_cast<std::uint64_t>(static_cast<std::int64_t>(chest.x)) +
                                                          static_cast<std::uint64_t>(static_cast<std::int64_t>(chest.y))) ^
                                static_cast<std::int64_t>(chest.z);
            st::blockentities::lootTable(region, chest, "twilightforest:graveyard", seed);
            region.setBlock(BlockPos{chest.x, chest.y - 1, chest.z}, st::blocks::java("minecraft:mossy_cobblestone"), 3);
            tReport.chests++;
        }
        // a wraith at the placement's block corner (finalizeMobSpawn STRUCTURE)
        st::blockentities::entity(region, placement.x, placement.y, placement.z, "twilightforest:wraith", Json::object());
        tReport.wraiths++;
    }

    for (auto const& [p, s] : data) {
        tReport.markers.push_back(s);
        if (s != "spawner") continue;
        region.setBlock(p, st::blocks::air(), 3);
        if (rand.nextInt(4) == 0) st::blockentities::spawner(region, p, "twilightforest:rising_zombie", Json::object());
    }
    return true;
}

void installFeatures() {
    namespace ff = gf::features;
    static bool once = false;
    if (!std::exchange(once, true)) data::onReload([] {
            std::lock_guard lock(paletteMutex());
            paletteCache().clear();
        });
    ff::registerType("twilightforest:druid_hut", [](ff::FeatureContext const& c) {
        return placeDruidHut(c.region, c.origin, st::ext::worldRandom(c.region), c.config);
    });
    ff::registerType("twilightforest:simple_well", [](ff::FeatureContext const& c) {
        return placeSimpleWell(c.region, c.origin, st::ext::worldRandom(c.region), c.config);
    });
    ff::registerType("twilightforest:fancy_well", [](ff::FeatureContext const& c) {
        return placeFancyWell(c.region, c.origin, st::ext::worldRandom(c.region), c.config);
    });
    ff::registerType("twilightforest:grove_ruins", [](ff::FeatureContext const& c) { return placeGroveRuins(c.region, c.origin, st::ext::worldRandom(c.region)); });
    ff::registerType("twilightforest:stone_circle", [](ff::FeatureContext const& c) { return placeStoneCircle(c.region, c.origin, st::ext::worldRandom(c.region)); });
    // GraveyardFeature draws from the feature context's random (ctx.random())
    ff::registerType("twilightforest:graveyard", [](ff::FeatureContext const& c) { return placeGraveyard(c.region, c.origin, c.random); });
}

} // namespace gf::feature::questcamps
