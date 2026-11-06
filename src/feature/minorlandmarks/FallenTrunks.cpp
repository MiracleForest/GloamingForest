// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/type/FallenTrunkStructure.java
//   src/main/java/twilightforest/world/components/structures/fallentrunk/FallenTrunkPiece.java
//   src/main/java/twilightforest/world/components/structures/fallentrunk/Hole.java
//   src/main/java/twilightforest/world/components/structures/fallentrunk/TrunkUnderDensityFunction.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The fallen trunks (twilightforest:fallen_trunk): an F6 start (avoid_landmark_grid set, exclusion
// zone around the hollow trees) whose layout runs FallenTrunkStructure#findGenerationPoint; the
// piece and its hole are in core/minorlandmarks (gtested), the terraformer is
// TrunkUnderDensityFunction.
#include "feature/minorlandmarks/Internal.h"

#include <map>
#include <mutex>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"

#include "core/jrand/JRand.h"
#include "core/jrand/JavaOrder.h"
#include "core/jrand/Mth.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/landmark/Landmark.h"
#include "feature/tags/Tags.h"
#include "feature/worldgen/Worldgen.h"
#include "mod/Log.h"

namespace gf::feature::minorlandmarks {

namespace {

namespace kit = gf::features::kit;
namespace jr  = gf::core::jrand;

constexpr int kRadiuses[] = {1, 2, 4};
constexpr float kMossChance = 0.44F;
constexpr char const* kSpawnerMonsters[] = {"twilightforest:swarm_spider", "twilightforest:hostile_wolf", "minecraft:cave_spider"};

/// FallenTrunkPiece.
class FallenTrunkPiece final : public st::Piece {
public:
    FallenTrunkPiece(int length, int radius, Json log, std::string chestLootTable, cml::Dir orientation, BlockBox const& box, std::int64_t holeSeed)
    : Piece(kTFFallenTrunk),
      length(length),
      radius(radius),
      log(std::move(log)),
      chestLootTable(std::move(chestLootTable)),
      holeSeed(holeSeed) {
        bbox = box;
        setVanillaOrientation(*this, toOrientation(orientation));
        rebuild();
    }
    explicit FallenTrunkPiece(Json const& saved) : Piece(kTFFallenTrunk) { load(saved); }

    [[nodiscard]] Json save() const override {
        Json j                = Piece::save();
        j["length"]           = length;
        j["radius"]           = radius;
        j["log"]              = log;
        j["chest_loot_table"] = chestLootTable;
        j["hole_seed"]        = holeSeed;
        return j;
    }
    void load(Json const& d) override {
        Piece::load(d);
        length         = d.value("length", 17);
        radius         = d.value("radius", 1);
        log            = d.contains("log") ? d["log"] : simpleProvider("twilightforest:twilight_oak_log", Json{{"axis", "y"}});
        chestLootTable = d.value("chest_loot_table", std::string("twilightforest:fallen_trunk_loot"));
        // TF saves the seed with putLong and reads it back with getInt: a reloaded piece's hole uses
        // the low 32 bits
        holeSeed = static_cast<std::int32_t>(d.value("hole_seed", std::int64_t{0}));
        rebuild();
    }

    void addChildren(st::Piece&, std::vector<std::unique_ptr<st::Piece>>& list, RandomSource&) override {
        auto inflate = [](BlockBox b, int n) { return BlockBox{b.minX - n, b.minY - n, b.minZ - n, b.maxX + n, b.maxY + n, b.maxZ + n}; };
        list.push_back(std::make_unique<sb::UtilityPiece>(0, inflate(bbox, cml::kTrunkTerraformPieceSize)));
        list.push_back(std::make_unique<sb::UtilityPiece>(0, inflate(bbox, 3), false));
    }

