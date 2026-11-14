// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/type/HollowTreeStructure.java
//   src/main/java/gloamingforest/world/components/structures/hollowtree/HollowTreePiece.java
//   src/main/java/gloamingforest/world/components/structures/hollowtree/HollowTreeTrunk.java
//   src/main/java/gloamingforest/world/components/structures/hollowtree/HollowTreeLargeBranch.java
//   src/main/java/gloamingforest/world/components/structures/hollowtree/HollowTreeMedBranch.java
//   src/main/java/gloamingforest/world/components/structures/hollowtree/HollowTreeSmallBranch.java
//   src/main/java/gloamingforest/world/components/structures/hollowtree/HollowTreeRoot.java
//   src/main/java/gloamingforest/world/components/structures/hollowtree/HollowTreeLeafDungeon.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The hollow trees (gloamingforest:hollow_tree, gloamingforest:swamp_hollow_tree): not landmarks,
// but F6 keeps their starts (the avoid_landmark_grid structure set) and builds them through this
// generator; the robust gloaming oak sapling (U3) grows one with landmark::generateAt.
#include "feature/minorlandmarks/Internal.h"

#include <algorithm>
#include <cmath>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"

#include "core/jrand/JRand.h"
#include "core/jrand/Mth.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/landmark/Landmark.h"
#include "mod/Log.h"

namespace gf::feature::minorlandmarks {

namespace {

namespace kit = gf::features::kit;

constexpr int kPlaceFlag        = 0b10011;
constexpr int kLeafDungeonChance = 8;

BlockPos above(BlockPos p, int n = 1) { return BlockPos{p.x, p.y + n, p.z}; }
BlockPos fromP(cml::Pos const& p) { return BlockPos{p.x, p.y, p.z}; }
cml::Pos toP(BlockPos const& p) { return {p.x, p.y, p.z}; }
BlockPos translate(BlockPos const& p, double distance, double angle, double tilt) { return fromP(cml::translate(toP(p), distance, angle, tilt)); }

/// HollowTreePiece: a vanilla StructurePiece with upstream's drawing helpers.
class HollowTreePiece : public st::Piece {
public:
    HollowTreePiece(char const* type, int depth, BlockBox const& box) : Piece(type) {
        genDepth = depth;
        bbox     = box;
    }

protected:
    void placeProvidedBlock(Region& world, Json const& provider, RandomSource& random, int sx, int sy, int sz, BlockBox const& sbb, BlockPos const& origin,
                            bool forcedPlace, bool leafHack) {
        BlockPos worldPos = getWorldPos(sx, sy, sz);
        if (!sbb.contains(worldPos.x, worldPos.y, worldPos.z) || (!forcedPlace && !treesReplaceable(world.getBlock(worldPos)))) return;
        Block const* state = sampleProvider(provider, random, worldPos);
        if (!state) return;
        if (st::blocks::javaProperty(*state, "distance")) {
            int d        = std::abs(origin.x - worldPos.x) + std::abs(origin.y - worldPos.y) + std::abs(origin.z - worldPos.z);
            int distance = leafHack ? 1 : std::clamp(d, 1, 7);
            world.setBlock(worldPos, withLeafDistance(*state, distance), kPlaceFlag);
        } else {
            world.setBlock(worldPos, *state, kPlaceFlag);
        }
    }

    void fillColumnDown(Region& world, Json const& provider, RandomSource& random, int sx, int sy, int sz, BlockBox const& box) {
        BlockPos worldPos = getWorldPos(sx, sy, sz);
        if (!box.contains(worldPos.x, worldPos.y, worldPos.z)) return;
        while (st::blocks::replaceableByStructures(world.getBlock(worldPos)) && worldPos.y > world.minY() + 1) {
            if (auto const* b = sampleProvider(provider, random, worldPos)) world.setBlock(worldPos, *b, kPlaceFlag);
            worldPos.y--;
        }
    }

    void fillVineColumnDown(Region& world, Json const& provider, RandomSource& random, int sx, int sy, int sz, BlockBox const& box, st::Orientation direction) {
        BlockPos worldPos = getWorldPos(sx, sy, sz);
        if (!box.contains(worldPos.x, worldPos.y, worldPos.z)) return;
        auto nonFluidReplaceable = [&] {
            Block const& b = world.getBlock(worldPos);
            return !st::blocks::isLiquid(b) && st::blocks::replaceableByStructures(b);
        };
        while (nonFluidReplaceable() && worldPos.y > world.minY() + 1) {
            if (auto const* b = sampleProvider(provider, random, worldPos))
                world.setBlock(worldPos, st::blocks::withJava(*b, Json{{st::blocks::directionName(direction), "true"}}), kPlaceFlag);
            worldPos.y--;
        }
    }

    void drawBresehnam(Region& level, BlockBox const& writeableBounds, BlockPos const& startPos, BlockPos const& endPos, Json const& provider, RandomSource& random) {
        for (auto const& p : cml::bresenham(toP(startPos), toP(endPos))) {
            BlockPos worldPos = fromP(p);
            if (writeableBounds.contains(worldPos.x, worldPos.y, worldPos.z) && treesReplaceable(level.getBlock(worldPos)))
                if (auto const* b = sampleProvider(provider, random, worldPos)) level.setBlock(worldPos, *b, kPlaceFlag);
        }
    }

