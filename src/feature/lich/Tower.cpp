// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/type/LichTowerStructure.java
//   src/main/java/twilightforest/world/components/structures/lichtowerrevamp/LichTowerPieces.java
//   src/main/java/twilightforest/world/components/structures/lichtowerrevamp/LichTowerUtil.java
//   src/main/java/twilightforest/world/components/structures/lichtowerrevamp/LichTowerFoyer.java
//   src/main/java/twilightforest/world/components/structures/lichtowerrevamp/LichTowerFoyerDecor.java
//   src/main/java/twilightforest/world/components/structures/lichtowerrevamp/LichTowerBase.java
//   src/main/java/twilightforest/world/components/structures/lichtowerrevamp/LichTowerBaseTrim.java
//   src/main/java/twilightforest/world/components/structures/lichtowerrevamp/LichTowerSegment.java
//   src/main/java/twilightforest/world/components/structures/lichtowerrevamp/LichBossRoom.java
//   src/main/java/twilightforest/world/components/structures/lichtowerrevamp/LichBossRoof.java
//   src/main/java/twilightforest/world/components/structures/lichtowerrevamp/LichTowerSpawnerBridge.java
//   src/main/java/twilightforest/world/components/structures/lichtowerrevamp/LichTowerWingBridge.java
//   src/main/java/twilightforest/world/components/structures/lichtowerrevamp/LichTowerWingRoom.java
//   src/main/java/twilightforest/world/components/structures/lichtowerrevamp/LichTowerWingRoof.java
//   src/main/java/twilightforest/world/components/structures/lichtowerrevamp/LichTowerWingBeard.java
//   src/main/java/twilightforest/world/components/structures/lichtowerrevamp/LichTowerRoomDecor.java
//   src/main/java/twilightforest/world/components/structures/lichtowerrevamp/LichTowerMagicGallery.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The lich tower revamp: a foyer, the central tower stacked from slices up to the boss room, wings of
// bridges and rooms with ladders, roofs and beards, mob bridges inside the tower and the magic
// gallery. The pieces follow TF's code; the templates are original GloamingForest designs (rule
// R11/R19) at TF's ids with TF's junction names (tools/structures/lich). Room interiors, roofs,
// beards and decor are U22's templates; while one is missing, a room falls back to a GloamingForest
// placeholder room of its size (twilightforest:lich_tower/placeholder/<size>), so the tower still
// assembles.

#include "feature/lich/Tower.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"

#include "core/lich/TowerMath.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/structure/Paintings.h"
#include "feature/ext/structure/Placement.h"
#include "feature/ext/structure/Processors.h"
#include "feature/ext/structure/TemplateDefinitions.h"
#include "feature/landmark/Landmark.h"
#include "feature/lich/Common.h"
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

constexpr char const* kNs = "twilightforest:lich_tower/";
std::string tid(char const* path) { return std::string(kNs) + path; }
/// The templates TF's code names directly have GloamingForest ids (no TF template definition knows them).
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

// piece type ids (TFStructurePieceTypes "TFLT" + suffix)
constexpr char const* kFoyer      = "twilightforest:TFLTTFoy";
constexpr char const* kFoyerDecor = "twilightforest:TFLTTFoyD";
constexpr char const* kBase       = "twilightforest:TFLTCTBase";
constexpr char const* kBaseTrim   = "twilightforest:TFLTCTTrim";
constexpr char const* kSegment    = "twilightforest:TFLTCTSeg";
constexpr char const* kMobBridge  = "twilightforest:TFLTMobBridge";
constexpr char const* kWingBridge = "twilightforest:TFLTBridge";
constexpr char const* kWingRoof   = "twilightforest:TFLTTRoof";
constexpr char const* kWingBeard  = "twilightforest:TFLTTBeard";
constexpr char const* kWingRoom   = "twilightforest:TFLTTRoom";
constexpr char const* kRoomDecor  = "twilightforest:TFLTTDecor";
constexpr char const* kGallery    = "twilightforest:TFLTTGallery";
constexpr char const* kBossRoom   = "twilightforest:TFLTTBoss";
constexpr char const* kBossRoof   = "twilightforest:TFLTTBossRoof";

