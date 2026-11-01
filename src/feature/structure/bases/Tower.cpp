// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/lichtower/TowerWingComponent.java
//   src/main/java/twilightforest/world/components/structures/lichtower/TowerMainComponent.java
//   src/main/java/twilightforest/world/components/structures/lichtower/TowerBridgeComponent.java
//   src/main/java/twilightforest/world/components/structures/lichtower/TowerOutbuildingComponent.java
//   src/main/java/twilightforest/world/components/structures/lichtower/TowerBeardComponent.java
//   src/main/java/twilightforest/world/components/structures/lichtower/TowerBeardAttachedComponent.java
//   src/main/java/twilightforest/world/components/structures/lichtower/TowerRoofComponent.java
//   src/main/java/twilightforest/world/components/structures/lichtower/TowerRoofAttachedSlabComponent.java
//   src/main/java/twilightforest/world/components/structures/lichtower/TowerRoofFenceComponent.java
//   src/main/java/twilightforest/world/components/structures/lichtower/TowerRoofGableForwardsComponent.java
//   src/main/java/twilightforest/world/components/structures/lichtower/TowerRoofPointyComponent.java
//   src/main/java/twilightforest/world/components/structures/lichtower/TowerRoofPointyOverhangComponent.java
//   src/main/java/twilightforest/world/components/structures/lichtower/TowerRoofSlabComponent.java
//   src/main/java/twilightforest/world/components/structures/lichtower/TowerRoofSlabForwardsComponent.java
//   src/main/java/twilightforest/world/components/structures/lichtower/TowerRoofStairsComponent.java
//   src/main/java/twilightforest/world/components/structures/lichtower/TowerRoofStairsOverhangComponent.java
//   src/main/java/twilightforest/util/TFStructureHelper.java
//   src/main/java/twilightforest/util/entities/EntityUtil.java (getPaintingOfSize)
//   src/main/java/twilightforest/util/BoundingBoxUtils.java (getComponentToAddBoundingBox)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "feature/structure/bases/Tower.h"

#include <algorithm>
#include <array>

#include "mc/world/level/block/Block.h"

#include "feature/features/Features.h"
#include "feature/structure/Internal.h"

namespace gf::structure::bases {

namespace {

/// A null Java Rotation (the ladder directions TF passes as null).
constexpr auto kNoRotation = static_cast<Rotation>(-1);
bool           isNull(Rotation r) { return static_cast<int>(r) < 0; }
Rotation       orNull(std::optional<Rotation> r) { return r.value_or(kNoRotation); }

constexpr std::array<Rotation, 4> kRotations{Rotation::None, Rotation::Clockwise90, Rotation::Clockwise180, Rotation::Counterclockwise90};

Block const& B(char const* id, Json const& props = Json::object()) { return blocks::java(id, props); }

// TFStructureHelper
Block const& stoneSlab() { return B("minecraft:smooth_stone_slab", Json{{"type", "bottom"}}); }
Block const& stoneSlabTop() { return B("minecraft:smooth_stone_slab", Json{{"type", "top"}}); }
Block const& stoneSlabDouble() { return B("minecraft:smooth_stone_slab", Json{{"type", "double"}}); }
Block const& birchSlab() { return B("minecraft:birch_slab", Json{{"type", "bottom"}}); }
Block const& birchSlabTop() { return B("minecraft:birch_slab", Json{{"type", "top"}}); }
Block const& birchPlanks() { return B("minecraft:birch_planks"); }

Block const& randomPlant(int i) {
    static char const* const plants[] = {"minecraft:potted_oak_sapling", "minecraft:potted_spruce_sapling", "minecraft:potted_birch_sapling",
                                         "minecraft:potted_jungle_sapling", "minecraft:potted_red_mushroom", "minecraft:potted_brown_mushroom"};
    return B(plants[std::clamp(i, 0, 5)]);
}
char const* randomTree(int i) {
    switch (i) {
    case 1:
        return "minecraft:spruce";
    case 2:
        return "minecraft:birch";
    case 3:
        return "minecraft:jungle_tree_no_vine";
    default:
        return "minecraft:oak";
    }
}

/// BoundingBoxUtils.getComponentToAddBoundingBox(..., centerBounds = false).
BlockBox componentBox(int x, int y, int z, int minX, int minY, int minZ, int spanX, int spanY, int spanZ, std::optional<Orientation> dir) {
    switch (dir.value_or(Orientation::South)) {
    case Orientation::West:
        return BlockBox::of(x - spanZ + minZ, y + minY, z + minX, x + minZ, y + spanY + minY, z + spanX + minX);
    case Orientation::North:
        return BlockBox::of(x - spanX - minX, y + minY, z - spanZ - minZ, x - minX, y + spanY + minY, z - minZ);
    case Orientation::East:
        return BlockBox::of(x + minZ, y + minY, z - spanX, x + spanZ + minZ, y + spanY + minY, z + minX);
    default:
        return BlockBox::of(x + minX, y + minY, z + minZ, x + spanX + minX, y + spanY + minY, z + spanZ + minZ);
    }
}

/// The painting variants of 1.21.1's registry (data-driven: sorted by id), width x height in blocks.
struct PaintingVariant {
    char const* id;
    int         w, h;
};
std::vector<PaintingVariant> const& paintingRegistry() {
    static std::vector<PaintingVariant> const v{
        {"alban", 1, 1},     {"aztec", 1, 1},      {"aztec2", 1, 1},   {"backyard", 3, 4},     {"baroque", 2, 2},  {"bomb", 1, 1},     {"bouquet", 3, 3},
        {"burning_skull", 4, 4}, {"bust", 2, 2},   {"cavebird", 3, 3}, {"changing", 4, 2},     {"cotan", 3, 3},    {"courbet", 2, 1},  {"creebet", 2, 1},
        {"donkey_kong", 4, 3}, {"earth", 2, 2},    {"endboss", 3, 3},  {"fern", 3, 3},         {"fighters", 4, 2}, {"finding", 4, 2},  {"fire", 2, 2},
        {"graham", 1, 2},    {"humble", 2, 2},     {"kebab", 1, 1},    {"lowmist", 4, 2},      {"match", 2, 2},    {"meditative", 1, 1}, {"orb", 4, 4},
        {"owlemons", 3, 3},  {"passage", 4, 2},    {"pigscene", 4, 4}, {"plant", 1, 1},        {"pointer", 4, 4},  {"pond", 3, 4},     {"pool", 2, 1},
        {"prairie_ride", 1, 2}, {"sea", 2, 1},     {"skeleton", 4, 3}, {"skull_and_roses", 2, 2}, {"stage", 2, 2}, {"sunflowers", 3, 3}, {"sunset", 2, 1},
        {"tides", 3, 3},     {"unpacked", 4, 4},   {"void", 2, 2},     {"wanderer", 1, 2},     {"wasteland", 1, 1}, {"water", 2, 2},   {"wind", 2, 2},
        {"wither", 2, 2},
    };
    return v;
}

/// EntityUtil.getPaintingOfSize(level, rand, minSize, minSize, false). TF still passes the old pixel
/// sizes (48, 32), so only minSize 0 finds paintings, as in TF.
std::optional<PaintingVariant> paintingOfSize(RandomSource& rand, int minSize) {
    std::vector<PaintingVariant> valid;
    for (auto const& v : paintingRegistry())
        if (v.w >= minSize || v.h >= minSize) valid.push_back(v);
    if (valid.empty()) return std::nullopt;
    return valid[static_cast<std::size_t>(rand.nextInt(static_cast<int>(valid.size())))];
}

/// Turns a ComponentOld's orientation for the scope (TF's setOrientation(rotation.rotate(temp)) ...
/// setOrientation(temp)).
class FakeOrientation {
public:
    FakeOrientation(ComponentOld& piece, std::optional<Orientation> o) : mPiece(piece), mO(piece.orientation), mR(piece.rotation), mM(piece.mirror) {
        piece.setOrientation(o);
    }
    ~FakeOrientation() {
        mPiece.orientation = mO;
        mPiece.rotation    = mR;
        mPiece.mirror      = mM;
    }
    FakeOrientation(FakeOrientation const&)            = delete;
    FakeOrientation& operator=(FakeOrientation const&) = delete;

private:
    ComponentOld& mPiece;
    Orientation   mO;
    Rotation      mR;
    Mirror        mM;
};

std::optional<Orientation> turned(std::optional<Orientation> o, Rotation r) {
    if (!o) return std::nullopt;
    return rotateDir(*o, r);
}

Rotation rotationPlus(Rotation start, int rotations) { return rotationFromIndex(rotationIndex(start) + rotations); }

} // namespace

// ==== TowerWingComponent ================================================================================

TowerWing::TowerWing(std::string typeId, int depth, int x, int y, int z) : ComponentOld(std::move(typeId), depth, x, y, z) { highestOpening = 0; }

TowerWing::TowerWing(std::string typeId, int depth, int x, int y, int z, int pSize, int pHeight, Orientation direction)
: ComponentOld(std::move(typeId), depth, x, y, z) {
    size   = pSize;
    height = pHeight;
    setOrientation(direction);
    highestOpening = 0;
    bbox           = componentBox(x, y, z, 0, 0, 0, size - 1, height - 1, size - 1, direction);
}

Json TowerWing::save() const {
    Json j           = ComponentOld::save();
    j["towerSize"]   = size;
    j["towerHeight"] = height;
    Json doors       = Json::array();
    for (auto const& d : openings) {
        doors.push_back(d.x);
        doors.push_back(d.y);
        doors.push_back(d.z);
    }
    j["doorInts"]       = doors;
    j["highestOpening"] = highestOpening;
    for (int i = 0; i < 4; i++) j["openingTowards" + std::to_string(i)] = openingTowards[static_cast<std::size_t>(i)];
    return j;
}

void TowerWing::load(Json const& data) {
    ComponentOld::load(data);
    size   = data.value("towerSize", 0);
    height = data.value("towerHeight", 0);
    openings.clear();
    auto const& doors = data.value("doorInts", Json::array());
    for (std::size_t i = 0; i + 2 < doors.size(); i += 3) openings.emplace_back(doors[i].get<int>(), doors[i + 1].get<int>(), doors[i + 2].get<int>());
    highestOpening = data.value("highestOpening", 0);
    for (int i = 0; i < 4; i++) openingTowards[static_cast<std::size_t>(i)] = data.value("openingTowards" + std::to_string(i), false);
}

void TowerWing::addChildren(Piece& parent, PieceList& list, RandomSource& rand) {
    // we should have a door where we started
    addOpening(0, 1, size / 2, Rotation::Clockwise180);
    makeARoof(parent, list, rand);
    makeABeard(parent, list, rand);
    if (size > 4) {
        for (auto towerRotation : kRotations) {
            if (towerRotation == Rotation::Clockwise180) continue;
            auto dest = getValidOpening(rand, towerRotation);
            if (!makeTowerWing(list, rand, 1, dest[0], dest[1], dest[2], size - 2, height - 4, towerRotation) && size > 8) {
                if (!makeTowerWing(list, rand, 1, dest[0], dest[1], dest[2], size - 4, height - 6, towerRotation))
                    makeTowerWing(list, rand, 1, dest[0], dest[1], dest[2], size - 6, height - 12, towerRotation);
            }
        }
    }
}

bool TowerWing::makeTowerWing(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight, Rotation rotation) {
    if (wingHeight < 6) return false;
    Orientation direction = getStructureRelativeRotation(rotation);
    auto        dx        = offsetTowerCoords(x, y, z, wingSize, direction);
    if (rand.nextInt(6) == 0) return makeBridge(list, rand, index, x, y, z, wingSize, wingHeight, rotation);
    auto   wing      = std::make_unique<TowerWing>("twilightforest:TFLTWin", index, dx[0], dx[1], dx[2], wingSize, wingHeight, direction);
    Piece* intersect = findCollisionPiece(list, wing->bbox);
    if (!intersect || intersect == this) {
        auto* raw = wing.get();
        list.push_back(std::move(wing));
        raw->addChildren(*this, list, rand);
        addOpening(x, y, z, rotation);
        return true;
    }
    if (rand.nextInt(3) > 0) return makeBridge(list, rand, index, x, y, z, wingSize, wingHeight, rotation);
    return false;
}

bool TowerWing::makeBridge(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight, Rotation rotation) {
    Orientation direction = getStructureRelativeRotation(rotation);
    auto        dx        = offsetTowerCoords(x, y, z, 3, direction);
    if (wingSize == 3 && wingHeight > 10) wingHeight = 6 + rand.nextInt(5);
    auto   bridge    = std::make_unique<TowerBridge>(index, dx[0], dx[1], dx[2], wingSize, wingHeight, direction);
    Piece* intersect = findCollisionPiece(list, bridge->bbox);
    if (!intersect || intersect == this) intersect = findCollisionPiece(list, bridge->getWingBB());
    else return false;
    if (!intersect || intersect == this) {
        auto* raw = bridge.get();
        list.push_back(std::move(bridge));
        raw->addChildren(*this, list, rand);
        addOpening(x, y, z, rotation);
        return true;
    }
    return false;
}

void TowerWing::addOpening(int dx, int dy, int dz, Rotation direction) {
    openingTowards[static_cast<std::size_t>(rotationIndex(direction))] = true;
    if (dy > highestOpening) highestOpening = dy;
    openings.emplace_back(dx, dy, dz);
}

void TowerWing::addOpening(int dx, int dy, int dz, Orientation facing) {
    addOpening(dx, dy, dz, relativeRotation(getOrientation().value_or(Orientation::North), facing));
}

void TowerWing::makeABeard(Piece& parent, PieceList& list, RandomSource& rand) {
    bool                         attached = parent.bbox.minY < bbox.minY;
    int                          index    = genDepth;
    BlockPos                     loc      = getLocatorPosition();
    std::unique_ptr<TowerBeard> beard;
    if (attached) beard = std::make_unique<TowerBeardAttached>(index + 1, *this, loc.x, loc.y, loc.z);
    else beard = std::make_unique<TowerBeard>("twilightforest:TFLTBea", index + 1, *this, loc.x, loc.y, loc.z);
    auto* raw = beard.get();
    list.push_back(std::move(beard));
    raw->addChildren(*this, list, rand);
}

void TowerWing::makeARoof(Piece& parent, PieceList& list, RandomSource& rand) {
    bool attached = parent.bbox.maxY > bbox.maxY;
    if (attached) makeAttachedRoof(list, rand);
    else makeFreestandingRoof(list, rand);
}

void TowerWing::makeAttachedRoof(PieceList& list, RandomSource& rand) {
    int      index = genDepth;
    BlockPos loc   = getLocatorPosition();
    if (!roofType && rand.nextInt(32) != 0) tryToFitRoof(list, rand, std::make_unique<TowerRoofGableForwards>(index + 1, *this, loc.x, loc.y, loc.z));
    if (!roofType && rand.nextInt(8) != 0) tryToFitRoof(list, rand, std::make_unique<TowerRoofSlabForwards>(index + 1, *this, loc.x, loc.y, loc.z));
    if (!roofType && rand.nextInt(32) != 0) tryToFitRoof(list, rand, std::make_unique<TowerRoofAttachedSlab>(index + 1, *this, loc.x, loc.y, loc.z));
    if (!roofType) tryToFitRoof(list, rand, std::make_unique<TowerRoofFence>(index + 1, *this, loc.x, loc.y, loc.z));
}

void TowerWing::tryToFitRoof(PieceList& list, RandomSource& rand, std::unique_ptr<TowerRoof> roof) {
    if (!roof->fits(*this, list)) return;
    auto* raw = roof.get();
    list.push_back(std::move(roof));
    raw->addChildren(*this, list, rand);
    roofType = raw->typeId;
}

void TowerWing::makeFreestandingRoof(PieceList& list, RandomSource& rand) {
    int      index = genDepth;
    BlockPos loc   = getLocatorPosition();
    if (!roofType && rand.nextInt(8) != 0) tryToFitRoof(list, rand, std::make_unique<TowerRoofPointyOverhang>(index + 1, *this, loc.x, loc.y, loc.z));
    if (!roofType) tryToFitRoof(list, rand, std::make_unique<TowerRoofStairsOverhang>(index + 1, *this, loc.x, loc.y, loc.z));
    if (!roofType) tryToFitRoof(list, rand, std::make_unique<TowerRoofStairs>(index + 1, *this, loc.x, loc.y, loc.z));
    if (!roofType && rand.nextInt(53) != 0)
        tryToFitRoof(list, rand, std::make_unique<TowerRoofSlab>("twilightforest:TFLTRS", index + 1, *this, loc.x, loc.y, loc.z));
    if (!roofType) tryToFitRoof(list, rand, std::make_unique<TowerRoofFence>(index + 1, *this, loc.x, loc.y, loc.z));
}

void TowerWing::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    // make walls, clear inside
    randomFill(region, sbb, 0, 0, 0, size - 1, height - 1, size - 1, false, rand, getStrongholdStones());
    fillAir(region, sbb, 1, 1, 1, size - 2, height - 2, size - 2);
    if (highestOpening > 1) makeStairs(region, rand, sbb);
    decorateThisTower(region, sbb);
    makeWindows(region, sbb, size < 4);
    makeOpenings(region, sbb);
}