    void drawBlockBlob(Region& world, BlockBox const& sbb, int sx, int sy, int sz, int blobRadius, RandomSource& random, Json const& provider, bool forcedPlace,
                       bool leafHack, bool imperfect) {
        BlockPos origin = getWorldPos(sx, sy, sz);
        auto     place  = [&](int x, int y, int z) { placeProvidedBlock(world, provider, random, x, y, z, sbb, origin, forcedPlace, leafHack); };
        for (int dx = 0; dx <= blobRadius; dx++)
            for (int dy = 0; dy <= blobRadius; dy++)
                for (int dz = 0; dz <= blobRadius; dz++) {
                    int dist;
                    if (dx >= dy && dx >= dz) dist = dx + static_cast<int>((std::max(dy, dz) * 0.5) + (std::min(dy, dz) * 0.25));
                    else if (dy >= dx && dy >= dz) dist = dy + static_cast<int>((std::max(dx, dz) * 0.5) + (std::min(dx, dz) * 0.25));
                    else dist = dz + static_cast<int>((std::max(dx, dy) * 0.5) + (std::min(dx, dy) * 0.25));
                    if (dist > blobRadius) continue;
                    if (imperfect && dist == blobRadius) {
                        if (dx == dy && dy == dz) continue;
                        if ((dx == dy && dz > dx && dx > 0) || (dy == dz && dx > dy && dy > 0) || (dz == dx && dy > dz && dz > 0)) {
                            if (random.nextInt(2) == 0) place(sx + dx, sy + dy, sz + dz);
                            if (random.nextInt(2) == 0) place(sx + dx, sy + dy, sz - dz);
                            if (random.nextInt(2) == 0) place(sx - dx, sy + dy, sz + dz);
                            if (random.nextInt(2) == 0) place(sx - dx, sy + dy, sz - dz);
                            if (random.nextInt(2) == 0) place(sx + dx, sy - dy, sz + dz);
                            if (random.nextInt(2) == 0) place(sx + dx, sy - dy, sz - dz);
                            if (random.nextInt(2) == 0) place(sx - dx, sy - dy, sz + dz);
                            if (random.nextInt(2) == 0) place(sx - dx, sy - dy, sz - dz);
                            continue;
                        }
                    }
                    place(sx + dx, sy + dy, sz + dz);
                    place(sx + dx, sy + dy, sz - dz);
                    place(sx - dx, sy + dy, sz + dz);
                    place(sx - dx, sy + dy, sz - dz);
                    place(sx + dx, sy - dy, sz + dz);
                    place(sx + dx, sy - dy, sz - dz);
                    place(sx - dx, sy - dy, sz + dz);
                    place(sx - dx, sy - dy, sz - dz);
                }
    }

    /// getInterChunkDecoRNG: XoroshiroRandomSource(seed + (minX * 321534781L) ^ (minZ * 756839L)).
    [[nodiscard]] core::jrand::Xoroshiro128pp interChunkDecoRNG(Region& level) const {
        std::int64_t a = core::mth::ladd(level.seed(), core::mth::lmul(bbox.minX, 321534781LL));
        return core::jrand::Xoroshiro128pp(a ^ core::mth::lmul(bbox.minZ, 756839LL));
    }
};

// ---- HollowTreeMedBranch (and the small branch, large branch, root built on it) ---------------------------

class MedBranch : public HollowTreePiece {
public:
    MedBranch(char const* type, int i, BlockPos src, BlockPos dest, BlockBox const& box, double length, double angle, double tilt, bool leafy, Json wood,
              Json leaves)
    : HollowTreePiece(type, i, box),
      src(src),
      dest(dest),
      length(length),
      angle(angle),
      tilt(tilt),
      leafy(leafy),
      wood(std::move(wood)),
      leaves(std::move(leaves)) {
        setVanillaOrientation(*this, st::Orientation::South);
    }
    MedBranch(char const* type, Json const& saved) : HollowTreePiece(type, 0, BlockBox{}) { load(saved); }

    static BlockBox branchBoundingBox(BlockPos const& a, BlockPos const& b, int extra) {
        return BlockBox{std::min(a.x, b.x) - extra, std::min(a.y, b.y) - extra, std::min(a.z, b.z) - extra,
                        std::max(a.x, b.x) + extra, std::max(a.y, b.y) + extra, std::max(a.z, b.z) + extra};
    }

    [[nodiscard]] Json save() const override {
        Json j            = Piece::save();
        j["srcPosX"]      = src.x;
        j["srcPosY"]      = src.y;
        j["srcPosZ"]      = src.z;
        j["destPosX"]     = dest.x;
        j["destPosY"]     = dest.y;
        j["destPosZ"]     = dest.z;
        j["branchLength"] = length;
        j["branchAngle"]  = angle;
        j["branchTilt"]   = tilt;
        j["branchLeafy"]  = leafy;
        j["wood"]         = wood;
        j["leaves"]       = leaves;
        return j;
    }
    void load(Json const& d) override {
        Piece::load(d);
        src    = BlockPos{d.value("srcPosX", 0), d.value("srcPosY", 0), d.value("srcPosZ", 0)};
        dest   = BlockPos{d.value("destPosX", 0), d.value("destPosY", 0), d.value("destPosZ", 0)};
        length = d.value("branchLength", 0.0);
        angle  = d.value("branchAngle", 0.0);
        tilt   = d.value("branchTilt", 0.0);
        leafy  = d.value("branchLeafy", false);
        if (auto it = d.find("wood"); it != d.end()) wood = *it;
        if (auto it = d.find("leaves"); it != d.end()) leaves = *it;
    }

