// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/type/LabyrinthStructure.java (getFirstPiece)
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeRuinsComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeMoundComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeUpperEntranceComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeEntranceShaftComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MinotaurMazeComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeRoomComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeRoomExitComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeRoomBossComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeRoomCollapseComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeMushRoomComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeRoomFountainComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeRoomVaultComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeRoomSpawnerChestsComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeDeadEndComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeDeadEndChestComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeDeadEndTrappedChestComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeDeadEndTripwireChestComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeDeadEndTorchesComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeDeadEndFountainComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeDeadEndFountainLavaComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeDeadEndPaintingComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeDeadEndRootsComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeDeadEndShroomsComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeCorridorComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeCorridorIronFenceComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeCorridorRootsComponent.java
//   src/main/java/gloamingforest/world/components/structures/minotaurmaze/MazeCorridorShroomsComponent.java
//   src/main/java/gloamingforest/util/HugeMushroomUtil.java
//   src/main/java/gloamingforest/util/features/FeaturePlacers.java (replaceBlocksDome)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "feature/labyrinth/Pieces.h"

#include <algorithm>
#include <climits>
#include <cmath>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"

#include "core/labyrinth/Terraformer.h"
#include "feature/ext/labyrinth/W0Internals.h"
#include "game/Blocks.h"

namespace gf::feature::labyrinth::maze {

namespace blocks = ::gf::structure::blocks;
namespace cl     = ::gf::core::labyrinth;
using ::gf::structure::Mirror;

namespace {

Block const& J(char const* id) { return blocks::java(id); }
Block const& J(char const* id, Json const& props) { return blocks::java(id, props); }

Block const& mazestone() { return J("gloamingforest:mazestone"); }
Block const& mazestoneBrick() { return J("gloamingforest:mazestone_brick"); }
Block const& mazestoneMosaic() { return J("gloamingforest:mazestone_mosaic"); }
Block const& mazestoneBorder() { return J("gloamingforest:mazestone_border"); }
Block const& decorativeMazestone() { return J("gloamingforest:decorative_mazestone"); }
Block const& cutMazestone() { return J("gloamingforest:cut_mazestone"); }

/// Direction.Plane.HORIZONTAL.getRandomDirection: {NORTH, EAST, SOUTH, WEST}[nextInt(4)].
Orientation randomHorizontal(RandomSource& rand) { return static_cast<Orientation>(rand.nextInt(4)); }

/// Block#getBlock() == Blocks.AIR (cave air and void air are not AIR).
bool isPlainAir(Block const& b) { return b.getTypeName() == "minecraft:air"; }

// ---- huge mushroom blocks (HugeMushroomUtil) ---------------------------------------------------------------

/// The six faces of a huge mushroom block (true: the cap / stem skin, false: pores).
struct Faces {
    bool up = true, down = true, north = true, south = true, east = true, west = true;
};

/// HugeMushroomType.
enum class Shroom { Center, North, South, East, West, NorthWest, NorthEast, SouthWest, SouthEast };

Faces facesOf(Shroom t) {
    switch (t) {
    case Shroom::Center: return {true, false, false, false, false, false};
    case Shroom::North: return {true, false, true, false, false, false};
    case Shroom::South: return {true, false, false, true, false, false};
    case Shroom::East: return {true, false, false, false, true, false};
    case Shroom::West: return {true, false, false, false, false, true};
    case Shroom::NorthWest: return {true, false, true, false, false, true};
    case Shroom::NorthEast: return {true, false, true, false, true, false};
    case Shroom::SouthWest: return {true, false, false, true, false, true};
    case Shroom::SouthEast: return {true, false, false, true, true, false};
    }
    return {};
}

/// BlockState#rotate of HugeMushroomBlock: the face at rotation.rotate(d) takes d's value.
Faces rotateFaces(Faces f, Rotation r) {
    // horizontal faces in clockwise order north, east, south, west
    bool h[4] = {f.north, f.east, f.south, f.west};
    int  s    = r == Rotation::Clockwise90 ? 1 : r == Rotation::Clockwise180 ? 2 : r == Rotation::Counterclockwise90 ? 3 : 0;
    bool o[4];
    for (int i = 0; i < 4; ++i) o[(i + s) % 4] = h[i];
    return {f.up, f.down, o[0], o[2], o[1], o[3]};
}

Faces mirrorFaces(Faces f, Mirror m) {
    if (m == Mirror::LeftRight) std::swap(f.north, f.south);
    else if (m == Mirror::FrontBack) std::swap(f.east, f.west);
    return f;
}

/// Bedrock's huge_mushroom_bits for a cap block's faces (0 pores .. 9 cap corners, 14 all cap).
int capBits(Faces f) {
    if (f.up && f.down && f.north && f.south && f.east && f.west) return 14;
    if (!f.up) return 0;
    int col = f.west ? 0 : (f.east ? 2 : 1);
    int row = f.north ? 0 : (f.south ? 2 : 1);
    return 1 + row * 3 + col;
}

/// A huge mushroom block as Java places it through StructurePiece#placeBlock (mirror, then rotate),
/// already turned for `piece` (F7's orientation does not turn huge_mushroom_bits).
Block const& mushroomBlock(Piece const& piece, char const* id, Faces faces) {
    std::string bid = std::string(id);
    int         bits;
    if (bid == "minecraft:mushroom_stem") {
        // 15: stem on every face, 10: stem sides with pores up and down
        bits = faces.up || faces.down ? 15 : 10;
    } else {
        bits = capBits(rotateFaces(mirrorFaces(faces, piece.mirror), piece.rotation));
    }
    if (auto const* b = ::gf::game::blockWithStates(bid, Json{{"huge_mushroom_bits", bits}})) return *b;
    return J(id);
}

Block const& redShroom(Piece const& p, Shroom t) { return mushroomBlock(p, "minecraft:red_mushroom_block", facesOf(t)); }
Block const& brownShroom(Piece const& p, Shroom t) { return mushroomBlock(p, "minecraft:brown_mushroom_block", facesOf(t)); }
Block const& capBlock(Piece const& p, bool red) { return mushroomBlock(p, red ? "minecraft:red_mushroom_block" : "minecraft:brown_mushroom_block", Faces{}); }
Block const& fullStem(Piece const& p) { return mushroomBlock(p, "minecraft:mushroom_stem", Faces{}); }
Block const& sideStem(Piece const& p) { return mushroomBlock(p, "minecraft:mushroom_stem", Faces{false, false, true, true, true, true}); }

/// TntBlock.UNSTABLE true (Bedrock's explode_bit: explodes when broken).
Block const& unstableTnt() {
    if (auto const* b = ::gf::game::blockWithStates("minecraft:tnt", Json{{"explode_bit", true}})) return *b;
    return J("minecraft:tnt");
}

BlockBox inflated(BlockBox const& b, int n) { return {b.minX - n, b.minY - n, b.minZ - n, b.maxX + n, b.maxY + n, b.maxZ + n}; }

std::optional<BlockBox> intersect(BlockBox const& a, BlockBox const& b) {
    BlockBox r{std::max(a.minX, b.minX), std::max(a.minY, b.minY), std::max(a.minZ, b.minZ), std::min(a.maxX, b.maxX), std::min(a.maxY, b.maxY), std::min(a.maxZ, b.maxZ)};
    if (r.minX > r.maxX || r.minY > r.maxY || r.minZ > r.maxZ) return std::nullopt;
    return r;
}

/// FeaturePlacers#processDomeColumn(level, pos, height, mask, Block target, replacement).
void processDomeColumn(Region& region, BlockPos const& pos, float height, BlockBox const& mask, char const* target, Block const& replacement) {
    if (!mask.contains(pos.x, pos.y, pos.z)) return;
    int top = region.maxY();
    for (int dY = 0; static_cast<float>(dY) < height && pos.y + dY < top; dY++) {
        BlockPos p{pos.x, pos.y + dY, pos.z};
        if (blocks::is(region.getBlock(p), target)) region.setBlock(p, replacement, 3);
    }
}

/// FeaturePlacers#replaceBlocksDome(level, center, radius, squish, chunkBox, structureBox, Block target, replacement).
void replaceBlocksDome(Region& region, BlockPos const& center, float radius, float squish, BlockBox const& chunkBox, BlockBox const& structureBox, char const* target,
                       Block const& replacement) {
    float radiusSquared = radius * radius;
    auto  common        = intersect(chunkBox, structureBox);
    if (!common) return;
    processDomeColumn(region, center, radiusSquared, *common, target, replacement);
    for (int z = 1; static_cast<float>(z) <= radius; z++) {
        for (int x = 0; static_cast<float>(x) <= radius; x++) {
            int distSq = x * x + z * z;
            if (static_cast<float>(distSq) <= radiusSquared) {
                float height = (radiusSquared - static_cast<float>(distSq)) * squish;
                processDomeColumn(region, BlockPos{center.x + x, center.y, center.z + z}, height, *common, target, replacement);
                processDomeColumn(region, BlockPos{center.x - x, center.y, center.z - z}, height, *common, target, replacement);
                processDomeColumn(region, BlockPos{center.x - z, center.y, center.z + x}, height, *common, target, replacement);
                processDomeColumn(region, BlockPos{center.x + z, center.y, center.z - x}, height, *common, target, replacement);
            }
        }
    }
}

/// BoundingBoxUtils#getComponentToAddBoundingBox(x, y, z, minX, minY, minZ, spanX, spanY, spanZ, dir, false).
BlockBox componentToAddBoundingBox(int x, int y, int z, int minX, int minY, int minZ, int spanX, int spanY, int spanZ, Orientation dir) {
    switch (dir) {
    case Orientation::West: return {x - spanZ + minZ, y + minY, z + minX, x + minZ, y + spanY + minY, z + spanX + minX};
    case Orientation::North: return {x - spanX - minX, y + minY, z - spanZ - minZ, x - minX, y + spanY + minY, z - minZ};
    case Orientation::East: return {x + minZ, y + minY, z - spanX, x + spanZ + minZ, y + spanY + minY, z + minX};
    default: return {x + minX, y + minY, z + minZ, x + spanX + minX, y + spanY + minY, z + spanZ + minZ};
    }
}

/// Adds `piece` to the list and runs its addChildren (Java's list.addPiece + addChildren).
void addPiece(PieceList& list, std::unique_ptr<Piece> piece, Piece& parent, RandomSource& random) {
    if (!piece) return;
    Piece* raw = piece.get();
    list.push_back(std::move(piece));
    raw->addChildren(parent, list, random);
}

} // namespace

// ==== MazeRuinsComponent =========================================================================================

Ruins::Ruins(int i, int x, int y, int z) : ComponentOld(kRuins, i, componentToAddBoundingBox(x, y, z, 0, 0, 0, 0, 0, 0, Orientation::South)) {
    setOrientation(Orientation::South);
}

void Ruins::addChildren(Piece&, PieceList& list, RandomSource& random) {
    // add a maze
    addPiece(list, std::make_unique<MinotaurMaze>(1, bbox.minX, bbox.minY - 14, bbox.minZ, 1, random), *this, random);
    // add maze entrance shaft
    addPiece(list, std::make_unique<EntranceShaft>(2, random, bbox.minX + 1, bbox.minY, bbox.minZ + 1), *this, random);
    // add aboveground maze entrance building
    addPiece(list, std::make_unique<Mound>(2, random, bbox.minX - 14, bbox.maxY, bbox.minZ - 14), *this, random);
}

void Ruins::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) {
    // I have no components
}

