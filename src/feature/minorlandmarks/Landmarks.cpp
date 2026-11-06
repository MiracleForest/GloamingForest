// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/type/HollowHillStructure.java
//   src/main/java/twilightforest/world/components/structures/type/HedgeMazeStructure.java
//   src/main/java/twilightforest/world/components/structures/HedgeMazeComponent.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The hollow hills (small, medium, large) and the hedge maze as F6 landmark generators.
// HollowHillStructure#canSpawnMob (monsters only inside the dome) is applied by F6 for every
// structure of type twilightforest:hollow_hill (feature/landmark/README.md): the layout gives the
// hill piece TF's box and leaves Generator::spawnIndex unset.
#include "feature/minorlandmarks/Internal.h"

#include <array>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"

#include "core/jrand/JRand.h"
#include "core/jrand/Mth.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/landmark/Landmark.h"
#include "feature/structure/bases/HollowHill.h"
#include "feature/structure/bases/Maze.h"
#include "mod/Log.h"

namespace gf::feature::minorlandmarks {

namespace {

constexpr int kMSize      = 16;
constexpr int kRadius     = (kMSize / 2 * 3) + 1; // 25
constexpr int kDiameter   = 2 * kRadius;          // 50
constexpr int kFloorLevel = 0;

/// HedgeMazeComponent.
class HedgeMaze final : public sb::ComponentOld {
public:
    HedgeMaze(int depth, int x, int y, int z) : ComponentOld(kTFHedge, depth, x, y, z) {
        setOrientation(st::Orientation::South);
        // BoundingBoxUtils.getComponentToAddBoundingBox(x, y, z, -RADIUS, -3, -RADIUS, RADIUS * 2, 10, RADIUS * 2, SOUTH, false)
        bbox = BlockBox{x - kRadius, y - 3, z - kRadius, x + kRadius, y + 7, z + kRadius};
    }
    explicit HedgeMaze(Json const& saved) : ComponentOld(kTFHedge, 0, 0, 0, 0) { load(saved); }

    void postProcess(Region& world, RandomSource& rand, BlockBox const& sbb, BlockPos const&) override {
        rand.setSeed(core::mth::ladd(world.seed(), static_cast<std::int64_t>(bbox.minX) * bbox.minZ)); // getMazeSeed
        sb::Maze maze(kMSize, kMSize, rand);
        maze.oddBias         = 2;
        maze.torchBlockState = &st::blocks::java("twilightforest:firefly");
        maze.wallBlockState  = &st::blocks::java(kHedge);
        maze.type            = 4;
        maze.tall            = 3;
        maze.roots           = 3;

        // grass underneath
        auto const& grass = st::blocks::java("minecraft:grass_block");
        for (int fx = 0; fx <= kDiameter; fx++)
            for (int fz = 0; fz <= kDiameter; fz++) placeBlock(world, grass, fx, kFloorLevel - 1, fz, sbb);

        auto jacko = [](char const* facing) -> Block const& { return st::blocks::java("minecraft:jack_o_lantern", Json{{"facing", facing}}); };
        auto const& northJacko = jacko("south");
        auto const& southJacko = jacko("north");
        auto const& westJacko  = jacko("east");
        auto const& eastJacko  = jacko("west");
        placeBlock(world, westJacko, 0, kFloorLevel, 24, sbb);
        placeBlock(world, westJacko, 0, kFloorLevel, 29, sbb);
        placeBlock(world, eastJacko, 50, kFloorLevel, 24, sbb);
        placeBlock(world, eastJacko, 50, kFloorLevel, 29, sbb);
        placeBlock(world, northJacko, 24, kFloorLevel, 0, sbb);
        placeBlock(world, northJacko, 29, kFloorLevel, 0, sbb);
        placeBlock(world, southJacko, 24, kFloorLevel, 50, sbb);
        placeBlock(world, southJacko, 29, kFloorLevel, 50, sbb);

        int              nrooms = kMSize / 3;
        std::vector<int> rcoords(static_cast<std::size_t>(nrooms * 2), 0);
        while (!maze.allCellsNonZero()) {
            maze.resetCells();
            rcoords.assign(static_cast<std::size_t>(nrooms * 2), 0);
            for (int i = 0; i < nrooms; i++) {
                int rx = 0, rz = 0;
                do {
                    rx = rand.nextInt(kMSize - 2) + 1;
                    rz = rand.nextInt(kMSize - 2) + 1;
                } while (cml::hedgeMazeIsNearRoom(rx, rz, rcoords));
                maze.carveRoom1(rx, rz);
                rcoords[static_cast<std::size_t>(i * 2)]     = rx;
                rcoords[static_cast<std::size_t>(i * 2 + 1)] = rz;
            }
            maze.generateRecursiveBacktracker(0, 0, rand);
        }
        maze.add4Exits();
        maze.copyToStructure(world, 1, kFloorLevel, 1, *this, sbb, rand);
        decorate3x3Rooms(world, rcoords, sbb);
    }