void TowerWing::makeOpeningMarkers(Region& region, RandomSource& rand, int numMarkers, BlockBox const& sbb) {
    if (size <= 4) return;
    char const* wools[] = {"minecraft:white_wool", "minecraft:orange_wool", "minecraft:magenta_wool", "minecraft:light_blue_wool"};
    for (int r = 0; r < 4; r++)
        for (int i = 0; i < numMarkers; i++) {
            auto spot = getValidOpening(rand, kRotations[static_cast<std::size_t>(r)]);
            placeBlock(region, B(wools[r]), spot[0], spot[1], spot[2], sbb);
        }
}

void TowerWing::decorateThisTower(Region& region, BlockBox const& sbb) {
    auto       seed = static_cast<std::uint64_t>(region.seed()) +
                static_cast<std::uint64_t>(static_cast<std::int64_t>(bbox.minX) * 321534781LL) *
                    static_cast<std::uint64_t>(static_cast<std::int64_t>(bbox.minZ) * 756839LL);
    JavaRandom decoRNG(static_cast<std::int64_t>(seed));
    if (size > 3) {
        if (isDeadEnd()) decorateDeadEnd(region, decoRNG, sbb);
        else decorateStairTower(region, decoRNG, sbb);
    }
}

void TowerWing::decorateDeadEnd(Region& region, RandomSource& rand, BlockBox const& sbb) {
    int floors      = (height - 1) / 5;
    int floorHeight = height / floors;
    for (int i = 1; i < floors; i++)
        for (int x = 1; x < size - 1; x++)
            for (int z = 1; z < size - 1; z++) placeBlock(region, birchPlanks(), x, i * floorHeight, z, sbb);

    if (floors > 1) {
        Rotation ladderDir = Rotation::Counterclockwise90;
        decorateFloor(region, rand, 0, 1, floorHeight, ladderDir, std::nullopt, sbb);
        for (int i = 1; i < floors - 1; i++) {
            int      bottom        = 1 + floorHeight * i;
            int      top           = floorHeight * (i + 1);
            Rotation downLadderDir = ladderDir;
            ladderDir              = addRotation(ladderDir, Rotation::Clockwise90);
            decorateFloor(region, rand, i, bottom, top, ladderDir, downLadderDir, sbb);
        }
        decorateFloor(region, rand, floors, 1 + floorHeight * (floors - 1), height - 1, std::nullopt, ladderDir, sbb);
    } else {
        decorateFloor(region, rand, 0, 1, height - 1, std::nullopt, std::nullopt, sbb);
    }
}

void TowerWing::decorateFloor(Region& region, RandomSource& rand, int, int bottom, int top, std::optional<Rotation> ladderUpDir,
                              std::optional<Rotation> ladderDownDir, BlockBox const& sbb) {
    if (ladderUpDir) {
        Block const& ladderUp = B("minecraft:ladder", Json{{"facing", blocks::directionName(rotateDir(Orientation::East, *ladderUpDir))}});
        int          dx       = getLadderX(*ladderUpDir);
        int          dz       = getLadderZ(*ladderUpDir);
        for (int dy = bottom; dy < top; dy++) placeBlock(region, ladderUp, dx, dy, dz, sbb);
    }
    if (ladderDownDir) {
        Block const& ladderDown = B("minecraft:ladder", Json{{"facing", blocks::directionName(rotateDir(Orientation::East, *ladderDownDir))}});
        int          dx         = getLadderX(*ladderDownDir);
        int          dz         = getLadderZ(*ladderDownDir);
        for (int dy = bottom - 1; dy < bottom + 2; dy++) placeBlock(region, ladderDown, dx, dy, dz, sbb);
    }
    Rotation up = orNull(ladderUpDir), down = orNull(ladderDownDir);
    if (rand.nextInt(7) == 0 && !ladderDownDir) decorateWell(region, rand, bottom, sbb);
    else if (rand.nextInt(7) == 0 && !ladderDownDir) decorateSkeletonRoom(region, rand, bottom, top, up, down, sbb);
    else if (rand.nextInt(6) == 0 && !ladderDownDir) decorateZombieRoom(region, rand, bottom, up, down, sbb);
    else if (rand.nextInt(5) == 0 && !ladderDownDir) decorateCactusRoom(region, rand, bottom, top, up, down, sbb);
    else if (rand.nextInt(4) == 0 && ladderDownDir) decorateTreasureChest(region, bottom, top, sbb);
    else if (rand.nextInt(5) == 0) decorateSpiderWebs(region, rand, bottom, top, up, down, sbb);
    else if (rand.nextInt(12) == 0 && ladderDownDir) decorateSolidRock(region, rand, bottom, top, up, down, sbb);
    else if (rand.nextInt(3) == 0) decorateFullLibrary(region, rand, bottom, top, up, down, sbb);
    else decorateLibrary(region, rand, bottom, top, up, down, sbb);
}

void TowerWing::decorateWell(Region& region, RandomSource& rand, int bottom, BlockBox const& sbb) {
    int          cx          = size / 2;
    Block const& waterOrLava = rand.nextInt(4) == 0 ? B("minecraft:lava") : B("minecraft:water");
    if (size > 5) {
        Block const& stoneBricks = B("minecraft:stone_bricks");
        Block const& slabs       = stoneSlab();
        placeBlock(region, stoneBricks, cx - 1, bottom, cx - 1, sbb);
        placeBlock(region, slabs, cx - 1, bottom + 1, cx - 1, sbb);
        placeBlock(region, stoneBricks, cx, bottom, cx - 1, sbb);
        placeBlock(region, stoneBricks, cx + 1, bottom, cx - 1, sbb);
        placeBlock(region, slabs, cx + 1, bottom + 1, cx - 1, sbb);
        placeBlock(region, stoneBricks, cx - 1, bottom, cx, sbb);
        placeBlock(region, waterOrLava, cx, bottom, cx, sbb);
        placeBlock(region, stoneBricks, cx + 1, bottom, cx, sbb);
        placeBlock(region, stoneBricks, cx - 1, bottom, cx + 1, sbb);
        placeBlock(region, slabs, cx - 1, bottom + 1, cx + 1, sbb);
        placeBlock(region, stoneBricks, cx, bottom, cx + 1, sbb);
        placeBlock(region, stoneBricks, cx + 1, bottom, cx + 1, sbb);
        placeBlock(region, slabs, cx + 1, bottom + 1, cx + 1, sbb);
    }
    placeBlock(region, waterOrLava, cx, bottom - 1, cx, sbb);
}

void TowerWing::decorateSkeletonRoom(Region& region, RandomSource& rand, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir,
                                     BlockBox const& sbb) {
    setSpawner(region, size / 2, bottom + 2, size / 2, sbb, "minecraft:skeleton");
    std::vector<BlockPos> chainList{BlockPos{size / 2, bottom + 2, size / 2}};
    for (int i = 0; i < size + 2; i++) {
        int      cx = 2 + rand.nextInt(size - 4);
        int      cz = 2 + rand.nextInt(size - 4);
        BlockPos chain{cx, height - 2, cz};
        if (!chainCollides(chain, chainList)) {
            for (int dy = bottom; dy < top; dy++) placeBlock(region, B("minecraft:iron_bars"), chain.x, dy, chain.z, sbb);
            chainList.push_back(chain);
        }
    }
    for (int dx = 1; dx <= size - 2; dx++)
        for (int dz = 1; dz <= size - 2; dz++)
            if ((dx == 1 || dx == size - 2 || dz == 1 || dz == size - 2) && !isWindowPos(dx, dz) && !isLadderPos(dx, dz, ladderUpDir, ladderDownDir))
                placeBlock(region, B("minecraft:cobweb"), dx, top - 1, dz, sbb);
}