// ==== MazeMoundComponent ==========================================================================================

Mound::Mound(int i, RandomSource& rand, int x, int y, int z) : ComponentOld(kMound, i, BlockBox{x, y, z, x + DIAMETER, y + 12, z + DIAMETER}) {
    setOrientation(randomHorizontal(rand));
}

void Mound::addChildren(Piece&, PieceList& list, RandomSource& random) {
    // add aboveground maze entrance building
    addPiece(list, std::make_unique<UpperEntrance>(3, random, bbox.minX + 10, bbox.minY, bbox.minZ + 10), *this, random);
}

void Mound::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) {
    // Mound shape contributed by Structure itself
}

// ==== MazeUpperEntranceComponent ==================================================================================

UpperEntrance::UpperEntrance(int i, RandomSource& rand, int x, int y, int z) : ComponentOld(kUpperEntrance, i, x, y, z) {
    setOrientation(randomHorizontal(rand));
    bbox = BlockBox{x, y, z, x + 15, y + 4, z + 15};
}

void UpperEntrance::postProcess(Region& world, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    auto c = cl::boxCenter(bbox);
    replaceBlocksDome(world, BlockPos{c.x, worldY(-1), c.z}, 16, 0.8F, sbb, inflated(bbox, 8), "minecraft:stone", J("minecraft:dirt"));

    // ceiling
    generateMaybeBox(world, sbb, rand, 0.7F, 0, 5, 0, 15, 5, 15, mazestone(), air(), true, false);

    fill(world, sbb, 0, 0, 0, 15, 0, 15, mazestoneMosaic(), air(), false);
    fill(world, sbb, 0, 1, 0, 15, 1, 15, decorativeMazestone(), air(), true);
    fill(world, sbb, 0, 2, 0, 15, 3, 15, mazestoneBrick(), air(), true);
    fill(world, sbb, 0, 4, 0, 15, 4, 15, decorativeMazestone(), air(), true);
    generateMaybeBox(world, sbb, rand, 0.2F, 0, 0, 0, 15, 5, 15, J("minecraft:gravel"), air(), true, false);

    // doorways
    Block const& fence = J("minecraft:oak_fence");
    fill(world, sbb, 6, 1, 0, 9, 4, 0, fence, air(), false);
    fillAir(world, sbb, 7, 1, 0, 8, 3, 0);
    fill(world, sbb, 6, 1, 15, 9, 4, 15, fence, air(), false);
    fillAir(world, sbb, 7, 1, 15, 8, 3, 15);
    fill(world, sbb, 0, 1, 6, 0, 4, 9, fence, air(), false);
    fillAir(world, sbb, 0, 1, 7, 0, 3, 8);
    fill(world, sbb, 15, 1, 6, 15, 4, 9, fence, air(), false);
    fillAir(world, sbb, 15, 1, 7, 15, 3, 8);

    // random holes
    fillAir(world, sbb, 1, 1, 1, 14, 4, 14);

    // entrance pit
    fill(world, sbb, 5, 1, 5, 10, 1, 10, decorativeMazestone(), air(), false);
    fill(world, sbb, 5, 4, 5, 10, 4, 10, decorativeMazestone(), air(), false);
    generateMaybeBox(world, sbb, rand, 0.7F, 5, 2, 5, 10, 3, 10, J("minecraft:iron_bars"), air(), false, false);

    fillAir(world, sbb, 6, 0, 6, 9, 4, 9);
}

// ==== MazeEntranceShaftComponent ==================================================================================