    void postProcess(Region& level, RandomSource&, BlockBox const& box, BlockPos const& pos) override {
        jr::LegacyRandom random(core::mth::blockPosAsLong(pos.x, pos.y, pos.z));
        if (radius == kRadiuses[0]) generateSmallFallenTrunk(level, random, box, random.nextBoolean());
        if (radius == kRadiuses[1]) generateFallenTrunk(level, random, box, random.nextBoolean(), false);
        if (radius == kRadiuses[2]) generateFallenTrunk(level, random, box, false, random.nextBoolean());
    }

    int          length = 17, radius = 1;
    Json         log;
    std::string  chestLootTable;
    std::int64_t holeSeed = 0;
    std::unique_ptr<cml::FallenTrunk> geometry;

private:
    void rebuild() {
        geometry = std::make_unique<cml::FallenTrunk>(bbox, toDir(orientation), length, radius, holeSeed);
        mHole.clear();
        for (auto const& p : geometry->holeBlocks()) mHole.insert(core::mth::blockPosAsLong(p.x, p.y, p.z));
    }

    void generateSmallFallenTrunk(Region& level, RandomSource& random, BlockBox const& box, bool hasHole) {
        for (int dx = 0; dx <= 3; dx++)
            for (int dy = 0; dy <= 3; dy++) {
                if (std::abs(dx - 1.5) + std::abs(dy - 1.5) != 2) continue;
                generateTrunkRod(level, random, box, dx, dy, hasHole);
            }
    }

    void generateFallenTrunk(Region& level, RandomSource& random, BlockBox const& box, bool hasHole, bool hasSpawnerAndChests) {
        int hollow = radius / 2, diameter = radius * 2;
        for (int dx = 0; dx <= diameter; dx++)
            for (int dy = 0; dy <= diameter; dy++) {
                int dist = cml::octagonalDist(std::abs(dx - radius), std::abs(dy - radius));
                if (dist > radius || dist <= hollow) continue;
                generateTrunkRod(level, random, box, dx, dy, hasHole);
            }
        if (hasSpawnerAndChests) generateSpawnerAndChests(level, random, box);
    }

    void generateTrunkRod(Region& level, RandomSource& random, BlockBox const& box, int dx, int dy, bool hasHole) {
        for (int dz = cml::kTrunkErodedLength; dz < length - 1 - cml::kTrunkErodedLength; dz++) placeLog(level, logState(), dx, dy, dz, box, random, hasHole);
        for (int dz = cml::kTrunkErodedLength - 1; dz >= 0; dz--) {
            if (random.nextBoolean()) break;
            placeLog(level, logState(), dx, dy, dz, box, random, hasHole);
        }
        for (int dz = length - 1 - cml::kTrunkErodedLength; dz < length - 1; dz++) {
            if (random.nextBoolean()) break;
            placeLog(level, logState(), dx, dy, dz, box, random, hasHole);
        }
    }

    Block const& logState() {
        // log.getState(random, pos).trySetValue(AXIS, Z) (a simple provider: no draw)
        if (!mLog) {
            jr::LegacyRandom unused(0);
            Block const*     b = sampleProvider(log, unused, BlockPos{0, 0, 0});
            mLog               = &st::blocks::withJava(b ? *b : st::blocks::java("twilightforest:twilight_oak_log"), Json{{"axis", "z"}});
        }
        return *mLog;
    }

    void placeLog(Region& level, Block const& state, int x, int y, int z, BlockBox const& box, RandomSource& random, bool hasHole) {
        jr::LegacyRandom randomChild(random.nextLong());
        if (hasHole && z > cml::kTrunkErodedLength && z < length - 1 - cml::kTrunkErodedLength - 1) {
            BlockPos w = getWorldPos(x, y, z);
            if (mHole.contains(core::mth::blockPosAsLong(w.x, w.y, w.z))) return;
        }
        Block const& existing = getBlock(level, x, y, z, box);
        if (st::blocks::inTag(existing, "minecraft:replaceable_by_trees") || st::blocks::inTag(existing, "minecraft:flowers") || st::blocks::isAir(existing)
            || randomChild.nextBoolean()) {
            placeBlock(level, state, x, y, z, box);
            BlockPos up = getWorldPos(x, y + 1, z);
            if (randomChild.nextFloat() <= kMossChance && box.contains(up.x, up.y, up.z) && st::blocks::inTag(getBlock(level, x, y + 1, z, box), "minecraft:replaceable")) {
                if (auto const* moss = st::blocks::fromJava("twilightforest:moss_patch")) placeBlock(level, *moss, x, y + 1, z, box);
            }
        }
    }