void TowerWing::decorateZombieRoom(Region& region, RandomSource& rand, int bottom, Rotation ladderUpDir, Rotation ladderDownDir, BlockBox const& sbb) {
    setSpawner(region, size / 2, bottom + 2, size / 2, sbb, "minecraft:zombie");
    for (int dx = 1; dx <= size - 2; dx++)
        for (int dz = 1; dz <= size - 2; dz++)
            if (!isWindowPos(dx, dz) && !isLadderPos(dx, dz, ladderUpDir, ladderDownDir))
                if (rand.nextInt(5) == 0) placeBlock(region, B("minecraft:brown_mushroom"), dx, bottom, dz, sbb);
    std::vector<BlockPos> slabList{BlockPos{size / 2, bottom + 2, size / 2}};
    for (int i = 0; i < size - 1; i++) {
        int      sx = 2 + rand.nextInt(size - 4);
        int      sz = 2 + rand.nextInt(size - 4);
        BlockPos slab{sx, height - 2, sz};
        if (!chainCollides(slab, slabList)) {
            placeBlock(region, B("minecraft:iron_bars"), slab.x, bottom, slab.z, sbb);
            placeBlock(region, birchSlab(), slab.x, bottom + 1, slab.z, sbb);
            placeBlock(region, B("minecraft:soul_sand"), slab.x, bottom + 2, slab.z, sbb);
            slabList.push_back(slab);
        }
    }
}

void TowerWing::decorateCactusRoom(Region& region, RandomSource& rand, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir, BlockBox const& sbb) {
    for (int dx = 1; dx <= size - 2; dx++)
        for (int dz = 1; dz <= size - 2; dz++) {
            placeBlock(region, B("minecraft:sand"), dx, bottom - 1, dz, sbb);
            if (!isWindowPos(dx, dz) && !isLadderPos(dx, dz, ladderUpDir, ladderDownDir))
                if (rand.nextInt(4) == 0) placeBlock(region, B("minecraft:dead_bush"), dx, bottom, dz, sbb);
        }
    std::vector<BlockPos> cactusList{BlockPos{size / 2, bottom + 2, size / 2}};
    for (int i = 0; i < size + 12; i++) {
        int      cx = 2 + rand.nextInt(size - 4);
        int      cz = 2 + rand.nextInt(size - 4);
        BlockPos cactus{cx, height - 2, cz};
        if (!chainCollides(cactus, cactusList)) {
            for (int dy = bottom; dy < top; dy++) placeBlock(region, B("minecraft:cactus"), cactus.x, dy, cactus.z, sbb);
            cactusList.push_back(cactus);
        }
    }
}

void TowerWing::decorateTreasureChest(Region& region, int bottom, int top, BlockBox const& sbb) {
    int          cx               = size / 2;
    Block const& stoneBrick       = B("minecraft:stone_bricks");
    Block const& stoneBrickStairs = B("minecraft:stone_brick_stairs");
    Block const& topStairs        = B("minecraft:stone_brick_stairs", Json{{"half", "top"}});
    placeBlock(region, stoneBrick, cx, bottom, cx, sbb);
    placeBlock(region, stoneBrick, cx, top - 1, cx, sbb);
    if (size < 6) {
        surroundBlockCardinalRotated(region, stoneBrickStairs, cx, bottom, cx, sbb);
        surroundBlockCardinalRotated(region, topStairs, cx, top - 1, cx, sbb);
    } else {
        surroundBlockCardinalRotated(region, stoneBrickStairs, cx, bottom, cx, sbb);
        surroundBlockCorners(region, stoneBrick, cx, bottom, cx, sbb);
        for (int cy = bottom + 1; cy < top - 1; cy++) surroundBlockCorners(region, stoneBrick, cx, cy, cx, sbb);
        surroundBlockCardinalRotated(region, topStairs, cx, top - 1, cx, sbb);
        surroundBlockCorners(region, stoneBrick, cx, top - 1, cx, sbb);
    }
    placeTreasureAtCurrentPosition(region, cx, bottom + 1, cx, "twilightforest:chests/tower_room", sbb);
}

void TowerWing::decorateSpiderWebs(Region& region, RandomSource& rand, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir, BlockBox const& sbb) {
    for (int dy = bottom; dy < top; dy++) {
        int chance = top - dy + 2;
        for (int dx = 1; dx <= size - 2; dx++)
            for (int dz = 1; dz <= size - 2; dz++)
                if (!isLadderPos(dx, dz, ladderUpDir, ladderDownDir) && rand.nextInt(chance) == 0) placeBlock(region, B("minecraft:cobweb"), dx, dy, dz, sbb);
    }
    if (rand.nextInt(5) == 0) {
        char const* spider = "minecraft:spider";
        switch (rand.nextInt(4)) {
        case 3:
            spider = "minecraft:cave_spider";
            break;
        case 2:
            spider = "twilightforest:swarm_spider";
            break;
        case 1:
            spider = "twilightforest:hedge_spider";
            break;
        default:
            break;
        }
        setSpawner(region, size / 2, bottom + 2, size / 2, sbb, spider);
    } else {
        decorateFurniture(region, rand, bottom, size - 2, sbb);
    }
}

void TowerWing::decorateFurniture(Region& region, RandomSource& rand, int bottom, int freeSpace, BlockBox const& sbb) {
    if (rand.nextInt(3) > 0) {
        placeBlock(region, B("minecraft:oak_fence"), size / 2, bottom, size / 2, sbb);
        placeBlock(region, B("minecraft:oak_pressure_plate"), size / 2, bottom + 1, size / 2, sbb);
    }
    auto chair = [&](char const* facing) -> Block const& { return B("minecraft:spruce_stairs", Json{{"facing", facing}}); };
    if (rand.nextInt(3) == 0 && freeSpace > 1) placeBlock(region, chair("west"), size / 2 + 1, bottom, size / 2, sbb);
    if (rand.nextInt(3) == 0 && freeSpace > 1) placeBlock(region, chair("north"), size / 2, bottom, size / 2 + 1, sbb);
    if (rand.nextInt(3) == 0 && freeSpace > 1) placeBlock(region, chair("east"), size / 2 - 1, bottom, size / 2, sbb);
    if (rand.nextInt(3) == 0 && freeSpace > 1) placeBlock(region, chair("south"), size / 2, bottom, size / 2 - 1, sbb);
}

void TowerWing::decorateSolidRock(Region& region, RandomSource& rand, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir, BlockBox const& sbb) {
    for (int dy = bottom; dy < top; dy++)
        for (int dx = 1; dx <= size - 2; dx++)
            for (int dz = 1; dz <= size - 2; dz++)
                if (!isLadderPos(dx, dz, ladderUpDir, ladderDownDir) && rand.nextInt(9) != 0) placeBlock(region, B("minecraft:stone"), dx, dy, dz, sbb);
}

void TowerWing::decorateLibrary(Region& region, RandomSource& rand, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir, BlockBox const& sbb) {
    for (int dx = 1; dx <= size - 2; dx++)
        for (int dz = 1; dz <= size - 2; dz++)
            for (int dy = bottom; dy < top - 1; dy++)
                if ((dx == 1 || dx == size - 2 || dz == 1 || dz == size - 2) && !isWindowPos(dx, dz) && !isLadderPos(dx, dz, ladderUpDir, ladderDownDir))
                    placeBlock(region, B("minecraft:bookshelf"), dx, dy, dz, sbb);
    if (rand.nextInt(2) == 0 && size > 5) decorateLibraryTreasure(region, rand, top, ladderUpDir, ladderDownDir, sbb);
    if (rand.nextInt(2) == 0 && size > 5) decorateFurniture(region, rand, bottom, size - 2, sbb);
}

void TowerWing::decorateLibraryTreasure(Region& region, RandomSource& rand, int top, Rotation ladderUpDir, Rotation ladderDownDir, BlockBox const& sbb) {
    constexpr char const* kTable = "twilightforest:chests/tower_library";
    // TF's switch falls through to the next case when the spot holds a ladder
    switch (rand.nextInt(4)) {
    case 0:
    default:
        if (!isLadderPos(2, 1, ladderUpDir, ladderDownDir)) {
            placeTreasureAtCurrentPosition(region, 2, top - 2, 1, kTable, sbb);
            break;
        }
        [[fallthrough]];
    case 1:
        if (!isLadderPos(size - 2, 2, ladderUpDir, ladderDownDir)) {
            placeTreasureAtCurrentPosition(region, size - 2, top - 2, 2, kTable, sbb);
            break;
        }
        [[fallthrough]];
    case 2:
        if (!isLadderPos(size - 3, size - 2, ladderUpDir, ladderDownDir)) {
            placeTreasureAtCurrentPosition(region, size - 3, top - 2, size - 2, kTable, sbb);
            break;
        }
        [[fallthrough]];
    case 3:
        if (!isLadderPos(1, size - 3, ladderUpDir, ladderDownDir)) {
            placeTreasureAtCurrentPosition(region, 1, top - 2, size - 3, kTable, sbb);
            break;
        }
    }
}

void TowerWing::decorateFullLibrary(Region& region, RandomSource& rand, int bottom, int top, Rotation ladderUpDir, Rotation ladderDownDir, BlockBox const& sbb) {
    for (int dx = 1; dx <= size - 2; dx++)
        for (int dz = 1; dz <= size - 2; dz++)
            for (int dy = bottom; dy < top; dy++) {
                bool ring = (dx % 2 != 0 && ((dz >= dx && dz <= size - dx - 1) || (dz >= size - dx - 1 && dz <= dx))) ||
                            (dz % 2 != 0 && ((dx >= dz && dx <= size - dz - 1) || (dx >= size - dz - 1 && dx <= dz)));
                if (ring && !isWindowPos(dx, dy, dz) && !isOpeningPos(dx, dy, dz) && !isLadderPos(dx, dz, ladderUpDir, ladderDownDir))
                    placeBlock(region, B("minecraft:bookshelf"), dx, dy, dz, sbb);
            }
    if (rand.nextInt(2) == 0 && size > 5) decorateLibraryTreasure(region, rand, top, ladderUpDir, ladderDownDir, sbb);
}

void TowerWing::decorateTrap(Region& region, int bottom, int top, BlockBox const& sbb) {
    for (int dx = 2; dx <= size - 3; dx++)
        for (int dz = 2; dz <= size - 3; dz++)
            if (dx == 2 || dx == size - 3 || dz == 2 || dz == size - 3) placeBlock(region, B("minecraft:tnt"), dx, -1, dz, sbb);
    for (int dy = bottom - 2; dy < top - 2; dy++) {
        placeBlock(region, B("minecraft:tnt"), 1, dy, 1, sbb);
        placeBlock(region, B("minecraft:tnt"), 1, dy, size - 2, sbb);
        placeBlock(region, B("minecraft:tnt"), size - 2, dy, 1, sbb);
        placeBlock(region, B("minecraft:tnt"), size - 2, dy, size - 2, sbb);
    }
}

bool TowerWing::isWindowPos(int x, int z) const {
    if (x == 1 && z == size / 2) return true;
    if (x == size - 2 && z == size / 2) return true;
    if (x == size / 2 && z == 1) return true;
    return x == size / 2 && z == size - 2;
}

bool TowerWing::isWindowPos(int x, int y, int z) const {
    int checkYDir = -1;
    if (x == 1 && z == size / 2) checkYDir = 2;
    else if (x == size - 2 && z == size / 2) checkYDir = 0;
    else if (x == size / 2 && z == 1) checkYDir = 3;
    else if (x == size / 2 && z == size - 2) checkYDir = 1;
    if (checkYDir > -1)
        return !openingTowards[static_cast<std::size_t>(checkYDir)] && (y == 2 || y == 3 || (height > 8 && (y == height - 3 || y == height - 4)));
    return false;
}

bool TowerWing::isOpeningPos(int x, int y, int z) const {
    for (auto const& door : openings) {
        BlockPos inside = door;
        if (inside.x == 0) inside.x++;
        else if (inside.x == size - 1) inside.x--;
        else if (inside.z == 0) inside.z++;
        else if (inside.z == size - 1) inside.z--;
        if (inside.x == x && inside.z == z && (inside.y == y || inside.y + 1 == y)) return true;
    }
    return false;
}

bool TowerWing::isLadderPos(int x, int z, Rotation ladderUpDir, Rotation ladderDownDir) const {
    if (!isNull(ladderUpDir) && x == getLadderX(ladderUpDir) && z == getLadderZ(ladderUpDir)) return true;
    return !isNull(ladderDownDir) && x == getLadderX(ladderDownDir) && z == getLadderZ(ladderDownDir);
}