EntranceShaft::EntranceShaft(int i, RandomSource& rand, int x, int y, int z)
: ComponentOld(kEntranceShaft, i, BlockBox{x, std::min(y, kSeaLevel), z, x + 6 - 1, std::max(y, kSeaLevel), z + 6 - 1}) {
    setOrientation(randomHorizontal(rand));
    // postProcess's this.boundingBox.encapsulate(pos.setY(generator.getSeaLevel() - 9)), done once
    // here: upstream grows the shared piece's box in its first postProcess; the start's box (the level 2
    // maze reaches lower) and the chunks the piece touches are the same either way, and no
    // generation thread writes the box
    bbox.minY = std::min(bbox.minY, kSeaLevel - 9);
}

void EntranceShaft::postProcess(Region& world, RandomSource&, BlockBox const& sbb, BlockPos const&) {
    int ySpan = bbox.maxY - bbox.minY + 1;
    fill(world, sbb, 0, 0, 0, 5, ySpan, 5, mazestoneBrick(), air(), true);
    fillAir(world, sbb, 1, 0, 1, 4, (kSeaLevel + 11) - bbox.minY, 4);
}

// ==== MinotaurMazeComponent ========================================================================================

MinotaurMaze::MinotaurMaze(int index, int x, int y, int z, int entranceX, int entranceZ, int level, RandomSource& random)
: ComponentOld(kMaze, index, x, y, z),
  mLevel(level) {
    setOrientation(Orientation::South);
    bbox = componentToAddBoundingBox(x, y, z, -getRadius(), 0, -getRadius(), getRadius() * 2 + 2, 5, getRadius() * 2 + 2, Orientation::South);

    // make maze object
    random.setSeed(getSeed());
    mMaze = std::make_unique<::gf::structure::bases::Maze>(getMazeSize(), getMazeSize(), random);

    // set the seed to a fixed value based on this maze's x and z
    configureMaze();

    // rooms
    addRoomsToMaze(entranceX, entranceZ, cl::kRooms, random);

    // make actual maze
    mMaze->generateRecursiveBacktracker(0, 0, random);
}

MinotaurMaze::MinotaurMaze(int index, int x, int y, int z, int level, RandomSource& random) : MinotaurMaze(index, x, y, z, 11, 11, level, random) {}

MinotaurMaze::MinotaurMaze(Json const& saved) : ComponentOld(kMaze, 0, 0, 0, 0) {
    load(saved);
    mLevel = saved.value("mazeLevel", 1);
    if (auto it = saved.find("roomCoords"); it != saved.end() && it->is_array())
        for (std::size_t i = 0; i < mRcoords.size() && i < it->size(); ++i) mRcoords[i] = (*it)[i].get<int>();

    // recreate maze object
    ::gf::core::jrand::LegacyRandom random(getSeed());
    mMaze = std::make_unique<::gf::structure::bases::Maze>(getMazeSize(), getMazeSize(), random);
    configureMaze();

    // blank out rcoords above 1 so that the room generation works properly
    for (std::size_t i = 2; i < mRcoords.size(); i++) mRcoords[i] = 0;

    // recreate rooms
    addRoomsToMaze(mRcoords[0], mRcoords[1], static_cast<int>(mRcoords.size() + 1) / 2, random);

    // regenerate maze
    mMaze->generateRecursiveBacktracker(0, 0, random);
}

Json MinotaurMaze::save() const {
    Json j          = ComponentOld::save();
    j["mazeLevel"]  = mLevel;
    j["roomCoords"] = Json(mRcoords);
    return j;
}

long long MinotaurMaze::getSeed() const { return cl::mazeSeed(bbox); }

void MinotaurMaze::addRoomsToMaze(int entranceX, int entranceZ, int nrooms, RandomSource& rand) {
    // make one entrance room always
    mRcoords[0] = entranceX;
    mRcoords[1] = entranceZ;
    mMaze->carveRoom1(entranceX, entranceZ);

    // add room coordinates, trying to keep them separate from existing rooms
    for (int i = 1; i < nrooms; i++) {
        int rx, rz;
        do {
            rx = rand.nextInt(getMazeSize() - 2) + 1;
            rz = rand.nextInt(getMazeSize() - 2) + 1;
        } while (cl::isNearRoom(rx, rz, mRcoords.data(), static_cast<int>(mRcoords.size() / 2), i == 1 ? 7 : 4));

        mMaze->carveRoom1(rx, rz);

        mRcoords[i * 2]     = rx;
        mRcoords[i * 2 + 1] = rz;
    }
}

void MinotaurMaze::configureMaze() {
    mMaze->headBlockState   = &decorativeMazestone();
    mMaze->wallBlockState   = &mazestoneBrick();
    mMaze->rootBlockState   = &decorativeMazestone();
    mMaze->pillarBlockState = &cutMazestone();
    mMaze->wallBlocks       = ::gf::structure::selectors::get("gloamingforest:mazestone");
    mMaze->torchRarity      = 0.05F;
    mMaze->tall             = 2;
    mMaze->head             = 1;
    mMaze->roots            = 1;
    mMaze->oddBias          = 4;
}

std::unique_ptr<Piece> MinotaurMaze::makeRoom(RandomSource& random, int i, int dx, int dz) const {
    int worldX = bbox.minX + dx * 5 - 4;
    int worldY = bbox.minY;
    int worldZ = bbox.minZ + dz * 5 - 4;
    return std::make_unique<Room>(cl::roomType(i, mLevel), 3 + i, random, worldX, worldY, worldZ);
}

std::unique_ptr<Piece> MinotaurMaze::makeDeadEnd(RandomSource& random, int dx, int dz, Orientation rotation) const {
    int worldX = bbox.minX + dx * 5 + 1;
    int worldY = bbox.minY;
    int worldZ = bbox.minZ + dz * 5 + 1;

    int  decorationType = random.nextInt(8);
    bool tripwire       = decorationType == 2 && random.nextBoolean();
    return std::make_unique<Cell>(cl::deadEndType(decorationType, tripwire, mLevel), 4, worldX, worldY, worldZ, rotation);
}

std::unique_ptr<Piece> MinotaurMaze::makeCorridor(RandomSource& random, int dx, int dz, Orientation rotation) const {
    int worldX = bbox.minX + dx * 5 + 1;
    int worldY = bbox.minY;
    int worldZ = bbox.minZ + dz * 5 + 1;

    int         decorationType = random.nextInt(5);
    char const* type           = cl::corridorType(decorationType, mLevel);
    if (!type) return nullptr;
    return std::make_unique<Cell>(type, 4, worldX, worldY, worldZ, rotation);
}

