// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/type/NagaCourtyardStructure.java
//   src/main/java/twilightforest/world/components/structures/courtyard/CourtyardMain.java
//   src/main/java/twilightforest/world/components/structures/courtyard/StructureMazeGenerator.java
//   src/main/java/twilightforest/world/components/structures/courtyard/NagaCourtyardHedgeAbstractComponent.java
//   src/main/java/twilightforest/world/components/structures/courtyard/NagaCourtyardHedgeCapComponent.java
//   src/main/java/twilightforest/world/components/structures/courtyard/NagaCourtyardHedgeCapPillarComponent.java
//   src/main/java/twilightforest/world/components/structures/courtyard/NagaCourtyardHedgeCornerComponent.java
//   src/main/java/twilightforest/world/components/structures/courtyard/NagaCourtyardHedgeIntersectionComponent.java
//   src/main/java/twilightforest/world/components/structures/courtyard/NagaCourtyardHedgeLineComponent.java
//   src/main/java/twilightforest/world/components/structures/courtyard/NagaCourtyardHedgePadderComponent.java
//   src/main/java/twilightforest/world/components/structures/courtyard/NagaCourtyardHedgeTJunctionComponent.java
//   src/main/java/twilightforest/world/components/structures/courtyard/CourtyardPathPiece.java
//   src/main/java/twilightforest/world/components/structures/courtyard/CourtyardTerrace.java
//   src/main/java/twilightforest/world/components/structures/courtyard/CourtyardTerraceHedge.java
//   src/main/java/twilightforest/world/components/structures/courtyard/CourtyardTerraceTemplateProcessor.java
//   src/main/java/twilightforest/world/components/structures/courtyard/CourtyardWall.java
//   src/main/java/twilightforest/world/components/structures/courtyard/CourtyardWallCornerInner.java
//   src/main/java/twilightforest/world/components/structures/courtyard/CourtyardWallCornerOuter.java
//   src/main/java/twilightforest/world/components/structures/courtyard/CourtyardWallPadder.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The Naga courtyard (U20): the structure type twilightforest:naga_courtyard as an F6 landmark
// generator, the 15 piece types and the layout of StructureMazeGenerator (src/core/naga). The 32
// templates are original GloamingForest designs (tools/structures/naga, rule R19). The
// CourtyardTerraceTemplateProcessor is F7's "twilightforest:courtyard_terrace".

#include "feature/naga/NagaInternal.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <set>

#include "core/naga/Courtyard.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/structure/TemplateDefinitions.h"
#include "feature/landmark/Landmark.h"
#include "feature/structure/Structure.h"
#include "feature/structure/bases/Component.h"
#include "feature/structure/bases/TemplatePieces.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::naga {

namespace st = gf::structure;
namespace sb = gf::structure::bases;
namespace cn = gf::core::naga;
using Json   = nlohmann::json;
using core::BlockBox;