    void generateSpawnerAndChests(Region& level, RandomSource& random, BlockBox const& box) {
        int      sx = radius, sy = 2, sz = (length - 1) / 2;
        BlockPos worldSpawnerPos = getWorldPos(sx, sy, sz);
        // Don't use main random for spawner due to desyncs in random.next() calls between chunks
        jr::LegacyRandom spawnerRandom(random.nextLong());
        if (box.contains(worldSpawnerPos.x, worldSpawnerPos.y, worldSpawnerPos.z))
            placeSpawnerAt(level, box, worldSpawnerPos, kSpawnerMonsters[spawnerRandom.nextInt(3)]);

        st::Orientation o = st::clockwise(orientation);
        if (mirror == st::Mirror::LeftRight) o = st::opposite(o);

        // the HashSet<Vec3i> of TF's chest offsets, in its iteration order
        std::vector<cml::Pos> first;
        for (int i = 0; i <= 6; i++) first.push_back({2, 1, -3 + i});
        first.push_back({1, 0, -3});
        first.push_back({1, 0, 2});
        auto order = [](std::vector<cml::Pos> const& v) {
            std::vector<int> hashes;
            for (auto const& p : v) hashes.push_back(jr::blockPosHash(p.x, p.y, p.z));
            std::vector<cml::Pos> out;
            for (auto i : jr::hashSetOrder(hashes)) out.push_back(v[i]);
            return out;
        };
        std::vector<cml::Pos> all = first;
        for (auto const& p : order(first)) all.push_back({-p.x, p.y, p.z});
        auto     list        = order(all);
        cml::Pos chestOffset = list[static_cast<std::size_t>(random.nextInt(static_cast<int>(list.size())))];

        st::Orientation  facing = chestOffset.x < 0 ? o : st::opposite(o);
        BlockPos         chestPos = getWorldPos(sx + chestOffset.x, sy + chestOffset.y, sz + chestOffset.z);
        jr::LegacyRandom chestRandom(random.nextLong());
        // StructurePiece#createChest(level, box, chestRandom, pos, table, state)
        if (box.contains(chestPos.x, chestPos.y, chestPos.z) && !st::blocks::is(level.getBlock(chestPos), "minecraft:chest"))
            st::blockentities::container(level, chestPos, "twilightforest:twilight_oak_chest", facing, chestLootTable, chestRandom.nextLong());
    }