void MinotaurMaze::decorateDeadEndsCorridors(RandomSource& random, PieceList& list) {
    auto const& m = *mMaze;
    for (int x = 0; x < m.width; x++) {
        for (int z = 0; z < m.depth; z++) {
            std::unique_ptr<Piece> component;

            // dead ends
            if (!m.isWall(x, z, x - 1, z) && m.isWall(x, z, x + 1, z) && m.isWall(x, z, x, z - 1) && m.isWall(x, z, x, z + 1))
                component = makeDeadEnd(random, x, z, Orientation::East);
            if (m.isWall(x, z, x - 1, z) && !m.isWall(x, z, x + 1, z) && m.isWall(x, z, x, z - 1) && m.isWall(x, z, x, z + 1))
                component = makeDeadEnd(random, x, z, Orientation::West);
            if (m.isWall(x, z, x - 1, z) && m.isWall(x, z, x + 1, z) && !m.isWall(x, z, x, z - 1) && m.isWall(x, z, x, z + 1))
                component = makeDeadEnd(random, x, z, Orientation::South);
            if (m.isWall(x, z, x - 1, z) && m.isWall(x, z, x + 1, z) && m.isWall(x, z, x, z - 1) && !m.isWall(x, z, x, z + 1))
                component = makeDeadEnd(random, x, z, Orientation::North);

            // corridors
            if (!m.isWall(x, z, x - 1, z) && !m.isWall(x, z, x + 1, z) && m.isWall(x, z, x, z - 1) && m.isWall(x, z, x, z + 1) && m.isWall(x - 1, z, x - 1, z - 1)
                && m.isWall(x - 1, z, x - 1, z + 1) && m.isWall(x + 1, z, x + 1, z - 1) && m.isWall(x + 1, z, x + 1, z + 1))
                component = makeCorridor(random, x, z, Orientation::West);
            if (!m.isWall(x, z, x, z - 1) && !m.isWall(x, z, x, z + 1) && m.isWall(x, z, x - 1, z) && m.isWall(x, z, x + 1, z) && m.isWall(x, z - 1, x - 1, z - 1)
                && m.isWall(x, z - 1, x + 1, z - 1) && m.isWall(x, z + 1, x - 1, z + 1) && m.isWall(x, z + 1, x + 1, z + 1))
                component = makeCorridor(random, x, z, Orientation::South);

            if (component) addPiece(list, std::move(component), *this, random);
        }
    }
}

void MinotaurMaze::addChildren(Piece&, PieceList& list, RandomSource& random) {
    // add a second story
    if (mLevel == 1) {
        int centerX = bbox.minX + ((bbox.maxX - bbox.minX) / 2);
        int centerZ = bbox.minZ + ((bbox.maxZ - bbox.minZ) / 2);
        addPiece(list, std::make_unique<MinotaurMaze>(1, centerX, bbox.minY - 10, centerZ, mRcoords[2], mRcoords[3], 2, random), *this, random);
    }

    // add rooms where we have our coordinates
    for (int i = 0; i < static_cast<int>(mRcoords.size()) / 2; i++) addPiece(list, makeRoom(random, i, mRcoords[i * 2], mRcoords[i * 2 + 1]), *this, random);

    // find dead ends and corridors and make components for them
    decorateDeadEndsCorridors(random, list);
}

void MinotaurMaze::postProcess(Region& world, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    Block const& bedrock = J("minecraft:bedrock");
    Block const& stone   = J("minecraft:stone");

    // level 2 maze surrounded by bedrock
    if (mLevel == 2) fill(world, sbb, 0, -1, 0, getDiameter() + 2, 6, getDiameter() + 2, bedrock, air(), false);

    // clear the area
    fillAir(world, sbb, 1, 1, 1, getDiameter(), 4, getDiameter());
    bool onlyReplaceCeiling = mLevel == 1;
    fill(world, sbb, 1, 5, 1, getDiameter() + 1, 5, getDiameter() + 1, mazestone(), stone, onlyReplaceCeiling);
    fill(world, sbb, 1, 0, 1, getDiameter() + 1, 0, getDiameter() + 1, mazestoneMosaic(), stone, false);

    mMaze->copyToStructure(world, 1, 2, 1, *this, sbb, rand);
}

// ==== MazeRoomComponent and subclasses =============================================================================

Room::Room(std::string typeId, int i, RandomSource& rand, int x, int y, int z) : ComponentOld(std::move(typeId), i, x, y, z) {
    setOrientation(randomHorizontal(rand));
    bbox = BlockBox{x, y, z, x + 15, y + 4, z + 15};
    if (this->typeId == kMushRoom) setOrientation(Orientation::South); // let's just make this easy on us?
    // specify a non-existant high spawn list value to stop actual monster spawns
    if (this->typeId == kRoomBoss || this->typeId == kRoomVault) spawnListIndex = INT_MAX;
}

void Room::postProcess(Region& world, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    if (typeId == kRoomBoss) return bossRoom(world, rand, sbb);
    if (typeId == kRoomVault) return vaultRoom(world, rand, sbb);
    roomBase(world, sbb);
    if (typeId == kRoomExit) exitRoom(world, sbb);
    else if (typeId == kRoomCollapse) collapseRoom(world, rand, sbb);
    else if (typeId == kMushRoom) mushRoom(world, rand, sbb);
    else if (typeId == kRoomFountain) fountainRoom(world, sbb);
    else if (typeId == kRoomSpawner) spawnerChestsRoom(world, sbb);
}

void Room::roomBase(Region& world, BlockBox const& sbb) {
    // floor border
    fill(world, sbb, 1, 0, 1, 14, 0, 14, mazestoneBorder(), air(), true);
    fill(world, sbb, 2, 0, 2, 13, 0, 13, mazestoneMosaic(), air(), true);

    // doorways
    Block const& fence = J("minecraft:oak_fence");
    if (isPlainAir(getBlock(world, 7, 1, 0, sbb))) {
        fill(world, sbb, 6, 1, 0, 9, 4, 0, fence, air(), false);
        fillAir(world, sbb, 7, 1, 0, 8, 3, 0);
    }
    if (isPlainAir(getBlock(world, 7, 1, 15, sbb))) {
        fill(world, sbb, 6, 1, 15, 9, 4, 15, fence, air(), false);
        fillAir(world, sbb, 7, 1, 15, 8, 3, 15);
    }
    if (isPlainAir(getBlock(world, 0, 1, 7, sbb))) {
        fill(world, sbb, 0, 1, 6, 0, 4, 9, fence, air(), false);
        fillAir(world, sbb, 0, 1, 7, 0, 3, 8);
    }
    if (isPlainAir(getBlock(world, 15, 1, 7, sbb))) {
        fill(world, sbb, 15, 1, 6, 15, 4, 9, fence, air(), false);
        fillAir(world, sbb, 15, 1, 7, 15, 3, 8);
    }
}

void Room::exitRoom(Region& world, BlockBox const& sbb) {
    // shaft down
    fill(world, sbb, 5, -5, 5, 10, 0, 10, mazestoneBrick(), air(), false);
    fill(world, sbb, 5, 1, 5, 10, 1, 10, decorativeMazestone(), air(), false);
    fill(world, sbb, 5, 2, 5, 10, 3, 10, J("minecraft:iron_bars"), air(), false);
    fill(world, sbb, 5, 4, 5, 10, 4, 10, decorativeMazestone(), air(), false);
    fillAir(world, sbb, 6, -5, 6, 9, 4, 9);
}

