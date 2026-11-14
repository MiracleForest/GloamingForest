// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/type/LichTowerStructure.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichTowerPieces.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichTowerUtil.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichTowerFoyer.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichTowerFoyerDecor.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichTowerBase.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichTowerBaseTrim.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichTowerSegment.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichBossRoom.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichBossRoof.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichTowerSpawnerBridge.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichTowerWingBridge.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichTowerWingRoom.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichTowerWingRoof.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichTowerWingBeard.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichTowerRoomDecor.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichTowerMagicGallery.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The lich tower revamp: a foyer, the central tower stacked from slices up to the boss room, wings of
// bridges and rooms with ladders, roofs and beards, mob bridges inside the tower and the magic
// gallery. The pieces follow upstream's code; the templates are original GloamingForest designs (rule
// R11/R19) at upstream's ids with upstream's junction names (tools/structures/lich). Room interiors, roofs,
// beards and decor are U22's templates; while one is missing, a room falls back to a GloamingForest
// placeholder room of its size (gloamingforest:lich_tower/placeholder/<size>), so the tower still
// assembles.

#include "feature/lich/Tower.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/material/Material.h"

#include "core/lich/Simplex.h"
#include "core/lich/TowerMath.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/structure/Paintings.h"
#include "feature/ext/structure/Placement.h"
#include "feature/ext/structure/Processors.h"
#include "feature/ext/structure/TemplateDefinitions.h"
#include "feature/features/Features.h"
#include "feature/landmark/Landmark.h"
#include "feature/lich/Common.h"
#include "feature/lich/Lich.h"
#include "feature/lich/Pending.h"
#include "feature/structure/Structure.h"
#include "feature/structure/bases/Component.h"
#include "feature/structure/bases/TemplatePieces.h"
#include "feature/tags/Tags.h"
#include "feature/worldgen/Worldgen.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::lich::tower {

namespace gs = gf::structure;
namespace sb = gf::structure::bases;
namespace sx = gf::structure::ext;
using gs::BlockBox;
using gs::Json;
using gs::Orientation;
using gs::RandomSource;
using gs::Region;
using gs::Rotation;
using sb::JigsawPiece;
using sb::JigsawPlaceContext;
using sb::JigsawRecord;
using sb::PieceList;
using sb::TerrainAdjustment;

namespace {

// ---- ids --------------------------------------------------------------------------------------------

constexpr char const* kNs = "gloamingforest:lich_tower/";
std::string tid(char const* path) { return std::string(kNs) + path; }
/// The templates upstream's code names directly have GloamingForest ids (no upstream template definition knows them).
std::string gid(char const* path) { return std::string("gf:lich_tower/") + path; }

constexpr char const* kJunctionRoom   = "gf:lich_tower/room";
constexpr char const* kJunctionBridge = "gf:lich_tower/bridge";
constexpr char const* kJunctionCenter = "gf:lich_tower/bridge_center";
constexpr char const* kJunctionRoof   = "gf:lich_tower/roof";
constexpr char const* kJunctionBeard  = "gf:lich_tower/beard";
constexpr char const* kJunctionDecor  = "gf:lich_tower/decor";
constexpr char const* kJunctionBelow  = "gf:lich_tower/tower_below";
constexpr char const* kJunctionAbove  = "gf:lich_tower/tower_above";

// LichTowerPieces spawn labels
constexpr int kYardSpawns     = 0;
constexpr int kInteriorSpawns = 1;
constexpr int kEmptySpawns    = 2;
constexpr int kDenySpawns     = -2; // SpawnIndexProvider.Deny

// piece type ids (GloamingStructurePieceTypes "GloamingLT" + suffix)
constexpr char const* kFoyer      = "gloamingforest:GloamingLTTFoy";
constexpr char const* kFoyerDecor = "gloamingforest:GloamingLTTFoyD";
constexpr char const* kBase       = "gloamingforest:GloamingLTCTBase";
constexpr char const* kBaseTrim   = "gloamingforest:GloamingLTCTTrim";
constexpr char const* kSegment    = "gloamingforest:GloamingLTCTSeg";
constexpr char const* kMobBridge  = "gloamingforest:GloamingLTMobBridge";
constexpr char const* kWingBridge = "gloamingforest:GloamingLTBridge";
constexpr char const* kWingRoof   = "gloamingforest:GloamingLTTRoof";
constexpr char const* kWingBeard  = "gloamingforest:GloamingLTTBeard";
constexpr char const* kWingRoom   = "gloamingforest:GloamingLTTRoom";
constexpr char const* kRoomDecor  = "gloamingforest:GloamingLTTDecor";
constexpr char const* kGallery    = "gloamingforest:GloamingLTTGallery";
constexpr char const* kBossRoom   = "gloamingforest:GloamingLTTBoss";
constexpr char const* kBossRoof   = "gloamingforest:GloamingLTTBossRoof";

// ---- the generation context of the running layout ------------------------------------------------------

thread_local sx::GenerationContext const* gContext = nullptr;

void grow(JigsawPiece& piece, JigsawPiece& parent, PieceList& pieces, RandomSource& random) {
    if (gContext) sx::addJigsaws(piece, parent, pieces, *gContext);
    else piece.addJigsaws(parent, pieces, random);
}

// ---- LichTowerUtil ------------------------------------------------------------------------------------

std::string json(Json const& j) { return j.dump(); }

std::string updateMarker() {
    return json({{"processor_type", "gloamingforest:update_marking"},
                 {"mark_updates", Json::array({"minecraft:birch_fence", "minecraft:polished_andesite_stairs", "minecraft:stone_brick_wall", "minecraft:mossy_stone_brick_wall",
                                               "minecraft:cobblestone_wall", "minecraft:mossy_cobblestone_wall", "gloamingforest:wrought_iron_fence", "gloamingforest:canopy_fence",
                                               "gloamingforest:twisted_stone_pillar"})}});
}

/// LichTowerUtil#addDefaultProcessors.
void addDefaultProcessors(std::vector<std::string>& p) {
    p.push_back("minecraft:jigsaw_replacement");
    p.push_back(json({{"processor_type", "minecraft:block_ignore"}, {"blocks", Json::array({Json{{"Name", "minecraft:structure_block"}}})}}));
    p.push_back("gloamingforest:stone_brick_variants");
    p.push_back("gloamingforest:cobble_variants");
    p.push_back("gloamingforest:infest_blocks");
    p.push_back(updateMarker());
}

std::string const& roomSpawners() {
    static std::string const p = sx::compileSpawnerProcessor(2, 0.8F, {{"minecraft:spider", 1}, {"minecraft:cave_spider", 1}, {"gloamingforest:swarm_spider", 1},
                                                                        {"gloamingforest:hedge_spider", 1}, {"minecraft:skeleton", 4}, {"minecraft:zombie", 4}});
    return p;
}

std::string const& centralSpawners() {
    static std::string const p = sx::compileSpawnerProcessor(4, 0.0F, {{"minecraft:skeleton", 2}, {"minecraft:zombie", 1}, {"gloamingforest:swarm_spider", 1}});
    return p;
}

std::string stairDecay(float chance) {
    return json({{"processor_type", "gloamingforest:vertical_decay"},
                 {"decay_blocks", Json::array({"gloamingforest:gloaming_oak_slab", "gloamingforest:canopy_slab", "gloamingforest:gloaming_oak_banister", "gloamingforest:canopy_banister"})},
                 {"decay_chance", chance}});
}

std::optional<std::string> randomTemplate(RandomSource& random, std::string const& pool) { return sx::getRandomTemplate(random, pool); }

/// A room template of `pool` that exists, else the placeholder room of the size (U22's templates
/// absent).
std::optional<std::string> roomOrPlaceholder(std::optional<std::string> id, int size) {
    if (id && gs::templates::get(*id)) return id;
    if (size < 0 || size > 3) return std::nullopt;
    static char const* const kPlaceholders[] = {"placeholder/3x3", "placeholder/5x5", "placeholder/7x7", "placeholder/9x9"};
    return gid(kPlaceholders[size]);
}

std::optional<std::string> rollRandomRoom(RandomSource& random, int size) {
    static char const* const kPools[] = {"3x3", "5x5", "7x7", "9x9"};
    if (size < 0 || size > 3) return std::nullopt;
    return roomOrPlaceholder(randomTemplate(random, tid(kPools[size])), size);
}

char const* sizeName(int size) {
    static char const* const k[] = {"3x3", "5x5", "7x7", "9x9"};
    return size >= 0 && size <= 3 ? k[size] : nullptr;
}

std::vector<std::string> shuffled(RandomSource& random, std::string const& pool) { return sx::getShuffledSequence(random, pool); }

std::vector<std::string> shuffledRoofs(RandomSource& random, int size, bool side) {
    auto const* s = sizeName(size);
    if (!s) return {};
    return shuffled(random, tid(s) + (side ? "/side_roof" : "/roof"));
}

std::vector<std::string> shuffledBeards(RandomSource& random, int size) {
    if (size < 1 || size > 3) return {};
    return shuffled(random, tid(sizeName(size)) + "/beard");
}

std::optional<std::string> getTrim(RandomSource& random, int size) {
    if (size < 1 || size > 3) return std::nullopt;
    return randomTemplate(random, tid(sizeName(size)) + "/trim");
}

std::optional<std::string> getFallbackRoof(RandomSource& random, int size) {
    auto const* s = sizeName(size);
    if (!s) return std::nullopt;
    return randomTemplate(random, tid(s) + "/roof_fallback"); // side roofs fall back to the same pool
}

std::optional<std::string> getFallbackBeard(RandomSource& random, int size) {
    if (size < 1 || size > 3) return std::nullopt;
    return randomTemplate(random, tid(sizeName(size)) + "/beard_fallback");
}

std::set<std::string> ladderPlacements(int size) {
    switch (size) {
    case 1: return {"gf:ladder_below/0", "gf:ladder_below/2"};
    case 2: return {"gf:ladder_below/0", "gf:ladder_below/1", "gf:ladder_below/3", "gf:ladder_below/4"};
    case 3: return {"gf:ladder_below/1", "gf:ladder_below/2", "gf:ladder_below/4", "gf:ladder_below/5"};
    default: return {};
    }
}

std::optional<std::string> getRoomUpwards(RandomSource& random, int size, int ladderOffset) {
    static std::set<int> const k5{0, 2}, k7{0, 1, 3, 4}, k9{1, 2, 4, 5};
    std::set<int> const* allowed = size == 1 ? &k5 : size == 2 ? &k7 : size == 3 ? &k9 : nullptr;
    if (!allowed || !allowed->contains(ladderOffset)) return std::nullopt;
    return randomTemplate(random, tid(sizeName(size)) + "/ladder_" + std::to_string(ladderOffset));
}

// ---- geometry helpers -----------------------------------------------------------------------------------

BlockPos add(BlockPos const& a, BlockPos const& b) { return BlockPos{a.x + b.x, a.y + b.y, a.z + b.z}; }
BlockPos up(BlockPos const& p, int n = 1) { return BlockPos{p.x, p.y + n, p.z}; }

BlockPos step(Orientation o) {
    switch (o) {
    case Orientation::North: return {0, 0, -1};
    case Orientation::South: return {0, 0, 1};
    case Orientation::West: return {-1, 0, 0};
    case Orientation::East: return {1, 0, 0};
    }
    return {0, 0, 0};
}
BlockPos relative(BlockPos const& p, Orientation o, int n = 1) {
    auto s = step(o);
    return BlockPos{p.x + s.x * n, p.y + s.y * n, p.z + s.z * n};
}
Orientation clockwise(Orientation o) {
    switch (o) {
    case Orientation::North: return Orientation::East;
    case Orientation::East: return Orientation::South;
    case Orientation::South: return Orientation::West;
    case Orientation::West: return Orientation::North;
    }
    return o;
}
Orientation counterClockwise(Orientation o) { return clockwise(clockwise(clockwise(o))); }
Orientation opposite(Orientation o) { return clockwise(clockwise(o)); }

/// Rotation#rotate(Direction).
Orientation rotate(Orientation o, Rotation r) {
    switch (r) {
    case Rotation::None: return o;
    case Rotation::Clockwise90: return clockwise(o);
    case Rotation::Clockwise180: return opposite(o);
    case Rotation::Counterclockwise90: return counterClockwise(o);
    }
    return o;
}

bool horizontalFront(JigsawRecord const& r) { return !r.frontVertical; }
bool frontIsDown(JigsawRecord const& r) { return r.frontVertical && !r.frontUp; }

/// JigsawUtil.getAbsoluteHorizontal: the front when horizontal, else the top.
Orientation absoluteHorizontal(JigsawRecord const& r) { return r.frontVertical ? r.top : r.front; }

/// A record with FrontAndTop(vertical, top).
JigsawRecord verticalRecord(JigsawRecord r, bool upward, Orientation top) {
    r.frontVertical = true;
    r.frontUp       = upward;
    r.top           = top;
    return r;
}

int span(BlockBox const& b, int axis) { return axis == 0 ? b.maxX - b.minX + 1 : axis == 1 ? b.maxY - b.minY + 1 : b.maxZ - b.minZ + 1; }

/// BoundingBoxUtils#extrusionFrom(box, UP, n): the n blocks above the box.
BlockBox extrudeUp(BlockBox const& b, int n) { return BlockBox{b.minX, b.maxY + 1, b.minZ, b.maxX, b.maxY + n, b.maxZ}; }
/// BoundingBoxUtils#cloneWithAdjustments.
BlockBox adjusted(BlockBox const& b, int x0, int y0, int z0, int x1, int y1, int z1) {
    return BlockBox{b.minX + x0, b.minY + y0, b.minZ + z0, b.maxX + x1, b.maxY + y1, b.maxZ + z1};
}

bool collides(PieceList const& pieces, BlockBox const& box) { return sb::findCollisionPiece(pieces, box) != nullptr; }

std::optional<JigsawPlaceContext> pick(BlockPos const& parentTemplatePos, JigsawRecord const& source, std::optional<std::string> const& templateId,
                                       std::string const& label, RandomSource& random) {
    if (!templateId || !gs::templates::get(*templateId)) return std::nullopt;
    return JigsawPlaceContext::pickPlaceableJunction(parentTemplatePos, source.pos, source, *templateId, label, random);
}

bool isBanister(Region& region, BlockPos const& pos) {
    return gf::tags::has(gf::tags::Registry::Block, "gloamingforest:banisters", game::blockId(region.getBlock(pos)));
}

void removeIfBanister(Region& region, BlockPos const& pos, BlockBox const& chunk) {
    if (!chunk.contains(pos.x, pos.y, pos.z) || !isBanister(region, pos)) return;
    if (auto const* air = game::blockWith("minecraft:air")) region.setBlock(pos, *air, 2);
}

// ---- label parsing (LichTowerWingRoom / LichTowerFoyer data markers) ------------------------------------

/// Java's String#split(regex) for one literal character: trailing empty strings are dropped.
std::vector<std::string> javaSplit(std::string const& s, char sep) {
    std::vector<std::string> out;
    std::size_t              start = 0;
    for (;;) {
        auto at = s.find(sep, start);
        out.push_back(s.substr(start, at == std::string::npos ? std::string::npos : at - start));
        if (at == std::string::npos) break;
        start = at + 1;
    }
    if (out.size() > 1)
        while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

/// StringUtils.isNumeric: non-empty, digits only.
bool isNumeric(std::string const& s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; }); }

/// ResourceLocation.bySeparator(label, sep): "gloamingforest.chests.x" -> "gloamingforest:chests/x"
/// (the first separator splits the namespace, the others become '/'); no separator: minecraft:.
std::string bySeparator(std::string const& label, char sep) {
    auto at = label.find(sep);
    if (at == std::string::npos) return "minecraft:" + label;
    std::string path = label.substr(at + 1);
    std::replace(path.begin(), path.end(), sep, '/');
    return label.substr(0, at) + ":" + path;
}

int quarterTurns(Rotation r) {
    switch (r) {
    case Rotation::None: return 0;
    case Rotation::Clockwise90: return 1;
    case Rotation::Clockwise180: return 2;
    case Rotation::Counterclockwise90: return 3;
    }
    return 0;
}
Rotation fromQuarterTurns(int q) {
    static Rotation const k[] = {Rotation::None, Rotation::Clockwise90, Rotation::Clockwise180, Rotation::Counterclockwise90};
    return k[((q % 4) + 4) % 4];
}
/// Rotation#getRotated.
Rotation compose(Rotation a, Rotation b) { return fromQuarterTurns(quarterTurns(a) + quarterTurns(b)); }
int      horizontalIndex(Orientation o) { return o == Orientation::South ? 0 : o == Orientation::West ? 1 : o == Orientation::North ? 2 : 3; }
/// RotationUtil.getRelativeRotation(from, to): the rotation that turns `from` into `to`.
Rotation relativeRotation(Orientation from, Orientation to) { return fromQuarterTurns(horizontalIndex(to) - horizontalIndex(from)); }
/// DirectionUtil.fromStringOrElse(name, fallback) for the horizontal names.
Orientation directionOr(std::string const& name, Orientation fallback) {
    if (name == "north") return Orientation::North;
    if (name == "south") return Orientation::South;
    if (name == "west") return Orientation::West;
    if (name == "east") return Orientation::East;
    return fallback;
}
char const* orientationName(Orientation o) {
    switch (o) {
    case Orientation::North: return "north";
    case Orientation::South: return "south";
    case Orientation::West: return "west";
    case Orientation::East: return "east";
    }
    return "north";
}