    void postProcess(Region& level, RandomSource&, BlockBox const& writeableBounds, BlockPos const&) override {
        auto decoRNG = interChunkDecoRNG(level);
        drawBresehnam(level, writeableBounds, src, dest, wood, decoRNG);
        drawBresehnam(level, writeableBounds, above(src), dest, wood, decoRNG);
        int    a         = decoRNG.nextInt(3) + 1;
        int    numShoots = std::min(a, static_cast<int>(length / 5));
        double angleInc  = 0.8 / numShoots;
        for (int i = 0; i < numShoots; i++) {
            double angleVar = (angleInc * i) - 0.4;
            double outVar   = (decoRNG.nextDouble() * 0.8) + 0.2;
            BlockPos bSrc   = translate(src, length * outVar, angle, tilt);
            drawSmallBranch(level, writeableBounds, bSrc, std::max(length * 0.3F, 2.0), angle + angleVar, tilt, decoRNG);
        }
        if (leafy) {
            int b            = decoRNG.nextInt(3) + 1;
            int numLeafBalls = std::min(b, static_cast<int>(length / 5));
            for (int i = 0; i < numLeafBalls; i++) {
                double   slength = (decoRNG.nextFloat() * 0.6F + 0.2F) * length;
                BlockPos bdst    = translate(BlockPos{src.x - bbox.minX, src.y - bbox.minY, src.z - bbox.minZ}, slength, angle, tilt);
                int      radius  = decoRNG.nextBoolean() ? 2 : 3;
                drawBlockBlob(level, writeableBounds, bdst.x, bdst.y, bdst.z, radius, decoRNG, leaves, false, false, true);
            }
            drawBlockBlob(level, writeableBounds, dest.x - bbox.minX, dest.y - bbox.minY, dest.z - bbox.minZ, 3, decoRNG, leaves, false, false, true);
        }
    }

    BlockPos src, dest;
    double   length = 0, angle = 0, tilt = 0;
    bool     leafy  = false;
    Json     wood, leaves;

protected:
    void drawSmallBranch(Region& world, BlockBox const& sbb, BlockPos const& sourcePos, double branchLength, double branchAngle, double branchTilt, RandomSource& random) {
        BlockPos branchDest = translate(sourcePos, branchLength, branchAngle, branchTilt);
        drawBresehnam(world, sbb, sourcePos, branchDest, wood, random);
        drawBlockBlob(world, sbb, branchDest.x - bbox.minX, branchDest.y - bbox.minY, branchDest.z - bbox.minZ, 2, random, leaves, false, false, true);
    }
};

class SmallBranch final : public MedBranch {
public:
    SmallBranch(int i, BlockPos src, double length, double angle, double tilt, bool leafy, Json wood, Json leaves)
    : MedBranch(kTFHTSB, i, src, translate(src, length, angle, tilt), branchBoundingBox(src, translate(src, length, angle, tilt), 3 + core::mth::ceil(length)), length,
                angle, tilt, leafy, std::move(wood), std::move(leaves)) {}
    explicit SmallBranch(Json const& saved) : MedBranch(kTFHTSB, saved) {}

    void postProcess(Region& level, RandomSource&, BlockBox const& writeableBounds, BlockPos const&) override {
        auto decoRNG = interChunkDecoRNG(level);
        drawBresehnam(level, writeableBounds, src, dest, wood, decoRNG);
        if (leafy) {
            int leafRad = decoRNG.nextInt(2) + 1;
            drawBlockBlob(level, writeableBounds, dest.x - bbox.minX, dest.y - bbox.minY, dest.z - bbox.minZ, leafRad, decoRNG, leaves, false, false, true);
        }
    }
};

class Root final : public MedBranch {
public:
    /// HollowTreeRoot(i, src, length, angle, tilt, leafy, root, wood): the root provider rides in the
    /// otherwise unused leaves field.
    Root(int i, BlockPos src, double length, double angle, double tilt, bool leafy, Json root, Json wood)
    : MedBranch(kTFHTRo, i, src, translate(src, length, angle, tilt), branchBoundingBox(src, translate(src, length, angle, tilt), 0), length, angle, tilt, leafy,
                std::move(wood), std::move(root)) {}
    explicit Root(Json const& saved) : MedBranch(kTFHTRo, saved) {}

    void postProcess(Region& level, RandomSource&, BlockBox const& writeableBounds, BlockPos const&) override {
        auto     decoRNG = interChunkDecoRNG(level);
        BlockPos rSrc{src.x - bbox.minX, src.y - bbox.minY, src.z - bbox.minZ};
        BlockPos rDest{dest.x - bbox.minX, dest.y - bbox.minY, dest.z - bbox.minZ};
        drawRootLine(level, writeableBounds, rSrc, rDest, decoRNG);
        drawRootLine(level, writeableBounds, BlockPos{rSrc.x, rSrc.y - 1, rSrc.z}, BlockPos{rDest.x, rDest.y - 1, rDest.z}, decoRNG);
    }

private:
    void drawRootLine(Region& world, BlockBox const& sbb, BlockPos const& rSrc, BlockPos const& rDest, RandomSource& random) {
        for (auto const& c : cml::bresenham(toP(rSrc), toP(rDest))) {
            BlockPos worldPos = getWorldPos(c.x, c.y, c.z);
            if (!sbb.contains(worldPos.x, worldPos.y, worldPos.z)) continue;
            if (st::blocks::inTag(world.getBlock(worldPos), "minecraft:logs")) continue; // wood, do nothing
            bool exposed = false;
            static constexpr int kDirs[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
            for (auto const& d : kDirs)
                if (st::blocks::canBeReplaced(world.getBlock(BlockPos{worldPos.x + d[0], worldPos.y + d[1], worldPos.z + d[2]}))) {
                    exposed = true;
                    break;
                }
            // exposed: wood; covered up: root (the root provider is the leaves field)
            placeProvidedBlock(world, exposed ? wood : leaves, random, c.x, c.y, c.z, sbb, BlockPos{0, 0, 0}, false, false);
        }
    }
};

/// HollowTreeLeafDungeon.
class LeafDungeon final : public HollowTreePiece {
public:
    LeafDungeon(int index, int x, int y, int z, int radius, Json wood, Json leaves, Json inside, Json lootContainer, std::string lootTable, std::string monster,
                RandomSource& random)
    : HollowTreePiece(kTFHTLD, index, BlockBox{x - radius, y - radius, z - radius, x + radius, y + radius, z + radius}),
      radius(radius),
      wood(std::move(wood)),
      leaves(std::move(leaves)),
      inside(std::move(inside)),
      lootContainer(std::move(lootContainer)),
      lootTable(std::move(lootTable)),
      monster(std::move(monster)) {
        // StructurePiece.getRandomHorizontalDirection(random)
        setVanillaOrientation(*this, toOrientation(cml::horizontalByIndex(random.nextInt(4))));
    }
    explicit LeafDungeon(Json const& saved) : HollowTreePiece(kTFHTLD, 0, BlockBox{}) { load(saved); }