void Room::bossRoom(Region& world, RandomSource& rand, BlockBox const& sbb) {
    // doorways
    Block const& fence = J("minecraft:oak_fence");
    if (isPlainAir(getBlock(world, 7, 1, 0, sbb))) fill(world, sbb, 6, 1, 0, 9, 4, 0, fence, air(), false);
    if (isPlainAir(getBlock(world, 7, 1, 15, sbb))) fill(world, sbb, 6, 1, 15, 9, 4, 15, fence, air(), false);
    if (isPlainAir(getBlock(world, 0, 1, 7, sbb))) fill(world, sbb, 0, 1, 6, 0, 4, 9, fence, air(), false);
    if (isPlainAir(getBlock(world, 15, 1, 7, sbb))) fill(world, sbb, 15, 1, 6, 15, 4, 9, fence, air(), false);

    // mycelium / small mushrooms on floor
    for (int x = 1; x < 14; x++) {
        for (int z = 1; z < 14; z++) {
            // calculate distance from middle
            int  dist     = static_cast<int>(std::llround(7.0 / std::sqrt((7.5 - x) * (7.5 - x) + (7.5 - z) * (7.5 - z))));
            bool mycelium = rand.nextInt(dist + 1) > 0;
            bool mushroom = rand.nextInt(dist) > 0;
            bool mushRed  = rand.nextBoolean();

            // make part of the floor mycelium
            if (mycelium) placeBlock(world, J("minecraft:mycelium"), x, 0, z, sbb);
            // add small mushrooms all over
            if (mushroom) placeBlock(world, J(mushRed ? "minecraft:red_mushroom" : "minecraft:brown_mushroom"), x, 1, z, sbb);
        }
    }

    // mushroom chest shelves in corner
    Block const& redMushroom   = capBlock(*this, true);
    Block const& brownMushroom = capBlock(*this, false);

    fill(world, sbb, 1, 1, 1, 3, 1, 3, redMushroom, air(), false);
    fill(world, sbb, 1, 2, 1, 1, 3, 4, redMushroom, air(), false);
    fill(world, sbb, 2, 2, 1, 4, 3, 1, redMushroom, air(), false);
    fill(world, sbb, 1, 4, 1, 3, 4, 3, redMushroom, air(), false);
    placeTreasureAtCurrentPosition(world, 3, 2, 3, kLootRoom, sbb);

    fill(world, sbb, 12, 1, 12, 14, 1, 14, redMushroom, air(), false);
    fill(world, sbb, 14, 2, 11, 14, 3, 14, redMushroom, air(), false);
    fill(world, sbb, 11, 2, 14, 14, 3, 14, redMushroom, air(), false);
    fill(world, sbb, 12, 4, 12, 14, 4, 14, redMushroom, air(), false);
    placeTreasureAtCurrentPosition(world, 12, 2, 12, kLootRoom, sbb);

    fill(world, sbb, 1, 1, 12, 3, 1, 14, redMushroom, air(), false);
    fill(world, sbb, 1, 2, 11, 1, 3, 14, redMushroom, air(), false);
    fill(world, sbb, 2, 2, 14, 4, 3, 14, redMushroom, air(), false);
    fill(world, sbb, 1, 4, 12, 3, 4, 14, redMushroom, air(), false);
    placeTreasureAtCurrentPosition(world, 3, 2, 12, kLootRoom, sbb);

    fill(world, sbb, 12, 1, 1, 14, 1, 3, brownMushroom, air(), false);
    fill(world, sbb, 11, 2, 1, 14, 3, 1, brownMushroom, air(), false);
    fill(world, sbb, 14, 2, 2, 14, 3, 4, brownMushroom, air(), false);
    fill(world, sbb, 12, 4, 1, 14, 4, 3, brownMushroom, air(), false);
    placeTreasureAtCurrentPosition(world, 12, 2, 3, kLootRoom, sbb);

    // a few more ceilingshrooms
    fill(world, sbb, 5, 4, 5, 7, 5, 7, brownMushroom, air(), false);
    fill(world, sbb, 8, 4, 8, 10, 5, 10, redMushroom, air(), false);

    // the moo-cen-mino-shrom-taur!
    setBlockStateRotated(world, J(kBossSpawner), 7, 2, 7, Rotation::None, sbb);
}

void Room::collapseRoom(Region& world, RandomSource& rand, BlockBox const& sbb) {
    for (int x = 1; x < 14; x++) {
        for (int z = 1; z < 14; z++) {
            // calculate distance from middle
            int dist   = static_cast<int>(std::llround(7.0 / std::sqrt((7.5 - x) * (7.5 - x) + (7.5 - z) * (7.5 - z))));
            int gravel = rand.nextInt(dist);
            int root   = rand.nextInt(dist);

            if (gravel > 0) {
                gravel++; // get it out of the floor
                fill(world, sbb, x, 1, z, x, gravel, z, J("minecraft:gravel"), air(), false);
                fillAir(world, sbb, x, gravel, z, x, gravel + 5, z);
            } else if (root > 0) {
                fill(world, sbb, x, 5, z, x, 5 + root, z, J("minecraft:dirt"), air(), true);
                fill(world, sbb, x, 5 - rand.nextInt(5), z, x, 5, z, J("gloamingforest:root_strand"), air(), false);
            } else if (rand.nextInt(dist + 1) > 0) {
                // remove ceiling
                fillAir(world, sbb, x, 5, z, x, 5, z);
            }
        }
    }
}

void Room::mushRoom(Region& world, RandomSource& rand, BlockBox const& sbb) {
    for (int x = 1; x < 14; x++) {
        for (int z = 1; z < 14; z++) {
            // calculate distance from middle
            int dist = static_cast<int>(std::llround(7.0 / std::sqrt((7.5 - x) * (7.5 - x) + (7.5 - z) * (7.5 - z))));

            // make part of the floor mycelium
            if (rand.nextInt(dist + 1) > 0) placeBlock(world, J("minecraft:mycelium"), x, 0, z, sbb);
            // add small mushrooms all over
            if (rand.nextInt(dist) > 0) placeBlock(world, J(rand.nextBoolean() ? "minecraft:red_mushroom" : "minecraft:brown_mushroom"), x, 1, z, sbb);
        }
    }

    // add our medium mushrooms
    makeMediumMushroom(world, sbb, 5, 2, 9, true);
    makeMediumMushroom(world, sbb, 5, 3, 9, true);
    makeMediumMushroom(world, sbb, 9, 2, 5, true);
    makeMediumMushroom(world, sbb, 6, 3, 4, false);
    makeMediumMushroom(world, sbb, 10, 1, 9, false);

    // bracket mushrooms on the wall
    placeBlock(world, fullStem(*this), 1, 2, 1, sbb);
    placeBlock(world, redShroom(*this, Shroom::Center), 1, 3, 1, sbb);
    placeBlock(world, redShroom(*this, Shroom::NorthWest), 2, 3, 1, sbb);
    placeBlock(world, redShroom(*this, Shroom::NorthWest), 1, 3, 2, sbb);

    placeBlock(world, fullStem(*this), 14, 3, 1, sbb);
    placeBlock(world, brownShroom(*this, Shroom::Center), 14, 4, 1, sbb);
    placeBlock(world, brownShroom(*this, Shroom::NorthEast), 13, 4, 1, sbb);
    placeBlock(world, brownShroom(*this, Shroom::NorthEast), 14, 4, 2, sbb);

    placeBlock(world, fullStem(*this), 1, 1, 14, sbb);
    placeBlock(world, brownShroom(*this, Shroom::Center), 1, 2, 14, sbb);
    placeBlock(world, brownShroom(*this, Shroom::SouthWest), 2, 2, 14, sbb);
    placeBlock(world, brownShroom(*this, Shroom::SouthWest), 1, 2, 13, sbb);

    placeBlock(world, brownShroom(*this, Shroom::Center), 14, 1, 14, sbb);
    placeBlock(world, brownShroom(*this, Shroom::SouthEast), 13, 1, 14, sbb);
    placeBlock(world, brownShroom(*this, Shroom::SouthEast), 14, 1, 13, sbb);

    // mushroom ceiling spots?
}

void Room::makeMediumMushroom(Region& world, BlockBox const& sbb, int mx, int my, int mz, bool red) {
    auto shroom = [&](Shroom t) -> Block const& { return red ? redShroom(*this, t) : brownShroom(*this, t); };
    Block const& mushroomStem = sideStem(*this);

    // cap
    placeBlock(world, shroom(Shroom::Center), mx, my, mz, sbb);
    placeBlock(world, shroom(Shroom::West), mx + 1, my, mz, sbb);
    placeBlock(world, shroom(Shroom::NorthWest), mx + 1, my, mz + 1, sbb);
    placeBlock(world, shroom(Shroom::North), mx, my, mz + 1, sbb);
    placeBlock(world, shroom(Shroom::NorthEast), mx - 1, my, mz + 1, sbb);
    placeBlock(world, shroom(Shroom::East), mx - 1, my, mz, sbb);
    placeBlock(world, shroom(Shroom::SouthEast), mx - 1, my, mz - 1, sbb);
    placeBlock(world, shroom(Shroom::South), mx, my, mz - 1, sbb);
    placeBlock(world, shroom(Shroom::SouthWest), mx + 1, my, mz - 1, sbb);

    // stem
    for (int y = 1; y < my; y++) placeBlock(world, mushroomStem, mx, y, mz, sbb);
}