/// LichTowerWingRoom#pickRandomMob: one of the label's comma list, else defaultRandomMob.
std::string pickRandomMob(RandomSource& random, std::vector<std::string> const& params) {
    if (params.size() >= 2) {
        auto monsters = javaSplit(params[1], ',');
        if (!monsters.empty()) {
            auto const& label = monsters[random.nextInt(static_cast<int>(monsters.size()))];
            if (label == "hedge_spider") return "gloamingforest:hedge_spider";
            if (label == "swarm_spider") return "gloamingforest:swarm_spider";
            // EntityType.byString(label).orElse(ZOMBIE): a well-formed id, else a zombie
            std::string id = label.find(':') == std::string::npos ? "minecraft:" + label : label;
            bool ok = !label.empty() && std::all_of(label.begin(), label.end(), [](unsigned char c) { return std::islower(c) || std::isdigit(c) || c == '_' || c == ':' || c == '.' || c == '/'; });
            return ok ? id : "minecraft:zombie";
        }
    }
    switch (random.nextInt(10)) { // defaultRandomMob
    case 7:
    case 8:
    case 9: return "minecraft:skeleton";
    case 6: return "minecraft:spider";
    case 5: return "minecraft:cave_spider";
    case 4: return "gloamingforest:hedge_spider";
    case 3: return "gloamingforest:swarm_spider";
    default: return "minecraft:zombie";
    }
}

/// A chest-like container with a facing and a loot table: the block now, the loot table once the
/// chunk is loaded (gf::loot::placeContainer, Pending.h).
void placeContainer(Region& region, BlockPos const& pos, std::string const& id, Orientation facing, std::string const& table, std::int64_t seed) {
    static int const kFacing[] = {2, 5, 3, 4}; // FacingID of North, East, South, West
    gf::feature::lich::pending::add(region.dimension(), pos, "container",
                                    Json{{"block", id}, {"facing", kFacing[static_cast<int>(facing)]}, {"table", table}, {"seed", seed}});
}

// ---- the piece base ----------------------------------------------------------------------------------

class LichPiece : public JigsawPiece, public sb::SortablePiece {
public:
    LichPiece(char const* type, int depth, std::string const& templateId, JigsawPlaceContext const& ctx) : JigsawPiece(type, depth, templateId, ctx) {
        addDefaultProcessors(placeSettings.processors);
    }
    [[nodiscard]] int getSortKey() const override { return sortKey; }
    [[nodiscard]] BlockBox getBeardifierBox() const override { return bbox; }
    [[nodiscard]] TerrainAdjustment getTerrainAdjustment() const override { return terrain; }
    [[nodiscard]] int getGroundLevelDelta() const override { return groundDelta; }
    [[nodiscard]] virtual int spawnLabel() const { return -1; }
    [[nodiscard]] BlockPos const& templatePos() const { return templatePosition; }
    [[nodiscard]] std::string const& templateId() const { return templateName; }
    [[nodiscard]] sb::PlaceSettings& settings() { return placeSettings; }
    [[nodiscard]] virtual bool isTrim() const { return false; }

    void processJigsaw(JigsawPiece& parent, PieceList& pieces, RandomSource& random, JigsawRecord const& connection, int index) override {
        process(parent, pieces, random, connection, index);
    }
    virtual void process(JigsawPiece&, PieceList&, RandomSource&, JigsawRecord const&, int) {}

protected:
    int               sortKey     = 0;
    TerrainAdjustment terrain     = TerrainAdjustment::None;
    int               groundDelta = 0;
};

LichPiece* asLich(gs::Piece* p) { return dynamic_cast<LichPiece*>(p); }

/// The template position of any jigsaw parent (GloamingJigsawPiece#templatePosition()).
BlockPos parentPos(JigsawPiece& parent) {
    if (auto* l = dynamic_cast<LichPiece*>(&parent)) return l->templatePos();
    return parent.getSourcePosition();
}
std::string parentTemplate(JigsawPiece& parent) { return parent.getTemplateName(); }

// ---- foyer --------------------------------------------------------------------------------------------

class TowerBase;
class FoyerDecor;

class Foyer final : public LichPiece {
public:
    Foyer(JigsawPlaceContext const& ctx, bool putChest, bool chestSide) : LichPiece(kFoyer, 0, gid("tower_foyer"), ctx), putChest(putChest), chestSide(chestSide) {
        terrain     = TerrainAdjustment::BeardBox;
        groundDelta = 1;
    }
    [[nodiscard]] int spawnLabel() const override { return kEmptySpawns; }
    [[nodiscard]] Json save() const override {
        auto j          = JigsawPiece::save();
        j["put_chest"]  = putChest;
        j["chest_side"] = chestSide;
        return j;
    }
    void load(Json const& d) override {
        JigsawPiece::load(d);
        putChest  = d.value("put_chest", false);
        chestSide = d.value("chest_side", false);
    }
    void process(JigsawPiece& parent, PieceList& pieces, RandomSource& random, JigsawRecord const& c, int) override;

protected:
    void handleDataMarker(std::string const& label, BlockPos const& pos, Region& region, RandomSource& random, BlockBox const& chunk, Rotation) override {
        auto split = javaSplit(label, '@');
        if (split.empty()) return;
        Rotation dataRotation = split.size() == 1 ? Rotation::Clockwise180 : relativeRotation(Orientation::North, directionOr(split[1], Orientation::South));
        // the chest generates at one of the two markers only
        if (!putChest || split[0] != (chestSide ? "chest_a" : "chest_b") || !chunk.contains(pos.x, pos.y, pos.z)) return;
        Orientation facing = rotate(Orientation::North, compose(placeSettings.rotation, dataRotation));
        placeBlock(region, "minecraft:chest", Json{{"facing", orientationName(facing)}}, pos.x, pos.y, pos.z, chunk);
        placeContainer(region, pos, "minecraft:chest", facing, "gloamingforest:chests/tower_room", random.nextLong());
        placeBlock(region, "gloamingforest:canopy_planks", Json::object(), pos.x, pos.y - 1, pos.z, chunk);
    }

private:
    bool                  putChest  = false;
    bool                  chestSide = false;
    std::vector<BlockPos> shelves;
    friend class FoyerDecor;
};

class FoyerDecor final : public LichPiece {
public:
    FoyerDecor(int depth, JigsawPlaceContext const& ctx) : LichPiece(kFoyerDecor, depth, gid("foyer_decor"), ctx) {}
    void postProcess(Region& region, RandomSource& random, BlockBox const& chunk, BlockPos const& pivot) override {
        JigsawPiece::postProcess(region, random, chunk, pivot);
        BlockPos at = add(getSourceJigsaw().pos, templatePos());
        if (!chunk.contains(at.x, at.y, at.z)) return;
        random.setSeed(core::lich::blockPosAsLong(at.x, at.y, at.z) + at.y);
        bool noChest = game::blockId(region.getBlock(up(at))) == "minecraft:chiseled_stone_bricks";
        Orientation facing = rotate(Orientation::South, placeSettings.rotation); // rotation.getRotated(CLOCKWISE_180) of north
        int pick = random.nextInt(5) + (noChest ? -1 : 0);
        if (pick == 4) {
            placeChest(region, chunk, random, at.x, at.y, at.z, "minecraft:chest", "gloamingforest:chests/tower_foyer");
            placeBlock(region, "minecraft:stone_brick_stairs", Json{{"half", "top"}, {"facing", dirName(facing)}}, at.x, at.y + 1, at.z, chunk);
        } else if (pick == 3) {
            placeBlock(region, "gloamingforest:candelabra",
                       Json{{"facing", dirName(rotate(Orientation::North, placeSettings.rotation))}, {"lighting", "dim"}, {"has_candle_1", true}, {"has_candle_2", true}, {"has_candle_3", true}},
                       at.x, at.y, at.z, chunk);
        } else {
            switch (random.nextInt(5)) {
            case 3:
                placeBlock(region, "gloamingforest:skeleton_skull_candle",
                           Json{{"lighting", "normal"}, {"candles", random.nextIntBetweenInclusive(1, 3)}, {"rotation", rotRotation(random.nextIntBetweenInclusive(7, 9))}}, at.x,
                           at.y, at.z, chunk);
                break;
            case 1:
            case 2: placeBlock(region, "minecraft:skeleton_skull", Json{{"rotation", rotRotation(random.nextIntBetweenInclusive(7, 9))}}, at.x, at.y, at.z, chunk); break;
            default: placeBlock(region, "minecraft:candle", Json{{"candles", random.nextIntBetweenInclusive(1, 3)}, {"lit", true}}, at.x, at.y, at.z, chunk); break;
            }
        }
    }

private:
    static char const* dirName(Orientation o) {
        switch (o) {
        case Orientation::North: return "north";
        case Orientation::South: return "south";
        case Orientation::West: return "west";
        case Orientation::East: return "east";
        }
        return "north";
    }
    /// A skull's 16-step rotation turned with the piece (Rotation#rotate(int, 16)).
    int rotRotation(int r) const {
        int add = placeSettings.rotation == Rotation::Clockwise90 ? 4 : placeSettings.rotation == Rotation::Clockwise180 ? 8 : placeSettings.rotation == Rotation::Counterclockwise90 ? 12 : 0;
        return (r + add) % 16;
    }
};

// ---- central tower ---------------------------------------------------------------------------------------

class BaseTrim final : public LichPiece {
public:
    explicit BaseTrim(JigsawPlaceContext const& ctx) : LichPiece(kBaseTrim, 1, gid("central_trim"), ctx) {
        terrain     = TerrainAdjustment::BeardBox;
        groundDelta = 1;
        sortKey     = -2;
    }
    [[nodiscard]] bool isTrim() const override { return true; }
};

class RoomDecor final : public LichPiece {
public:
    RoomDecor(int depth, std::string const& id, JigsawPlaceContext const& ctx) : LichPiece(kRoomDecor, depth, id, ctx) {
        placeSettings.processors.push_back(roomSpawners());
        sortKey = 2;
    }
    static void addDecor(JigsawPiece& parent, PieceList& pieces, RandomSource& random, JigsawRecord const& c, int depth, bool central) {
        auto id = randomTemplate(random, tid(central ? "center_decor" : "room_decor"));
        auto pc = pick(parentPos(parent), c, id, kJunctionDecor, random);
        if (!pc) return;
        auto  decor = std::make_unique<RoomDecor>(depth, *id, *pc);
        auto* raw   = decor.get();
        pieces.push_back(std::move(decor));
        grow(*raw, parent, pieces, random);
    }

protected:
    void handleDataMarker(std::string const& label, BlockPos const& pos, Region& region, RandomSource& random, BlockBox const& chunk, Rotation) override {
        placeBlock(region, "minecraft:air", Json::object(), pos.x, pos.y, pos.z, chunk);
        // GloamingStructureHelper#randomPlant: a potted oak / spruce / birch / jungle sapling, red or brown mushroom
        static char const* const kPlants[] = {"minecraft:potted_oak_sapling",    "minecraft:potted_spruce_sapling", "minecraft:potted_birch_sapling",
                                              "minecraft:potted_jungle_sapling", "minecraft:potted_red_mushroom",   "minecraft:potted_brown_mushroom"};
        if (label == "sapling") {
            placeBlock(region, kPlants[random.nextInt(6)], Json::object(), pos.x, pos.y, pos.z, chunk);
        } else if (label == "tree") {
            // GloamingStructureHelper#randomTree(nextInt(4)): oak, spruce, birch or a jungle tree without
            // vines; a plant when the tree does not fit
            static char const* const kTrees[] = {"minecraft:oak", "minecraft:spruce", "minecraft:birch", "minecraft:jungle_tree_no_vine"};
            char const*              tree     = kTrees[random.nextInt(4)];
            if (!gf::features::place(tree, region, pos, random))
                placeBlock(region, kPlants[random.nextInt(6)], Json::object(), pos.x, pos.y, pos.z, chunk);
        }
    }
};

class TowerBase final : public LichPiece {
public:
    explicit TowerBase(JigsawPlaceContext const& ctx) : LichPiece(kBase, 1, gid("tower_base"), ctx) {
        // LichTowerUtil.addDefaultProcessors(placeSettings.addProcessor(TrimProcessor.INSTANCE)):
        // the trim processor runs first
        placeSettings.processors.insert(placeSettings.processors.begin(), trimProcessor());
        bbox = adjusted(bbox, 0, 0, 0, 0, 30, 0);
        casketWingIndex = firstMatchIndex([](JigsawRecord const& r) { return r.target == kJunctionBridge; });
        terrain         = TerrainAdjustment::BeardBox;
        groundDelta     = 1;
        sortKey         = 1;
    }
    [[nodiscard]] int spawnLabel() const override { return kInteriorSpawns; }
    [[nodiscard]] Json save() const override {
        auto j            = JigsawPiece::save();
        j["CasketWingIdx"] = casketWingIndex;
        return j;
    }
    void load(Json const& d) override {
        JigsawPiece::load(d);
        casketWingIndex = d.value("CasketWingIdx", -1);
        placeSettings.processors.clear();
        placeSettings.processors.push_back(trimProcessor());
        addDefaultProcessors(placeSettings.processors);
    }
    /// LichTowerBase.TrimProcessor: a polished andesite stair of the base does not replace the
    /// stone bricks a wing's trim put there.
    static std::string trimProcessor() { return json({{"processor_type", "gf:lich_tower_trim"}}); }
    void process(JigsawPiece& parent, PieceList& pieces, RandomSource& random, JigsawRecord const& c, int index) override;

protected:
    void handleDataMarker(std::string const& label, BlockPos const& pos, Region& region, RandomSource& random, BlockBox const& chunk, Rotation) override {
        auto colon = label.find(':');
        if (colon == std::string::npos || label.substr(0, colon) != "candle") return;
        auto n = label.substr(colon + 1);
        if (n.empty() || !std::all_of(n.begin(), n.end(), ::isdigit)) return;
        placeBlock(region, "minecraft:air", Json::object(), pos.x, pos.y, pos.z, chunk);
        bool major = std::stoi(n) == 2;
        if (!major && random.nextInt(3) != 0) return;
        int count = major ? 3 : 1 + random.nextInt(2);
        placeBlock(region, "minecraft:candle", Json{{"lit", true}, {"candles", count}}, pos.x, pos.y, pos.z, chunk);
    }

private:
    int casketWingIndex = -1;
};

void Foyer::process(JigsawPiece& parent, PieceList& pieces, RandomSource& random, JigsawRecord const& c, int) {
    if (c.target == "gf:lich_tower/tower_base") {
        auto pc = pick(templatePos(), c, gid("tower_base"), "gf:lich_tower/tower_base", random);
        if (!pc) return;
        auto  base = std::make_unique<TowerBase>(*pc);
        auto* raw  = base.get();
        pieces.push_back(std::move(base));
        grow(*raw, *this, pieces, random);
    } else if (c.target == "gf:shelf" && random.nextFloat() <= 0.5F) {
        auto pc = pick(templatePos(), c, gid("foyer_decor"), "gf:shelf", random);
        if (!pc) return;
        for (auto const& s : shelves)
            if (std::abs(s.x - c.pos.x) + std::abs(s.y - c.pos.y) + std::abs(s.z - c.pos.z) < 1.5F) return; // no shelf next to a shelf
        auto  decor = std::make_unique<FoyerDecor>(genDepth + 1, *pc);
        auto* raw   = decor.get();
        pieces.push_back(std::move(decor));
        grow(*raw, *this, pieces, random);
        shelves.push_back(c.pos);
    }
    (void)parent;
}

/// LichTowerSegment.
class Segment final : public LichPiece {
public:
    Segment(int depth, JigsawPlaceContext const& ctx, bool mobBridge, bool wings, bool gallery, std::string const& templateId)
    : LichPiece(kSegment, depth, templateId, ctx), putMobBridge(mobBridge), putWings(wings), putGallery(gallery) {
        addStairDecay();
    }
    [[nodiscard]] int spawnLabel() const override { return kInteriorSpawns; }
    [[nodiscard]] Json save() const override {
        auto j           = JigsawPiece::save();
        j["put_bridge"]  = putMobBridge;
        j["put_wings"]   = putWings;
        j["put_gallery"] = putGallery;
        return j;
    }
    void load(Json const& d) override {
        JigsawPiece::load(d);
        putMobBridge = d.value("put_bridge", false);
        putWings     = d.value("put_wings", false);
        putGallery   = d.value("put_gallery", false);
        placeSettings.processors.clear();
        addDefaultProcessors(placeSettings.processors);
        addStairDecay();
    }
    void process(JigsawPiece& parent, PieceList& pieces, RandomSource& random, JigsawRecord const& c, int index) override;

    static void buildTowerBySegments(PieceList& pieces, RandomSource& random, JigsawRecord const& source, JigsawPiece& base, int segments);

protected:
    void handleDataMarker(std::string const& label, BlockPos const& pos, Region& region, RandomSource& random, BlockBox const& chunk, Rotation) override {
        sx::placePainting(label, pos, region, random, chunk, placeSettings.rotation, 2, 10, "gloamingforest:tower_paintings");
    }

private:
    void addStairDecay() {
        int level = core::lich::stairDecayLevel(genDepth);
        if (level >= 0) placeSettings.processors.push_back(stairDecay(core::lich::stairDecayChance(level)));
    }
    bool putMobBridge = false, putWings = false, putGallery = false;
};

class BossRoof final : public LichPiece {
public:
    explicit BossRoof(JigsawPlaceContext const& ctx) : LichPiece(kBossRoof, 1, gid("tower_boss_roof"), ctx) {}
    [[nodiscard]] int spawnLabel() const override { return kDenySpawns; }
};