int TowerWing::getLadderX(Rotation ladderDir) const {
    switch (ladderDir) {
    case Rotation::None:
        return size - 2;
    case Rotation::Clockwise90:
        return size / 2 + 1;
    case Rotation::Clockwise180:
        return 1;
    case Rotation::Counterclockwise90:
        return size / 2 - 1;
    }
    return size / 2;
}

int TowerWing::getLadderZ(Rotation ladderDir) const {
    switch (ladderDir) {
    case Rotation::None:
        return size / 2 - 1;
    case Rotation::Clockwise90:
        return size - 2;
    case Rotation::Clockwise180:
        return size / 2 + 1;
    case Rotation::Counterclockwise90:
        return 1;
    }
    return size / 2;
}

void TowerWing::decorateStairTower(Region& region, RandomSource& rand, BlockBox const& sbb) {
    if (height - highestOpening > 8) {
        int base        = highestOpening + 3;
        int floors      = (height - base) / 5;
        int floorHeight = (height - base) / floors;
        for (int i = 0; i < floors; i++)
            for (int x = 1; x < size - 1; x++)
                for (int z = 1; z < size - 1; z++) placeBlock(region, birchPlanks(), x, i * floorHeight + base, z, sbb);

        Rotation     ladderDir = Rotation::None;
        int          dx        = getLadderX(ladderDir);
        int          dz        = getLadderZ(ladderDir);
        Block const& ladder    = B("minecraft:ladder", Json{{"facing", blocks::directionName(rotateDir(Orientation::East, ladderDir))}});
        for (int dy = 1; dy < 3; dy++) placeBlock(region, ladder, dx, base - dy, dz, sbb);

        for (int i = 0; i < floors - 1; i++) {
            int      bottom        = base + 1 + floorHeight * i;
            int      top           = base + floorHeight * (i + 1);
            Rotation downLadderDir = ladderDir;
            ladderDir              = addRotation(ladderDir, Rotation::Clockwise90);
            decorateFloor(region, rand, i, bottom, top, ladderDir, downLadderDir, sbb);
        }
        decorateFloor(region, rand, floors, base + 1 + floorHeight * (floors - 1), height - 1, std::nullopt, ladderDir, sbb);

        if (base > 8) {
            switch (rand.nextInt(4)) {
            case 0:
                decorateChandelier(region, rand, base + 1, sbb);
                break;
            case 1:
                decorateHangingChains(region, rand, base + 1, sbb);
                break;
            case 2:
                decorateFloatingBooks(region, rand, base + 1, sbb);
                break;
            case 3:
                decorateFloatingVines(region, rand, base + 1, sbb);
                break;
            }
        }
    } else {
        if (size > 5) {
            switch (rand.nextInt(4)) {
            case 0:
                decorateChandelier(region, rand, height, sbb);
                break;
            case 1:
                decorateHangingChains(region, rand, height, sbb);
                break;
            case 2:
                decorateFloatingBooks(region, rand, height, sbb);
                break;
            case 3:
                decorateFloatingVines(region, rand, height, sbb);
                break;
            }
        } else if (size > 3) {
            switch (rand.nextInt(3)) {
            case 0:
                decorateHangingChains(region, rand, height, sbb);
                break;
            case 1:
                decorateFloatingBooks(region, rand, height, sbb);
                break;
            case 2:
                decorateFloatingVines(region, rand, height, sbb);
                break;
            }
        }
    }
    decorateStairFloor(region, rand, sbb);
}

void TowerWing::decorateStairFloor(Region& region, RandomSource& rand, BlockBox const& sbb) {
    if (size > 5) {
        if (rand.nextInt(3) == 0) decorateStairWell(region, rand, sbb);
        else if (rand.nextInt(3) > 0 || size >= 15) decoratePlanter(region, rand, sbb);
    }
}

void TowerWing::decorateChandelier(Region& region, RandomSource& rand, int decoTop, BlockBox const& sbb) {
    if (decoTop < 8 || size < 8) return;
    int          cx    = size / 2;
    int          cy    = decoTop - rand.nextInt(decoTop - 7) - 2;
    int          cz    = size / 2;
    Block const& fence = B("minecraft:oak_fence");
    surroundBlockCardinal(region, fence, cx, cy, cz, sbb);
    surroundBlockCardinal(region, fence, cx, cy + 1, cz, sbb);
    for (int y = cy; y < decoTop - 1; y++) placeBlock(region, fence, cx, y, cz, sbb);
}

void TowerWing::decorateHangingChains(Region& region, RandomSource& rand, int decoTop, BlockBox const& sbb) {
    std::vector<BlockPos> chainList;
    for (int i = 0; i < size + 2; i++) {
        int      filled = size < 15 ? 2 : 4;
        int      cx     = filled + rand.nextInt(size - filled * 2);
        int      cz     = filled + rand.nextInt(size - filled * 2);
        BlockPos chain{cx, decoTop - 2, cz};
        if (!chainCollides(chain, chainList)) {
            int length = 1 + rand.nextInt(decoTop - 7);
            decorateOneChain(region, rand, chain.x, decoTop, length, chain.z, sbb);
            chainList.push_back(chain);
        }
    }
}

bool TowerWing::chainCollides(BlockPos const& coords, std::vector<BlockPos> const& list) const {
    for (auto const& existing : list) {
        if (coords.z == existing.z && std::abs(coords.x - existing.x) <= 1) return true;
        if (coords.x == existing.x && std::abs(coords.z - existing.z) <= 1) return true;
    }
    return false;
}

void TowerWing::decorateOneChain(Region& region, RandomSource& rand, int dx, int decoTop, int length, int dz, BlockBox const& sbb) {
    for (int y = 1; y <= length; y++) placeBlock(region, B("minecraft:iron_bars"), dx, decoTop - y - 1, dz, sbb);
    char const* ball = "minecraft:glowstone";
    switch (rand.nextInt(10)) {
    case 0:
        ball = "minecraft:iron_block";
        break;
    case 1:
        ball = "minecraft:bookshelf";
        break;
    case 2:
        ball = "minecraft:netherrack";
        break;
    case 3:
        ball = "minecraft:soul_sand";
        break;
    case 4:
        ball = "minecraft:glass";
        break;
    case 5:
        ball = "minecraft:lapis_block";
        break;
    case 6:
        ball = "minecraft:infested_stone_bricks";
        break;
    default:
        break;
    }
    placeBlock(region, B(ball), dx, decoTop - length - 2, dz, sbb);
}

void TowerWing::decorateFloatingBooks(Region& region, RandomSource& rand, int decoTop, BlockBox const& sbb) {
    std::vector<BlockPos> shelfList;
    for (int i = 0; i < size + 2; i++) {
        int      filled = size < 15 ? 2 : 4;
        int      sx     = filled + rand.nextInt(size - filled * 2);
        int      sz     = filled + rand.nextInt(size - filled * 2);
        BlockPos shelf{sx, decoTop - 2, sz};
        if (!chainCollides(shelf, shelfList)) {
            int bottom = 2 + rand.nextInt(decoTop - 7);
            int top    = rand.nextInt(bottom - 1) + 2;
            for (int y = top; y <= bottom; y++) placeBlock(region, B("minecraft:bookshelf"), shelf.x, decoTop - y, shelf.z, sbb);
            shelfList.push_back(shelf);
        }
    }
}

void TowerWing::decorateFloatingVines(Region& region, RandomSource& rand, int decoTop, BlockBox const& sbb) {
    Block const& mossy     = B("minecraft:mossy_cobblestone");
    Block const& vineNorth = B("minecraft:vine", Json{{"north", true}});
    Block const& vineSouth = B("minecraft:vine", Json{{"south", true}});
    Block const& vineEast  = B("minecraft:vine", Json{{"east", true}});
    Block const& vineWest  = B("minecraft:vine", Json{{"west", true}});
    std::vector<BlockPos> mossList;
    for (int i = 0; i < size + 2; i++) {
        int      filled = size < 15 ? 2 : 4;
        int      mx     = filled + rand.nextInt(size - filled * 2);
        int      mz     = filled + rand.nextInt(size - filled * 2);
        BlockPos moss{mx, decoTop - 2, mz};
        if (!chainCollides(moss, mossList)) {
            int bottom = 2 + rand.nextInt(decoTop - 7);
            int top    = rand.nextInt(bottom - 1) + 2;
            for (int y = top; y <= bottom; y++) {
                placeBlock(region, mossy, moss.x, decoTop - y, moss.z, sbb);
                placeBlock(region, vineEast, moss.x + 1, decoTop - y, moss.z, sbb);
                placeBlock(region, vineWest, moss.x - 1, decoTop - y, moss.z, sbb);
                placeBlock(region, vineSouth, moss.x, decoTop - y, moss.z + 1, sbb);
                placeBlock(region, vineNorth, moss.x, decoTop - y, moss.z - 1, sbb);
            }
            mossList.push_back(moss);
        }
    }
    for (int y = highestOpening + 3; y < decoTop - 1; y++) {
        for (int x = 1; x < size - 1; x++) {
            if (rand.nextInt(3) == 0) placeBlock(region, vineSouth, x, y, 1, sbb);
            if (rand.nextInt(3) == 0) placeBlock(region, vineNorth, x, y, size - 2, sbb);
        }
        for (int z = 1; z < size - 1; z++) {
            if (rand.nextInt(3) == 0) placeBlock(region, vineEast, 1, y, z, sbb);
            if (rand.nextInt(3) == 0) placeBlock(region, vineWest, size - 2, y, z, sbb);
        }
    }
}

void TowerWing::decoratePlanter(Region& region, RandomSource& rand, BlockBox const& sbb) {
    int cx = size / 2;
    surroundBlockCardinal(region, stoneSlab(), cx, 1, cx, sbb);
    if (size > 7) surroundBlockCorners(region, stoneSlabDouble(), cx, 1, cx, sbb);
    placeBlock(region, B("minecraft:grass_block"), cx, 1, cx, sbb);
    int          i     = rand.nextInt(6);
    Block const& plant = randomPlant(i);
    BlockPos     pos   = getBlockPosWithOffset(cx, 2, cx);
    // TF tries a vanilla tree for the brown mushroom pick (world.getRandom(): the region exposes none,
    // so the piece's random is used); either way the plant is placed, as written in TF
    if (i > 4) (void)features::place(randomTree(rand.nextInt(4)), region, pos, rand);
    placeBlock(region, plant, cx, 2, cx, sbb);
}

void TowerWing::decorateStairWell(Region& region, RandomSource& rand, BlockBox const& sbb) {
    int          cx          = size / 2;
    int          cy          = 1;
    Block const& waterOrLava = rand.nextInt(4) == 0 ? B("minecraft:lava") : B("minecraft:water");
    Block const& slab        = B("minecraft:smooth_stone_slab");
    Block const& brick       = B("minecraft:stone_bricks");
    if (size > 7) {
        placeBlock(region, brick, cx - 1, cy, cx - 1, sbb);
        placeBlock(region, slab, cx - 1, cy + 1, cx - 1, sbb);
        placeBlock(region, brick, cx, cy, cx - 1, sbb);
        placeBlock(region, brick, cx + 1, cy, cx - 1, sbb);
        placeBlock(region, slab, cx + 1, cy + 1, cx - 1, sbb);
        placeBlock(region, brick, cx - 1, cy, cx, sbb);
        placeBlock(region, waterOrLava, cx, cy, cx, sbb);
        placeBlock(region, brick, cx + 1, cy, cx, sbb);
        placeBlock(region, brick, cx - 1, cy, cx + 1, sbb);
        placeBlock(region, slab, cx - 1, cy + 1, cx + 1, sbb);
        placeBlock(region, brick, cx, cy, cx + 1, sbb);
        placeBlock(region, brick, cx + 1, cy, cx + 1, sbb);
        placeBlock(region, slab, cx + 1, cy + 1, cx + 1, sbb);
    }
    placeBlock(region, waterOrLava, cx, 0, cx, sbb);
}

bool TowerWing::isDeadEnd() const { return openings.size() == 1; }

void TowerWing::makeOpenings(Region& region, BlockBox const& sbb) {
    for (auto const& door : openings) makeDoorOpening(region, door.x, door.y, door.z, sbb);
}

void TowerWing::makeDoorOpening(Region& region, int dx, int dy, int dz, BlockBox const& sbb) {
    placeBlock(region, air(), dx, dy, dz, sbb);
    placeBlock(region, air(), dx, dy + 1, dz, sbb);
    if (!getBlock(region, dx, dy + 2, dz, sbb).isAir()) placeBlock(region, stoneSlabDouble(), dx, dy + 2, dz, sbb);
}