void Room::fountainRoom(Region& world, BlockBox const& sbb) {
    fill(world, sbb, 5, 1, 5, 10, 1, 10, decorativeMazestone(), air(), false);
    fill(world, sbb, 6, 1, 6, 9, 1, 9, J("minecraft:water"), air(), false);
}

void Room::vaultRoom(Region& world, RandomSource& rand, BlockBox const& sbb) {
    // fill room with bricks
    fill(world, sbb, 0, 1, 0, 15, 4, 15, decorativeMazestone(), air(), false);
    fill(world, sbb, 0, 2, 0, 15, 3, 15, mazestoneBrick(), air(), false);

    // 4x4 room in the middle
    fillAir(world, sbb, 6, 2, 6, 9, 3, 9);

    // pressure plates, sand & tnt
    Block const& plate = J("minecraft:oak_pressure_plate");
    fill(world, sbb, 6, 2, 5, 9, 2, 5, plate, air(), false);
    fill(world, sbb, 6, 2, 10, 9, 2, 10, plate, air(), false);
    fill(world, sbb, 5, 2, 6, 5, 2, 9, plate, air(), false);
    fill(world, sbb, 10, 2, 6, 10, 2, 9, plate, air(), false);

    // unfair sand
    Block const& sand = J("minecraft:sand");
    fill(world, sbb, 6, 4, 5, 9, 4, 5, sand, air(), false);
    fill(world, sbb, 6, 4, 10, 9, 4, 10, sand, air(), false);
    fill(world, sbb, 5, 4, 6, 5, 4, 9, sand, air(), false);
    fill(world, sbb, 10, 4, 6, 10, 4, 9, sand, air(), false);

    Block const& tnt = J("minecraft:tnt");
    fill(world, sbb, 6, 0, 5, 9, 0, 5, tnt, air(), false);
    fill(world, sbb, 6, 0, 10, 9, 0, 10, tnt, air(), false);
    fill(world, sbb, 5, 0, 6, 5, 0, 9, tnt, air(), false);
    fill(world, sbb, 10, 0, 6, 10, 0, 9, tnt, air(), false);

    // LEWTZ!
    int i = rand.nextInt(4); // Set the index of the jackpot chest, so it's not always the same one

    setVaultDoubleLootChest(world, 7, 6, 8, 6, Orientation::South, sbb, i == 0);
    setVaultDoubleLootChest(world, 8, 9, 7, 9, Orientation::North, sbb, i == 1);
    setVaultDoubleLootChest(world, 6, 8, 6, 7, Orientation::East, sbb, i == 2);
    setVaultDoubleLootChest(world, 9, 7, 9, 8, Orientation::West, sbb, i == 3);
}

void Room::setVaultDoubleLootChest(Region& world, int x, int z, int x2, int z2, Orientation facing, BlockBox const& sbb, bool jackpot) {
    setDoubleLootChest(world, x, 2, z, x2, 2, z2, facing, jackpot ? kLootJackpot : kLootVault, kLootVault, sbb, false);
}

void Room::spawnerChestsRoom(Region& world, BlockBox const& sbb) {
    // 4 pillar enclosures
    placePillarEnclosure(world, sbb, 3, 3);
    placePillarEnclosure(world, sbb, 10, 3);
    placePillarEnclosure(world, sbb, 3, 10);
    placePillarEnclosure(world, sbb, 10, 10);

    // spawner
    setSpawner(world, 4, 2, 4, sbb, kMinotaur);

    // treasure
    placeTreasureAtCurrentPosition(world, 4, 2, 11, kLootRoom, sbb);
    // treasure
    placeTreasureAtCurrentPosition(world, 11, 2, 4, kLootRoom, sbb);

    // trap
    Block const& tnt = J("minecraft:tnt");
    placeBlock(world, J("minecraft:oak_pressure_plate"), 11, 1, 11, sbb);
    placeBlock(world, tnt, 10, 0, 11, sbb);
    placeBlock(world, tnt, 11, 0, 10, sbb);
    placeBlock(world, tnt, 11, 0, 12, sbb);
    placeBlock(world, tnt, 12, 0, 11, sbb);
}

void Room::placePillarEnclosure(Region& world, BlockBox const& sbb, int dx, int dz) {
    for (int y = 1; y < 5; y++) {
        Block const& chiselledMazeBlock = cutMazestone();
        placeBlock(world, chiselledMazeBlock, dx, y, dz, sbb);
        placeBlock(world, chiselledMazeBlock, dx + 2, y, dz, sbb);
        placeBlock(world, chiselledMazeBlock, dx, y, dz + 2, sbb);
        placeBlock(world, chiselledMazeBlock, dx + 2, y, dz + 2, sbb);
    }
    placeBlock(world, J("minecraft:oak_planks"), dx + 1, 1, dz + 1, sbb);
    placeBlock(world, J("minecraft:oak_planks"), dx + 1, 4, dz + 1, sbb);

    Block const& defaultState = J("minecraft:oak_stairs");

    placeBlock(world, getStairState(defaultState, Orientation::North, false), dx + 1, 1, dz, sbb);
    placeBlock(world, getStairState(defaultState, Orientation::West, false), dx, 1, dz + 1, sbb);
    placeBlock(world, getStairState(defaultState, Orientation::East, false), dx + 2, 1, dz + 1, sbb);
    placeBlock(world, getStairState(defaultState, Orientation::South, false), dx + 1, 1, dz + 2, sbb);

    placeBlock(world, getStairState(defaultState, Orientation::North, true), dx + 1, 4, dz, sbb);
    placeBlock(world, getStairState(defaultState, Orientation::West, true), dx, 4, dz + 1, sbb);
    placeBlock(world, getStairState(defaultState, Orientation::East, true), dx + 2, 4, dz + 1, sbb);
    placeBlock(world, getStairState(defaultState, Orientation::South, true), dx + 1, 4, dz + 2, sbb);

    Block const& bars = J("minecraft:iron_bars");
    placeBlock(world, bars, dx + 1, 2, dz, sbb);
    placeBlock(world, bars, dx, 2, dz + 1, sbb);
    placeBlock(world, bars, dx + 2, 2, dz + 1, sbb);
    placeBlock(world, bars, dx + 1, 2, dz + 2, sbb);
    placeBlock(world, bars, dx + 1, 3, dz, sbb);
    placeBlock(world, bars, dx, 3, dz + 1, sbb);
    placeBlock(world, bars, dx + 2, 3, dz + 1, sbb);
    placeBlock(world, bars, dx + 1, 3, dz + 2, sbb);
}

// ==== MazeDeadEndComponent / MazeCorridorComponent and subclasses ===================================================

Cell::Cell(std::string typeId, int i, int x, int y, int z, Orientation rotation) : ComponentOld(std::move(typeId), i, x, y, z) {
    setOrientation(rotation);
    bbox = BlockBox{x, y, z, x + 5, y + 5, z + 5};
    // specify a non-existant high spawn list value to stop actual monster spawns
    if (deniesSpawns(this->typeId)) spawnListIndex = INT_MAX;
}