class BossRoom final : public LichPiece {
public:
    explicit BossRoom(JigsawPlaceContext const& ctx) : LichPiece(kBossRoom, 1, gid("tower_boss_room"), ctx) {}
    [[nodiscard]] int spawnLabel() const override { return kDenySpawns; }
    void postProcess(Region& region, RandomSource& random, BlockBox const& chunk, BlockPos const& pivot) override {
        JigsawPiece::postProcess(region, random, chunk, pivot);
        BlockPos center = up(add(getSourceJigsaw().pos, templatePos()), 9);
        for (auto const& s : core::lich::bossRoomCandles(center.x, center.y, center.z)) {
            BlockPos at{center.x + s.x, center.y + s.y, center.z + s.z};
            if (!chunk.contains(at.x, at.y, at.z)) continue;
            if (!region.getBlock(at).isAir() || !region.getBlock(BlockPos{at.x, at.y - 1, at.z}).isAir()) continue;
            if (auto const* candle = game::blockWithStates("minecraft:candle", Json{{"lit", true}})) region.setBlock(at, *candle, 2);
        }
    }
    void process(JigsawPiece&, PieceList& pieces, RandomSource& random, JigsawRecord const& c, int) override {
        if (c.target != kJunctionBelow) return;
        auto pc = pick(templatePos(), c, gid("tower_boss_roof"), kJunctionBelow, random);
        if (!pc) return;
        auto  roof = std::make_unique<BossRoof>(*pc);
        auto* raw  = roof.get();
        pieces.push_back(std::move(roof));
        grow(*raw, *this, pieces, random);
    }

protected:
    void handleDataMarker(std::string const& label, BlockPos const& pos, Region& region, RandomSource& random, BlockBox const& chunk, Rotation) override {
        sx::placePainting(label, pos, region, random, chunk, placeSettings.rotation, 3, 3, "gloamingforest:tower_boss_paintings");
    }
};

/// LichTowerSpawnerBridge (the mob bridges inside the central tower).
class SpawnerBridge final : public LichPiece {
public:
    SpawnerBridge(int depth, std::string const& id, JigsawPlaceContext const& ctx, bool inverted) : LichPiece(kMobBridge, depth, id, ctx), inverted(inverted) { addProcessors(); }
    [[nodiscard]] Json save() const override {
        auto j        = JigsawPiece::save();
        j["inverted"] = inverted;
        return j;
    }
    void load(Json const& d) override {
        JigsawPiece::load(d);
        inverted = d.value("inverted", false);
        placeSettings.processors.clear();
        addDefaultProcessors(placeSettings.processors);
        addProcessors();
    }

private:
    void addProcessors() {
        placeSettings.processors.push_back(centralSpawners());
        if (inverted)
            placeSettings.processors.push_back(json({{"processor_type", "gloamingforest:wood_multiswizzle"},
                                                     {"palettes", Json::array({Json::array({"gloamingforest:gloaming_oak", "gloamingforest:canopy"}),
                                                                               Json::array({"gloamingforest:canopy", "gloamingforest:gloaming_oak"})})}}));
    }
    bool inverted = false;
};

// ---- wings ---------------------------------------------------------------------------------------------

class WingRoom;

class WingRoof final : public LichPiece {
public:
    WingRoof(int depth, std::string const& id, JigsawPlaceContext const& ctx) : LichPiece(kWingRoof, depth, id, ctx) {
        placeSettings.processors.push_back("gloamingforest:soft_replace");
    }
    [[nodiscard]] BlockBox generationCollisionBox() const {
        if (span(bbox, 0) < 2 || span(bbox, 1) < 2 || span(bbox, 2) < 2) return BlockBox{bbox.minX, bbox.minY, bbox.minZ, bbox.maxX, std::max(bbox.minY, bbox.maxY - 1), bbox.maxZ};
        return adjusted(bbox, 1, 1, 1, -1, 0, -1);
    }
};

class WingBeard final : public LichPiece {
public:
    WingBeard(int depth, std::string const& id, JigsawPlaceContext const& ctx, bool ground) : LichPiece(kWingBeard, depth, id, ctx), generateGround(ground) {
        placeSettings.processors.push_back("gloamingforest:soft_replace");
        terrain     = ground ? TerrainAdjustment::BeardBox : TerrainAdjustment::None;
        groundDelta = 4;
        sortKey     = -1;
    }
    [[nodiscard]] bool isTrim() const override { return generateGround; }
    [[nodiscard]] Json save() const override {
        auto j          = JigsawPiece::save();
        j["gen_ground"] = generateGround;
        return j;
    }
    void load(Json const& d) override {
        JigsawPiece::load(d);
        generateGround = d.value("gen_ground", false);
        terrain        = generateGround ? TerrainAdjustment::BeardBox : TerrainAdjustment::None;
    }
    [[nodiscard]] BlockBox generationCollisionBox() const {
        if (span(bbox, 0) < 2 || span(bbox, 1) < 2 || span(bbox, 2) < 2) return BlockBox{bbox.minX, std::min(bbox.maxY, bbox.minY + 1), bbox.minZ, bbox.maxX, bbox.maxY, bbox.maxZ};
        return adjusted(bbox, 1, 0, 1, -1, -1, -1);
    }

private:
    bool generateGround = false;
};

/// LichTowerWingRoom#getVerticalOrientation.
JigsawRecord verticalOrientation(JigsawRecord const& connection, bool upward, JigsawPiece& room) {
    Orientation source = absoluteHorizontal(room.getSourceJigsaw());
    return verticalRecord(connection, upward, opposite(source));
}

bool tryRoof(PieceList& pieces, RandomSource& random, JigsawRecord const& connection, std::optional<std::string> const& roofId, JigsawRecord const& oriented, bool allowClipping,
             JigsawPiece& parent, int depth) {
    auto pc = pick(parentPos(parent), oriented, roofId, kJunctionRoof, random);
    if (!pc) return false;
    auto roof = std::make_unique<WingRoof>(depth, *roofId, *pc);
    if (!allowClipping && collides(pieces, roof->generationCollisionBox())) return false;
    auto* raw = roof.get();
    pieces.push_back(std::move(roof));
    grow(*raw, parent, pieces, random);
    (void)connection;
    return true;
}

class WingBridge final : public LichPiece {
public:
    WingBridge(int depth, JigsawPlaceContext const& ctx, std::string const& id, bool fromCentral) : LichPiece(kWingBridge, depth, id, ctx), fromCentral(fromCentral) {
        groundDelta = 1;
        sortKey     = 2;
    }
    [[nodiscard]] Json save() const override {
        auto j            = JigsawPiece::save();
        j["from_central"] = fromCentral;
        return j;
    }
    void load(Json const& d) override {
        JigsawPiece::load(d);
        fromCentral = d.value("from_central", false);
    }
    void postProcess(Region& region, RandomSource& random, BlockBox const& chunk, BlockPos const& pivot) override {
        JigsawPiece::postProcess(region, random, chunk, pivot);
        if (!fromCentral) return;
        auto const& src   = getSourceJigsaw();
        BlockPos    at    = add(templatePos(), src.pos);
        BlockPos    left  = relative(at, clockwise(src.front));
        BlockPos    right = relative(at, counterClockwise(src.front));
        removeIfBanister(region, left, chunk);
        removeIfBanister(region, up(left), chunk);
        removeIfBanister(region, right, chunk);
        removeIfBanister(region, BlockPos{right.x, right.y - 1, right.z}, chunk);
    }

    static void tryRoomAndBridge(JigsawPiece& parent, PieceList& pieces, RandomSource& random, JigsawRecord const& c, bool fromCentral, int roomMax, bool ground, int depth,
                                 std::optional<std::string> override);
    static bool tryBridge(JigsawPiece& parent, PieceList& pieces, RandomSource& random, JigsawRecord const& c, bool fromCentral, int roomMax, bool ground, int depth,
                          std::optional<std::string> const& bridgeId, bool allowClipping, std::optional<std::string> const& override, bool tiny);
    static void putCover(JigsawPiece& parent, PieceList& pieces, RandomSource& random, JigsawRecord const& c, bool noWindow, int depth);
    bool tryGenerateRoom(PieceList& pieces, RandomSource& random, int roomMax, bool ground, std::optional<std::string> const& override, bool tiny);
    static bool tryPlaceRoom(PieceList& pieces, RandomSource& random, std::optional<std::string> const& roomId, JigsawRecord const& c, int size, bool canPutGround,
                             bool allowClipping, JigsawPiece& parent, int depth);

private:
    bool fromCentral = false;
};

class WingRoom final : public LichPiece {
public:
    WingRoom(int depth, JigsawPlaceContext const& ctx, std::string const& id, int size, bool ground, bool canLadder, RandomSource& random)
    : LichPiece(kWingRoom, depth, id, ctx), roomSize(size), generateGround(ground) {
        placeSettings.processors.push_back(roomSpawners());
        sx::setIgnoreWaterlogging(placeSettings.processors, true);
        auto ladders = canLadder ? ladderPlacements(size) : std::set<std::string>{};
        ladderIndex  = canLadder ? pickFirst([&](std::string const& t) { return ladders.contains(t); }) : -1;
        ladderTarget = ladderIndex >= 0 ? spareJigsaws[ladderIndex].target : std::string();
        roofFallback = canLadder ? pickFirst([](std::string const& t) { return t == kJunctionRoof; }) : -1;
        generateCeilingPlacements(random);
    }
    [[nodiscard]] int spawnLabel() const override { return kInteriorSpawns; }
    [[nodiscard]] Json save() const override {
        auto j                          = JigsawPiece::save();
        j["room_size"]                  = roomSize;
        j["gen_ground"]                 = generateGround;
        j["ladder_index"]               = ladderIndex;
        j["roof_index"]                 = roofFallback;
        j["allowed_ceiling_placements"] = ceiling;
        return j;
    }
    void load(Json const& d) override {
        JigsawPiece::load(d);
        roomSize       = d.value("room_size", 0);
        generateGround = d.value("gen_ground", false);
        ladderIndex    = d.value("ladder_index", -1);
        roofFallback   = d.value("roof_index", -1);
        ceiling        = d.value("allowed_ceiling_placements", std::vector<int>{});
        ladderTarget   = ladderIndex >= 0 && ladderIndex < static_cast<int>(spareJigsaws.size()) ? spareJigsaws[ladderIndex].target : std::string();
        placeSettings.processors.clear();
        addDefaultProcessors(placeSettings.processors);
        placeSettings.processors.push_back(roomSpawners());
        sx::setIgnoreWaterlogging(placeSettings.processors, true);
    }

    void process(JigsawPiece& parent, PieceList& pieces, RandomSource& random, JigsawRecord const& c, int index) override {
        if (c.target == kJunctionBridge) {
            if (roomSize < 1) return;
            bool terminate        = genDepth > 30 || random.nextInt(towerStackIndex() * 2 + 1) == 0;
            bool tooCloseToGround = generateGround || (span(bbox, 1) > 11 && c.pos.y < 7);
            if (terminate || tooCloseToGround) WingBridge::putCover(*this, pieces, random, c, true, genDepth + 1);
            else WingBridge::tryRoomAndBridge(*this, pieces, random, c, false, roomSize - random.nextInt(2), false, genDepth + 1, std::nullopt);
            return;
        }
        if (c.target == kJunctionRoof) {
            if (ladderIndex < 0) putRoof(pieces, random, c);
            return;
        }
        if (c.target == kJunctionBeard) {
            if (frontIsDown(getSourceJigsaw())) return; // the room is entered from below by a ladder
            auto oriented = verticalOrientation(c, false, *this);
            if (generateGround) tryBeard(pieces, random, c, getTrim(random, roomSize), oriented, true, true);
            else {
                for (auto const& b : shuffledBeards(random, roomSize))
                    if (tryBeard(pieces, random, c, b, oriented, false, false)) return;
                tryBeard(pieces, random, c, getFallbackBeard(random, roomSize), oriented, true, false);
            }
        } else if (c.target == kJunctionDecor) {
            RoomDecor::addDecor(*this, pieces, random, c, genDepth + 1, false);
        }
        if (ladderIndex == index && ladderTarget == c.target && !ladderTarget.empty()) {
            int  offset = ladderTarget.back() - '0';
            auto roomId = getRoomUpwards(random, roomSize, offset);
            if (roomId && (*roomId == templateName || *roomId == parentTemplate(parent))) roomId = getRoomUpwards(random, roomSize, offset); // one reroll
            JigsawRecord top = c;
            top.pos          = BlockPos{c.pos.x, c.pos.y + span(bbox, 1) - c.pos.y - 1, c.pos.z};
            if (auto pc = pick(templatePos(), top, roomId, c.target, random)) {
                BlockBox box   = pc->makeBoundingBox();
                bool     fits  = !collides(pieces, extrudeUp(box, core::lich::mthCeil(span(box, 1) * 1.5F)));
                bool     canL  = fits && horizontalFront(getSourceJigsaw()) && random.nextBoolean();
                auto     room  = std::make_unique<WingRoom>(genDepth + 1, *pc, *roomId, roomSize, false, canL, random);
                if (!collides(pieces, adjusted(room->bbox, 1, 0, 1, -1, 0, -1))) {
                    auto* raw = room.get();
                    pieces.push_back(std::move(room));
                    grow(*raw, *this, pieces, random);
                    return;
                }
            }
            if (roofFallback >= 0 && roofFallback < static_cast<int>(spareJigsaws.size())) putRoof(pieces, random, spareJigsaws[roofFallback]);
        }
    }

    void postProcess(Region& region, RandomSource& random, BlockBox const& chunk, BlockPos const& pivot) override {
        JigsawPiece::postProcess(region, random, chunk, pivot);
        auto const& src = getSourceJigsaw();
        if (frontIsDown(src)) {
            BlockPos at = add(templatePos(), src.pos);
            if (chunk.contains(at.x, at.y, at.z)) {
                Json ladder{{"facing", dirName(src.top)}};
                for (int dy = -1; dy <= 1; ++dy) placeBlock(region, "minecraft:ladder", ladder, at.x, at.y + dy, at.z, chunk);
                for (int dy = 2; dy <= 5; ++dy) placeBlock(region, "minecraft:air", Json::object(), at.x, at.y + dy, at.z, chunk);
            }
        }
        if (ladderIndex >= 0 && ladderIndex < static_cast<int>(spareJigsaws.size())) {
            auto const& lj    = spareJigsaws[ladderIndex];
            BlockPos    start = add(templatePos(), lj.pos);
            if (chunk.contains(start.x, start.y, start.z)) {
                int  endY = start.y + span(bbox, 1) - lj.pos.y - 1;
                Json ladder{{"facing", dirName(lj.top)}};
                for (int y = start.y; y < endY; ++y) placeBlock(region, "minecraft:ladder", ladder, start.x, y, start.z, chunk);
                placeBlock(region, "minecraft:cobblestone", Json::object(), start.x, endY, start.z, chunk);
            }
        }
        if (generateGround) {
            for (auto [x, z] : {std::pair{bbox.minX, bbox.minZ}, std::pair{bbox.maxX, bbox.minZ}, std::pair{bbox.maxX, bbox.maxZ}, std::pair{bbox.minX, bbox.maxZ}}) {
                if (!chunk.contains(x, bbox.minY, z)) continue;
                placeBlock(region, "minecraft:stone_bricks", Json::object(), x, bbox.minY, z, chunk);
                placeBlock(region, "minecraft:stone_bricks", Json::object(), x, bbox.minY + 1, z, chunk);
            }
        }
    }

protected:
    void handleDataMarker(std::string const& label, BlockPos const& pos0, Region& region, RandomSource& random, BlockBox const& chunk, Rotation) override;

private:
    static char const* dirName(Orientation o) {
        switch (o) {
        case Orientation::North: return "north";
        case Orientation::South: return "south";
        case Orientation::West: return "west";
        case Orientation::East: return "east";
        }
        return "north";
    }
    template <class F>
    int pickFirst(F&& filter) const {
        for (std::size_t i = 0; i < spareJigsaws.size(); ++i)
            if (filter(spareJigsaws[i].target)) return static_cast<int>(i);
        return -1;
    }
    int towerStackIndex() const {
        bool above = ladderIndex >= 0;
        bool below = frontIsDown(getSourceJigsaw());
        return above && !below ? 0 : above ? 1 : 2;
    }
    bool putRoof(PieceList& pieces, RandomSource& random, JigsawRecord const& c) {
        auto     oriented = verticalOrientation(c, true, *this);
        BlockBox ext      = BlockBox{bbox.minX, bbox.maxY + 1, bbox.minZ, bbox.maxX, bbox.maxY + 1, bbox.maxZ};
        auto     dir      = opposite(oriented.top);
        ext               = add2(ext, dir);
        bool side         = horizontalFront(c) && collides(pieces, ext);
        for (auto const& r : shuffledRoofs(random, roomSize, side))
            if (tryRoof(pieces, random, c, r, oriented, false, *this, genDepth + 1)) return true;
        tryRoof(pieces, random, c, getFallbackRoof(random, roomSize), oriented, true, *this, genDepth + 1);
        return false;
    }
    static BlockBox add2(BlockBox b, Orientation d) {
        auto s = step(d);
        if (s.x > 0) b.maxX += s.x;
        if (s.x < 0) b.minX += s.x;
        if (s.z > 0) b.maxZ += s.z;
        if (s.z < 0) b.minZ += s.z;
        return b;
    }
    bool tryBeard(PieceList& pieces, RandomSource& random, JigsawRecord const&, std::optional<std::string> const& id, JigsawRecord const& oriented, bool allowClipping,
                  bool ground) {
        auto pc = pick(templatePos(), oriented, id, kJunctionBeard, random);
        if (!pc) return false;
        auto beard = std::make_unique<WingBeard>(genDepth + 1, *id, *pc, ground);
        if (!allowClipping && collides(pieces, beard->generationCollisionBox())) return false;
        auto* raw = beard.get();
        pieces.push_back(std::move(beard));
        grow(*raw, *this, pieces, random);
        return true;
    }
    /// generateCeilingPlacements: one hanging marker per x and z column.
    void generateCeilingPlacements(RandomSource& random) {
        int width = std::max(span(bbox, 0), span(bbox, 2));
        ceiling.assign(std::max(width, 0), -1);
        auto const* t = gs::templates::get(templateName);
        if (!t || width <= 0) return;
        std::vector<BlockPos> hangers;
        for (auto const& m : t->data.value("markers", Json::array())) {
            auto meta = m.value("data", Json::object()).value("metadata", std::string());
            if (meta.rfind("rope", 0) != 0 && meta.rfind("chain", 0) != 0) continue;
            auto p = m.value("pos", std::vector<int>{0, 0, 0});
            hangers.push_back(BlockPos{p[0], p[1], p[2]});
        }
        if (hangers.empty()) return;
        // Util.shuffle, then drop the ones sharing an x or z (the chance filter of upstream keeps the
        // markers whose chance roll fails out of the dedup, as filterAlignedPieces does)
        for (int i = static_cast<int>(hangers.size()); i > 1; --i) std::swap(hangers[i - 1], hangers[random.nextInt(i)]);
        std::set<int> xs, zs;
        for (auto const& p : hangers) {
            if (xs.contains(p.x) || zs.contains(p.z)) continue;
            xs.insert(p.x);
            zs.insert(p.z);
            if (p.x >= 0 && p.x < width) ceiling[p.x] = p.z;
        }
    }
    bool canHang(BlockPos const& pos) const {
        int dx = pos.x - bbox.minX, dz = pos.z - bbox.minZ;
        return dx >= 0 && dx < static_cast<int>(ceiling.size()) && ceiling[dx] == dz;
    }
    /// LichTowerWingRoom#parseRange: "n" or "a-b" (StringUtils.isNumeric parts), else lo..hi.
    static int parseRange(std::string const& s, RandomSource& random, int lo, int hi) {
        auto parts = javaSplit(s, '-');
        if (parts.size() == 1 && isNumeric(parts[0])) return std::stoi(parts[0]);
        if (parts.size() == 2 && isNumeric(parts[0]) && isNumeric(parts[1])) return random.nextIntBetweenInclusive(std::stoi(parts[0]), std::stoi(parts[1]));
        return random.nextIntBetweenInclusive(lo, hi);
    }
    /// LichTowerWingRoom#getHeadRotation: "n" or "a+b" (a..a+b, mod 16), else 0..15.
    static int headRotation(std::string const& s, RandomSource& random) {
        auto parts = javaSplit(s, '+');
        if (parts.size() == 1 && isNumeric(parts[0])) return std::stoi(parts[0]);
        if (parts.size() == 2 && isNumeric(parts[0]) && isNumeric(parts[1])) {
            int src = std::stoi(parts[0]), extra = std::stoi(parts[1]);
            return ((random.nextIntBetweenInclusive(src, src + extra) % 16) + 16) % 16;
        }
        return random.nextIntBetweenInclusive(0, 15);
    }
    /// putCandles: min(4, the label's range or 1..3) lit candles (none at 0).
    void putCandles(Region& region, BlockBox const& chunk, RandomSource& random, std::vector<std::string> const& params, BlockPos const& pos, std::string const& id) {
        int amount = std::min(4, params.size() == 2 ? parseRange(params[1], random, 1, 3) : random.nextIntBetweenInclusive(1, 3));
        if (amount <= 0) return;
        placeBlock(region, id, Json{{"lit", true}, {"candles", amount}}, pos.x, pos.y, pos.z, chunk);
    }
    void handleDataParams(BlockPos const& pos, Region& region, RandomSource& random, BlockBox const& chunk, std::vector<std::string> const& params, Rotation dataRotation);