    [[nodiscard]] Json save() const override {
        Json j          = Piece::save();
        j["leafRadius"] = radius;
        j["wood"]       = wood;
        j["leaves"]     = leaves;
        j["air"]        = inside;
        j["loot_block"] = lootContainer;
        j["loot_table"] = lootTable;
        j["monster"]    = monster;
        return j;
    }
    void load(Json const& d) override {
        Piece::load(d);
        radius = d.value("leafRadius", 4);
        if (auto it = d.find("wood"); it != d.end()) wood = *it;
        if (auto it = d.find("leaves"); it != d.end()) leaves = *it;
        if (auto it = d.find("air"); it != d.end()) inside = *it;
        if (auto it = d.find("loot_block"); it != d.end()) lootContainer = *it;
        lootTable = d.value("loot_table", std::string("gloamingforest:tree_cache"));
        monster   = d.value("monster", std::string("gloamingforest:swarm_spider"));
    }

    void postProcess(Region& level, RandomSource&, BlockBox const& writeableBounds, BlockPos const&) override {
        auto decoRNG = interChunkDecoRNG(level);
        drawBlockBlob(level, writeableBounds, radius, radius, radius, 5, decoRNG, leaves, false, true, true);
        drawBlockBlob(level, writeableBounds, radius, radius, radius, 3, decoRNG, wood, false, false, false);
        drawBlockBlob(level, writeableBounds, radius, radius, radius, 2, decoRNG, inside, true, false, true);
        placeTreasure(level, radius, radius - 1, radius, writeableBounds, decoRNG);
        // placeSpawnerAtCurrentPosition
        BlockPos pos = getWorldPos(radius, radius, radius);
        if (writeableBounds.contains(pos.x, pos.y, pos.z) && !st::blocks::is(level.getBlock(pos), "minecraft:spawner")) placeSpawnerAt(level, writeableBounds, pos, monster);
    }

private:
    void placeTreasure(Region& world, int x, int y, int z, BlockBox const& sbb, RandomSource& random) {
        static constexpr st::Orientation kDirs[] = {st::Orientation::North, st::Orientation::East, st::Orientation::South, st::Orientation::West};
        st::Orientation direction = kDirs[random.nextInt(4)];
        BlockPos        pos       = getWorldPos(x, y, z);
        pos.x += st::stepX(direction) * 2;
        pos.z += st::stepZ(direction) * 2;
        Block const* state = sampleProvider(lootContainer, random, pos);
        std::string  id    = providerBlockId(lootContainer);
        if (id.empty()) id = "minecraft:chest";
        (void)state; // a simple provider: no draw (upstream samples the state before the checks)
        if (sbb.contains(pos.x, pos.y, pos.z) && !st::blocks::is(world.getBlock(pos), id)) {
            // the chest faces away from the center (ChestBlock.FACING = direction.getOpposite());
            // RandomizableContainerBlockEntity#setLootTable(table, random.nextLong())
            std::int64_t seed = random.nextLong();
            st::blockentities::container(world, pos, id, st::opposite(direction), lootTable, seed);
        }
    }

    int         radius = 4;
    Json        wood, leaves, inside, lootContainer;
    std::string lootTable, monster;
};

class LargeBranch final : public MedBranch {
public:
    LargeBranch(int i, BlockPos src, double length, double angle, double tilt, bool leafy, RandomSource& rand, HollowTreeConfig const& c)
    : MedBranch(kTFHTLB, i, src, translate(src, length, angle, tilt), branchBoundingBox(src, translate(src, length, angle, tilt), 3 + core::mth::ceil(length)), length,
                angle, tilt, leafy, c.wood, c.leaves),
      dungeonWood(c.dungeonWood),
      dungeonAir(c.dungeonAir),
      dungeonLootBlock(c.dungeonLootBlock),
      dungeonLootTable(c.dungeonLootTable),
      dungeonMonster(c.dungeonMonster) {
        hasLeafDungeon = rand.nextInt(kLeafDungeonChance) == 0;
    }
    explicit LargeBranch(Json const& saved) : MedBranch(kTFHTLB, saved) { load(saved); }