bool deniesSpawns(std::string const& t) { return t == kDeadEndChest || t == kDeadEndTrap || t == kDeadEndTrip || t == kRoomBoss || t == kRoomVault; }

void Cell::postProcess(Region& world, RandomSource& rand, BlockBox const& sbb, BlockPos const&) {
    if (typeId == kDeadEnd) deadEnd(world, sbb);
    else if (typeId == kDeadEndChest) deadEndChest(world, sbb, false);
    else if (typeId == kDeadEndTrap) deadEndTrapped(world, sbb);
    else if (typeId == kDeadEndTrip) deadEndTripwire(world, sbb);
    else if (typeId == kDeadEndTorch) deadEndTorches(world, sbb);
    else if (typeId == kDeadEndWater) deadEndFountain(world, sbb);
    else if (typeId == kDeadEndLava) deadEndFountainLava(world, sbb);
    else if (typeId == kDeadEndPaint) deadEndPainting(world, sbb);
    else if (typeId == kDeadEndRoots) deadEndRoots(world, rand, sbb);
    else if (typeId == kDeadEndShroom) deadEndShrooms(world, rand, sbb);
    else if (typeId == kCorridor) corridor(world, sbb);
    else if (typeId == kCorridorIron) corridorIronFence(world, sbb);
    else if (typeId == kCorridorRoots) corridorRoots(world, rand, sbb);
    else if (typeId == kCorridorShroom) corridorShrooms(world, rand, sbb);
}

void Cell::deadEnd(Region& world, BlockBox const& sbb) {
    fill(world, sbb, 1, 1, 0, 4, 4, 0, J("minecraft:oak_fence"), air(), false);
    fillAir(world, sbb, 2, 1, 0, 3, 3, 0);
}

void Cell::deadEndChest(Region& world, BlockBox const& sbb, bool trapped) {
    // dais
    placeBlock(world, J("minecraft:oak_planks"), 2, 1, 4, sbb);
    placeBlock(world, J("minecraft:oak_planks"), 3, 1, 4, sbb);
    placeBlock(world, getStairState(J("minecraft:oak_stairs"), Orientation::North, false), 2, 1, 3, sbb);
    placeBlock(world, getStairState(J("minecraft:oak_stairs"), Orientation::North, false), 3, 1, 3, sbb);

    // chest
    setDoubleLootChest(world, 2, 2, 4, 3, 2, 4, Orientation::South, kLootDeadEnd, sbb, trapped);

    // doorway w/ bars
    fill(world, sbb, 1, 1, 0, 4, 3, 1, cutMazestone(), air(), false);
    fill(world, sbb, 1, 4, 0, 4, 4, 1, decorativeMazestone(), air(), false);
    fill(world, sbb, 2, 1, 0, 3, 3, 1, J("minecraft:iron_bars"), air(), false);
}

void Cell::deadEndTrapped(Region& world, BlockBox const& sbb) {
    deadEndChest(world, sbb, true);

    // TNT!
    Block const& tnt = J("minecraft:tnt");
    placeBlock(world, tnt, 2, 0, 3, sbb);
    placeBlock(world, tnt, 3, 0, 3, sbb);
    placeBlock(world, tnt, 2, 0, 4, sbb);
    placeBlock(world, tnt, 3, 0, 4, sbb);
}

void Cell::deadEndTripwire(Region& world, BlockBox const& sbb) {
    // normal chest room
    deadEndChest(world, sbb, false);

    // add tripwire
    placeTripwire(world, 1, 1, 2, 3, Orientation::East, sbb);

    // TNT! Since the trap requires tripwire, we'll make punching the tnt NOT the solution
    Block const& tnt = unstableTnt();
    placeBlock(world, tnt, 0, 0, 2, sbb);

    // Air blocks are required underneath to maximize TNT destruction of chest
    placeBlock(world, air(), 0, -1, 2, sbb);
    placeBlock(world, air(), 1, -1, 2, sbb);

    placeBlock(world, tnt, 2, 0, 4, sbb);
    placeBlock(world, tnt, 3, 0, 4, sbb);
    placeBlock(world, tnt, 2, 0, 3, sbb);
    placeBlock(world, tnt, 3, 0, 3, sbb);
}

void Cell::deadEndTorches(Region& world, BlockBox const& sbb) {
    // normal doorway
    deadEnd(world, sbb);

    // torches!
    fill(world, sbb, 2, 1, 4, 3, 4, 4, J("minecraft:wall_torch", Json{{"facing", "south"}}), air(), false);
    fill(world, sbb, 1, 1, 1, 1, 4, 4, J("minecraft:wall_torch", Json{{"facing", "west"}}), air(), false);
    fill(world, sbb, 4, 1, 1, 4, 4, 4, J("minecraft:wall_torch", Json{{"facing", "east"}}), air(), false);
}

void Cell::deadEndFountain(Region& world, BlockBox const& sbb) {
    // normal doorway
    deadEnd(world, sbb);
    // back wall brick
    fill(world, sbb, 1, 1, 4, 4, 4, 4, mazestoneBrick(), air(), false);
    // water
    placeBlock(world, J("minecraft:water"), 2, 3, 4, sbb);
    placeBlock(world, J("minecraft:water"), 3, 3, 4, sbb);
    // receptacle
    placeBlock(world, air(), 2, 0, 3, sbb);
    placeBlock(world, air(), 3, 0, 3, sbb);
}

void Cell::deadEndFountainLava(Region& world, BlockBox const& sbb) {
    // normal fountain
    deadEndFountain(world, sbb);
    // remove water
    placeBlock(world, air(), 2, 3, 4, sbb);
    placeBlock(world, air(), 3, 3, 4, sbb);
    // lava instead of water
    placeBlock(world, J("minecraft:lava"), 2, 3, 4, sbb);
    placeBlock(world, J("minecraft:lava"), 3, 3, 4, sbb);
}

void Cell::deadEndPainting(Region& world, BlockBox const& sbb) {
    // normal doorway
    deadEnd(world, sbb);
    // torches (upstream's painting is commented out)
    placeBlock(world, J("minecraft:wall_torch", Json{{"facing", "west"}}), 1, 3, 3, sbb);
    placeBlock(world, J("minecraft:wall_torch", Json{{"facing", "east"}}), 4, 3, 3, sbb);
}

void Cell::deadEndRoots(Region& world, RandomSource& rand, BlockBox const& sbb) {
    // no door
    for (int x = 1; x < 5; x++) {
        for (int z = 0; z < 5; z++) {
            // as we go +z, there are more vines
            if (rand.nextInt(z + 2) > 0) {
                int length = rand.nextInt(6);
                // place dirt above ceiling
                placeBlock(world, J("minecraft:dirt"), x, 6, z, sbb);
                // roots
                for (int y = 6 - length; y < 6; y++) placeBlock(world, J("gloamingforest:root_strand"), x, y, z, sbb);
                // occasional gravel
                if (rand.nextInt(z + 1) > 1) {
                    placeBlock(world, J("minecraft:gravel"), x, 1, z, sbb);
                    if (rand.nextInt(z + 1) > 1) placeBlock(world, J("minecraft:gravel"), x, 2, z, sbb);
                }
            }
        }
    }
}