    int                roomSize       = 0;
    bool               generateGround = false;
    int                ladderIndex    = -1;
    std::string        ladderTarget;
    int                roofFallback   = -1;
    std::vector<int>   ceiling;
};

void WingRoom::handleDataMarker(std::string const& label, BlockPos const& pos0, Region& region, RandomSource& random, BlockBox const& chunk, Rotation) {
    BlockPos pos      = pos0;
    auto     modified = javaSplit(label, '>');
    std::string variety = modified.size() == 2 ? modified[1] : label;
    if (modified.size() == 2) {
        auto const& mod = modified[0];
        bool        rope = mod.rfind("rope", 0) == 0, chain = mod.rfind("chain", 0) == 0;
        if (rope || chain) {
            // danglingBlock: the rope / chain hangs only from the ceiling cell this room allows
            auto chance = javaSplit(mod.substr(rope ? 4 : 5), '%');
            if (!canHang(pos)) return;
            int len = parseRange(chance.empty() ? std::string() : chance.back(), random, 1, 2);
            if (len > 0) {
                for (int i = 0; i < len; ++i) placeBlock(region, rope ? "gloamingforest:rope" : "minecraft:chain", Json::object(), pos.x, pos.y - i, pos.z, chunk);
                pos.y -= len;
            }
        } else if (mod == "pedestal") {
            placeBlock(region, "gloamingforest:twisted_stone_pillar", Json::object(), pos.x, pos.y, pos.z, chunk);
            pos.y += 1;
        } else if (mod == "below") {
            pos.y -= 1; // the 9x9 winding_ways room
        }
    }
    auto direction = javaSplit(variety, '@');
    if (direction.empty()) return;
    Rotation dataRotation = direction.size() == 1 ? Rotation::Clockwise180 : relativeRotation(Orientation::North, directionOr(direction[1], Orientation::South));
    auto permutations = javaSplit(direction[0], '|');
    if (permutations.empty()) return;
    std::string const& chosen = permutations[random.nextInt(static_cast<int>(permutations.size()))];
    auto               params = javaSplit(chosen, ':');
    if (params.empty()) return;
    placeBlock(region, "minecraft:air", Json::object(), pos.x, pos.y, pos.z, chunk); // removeBlock: the data marker's block entity
    handleDataParams(pos, region, random, chunk, params, dataRotation);
}

void WingRoom::handleDataParams(BlockPos const& pos, Region& region, RandomSource& random, BlockBox const& chunk, std::vector<std::string> const& params, Rotation dataRotation) {
    if (!chunk.contains(pos.x, pos.y, pos.z)) return;
    auto const& k     = params[0];
    int         dim   = region.dimension();
    Rotation    state = compose(placeSettings.rotation, dataRotation); // placeSettings.getRotation().getRotated(dataRotation)
    auto put = [&](std::string const& id, Json const& states = Json::object()) { placeBlock(region, id, states, pos.x, pos.y, pos.z, chunk); };
    auto facing = [&](Rotation r) { return dirName(rotate(Orientation::North, r)); };
    // the heads turn with placeSettings.getRotation().getRotated(dataRotation.getRotated(CLOCKWISE_180))
    Rotation headTurn = compose(placeSettings.rotation, compose(dataRotation, Rotation::Clockwise180));
    auto     turnHead = [&](int rotation16) { return (rotation16 + 4 * quarterTurns(headTurn)) % 16; };
    if (k == "air" || k == "empty") return;
    if (k == "bookshelf") return put("minecraft:bookshelf");
    if (k == "canopy_shelf" || k == "canopy_bookshelf") return put("gloamingforest:canopy_bookshelf");
    if (k == "stone_brick_slab") return put("minecraft:stone_brick_slab");
    if (k == "lava") return put("minecraft:lava");
    if (k == "water") return put("minecraft:water");
    if (k == "firefly_jar") return put("gloamingforest:firefly_jar");
    if (k == "terrorcotta_arcs") return put("gloamingforest:terrorcotta_arcs");
    if (k == "mason_jar") {
        // putMasonJar: the jar's loot table (or the item the label names), its item turned with the piece
        put("gloamingforest:mason_jar");
        if (params.size() >= 2) {
            static std::map<std::string, std::string> const kTables{{"jar", "gloamingforest:chests/tower_jars"},
                                                                    {"room", "gloamingforest:chests/tower_room"},
                                                                    {"library", "gloamingforest:chests/tower_library"},
                                                                    {"potion", "gloamingforest:chests/tower_potion"},
                                                                    {"enchanting", "gloamingforest:chests/tower_enchanting"}};
            // a known table fills the jar; any other label is a table id, or the item when no such
            // table fills it (fillFromLootTable fails)
            auto it   = kTables.find(params[1]);
            Json data = it != kTables.end() ? Json{{"table", it->second}} : Json{{"table", bySeparator(params[1], '.')}, {"item", bySeparator(params[1], '.')}};
            data["seed"] = random.nextLong();
            data["item_rotation"] = quarterTurns(placeSettings.rotation) * 4 + (params.size() == 3 ? headRotation(params[2], random) : 0);
            gf::feature::lich::pending::add(dim, pos, "jar", data);
        }
        if (game::blockId(region.getBlock(up(pos))) == "gloamingforest:canopy_bookshelf")
            placeBlock(region, "gloamingforest:canopy_slab", Json{{"type", "top"}}, pos.x, pos.y + 1, pos.z, chunk);
        return;
    }
    if (k == "canopy_slab") return put("gloamingforest:canopy_slab");
    if (k == "canopy_stairs") return put("gloamingforest:canopy_stairs");
    static std::map<std::string, char const*> const kHeads{{"creeper_head", "minecraft:creeper_head"},
                                                           {"skeleton_skull", "minecraft:skeleton_skull"},
                                                           {"wither_skull", "minecraft:wither_skeleton_skull"},
                                                           {"zombie_head", "minecraft:zombie_head"}};
    if (auto h = kHeads.find(k); h != kHeads.end()) {
        int rotation = params.size() >= 2 ? headRotation(params[1], random) : random.nextIntBetweenInclusive(0, 15);
        return put(h->second, Json{{"rotation", turnHead(rotation)}});
    }
    static std::map<std::string, char const*> const kCandleHeads{{"creeper_candle", "gloamingforest:creeper_skull_candle"},
                                                                 {"skeleton_candle", "gloamingforest:skeleton_skull_candle"},
                                                                 {"wither_candle", "gloamingforest:wither_skeleton_skull_candle"},
                                                                 {"zombie_candle", "gloamingforest:zombie_skull_candle"}};
    auto putHeadCandles = [&](BlockPos const& at, char const* id) {
        int amount = std::min(4, params.size() >= 2 ? parseRange(params[1], random, 1, 3) : random.nextIntBetweenInclusive(1, 3));
        if (amount <= 0) return;
        int rotation = params.size() >= 3 ? headRotation(params[2], random) : random.nextIntBetweenInclusive(0, 15);
        placeBlock(region, id, Json{{"lighting", "normal"}, {"candles", amount}, {"rotation", turnHead(rotation)}}, at.x, at.y, at.z, chunk);
    };
    if (auto h = kCandleHeads.find(k); h != kCandleHeads.end()) return putHeadCandles(pos, h->second);
    if (k == "spawner" || k == "sinister_spawner") {
        // configureBaseSpawner: one mob (the label's list or the default roll) with the custom spawn
        // rules block light 0..7 / sky light 0..15; the spawn range from the third parameter
        std::string mob = pickRandomMob(random, params);
        int         range = 0;
        if (k == "spawner" && params.size() == 3 && isNumeric(params[2])) range = std::clamp(std::stoi(params[2]), 1, 16);
        if (k == "sinister_spawner" && params.size() >= 3 && isNumeric(params[2])) range = std::clamp(std::stoi(params[2]), 1, 16);
        gs::TemplateSettings s;
        s.processors.push_back(sx::compileSpawnerProcessor(range, 0.0F, {{mob, 1}}));
        s.clip = chunk;
        s.seed = core::lich::blockPosAsLong(pos.x, pos.y, pos.z); // no draw from the piece random (upstream draws none here)
        bool placed = gs::templates::place(gid(k == "spawner" ? "stamp/spawner" : "stamp/sinister_spawner"), region, pos, s);
        if (!placed) placeSpawner(region, chunk, pos.x, pos.y, pos.z, mob);
        if (k == "sinister_spawner") {
            // putSinisterSpawner: the ominous flame, the ominous spawner drops, the entity scan range
            Json data{{"entity", mob}};
            if (range > 0) data["range"] = range;
            if (params.size() >= 4 && isNumeric(params[3])) data["scan"] = std::clamp(std::stoi(params[3]), 1, 32);
            gf::feature::lich::pending::add(dim, pos, "sinister", data);
        }
        return;
    }
    if (k == "brewing_stand") {
        // putBrewingStand: three bottles of one potion, blaze powder and fuel
        put("minecraft:brewing_stand", Json{{"has_bottle_0", true}, {"has_bottle_1", true}, {"has_bottle_2", true}});
        bool splash = random.nextInt(4) == 0;
        int  roll   = random.nextInt(7);
        int  aux    = roll == 6 ? 22 : (roll == 4 || roll == 5) ? 28 : (roll >= 1 && roll <= 3) ? 21 : 0; // strong healing / regeneration / healing / water
        int  blaze  = random.nextIntBetweenInclusive(1, 5);
        int  fuel   = random.nextIntBetweenInclusive(10, 20);
        gf::feature::lich::pending::add(dim, pos, "brewing_stand", Json{{"splash", splash}, {"aux", aux}, {"blaze", blaze}, {"fuel", fuel}});
        return;
    }
    if (k == "lectern") {
        // putTrappableLectern: half of them hold a writable book, the other half a Death Tome mimic
        bool mimic = random.nextBoolean();
        put("minecraft:lectern", Json{{"facing", facing(state)}, {"has_book", !mimic}});
        gf::feature::lich::pending::add(dim, pos, "lectern", Json{{"mimic", mimic}});
        return;
    }
    if (k == "chiseled_canopy_shelf") {
        // putTrappableBookshelf: a twelfth are Death Tome spawners; two thirds of the slots hold a book
        bool hostile = random.nextInt(12) == 0;
        Json states{{"facing", facing(state)}, {"spawner", hostile}};
        std::vector<int> filled;
        for (int i = 0; i < 6; ++i)
            if (random.nextInt(3) != 0) {
                filled.push_back(i);
                states["slot_" + std::to_string(i) + "_occupied"] = true;
            }
        put("gloamingforest:chiseled_canopy_bookshelf", states);
        Json slots = Json::array();
        for (int i : filled) {
            // spawner shelves never hold enchanted books; otherwise 1 in 16 is enchanted at 1..40
            int enchant = hostile || random.nextInt(16) != 0 ? 0 : random.nextIntBetweenInclusive(1, 40);
            slots.push_back(Json{{"slot", i}, {"enchant", enchant}});
        }
        gf::feature::lich::pending::add(dim, pos, "shelf", Json{{"slots", slots}, {"hostile", hostile}});
        return;
    }
    if (k == "chest" || k == "trapped_chest") {
        // putChest: the chest turned with the piece and the marker, its loot table, a top slab above it
        std::string table;
        if (params.size() == 2) {
            static std::map<std::string, std::string> const kTables{{"room", "gloamingforest:chests/tower_room"},
                                                                    {"library", "gloamingforest:chests/tower_library"},
                                                                    {"potion", "gloamingforest:chests/tower_potion"},
                                                                    {"enchanting", "gloamingforest:chests/tower_enchanting"}};
            auto it = kTables.find(params[1]);
            table   = it != kTables.end() ? it->second : bySeparator(params[1], '.');
        }
        std::string id = k == "chest" ? "minecraft:chest" : "minecraft:trapped_chest";
        put(id, Json{{"facing", facing(state)}});
        if (!table.empty()) placeContainer(region, pos, id, rotate(Orientation::North, state), table, random.nextLong());
        if (game::blockId(region.getBlock(up(pos))) == "gloamingforest:canopy_bookshelf")
            placeBlock(region, "gloamingforest:canopy_slab", Json{{"type", "top"}}, pos.x, pos.y + 1, pos.z, chunk);
        return;
    }
    if (k == "candle" || k == "candles") return putCandles(region, chunk, random, params, pos, "minecraft:candle");
    static char const* const kColors[] = {"white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray", "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black"};
    for (auto const* c : kColors)
        if (k == std::string(c) + "_candle") return putCandles(region, chunk, random, params, pos, "minecraft:" + k);
    if (k == "water_cauldron") {
        int amount = std::min(3, params.size() == 2 ? parseRange(params[1], random, 1, 3) : random.nextIntBetweenInclusive(1, 3));
        if (amount <= 0) return put("minecraft:cauldron");
        return put("minecraft:water_cauldron", Json{{"level", amount}});
    }
    if (k == "zombie_trap") {
        // putZombieTrap: a capped (or post) iron fence with a zombie leashed to it on a random side
        // that stays inside the chunk
        bool open = region.getBlock(up(pos)).isAir();
        put("gloamingforest:wrought_iron_fence", Json{{"post", open ? "capped" : "post"}});
        std::vector<Orientation> dirs{Orientation::North, Orientation::East, Orientation::South, Orientation::West};
        // Direction.Plane.HORIZONTAL.shuffledCopy(random): Util.shuffle over [NORTH, EAST, SOUTH, WEST]
        for (int i = static_cast<int>(dirs.size()); i > 1; --i) std::swap(dirs[i - 1], dirs[random.nextInt(i)]);
        int xIn = ((pos.x % 16) + 16) % 16, zIn = ((pos.z % 16) + 16) % 16;
        std::erase_if(dirs, [&](Orientation d) {
            return (xIn == 0 && d == Orientation::West) || (zIn == 0 && d == Orientation::North) || (xIn == 15 && d == Orientation::East) || (zIn == 15 && d == Orientation::South);
        });
        if (dirs.empty()) return;
        Orientation d  = dirs[random.nextInt(static_cast<int>(dirs.size()))];
        BlockPos    at = relative(pos, d);
        gf::feature::lich::pending::add(dim, pos, "leashed_zombie", Json{{"post", {pos.x, pos.y, pos.z}}, {"at", {at.x + 0.5, at.y - 1, at.z + 0.5}}});
        return;
    }
    if (k == "wrought_iron_post") return put("gloamingforest:wrought_iron_fence", Json{{"post", "post"}});
    if (k == "empty_lectern") return put("minecraft:lectern", Json{{"facing", facing(state)}});
    if (k == "candled_lectern") {
        if (random.nextInt(4) != 0) putCandles(region, chunk, random, params, up(pos), "minecraft:candle");
        else putHeadCandles(up(pos), "gloamingforest:skeleton_skull_candle");
        return put("minecraft:lectern", Json{{"facing", facing(state)}});
    }
    // any other label: the block with that id ("." or ":" as the separator), turned like a block
    std::string id = k.find('.') != std::string::npos ? bySeparator(k, '.') : (k.find(':') == std::string::npos ? "minecraft:" + k : k);
    if (game::blockWith(id)) put(id);
    else log().warn("lich: variation label {} names no block ({})", k, templateName);
}