std::array<int, 3> TowerWing::getValidOpening(RandomSource& rand, Rotation direction) {
    int wLength = size - 2;
    int offset  = 1;
    if (size == 15) {
        wLength = 11;
        offset  = 2;
    }
    if (direction == Rotation::None || direction == Rotation::Clockwise180) {
        int rx = direction == Rotation::None ? size - 1 : 0;
        int rz = offset + rand.nextInt(wLength);
        int ry = getYByStairs(rz, rand, direction);
        return {rx, ry, rz};
    }
    if (direction == Rotation::Clockwise90 || direction == Rotation::Counterclockwise90) {
        int rx = offset + rand.nextInt(wLength);
        int rz = direction == Rotation::Clockwise90 ? size - 1 : 0;
        int ry = getYByStairs(rx, rand, direction);
        return {rx, ry, rz};
    }
    return {0, 0, 0};
}

int TowerWing::getYByStairs(int rx, RandomSource& rand, Rotation direction) {
    int  rise = 1, base = 0;
    bool zAxis = direction == Rotation::None || direction == Rotation::Clockwise180;
    if (size == 15) {
        rise = 10;
        base = zAxis ? 23 : 28;
    }
    if (size == 9) {
        rise = 6;
        base = zAxis ? 2 : 5;
    }
    if (size == 7) {
        rise = 4;
        base = zAxis ? 2 : 4;
    }
    if (size == 5) {
        rise = 4;
        switch (direction) {
        case Rotation::None:
            base = 3;
            break;
        case Rotation::Clockwise90:
            base = 2;
            break;
        case Rotation::Clockwise180:
            base = 5;
            break;
        case Rotation::Counterclockwise90:
            base = 4;
            break;
        }
    }
    int flights = ((height - 6 - base) / rise) + 1;
    if (base > 0 && flights > 0) {
        int flightChosen = rand.nextInt(flights);
        int dy           = flightChosen * rise + base;
        bool ascending   = direction == Rotation::None || direction == Rotation::Counterclockwise90;
        if (size == 15) dy -= ascending ? (rx - 2) / 2 : (size - rx - 3) / 2;
        else dy -= ascending ? (rx - 1) / 2 : (size - rx - 2) / 2;
        if (dy < 1) dy = 1;
        return dy;
    }
    return 0;
}

void TowerWing::makeWindows(Region& region, BlockBox const& sbb, bool real) {
    for (auto rotation : kRotations) {
        bool realWindows = real && !openingTowards[static_cast<std::size_t>(rotationIndex(rotation))];
        makeWindowBlock(region, size - 1, 2, size / 2, rotation, sbb, realWindows);
        makeWindowBlock(region, size - 1, 3, size / 2, rotation, sbb, realWindows);
        makeWindowBase(region, size - 1, 1, size / 2, rotation, sbb);
        if (height > 8) {
            makeWindowBlock(region, size - 1, height - 3, size / 2, rotation, sbb, realWindows);
            makeWindowBlock(region, size - 1, height - 4, size / 2, rotation, sbb, realWindows);
            makeWindowBase(region, size - 1, height - 5, size / 2, rotation, sbb);
        }
    }
}

void TowerWing::makeWindowBlock(Region& region, int x, int y, int z, Rotation rotation, BlockBox const& sbb, bool realWindows) {
    FakeOrientation fake(*this, turned(getOrientation(), rotation));
    Block const&    outside = getBlock(region, x + 1, y, z, sbb);
    Block const&    inside  = getBlock(region, x - 1, y, z, sbb);
    if (realWindows && inside.isAir() && outside.isAir()) placeBlock(region, B("minecraft:glass_pane"), x, y, z, sbb);
    else placeBlock(region, B("minecraft:cobblestone"), x, y, z, sbb);
}

void TowerWing::makeWindowBase(Region& region, int x, int y, int z, Rotation rotation, BlockBox const& sbb) {
    FakeOrientation fake(*this, turned(getOrientation(), rotation));
    placeBlock(region, stoneSlabDouble(), x, y, z, sbb);
}

bool TowerWing::makeStairs(Region& region, RandomSource& rand, BlockBox const& sbb) {
    if (size == 15) return makeStairs15(region, rand, sbb);
    if (size == 9) return makeStairs9(region, rand, sbb);
    if (size == 7) return makeStairs7(region, rand, sbb);
    if (size == 5) return makeStairs5(region, rand, sbb);
    return false;
}

bool TowerWing::makeStairs5(Region& region, RandomSource&, BlockBox const& sbb) {
    int rise       = 1;
    int numFlights = highestOpening / rise;
    for (int i = 0; i < numFlights; i++) makeStairs5flight(region, sbb, i * rise, rotationPlus(Rotation::None, i * 3), true);
    return true;
}

void TowerWing::makeStairs5flight(Region& region, BlockBox const& sbb, int h, Rotation rotation, bool useBirchWood) {
    FakeOrientation fake(*this, turned(getOrientation(), rotation));
    Block const&    bottomSlab = useBirchWood ? birchSlab() : stoneSlab();
    Block const&    topSlab    = useBirchWood ? birchSlabTop() : stoneSlabTop();
    placeBlock(region, bottomSlab, 2, 1 + h, 3, sbb);
    placeBlock(region, topSlab, 3, 1 + h, 3, sbb);
}

bool TowerWing::makeStairs7(Region& region, RandomSource&, BlockBox const& sbb) {
    placeBlock(region, birchSlab(), 1, 1, 4, sbb);
    placeBlock(region, birchSlabTop(), 1, 1, 5, sbb);
    placeBlock(region, stoneSlab(), 5, 1, 2, sbb);
    placeBlock(region, stoneSlabTop(), 5, 1, 1, sbb);
    int rise       = 2;
    int numFlights = highestOpening / rise;
    for (int i = 0; i < numFlights; i++) {
        makeStairs7flight(region, sbb, 1 + i * rise, rotationPlus(Rotation::None, i * 3), true);
        makeStairs7flight(region, sbb, 1 + i * rise, rotationPlus(Rotation::Clockwise180, i * 3), false);
    }
    return true;
}

void TowerWing::makeStairs7flight(Region& region, BlockBox const& sbb, int h, Rotation rotation, bool useBirchWood) {
    FakeOrientation fake(*this, turned(getOrientation(), rotation));
    Block const&    slabBottom = useBirchWood ? birchSlab() : stoneSlab();
    Block const&    slabTop    = useBirchWood ? birchSlabTop() : stoneSlabTop();
    placeBlock(region, slabBottom, 2, 1 + h, 5, sbb);
    placeBlock(region, slabTop, 3, 1 + h, 5, sbb);
    placeBlock(region, slabBottom, 4, 2 + h, 5, sbb);
    placeBlock(region, slabTop, 5, 2 + h, 5, sbb);
}

bool TowerWing::makeStairs9(Region& region, RandomSource&, BlockBox const& sbb) {
    placeBlock(region, birchSlab(), 1, 1, 6, sbb);
    placeBlock(region, birchSlabTop(), 1, 1, 7, sbb);
    placeBlock(region, stoneSlab(), 7, 1, 2, sbb);
    placeBlock(region, stoneSlabTop(), 7, 1, 1, sbb);
    int rise       = 3;
    int numFlights = highestOpening / rise;
    for (int i = 0; i < numFlights; i++) {
        makeStairs9flight(region, sbb, 1 + i * rise, rotationPlus(Rotation::None, i * 3), true);
        makeStairs9flight(region, sbb, 1 + i * rise, rotationPlus(Rotation::Clockwise180, i * 3), false);
    }
    return true;
}

void TowerWing::makeStairs9flight(Region& region, BlockBox const& sbb, int h, Rotation rotation, bool useBirchWood) {
    FakeOrientation fake(*this, turned(getOrientation(), rotation));
    Block const&    slabBot = useBirchWood ? birchSlab() : stoneSlab();
    Block const&    slabTop = useBirchWood ? birchSlabTop() : stoneSlabTop();
    placeBlock(region, slabBot, 2, 1 + h, 7, sbb);
    placeBlock(region, slabTop, 3, 1 + h, 7, sbb);
    placeBlock(region, slabBot, 4, 2 + h, 7, sbb);
    placeBlock(region, slabTop, 5, 2 + h, 7, sbb);
    placeBlock(region, slabBot, 6, 3 + h, 7, sbb);
    placeBlock(region, slabTop, 7, 3 + h, 7, sbb);
}

bool TowerWing::makeStairs15(Region& region, RandomSource& rand, BlockBox const& sbb) {
    Block const& planks = birchPlanks();
    Block const& fence  = B("minecraft:oak_fence");
    Block const& bSlab  = birchSlab();
    Block const& sSlab  = stoneSlab();
    Block const& dSlab  = stoneSlabDouble();
    placeBlock(region, bSlab, 1, 1, 9, sbb);
    placeBlock(region, bSlab, 2, 1, 9, sbb);
    placeBlock(region, planks, 1, 1, 10, sbb);
    placeBlock(region, planks, 2, 1, 10, sbb);
    placeBlock(region, bSlab, 1, 2, 11, sbb);
    placeBlock(region, bSlab, 2, 2, 11, sbb);
    placeBlock(region, planks, 1, 2, 12, sbb);
    placeBlock(region, planks, 2, 2, 12, sbb);
    placeBlock(region, planks, 1, 2, 13, sbb);
    placeBlock(region, planks, 2, 2, 13, sbb);
    placeBlock(region, planks, 3, 2, 11, sbb);
    placeBlock(region, fence, 3, 3, 11, sbb);
    placeBlock(region, fence, 3, 4, 11, sbb);
    placeBlock(region, planks, 3, 1, 10, sbb);
    placeBlock(region, fence, 3, 2, 10, sbb);
    placeBlock(region, fence, 3, 3, 10, sbb);
    placeBlock(region, planks, 3, 1, 9, sbb);
    placeBlock(region, fence, 3, 2, 9, sbb);
    placeBlock(region, sSlab, 13, 1, 5, sbb);
    placeBlock(region, sSlab, 12, 1, 5, sbb);
    placeBlock(region, dSlab, 13, 1, 4, sbb);
    placeBlock(region, dSlab, 12, 1, 4, sbb);
    placeBlock(region, sSlab, 13, 2, 3, sbb);
    placeBlock(region, sSlab, 12, 2, 3, sbb);
    placeBlock(region, dSlab, 13, 2, 2, sbb);
    placeBlock(region, dSlab, 12, 2, 2, sbb);
    placeBlock(region, dSlab, 13, 2, 1, sbb);
    placeBlock(region, dSlab, 12, 2, 1, sbb);
    placeBlock(region, dSlab, 11, 2, 3, sbb);
    placeBlock(region, fence, 11, 3, 3, sbb);
    placeBlock(region, fence, 11, 4, 3, sbb);
    placeBlock(region, dSlab, 11, 1, 4, sbb);
    placeBlock(region, fence, 11, 2, 4, sbb);
    placeBlock(region, fence, 11, 3, 4, sbb);
    placeBlock(region, dSlab, 11, 1, 5, sbb);
    placeBlock(region, fence, 11, 2, 5, sbb);
    int rise       = 5;
    int numFlights = highestOpening / rise;
    for (int i = 0; i < numFlights; i++) {
        makeStairs15flight(region, rand, sbb, 2 + i * rise, rotationPlus(Rotation::None, i * 3), true);
        makeStairs15flight(region, rand, sbb, 2 + i * rise, rotationPlus(Rotation::Clockwise180, i * 3), false);
    }
    return true;
}