void Cell::deadEndShrooms(Region& world, RandomSource& rand, BlockBox const& sbb) {
    // no door
    // mycelium & mushrooms going back
    for (int x = 1; x < 5; x++) {
        for (int z = 0; z < 5; z++) {
            if (rand.nextInt(z + 2) > 0) placeBlock(world, J("minecraft:mycelium"), x, 0, z, sbb);
            if (rand.nextInt(z + 2) > 0) placeBlock(world, J(rand.nextBoolean() ? "minecraft:red_mushroom" : "minecraft:brown_mushroom"), x, 1, z, sbb);
        }
    }

    // brackets?
    bool         mushFlag = rand.nextBoolean();
    Block const* mushType = &capBlock(*this, mushFlag);
    Block const& full     = fullStem(*this);
    Block const& stem     = sideStem(*this);
    int          mushY    = rand.nextInt(4) + 1;
    int          mushZ    = rand.nextInt(3) + 1;
    placeBlock(world, full, 1, mushY - 1, mushZ, sbb);
    fill(world, sbb, 1, 1, mushZ, 1, mushY, mushZ, stem, air(), false);
    fill(world, sbb, 1, mushY, mushZ - 1, 2, mushY, mushZ + 1, *mushType, air(), false);

    mushType = &capBlock(*this, !mushFlag);
    mushY    = rand.nextInt(4) + 1;
    mushZ    = rand.nextInt(3) + 1;
    fill(world, sbb, 4, 1, mushZ, 4, mushY, mushZ, stem, air(), false);
    fill(world, sbb, 3, mushY, mushZ - 1, 4, mushY, mushZ + 1, *mushType, air(), false);

    mushType  = &capBlock(*this, rand.nextBoolean());
    mushY     = rand.nextInt(4) + 1;
    int mushX = rand.nextInt(3) + 2;
    fill(world, sbb, mushX, 1, 4, mushX, mushY, 4, stem, air(), false);
    fill(world, sbb, mushX - 1, mushY, 3, mushX + 1, mushY, 4, *mushType, air(), false);
}

void Cell::corridor(Region& world, BlockBox const& sbb) {
    // arch
    fill(world, sbb, 1, 1, 2, 4, 4, 3, J("minecraft:oak_fence"), air(), false);
    fillAir(world, sbb, 2, 1, 2, 3, 3, 3);
}

void Cell::corridorIronFence(Region& world, BlockBox const& sbb) {
    fill(world, sbb, 1, 4, 2, 4, 4, 3, decorativeMazestone(), air(), false);
    fill(world, sbb, 1, 1, 2, 4, 3, 3, cutMazestone(), air(), false);
    fill(world, sbb, 2, 1, 2, 3, 3, 3, J("minecraft:iron_bars"), air(), false);
}

void Cell::corridorRoots(Region& world, RandomSource& rand, BlockBox const& sbb) {
    for (int x = 1; x < 5; x++) {
        for (int z = 0; z < 5; z++) {
            int freq = x;
            if (rand.nextInt(freq + 2) > 0) {
                int length = rand.nextInt(6);
                // place dirt above ceiling
                placeBlock(world, J("minecraft:dirt"), x, 6, z, sbb);
                // roots
                for (int y = 6 - length; y < 6; y++) placeBlock(world, J("gloamingforest:root_strand"), x, y, z, sbb);
                // occasional gravel
                if (rand.nextInt(freq + 1) > 1) {
                    placeBlock(world, J("minecraft:gravel"), x, 1, z, sbb);
                    if (rand.nextInt(freq + 1) > 1) placeBlock(world, J("minecraft:gravel"), x, 2, z, sbb);
                }
            }
        }
    }
}

void Cell::corridorShrooms(Region& world, RandomSource& rand, BlockBox const& sbb) {
    // mycelium & mushrooms
    for (int x = 1; x < 5; x++) {
        for (int z = 0; z < 5; z++) {
            if (rand.nextInt(2) > 0) placeBlock(world, J("minecraft:mycelium"), x, 0, z, sbb);
            if (rand.nextInt(2) > 0) placeBlock(world, J(rand.nextBoolean() ? "minecraft:red_mushroom" : "minecraft:brown_mushroom"), x, 1, z, sbb);
        }
    }

    // brackets?
    bool         mushFlag = rand.nextBoolean();
    Block const* mushType = &capBlock(*this, mushFlag);
    Block const& full     = fullStem(*this);
    Block const& stem     = sideStem(*this);
    int          mushY    = rand.nextInt(4) + 1;
    int          mushZ    = rand.nextInt(4) + 1;
    placeBlock(world, full, 1, mushY - 1, mushZ, sbb);
    fill(world, sbb, 1, 1, mushZ, 1, mushY, mushZ, stem, air(), false);
    fill(world, sbb, 1, mushY, mushZ - 1, 2, mushY, mushZ + 1, *mushType, air(), false);

    mushType = &capBlock(*this, !mushFlag);
    mushY    = rand.nextInt(4) + 1;
    mushZ    = rand.nextInt(4) + 1;
    fill(world, sbb, 4, 1, mushZ, 4, mushY, mushZ, stem, air(), false);
    fill(world, sbb, 3, mushY, mushZ - 1, 4, mushY, mushZ + 1, *mushType, air(), false);
}

// ==== registration and layout =======================================================================================

void registerPieces() {
    static bool done = false;
    if (done) return;
    done = true;
    namespace st = ::gf::structure;
    st::registerPieceType(kRuins, [](Json const& saved) -> std::unique_ptr<Piece> {
        auto p = std::make_unique<Ruins>(0, 0, 0, 0);
        p->load(saved);
        return p;
    });
    st::registerPieceType(kMound, [](Json const& saved) -> std::unique_ptr<Piece> {
        ::gf::core::jrand::LegacyRandom r(0);
        auto                             p = std::make_unique<Mound>(0, r, 0, 0, 0);
        p->load(saved);
        return p;
    });
    st::registerPieceType(kUpperEntrance, [](Json const& saved) -> std::unique_ptr<Piece> {
        ::gf::core::jrand::LegacyRandom r(0);
        auto                             p = std::make_unique<UpperEntrance>(0, r, 0, 0, 0);
        p->load(saved);
        return p;
    });
    st::registerPieceType(kEntranceShaft, [](Json const& saved) -> std::unique_ptr<Piece> {
        ::gf::core::jrand::LegacyRandom r(0);
        auto                             p = std::make_unique<EntranceShaft>(0, r, 0, 0, 0);
        p->load(saved);
        return p;
    });
    st::registerPieceType(kMaze, [](Json const& saved) -> std::unique_ptr<Piece> { return std::make_unique<MinotaurMaze>(saved); });
    for (char const* t : {kRoom, kRoomExit, kRoomBoss, kRoomCollapse, kMushRoom, kRoomFountain, kRoomVault, kRoomSpawner}) {
        st::registerPieceType(t, [t](Json const& saved) -> std::unique_ptr<Piece> {
            auto p = std::make_unique<Room>(t);
            p->load(saved);
            return p;
        });
    }
    for (char const* t : {kDeadEnd, kDeadEndChest, kDeadEndTrap, kDeadEndTrip, kDeadEndTorch, kDeadEndWater, kDeadEndLava, kDeadEndPaint, kDeadEndRoots,
                          kDeadEndShroom, kCorridor, kCorridorIron, kCorridorRoots, kCorridorShroom}) {
        st::registerPieceType(t, [t](Json const& saved) -> std::unique_ptr<Piece> {
            auto p = std::make_unique<Cell>(t);
            p->load(saved);
            return p;
        });
    }
}

void layout(RandomSource& context, int x, int y, int z, PieceList& pieces) {
    // LabyrinthStructure#getFirstPiece: offset centers labyrinth mound on intersection of 4 chunk boundaries
    auto*  first = new Ruins(0, x + 5, y, z + 5);
    pieces.push_back(std::unique_ptr<Piece>(first));
    first->addChildren(*first, pieces, context);
}

} // namespace gf::feature::labyrinth::maze