void WingBridge::tryRoomAndBridge(JigsawPiece& parent, PieceList& pieces, RandomSource& random, JigsawRecord const& c, bool fromCentral, int roomMax, bool ground, int depth,
                                  std::optional<std::string> override) {
    if (!ground) {
        if (fromCentral) {
            for (auto const& b : shuffled(random, tid("bridge_from_central")))
                if (tryBridge(parent, pieces, random, c, true, roomMax, false, depth, b, true, override, false)) return;
        } else {
            for (auto const& b : shuffled(random, tid("room_bridge")))
                if (tryBridge(parent, pieces, random, c, false, roomMax, false, depth, b, false, override, false)) return;
            for (auto const& b : shuffled(random, tid("end_bridge")))
                if (tryBridge(parent, pieces, random, c, false, 0, false, depth, b, false, override, true)) return;
        }
    }
    if (fromCentral) tryBridge(parent, pieces, random, c, true, roomMax, ground, depth, randomTemplate(random, tid("bridge_from_central_fallback")), true, override, false);
    else if (!tryBridge(parent, pieces, random, c, false, roomMax, ground, depth, randomTemplate(random, tid("room_bridge_fallback")), true, override, true))
        putCover(parent, pieces, random, c, ground, depth);
}

bool WingBridge::tryBridge(JigsawPiece& parent, PieceList& pieces, RandomSource& random, JigsawRecord const& c, bool fromCentral, int roomMax, bool ground, int depth,
                           std::optional<std::string> const& bridgeId, bool allowClipping, std::optional<std::string> const& override, bool tiny) {
    auto pc = pick(parentPos(parent), c, bridgeId, fromCentral ? kJunctionCenter : kJunctionBridge, random);
    if (!pc) return false;
    auto bridge = std::make_unique<WingBridge>(depth, *pc, *bridgeId, fromCentral);
    if (!allowClipping && collides(pieces, bridge->bbox)) return false;
    if (!bridge->tryGenerateRoom(pieces, random, roomMax, ground, override, tiny)) return false;
    auto* raw = bridge.get();
    pieces.push_back(std::move(bridge));
    grow(*raw, parent, pieces, random);
    return true;
}

void WingBridge::putCover(JigsawPiece& parent, PieceList& pieces, RandomSource& random, JigsawRecord const& c, bool noWindow, int depth) {
    BlockPos pp = parentPos(parent);
    BlockPos a  = add(pp, relative(c.pos, c.front, 1));
    BlockPos b  = add(pp, relative(c.pos, c.front, 3));
    BlockBox clearance{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z), std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
    bool     cobbleOnly = noWindow || collides(pieces, clearance);
    auto     cover      = randomTemplate(random, tid(cobbleOnly ? "door_stopper_fallback" : "door_stopper"));
    auto     pc         = pick(pp, c, cover, kJunctionBridge, random);
    if (!pc) return;
    auto  piece = std::make_unique<WingBridge>(depth, *pc, *cover, false);
    auto* raw   = piece.get();
    pieces.push_back(std::move(piece));
    grow(*raw, parent, pieces, random);
}

bool WingBridge::tryGenerateRoom(PieceList& pieces, RandomSource& random, int roomMax, bool ground, std::optional<std::string> const& override, bool tiny) {
    if (spareJigsaws.empty()) return false;
    if (override) return tryPlaceRoom(pieces, random, override, spareJigsaws.front(), 3, ground, false, *this, genDepth + 1);
    int minSize = tiny ? 0 : 1;
    for (auto const& point : spareJigsaws)
        for (int size = std::max(0, roomMax - 1); size >= minSize; --size)
            if (tryPlaceRoom(pieces, random, rollRandomRoom(random, size), point, size, ground, false, *this, genDepth + 1)) return true;
    return false;
}

bool WingBridge::tryPlaceRoom(PieceList& pieces, RandomSource& random, std::optional<std::string> const& roomId, JigsawRecord const& c, int size, bool canPutGround,
                              bool allowClipping, JigsawPiece& parent, int depth) {
    auto pc = pick(parentPos(parent), c, roomId, kJunctionRoom, random);
    if (!pc) return false;
    bool     ground = canPutGround && c.pos.y < 4;
    BlockBox box    = pc->makeBoundingBox();
    bool     ladder = !collides(pieces, extrudeUp(box, core::lich::mthCeil(span(box, 1) * 1.5F)));
    auto     room   = std::make_unique<WingRoom>(depth, *pc, *roomId, size, ground, ladder, random);
    if (!allowClipping && collides(pieces, room->bbox)) return false;
    auto* raw = room.get();
    pieces.push_back(std::move(room));
    grow(*raw, parent, pieces, random);
    return true;
}

/// LichTowerMagicGallery.
class Gallery final : public LichPiece {
public:
    Gallery(int depth, std::string const& id, JigsawPlaceContext const& ctx) : LichPiece(kGallery, depth, id, ctx) { placeSettings.processors.push_back(roomSpawners()); }
    [[nodiscard]] int spawnLabel() const override { return kInteriorSpawns; }
    void postProcess(Region& region, RandomSource& random, BlockBox const& chunk, BlockPos const& pivot) override {
        auto const& src   = getSourceJigsaw();
        BlockPos    at    = add(templatePos(), src.pos);
        Orientation front = src.front;
        BlockPos    left  = relative(at, clockwise(front));
        Orientation ccw   = counterClockwise(front);
        int         s     = (ccw == Orientation::East || ccw == Orientation::West) ? span(bbox, 0) : span(bbox, 2);
        int         even  = (s + 1) % 2;
        BlockPos    right = relative(at, ccw, 1 + even);
        removeIfBanister(region, left, chunk);
        removeIfBanister(region, up(left), chunk);
        removeIfBanister(region, right, chunk);
        removeIfBanister(region, BlockPos{right.x, right.y - 1, right.z}, chunk);
        if (even == 1) {
            auto r1 = relative(at, ccw, 1);
            removeIfBanister(region, BlockPos{r1.x, r1.y - 1, r1.z}, chunk);
        }
        JigsawPiece::postProcess(region, random, chunk, pivot);
    }
    void process(JigsawPiece&, PieceList& pieces, RandomSource& random, JigsawRecord const& c, int) override {
        if (c.target != kJunctionRoof) return;
        bool odd = (std::min(span(bbox, 0), span(bbox, 2)) & 1) == 1;
        auto roof = randomTemplate(random, tid(odd ? "gallery_odd" : "gallery_even"));
        tryRoof(pieces, random, c, roof, verticalOrientation(c, true, *this), true, *this, genDepth + 1);
    }
    static void tryPlaceGallery(PieceList& pieces, RandomSource& random, std::optional<std::string> const& id, JigsawRecord const& c, JigsawPiece& parent, int depth) {
        auto pc = pick(parentPos(parent), c, id, kJunctionCenter, random);
        if (!pc) return;
        auto  g   = std::make_unique<Gallery>(depth, *id, *pc);
        auto* raw = g.get();
        pieces.push_back(std::move(g));
        grow(*raw, parent, pieces, random);
    }

protected:
    void handleDataMarker(std::string const& label, BlockPos const& pos, Region& region, RandomSource& random, BlockBox const& chunk, Rotation) override {
        placeBlock(region, "minecraft:air", Json::object(), pos.x, pos.y, pos.z, chunk);
        if (label == "painting") {
            // the gallery's magic painting (U22's gloamingforest:magic_painting) facing the piece's
            // rotation of south, its variant named after the gallery; spawned once the chunk is loaded
            static std::map<std::string, std::string> const kVariants{
                {"gloamingforest:lich_tower/gallery/castaway_paradise", "gloamingforest:castaway_paradise"},
                {"gloamingforest:lich_tower/gallery/darkness", "gloamingforest:darkness"},
                {"gloamingforest:lich_tower/gallery/lucid_lands", "gloamingforest:lucid_lands"},
                {"gloamingforest:lich_tower/gallery/music_in_the_mire", "gloamingforest:music_in_the_mire"},
                {"gloamingforest:lich_tower/gallery/the_hostile_paradise", "gloamingforest:the_hostile_paradise"}};
            auto it = kVariants.find(templateName);
            if (it == kVariants.end() || !chunk.contains(pos.x, pos.y, pos.z)) return;
            gf::feature::lich::pending::add(region.dimension(), pos, "magic_painting",
                                            Json{{"variant", it->second}, {"direction", orientationName(rotate(Orientation::South, placeSettings.rotation))}});
            return;
        }
        sx::placePainting(label, pos, region, random, chunk, placeSettings.rotation, 2, 1, "gloamingforest:tower_paintings");
    }
};

void TowerBase::process(JigsawPiece&, PieceList& pieces, RandomSource& random, JigsawRecord const& c, int index) {
    if (c.target == kJunctionBelow) {
        Segment::buildTowerBySegments(pieces, random, c, *this, random.nextIntBetweenInclusive(12, 15));
    } else if (c.target == kJunctionBridge) {
        std::optional<std::string> room;
        if (index == casketWingIndex) room = randomTemplate(random, tid("9x9/special"));
        if (room && !gs::templates::get(*room)) room.reset(); // U22's keepsake casket room is not there
        if (room || c.pos.y < 6) WingBridge::tryRoomAndBridge(*this, pieces, random, c, true, 4, true, genDepth + 1, room);
    } else if (c.target == kJunctionDecor) {
        RoomDecor::addDecor(*this, pieces, random, c, genDepth + 1, true);
    } else if (c.target == "gf:lich_tower/tower_trim") {
        auto pc = pick(templatePos(), c, gid("central_trim"), "gf:lich_tower/tower_trim", random);
        if (!pc) return;
        auto  trim = std::make_unique<BaseTrim>(*pc);
        auto* raw  = trim.get();
        pieces.push_back(std::move(trim));
        grow(*raw, *this, pieces, random);
    }
}

void Segment::buildTowerBySegments(PieceList& pieces, RandomSource& random, JigsawRecord const& source, JigsawPiece& base, int segments) {
    std::string              slice = gid("tower_slice");
    std::vector<LichPiece*>  built;
    JigsawPiece*             prior    = &base;
    JigsawRecord             priorJig = source;
    int                      mob      = random.nextIntBetweenInclusive(0, 5);
    for (int i = 0; i < segments; ++i) {
        auto pc = pick(parentPos(*prior), priorJig, slice, kJunctionBelow, random);
        if (!pc) continue;
        bool  wings   = i > (segments >> 1);
        bool  gallery = i == segments - 1;
        auto  seg     = std::make_unique<Segment>(prior->genDepth + 1, *pc, mob == 0, wings, gallery, slice);
        auto* raw     = seg.get();
        pieces.push_back(std::move(seg));
        built.push_back(raw);
        auto const* above = pc->findFirst(kJunctionAbove);
        if (!above) break;
        prior    = raw;
        priorJig = *above;
        mob      = mob == 0 ? random.nextIntBetweenInclusive(2, 5) : mob - 1;
    }
    if (auto pc = pick(parentPos(*prior), priorJig, gid("tower_boss_room"), kJunctionBelow, random)) {
        auto  room = std::make_unique<BossRoom>(*pc);
        auto* raw  = room.get();
        pieces.push_back(std::move(room));
        grow(*raw, *prior, pieces, random);
        auto  boundary = std::make_unique<Segment>(prior->genDepth + 1, *pc, false, false, false, gid("tower_boss_boundary"));
        auto* rawB     = boundary.get();
        pieces.push_back(std::move(boundary));
        grow(*rawB, *prior, pieces, random);
    }
    if (built.empty()) return;
    // the topmost slice first (the gallery), then the others from the bottom up
    LichPiece* top = built.back();
    built.pop_back();
    grow(*top, built.empty() ? base : *built.back(), pieces, random);
    JigsawPiece* parent = &base;
    for (auto* piece : built) {
        grow(*piece, *parent, pieces, random);
        parent = piece;
    }
}

void Segment::process(JigsawPiece&, PieceList& pieces, RandomSource& random, JigsawRecord const& c, int index) {
    if (c.target == kJunctionBridge) {
        if (!putWings) return;
        if (putGallery) {
            if (index == 2 && random.nextInt(10) == 0) Gallery::tryPlaceGallery(pieces, random, randomTemplate(random, tid("gallery")), c, *this, genDepth + 1);
        } else {
            WingBridge::tryRoomAndBridge(*this, pieces, random, c, true, 4, false, genDepth + 1, std::nullopt);
        }
    } else if (c.target == "gf:mob_bridge" && putMobBridge) {
        // either keep the jigsaw's rotation or spin it 180 (FrontAndTop.fromFrontAndTop(front,
        // top.getOpposite()); the slice's mob bridge jigsaw is a vertical one, so its top turns)
        JigsawRecord forPlacement = c;
        if (!random.nextBoolean()) forPlacement.top = opposite(c.top);
        auto id = randomTemplate(random, tid("mob_bridge"));
        auto pc = pick(templatePos(), forPlacement, id, "gf:mob_bridge", random);
        if (!pc) return;
        auto  bridge = std::make_unique<SpawnerBridge>(genDepth + 1, *id, *pc, random.nextBoolean());
        auto* raw    = bridge.get();
        pieces.push_back(std::move(bridge));
        grow(*raw, *this, pieces, random);
    }
}

// ---- the yard (LichYardBox, LichPerimeterFence, LichYardGrave, LichYardLights) --------------------------

constexpr char const* kYardPath   = "gloamingforest:GloamingLTPath";
constexpr char const* kYardFence  = "gloamingforest:GloamingLTFence";
constexpr char const* kYardGrave  = "gloamingforest:GloamingLTGrave";
constexpr char const* kYardLights = "gloamingforest:GloamingLTLight";

/// Java's Direction for the yard pieces: the four horizontals and UP (LichYardBox's "no axis").
struct YardDir {
    bool        up = true;
    Orientation h  = Orientation::North;
    /// Direction#ordinal (DOWN 0, UP 1, NORTH 2, SOUTH 3, WEST 4, EAST 5).
    [[nodiscard]] int ordinal() const {
        if (up) return 1;
        switch (h) {
        case Orientation::North: return 2;
        case Orientation::South: return 3;
        case Orientation::West: return 4;
        case Orientation::East: return 5;
        }
        return 1;
    }
    static YardDir fromOrdinal(int o) {
        switch (o) {
        case 2: return {false, Orientation::North};
        case 3: return {false, Orientation::South};
        case 4: return {false, Orientation::West};
        case 5: return {false, Orientation::East};
        default: return {true, Orientation::North};
        }
    }
    /// The axis: 0 x, 1 y, 2 z.
    [[nodiscard]] int axis() const { return up ? 1 : (h == Orientation::East || h == Orientation::West) ? 0 : 2; }
};