namespace {

constexpr char kKey[] = "twilightforest:naga_courtyard";

// CourtyardMain.HEDGE_FLOOF, WALL_DECAY, WALL_INTEGRITY as inline processors
std::string blockRot(float integrity) {
    return Json{{"processor_type", "minecraft:block_rot"}, {"integrity", integrity}}.dump();
}
std::string const& ignoreAir() {
    static std::string const s = R"({"processor_type":"minecraft:block_ignore","blocks":[{"Name":"minecraft:air"}]})";
    return s;
}
constexpr float kHedgeFloof    = 0.5f;
constexpr float kWallDecay     = 0.1f;
constexpr float kWallIntegrity = 0.95f;

constexpr char kNagastone[]   = "twilightforest:nagastone_variants";
constexpr char kStoneBricks[] = "twilightforest:stone_brick_variants";
constexpr char kSmoothStone[] = "twilightforest:smooth_stone_variants";
constexpr char kCobble[]      = "twilightforest:cobble_variants";
constexpr char kTerrace[]     = "twilightforest:courtyard_terrace";

st::Rotation rotationOf(int index) { return static_cast<st::Rotation>(((index % 4) + 4) % 4); }

/// The courtyard's templates: gloamingforest:courtyard/<TF name> (TF: twilightforest:courtyard/<name>;
/// assemble's dangling reference check refuses a twilightforest: template id that owners.tsv does
/// not know), the spawner keeping TF's id (its template definition is known).
std::string tpl(char const* name) { return std::string("gloamingforest:courtyard/") + name; }
constexpr char kCenterJigsaw[] = "gloamingforest:courtyard_center"; ///< TF: "twilightforest:center"

// ---- NagaCourtyardHedgeAbstractComponent ---------------------------------------------------------

struct HedgeKind {
    char const* type;
    char const* hedge;
    char const* big;
};
HedgeKind hedgeKind(cn::PieceKind kind) {
    switch (kind) {
    case cn::PieceKind::HedgeCap:
        return {"twilightforest:TFNCCp", "hedge_end", "hedge_end_big"};
    case cn::PieceKind::HedgeCapPillar:
        return {"twilightforest:TFNCCpP", "hedge_end_pillar", "hedge_end_pillar_big"};
    case cn::PieceKind::HedgeCorner:
        return {"twilightforest:TFNCCr", "hedge_corner", "hedge_corner_big"};
    case cn::PieceKind::HedgeT:
        return {"twilightforest:TFNCT", "hedge_t", "hedge_t_big"};
    case cn::PieceKind::HedgeIntersection:
        return {"twilightforest:TFNCIs", "hedge_intersection", "hedge_intersection_big"};
    case cn::PieceKind::HedgePadder:
        return {"twilightforest:TFNCPd", "hedge_between", "hedge_between_big"};
    default:
        return {"twilightforest:TFNCLn", "hedge_line", "hedge_line_big"};
    }
}

class HedgePiece final : public sb::ComponentTemplate {
public:
    HedgePiece(HedgeKind kind, int depth, int x, int y, int z, st::Rotation rot)
    : ComponentTemplate(kind.type, depth, x, y, z, rot),
      mHedge(tpl(kind.hedge)),
      mHedgeBig(tpl(kind.big)) {}
    HedgePiece(HedgeKind kind, Json const& saved) : ComponentTemplate(kind.type, 0, 0, 0, 0, st::Rotation::None), mHedge(tpl(kind.hedge)), mHedgeBig(tpl(kind.big)) {
        load(saved);
        mLoaded = true;
    }

    void runLazyLoader() {
        BlockBox keep = bbox;
        setup();
        if (mLoaded) bbox = keep; // a loaded piece keeps the box it was saved with
        mSetUp = true;
    }

    void postProcess(st::Region& region, st::RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override {
        (void)random;
        (void)pivot;
        if (!mSetUp) runLazyLoader(); // the LAZY_TEMPLATE_LOADER of a loaded piece
        st::TemplateSettings s;
        s.rotation   = placeSettings.rotation;
        s.mirror     = placeSettings.mirror;
        s.clip       = chunkBox;
        s.processors = {kNagastone};
        st::templates::place(mHedge, region, rotatedPosition, s);
        // placeSettings keeps NagastoneVariants (Java mutates the same settings): + air ignored +
        // BlockRotProcessor(HEDGE_FLOOF)
        s.processors = {kNagastone, ignoreAir(), blockRot(kHedgeFloof)};
        st::templates::place(mHedgeBig, region, rotatedPosition, s);
    }

protected:
    void loadTemplates() override { templateId = mHedge; }

private:
    std::string mHedge, mHedgeBig;
    bool        mSetUp  = false;
    bool        mLoaded = false;
};

// ---- CourtyardPathPiece -------------------------------------------------------------------------

class PathPiece final : public sb::TemplatePiece {
public:
    PathPiece(int depth, int x, int y, int z)
    : TemplatePiece("twilightforest:TFNCPa", depth, tpl("pathway"), withProcessors(sb::makeSettings(st::Rotation::None), {kNagastone}), BlockPos{x, y + 1, z}) {}
    explicit PathPiece(Json const& saved) : TemplatePiece("twilightforest:TFNCPa", 0, tpl("pathway"), sb::makeSettings(st::Rotation::None), BlockPos{0, 0, 0}) {
        load(saved);
        if (placeSettings.processors.empty()) placeSettings.processors = {kNagastone};
    }