void TowerWing::makeStairs15flight(Region& region, RandomSource& rand, BlockBox const& sbb, int h, Rotation rotation, bool useBirchWood) {
    FakeOrientation fake(*this, turned(getOrientation(), rotation));
    Block const&    fence   = B("minecraft:oak_fence");
    Block const&    slabBot = useBirchWood ? birchSlab() : stoneSlab();
    Block const&    slabTop = useBirchWood ? birchSlabTop() : stoneSlabTop();
    Block const&    slabDbl = useBirchWood ? birchPlanks() : stoneSlabDouble();

    placeBlock(region, slabBot, 3, 1 + h, 13, sbb);
    maybeGenerateBlock(region, sbb, rand, 0.9F, 4, 1 + h, 13, slabTop);
    placeBlock(region, slabBot, 5, 2 + h, 13, sbb);
    placeBlock(region, slabTop, 6, 2 + h, 13, sbb);
    placeBlock(region, slabBot, 7, 3 + h, 13, sbb);
    placeBlock(region, slabTop, 8, 3 + h, 13, sbb);
    placeBlock(region, slabBot, 9, 4 + h, 13, sbb);
    maybeGenerateBlock(region, sbb, rand, 0.9F, 10, 4 + h, 13, slabTop);
    maybeGenerateBlock(region, sbb, rand, 0.9F, 11, 5 + h, 13, slabBot);
    placeBlock(region, slabTop, 12, 5 + h, 13, sbb);
    placeBlock(region, slabTop, 13, 5 + h, 13, sbb);

    maybeGenerateBlock(region, sbb, rand, 0.9F, 3, 1 + h, 12, slabBot);
    placeBlock(region, slabTop, 4, 1 + h, 12, sbb);
    placeBlock(region, slabBot, 5, 2 + h, 12, sbb);
    placeBlock(region, slabTop, 6, 2 + h, 12, sbb);
    maybeGenerateBlock(region, sbb, rand, 0.9F, 7, 3 + h, 12, slabBot);
    placeBlock(region, slabTop, 8, 3 + h, 12, sbb);
    placeBlock(region, slabBot, 9, 4 + h, 12, sbb);
    maybeGenerateBlock(region, sbb, rand, 0.9F, 10, 4 + h, 12, slabTop);
    placeBlock(region, slabBot, 11, 5 + h, 12, sbb);
    placeBlock(region, slabTop, 12, 5 + h, 12, sbb);
    placeBlock(region, slabTop, 13, 5 + h, 12, sbb);

    placeBlock(region, slabDbl, 4, 1 + h, 11, sbb);
    placeBlock(region, slabDbl, 5, 2 + h, 11, sbb);
    maybeGenerateBlock(region, sbb, rand, 0.9F, 6, 2 + h, 11, slabTop);
    placeBlock(region, slabDbl, 7, 3 + h, 11, sbb);
    maybeGenerateBlock(region, sbb, rand, 0.9F, 8, 3 + h, 11, slabTop);
    placeBlock(region, slabDbl, 9, 4 + h, 11, sbb);
    placeBlock(region, slabTop, 10, 4 + h, 11, sbb);
    placeBlock(region, slabDbl, 11, 5 + h, 11, sbb);

    placeBlock(region, fence, 4, 2 + h, 11, sbb);
    placeBlock(region, fence, 5, 3 + h, 11, sbb);
    placeBlock(region, fence, 6, 3 + h, 11, sbb);
    placeBlock(region, fence, 7, 4 + h, 11, sbb);
    placeBlock(region, fence, 8, 4 + h, 11, sbb);
    placeBlock(region, fence, 9, 5 + h, 11, sbb);
    placeBlock(region, fence, 10, 5 + h, 11, sbb);
    placeBlock(region, fence, 11, 6 + h, 11, sbb);

    placeBlock(region, fence, 4, 3 + h, 11, sbb);
    placeBlock(region, fence, 6, 4 + h, 11, sbb);
    placeBlock(region, fence, 8, 5 + h, 11, sbb);
    placeBlock(region, fence, 10, 6 + h, 11, sbb);
    placeBlock(region, fence, 11, 7 + h, 11, sbb);
}

void TowerWing::generatePaintingsOnWall(Region& region, RandomSource& rand, int howMany, int floorLevel, Orientation direction, int minSize,
                                        BlockBox const& sbb) {
    for (int i = 0; i < howMany; i++) {
        auto pCoords = getRandomWallSpot(rand, floorLevel, direction, sbb);
        if (!pCoords) continue;
        auto painting = paintingOfSize(rand, minSize);
        if (!painting) continue; // EntityUtil.tryHangPainting(null) is false
        blockentities::entity(region, pCoords->x + 0.5, pCoords->y, pCoords->z + 0.5, "minecraft:painting",
                              Json{{"motive", painting->id}, {"direction", blocks::directionName(direction)}});
    }
}

std::optional<BlockPos> TowerWing::getRandomWallSpot(RandomSource& rand, int floorLevel, Orientation direction, BlockBox const& sbb) {
    int minX = bbox.minX + 2, maxX = bbox.maxX - 2;
    int minY = bbox.minY + floorLevel + 2, maxY = bbox.maxY - 2;
    int minZ = bbox.minZ + 2, maxZ = bbox.maxZ - 2;
    if (direction == Orientation::South) minZ = maxZ = bbox.minZ;
    else if (direction == Orientation::West) minX = maxX = bbox.maxX;
    else if (direction == Orientation::North) minZ = maxZ = bbox.maxZ;
    else if (direction == Orientation::East) minX = maxX = bbox.minX;
    for (int i = 0; i < 30; i++) {
        int      cx = minX + (maxX > minX ? rand.nextInt(maxX - minX) : 0);
        int      cy = minY + (maxY > minY ? rand.nextInt(maxY - minY) : 0);
        int      cz = minZ + (maxZ > minZ ? rand.nextInt(maxZ - minZ) : 0);
        BlockPos p{cx + stepX(direction), cy, cz + stepZ(direction)};
        if (sbb.contains(p.x, p.y, p.z)) return p;
    }
    return std::nullopt;
}

void TowerWing::makeGlyphBranches(Region& region, RandomSource& rand, Block const& colour, BlockBox const& sbb) {
    Rotation rotation    = kRotations[static_cast<std::size_t>(rand.nextInt(4))];
    int      startHeight = height > 1 ? rand.nextInt(static_cast<int>(static_cast<float>(height) * 0.66F)) : height;
    int      startZ      = 3 + rand.nextInt(size - 6);
    int      dx          = getXWithOffsetRotated(0, startZ, rotation);
    int      dz          = getZWithOffsetRotated(0, startZ, rotation);
    if (sbb.contains(dx, bbox.minY + 1, dz)) {
        for (int dy = worldY(startHeight); dy > 0; dy--) {
            BlockPos     pos{dx, dy, dz};
            Block const& at = region.getBlock(pos);
            // #twilightforest:castle_blocks that conduct redstone (full blocks)
            if (blocks::inTag(at, "twilightforest:castle_blocks") && !blocks::isPartial(at)) region.setBlock(pos, colour, 2);
            else break;
        }
    }
    int leftOffset = startZ - (1 + rand.nextInt(3));
    int leftHeight = rand.nextInt(std::max(height - startHeight, 1));
    if (leftOffset >= 0) {
        for (int z = startZ; z > leftOffset; z--) setBlockStateRotated(region, colour, 0, startHeight, z, rotation, sbb);
        for (int y = startHeight; y < startHeight + leftHeight; y++) setBlockStateRotated(region, colour, 0, y, leftOffset, rotation, sbb);
    }
    int rightOffset = startZ + (1 + rand.nextInt(3));
    int rightHeight = rand.nextInt(std::max(height - startHeight, 1));
    if (rightOffset < size - 1) {
        for (int z = startZ; z < rightOffset; z++) setBlockStateRotated(region, colour, 0, startHeight, z, rotation, sbb);
        for (int y = startHeight; y < startHeight + rightHeight; y++) setBlockStateRotated(region, colour, 0, y, rightOffset, rotation, sbb);
    }
}

int TowerWing::getGroundLevelDelta() const { return 1; }

// ==== TowerMainComponent ===================================================================================

TowerMain::TowerMain(RandomSource& rand, int depth, int x, int y, int z)
: TowerWing("twilightforest:TFLTMai", depth, x, y + 1, z, 15, 55 + rand.nextInt(32), Orientation::South) {}

void TowerMain::addChildren(Piece& parent, PieceList& list, RandomSource& rand) {
    makeARoof(parent, list, rand);
    for (auto rotation : kRotations) {
        auto dest = getValidOpening(rand, rotation);
        if (dest[1] < height / 2) dest[1] += 20;
        int childHeight = std::min(21 + rand.nextInt(10), height - dest[1] - 3);
        if (!makeTowerWing(list, rand, 1, dest[0], dest[1], dest[2], 9, childHeight, rotation))
            makeTowerWing(list, rand, 1, dest[0], dest[1], dest[2], 7, childHeight, rotation);
    }
    for (auto rotation : kRotations) {
        auto dest = getValidOpening(rand, rotation);
        if (dest[1] < height / 2) dest[1] += 10;
        int childHeight = std::min(21 + rand.nextInt(10), height - dest[1] - 3);
        if (!makeTowerWing(list, rand, 1, dest[0], dest[1], dest[2], 9, childHeight, rotation))
            makeTowerWing(list, rand, 1, dest[0], dest[1], dest[2], 7, childHeight, rotation);
    }
    for (auto rotation : kRotations) {
        auto dest        = getValidOpening(rand, rotation);
        int  childHeight = std::min(7 + rand.nextInt(6), height - dest[1] - 3);
        if (!makeTowerWing(list, rand, 1, dest[0], dest[1], dest[2], 5, childHeight, rotation))
            makeTowerWing(list, rand, 1, dest[0], dest[1], dest[2], 3, childHeight, rotation);
    }
    for (auto rotation : kRotations) {
        auto dest        = getOutbuildingOpening(rand, rotation);
        int  childHeight = 11 + rand.nextInt(10);
        int  childSize   = 7 + rand.nextInt(2) * 2;
        makeTowerOutbuilding(list, rand, 1, dest[0], dest[1], dest[2], childSize, childHeight, rotation);
    }
    for (int i = 0; i < 4; i++)
        for (auto towerRotation : kRotations) {
            auto dest        = getValidOpening(rand, towerRotation);
            int  childHeight = 6 + rand.nextInt(5);
            if (rand.nextInt(3) == 0 || !makeTowerWing(list, rand, 1, dest[0], dest[1], dest[2], 5, childHeight, towerRotation))
                makeTowerWing(list, rand, 1, dest[0], dest[1], dest[2], 3, childHeight, towerRotation);
        }
}

std::array<int, 3> TowerMain::getOutbuildingOpening(RandomSource& rand, Rotation rotation) {
    int rx = 0, ry = 1, rz = 0;
    switch (rotation) {
    case Rotation::None:
        rx = size - 1;
        rz = 6 + rand.nextInt(8);
        break;
    case Rotation::Clockwise90:
        rx = 1 + rand.nextInt(11);
        rz = size - 1;
        break;
    case Rotation::Clockwise180:
        rx = 0;
        rz = 1 + rand.nextInt(8);
        break;
    case Rotation::Counterclockwise90:
        rx = 3 + rand.nextInt(11);
        rz = 0;
        break;
    }
    return {rx, ry, rz};
}

bool TowerMain::makeTowerOutbuilding(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight, Rotation rotation) {
    Orientation direction   = getStructureRelativeRotation(rotation);
    auto        dx          = offsetTowerCoords(x, y, z, wingSize, direction);
    auto        outbuilding = std::make_unique<TowerOutbuilding>(index, dx[0], dx[1], dx[2], wingSize, wingHeight, direction);
    if (findCollisionPiece(list, outbuilding->bbox)) return false;
    auto* raw = outbuilding.get();
    list.push_back(std::move(outbuilding));
    raw->addChildren(*this, list, rand);
    addOpening(x, y, z, rotation);
    return true;
}

void TowerMain::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    randomFill(region, sbb, 0, 0, 0, size - 1, height - 1, size - 1, false, rand, getStrongholdStones());
    fillAir(region, sbb, 1, 1, 1, size - 2, height - 2, size - 2);
    Block const& cobble = B("minecraft:cobblestone");
    for (int x = 0; x < size; x++)
        for (int z = 0; z < size; z++) fillColumnDown(region, cobble, x, -1, z, sbb);
    if (height - highestOpening > 15) highestOpening = height - 15;
    makeStairs(region, rand, sbb);
    makeOpenings(region, sbb);
    decorateStairFloor(region, rand, sbb);
    makeStairwayCrossings(region, rand, sbb);
    makeLichRoom(region, rand, sbb);
    makeTowerPaintings(region, rand, sbb);
}

void TowerMain::makeStairwayCrossings(Region& region, RandomSource& rand, BlockBox const& sbb) {
    int flights = highestOpening / 5 - 2;
    for (int i = 2 + rand.nextInt(2); i < flights; i += 1 + rand.nextInt(5)) makeStairCrossing(region, rand, i, sbb);
}