    [[nodiscard]] Json save() const override {
        Json j                  = MedBranch::save();
        j["has_leaf_dungeon"]   = hasLeafDungeon;
        j["dungeon_wood"]       = dungeonWood;
        j["dungeon_air"]        = dungeonAir;
        j["dungeon_loot_block"] = dungeonLootBlock;
        j["dungeon_loot_table"] = dungeonLootTable;
        j["dungeon_monster"]    = dungeonMonster;
        return j;
    }
    void load(Json const& d) override {
        MedBranch::load(d);
        hasLeafDungeon = d.value("has_leaf_dungeon", false);
        if (auto it = d.find("dungeon_wood"); it != d.end()) dungeonWood = *it;
        if (auto it = d.find("dungeon_air"); it != d.end()) dungeonAir = *it;
        if (auto it = d.find("dungeon_loot_block"); it != d.end()) dungeonLootBlock = *it;
        dungeonLootTable = d.value("dungeon_loot_table", std::string("gloamingforest:tree_cache"));
        dungeonMonster   = d.value("dungeon_monster", std::string("gloamingforest:swarm_spider"));
    }

    void addChildren(st::Piece&, std::vector<std::unique_ptr<st::Piece>>& list, RandomSource& rand) override {
        int index         = genDepth;
        int a             = rand.nextInt(static_cast<int>(length / 6));
        int numMedBranches = a + static_cast<int>(length / 8);
        for (int i = 0; i <= numMedBranches; i++) {
            double   outVar   = (rand.nextDouble() * 0.3) + 0.3;
            double   angleVar = rand.nextDouble() * 0.225 * ((i & 1) == 0 ? 1.0 : -1.0);
            BlockPos bsrc     = translate(src, length * outVar, angle, tilt);
            BlockPos d        = translate(bsrc, length * 0.6, angle + angleVar, tilt);
            list.push_back(std::make_unique<MedBranch>(kTFHTMB, index + 2 + i, bsrc, d, branchBoundingBox(bsrc, d, 3 + core::mth::ceil(length * 0.6)), length * 0.6,
                                                       angle + angleVar, tilt, leafy, wood, leaves));
        }
        if (hasLeafDungeon)
            list.push_back(std::make_unique<LeafDungeon>(index + 1, dest.x, dest.y, dest.z, 4, dungeonWood, leaves, dungeonAir, dungeonLootBlock, dungeonLootTable,
                                                         dungeonMonster, rand));
    }

    void postProcess(Region& level, RandomSource&, BlockBox const& writeableBounds, BlockPos const&) override {
        auto decoRNG = interChunkDecoRNG(level);
        drawBresehnam(level, writeableBounds, src, dest, wood, decoRNG);
        for (int i = 0; i <= 4; i++) {
            int vx = (i & 2) == 0 ? 1 : 0;
            int vy = (i & 1) == 0 ? 1 : -1;
            int vz = (i & 2) == 0 ? 0 : 1;
            drawBresehnam(level, writeableBounds, BlockPos{src.x + vx, src.y + vy, src.z + vz}, dest, wood, decoRNG);
        }
        int numSmallBranches = decoRNG.nextInt(2) + 1;
        for (int i = 0; i <= numSmallBranches; i++) {
            double   outVar   = (decoRNG.nextFloat() * 0.25F) + 0.25F;
            double   angleVar = decoRNG.nextFloat() * 0.25F * ((i & 1) == 0 ? 1.0F : -1.0F);
            BlockPos bsrc     = translate(src, length * outVar, angle, tilt);
            drawSmallBranch(level, writeableBounds, bsrc, std::max(length * 0.3F, 2.0), angle + angleVar, tilt, decoRNG);
        }
        if (leafy && !hasLeafDungeon)
            drawBlockBlob(level, writeableBounds, dest.x - bbox.minX, dest.y - bbox.minY, dest.z - bbox.minZ, 3, decoRNG, leaves, false, false, true);
    }

    bool        hasLeafDungeon = false;
    Json        dungeonWood, dungeonAir, dungeonLootBlock;
    std::string dungeonLootTable, dungeonMonster;
};

/// HollowTreeTrunk.
class Trunk final : public HollowTreePiece {
public:
    Trunk(int height, int radius, BlockBox const& box, HollowTreeConfig config) : HollowTreePiece(kTFHTTr, 0, box), height(height), radius(radius), c(std::move(config)) {
        setVanillaOrientation(*this, st::Orientation::South);
    }
    explicit Trunk(Json const& saved) : HollowTreePiece(kTFHTTr, 0, BlockBox{}), c(HollowTreeConfig::defaults()) { load(saved); }

    [[nodiscard]] Json save() const override {
        Json j                  = Piece::save();
        j["trunkHeight"]        = height;
        j["trunkRadius"]        = radius;
        j["log"]                = c.log;
        j["wood"]               = c.wood;
        j["root"]               = c.root;
        j["leaves"]             = c.leaves;
        j["vine"]               = c.vine;
        j["bug"]                = c.bug;
        j["dungeon_wood"]       = c.dungeonWood;
        j["dungeon_air"]        = c.dungeonAir;
        j["dungeon_loot_block"] = c.dungeonLootBlock;
        j["dungeon_loot_table"] = c.dungeonLootTable;
        j["dungeon_monster"]    = c.dungeonMonster;
        return j;
    }
    void load(Json const& d) override {
        Piece::load(d);
        height = d.value("trunkHeight", height);
        radius = d.value("trunkRadius", radius);
        auto get = [&](char const* k, Json& out) {
            if (auto it = d.find(k); it != d.end() && it->is_object()) out = *it;
        };
        get("log", c.log);
        get("wood", c.wood);
        get("root", c.root);
        get("leaves", c.leaves);
        get("vine", c.vine);
        get("bug", c.bug);
        get("dungeon_wood", c.dungeonWood);
        get("dungeon_air", c.dungeonAir);
        get("dungeon_loot_block", c.dungeonLootBlock);
        c.dungeonLootTable = d.value("dungeon_loot_table", c.dungeonLootTable);
        c.dungeonMonster   = d.value("dungeon_monster", c.dungeonMonster);
    }