    void postProcess(st::Region& region, st::RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override {
        // F7's placePieceAdjusted calls the virtual postProcess (Java: customPostProcess): the
        // inner call places the template
        if (mAdjusting) {
            TemplatePiece::postProcess(region, random, chunkBox, pivot);
            return;
        }
        mAdjusting = true;
        placePieceAdjusted(region, random, chunkBox, pivot, -1);
        mAdjusting = false;
    }

    static sb::PlaceSettings withProcessors(sb::PlaceSettings s, std::vector<std::string> p) {
        s.processors = std::move(p);
        return s;
    }

private:
    bool mAdjusting = false;
};

// ---- CourtyardTerrace / CourtyardTerraceHedge ---------------------------------------------------

sb::PlaceSettings terraceSettings() {
    auto s       = sb::makeSettings(st::Rotation::None);
    s.processors = {kTerrace, kNagastone, kStoneBricks};
    return s;
}

class TerracePiece final : public sb::TemplatePiece {
public:
    TerracePiece(bool hedge, int depth, int x, int y, int z, std::string const& templateId)
    : TemplatePiece(hedge ? "twilightforest:TFNCHe" : "twilightforest:TFNCTe", depth, templateId, terraceSettings(), BlockPos{x, y, z}),
      mHedge(hedge) {}
    TerracePiece(bool hedge, Json const& saved)
    : TemplatePiece(hedge ? "twilightforest:TFNCHe" : "twilightforest:TFNCTe", 0, tpl(hedge ? "terrace_hedge" : "terrace_fire"), terraceSettings(), BlockPos{0, 0, 0}),
      mHedge(hedge) {
        load(saved);
        if (placeSettings.processors.empty()) placeSettings.processors = terraceSettings().processors;
    }

    void postProcess(st::Region& region, st::RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override {
        TemplatePiece::postProcess(region, random, chunkBox, pivot);
        if (!mHedge) return;
        // placeSettings.copy().clearProcessors() + air ignored + BlockRotProcessor(HEDGE_FLOOF)
        st::TemplateSettings s;
        s.rotation   = placeSettings.rotation;
        s.mirror     = placeSettings.mirror;
        s.clip       = chunkBox;
        s.processors = {ignoreAir(), blockRot(kHedgeFloof)};
        st::templates::place(tpl("terrace_hedge_big"), region, templatePosition, s);
    }

    /// PieceBeardifierModifier: the piece box, BEARD_BOX, ground level delta 3.
    [[nodiscard]] bool hedge() const { return mHedge; }

private:
    bool mHedge;
};

// ---- CourtyardWall / WallPadder / WallCornerOuter / WallCornerInner -----------------------------

struct WallKind {
    char const* type;
    char const* intact;
    char const* decayed;
};
WallKind wallKind(cn::PieceKind kind) {
    switch (kind) {
    case cn::PieceKind::WallPadder:
        return {"twilightforest:TFNCWP", "courtyard_wall_padding", "courtyard_wall_padding_decayed"};
    case cn::PieceKind::WallCornerOuter:
        return {"twilightforest:TFNCWC", "courtyard_wall_corner", "courtyard_wall_corner_decayed"};
    case cn::PieceKind::WallCornerInner:
        return {"twilightforest:TFNCWA", "courtyard_wall_corner_inner", "courtyard_wall_corner_inner_decayed"};
    default:
        return {"twilightforest:TFNCWl", "courtyard_wall", "courtyard_wall_decayed"};
    }
}

sb::PlaceSettings wallSettings(st::Rotation rotation) {
    auto s       = sb::makeSettings(rotation);
    s.processors = {blockRot(kWallIntegrity), kSmoothStone, kNagastone, kStoneBricks, kCobble};
    return s;
}
sb::PlaceSettings wallDecaySettings(st::Rotation rotation) {
    auto s       = sb::makeSettings(rotation);
    s.processors = {blockRot(kWallDecay), kCobble};
    return s;
}

class WallPiece final : public sb::DoubleTemplatePiece {
public:
    WallPiece(WallKind kind, int depth, int x, int y, int z, st::Rotation rotation)
    : DoubleTemplatePiece(kind.type, depth, tpl(kind.intact), wallSettings(rotation), tpl(kind.decayed), wallDecaySettings(rotation), BlockPos{x, y, z}) {}
    WallPiece(WallKind kind, Json const& saved)
    : DoubleTemplatePiece(kind.type, 0, tpl(kind.intact), wallSettings(st::Rotation::None), tpl(kind.decayed), wallDecaySettings(st::Rotation::None), BlockPos{0, 0, 0}) {
        load(saved);
        // readSettings(nbt) + the processors of the constructor (TF's codec constructor)
        auto rot              = placeSettings.rotation;
        placeSettings         = wallSettings(rot);
        placeSettingsOverlay  = wallDecaySettings(placeSettingsOverlay.rotation);
    }
};

// ---- CourtyardMain ------------------------------------------------------------------------------

class CourtyardMain final : public sb::Component {
public:
    /// CourtyardMain(rand, i, x, y, z): the maze is generated from `random` (the start piece's
    /// random of LandmarkStructure#findGenerationPoint).
    CourtyardMain(st::RandomSource& random, int depth, int x, int y, int z) : Component("twilightforest:TFNCMn", depth, x, y, z) {
        for (auto& row : mMaze) row.fill(0);
        cn::generateMaze(mMaze, mClipping, random);
        orientation      = st::Orientation::North;
        mirror           = st::Mirror::None;
        rotation         = st::Rotation::None;
        bbox             = cn::mainBox(x, y, z);
        mSizeConstraints = cn::sizeConstraints(x, y, z);
        mPlaceSpawner    = false;
    }
    explicit CourtyardMain(Json const& saved) : Component("twilightforest:TFNCMn", 0, 0, 0, 0) {
        load(saved);
        for (auto& row : mMaze) row.fill(0);
        if (auto const& m = saved.value("maze", Json::array()); m.is_array())
            for (int x = 0; x < cn::kMazeSize && x < static_cast<int>(m.size()); x++)
                for (int y = 0; y < cn::kMazeSize && y < static_cast<int>(m[x].size()); y++) mMaze[x][y] = m[x][y].get<int>();
        if (auto const& c = saved.value("constraints", Json::array()); c.is_array() && c.size() == 6)
            mSizeConstraints = BlockBox{c[0].get<int>(), c[1].get<int>(), c[2].get<int>(), c[3].get<int>(), c[4].get<int>(), c[5].get<int>()};
        // old courtyards without the flag placed the spawner themselves
        mPlaceSpawner = saved.value("placeSpawner", true);
    }