void TowerMain::makeStairCrossing(Region& region, RandomSource& rand, int flight, BlockBox const& sbb) {
    std::optional<FakeOrientation> fake;
    if (flight % 2 == 0) fake.emplace(*this, getStructureRelativeRotation(Rotation::Clockwise90));
    int          floorLevel    = flight * 5;
    Block const& crossingFloor = rand.nextBoolean() ? stoneSlabDouble() : birchPlanks();
    for (int dx = 6; dx <= 8; dx++)
        for (int dz = 4; dz <= 10; dz++) placeBlock(region, crossingFloor, dx, floorLevel, dz, sbb);
    floorLevel++;
    Block const& fence = B("minecraft:oak_fence");
    int          dx    = 6;
    for (int dz = 3; dz <= 11; dz++) placeBlock(region, fence, dx, floorLevel, dz, sbb);
    dx++;
    for (int dz = 3; dz <= 11; dz++) placeBlock(region, air(), dx, floorLevel, dz, sbb);
    dx++;
    for (int dz = 3; dz <= 11; dz++) placeBlock(region, fence, dx, floorLevel, dz, sbb);
    placeBlock(region, crossingFloor, 6, floorLevel - 1, 11, sbb);
    placeBlock(region, crossingFloor, 8, floorLevel - 1, 3, sbb);
    placeBlock(region, fence, 5, floorLevel, 11, sbb);
    placeBlock(region, fence, 9, floorLevel, 3, sbb);
    char const* mobID = "minecraft:skeleton";
    switch (rand.nextInt(4)) {
    case 2:
        mobID = "minecraft:zombie";
        break;
    case 3:
        mobID = "twilightforest:swarm_spider";
        break;
    default:
        break;
    }
    setSpawner(region, 7, floorLevel + 2, 7, sbb, mobID);
    placeBlock(region, fence, 6, floorLevel + 1, 7, sbb);
    placeBlock(region, fence, 8, floorLevel + 1, 7, sbb);
    placeBlock(region, fence, 6, floorLevel + 2, 7, sbb);
    placeBlock(region, fence, 8, floorLevel + 2, 7, sbb);
}

void TowerMain::makeLichRoom(Region& region, RandomSource& rand, BlockBox const& sbb) {
    int      floorLevel = 2 + (highestOpening / 5) * 5;
    Rotation i          = (highestOpening / 5) % 2 == 0 ? Rotation::None : Rotation::Clockwise90;
    makeLichFloor(region, floorLevel, i, sbb);
    decorateLichChandelier(region, floorLevel, sbb);
    decoratePaintings(region, rand, floorLevel, sbb);
    decorateTorches(region, rand, floorLevel, sbb);
    placeBlock(region, B("twilightforest:lich_boss_spawner"), size / 2, floorLevel + 2, size / 2, sbb);
}

void TowerMain::makeTowerPaintings(Region& region, RandomSource& rand, BlockBox const& sbb) {
    int howMany = 10;
    for (auto d : {Orientation::West, Orientation::East, Orientation::North, Orientation::South}) {
        generatePaintingsOnWall(region, rand, howMany, 0, d, 48, sbb);
        generatePaintingsOnWall(region, rand, howMany, 0, d, 32, sbb);
        generatePaintingsOnWall(region, rand, howMany, 0, d, 0, sbb);
    }
}

void TowerMain::makeLichFloor(Region& region, int floorLevel, Rotation rotation, BlockBox const& sbb) {
    FakeOrientation fake(*this, getStructureRelativeRotation(rotation));
    Block const&    slab  = birchSlabTop();
    Block const&    plank = birchPlanks();
    Block const&    glass = B("minecraft:glass");
    for (int fx = 1; fx < 14; fx++)
        for (int fz = 1; fz < 14; fz++) {
            if ((fx == 1 || fx == 2) && (fz >= 6 && fz <= 12)) {
                if (fz == 6) placeBlock(region, slab, fx, floorLevel, fz, sbb);
            } else if ((fx == 12 || fx == 13) && (fz >= 3 && fz <= 8)) {
                if (fz == 8) placeBlock(region, slab, fx, floorLevel, fz, sbb);
            } else if ((fx >= 4 && fx <= 10) && (fz >= 4 && fz <= 10)) {
                if ((fx == 4 && fz == 4) || (fx == 10 && fz == 10)) placeBlock(region, plank, fx, floorLevel, fz, sbb);
                else placeBlock(region, glass, fx, floorLevel, fz, sbb);
            } else if ((fx == 2 || fx == 3) && (fz == 2 || fz == 3)) {
                placeBlock(region, glass, fx, floorLevel, fz, sbb);
            } else if ((fx == 11 || fx == 12) && (fz == 11 || fz == 12)) {
                placeBlock(region, glass, fx, floorLevel, fz, sbb);
            } else {
                placeBlock(region, plank, fx, floorLevel, fz, sbb);
            }
        }
    placeBlock(region, air(), 3, floorLevel + 1, 11, sbb);
    placeBlock(region, air(), 3, floorLevel + 1, 10, sbb);
    placeBlock(region, air(), 3, floorLevel + 2, 11, sbb);
    placeBlock(region, air(), 11, floorLevel + 1, 3, sbb);
    placeBlock(region, air(), 11, floorLevel + 1, 4, sbb);
    placeBlock(region, air(), 11, floorLevel + 2, 3, sbb);
}

void TowerMain::decorateLichChandelier(Region& region, int floorLevel, BlockBox const& sbb) {
    int          cx = size / 2, cy = floorLevel + 4, cz = size / 2;
    Block const& fence = B("minecraft:oak_fence");
    Block const& torch = B("minecraft:torch");
    placeBlock(region, fence, cx + 1, cy, cz, sbb);
    placeBlock(region, fence, cx + 2, cy, cz, sbb);
    placeBlock(region, fence, cx + 1, cy, cz + 1, sbb);
    placeBlock(region, fence, cx, cy, cz + 1, sbb);
    placeBlock(region, fence, cx, cy, cz + 2, sbb);
    placeBlock(region, fence, cx - 1, cy, cz + 1, sbb);
    placeBlock(region, fence, cx - 1, cy, cz, sbb);
    placeBlock(region, fence, cx - 2, cy, cz, sbb);
    placeBlock(region, fence, cx - 1, cy, cz - 1, sbb);
    placeBlock(region, fence, cx, cy, cz - 1, sbb);
    placeBlock(region, fence, cx, cy, cz - 2, sbb);
    placeBlock(region, fence, cx + 1, cy, cz - 1, sbb);
    cy++;
    placeBlock(region, fence, cx + 1, cy, cz, sbb);
    placeBlock(region, torch, cx + 2, cy, cz, sbb);
    placeBlock(region, torch, cx + 1, cy, cz + 1, sbb);
    placeBlock(region, fence, cx, cy, cz + 1, sbb);
    placeBlock(region, torch, cx, cy, cz + 2, sbb);
    placeBlock(region, torch, cx - 1, cy, cz + 1, sbb);
    placeBlock(region, fence, cx - 1, cy, cz, sbb);
    placeBlock(region, torch, cx - 2, cy, cz, sbb);
    placeBlock(region, torch, cx - 1, cy, cz - 1, sbb);
    placeBlock(region, fence, cx, cy, cz - 1, sbb);
    placeBlock(region, torch, cx, cy, cz - 2, sbb);
    placeBlock(region, torch, cx + 1, cy, cz - 1, sbb);
    cy++;
    placeBlock(region, torch, cx + 1, cy, cz, sbb);
    placeBlock(region, torch, cx, cy, cz + 1, sbb);
    placeBlock(region, torch, cx - 1, cy, cz, sbb);
    placeBlock(region, torch, cx, cy, cz - 1, sbb);
    for (int y = floorLevel + 5; y < height - 1; y++) placeBlock(region, fence, cx, y, cz, sbb);
}

void TowerMain::decoratePaintings(Region& region, RandomSource& rand, int floorLevel, BlockBox const& sbb) {
    int howMany = 25;
    // Direction.Plane.HORIZONTAL: NORTH, EAST, SOUTH, WEST
    for (auto horizontal : {Orientation::North, Orientation::East, Orientation::South, Orientation::West}) {
        generatePaintingsOnWall(region, rand, howMany, floorLevel, horizontal, 48, sbb);
        generatePaintingsOnWall(region, rand, howMany, floorLevel, horizontal, 32, sbb);
        generatePaintingsOnWall(region, rand, howMany, floorLevel, horizontal, 0, sbb);
    }
}

void TowerMain::decorateTorches(Region& region, RandomSource& rand, int floorLevel, BlockBox const& sbb) {
    for (auto direction : {Orientation::South, Orientation::East, Orientation::North, Orientation::West}) {
        // generateTorchesOnWall: up to 5 torches on fence holders, not over paintings or other torches
        for (int i = 0; i < 5; i++) {
            auto wCoords = getRandomWallSpot(rand, floorLevel, direction, sbb);
            if (!wCoords) continue;
            BlockPos above{wCoords->x, wCoords->y + 1, wCoords->z};
            if (region.getBlock(*wCoords).isAir() && region.getBlock(above).isAir()) {
                region.setBlock(*wCoords, B("minecraft:oak_fence"), 2);
                region.setBlock(above, B("minecraft:torch"), 2);
            }
        }
    }
}

TerrainAdjustment TowerMain::getTerrainAdjustment() const { return TerrainAdjustment::BeardBox; }

// ==== TowerBridgeComponent =================================================================================

TowerBridge::TowerBridge(int depth, int x, int y, int z, int pSize, int pHeight, Orientation direction)
: TowerWing("twilightforest:TFLTBri", depth, x, y, z, 3, 3, direction),
  dSize(pSize),
  dHeight(pHeight) {}

void TowerBridge::addChildren(Piece&, PieceList& list, RandomSource& rand) {
    std::array<int, 3> dest{2, 1, 1};
    makeTowerWing(list, rand, 1, dest[0], dest[1], dest[2], dSize, dHeight, Rotation::None);
}

BlockBox TowerBridge::getWingBB() const {
    auto dest = offsetTowerCoords(2, 1, 1, dSize, getOrientation().value_or(Orientation::South));
    return componentBox(dest[0], dest[1], dest[2], 0, 0, 0, dSize - 1, dHeight - 1, dSize - 1, getOrientation());
}

void TowerBridge::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    Block const& fence = B("minecraft:oak_fence");
    Block const& brick = B("minecraft:stone_bricks");
    for (int x = 0; x < 3; x++) {
        placeBlock(region, fence, x, 2, 0, sbb);
        placeBlock(region, fence, x, 2, 2, sbb);
        placeBlock(region, brick, x, 1, 0, sbb);
        placeBlock(region, brick, x, 1, 2, sbb);
        placeBlock(region, brick, x, 0, 0, sbb);
        placeBlock(region, brick, x, 0, 1, sbb);
        placeBlock(region, brick, x, 0, 2, sbb);
        placeBlock(region, brick, x, -1, 1, sbb);
    }
    placeBlock(region, brick, -1, -1, 1, sbb);
    placeBlock(region, brick, 3, -1, 1, sbb);
    fillAir(region, sbb, 0, 1, 1, 2, 2, 1);
}

// ==== TowerOutbuildingComponent ===========================================================================

TowerOutbuilding::TowerOutbuilding(int depth, int x, int y, int z, int pSize, int pHeight, Orientation direction)
: TowerWing("twilightforest:TFLTOut", depth, x, y, z, pSize, pHeight, direction) {}

void TowerOutbuilding::makeABeard(Piece&, PieceList&, RandomSource&) {}

bool TowerOutbuilding::makeTowerWing(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int wingSize, int wingHeight, Rotation direction) {
    if (y > 7) return TowerWing::makeTowerWing(list, rand, index, x, y, z, wingSize, wingHeight, direction);
    return false;
}

void TowerOutbuilding::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) {
    Block const& cobble = B("minecraft:cobblestone");
    for (int x = 0; x < size; x++)
        for (int z = 0; z < size; z++) fillColumnDown(region, cobble, x, -1, z, sbb);
    TowerWing::postProcess(region, rand, sbb, pivot);
}

// ==== beards ===================================================================================================

TowerBeard::TowerBeard(std::string typeId, int depth, TowerWing const& wing, int x, int y, int z) : ComponentOld(std::move(typeId), depth, x, y, z) {
    setOrientation(wing.getOrientation());
    size   = wing.size - 2;
    height = size / 2;
    bbox   = BlockBox::of(wing.bbox.minX + 1, wing.bbox.minY - height - 1, wing.bbox.minZ + 1, wing.bbox.maxX - 1, wing.bbox.minY - 1, wing.bbox.maxZ - 1);
}

void TowerBeard::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const&) { makePyramidBeard(region, rand, sbb); }

Json TowerBeard::save() const {
    Json j           = ComponentOld::save();
    j["beardSize"]   = size;
    j["beardHeight"] = height;
    return j;
}

void TowerBeard::load(Json const& data) {
    ComponentOld::load(data);
    size   = data.value("beardSize", 0);
    height = data.value("beardHeight", 0);
}