    void addChildren(st::Piece&, std::vector<std::unique_ptr<st::Piece>>& list, RandomSource& rand) override {
        int index       = genDepth;
        int numBranches = rand.nextInt(3) + 3;
        for (int i = 0; i <= numBranches; i++) {
            int    branchHeight   = static_cast<int>(height * rand.nextDouble() * 0.9) + (height / 10);
            double branchRotation = rand.nextDouble();
            makeSmallBranch(list, rand, index + i + 1, branchHeight, 4, branchRotation, 0.35, true);
        }
        buildFullCrown(list, rand, index + numBranches + 1);
        buildBranchRing(list, rand, index, 4, 2, 6, 0.75, 3, 5, 3, false);
        buildBranchRing(list, rand, index, 2, 2, 8, 0.9, 3, 5, 3, false);
    }

    void postProcess(Region& level, RandomSource&, BlockBox const& writeableBounds, BlockPos const&) override {
        auto            decoRNG      = interChunkDecoRNG(level);
        int             hollow       = radius / 2;
        static constexpr st::Orientation kFrom2D[] = {st::Orientation::South, st::Orientation::West, st::Orientation::North, st::Orientation::East};
        st::Orientation vineDirection = kFrom2D[decoRNG.nextInt(4)];
        bool            vineAxisX     = vineDirection == st::Orientation::East || vineDirection == st::Orientation::West;
        for (int dx = 0; dx <= 2 * radius; dx++)
            for (int dz = 0; dz <= 2 * radius; dz++) {
                int dist = cml::octagonalDist(std::abs(dx - radius), std::abs(dz - radius));
                for (int dy = 0; dy <= height; dy++)
                    if (dist <= radius && dist > hollow) placeProvidedBlock(level, c.log, decoRNG, dx + 1, dy, dz + 1, writeableBounds, BlockPos{0, 0, 0}, false, false);
                if (dist <= radius) fillColumnDown(level, c.log, decoRNG, dx + 1, -1, dz + 1, writeableBounds);
                if (dist == hollow
                    && (vineAxisX ? (dx == radius + (hollow * st::stepX(vineDirection))) : (dz == radius + (hollow * st::stepZ(vineDirection)))))
                    fillVineColumnDown(level, c.vine, decoRNG, dx + 1, height, dz + 1, writeableBounds, vineDirection);
            }
        int a          = decoRNG.nextInt(3 * radius);
        int numInsects = a + decoRNG.nextInt(3 * radius) + 10;
        for (int i = 0; i <= numInsects; i++) {
            int    fHeight = static_cast<int>(height * decoRNG.nextDouble() * 0.9) + (height / 10);
            double fAngle  = decoRNG.nextDouble();
            addInsect(level, decoRNG, fHeight, fAngle, writeableBounds);
        }
    }

    int              height = 32, radius = 1;
    HollowTreeConfig c;

private:
    BlockPos branchSrc(int branchHeight, double branchRotation) const {
        return translate(BlockPos{bbox.minX + radius + 1, bbox.minY + branchHeight, bbox.minZ + radius + 1}, radius, branchRotation, 0.5);
    }

    void buildFullCrown(std::vector<std::unique_ptr<st::Piece>>& list, RandomSource& rand, int index) {
        int crownRadius = radius * 4 + 4;
        int bvar        = radius + 2;
        index += buildBranchRing(list, rand, index, height - crownRadius, 0, crownRadius, 0.35, bvar, bvar + 2, 2, true);
        index += buildBranchRing(list, rand, index, height - (crownRadius / 2), 0, crownRadius, 0.28, bvar, bvar + 2, 1, true);
        index += buildBranchRing(list, rand, index, height, 0, crownRadius, 0.15, 2, 4, 2, true);
        index += buildBranchRing(list, rand, index, height, 0, crownRadius / 2, 0.05, bvar, bvar + 2, 1, true);
    }

    int buildBranchRing(std::vector<std::unique_ptr<st::Piece>>& list, RandomSource& rand, int index, int branchHeight, int heightVar, int length, double tilt,
                        int minBranches, int maxBranches, int size, bool leafy) {
        int    numBranches    = rand.nextInt(maxBranches - minBranches + 1) + minBranches;
        double branchRotation = 1.0 / numBranches;
        double branchOffset   = rand.nextDouble();
        for (int i = 0; i <= numBranches; i++) {
            int dHeight = heightVar > 0 ? branchHeight - heightVar + rand.nextInt(2 * heightVar) : branchHeight;
            double rot  = i * branchRotation + branchOffset;
            if (size == 2) makeLargeBranch(list, rand, index, dHeight, length, rot, tilt, leafy);
            else if (size == 1) makeMedBranch(list, index, dHeight, length, rot, tilt, leafy);
            else if (size == 3) makeRoot(list, index, dHeight, length, rot, tilt);
            else makeSmallBranch(list, rand, index, dHeight, length, rot, tilt, leafy);
        }
        return numBranches;
    }