BlockBox inflated(BlockBox const& b, int n) { return BlockBox{b.minX - n, b.minY - n, b.minZ - n, b.maxX + n, b.maxY + n, b.maxZ + n}; }
BlockBox inflated(BlockBox const& b, int x, int y, int z) { return BlockBox{b.minX - x, b.minY - y, b.minZ - z, b.maxX + x, b.maxY + y, b.maxZ + z}; }
BlockBox withY(BlockBox const& b, int minY, int maxY) { return BlockBox{b.minX, minY, b.minZ, b.maxX, maxY, b.maxZ}; }
/// BoundingBoxUtils.wrappedCoordinates(padding, a, b).
BlockBox wrapped(int padding, BlockPos const& a, BlockPos const& b) {
    return BlockBox{std::min(a.x, b.x) - padding, std::min(a.y, b.y) - padding, std::min(a.z, b.z) - padding,
                    std::max(a.x, b.x) + padding, std::max(a.y, b.y) + padding, std::max(a.z, b.z) + padding};
}
/// BoundingBoxUtils.safeRetract(box, direction, length) for a horizontal direction or UP.
BlockBox safeRetract(BlockBox const& b, YardDir d, int length) {
    int axis = d.axis();
    if (span(b, axis) <= length) return b;
    if (d.up) return adjusted(b, 0, 0, 0, 0, -length, 0);
    switch (d.h) {
    case Orientation::West: return adjusted(b, length, 0, 0, 0, 0, 0);
    case Orientation::East: return adjusted(b, 0, 0, 0, -length, 0, 0);
    case Orientation::North: return adjusted(b, 0, 0, length, 0, 0, 0);
    case Orientation::South: return adjusted(b, 0, 0, 0, 0, 0, -length);
    }
    return b;
}
/// BoundingBoxUtils.getIntersectionOfSBBs (nullopt when they do not meet).
std::optional<BlockBox> intersection(BlockBox const& a, BlockBox const& b) {
    if (a.maxX < b.minX || a.minX > b.maxX || a.maxY < b.minY || a.minY > b.maxY || a.maxZ < b.minZ || a.minZ > b.maxZ) return std::nullopt;
    return BlockBox{std::max(a.minX, b.minX), std::max(a.minY, b.minY), std::max(a.minZ, b.minZ), std::min(a.maxX, b.maxX), std::min(a.maxY, b.maxY), std::min(a.maxZ, b.maxZ)};
}
/// BoundingBox#getCenter.
BlockPos center(BlockBox const& b) { return BlockPos{b.minX + (b.maxX - b.minX + 1) / 2, b.minY + (b.maxY - b.minY + 1) / 2, b.minZ + (b.maxZ - b.minZ + 1) / 2}; }
/// BoundingBoxUtils.bottomCenterOf.
BlockPos bottomCenter(BlockBox const& b) { return BlockPos{b.minX + (b.maxX - b.minX + 1) / 2, b.minY, b.minZ + (b.maxZ - b.minZ + 1) / 2}; }
/// Mth.lerp(delta, a, b) (float).
float lerpF(float delta, float a, float b) { return a + delta * (b - a); }
int   clampI(int v, int lo, int hi) { return std::min(std::max(v, lo), hi); }
int   signI(int v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

/// The first of `ids` the block registry knows (Bedrock renamed a few of them).
Block const* firstBlock(std::initializer_list<char const*> ids) {
    for (auto const* id : ids)
        if (auto const* b = game::blockWith(id)) return b;
    return nullptr;
}

bool isDirt(std::string const& id) {
    static std::set<std::string> const kDirt{"minecraft:dirt",      "minecraft:grass_block",    "minecraft:podzol", "minecraft:coarse_dirt",
                                             "minecraft:mycelium",  "minecraft:dirt_with_roots", "minecraft:moss_block", "minecraft:mud",
                                             "minecraft:muddy_mangrove_roots", "minecraft:grass"};
    return kDirt.contains(id) || gf::tags::has(gf::tags::Registry::Block, "minecraft:dirt", id);
}

/// WorldGenLevel#getHeight(type, x, z) of the yard pieces, read from the blocks: the y above the
/// highest block of the column that the heightmap counts (WORLD_SURFACE_WG: any block;
/// OCEAN_FLOOR_WG: blocking motion; MOTION_BLOCKING: blocking motion or fluid; ..._NO_LEAVES: and
/// no leaves). The regions differ in what their own height() answers (a loaded world's
/// BlockSource#getAboveTopSolidBlock has its own idea of solid), so the yard asks the blocks.
template <class BlockAt>
int columnHeight(BlockAt const& blockAt, int minY, int maxY, gf::worldgen::Heightmap type, int x, int z) {
    using H = gf::worldgen::Heightmap;
    for (int y = maxY - 1; y >= minY; --y) {
        Block const& b = blockAt(BlockPos{x, y, z});
        if (b.isAir()) continue;
        if (type == H::WorldSurfaceWg || type == H::WorldSurface) return y + 1;
        auto const& m      = b.getMaterial();
        bool        fluid  = m.mLiquid;
        bool        blocks = m.mBlocksMotion && !fluid;
        switch (type) {
        case H::OceanFloorWg:
        case H::OceanFloor:
            if (blocks) return y + 1;
            break;
        case H::MotionBlocking:
            if (blocks || fluid) return y + 1;
            break;
        case H::MotionBlockingNoLeaves: {
            bool leaves = game::blockId(b).ends_with("leaves") || gf::tags::has(gf::tags::Registry::Block, "minecraft:leaves", game::blockId(b));
            if ((blocks || fluid) && !leaves) return y + 1;
            break;
        }
        default: return y + 1;
        }
    }
    return minY;
}
int surfaceHeight(Region const& region, gf::worldgen::Heightmap type, int x, int z) {
    return columnHeight([&](BlockPos const& p) -> Block const& { return region.getBlock(p); }, region.minY(), region.maxY(), type, x, z);
}

/// LichYardBox: the dirt of the yard (doDirtMotley) and its paths; a path along an axis places
/// its spruce fence and gets the graves along it.
class YardBox final : public gs::Piece, public sb::SortablePiece {
public:
    YardBox(BlockBox const& box, float feather, YardDir dir, bool dirt, float scale, float offset)
    : gs::Piece(kYardPath), feather(feather), dir(dir), dirt(dirt), scale(scale), offset(offset) {
        bbox = box;
    }
    [[nodiscard]] int  getSortKey() const override { return dirt ? std::numeric_limits<int>::min() : std::numeric_limits<int>::min() + 255; }
    [[nodiscard]] bool terrainAdjusted() const { return dirt; } ///< BEARD_BOX for the dirt, NONE for the paths
    [[nodiscard]] Json save() const override {
        auto j          = gs::Piece::save();
        j["feather"]    = feather;
        j["direction"]  = dir.ordinal();
        j["dirt_mix"]   = dirt;
        j["dirt_scale"] = scale;
        j["offset"]     = offset;
        return j;
    }
    void load(Json const& d) override {
        gs::Piece::load(d);
        feather = d.value("feather", 0.0F);
        dir     = YardDir::fromOrdinal(d.value("direction", 1));
        dirt    = d.value("dirt_mix", false);
        scale   = d.value("dirt_scale", 0.0F);
        offset  = d.value("offset", 0.0F);
    }

    void postProcess(Region& region, RandomSource& random, BlockBox const& chunk, BlockPos const&) override {
        auto inter = intersection(bbox, chunk);
        if (!inter || scale == 0.0F) return;
        // the fence only along a path (not the dirt); the box minus the 4 blocks at its start
        BlockBox fenceBounds = generateFence() ? safeRetract(bbox, YardDir{dir.up, opposite(dir.h)}, 4) : bbox;
        for (int z = inter->minZ; z <= inter->maxZ; ++z)
            for (int x = inter->minX; x <= inter->maxX; ++x) processPos(region, random, x, z, fenceBounds, chunk);
    }

    /// LichYardBox#addDecoration: five tries for a grave beside a path (none for the dirt or the
    /// first path).
    void addDecoration(JigsawPiece& parent, PieceList& pieces, RandomSource& random);

private:
    [[nodiscard]] bool generateFence() const { return !dirt; }

    Block const* pickDirt(int x, int y, int z, RandomSource& random) const {
        float s     = scale * 2.5F;
        float randF = random.nextFloat();
        float noise = randF < 0.25F ? randF * 4.0F : core::lich::simplex3(float(x) * s, float(y) * s + 1024.0F, float(z) * s) * 0.5F + 0.5F;
        if (noise > 0.6F) return firstBlock({"minecraft:coarse_dirt"});
        if (noise > 0.4F) return firstBlock({"minecraft:dirt"});
        return firstBlock({"minecraft:dirt_with_roots", "minecraft:rooted_dirt"}); // rooted dirt
    }

    void processPos(Region& region, RandomSource& random, int x, int z, BlockBox const& fenceBounds, BlockBox const& chunk) {
        int      y = surfaceHeight(region, gf::worldgen::Heightmap::MotionBlockingNoLeaves, x, z) - 1;
        BlockPos at{x, y, z};
        if (!isDirt(game::blockId(region.getBlock(at)))) return;
        int   xBorder = std::min(x - bbox.minX, bbox.maxX - x);
        int   zBorder = std::min(z - bbox.minZ, bbox.maxZ - z);
        float border  = static_cast<float>(std::min(xBorder, zBorder)) + offset;
        float level   = border > feather ? 1.0F : std::clamp(border / feather, 0.0F, 1.0F);
        float noise   = core::lich::simplex3(float(x) * scale, float(y) * scale, float(z) * scale) * 0.5F - 0.5F;
        if (noise + level < 0.0F) {
            if (generateFence() && fenceBounds.minX <= x && x <= fenceBounds.maxX && fenceBounds.minZ <= z && z <= fenceBounds.maxZ) {
                float fenceNoise = core::lich::simplex3(float(x) * 0.15F, float(y) * 0.15F - 1024.0F, float(z) * 0.15F) * 0.5F;
                if (std::abs(fenceNoise) > 0.15F) {
                    int  rounded = core::lich::javaRound(fenceNoise + 0.5F);
                    bool edge    = dir.axis() == 2 ? (x == bbox.minX + rounded || x == bbox.maxX - rounded) : (z == bbox.minZ + rounded || z == bbox.maxZ - rounded);
                    if (edge)
                        if (auto const* fence = game::blockWith("minecraft:spruce_fence")) region.setBlock(up(at), *fence, 3); // the shape follows its neighbours
                }
            }
            return;
        }
        Block const* state = dirt ? pickDirt(x, y, z, random) : firstBlock({"minecraft:grass_path", "minecraft:dirt_path"});
        auto const*  air   = game::blockWith("minecraft:air");
        if (state) region.setBlock(at, *state, 3);
        // the plants on it go (a dead bush on 1.25 % of the dirt)
        if (dirt && random.nextFloat() < 0.0125F) {
            if (auto const* bush = firstBlock({"minecraft:deadbush", "minecraft:dead_bush"})) region.setBlock(up(at), *bush, 3);
        } else if (air) region.setBlock(up(at), *air, 3);
        if (air) region.setBlock(up(at, 2), *air, 3);
        (void)chunk;
    }

    float   feather = 0.0F;
    YardDir dir;
    bool    dirt   = false;
    float   scale  = 0.0F;
    float   offset = 0.0F;
};

/// LichYardLights: candles on iron posts along the first path.
class YardLights final : public gs::Piece {
public:
    YardLights(BlockBox const& box, int axis) : gs::Piece(kYardLights), axis(axis) { bbox = box; }
    [[nodiscard]] Json save() const override {
        auto j  = gs::Piece::save();
        j["axis"] = axis;
        return j;
    }
    void load(Json const& d) override {
        gs::Piece::load(d);
        axis = d.value("axis", 1);
    }
    void postProcess(Region& region, RandomSource& random, BlockBox const& chunk, BlockPos const&) override {
        auto inter = intersection(bbox, inflated(chunk, -1));
        if (!inter) return;
        auto isAir = [&](BlockPos const& p) { return game::blockId(region.getBlock(p)) == "minecraft:air"; }; // not cave air
        for (int z = inter->minZ; z <= inter->maxZ; ++z)
            for (int x = inter->minX; x <= inter->maxX; ++x) {
                int      y = surfaceHeight(region, gf::worldgen::Heightmap::WorldSurfaceWg, x, z);
                BlockPos at{x, y, z};
                if (!isAir(at) || random.nextFloat() > 0.125F) continue;
                if (!isAir(BlockPos{x, y, z - 1}) || !isAir(BlockPos{x, y, z + 1}) || !isAir(BlockPos{x + 1, y, z}) || !isAir(BlockPos{x - 1, y, z})) continue;
                bool nearEdge = axis == 2 ? std::min(x - bbox.minX, bbox.maxX - x) < 3 : std::min(z - bbox.minZ, bbox.maxZ - z) < 3;
                if (!nearEdge) continue;
                if (auto const* post = game::blockWith("gloamingforest:wrought_iron_fence")) region.setBlock(at, *post, 3);
                if (auto const* candle = game::blockWithStates("minecraft:candle", Json{{"lit", true}})) region.setBlock(up(at), *candle, 3);
            }
    }

private:
    int axis = 1; ///< Direction.Axis ordinal (x 0, y 1, z 2)
};

/// The jigsaw pieces of the yard keep only the jigsaw replacement and the structure block ignore.
void yardProcessors(std::vector<std::string>& p) {
    p.clear();
    p.push_back("minecraft:jigsaw_replacement");
    p.push_back(json({{"processor_type", "minecraft:block_ignore"}, {"blocks", Json::array({Json{{"Name", "minecraft:structure_block"}}})}}));
}

/// StructureTemplate.calculateRelativePosition(settings, pos): the template position turned by the
/// placement (no mirror: the lich tower never mirrors).
BlockPos turned(BlockPos const& p, Rotation r, BlockPos const& pivot) {
    int i = p.x, k = p.z, l = pivot.x, m = pivot.z;
    switch (r) {
    case Rotation::Counterclockwise90: return BlockPos{l - m + k, p.y, l + m - i};
    case Rotation::Clockwise90: return BlockPos{l + m - k, p.y, m - l + i};
    case Rotation::Clockwise180: return BlockPos{l + l - i, p.y, m + m - k};
    default: return p;
    }
}

/// LichYardGrave: a grave beside a path, with dirt filled under it down to the ground.
class YardGrave final : public LichPiece {
public:
    YardGrave(JigsawPlaceContext const& ctx, std::string const& id) : LichPiece(kYardGrave, 0, id, ctx) {
        yardProcessors(placeSettings.processors);
        terrain     = TerrainAdjustment::BeardBox;
        groundDelta = 1;
    }
    void load(Json const& d) override {
        JigsawPiece::load(d);
        yardProcessors(placeSettings.processors);
    }
    void postProcess(Region& region, RandomSource& random, BlockBox const& chunk, BlockPos const& pivot) override {
        // the box shrunk by one and pushed one block toward the path (safeRetract(front, -1))
        BlockBox fill  = inflated(bbox, -1);
        auto     front = getSourceJigsaw().front;
        switch (front) {
        case Orientation::West: fill.minX -= 1; break;
        case Orientation::East: fill.maxX += 1; break;
        case Orientation::North: fill.minZ -= 1; break;
        case Orientation::South: fill.maxZ += 1; break;
        }
        if (auto applicable = intersection(fill, chunk)) {
            int yUnder = applicable->minY - 1;
            if (auto const* dirtBlock = game::blockWith("minecraft:dirt"))
                for (int x = applicable->minX; x <= applicable->maxX; ++x)
                    for (int z = applicable->minZ; z <= applicable->maxZ; ++z) {
                        int bottom = surfaceHeight(region, gf::worldgen::Heightmap::OceanFloorWg, x, z);
                        for (int y = yUnder; y >= bottom; --y) region.setBlock(BlockPos{x, y, z}, *dirtBlock, 3);
                    }
        }
        JigsawPiece::postProcess(region, random, chunk, pivot);
    }
};

/// LichPerimeterFence: the iron fence around the yard, from the far end of the path to the two
/// grounded wing trims beside the tower.
class PerimeterFence final : public LichPiece {
public:
    PerimeterFence(JigsawPlaceContext const& ctx, std::string const& id, RandomSource& random) : LichPiece(kYardFence, 0, id, ctx) {
        yardProcessors(placeSettings.processors);
        terrain     = TerrainAdjustment::BeardBox;
        groundDelta = 2;
        // a quarter of the fences get a zombie leashed to one of their iron posts
        if (random.nextFloat() > 0.25F) return;
        auto posts = postBlocks();
        if (posts.empty()) return;
        for (int i = static_cast<int>(posts.size()); i > 1; --i) std::swap(posts[i - 1], posts[random.nextInt(i)]); // Util.shuffle
        leashPos = add(templatePos(), turned(posts.front(), placeSettings.rotation, placeSettings.pivot));
    }
    [[nodiscard]] int spawnLabel() const override { return kDenySpawns; }
    [[nodiscard]] int getSortKey() const override { return bbox.maxY; } // higher fences later, so lower slabs do not replace full blocks
    [[nodiscard]] Json save() const override {
        auto j = JigsawPiece::save();
        if (leashPos) j["leash_pos"] = Json::array({leashPos->x, leashPos->y, leashPos->z});
        return j;
    }
    void load(Json const& d) override {
        JigsawPiece::load(d);
        yardProcessors(placeSettings.processors);
        leashPos.reset();
        if (auto it = d.find("leash_pos"); it != d.end() && it->is_array() && it->size() == 3) leashPos = BlockPos{(*it)[0].get<int>(), (*it)[1].get<int>(), (*it)[2].get<int>()};
    }

    [[nodiscard]] std::vector<JigsawRecord> leftJunctions() const { return matchSpareJigsaws([](JigsawRecord const& r) { return r.name == "gf:lich_tower/fence_edge_left"; }); }
    [[nodiscard]] std::vector<JigsawRecord> rightJunctions() const { return matchSpareJigsaws([](JigsawRecord const& r) { return r.name == "gf:lich_tower/fence_edge_right"; }); }
    [[nodiscard]] std::vector<BlockPos> fencePostPositions() const {
        std::vector<BlockPos> out;
        for (auto const& r : leftJunctions()) out.push_back(add(templatePos(), r.pos));
        for (auto const& r : rightJunctions()) out.push_back(add(templatePos(), r.pos));
        return out;
    }

    void postProcess(Region& region, RandomSource& random, BlockBox const& chunk, BlockPos const& pivot) override {
        JigsawPiece::postProcess(region, random, chunk, pivot);
        generateBoundZombie(region, chunk);
        generateEscapeLadder(region, chunk);
    }

private:
    /// The template's iron fence blocks in the POST state (template positions).
    [[nodiscard]] std::vector<BlockPos> postBlocks() const {
        std::vector<BlockPos> out;
        auto const*           t = gs::templates::get(templateName);
        if (!t) return out;
        auto const& palette = t->data.value("palette", Json::array());
        std::vector<bool> isPost;
        for (auto const& e : palette)
            isPost.push_back(e.value("Name", std::string()) == "gloamingforest:wrought_iron_fence" && e.value("Properties", Json::object()).value("post", std::string()) == "post");
        int x = 0, y = 0, z = 0, sx = t->sizeX, sz = t->sizeZ;
        for (auto const& run : t->data.value("blocks", Json::array())) {
            int idx = run[0].get<int>(), n = run[1].get<int>();
            for (int i = 0; i < n; ++i) {
                if (idx >= 0 && idx < static_cast<int>(isPost.size()) && isPost[idx]) out.push_back(BlockPos{x, y, z});
                if (++x == sx) {
                    x = 0;
                    if (++z == sz) {
                        z = 0;
                        ++y;
                    }
                }
            }
        }
        return out;
    }
    void generateBoundZombie(Region& region, BlockBox const& chunk) {
        if (!leashPos || !chunk.contains(leashPos->x, leashPos->y, leashPos->z)) return;
        Orientation facing = getSourceJigsaw().top;
        BlockPos    at     = relative(*leashPos, facing);
        if (!chunk.contains(at.x, at.y, at.z)) return;
        gf::feature::lich::pending::add(region.dimension(), *leashPos, "leashed_zombie",
                                        Json{{"post", {leashPos->x, leashPos->y, leashPos->z}}, {"at", {at.x + 0.5, at.y - 1, at.z + 0.5}}});
    }
    void generateEscapeLadder(Region& region, BlockBox const& chunk) {
        auto const& src = getSourceJigsaw();
        if (spareJigsaws.size() != 1 || !src.frontVertical) return;
        Orientation ladderDir = opposite(src.top);
        BlockPos    column    = relative(getSourcePosition(), ladderDir);
        if (!chunk.contains(column.x, column.y, column.z)) return;
        // level.setBlock for all four once the column's base is in the chunk (not clipped to it)
        BlockBox ladder{column.x, column.y + 1, column.z, column.x, column.y + 4, column.z};
        for (int dy = 1; dy <= 4; ++dy) placeBlock(region, "minecraft:ladder", Json{{"facing", orientationName(ladderDir)}}, column.x, column.y + dy, column.z, ladder);
    }

public:
    /// LichPerimeterFence#addJigsaws: a fence with one spare post (not the front one) keeps the
    /// trees off its escape ladder with a UtilityPiece, a box of 3 around the column 2 out and 2 up.
    void addTreeClearance(PieceList& pieces) const {
        auto const& src = getSourceJigsaw();
        if (spareJigsaws.size() != 1 || !src.frontVertical) return; // ladderDirection.getAxis() != Y
        BlockPos column = up(relative(getSourcePosition(), opposite(src.top), 2), 2);
        BlockBox box{column.x - 3, column.y - 3, column.z - 3, column.x + 3, column.y + 3, column.z + 3};
        pieces.push_back(std::make_unique<sb::UtilityPiece>(genDepth + 1, box, false));
    }

private:

    std::optional<BlockPos> leashPos;
};

/// LichPerimeterFence#nextFence: the next fence of a side, its post shared with `parent`'s first
/// junction, turned by `rotation`, homing onto the trim's height within 20 blocks of it.
PerimeterFence* nextFence(PerimeterFence& parent, PieceList& pieces, RandomSource& random, std::vector<JigsawRecord> const& junctions, Rotation rotation,
                          std::string const& templateId, BlockBox const& destination) {
    if (junctions.empty()) return nullptr;
    JigsawRecord const& junction = junctions.front();
    // FrontAndTop.fromFrontAndTop(front.getOpposite(), rotation.rotate(top))
    JigsawRecord connect = junction;
    if (junction.frontVertical) connect.frontUp = !junction.frontUp;
    else connect.front = opposite(junction.front);
    connect.top       = rotate(junction.top, rotation);
    BlockPos post     = add(parent.templatePos(), junction.pos);
    int      xC       = clampI(post.x, destination.minX, destination.maxX);
    int      zC       = clampI(post.z, destination.minZ, destination.maxZ);
    int      hDist    = std::abs(xC - post.x) + std::abs(zC - post.z); // horizontalManhattanDistance
    int      dY       = 0;
    if (hDist <= 20) dY = clampI(parent.templatePos().y - 1, destination.minY + 2, destination.maxY - 2) - 1 - parent.templatePos().y;
    BlockPos parentPos = up(parent.templatePos(), signI(dY) - 1);
    auto     pc        = JigsawPlaceContext::pickPlaceableJunction(parentPos, junction.pos, connect, templateId, junction.target, random);
    if (!pc) return nullptr;
    auto  fence = std::make_unique<PerimeterFence>(*pc, templateId, random);
    auto* raw   = fence.get();
    pieces.push_back(std::move(fence));
    grow(*raw, parent, pieces, random);
    raw->addTreeClearance(pieces);
    return raw;
}

using JunctionGetter = std::vector<JigsawRecord> (PerimeterFence::*)() const;

/// LichPerimeterFence#generateUntilNearDest.
PerimeterFence* generateUntilNearDest(PieceList& pieces, RandomSource& random, BlockBox const& destBox, int turnAtIndex, PerimeterFence* fence, Rotation turn,
                                      JunctionGetter getter, std::string const& templateId) {
    int  infolded = 0, counterRotated = 0;
    bool foldNext = false;
    int  foldAt   = random.nextInt(turnAtIndex - 1);
    for (int idx = 0; idx < 16; ++idx) {
        bool makeTurn = idx == turnAtIndex;
        bool march    = idx > turnAtIndex;
        if (!fence) break;
        auto junctions = (fence->*getter)();
        if (junctions.empty()) break;
        if (march) {
            // the spare post lines up with the trim: approach it with a right-angle turn
            auto const& first  = junctions.front();
            BlockPos    check  = add(fence->templatePos(), first.pos);
            BlockPos    dest{clampI(check.x, destBox.minX, destBox.maxX), clampI(check.y, destBox.minY, destBox.maxY), clampI(check.z, destBox.minZ, destBox.maxZ)};
            auto        target = step(rotate(first.top, turn));
            if ((dest.x - check.x) * target.x + (dest.z - check.z) * target.z == 0) break;
        } else if (idx == foldAt) {
            infolded       = random.nextIntBetweenInclusive(1, idx + 1);
            counterRotated = turnAtIndex - infolded;
            turnAtIndex += infolded;
            foldNext = true;
        }
        if (infolded > 0) {
            Rotation nextTurn = foldNext ? turn : Rotation::None;
            foldNext          = false;
            fence             = nextFence(*fence, pieces, random, junctions, nextTurn, templateId, destBox);
            if (--infolded == 0) foldNext = true;
        } else if (counterRotated > 0) {
            Rotation nextTurn = foldNext ? compose(Rotation::Clockwise180, turn) : (makeTurn ? turn : Rotation::None);
            foldNext          = false;
            fence             = nextFence(*fence, pieces, random, junctions, nextTurn, templateId, destBox);
            --counterRotated;
        } else {
            fence = nextFence(*fence, pieces, random, junctions, makeTurn ? turn : Rotation::None, templateId, destBox);
        }
    }
    return fence;
}

/// LichPerimeterFence#generateSidedPerimeter.
void generateSidedPerimeter(PerimeterFence& front, PieceList& pieces, RandomSource& random, std::string const& fullFenceId, BlockBox const& destination, JunctionGetter getter,
                            Rotation rotation) {
    PerimeterFence* fence = nextFence(front, pieces, random, (front.*getter)(), Rotation::None, fullFenceId, destination);
    if (!fence) return;
    fence = generateUntilNearDest(pieces, random, destination, 4, fence, rotation, getter, fullFenceId);
    if (!fence) return; // (Java would fail on the null fence here)
    auto junctions = (fence->*getter)();
    if (junctions.empty()) return;
    BlockPos post = add(fence->templatePos(), junctions.front().pos);
    BlockPos clamped{clampI(post.x, destination.minX, destination.maxX), clampI(post.y, destination.minY, destination.maxY), clampI(post.z, destination.minZ, destination.maxZ)};
    int      greatest = std::max({std::abs(clamped.x - post.x), std::abs(clamped.y - post.y), std::abs(clamped.z - post.z)});
    // fences of up to 7 blocks until the trim is within 2
    for (int distance = std::min(greatest + 1, 32); distance > 2;) {
        int stepSize = std::min(distance, 7);
        distance -= stepSize;
        fence = nextFence(*fence, pieces, random, (fence->*getter)(), rotation, gid(("outer_fence_" + std::to_string(stepSize)).c_str()), destination);
        if (!fence) return;
        rotation = Rotation::None;
    }
}

/// LichPerimeterFence#getClosestTrimOnGround: the grounded wing trim nearest `pos`.
std::optional<BlockBox> closestTrimOnGround(PieceList const& pieces, BlockPos const& pos) {
    std::optional<BlockBox> best;
    float                   minDist = std::numeric_limits<float>::max();
    for (auto const& p : pieces) {
        auto const* beard = dynamic_cast<WingBeard const*>(p.get());
        if (!beard || !beard->isTrim()) continue;
        BlockPos bc = bottomCenter(p->bbox);
        int      dx = pos.x - bc.x, dz = pos.z - bc.z;
        float    d  = std::sqrt(static_cast<float>(dx * dx + dz * dz));
        if (d < minDist) {
            minDist = d;
            best    = p->bbox;
        }
    }
    return best;
}

/// LichPerimeterFence#generateFence.
void generateFence(JigsawPiece& vestibule, PieceList& pieces, RandomSource& random, Orientation direction, BlockPos const& fenceCenter) {
    std::string full = gid("outer_fence_7");
    // startPerimeterFence: an outer_fence_7 whose source sits two blocks under the fence centre
    JigsawRecord oriented = verticalRecord(JigsawRecord{}, true, direction);
    auto         pc       = JigsawPlaceContext::pickPlaceableJunction(BlockPos{fenceCenter.x, fenceCenter.y - 2, fenceCenter.z}, BlockPos{0, 0, 0}, oriented, full,
                                                                     "gf:lich_tower/fence_source", random);
    if (!pc) return;
    auto  starter = std::make_unique<PerimeterFence>(*pc, full, random);
    auto* front   = starter.get();
    pieces.push_back(std::move(starter));
    grow(*front, vestibule, pieces, random);
    front->addTreeClearance(pieces);
    // the two sides run to the grounded wing trims beside the tower base
    TowerBase const* base = nullptr;
    for (auto const& p : pieces)
        if (auto* b = dynamic_cast<TowerBase const*>(p.get())) {
            base = b;
            break;
        }
    if (!base) return;
    BlockPos    baseBottom = bottomCenter(base->bbox);
    Orientation baseFront  = base->getSourceJigsaw().front;
    auto        leftDest   = closestTrimOnGround(pieces, relative(baseBottom, clockwise(baseFront), 64));
    auto        rightDest  = closestTrimOnGround(pieces, relative(baseBottom, counterClockwise(baseFront), 64));
    if (!leftDest || !rightDest) return;
    generateSidedPerimeter(*front, pieces, random, full, inflated(*leftDest, -1), &PerimeterFence::leftJunctions, Rotation::Clockwise90);
    generateSidedPerimeter(*front, pieces, random, full, inflated(*rightDest, -1), &PerimeterFence::rightJunctions, Rotation::Counterclockwise90);
}

void YardBox::addDecoration(JigsawPiece& parent, PieceList& pieces, RandomSource& random) {
    int axis = dir.axis();
    if (axis == 1 || scale != 0.0F) return;
    int baseY = bbox.minY;
    for (int i = 0; i < 5; ++i) {
        // Direction.fromAxisAndDirection(axis, nextBoolean() ? NEGATIVE : POSITIVE).getClockWise()
        bool        negative = random.nextBoolean();
        Orientation along    = axis == 0 ? (negative ? Orientation::West : Orientation::East) : (negative ? Orientation::North : Orientation::South);
        Orientation side     = clockwise(along);
        float       delta    = lerpF(random.nextFloat(), 0.05F, 0.95F);
        BlockPos    c        = center(bbox);
        BlockPos    onPath   = axis == 0 ? BlockPos{sx::lerpDiscrete(delta, bbox.minX, bbox.maxX), c.y, c.z} : BlockPos{c.x, c.y, sx::lerpDiscrete(delta, bbox.minZ, bbox.maxZ)};
        BlockPos    at       = relative(onPath, side, random.nextIntBetweenInclusive(2, 4));
        JigsawRecord oriented;
        oriented.front = side;
        oriented.top   = Orientation::North;
        auto graveId   = randomTemplate(random, tid("grave"));
        if (!graveId) continue;
        auto pc = JigsawPlaceContext::pickPlaceableJunction(BlockPos{at.x, baseY - 1, at.z}, BlockPos{0, 0, 0}, oriented, *graveId, "gf:lich_tower/grave", random);
        if (!pc) continue;
        auto grave = std::make_unique<YardGrave>(*pc, *graveId);
        if (collides(pieces, grave->bbox)) continue;
        auto* raw = grave.get();
        pieces.push_back(std::move(grave));
        grow(*raw, parent, pieces, random);
    }
}

/// LichYardBox#putSidePath.
YardBox* putSidePath(PieceList& pieces, BlockPos const& nearVestibule, Orientation dirFromVestibule, Orientation side, BlockPos const& pathEnd, int spread) {
    BlockPos fromVestibule = relative(nearVestibule, side, 24);
    BlockBox box           = withY(wrapped(1, pathEnd, relative(fromVestibule, opposite(dirFromVestibule), spread)), nearVestibule.y, nearVestibule.y + 10);
    auto     path          = std::make_unique<YardBox>(box, -1.0F, YardDir{false, dirFromVestibule}, false, 0.0F, 0.0F);
    auto*    raw           = path.get();
    pieces.push_back(std::move(path));
    return raw;
}

/// LichYardBox#generateYard: the path from the vestibule, the cross path, the two side paths, the
/// lights along the first path and the graves beside the paths.
void generateYard(Foyer& foyer, PieceList& pieces, BlockPos const& nearVestibule, BlockPos const& nearFence, RandomSource& random, Orientation dirFromVestibule) {
    int                   ySurface = foyer.bbox.minY + foyer.getGroundLevelDelta();
    std::vector<YardBox*> paths;
    int                   axis     = (dirFromVestibule == Orientation::East || dirFromVestibule == Orientation::West) ? 0 : 2;
    BlockBox              firstBox = withY(inflated(wrapped(3, nearVestibule, nearFence), 1), ySurface, ySurface + 10);
    auto                  first    = std::make_unique<YardBox>(firstBox, 2.5F, YardDir{false, dirFromVestibule}, false, 0.35F, -1.0F);
    paths.push_back(first.get());
    pieces.push_back(std::move(first));
    // the cross path, somewhere along the first
    float    delta     = lerpF(random.nextFloat(), 0.2F, 0.8F);
    BlockPos randomPos{sx::lerpDiscrete(delta, nearVestibule.x, nearFence.x), sx::lerpDiscrete(delta, nearVestibule.y, nearFence.y), sx::lerpDiscrete(delta, nearVestibule.z, nearFence.z)};
    constexpr int crossSpan = 24;
    BlockPos pathLeft  = relative(randomPos, clockwise(dirFromVestibule), crossSpan);
    BlockPos pathRight = relative(randomPos, counterClockwise(dirFromVestibule), crossSpan);
    BlockBox crossBox  = withY(wrapped(1, pathLeft, pathRight), ySurface, ySurface + 10);
    auto     cross     = std::make_unique<YardBox>(crossBox, -1.0F, YardDir{false, clockwise(dirFromVestibule)}, false, 0.0F, 0.0F);
    paths.push_back(cross.get());
    pieces.push_back(std::move(cross));
    BlockPos vestibuleGround{nearVestibule.x, ySurface, nearVestibule.z};
    paths.push_back(putSidePath(pieces, vestibuleGround, dirFromVestibule, clockwise(dirFromVestibule), pathLeft, crossSpan));
    paths.push_back(putSidePath(pieces, vestibuleGround, dirFromVestibule, counterClockwise(dirFromVestibule), pathRight, crossSpan));
    // the lights first, then the graves (they check for overlap with the paths)
    BlockBox lightBox = inflated(firstBox, axis == 2 ? 3 : 0, 0, axis == 0 ? 3 : 0);
    pieces.push_back(std::make_unique<YardLights>(lightBox, axis));
    for (auto* path : paths) path->addDecoration(foyer, pieces, random);
}

/// LichYardBox#beginYard.
void beginYard(Foyer& foyer, PieceList& pieces, RandomSource& random) {
    int  ySurface = foyer.bbox.minY + foyer.getGroundLevelDelta();
    auto paths    = foyer.matchSpareJigsaws([](JigsawRecord const& r) { return r.target == "gf:lich_tower/path"; });
    if (paths.empty()) return;
    JigsawRecord const& path       = paths.front();
    int                 pathLength = 24 + random.nextInt(8); // nextInt(24, 32)
    Orientation         direction  = rotate(Orientation::South, foyer.settings().rotation);
    BlockPos            generate   = add(foyer.templatePos(), path.pos);
    BlockPos            fenceCenter = relative(generate, direction, pathLength);
    generateFence(foyer, pieces, random, direction, BlockPos{fenceCenter.x, ySurface, fenceCenter.z});
    BlockPos nearVestibule = up(relative(generate, direction, 1), 4);
    BlockPos nearFence     = up(relative(fenceCenter, opposite(direction), 6), -4);
    generateYard(foyer, pieces, nearVestibule, nearFence, random, direction);
    // the whole yard: the foyer's centre 10 up and bottom 10 down and every fence post
    std::vector<BlockPos> spots{up(center(foyer.bbox), 10), up(bottomCenter(foyer.bbox), -10)};
    for (auto const& p : pieces)
        if (auto* f = dynamic_cast<PerimeterFence*>(p.get()))
            for (auto const& post : f->fencePostPositions()) spots.push_back(post);
    BlockBox full{spots[0].x, spots[0].y, spots[0].z, spots[0].x, spots[0].y, spots[0].z};
    for (auto const& s : spots) {
        full.minX = std::min(full.minX, s.x), full.minY = std::min(full.minY, s.y), full.minZ = std::min(full.minZ, s.z);
        full.maxX = std::max(full.maxX, s.x), full.maxY = std::max(full.maxY, s.y), full.maxZ = std::max(full.maxZ, s.z);
    }
    Orientation back    = opposite(foyer.getSourceJigsaw().top);
    BlockBox    yardBox = safeRetract(withY(inflated(full, 3), ySurface, ySurface + 10), YardDir{false, back}, 5);
    auto        dirt    = std::make_unique<YardBox>(yardBox, 8.0F, YardDir{true, Orientation::North}, true, 0.1F, 0.0F);
    auto*       raw     = dirt.get();
    pieces.push_back(std::move(dirt));
    raw->addDecoration(foyer, pieces, random);
}

// ---- start ---------------------------------------------------------------------------------------------

/// LichTowerStructure#getFirstPiece + #generateFromStartingPiece.
/// The pieces of the last layoutForTest (placeForTest places them).
PieceList& testPieces() {
    static PieceList p;
    return p;
}

bool layoutWith(gf::landmark::LayoutContext& ctx, RandomSource& contextRandom, sx::GenerationContext const& gctx) {
    auto&         start  = ctx.start;
    RandomSource& random = ctx.random;
    // direction = Rotation.getRandom(random).rotate(SOUTH); 24 blocks that way
    static Rotation const kRotations[] = {Rotation::None, Rotation::Clockwise90, Rotation::Clockwise180, Rotation::Counterclockwise90};
    Orientation           direction    = rotate(Orientation::South, kRotations[random.nextInt(4)]);
    BlockPos              placePos     = relative(BlockPos{start.center.x, start.center.y, start.center.z}, direction, 24);
    JigsawRecord          oriented     = verticalRecord(JigsawRecord{}, true, direction);
    oriented.pos                       = BlockPos{0, 0, 0};
    auto pc = JigsawPlaceContext::pickPlaceableJunction(placePos, BlockPos{0, 0, 0}, oriented, gid("tower_foyer"), "gf:lich_tower/vestibule", random);
    if (!pc) {
        log().warn("lich: the lich tower foyer template is missing (gloamingforest:lich_tower/tower_foyer)");
        return false;
    }
    auto  foyer = std::make_unique<Foyer>(*pc, true, random.nextBoolean());
    auto* raw   = foyer.get();
    PieceList pieces;
    pieces.push_back(std::move(foyer));
    gContext = &gctx;
    sx::addJigsaws(*raw, *raw, pieces, gctx);
    // LichTowerStructure#generateFromStartingPiece: the yard once the tower stands
    beginYard(*raw, pieces, contextRandom);
    gContext = nullptr;
    // spawn labels and the terraformer's trim boxes for the start
    Json spawns = Json::array(), trims = Json::array();
    int  yBase  = pieces.empty() ? start.center.y : pieces.front()->bbox.minY;
    for (auto const& p : pieces) {
        auto const& b = p->bbox;
        if (auto* l = asLich(p.get())) {
            int label = l->spawnLabel();
            if (label != -1) spawns.push_back({b.minX, b.minY, b.minZ, b.maxX, b.maxY, b.maxZ, label});
            if (dynamic_cast<Foyer*>(l) || l->isTrim()) trims.push_back({b.minX, b.minY, b.minZ, b.maxX, b.maxY, b.maxZ});
        } else if (auto* yard = dynamic_cast<YardBox*>(p.get())) {
            spawns.push_back({b.minX, b.minY, b.minZ, b.maxX, b.maxY, b.maxZ, kYardSpawns});
            // a yard box that adjusts the terrain (the dirt) counts 5 blocks lower
            if (yard->terrainAdjusted()) trims.push_back({b.minX, b.minY - 5, b.minZ, b.maxX, b.maxY - 5, b.maxZ});
        }
    }
    start.data["lich_spawns"] = spawns;
    start.data["lich_trims"]  = trims;
    start.data["lich_ybase"]  = yBase;
    for (auto& p : pieces) ctx.pieces.push_back(std::move(p));
    return true;
}

bool layout(gf::landmark::LayoutContext& ctx) {
    auto ctxRandom = gf::landmark::ext::contextRandom(ctx.start);
    int  dim       = ctx.start.dimension;
    // ChunkGenerator#getFirstOccupiedHeight: the terrain provider's noise height. Where none answers
    // (no provider for the dimension: /gloamingforest place and generateAt outside the Gloaming Forest, or
    // before U2's terrain registers one) the loaded ground stands in when the layout runs on the
    // server thread (a generation thread must not touch the level), else the start's own height.
    int  fallbackY = ctx.start.center.y - 1;
    auto* loaded   = onServerThread() ? game::regionOf(dim) : nullptr;
    sx::GenerationContext gctx{ctxRandom.get(), gf::worldgen::seed(), [dim, fallbackY, loaded](gf::worldgen::Heightmap type, int x, int z) {
                                   if (auto h = gf::landmark::ext::firstOccupiedHeight(dim, x, z, type)) return *h;
                                   if (loaded && loaded->hasChunksAt(BlockPos{x, loaded->getMinHeight(), z}, 0, false)) {
                                       return columnHeight([&](BlockPos const& p) -> Block const& { return loaded->getBlock(p); }, loaded->getMinHeight(),
                                                           loaded->getMaxHeight(), type, x, z) - 1;
                                   }
                                   return fallbackY;
                               }};
    return layoutWith(ctx, *ctxRandom, gctx);
}

/// EntityEvents#getSpawnListIndexAt over the pieces' labels (Deny: none).
int spawnIndex(gf::landmark::Start const& start, BlockPos const& pos) {
    auto it = start.data.find("lich_spawns");
    if (it == start.data.end() || !it->is_array()) return -1;
    int best = -1;
    for (auto const& e : *it) {
        if (!e.is_array() || e.size() != 7) continue;
        if (pos.x < e[0] || pos.y < e[1] || pos.z < e[2] || pos.x > e[3] || pos.y > e[4] || pos.z > e[5]) continue;
        int label = e[6].get<int>();
        // SpawnIndexProvider.Deny answers -1 to the highest-index-wins rule: an overlapping labelled
        // piece (the boss boundary segment) still gives its list
        best = std::max(best, label == kDenySpawns ? -1 : label);
    }
    return best;
}

/// LichTowerStructure#getStructureTerraformer.
double terraform(gf::landmark::Start const& start, int x, int y, int z) {
    auto it = start.data.find("lich_trims");
    if (it == start.data.end() || !it->is_array() || it->empty()) return 0.0;
    int    yBase = start.data.value("lich_ybase", start.center.y);
    // DensityFunctions.yClampedGradient(yBase - 2, yBase - 1, 1, 0)
    double act = y <= yBase - 2 ? 1.0 : y >= yBase - 1 ? 0.0 : 0.0;
    if (act == 0.0) return 0.0;
    std::vector<BlockBox> boxes;
    for (auto const& b : *it) boxes.push_back(BlockBox{b[0], b[1], b[2], b[3], b[4], b[5]});
    return act * gf::landmark::ext::boxDensity(boxes, -5, -5, "bury", x, y, z);
}

} // namespace