    [[nodiscard]] sb::TerrainAdjustment getTerrainAdjustment() const override { return sb::TerrainAdjustment::BeardBox; }

private:
    void decorate3x3Rooms(Region& world, std::vector<int> const& rcoords, BlockBox const& sbb) {
        for (std::size_t i = 0; i < rcoords.size() / 2; i++) {
            int dx = rcoords[i * 2] * 3 + 3;
            int dz = rcoords[i * 2 + 1] * 3 + 3;
            decorate3x3Room(world, dx, dz, sbb);
        }
    }

    void decorate3x3Room(Region& world, int x, int z, BlockBox const& sbb) {
        // RandomSource.create(world.getSeed() ^ x + z)
        core::jrand::LegacyRandom roomRNG(world.seed() ^ static_cast<std::int64_t>(x + z));
        roomJackO(world, roomRNG, x, z, 8, sbb);
        if (roomRNG.nextInt(4) == 0) roomJackO(world, roomRNG, x, z, 8, sbb);
        roomSpawner(world, roomRNG, x, z, 8, sbb);
        roomTreasure(world, roomRNG, x, z, 8, sbb, "twilightforest:hedge_maze");
        if (roomRNG.nextInt(4) == 0) roomTreasure(world, roomRNG, x, z, 8, sbb, "twilightforest:hedge_cloth");
    }

    void roomSpawner(Region& world, RandomSource& rand, int x, int z, int diameter, BlockBox const& sbb) {
        int         rx = x + rand.nextInt(diameter) - (diameter / 2);
        int         rz = z + rand.nextInt(diameter) - (diameter / 2);
        std::string mob;
        switch (rand.nextInt(3)) {
        case 1: mob = "twilightforest:swarm_spider"; break;
        case 2: mob = "twilightforest:hostile_wolf"; break;
        default: mob = "twilightforest:hedge_spider"; break;
        }
        setSpawner(world, rx, kFloorLevel, rz, sbb, mob);
    }

    void roomTreasure(Region& world, RandomSource& rand, int x, int z, int diameter, BlockBox const& sbb, std::string const& table) {
        int      rx    = x + rand.nextInt(diameter) - (diameter / 2);
        int      rz    = z + rand.nextInt(diameter) - (diameter / 2);
        int      xDiff = x - rx;
        int      zDiff = z - rz;
        BlockPos pos{worldX(rx, rz), worldY(kFloorLevel), worldZ(rx, rz)};
        if (sbb.contains(pos.x, pos.y, pos.z) && !st::blocks::is(world.getBlock(pos), "minecraft:chest")) {
            st::Orientation facing;
            if (std::abs(xDiff) > std::abs(zDiff)) facing = xDiff < 0 ? st::Orientation::West : st::Orientation::East;
            else facing = zDiff < 0 ? st::Orientation::North : st::Orientation::South;
            // TFLootTables.generateChest(world, pos, facing, false, table): seed getSeed() * x + y ^ z
            std::int64_t seed = core::mth::ladd(core::mth::lmul(world.seed(), pos.x), pos.y) ^ pos.z;
            placeContainerAt(world, sbb, pos, "minecraft:chest", facing, table, seed);
        }
    }