    void makeSmallBranch(std::vector<std::unique_ptr<st::Piece>>& list, RandomSource&, int index, int branchHeight, int branchLength, double rot, double angle, bool leafy) {
        list.push_back(std::make_unique<SmallBranch>(index, branchSrc(branchHeight, rot), branchLength, rot, angle, leafy, c.wood, c.leaves)); // no children
    }
    void makeMedBranch(std::vector<std::unique_ptr<st::Piece>>& list, int index, int branchHeight, int branchLength, double rot, double angle, bool leafy) {
        BlockPos s = branchSrc(branchHeight, rot);
        BlockPos d = translate(s, branchLength, rot, angle);
        list.push_back(std::make_unique<MedBranch>(kTFHTMB, index, s, d, MedBranch::branchBoundingBox(s, d, 3 + branchLength), branchLength, rot, angle, leafy, c.wood,
                                                   c.leaves)); // no children
    }
    void makeLargeBranch(std::vector<std::unique_ptr<st::Piece>>& list, RandomSource& rand, int index, int branchHeight, int branchLength, double rot, double angle,
                         bool leafy) {
        auto  branch = std::make_unique<LargeBranch>(index, branchSrc(branchHeight, rot), branchLength, rot, angle, leafy, rand, c);
        auto* raw    = branch.get();
        list.push_back(std::move(branch));
        raw->addChildren(*this, list, rand);
    }
    void makeRoot(std::vector<std::unique_ptr<st::Piece>>& list, int index, int branchHeight, int branchLength, double rot, double angle) {
        list.push_back(std::make_unique<Root>(index, branchSrc(branchHeight, rot), branchLength, rot, angle, false, c.root, c.wood)); // no children
    }