    /// StructureMazeGenerator#addChildren + CourtyardMain#addChildren.
    void addChildren(st::Piece& start, std::vector<std::unique_ptr<st::Piece>>& pieces, st::RandomSource& random) override {
        (void)start;
        for (auto const& r : cn::layoutPieces(mMaze, mClipping, mSizeConstraints, random)) pieces.push_back(makePiece(r));
        BlockPos pos = getWorldPos(cn::kRadius / 2, 1, cn::kRadius / 2);
        // Rotation.getRandom(random).rotate(Direction.SOUTH), as FrontAndTop(UP, direction)
        static constexpr gf::structure::ext::FrontAndTop kUp[4] = {
            gf::structure::ext::FrontAndTop::UpSouth, gf::structure::ext::FrontAndTop::UpWest, gf::structure::ext::FrontAndTop::UpNorth,
            gf::structure::ext::FrontAndTop::UpEast};
        auto oriented = kUp[random.nextInt(4)];
        auto spawner  = gf::structure::ext::initializeTemplateFromPool("twilightforest:courtyard/center", pos, oriented, kCenterJigsaw, random, genDepth + 1);
        if (spawner) pieces.push_back(std::move(spawner));
        else log().warn("naga: the courtyard's center pool gave no spawner piece at {} {} {}", pos.x, pos.y, pos.z);
    }

    void postProcess(st::Region& region, st::RandomSource&, BlockBox const& chunkBox, BlockPos const&) override {
        if (mPlaceSpawner) placeBlock(region, "twilightforest:naga_boss_spawner", Json::object(), cn::kRadius / 2, 3, cn::kRadius / 2, chunkBox);
    }

    [[nodiscard]] Json save() const override {
        Json j    = Component::save();
        Json maze = Json::array();
        for (auto const& row : mMaze) maze.push_back(Json(row));
        j["mazeWidth"]    = cn::kRowOfCells;
        j["mazeHeight"]   = cn::kRowOfCells;
        j["maze"]         = std::move(maze);
        j["constraints"]  = Json::array({mSizeConstraints.minX, mSizeConstraints.minY, mSizeConstraints.minZ, mSizeConstraints.maxX, mSizeConstraints.maxY,
                                         mSizeConstraints.maxZ});
        j["placeSpawner"] = mPlaceSpawner;
        return j;
    }