std::vector<PieceBox> layoutForTest(BlockPos const& origin, std::int64_t seed) {
    gf::landmark::Start start;
    start.key    = kTowerKey;
    start.center = core::GlobalPos{0, origin.x, origin.y, origin.z};
    gf::core::jrand::LegacyRandom startRandom(seed);
    gf::core::jrand::WorldgenRandom contextRandom(std::make_unique<gf::core::jrand::LegacyRandom>(0));
    contextRandom.setLargeFeatureSeed(seed, origin.x >> 4, origin.z >> 4);
    // flat terrain at the origin's height (the first occupied block one below it)
    int y = origin.y - 1;
    sx::GenerationContext gctx{&contextRandom, seed, [y](gf::worldgen::Heightmap, int, int) { return y; }};
    gf::landmark::LayoutContext ctx{start, startRandom, {}};
    std::vector<PieceBox> out;
    if (!layoutWith(ctx, contextRandom, gctx)) return out;
    for (auto const& p : ctx.pieces) out.push_back(PieceBox{p->typeId, p->bbox, p.get()});
    testPieces() = std::move(ctx.pieces);
    return out;
}

void placeForTest(std::function<std::unique_ptr<Region>(BlockPos const&)> const& regionAt, BlockBox const& clip, std::int64_t seed) {
    pending::Suppress quiet;
    gf::core::jrand::LegacyRandom random(seed);
    // in F6's order: LandmarkStructure sorts the pieces by SortablePiece key (the yard's dirt and
    // paths first, before the tower stands over them; higher fences later)
    std::vector<gs::Piece*> order;
    for (auto& p : testPieces()) order.push_back(p.get());
    std::stable_sort(order.begin(), order.end(), [](gs::Piece const* a, gs::Piece const* b) {
        auto key = [](gs::Piece const* p) {
            auto const* s = dynamic_cast<sb::SortablePiece const*>(p);
            return s ? s->getSortKey() : 0;
        };
        return key(a) < key(b);
    });
    for (auto* p : order) {
        if (p->bbox.minY > clip.maxY || p->bbox.maxY < clip.minY) continue; // nothing of it inside the area
        // one call per chunk column the piece meets, each clipped to `clip` (Java's postProcess per chunk)
        for (int cx = (std::max(p->bbox.minX, clip.minX)) >> 4; cx <= (std::min(p->bbox.maxX, clip.maxX)) >> 4; ++cx)
            for (int cz = (std::max(p->bbox.minZ, clip.minZ)) >> 4; cz <= (std::min(p->bbox.maxZ, clip.maxZ)) >> 4; ++cz) {
                BlockBox chunk{std::max(cx << 4, clip.minX), clip.minY, std::max(cz << 4, clip.minZ), std::min((cx << 4) + 15, clip.maxX), clip.maxY,
                               std::min((cz << 4) + 15, clip.maxZ)};
                if (chunk.minX > chunk.maxX || chunk.minZ > chunk.maxZ) continue;
                random.setSeed(seed + cx * 341873128712LL + cz * 132897987541LL);
                auto region = regionAt(BlockPos{(cx << 4) + 8, chunk.minY, (cz << 4) + 8});
                if (region) p->postProcess(*region, random, chunk, BlockPos{chunk.minX, chunk.minY, chunk.minZ});
            }
    }
}