// ---- the generation context of the running layout ------------------------------------------------------

thread_local sx::GenerationContext const* gContext = nullptr;

void grow(JigsawPiece& piece, JigsawPiece& parent, PieceList& pieces, RandomSource& random) {
    if (gContext) sx::addJigsaws(piece, parent, pieces, *gContext);
    else piece.addJigsaws(parent, pieces, random);
}

// ---- LichTowerUtil ------------------------------------------------------------------------------------

std::string json(Json const& j) { return j.dump(); }

std::string updateMarker() {
    return json({{"processor_type", "twilightforest:update_marking"},
                 {"mark_updates", Json::array({"minecraft:birch_fence", "minecraft:polished_andesite_stairs", "minecraft:stone_brick_wall", "minecraft:mossy_stone_brick_wall",
                                               "minecraft:cobblestone_wall", "minecraft:mossy_cobblestone_wall", "twilightforest:wrought_iron_fence", "twilightforest:canopy_fence",
                                               "twilightforest:twisted_stone_pillar"})}});
}

/// LichTowerUtil#addDefaultProcessors.
void addDefaultProcessors(std::vector<std::string>& p) {
    p.push_back("minecraft:jigsaw_replacement");
    p.push_back(json({{"processor_type", "minecraft:block_ignore"}, {"blocks", Json::array({Json{{"Name", "minecraft:structure_block"}}})}}));
    p.push_back("twilightforest:stone_brick_variants");
    p.push_back("twilightforest:cobble_variants");
    p.push_back("twilightforest:infest_blocks");
    p.push_back(updateMarker());
}

std::string const& roomSpawners() {
    static std::string const p = sx::compileSpawnerProcessor(2, 0.8F, {{"minecraft:spider", 1}, {"minecraft:cave_spider", 1}, {"twilightforest:swarm_spider", 1},
                                                                        {"twilightforest:hedge_spider", 1}, {"minecraft:skeleton", 4}, {"minecraft:zombie", 4}});
    return p;
}

std::string const& centralSpawners() {
    static std::string const p = sx::compileSpawnerProcessor(4, 0.0F, {{"minecraft:skeleton", 2}, {"minecraft:zombie", 1}, {"twilightforest:swarm_spider", 1}});
    return p;
}