    [[nodiscard]] cn::Maze const& maze() const { return mMaze; }

private:
    cn::Maze           mMaze{};
    cn::CornerClipping mClipping{};
    BlockBox           mSizeConstraints{};
    bool               mPlaceSpawner = false;
};

} // namespace

std::unique_ptr<st::Piece> makePiece(cn::PieceRecord const& r) {
    auto rot = rotationOf(r.rotation);
    switch (r.kind) {
    case cn::PieceKind::HedgeCap:
    case cn::PieceKind::HedgeCapPillar:
    case cn::PieceKind::HedgeCorner:
    case cn::PieceKind::HedgeT:
    case cn::PieceKind::HedgeLine:
    case cn::PieceKind::HedgeIntersection:
    case cn::PieceKind::HedgePadder:
        return std::make_unique<HedgePiece>(hedgeKind(r.kind), r.genDepth, r.x, r.y, r.z, rot);
    case cn::PieceKind::Terrace:
    case cn::PieceKind::TerraceHedge:
        return std::make_unique<TerracePiece>(r.kind == cn::PieceKind::TerraceHedge, r.genDepth, r.x, r.y, r.z, r.terraceTemplate);
    case cn::PieceKind::Path:
        return std::make_unique<PathPiece>(r.genDepth, r.x, r.y, r.z);
    case cn::PieceKind::Wall:
    case cn::PieceKind::WallPadder:
    case cn::PieceKind::WallCornerOuter:
    case cn::PieceKind::WallCornerInner:
        return std::make_unique<WallPiece>(wallKind(r.kind), r.genDepth, r.x, r.y, r.z, rot);
    }
    return nullptr;
}

std::vector<std::unique_ptr<st::Piece>> courtyardPieces(st::RandomSource& firstPieceRandom, st::RandomSource& contextRandom, int x, int y, int z) {
    std::vector<std::unique_ptr<st::Piece>> pieces;
    // NagaCourtyardStructure#getFirstPiece: CourtyardMain(random, 0, x + 1, y, z + 1)
    auto  main = std::make_unique<CourtyardMain>(firstPieceRandom, 0, x + 1, y, z + 1);
    auto* raw  = main.get();
    pieces.push_back(std::move(main));
    raw->addChildren(*raw, pieces, contextRandom);
    // no courtyard piece is a SortablePiece; TFStructureComponentTemplate's LAZY_TEMPLATE_LOADER
    static std::set<std::string> const hedges{"twilightforest:TFNCCp", "twilightforest:TFNCCpP", "twilightforest:TFNCCr", "twilightforest:TFNCT",
                                              "twilightforest:TFNCLn", "twilightforest:TFNCIs", "twilightforest:TFNCPd"};
    for (auto& p : pieces)
        if (hedges.contains(p->typeId)) static_cast<HedgePiece*>(p.get())->runLazyLoader();
    return pieces;
}

namespace {

/// The beardifier of the start, as NeoForge's Beardifier builds its rigids: a piece implementing
/// PieceBeardifierModifier gives Rigid(getBeardifierBox, getTerrainAdjustment, getGroundLevelDelta)
/// unless its adjustment is NONE (the terraces CourtyardTerrace / CourtyardTerraceHedge: BEARD_BOX
/// with a ground level delta of 3; the spawner's TwilightJigsawPiece: its pool entry's adjustment,
/// NONE for courtyard/center), every other piece Rigid(box, the structure's BEARD_THIN, 0); plus
/// TF's inverted pyramid terraformer (CustomDensitySource, 3, 4). Vanilla's rigid term is
/// BoxDensityFunction's with the box's bottom raised by the delta (dYMin), so boxDensity computes
/// it exactly. The boxes near a chunk are gathered once per chunk and thread.
struct BeardCache {
    std::int64_t          key = 0;
    int                   chunkX = 0, chunkZ = 0;
    std::vector<BlockBox> thin;    ///< BEARD_THIN, delta 0
    std::vector<BlockBox> terrace; ///< BEARD_BOX, delta 3
};

/// The beardifier kind of a piece type: 0 BEARD_THIN, 1 BEARD_BOX (delta 3), 2 none.
int beardKind(std::string const& type) {
    if (type == "twilightforest:TFNCTe" || type == "twilightforest:TFNCHe") return 1;
    if (type == "twilightforest:TFJigsawTemplate") return 2;
    return 0;
}

double terraform(gf::landmark::Start const& start, int x, int y, int z) {
    thread_local BeardCache cache;
    std::int64_t key = start.seed ^ (static_cast<std::int64_t>(start.center.x) << 32) ^ static_cast<std::int64_t>(start.center.z) * 31 ^
                       static_cast<std::int64_t>(start.pieceBoxes.size()) << 20;
    int cx = x >> 4, cz = z >> 4;
    if (cache.key != key || cache.chunkX != cx || cache.chunkZ != cz) {
        cache.key    = key;
        cache.chunkX = cx;
        cache.chunkZ = cz;
        cache.thin.clear();
        cache.terrace.clear();
        BlockBox around{(cx << 4) - 24, -4096, (cz << 4) - 24, (cx << 4) + 39, 4096, (cz << 4) + 39};
        // the piece types come with the start's record; a start the store does not hold (no
        // types) beards every box thin
        auto pieces = gf::landmark::ext::startPieces(start);
        bool typed  = !pieces.empty() && !pieces.front().type.empty();
        if (typed) {
            for (auto const& p : pieces) {
                if (!p.box.intersects(around)) continue;
                int kind = beardKind(p.type);
                if (kind == 0) cache.thin.push_back(p.box);
                else if (kind == 1) cache.terrace.push_back(p.box);
            }
        } else
            for (auto const& b : start.pieceBoxes)
                if (b.intersects(around)) cache.thin.push_back(b);
    }
    double d = gf::landmark::ext::invertedPyramid(start, 3, 4, x, y, z);
    if (!cache.thin.empty()) d += gf::landmark::ext::boxDensity(cache.thin, 0, 0, "beard_thin", x, y, z);
    if (!cache.terrace.empty()) d += gf::landmark::ext::boxDensity(cache.terrace, 3, 0, "beard_box", x, y, z);
    return d;
}

} // namespace

void installCourtyard() {
    // the piece types (TFStructurePieceTypes), for persisted starts
    using K = cn::PieceKind;
    for (K kind : {K::HedgeCap, K::HedgeCapPillar, K::HedgeCorner, K::HedgeT, K::HedgeLine, K::HedgeIntersection, K::HedgePadder}) {
        auto hk = hedgeKind(kind);
        st::registerPieceType(hk.type, [hk](Json const& saved) -> std::unique_ptr<st::Piece> { return std::make_unique<HedgePiece>(hk, saved); });
    }
    for (K kind : {K::Wall, K::WallPadder, K::WallCornerOuter, K::WallCornerInner}) {
        auto wk = wallKind(kind);
        st::registerPieceType(wk.type, [wk](Json const& saved) -> std::unique_ptr<st::Piece> { return std::make_unique<WallPiece>(wk, saved); });
    }
    st::registerPieceType("twilightforest:TFNCTe", [](Json const& saved) -> std::unique_ptr<st::Piece> { return std::make_unique<TerracePiece>(false, saved); });
    st::registerPieceType("twilightforest:TFNCHe", [](Json const& saved) -> std::unique_ptr<st::Piece> { return std::make_unique<TerracePiece>(true, saved); });
    st::registerPieceType("twilightforest:TFNCPa", [](Json const& saved) -> std::unique_ptr<st::Piece> { return std::make_unique<PathPiece>(saved); });
    st::registerPieceType("twilightforest:TFNCMn", [](Json const& saved) -> std::unique_ptr<st::Piece> { return std::make_unique<CourtyardMain>(saved); });

    gf::landmark::registerGenerator(kKey, gf::landmark::Generator{
        .layout =
            [](gf::landmark::LayoutContext& ctx) {
                auto context = gf::landmark::ext::contextRandom(ctx.start);
                auto pieces  = courtyardPieces(ctx.random, *context, ctx.start.center.x, ctx.start.center.y, ctx.start.center.z);
                for (auto& p : pieces) ctx.pieces.push_back(std::move(p));
                return true;
            },
        .terraform = terraform,
    });
}

} // namespace gf::feature::naga