void forgetTestPieces() { testPieces().clear(); }

std::vector<PieceBox> perimeterForTest(int lateral, std::int64_t seed) {
    std::vector<PieceBox> out;
    PerimeterFence*       front = nullptr;
    TowerBase const*      base  = nullptr;
    for (auto& p : testPieces()) {
        if (!front) front = dynamic_cast<PerimeterFence*>(p.get());
        if (!base) base = dynamic_cast<TowerBase const*>(p.get());
    }
    if (!front || !base) return out;
    // two grounded trims `lateral` blocks to the sides of the tower base (LichPerimeterFence
    // #generateFence's destinations, getClosestTrimOnGround, then inflatedBy(-1))
    BlockPos    bottom = bottomCenter(base->bbox);
    Orientation facing = base->getSourceJigsaw().front;
    auto        trimAt = [&](Orientation side) {
        BlockPos c = relative(bottom, side, lateral);
        return inflated(BlockBox{c.x - 4, bottom.y, c.z - 4, c.x + 4, bottom.y + 8, c.z + 4}, -1);
    };
    gf::core::jrand::LegacyRandom random(seed);
    auto&                         pieces = testPieces();
    std::size_t                   before = pieces.size();
    std::string                   full   = gid("outer_fence_7");
    generateSidedPerimeter(*front, pieces, random, full, trimAt(clockwise(facing)), &PerimeterFence::leftJunctions, Rotation::Clockwise90);
    generateSidedPerimeter(*front, pieces, random, full, trimAt(counterClockwise(facing)), &PerimeterFence::rightJunctions, Rotation::Counterclockwise90);
    for (std::size_t i = before; i < pieces.size(); ++i) {
        auto const& p = pieces[i];
        auto const* f = dynamic_cast<PerimeterFence const*>(p.get());
        out.push_back(PieceBox{f ? p->typeId + "|" + f->templateId() : p->typeId, p->bbox, p.get()});
    }
    return out;
}

void registerTower() {
    gf::landmark::registerGenerator(kTowerKey, {.layout = layout, .terraform = terraform, .spawnIndex = spawnIndex});
    // LichTowerBase.TrimProcessor: keeps what is in #minecraft:stone_bricks under the base's stairs
    gs::registerProcessor("gf:lich_tower_trim", [](Region& region, gs::BlockInfo const&, gs::BlockInfo const& placed, gs::TemplateSettings const&, Json const&) -> std::optional<gs::BlockInfo> {
        static std::set<std::string> const kStoneBricks{"minecraft:stone_bricks", "minecraft:mossy_stone_bricks", "minecraft:cracked_stone_bricks", "minecraft:chiseled_stone_bricks"};
        if (placed.block && game::blockId(*placed.block) == "minecraft:polished_andesite_stairs" && kStoneBricks.contains(game::blockId(region.getBlock(placed.pos))))
            return std::nullopt;
        return placed;
    });
    // piece factories (saved starts): the constructors' own arguments come back from load()
    gs::registerPieceType(kFoyer, [](Json const& s) -> std::unique_ptr<gs::Piece> {
        auto p = std::make_unique<Foyer>(JigsawPlaceContext{}, false, false);
        p->load(s);
        return p;
    });
    gs::registerPieceType(kFoyerDecor, [](Json const& s) -> std::unique_ptr<gs::Piece> {
        auto p = std::make_unique<FoyerDecor>(0, JigsawPlaceContext{});
        p->load(s);
        return p;
    });
    gs::registerPieceType(kBase, [](Json const& s) -> std::unique_ptr<gs::Piece> {
        auto p = std::make_unique<TowerBase>(JigsawPlaceContext{});
        p->load(s);
        return p;
    });
    gs::registerPieceType(kBaseTrim, [](Json const& s) -> std::unique_ptr<gs::Piece> {
        auto p = std::make_unique<BaseTrim>(JigsawPlaceContext{});
        p->load(s);
        return p;
    });
    gs::registerPieceType(kSegment, [](Json const& s) -> std::unique_ptr<gs::Piece> {
        auto p = std::make_unique<Segment>(0, JigsawPlaceContext{}, false, false, false, s.value("Template", std::string()));
        p->load(s);
        return p;
    });
    gs::registerPieceType(kBossRoom, [](Json const& s) -> std::unique_ptr<gs::Piece> {
        auto p = std::make_unique<BossRoom>(JigsawPlaceContext{});
        p->load(s);
        return p;
    });
    gs::registerPieceType(kBossRoof, [](Json const& s) -> std::unique_ptr<gs::Piece> {
        auto p = std::make_unique<BossRoof>(JigsawPlaceContext{});
        p->load(s);
        return p;
    });
    gs::registerPieceType(kMobBridge, [](Json const& s) -> std::unique_ptr<gs::Piece> {
        auto p = std::make_unique<SpawnerBridge>(0, s.value("Template", std::string()), JigsawPlaceContext{}, false);
        p->load(s);
        return p;
    });
    gs::registerPieceType(kWingBridge, [](Json const& s) -> std::unique_ptr<gs::Piece> {
        auto p = std::make_unique<WingBridge>(0, JigsawPlaceContext{}, s.value("Template", std::string()), false);
        p->load(s);
        return p;
    });
    gs::registerPieceType(kWingRoof, [](Json const& s) -> std::unique_ptr<gs::Piece> {
        auto p = std::make_unique<WingRoof>(0, s.value("Template", std::string()), JigsawPlaceContext{});
        p->load(s);
        return p;
    });
    gs::registerPieceType(kWingBeard, [](Json const& s) -> std::unique_ptr<gs::Piece> {
        auto p = std::make_unique<WingBeard>(0, s.value("Template", std::string()), JigsawPlaceContext{}, false);
        p->load(s);
        return p;
    });
    gs::registerPieceType(kWingRoom, [](Json const& s) -> std::unique_ptr<gs::Piece> {
        static gf::core::jrand::LegacyRandom dummy(0);
        auto p = std::make_unique<WingRoom>(0, JigsawPlaceContext{}, s.value("Template", std::string()), 0, false, false, dummy);
        p->load(s);
        return p;
    });
    gs::registerPieceType(kRoomDecor, [](Json const& s) -> std::unique_ptr<gs::Piece> {
        auto p = std::make_unique<RoomDecor>(0, s.value("Template", std::string()), JigsawPlaceContext{});
        p->load(s);
        return p;
    });
    gs::registerPieceType(kYardPath, [](Json const& s) -> std::unique_ptr<gs::Piece> {
        auto p = std::make_unique<YardBox>(BlockBox{}, 0.0F, YardDir{}, false, 0.0F, 0.0F);
        p->load(s);
        return p;
    });
    gs::registerPieceType(kYardLights, [](Json const& s) -> std::unique_ptr<gs::Piece> {
        auto p = std::make_unique<YardLights>(BlockBox{}, 1);
        p->load(s);
        return p;
    });
    gs::registerPieceType(kYardGrave, [](Json const& s) -> std::unique_ptr<gs::Piece> {
        auto p = std::make_unique<YardGrave>(JigsawPlaceContext{}, s.value("Template", std::string()));
        p->load(s);
        return p;
    });
    gs::registerPieceType(kYardFence, [](Json const& s) -> std::unique_ptr<gs::Piece> {
        gf::core::jrand::LegacyRandom unused(0);
        auto p = std::make_unique<PerimeterFence>(JigsawPlaceContext{}, s.value("Template", std::string()), unused);
        p->load(s);
        return p;
    });
    gs::registerPieceType(kGallery, [](Json const& s) -> std::unique_ptr<gs::Piece> {
        auto p = std::make_unique<Gallery>(0, s.value("Template", std::string()), JigsawPlaceContext{});
        p->load(s);
        return p;
    });
}

} // namespace gf::feature::lich::tower