    void roomJackO(Region& world, RandomSource& rand, int x, int z, int diameter, BlockBox const& sbb) {
        int rx = x + rand.nextInt(diameter) - (diameter / 2);
        int rz = z + rand.nextInt(diameter) - (diameter / 2);
        // Direction.from2DDataValue(rand.nextInt(4)): S, W, N, E
        static constexpr char const* kFacing[] = {"south", "west", "north", "east"};
        placeBlock(world, st::blocks::java("minecraft:jack_o_lantern", Json{{"facing", kFacing[rand.nextInt(4)]}}), rx, kFloorLevel, rz, sbb);
    }
};

/// The hill sizes of the three structures, their speleothem configs (the structure JSON's
/// "speleothem_config") and HollowHillStructure#getFirstPiece's offsets.
struct HillKind {
    char const* key;
    int         size;
};
constexpr HillKind kHills[] = {{kSmallHill, 1}, {kMediumHill, 2}, {kLargeHill, 3}};

std::string speleothemConfigOf(char const* key) {
    if (auto const* d = gf::landmark::descriptor(key)) {
        if (auto it = d->extra.find("speleothem_config"); it != d->extra.end() && it->is_string()) return it->get<std::string>();
    }
    return key; // TF's structure_speleothem_settings use the structure's own name
}

} // namespace

std::unique_ptr<st::Piece> makeHedgeMaze(int genDepth, int x, int y, int z) { return std::make_unique<HedgeMaze>(genDepth, x, y, z); }

std::unique_ptr<st::Piece> makeHollowHill(int hillSize, int x, int y, int z, std::string const& speleothemConfig) {
    return std::make_unique<sb::HollowHill>(kTFHill, 0, hillSize, x, y, z, speleothemConfig);
}

void installLandmarks() {
    st::registerPieceType(kTFHedge, [](Json const& saved) -> std::unique_ptr<st::Piece> { return std::make_unique<HedgeMaze>(saved); });
    st::registerPieceType(kTFHill, [](Json const& saved) -> std::unique_ptr<st::Piece> {
        auto p = std::make_unique<sb::HollowHill>(kTFHill, 0, saved.value("hillSize", 1), 0, 0, 0, saved.value("config_id", std::string(kSmallHill)));
        p->load(saved);
        return p;
    });

    for (auto const& hill : kHills) {
        std::string key    = hill.key;
        int         size   = hill.size;
        std::string config = speleothemConfigOf(hill.key);
        gf::landmark::registerGenerator(key, gf::landmark::Generator{
            .layout =
                [size, config](gf::landmark::LayoutContext& ctx) {
                    // HollowHillStructure#getFirstPiece (no children)
                    auto const& c  = ctx.start.center;
                    int         ox = size == 1 ? 3 : size == 2 ? 7 : 11;
                    int         oy = size == 1 ? 2 : 5;
                    ctx.pieces.push_back(makeHollowHill(size, c.x - ox, c.y - oy, c.z - ox, config));
                    return true;
                },
            .terraform = [size](gf::landmark::Start const& start, int x, int y, int z) { return cml::hollowHillTerraformer(size, start.box, x, y, z); },
        });
    }

    gf::landmark::registerGenerator(kHedgeMaze, gf::landmark::Generator{
        .layout =
            [](gf::landmark::LayoutContext& ctx) {
                // HedgeMazeStructure#getFirstPiece: HedgeMazeComponent(0, x + 1, y + 4, z + 1); no children
                auto const& c = ctx.start.center;
                ctx.pieces.push_back(makeHedgeMaze(0, c.x + 1, c.y + 4, c.z + 1));
                return true;
            },
        // CustomDensitySource.getInvertedPyramidTerraformer(start, 0, 4); the piece's BEARD_BOX is U2's beardifier
        .terraform = [](gf::landmark::Start const& start, int x, int y, int z) { return gf::landmark::ext::invertedPyramid(start, 0, 4, x, y, z); },
    });
    gf::log().info("minorlandmarks: hollow hills and hedge maze registered");
}

} // namespace gf::feature::minorlandmarks