    void addInsect(Region& world, RandomSource& random, int fHeight, double fAngle, BlockBox const& sbb) {
        auto bugSpot = cml::translate({radius + 1, fHeight, radius + 1}, radius + 1, fAngle, 0.5);
        int  ox = worldX(bugSpot.x, bugSpot.z), oy = worldY(bugSpot.y), oz = worldZ(bugSpot.x, bugSpot.z);
        if (!sbb.contains(ox, oy, oz)) return;
        BlockPos src{ox, oy, oz};
        double   wrapped = std::fmod(fAngle, 1.0);
        auto     facing  = st::Rotation::Clockwise90;
        if (wrapped > 0.875 || wrapped <= 0.125) facing = st::Rotation::Clockwise180;
        else if (wrapped > 0.375 && wrapped <= 0.625) facing = st::Rotation::None;
        else if (wrapped > 0.625) facing = st::Rotation::Counterclockwise90;
        Block const* bug = sampleProvider(c.bug, random, src);
        if (!bug) return;
        Block const& decor = st::blocks::rotate(*bug, facing);
        if (st::blocks::canBeReplaced(world.getBlock(src)) && kit::canSurvive(world, src, decor)) world.setBlock(src, decor, 3);
    }
};

void chunkOf(gf::landmark::Start const& start, int& cx, int& cz) {
    cx = start.center.x >> 4;
    cz = start.center.z >> 4;
    if (auto it = start.data.find("chunk"); it != start.data.end() && it->is_array() && it->size() == 2) {
        cx = (*it)[0].get<int>();
        cz = (*it)[1].get<int>();
    }
}

int maxBuildHeightOf(int dimension) {
    // the Gloaming Forest: min_y -32, height 288; the overworld (a sapling grown there): 320
    return dimension == 0 ? 320 : 256;
}

} // namespace

HollowTreeConfig HollowTreeConfig::defaults() {
    HollowTreeConfig c;
    c.height           = Json{{"type", "minecraft:uniform"}, {"min_inclusive", 32}, {"max_inclusive", 95}};
    c.radius           = Json{{"type", "minecraft:uniform"}, {"min_inclusive", 1}, {"max_inclusive", 4}};
    c.log              = simpleProvider("gloamingforest:gloaming_oak_log", Json{{"axis", "y"}});
    c.wood             = simpleProvider("gloamingforest:gloaming_oak_wood", Json{{"axis", "y"}});
    c.root             = simpleProvider("gloamingforest:root");
    c.leaves           = simpleProvider("gloamingforest:gloaming_oak_leaves", Json{{"distance", "7"}, {"persistent", "false"}, {"waterlogged", "false"}});
    c.vine             = simpleProvider("minecraft:vine");
    c.bug              = Json{{"type", "minecraft:weighted_state_provider"},
                              {"entries", Json::array({Json{{"data", Json{{"Name", "gloamingforest:firefly"}, {"Properties", {{"facing", "north"}}}}}, {"weight", 1}},
                                                       Json{{"data", Json{{"Name", "gloamingforest:cicada"}, {"Properties", {{"facing", "north"}}}}}, {"weight", 1}}})}};
    c.dungeonWood      = c.wood;
    c.dungeonAir       = simpleProvider("minecraft:air");
    c.dungeonLootBlock = simpleProvider("minecraft:chest", Json{{"type", "single"}, {"facing", "west"}, {"waterlogged", "false"}});
    return c;
}

HollowTreeConfig HollowTreeConfig::fromJson(Json const& s) {
    HollowTreeConfig c = defaults();
    auto get           = [&](char const* k, Json& out) {
        if (auto it = s.find(k); it != s.end() && it->is_object()) out = *it;
    };
    get("height", c.height);
    get("radius", c.radius);
    get("log", c.log);
    get("wood", c.wood);
    get("root", c.root);
    get("leaves", c.leaves);
    get("vine", c.vine);
    get("bug", c.bug);
    get("dungeon_wood", c.dungeonWood);
    get("dungeon_air", c.dungeonAir);
    get("dungeon_loot_block", c.dungeonLootBlock);
    c.dungeonLootTable = s.value("dungeon_loot_table", c.dungeonLootTable);
    c.dungeonMonster   = s.value("dungeon_monster", c.dungeonMonster);
    c.allowInWater     = s.value("allow_in_water", false);
    return c;
}

std::vector<std::unique_ptr<st::Piece>> hollowTreePieces(HollowTreeConfig const& config, int height, int radius, int x, int y, int z, RandomSource& random) {
    std::vector<std::unique_ptr<st::Piece>> pieces;
    BlockBox box{x - (radius + 1), y, z - (radius + 1), x + radius + 1, y + height, z + radius + 1};
    auto     trunk = std::make_unique<Trunk>(height, radius, box, config);
    auto*    raw   = trunk.get();
    pieces.push_back(std::move(trunk));
    raw->addChildren(*raw, pieces, random);
    return pieces;
}

void installHollowTrees() {
    st::registerPieceType(kTFHTTr, [](Json const& saved) -> std::unique_ptr<st::Piece> { return std::make_unique<Trunk>(saved); });
    st::registerPieceType(kTFHTLB, [](Json const& saved) -> std::unique_ptr<st::Piece> { return std::make_unique<LargeBranch>(saved); });
    st::registerPieceType(kTFHTMB, [](Json const& saved) -> std::unique_ptr<st::Piece> { return std::make_unique<MedBranch>(kTFHTMB, saved); });
    st::registerPieceType(kTFHTSB, [](Json const& saved) -> std::unique_ptr<st::Piece> { return std::make_unique<SmallBranch>(saved); });
    st::registerPieceType(kTFHTRo, [](Json const& saved) -> std::unique_ptr<st::Piece> { return std::make_unique<Root>(saved); });
    st::registerPieceType(kTFHTLD, [](Json const& saved) -> std::unique_ptr<st::Piece> { return std::make_unique<LeafDungeon>(saved); });

    for (char const* key : {kHollowTree, kSwampHollowTree}) {
        HollowTreeConfig config = HollowTreeConfig::defaults();
        if (auto const* d = gf::landmark::descriptor(key)) config = HollowTreeConfig::fromJson(d->extra);
        if (std::string(key) == kSwampHollowTree && !(gf::landmark::descriptor(key) && gf::landmark::descriptor(key)->extra.contains("allow_in_water")))
            config.allowInWater = true; // GloamingStructures: the swamp hollow tree may stand in water
        gf::landmark::registerGenerator(key, gf::landmark::Generator{
            .layout =
                [config](gf::landmark::LayoutContext& ctx) {
                    int cx = 0, cz = 0;
                    chunkOf(ctx.start, cx, cz);
                    auto& c = ctx.start.center;
                    // RandomSource.create(seed + chunkX * 25117L + chunkZ * 151121L): ctx.random, the
                    // same source for findGenerationPoint and generateFromSapling. A natural start
                    // arrives at its chunk's corner (a new start) or at the point findGenerationPoint
                    // found (a start rebuilt); any other center is a placed one (the sapling,
                    // /gloamingforest place, selftests) and grows as generateFromSapling does.
                    core::jrand::LegacyRandom probe(core::mth::ladd(core::mth::ladd(ctx.start.seed, static_cast<std::int64_t>(cx) * 25117LL),
                                                                    static_cast<std::int64_t>(cz) * 151121LL));
                    int px = cx * 16 + probe.nextInt(16), pz = cz * 16 + probe.nextInt(16);
                    bool natural = naturalStart(ctx.start.data, cx, cz, c.x, c.z, px, pz);
                    if (natural) {
                        // HollowTreeStructure#findGenerationPoint
                        int x = cx * 16 + ctx.random.nextInt(16);
                        int z = cz * 16 + ctx.random.nextInt(16);
                        auto seaFloor = gf::landmark::ext::firstOccupiedHeight(ctx.start.dimension, x, z, worldgen::Heightmap::OceanFloorWg);
                        auto surface  = gf::landmark::ext::firstOccupiedHeight(ctx.start.dimension, x, z, worldgen::Heightmap::WorldSurfaceWg);
                        if (!seaFloor || !surface) return false;
                        int worldY = *surface;
                        int height = std::min(kit::sampleInt(config.height, ctx.random) + worldY, maxBuildHeightOf(ctx.start.dimension)) - worldY;
                        if (height < 16 || (!config.allowInWater && *seaFloor < worldY)) return false;
                        // the noise biome at (x, worldY, z): F6's Structure#isValidBiome check at the center
                        int radius = kit::sampleInt(config.radius, ctx.random);
                        c          = core::GlobalPos{ctx.start.dimension, x, worldY, z};
                        auto context = gf::landmark::ext::contextRandom(ctx.start);
                        for (auto& p : hollowTreePieces(config, height, radius, x, worldY, z, *context)) ctx.pieces.push_back(std::move(p));
                        return true;
                    }
                    // HollowTreeStructure#generateFromSapling
                    int height = std::min(kit::sampleInt(config.height, ctx.random) + c.y, maxBuildHeightOf(ctx.start.dimension)) - c.y;
                    if (height < 16) return false;
                    int radius = kit::sampleInt(config.radius, ctx.random);
                    for (auto& p : hollowTreePieces(config, height, radius, c.x, c.y, c.z, ctx.random)) ctx.pieces.push_back(std::move(p));
                    return true;
                },
        });
    }
    gf::log().info("minorlandmarks: hollow trees registered");
}

} // namespace gf::feature::minorlandmarks