std::string stairDecay(float chance) {
    return json({{"processor_type", "twilightforest:vertical_decay"},
                 {"decay_blocks", Json::array({"twilightforest:twilight_oak_slab", "twilightforest:canopy_slab", "twilightforest:twilight_oak_banister", "twilightforest:canopy_banister"})},
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
    return gf::tags::has(gf::tags::Registry::Block, "twilightforest:banisters", game::blockId(region.getBlock(pos)));
}

void removeIfBanister(Region& region, BlockPos const& pos, BlockBox const& chunk) {
    if (!chunk.contains(pos.x, pos.y, pos.z) || !isBanister(region, pos)) return;
    if (auto const* air = game::blockWith("minecraft:air")) region.setBlock(pos, *air, 2);
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

/// The template position of any jigsaw parent (TwilightJigsawPiece#templatePosition()).
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
        auto at       = label.find('@');
        auto name     = label.substr(0, at);
        if (!putChest || name != (chestSide ? "chest_a" : "chest_b")) return;
        // the chest faces the marker's direction (default south: CLOCKWISE_180 of north)
        placeChest(region, chunk, random, pos.x, pos.y, pos.z, "minecraft:chest", "twilightforest:chests/tower_room");
        placeBlock(region, "twilightforest:canopy_planks", Json::object(), pos.x, pos.y - 1, pos.z, chunk);
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
            placeChest(region, chunk, random, at.x, at.y, at.z, "minecraft:chest", "twilightforest:chests/tower_foyer");
            placeBlock(region, "minecraft:stone_brick_stairs", Json{{"half", "top"}, {"facing", dirName(facing)}}, at.x, at.y + 1, at.z, chunk);
        } else if (pick == 3) {
            placeBlock(region, "twilightforest:candelabra",
                       Json{{"facing", dirName(rotate(Orientation::North, placeSettings.rotation))}, {"lighting", "dim"}, {"has_candle_1", true}, {"has_candle_2", true}, {"has_candle_3", true}},
                       at.x, at.y, at.z, chunk);
        } else {
            switch (random.nextInt(5)) {
            case 3:
                placeBlock(region, "twilightforest:skeleton_skull_candle",
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
        if (label == "sapling" || label == "tree") {
            // TFStructureHelper#randomPlant (the tree features are F5 / U3 features: a plant here)
            static char const* const kPlants[] = {"minecraft:oak_sapling", "minecraft:spruce_sapling", "minecraft:birch_sapling", "minecraft:jungle_sapling",
                                                  "minecraft:acacia_sapling", "minecraft:dark_oak_sapling", "minecraft:brown_mushroom", "minecraft:red_mushroom",
                                                  "minecraft:dead_bush"};
            placeBlock(region, kPlants[random.nextInt(9)], Json::object(), pos.x, pos.y, pos.z, chunk);
        }
    }
};

class TowerBase final : public LichPiece {
public:
    explicit TowerBase(JigsawPlaceContext const& ctx) : LichPiece(kBase, 1, gid("tower_base"), ctx) {
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
    }
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
        sx::placePainting(label, pos, region, random, chunk, placeSettings.rotation, 2, 10, "twilightforest:tower_paintings");
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
        sx::placePainting(label, pos, region, random, chunk, placeSettings.rotation, 3, 3, "twilightforest:tower_boss_paintings");
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
            placeSettings.processors.push_back(json({{"processor_type", "twilightforest:wood_multiswizzle"},
                                                     {"palettes", Json::array({Json::array({"twilightforest:twilight_oak", "twilightforest:canopy"}),
                                                                               Json::array({"twilightforest:canopy", "twilightforest:twilight_oak"})})}}));
    }
    bool inverted = false;
};

// ---- wings ---------------------------------------------------------------------------------------------

class WingRoom;

class WingRoof final : public LichPiece {
public:
    WingRoof(int depth, std::string const& id, JigsawPlaceContext const& ctx) : LichPiece(kWingRoof, depth, id, ctx) {
        placeSettings.processors.push_back("twilightforest:soft_replace");
    }
    [[nodiscard]] BlockBox generationCollisionBox() const {
        if (span(bbox, 0) < 2 || span(bbox, 1) < 2 || span(bbox, 2) < 2) return BlockBox{bbox.minX, bbox.minY, bbox.minZ, bbox.maxX, std::max(bbox.minY, bbox.maxY - 1), bbox.maxZ};
        return adjusted(bbox, 1, 1, 1, -1, 0, -1);
    }
};

class WingBeard final : public LichPiece {
public:
    WingBeard(int depth, std::string const& id, JigsawPlaceContext const& ctx, bool ground) : LichPiece(kWingBeard, depth, id, ctx), generateGround(ground) {
        placeSettings.processors.push_back("twilightforest:soft_replace");
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
        // Util.shuffle, then drop the ones sharing an x or z (the chance filter of TF keeps the
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
    static int parseRange(std::string const& s, RandomSource& random, int lo, int hi) {
        if (s.empty()) return random.nextIntBetweenInclusive(lo, hi);
        auto dash = s.find('-');
        try {
            if (dash == std::string::npos) return std::stoi(s);
            return random.nextIntBetweenInclusive(std::stoi(s.substr(0, dash)), std::stoi(s.substr(dash + 1)));
        } catch (...) {
            return random.nextIntBetweenInclusive(lo, hi);
        }
    }
    void putCandles(Region& region, BlockBox const& chunk, RandomSource& random, std::vector<std::string> const& params, BlockPos const& pos, std::string const& id) {
        int count = params.size() >= 2 ? std::clamp(parseRange(params[1], random, 1, 3), 1, 4) : random.nextIntBetweenInclusive(1, 3);
        placeBlock(region, id, Json{{"lit", true}, {"candles", count}}, pos.x, pos.y, pos.z, chunk);
    }

    int                roomSize       = 0;
    bool               generateGround = false;
    int                ladderIndex    = -1;
    std::string        ladderTarget;
    int                roofFallback   = -1;
    std::vector<int>   ceiling;
};

void WingRoom::handleDataMarker(std::string const& label, BlockPos const& pos0, Region& region, RandomSource& random, BlockBox const& chunk, Rotation) {
    BlockPos pos     = pos0;
    auto     gt      = label.find('>');
    std::string variety = gt == std::string::npos ? label : label.substr(gt + 1);
    if (gt != std::string::npos) {
        std::string mod = label.substr(0, gt);
        bool        rope = mod.rfind("rope", 0) == 0, chain = mod.rfind("chain", 0) == 0;
        if (rope || chain) {
            if (!canHang(pos)) return;
            std::string params = mod.substr(rope ? 4 : 5);
            auto        pct    = params.rfind('%');
            int         len    = parseRange(pct == std::string::npos ? std::string() : params.substr(pct + 1), random, 1, 2);
            for (int i = 0; i < len; ++i) placeBlock(region, rope ? "twilightforest:rope" : "minecraft:chain", Json::object(), pos.x, pos.y - i, pos.z, chunk);
            pos.y -= len;
        } else if (mod == "pedestal") {
            placeBlock(region, "twilightforest:twisted_stone_pillar", Json::object(), pos.x, pos.y, pos.z, chunk);
            pos.y += 1;
        } else if (mod == "below") {
            pos.y -= 1;
        }
    }
    auto at = variety.find('@');
    std::string body = variety.substr(0, at);
    std::vector<std::string> options;
    for (std::size_t s = 0;;) {
        auto bar = body.find('|', s);
        options.push_back(body.substr(s, bar == std::string::npos ? std::string::npos : bar - s));
        if (bar == std::string::npos) break;
        s = bar + 1;
    }
    std::string chosen = options[random.nextInt(static_cast<int>(options.size()))];
    std::vector<std::string> params;
    for (std::size_t s = 0;;) {
        auto colon = chosen.find(':', s);
        params.push_back(chosen.substr(s, colon == std::string::npos ? std::string::npos : colon - s));
        if (colon == std::string::npos) break;
        s = colon + 1;
    }
    if (params.empty()) return;
    placeBlock(region, "minecraft:air", Json::object(), pos.x, pos.y, pos.z, chunk);
    auto const& k = params[0];
    auto put      = [&](char const* id, Json const& states = Json::object()) { placeBlock(region, id, states, pos.x, pos.y, pos.z, chunk); };
    if (k == "air" || k == "empty") return;
    if (k == "bookshelf") return put("minecraft:bookshelf");
    if (k == "canopy_shelf" || k == "canopy_bookshelf") return put("twilightforest:canopy_bookshelf");
    if (k == "stone_brick_slab") return put("minecraft:stone_brick_slab");
    if (k == "lava") return put("minecraft:lava");
    if (k == "water") return put("minecraft:water");
    if (k == "firefly_jar") return put("twilightforest:firefly_jar");
    if (k == "terrorcotta_arcs") return put("twilightforest:terrorcotta_arcs");
    if (k == "mason_jar") return put("twilightforest:mason_jar");
    if (k == "canopy_slab") return put("twilightforest:canopy_slab");
    if (k == "canopy_stairs") return put("twilightforest:canopy_stairs");
    if (k == "creeper_head" || k == "skeleton_skull" || k == "wither_skull" || k == "zombie_head") {
        static std::map<std::string, char const*> const heads{{"creeper_head", "minecraft:creeper_head"}, {"skeleton_skull", "minecraft:skeleton_skull"},
                                                              {"wither_skull", "minecraft:wither_skeleton_skull"}, {"zombie_head", "minecraft:zombie_head"}};
        return put(heads.at(k), Json{{"rotation", params.size() >= 2 ? parseRange(params[1], random, 0, 15) & 15 : random.nextInt(16)}});
    }
    if (k == "creeper_candle" || k == "skeleton_candle" || k == "wither_candle" || k == "zombie_candle") {
        std::string id = k == "wither_candle" ? "twilightforest:wither_skele_skull_candle" : "twilightforest:" + k.substr(0, k.find('_')) + "_skull_candle";
        int         count = params.size() >= 2 ? std::clamp(parseRange(params[1], random, 1, 3), 1, 4) : random.nextIntBetweenInclusive(1, 3);
        return put(id.c_str(), Json{{"lighting", "normal"}, {"candles", count}});
    }
    if (k == "spawner") {
        static char const* const kMobs[] = {"minecraft:spider", "minecraft:cave_spider", "twilightforest:hedge_spider", "twilightforest:swarm_spider"};
        std::string mob;
        if (params.size() >= 2 && !params[1].empty()) {
            auto list = params[1];
            auto first = list.substr(0, list.find(','));
            mob       = first.find(':') == std::string::npos ? (first == "hedge_spider" || first == "swarm_spider" ? "twilightforest:" + first : "minecraft:" + first) : first;
        } else {
            int r = random.nextInt(10);
            mob   = r < 4 ? kMobs[r] : r < 7 ? "minecraft:skeleton" : "minecraft:zombie";
        }
        placeSpawner(region, chunk, pos.x, pos.y, pos.z, mob);
        return;
    }
    if (k == "sinister_spawner") return put("twilightforest:sinister_spawner");
    if (k == "brewing_stand") return put("minecraft:brewing_stand");
    if (k == "lectern" || k == "empty_lectern" || k == "candled_lectern") {
        put("minecraft:lectern");
        if (k == "candled_lectern") {
            if (random.nextInt(4) != 0) placeBlock(region, "minecraft:candle", Json{{"lit", true}, {"candles", random.nextIntBetweenInclusive(1, 3)}}, pos.x, pos.y + 1, pos.z, chunk);
            else placeBlock(region, "twilightforest:skeleton_skull_candle", Json{{"lighting", "normal"}, {"candles", random.nextIntBetweenInclusive(1, 3)}}, pos.x, pos.y + 1, pos.z, chunk);
        }
        // "lectern": TF puts a writable book on half of them and a Death Tome mimic on the other
        // half; the book (lectern block entity) and the mimic (an entity spawned at generation) are
        // not placed yet (docs/units/lich.md, "Not implemented")
        if (k == "lectern") (void)random.nextBoolean();
        return;
    }
    if (k == "chiseled_canopy_shelf") return put("twilightforest:chiseled_canopy_bookshelf");
    if (k == "chest" || k == "trapped_chest") {
        std::string table = "twilightforest:chests/tower_room";
        if (params.size() >= 2) {
            static std::map<std::string, std::string> const kTables{{"room", "twilightforest:chests/tower_room"}, {"library", "twilightforest:chests/tower_library"},
                                                                    {"potion", "twilightforest:chests/tower_potion"}, {"enchanting", "twilightforest:chests/tower_enchanting"},
                                                                    {"jar", "twilightforest:chests/tower_jars"}};
            if (auto it = kTables.find(params[1]); it != kTables.end()) table = it->second;
            else {
                table = params[1];
                std::replace(table.begin(), table.end(), '.', '/');
                if (table.find(':') == std::string::npos && params.size() >= 3) table = params[1] + ":" + params[2];
            }
        }
        placeChest(region, chunk, random, pos.x, pos.y, pos.z, k == "chest" ? "minecraft:chest" : "minecraft:trapped_chest", table);
        return;
    }
    if (k == "candle" || k == "candles") return putCandles(region, chunk, random, params, pos, "minecraft:candle");
    if (k.size() > 7 && k.substr(k.size() - 7) == "_candle") return putCandles(region, chunk, random, params, pos, "minecraft:" + k);
    if (k == "water_cauldron") {
        int level = params.size() >= 2 ? std::clamp(parseRange(params[1], random, 1, 3), 0, 3) : random.nextIntBetweenInclusive(1, 3);
        if (level == 0) return put("minecraft:cauldron");
        return put("minecraft:water_cauldron", Json{{"level", level}});
    }
    if (k == "zombie_trap" || k == "wrought_iron_post") return put("twilightforest:wrought_iron_fence");
    // any other label: the block with that id ("." or ":" as the separator)
    std::string id = params.size() >= 2 && k.find('.') == std::string::npos ? k + ":" + params[1] : k;
    std::replace(id.begin(), id.end(), '.', ':');
    if (id.find(':') == std::string::npos) id = "minecraft:" + id;
    if (game::blockWith(id)) put(id.c_str());
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
        // the gallery's magic painting is U22's entity (twilightforest:magic_painting, the variant named
        // after the gallery); it is not spawned yet (docs/units/lich.md, "Not implemented")
        if (label == "painting") return;
        sx::placePainting(label, pos, region, random, chunk, placeSettings.rotation, 2, 1, "twilightforest:tower_paintings");
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
        // FrontAndTop.fromFrontAndTop(front, top.getOpposite()) of a horizontal jigsaw (top UP -> DOWN)
        // names no FrontAndTop value: the roll is kept for the random sequence, the placement uses
        // the jigsaw as it is
        JigsawRecord forPlacement = c;
        (void)random.nextBoolean();
        auto id = randomTemplate(random, tid("mob_bridge"));
        auto pc = pick(templatePos(), forPlacement, id, "gf:mob_bridge", random);
        if (!pc) return;
        auto  bridge = std::make_unique<SpawnerBridge>(genDepth + 1, *id, *pc, random.nextBoolean());
        auto* raw    = bridge.get();
        pieces.push_back(std::move(bridge));
        grow(*raw, *this, pieces, random);
    }
}

// ---- start ---------------------------------------------------------------------------------------------

/// LichTowerStructure#getFirstPiece + #generateFromStartingPiece.
bool layout(gf::landmark::LayoutContext& ctx) {
    auto& start     = ctx.start;
    auto  ctxRandom = gf::landmark::ext::contextRandom(start);
    int   dim       = start.dimension;
    sx::GenerationContext gctx{ctxRandom.get(), gf::worldgen::seed(), [dim](gf::worldgen::Heightmap type, int x, int z) {
                                   return gf::landmark::ext::firstOccupiedHeight(dim, x, z, type).value_or(0);
                               }};
    RandomSource& random = ctx.random;
    // direction = Rotation.getRandom(random).rotate(SOUTH); 24 blocks that way
    static Rotation const kRotations[] = {Rotation::None, Rotation::Clockwise90, Rotation::Clockwise180, Rotation::Counterclockwise90};
    Orientation           direction    = rotate(Orientation::South, kRotations[random.nextInt(4)]);
    BlockPos              placePos     = relative(BlockPos{start.center.x, start.center.y, start.center.z}, direction, 24);
    JigsawRecord          oriented     = verticalRecord(JigsawRecord{}, true, direction);
    oriented.pos                       = BlockPos{0, 0, 0};
    auto pc = JigsawPlaceContext::pickPlaceableJunction(placePos, BlockPos{0, 0, 0}, oriented, gid("tower_foyer"), "gf:lich_tower/vestibule", random);
    if (!pc) {
        log().warn("lich: the lich tower foyer template is missing (twilightforest:lich_tower/tower_foyer)");
        return false;
    }
    auto  foyer = std::make_unique<Foyer>(*pc, true, random.nextBoolean());
    auto* raw   = foyer.get();
    PieceList pieces;
    pieces.push_back(std::move(foyer));
    gContext = &gctx;
    sx::addJigsaws(*raw, *raw, pieces, gctx);
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
        }
    }
    start.data["lich_spawns"] = spawns;
    start.data["lich_trims"]  = trims;
    start.data["lich_ybase"]  = yBase;
    for (auto& p : pieces) ctx.pieces.push_back(std::move(p));
    return true;
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
        if (label == kDenySpawns) return -1;
        best = std::max(best, label);
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

void registerTower() {
    gf::landmark::registerGenerator(kTowerKey, {.layout = layout, .terraform = terraform, .spawnIndex = spawnIndex});
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
    gs::registerPieceType(kGallery, [](Json const& s) -> std::unique_ptr<gs::Piece> {
        auto p = std::make_unique<Gallery>(0, s.value("Template", std::string()), JigsawPlaceContext{});
        p->load(s);
        return p;
    });
}

} // namespace gf::feature::lich::tower