void TowerBeard::makePyramidBeard(Region& region, RandomSource& rand, BlockBox const& sbb) {
    for (int y = 0; y <= height; y++) {
        int min = y;
        int max = size - y - 1;
        randomFill(region, sbb, min, height - y, min, max, height - y, max, false, rand, getStrongholdStones());
    }
}

TowerBeardAttached::TowerBeardAttached(int depth, TowerWing const& wing, int x, int y, int z) : TowerBeard("twilightforest:TFLTBA", depth, wing, x, y, z) {
    bbox = BlockBox::of(wing.bbox.minX, wing.bbox.minY - height - 1, wing.bbox.minZ, wing.bbox.maxX, wing.bbox.maxY - 1, wing.bbox.maxZ);
}

void TowerBeardAttached::postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const&) { makeAttachedBeard(region, rand, sbb); }

void TowerBeardAttached::makeAttachedBeard(Region& region, RandomSource& rand, BlockBox const& sbb) {
    for (int y = 0; y <= height; y++) {
        int min = y + 1;
        int max = size - y;
        randomFill(region, sbb, 0, height - y, min, max, height - y, max, false, rand, getStrongholdStones());
    }
}

// ==== roofs ====================================================================================================

TowerRoof::TowerRoof(std::string typeId, int depth, int x, int y, int z) : ComponentOld(std::move(typeId), depth, x, y, z) { spawnListIndex = -1; }

void TowerRoof::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) {}

bool TowerRoof::fits(TowerWing const& parent, PieceList const& pieces) const { return findCollisionPiece(pieces, bbox) == &parent; }

Json TowerRoof::save() const {
    Json j          = ComponentOld::save();
    j["roofSize"]   = size;
    j["roofHeight"] = height;
    return j;
}

void TowerRoof::load(Json const& data) {
    ComponentOld::load(data);
    size   = data.value("roofSize", 0);
    height = data.value("roofHeight", 0);
}

void TowerRoof::makeAttachedOverhangBB(TowerWing const& wing) {
    auto const& w = wing.bbox;
    switch (getOrientation().value_or(kNoOrientation)) {
    case Orientation::South:
        bbox = BlockBox::of(w.minX, w.maxY, w.minZ - 1, w.maxX + 1, w.maxY + height - 1, w.maxZ + 1);
        break;
    case Orientation::West:
        bbox = BlockBox::of(w.minX - 1, w.maxY, w.minZ, w.maxX + 1, w.maxY + height - 1, w.maxZ + 1);
        break;
    case Orientation::East:
        bbox = BlockBox::of(w.minX - 1, w.maxY, w.minZ - 1, w.maxX, w.maxY + height - 1, w.maxZ + 1);
        break;
    case Orientation::North:
        bbox = BlockBox::of(w.minX - 1, w.maxY, w.minZ - 1, w.maxX + 1, w.maxY + height - 1, w.maxZ);
        break;
    default:
        break;
    }
}

void TowerRoof::makeCapBB(TowerWing const& wing) {
    auto const& w = wing.bbox;
    bbox          = BlockBox::of(w.minX, w.maxY, w.minZ, w.maxX, w.maxY + height, w.maxZ);
}

void TowerRoof::makeOverhangBB(TowerWing const& wing) {
    auto const& w = wing.bbox;
    bbox          = BlockBox::of(w.minX - 1, w.maxY, w.minZ - 1, w.maxX + 1, w.maxY + height - 1, w.maxZ + 1);
}

TowerRoofFence::TowerRoofFence(int depth, TowerWing const& wing, int x, int y, int z) : TowerRoof("twilightforest:TFLTRF", depth, x, y, z) {
    setOrientation(wing.getOrientation());
    size   = wing.size;
    height = 0;
    makeCapBB(wing);
}

void TowerRoofFence::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    int y = height + 1;
    for (int x = 0; x <= size - 1; x++)
        for (int z = 0; z <= size - 1; z++)
            if (x == 0 || x == size - 1 || z == 0 || z == size - 1) placeBlock(region, B("minecraft:oak_fence"), x, y, z, sbb);
}

TowerRoofGableForwards::TowerRoofGableForwards(int depth, TowerWing const& wing, int x, int y, int z) : TowerRoof("twilightforest:TFLTRGF", depth, x, y, z) {
    setOrientation(wing.getOrientation());
    size   = wing.size + 2;
    height = size;
    makeAttachedOverhangBB(wing);
}

void TowerRoofGableForwards::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    Block const& slab   = birchSlab();
    Block const& planks = birchPlanks();
    int          slopeChange = slopeChangeForSize();
    for (int y = 0; y <= height; y++) {
        int min = 0, max = 0;
        if (y < slopeChange) {
            min = y;
            max = size - y - 1;
        } else {
            min = (y + slopeChange) / 2;
            max = size - ((y + slopeChange) / 2) - 1;
        }
        for (int x = 0; x <= size - 2; x++)
            for (int z = min; z <= max; z++) {
                if (z == min || z == max) placeBlock(region, planks, x, y, z, sbb);
                else if (x < size - 2) placeBlock(region, planks, x, y, z, sbb);
            }
    }
    int top  = (size + 1) - slopeChange;
    int zMid = size / 2;
    placeBlock(region, birchSlabTop(), size - 1, top - 1, zMid, sbb);
    placeBlock(region, slab, 0, top, zMid, sbb);
    placeBlock(region, slab, size - 3, top, zMid, sbb);
    placeBlock(region, planks, size - 2, top, zMid, sbb);
    placeBlock(region, planks, size - 1, top, zMid, sbb);
    placeBlock(region, planks, size - 1, top + 1, zMid, sbb);
}

int TowerRoofGableForwards::slopeChangeForSize() const { return size > 10 ? 3 : size > 6 ? 2 : 1; }

TowerRoofPointy::TowerRoofPointy(std::string typeId, int depth, TowerWing const& wing, int x, int y, int z) : TowerRoof(std::move(typeId), depth, x, y, z) {
    setOrientation(wing.getOrientation());
    size   = wing.size;
    height = size;
    makeCapBB(wing);
}

void TowerRoofPointy::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    Block const& slab   = birchSlab();
    Block const& planks = birchPlanks();
    for (int y = 0; y <= height; y++) {
        int min = 0, max = 0;
        int slopeChange = slopeChangeForSize();
        if (y < slopeChange) {
            min = y;
            max = size - y - 1;
        } else {
            min = (y + slopeChange) / 2;
            max = size - ((y + slopeChange) / 2) - 1;
        }
        int mid = min + ((max - min) / 2);
        for (int x = min; x <= max; x++)
            for (int z = min; z <= max; z++) {
                placeBlock(region, planks, x, y, z, sbb);
                if ((x == min && (z == min || z == max)) || (x == max && (z == min || z == max))) placeBlock(region, slab, x, y + 1, z, sbb);
                if (((((x == min || x == max) && z == mid) && x % 2 == 0) || (((z == min || z == max) && x == mid) && z % 2 == 0)) && mid != min + 1)
                    placeBlock(region, slab, x, y + 1, z, sbb);
            }
    }
}

int TowerRoofPointy::slopeChangeForSize() const { return size > 10 ? 3 : size > 6 ? 2 : 1; }

TowerRoofPointyOverhang::TowerRoofPointyOverhang(int depth, TowerWing const& wing, int x, int y, int z)
: TowerRoofPointy("twilightforest:TFLTRPO", depth, wing, x, y, z) {
    setOrientation(wing.getOrientation());
    size   = wing.size + 2;
    height = size;
    makeOverhangBB(wing);
}

TowerRoofSlab::TowerRoofSlab(std::string typeId, int depth, TowerWing const& wing, int x, int y, int z) : TowerRoof(std::move(typeId), depth, x, y, z) {
    setOrientation(wing.getOrientation());
    size   = wing.size;
    height = size / 2;
    makeCapBB(wing);
}

void TowerRoofSlab::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    makePyramidCap(region, B("minecraft:birch_slab"), birchPlanks(), sbb);
}

void TowerRoofSlab::makePyramidCap(Region& region, Block const& slabType, Block const& woodType, BlockBox const& sbb) {
    for (int y = 0; y <= height; y++) {
        int min = 2 * y;
        int max = size - (2 * y) - 1;
        for (int x = min; x <= max; x++)
            for (int z = min; z <= max; z++) {
                if (x == min || x == max || z == min || z == max) placeBlock(region, slabType, x, y, z, sbb);
                else placeBlock(region, woodType, x, y, z, sbb);
            }
    }
}

void TowerRoofSlab::makeConnectedCap(Region& region, Block const& slabType, Block const& woodType, BlockBox const& sbb) {
    for (int y = 0; y < height; y++) {
        int min = 2 * y;
        int max = size - (2 * y) - 1;
        for (int x = 0; x <= max; x++)
            for (int z = min; z <= max; z++) {
                if (x == max || z == min || z == max) placeBlock(region, slabType, x, y, z, sbb);
                else placeBlock(region, woodType, x, y, z, sbb);
            }
    }
}

TowerRoofAttachedSlab::TowerRoofAttachedSlab(int depth, TowerWing const& wing, int x, int y, int z)
: TowerRoofSlab("twilightforest:TFLTRAS", depth, wing, x, y, z) {}

void TowerRoofAttachedSlab::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    makeConnectedCap(region, B("minecraft:birch_slab"), birchPlanks(), sbb);
}

TowerRoofSlabForwards::TowerRoofSlabForwards(int depth, TowerWing const& wing, int x, int y, int z)
: TowerRoofSlab("twilightforest:TFLTRSF", depth, wing, x, y, z) {
    setOrientation(wing.getOrientation());
    size   = wing.size + 2;
    height = size / 2;
    makeAttachedOverhangBB(wing);
}

void TowerRoofSlabForwards::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    Block const& slab = B("minecraft:birch_slab");
    Block const& dbl  = B("minecraft:birch_slab", Json{{"type", "double"}});
    for (int y = 0; y <= height; y++) {
        int min = 2 * y;
        int max = size - (2 * y) - 1;
        for (int x = 0; x <= max - 1; x++)
            for (int z = min; z <= max; z++) {
                if (x == max - 1 || z == min || z == max) placeBlock(region, slab, x, y, z, sbb);
                else placeBlock(region, dbl, x, y, z, sbb);
            }
    }
}

namespace {
void stairsRoof(ComponentOld& piece, Region& region, BlockBox const& sbb, int size, int height) {
    Block const& slab   = B("minecraft:birch_slab");
    Block const& planks = birchPlanks();
    Block const& north  = B("minecraft:birch_stairs", Json{{"facing", "north"}});
    Block const& south  = B("minecraft:birch_stairs", Json{{"facing", "south"}});
    Block const& east   = B("minecraft:birch_stairs", Json{{"facing", "east"}});
    Block const& west   = B("minecraft:birch_stairs", Json{{"facing", "west"}});
    for (int y = 0; y <= height; y++) {
        int min = y;
        int max = size - y - 1;
        for (int x = min; x <= max; x++)
            for (int z = min; z <= max; z++) {
                if (x == min) piece.placeBlock(region, (z == min || z == max) ? slab : west, x, y, z, sbb);
                else if (x == max) piece.placeBlock(region, (z == min || z == max) ? slab : east, x, y, z, sbb);
                else if (z == max) piece.placeBlock(region, south, x, y, z, sbb);
                else if (z == min) piece.placeBlock(region, north, x, y, z, sbb);
                else piece.placeBlock(region, planks, x, y, z, sbb);
            }
    }
}
} // namespace

TowerRoofStairs::TowerRoofStairs(int depth, TowerWing const& wing, int x, int y, int z) : TowerRoof("twilightforest:TFLTRSt", depth, x, y, z) {
    setOrientation(Orientation::South);
    size   = wing.size;
    height = size / 2;
    makeCapBB(wing);
}

void TowerRoofStairs::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) { stairsRoof(*this, region, sbb, size, height); }

TowerRoofStairsOverhang::TowerRoofStairsOverhang(int depth, TowerWing const& wing, int x, int y, int z) : TowerRoof("twilightforest:TFLTRStO", depth, x, y, z) {
    setOrientation(Orientation::South);
    size        = wing.size + 2;
    height      = size / 2;
    auto const& w = wing.bbox;
    bbox          = BlockBox::of(w.minX - 1, w.maxY, w.minZ - 1, w.maxX + 1, w.maxY + height - 1, w.maxZ + 1);
}

void TowerRoofStairsOverhang::postProcess(Region& region, RandomSource&, BlockBox const& sbb, BlockPos const&) { stairsRoof(*this, region, sbb, size, height); }

} // namespace gf::structure::bases