    std::unordered_set<std::int64_t> mHole;
    Block const*                     mLog = nullptr;
};

/// BiomeSource#findBiomeHorizontal(x, y, z, radius, 1, predicate, random, false, sampler) != null.
bool hasInvalidNearbyBiome(std::string const& biomes, int x, int y, int z, int radius, RandomSource& random) {
    int  qx = x >> 2, qz = z >> 2, k = radius >> 2;
    bool found = false;
    int  count = 0;
    for (int l1 = -k; l1 <= k; l1++)
        for (int i2 = -k; i2 <= k; i2++) {
            std::string biome = worldgen::biomeAt(BlockPos{(qx + i2) << 2, y, (qz + l1) << 2});
            bool        valid = !biome.empty()
                     && (biomes.starts_with("#") ? gf::tags::has(gf::tags::Registry::Biome, biomes.substr(1), biome) : biomes == biome);
            if (!valid) {
                if (found) (void)random.nextInt(count + 1);
                found = true;
                ++count;
            }
        }
    return found;
}

struct TerraCache {
    BlockBox                                 box{};
    int                                      length = 0, radius = 0;
    std::int64_t                             seed   = 0;
    cml::Dir                                 dir    = cml::Dir::South;
    std::unique_ptr<cml::TrunkUnderDensity> density;
};

} // namespace

FallenTrunkConfig FallenTrunkConfig::fromJson(Json const& s) {
    FallenTrunkConfig c;
    c.length         = s.value("length", Json{{"type", "minecraft:uniform"}, {"min_inclusive", 17}, {"max_inclusive", 24}});
    c.bigTrunkLength = s.value("big_trunk_length", Json{{"type", "minecraft:uniform"}, {"min_inclusive", 22}, {"max_inclusive", 28}});
    c.log            = s.value("log", simpleProvider("twilightforest:twilight_oak_log", Json{{"axis", "y"}}));
    c.chestLootTable = s.value("chest_loot_table", c.chestLootTable);
    return c;
}

std::vector<std::unique_ptr<st::Piece>> fallenTrunkPieces(FallenTrunkConfig const& config, int length, int radius, cml::Dir orientation, BlockBox const& box,
                                                          std::int64_t holeSeed) {
    std::vector<std::unique_ptr<st::Piece>> pieces;
    auto  piece = std::make_unique<FallenTrunkPiece>(length, radius, config.log, config.chestLootTable, orientation, box, holeSeed);
    auto* raw   = piece.get();
    pieces.push_back(std::move(piece));
    jr::LegacyRandom unused(0); // addChildren draws nothing
    raw->addChildren(*raw, pieces, unused);
    return pieces;
}

double fallenTrunkTerraform(BlockBox const& box, cml::Dir orientation, int length, int radius, std::int64_t holeSeed, int x, int y, int z) {
    thread_local TerraCache cache;
    if (!cache.density || !(cache.box.minX == box.minX && cache.box.minY == box.minY && cache.box.minZ == box.minZ && cache.box.maxX == box.maxX
                            && cache.box.maxY == box.maxY && cache.box.maxZ == box.maxZ)
        || cache.length != length || cache.radius != radius || cache.seed != holeSeed || cache.dir != orientation) {
        cml::FallenTrunk piece(box, orientation, length, radius, holeSeed);
        cache.box     = box;
        cache.length  = length;
        cache.radius  = radius;
        cache.seed    = holeSeed;
        cache.dir     = orientation;
        cache.density = std::make_unique<cml::TrunkUnderDensity>(piece, radius == kRadiuses[2], 1, 2);
    }
    return cache.density->compute(x, y, z);
}

void installFallenTrunks() {
    st::registerPieceType(kTFFallenTrunk, [](Json const& saved) -> std::unique_ptr<st::Piece> { return std::make_unique<FallenTrunkPiece>(saved); });

    FallenTrunkConfig config;
    std::string       biomes = "#twilightforest:valid_hollow_tree_biomes";
    if (auto const* d = gf::landmark::descriptor(kFallenTrunk)) {
        config = FallenTrunkConfig::fromJson(d->extra);
        if (auto it = d->extra.find("biomes"); it != d->extra.end() && it->is_string()) biomes = it->get<std::string>();
    } else {
        config = FallenTrunkConfig::fromJson(Json::object());
    }

    gf::landmark::registerGenerator(kFallenTrunk, gf::landmark::Generator{
        .layout =
            [config, biomes](gf::landmark::LayoutContext& ctx) {
                int cx = ctx.start.center.x >> 4, cz = ctx.start.center.z >> 4;
                if (auto it = ctx.start.data.find("chunk"); it != ctx.start.data.end() && it->is_array() && it->size() == 2) {
                    cx = (*it)[0].get<int>();
                    cz = (*it)[1].get<int>();
                }
                auto& c = ctx.start.center;
                // FallenTrunkStructure#findGenerationPoint: RandomSource.create(seed + x * 14413411L + z * 43387781L)
                std::int64_t seed = core::mth::ladd(core::mth::ladd(ctx.start.seed, static_cast<std::int64_t>(cx) * 14413411LL),
                                                    static_cast<std::int64_t>(cz) * 43387781LL);
                jr::LegacyRandom random(seed);
                int              x = cx * 16 + random.nextInt(16);
                int              z = cz * 16 + random.nextInt(16);
                // a start placed by a command or a selftest keeps its center (natural starts arrive at
                // the chunk corner or, rebuilt, at the point found here)
                bool natural = (c.x == cx * 16 && c.z == cz * 16) || (c.x == cx * 16 + 7 && c.z == cz * 16 + 7) || (c.x == x && c.z == z);
                int  worldY  = c.y;
                if (natural) {
                    auto surface = gf::landmark::ext::firstOccupiedHeight(ctx.start.dimension, x, z, worldgen::Heightmap::WorldSurfaceWg);
                    if (!surface) return false;
                    worldY = *surface;
                } else {
                    x = c.x;
                    z = c.z;
                }
                int radius = kRadiuses[random.nextInt(3)];
                int length = radius == kRadiuses[2] ? kit::sampleInt(config.bigTrunkLength, random) : kit::sampleInt(config.length, random);
                if (natural) {
                    // isValidNoiseBiome is F6's Structure#isValidBiome check at the center
                    int maxLength = config.length.value("max_inclusive", 24);
                    if (hasInvalidNearbyBiome(biomes, x, worldY, z, maxLength, random)) return false;
                }
                cml::Dir orientation = cml::horizontalByIndex(random.nextInt(4));
                int      xySize      = radius > 1 ? radius * 2 + 1 : 4;
                int      zSize       = length - 1;
                BlockBox baseBox     = cml::orientBox(x, worldY, z, 0, 0, 0, xySize, xySize, zSize, orientation);
                int      targetY     = worldY;
                if (radius != kRadiuses[0] && natural) {
                    auto adjusted = gf::landmark::ext::adjustForTerrain(ctx.start.dimension, baseBox.minX, baseBox.minZ, baseBox.maxX, baseBox.maxZ, 4,
                                                                        worldgen::Heightmap::WorldSurfaceWg);
                    if (adjusted) targetY = *adjusted;
                }
                BlockBox     adjustedBox = cml::orientBox(x, targetY, z, 0, 0, 0, xySize, xySize, zSize, orientation);
                std::int64_t holeSeed    = random.nextLong();
                c                        = core::GlobalPos{ctx.start.dimension, x, adjustedBox.minY, z};
                for (auto& p : fallenTrunkPieces(config, length, radius, orientation, adjustedBox, holeSeed)) ctx.pieces.push_back(std::move(p));
                return true;
            },
        .terraform =
            [](gf::landmark::Start const& start, int x, int y, int z) {
                // getStructureTerraformer: the first piece (the trunk); its fields from the start's saved pieces
                thread_local std::map<std::tuple<int, int, int, int>, Json> tags;
                if (start.pieceBoxes.empty()) return 0.0;
                auto const& b   = start.pieceBoxes.front();
                auto        key = std::make_tuple(b.minX, b.minY, b.minZ, b.maxX * 31 + b.maxZ);
                auto        it  = tags.find(key);
                if (it == tags.end()) {
                    if (tags.size() > 64) tags.clear();
                    Json tag;
                    for (auto const& p : gf::landmark::ext::startPieces(start))
                        if (p.type == kTFFallenTrunk) {
                            tag = p.tag;
                            break;
                        }
                    it = tags.emplace(key, std::move(tag)).first;
                }
                Json const& t = it->second;
                if (!t.is_object() || !t.contains("radius")) return 0.0;
                int o = t.value("O", 0);
                static constexpr cml::Dir kFrom2D[] = {cml::Dir::South, cml::Dir::West, cml::Dir::North, cml::Dir::East};
                return fallenTrunkTerraform(b, kFrom2D[((o % 4) + 4) % 4], t.value("length", 17), t.value("radius", 1), t.value("hole_seed", std::int64_t{0}), x, y, z);
            },
    });
    gf::log().info("minorlandmarks: fallen trunks registered");
}

} // namespace gf::feature::minorlandmarks
