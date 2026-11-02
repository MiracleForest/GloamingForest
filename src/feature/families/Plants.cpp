// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/SpecialFlowerPotBlock.java
//   src/main/java/twilightforest/block/SnowLoggable.java
//   src/main/java/twilightforest/block/TFBushBlock.java
//   src/main/java/twilightforest/block/LightableBlock.java
//   src/main/java/twilightforest/world/components/feature/trees/growers/StructureTreeGrower.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Block families (F11): leaves (Java LeavesBlock: distance to a log, persistence, decay), saplings
// (SaplingBlock: two stages; the tree is Java's TreeGrower from params.grower: the 2x2 mega tree,
// the secondary chance and trees, placed through gf::features; TF's StructureTreeGrower and units'
// own growers through feature/ext/families/Growers.h), potted plants (FlowerPotBlock and TF's
// SpecialFlowerPotBlock), log / wood stripping (AxeItem with TF's strippables), TF's snowloggable
// blocks (snow layers inside the block) and TF's lightable blocks.

#include <algorithm>
#include <cmath>
#include <optional>
#include <random>
#include <set>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorSoundIdentifier.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/BrightnessPair.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "feature/common/Interactions.h"
#include "feature/ext/families/Growers.h"
#include "feature/families/BlockEvents.h"
#include "feature/families/Families.h"
#include "feature/features/Features.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::families {

namespace ia = interactions;

namespace detail {
/// Selftests: replaces gf::features::place for saplings (nullptr: the real one).
using TreePlacer = std::function<bool(std::string const& feature, BlockSource& region, BlockPos const& pos)>;
TreePlacer& treePlacer() {
    static TreePlacer p;
    return p;
}
} // namespace detail

namespace {

std::vector<ia::HandlerId>& handlerIds() {
    static std::vector<ia::HandlerId> ids;
    return ids;
}

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
float nextFloat() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng()); }

// ---- leaves (Java LeavesBlock) -------------------------------------------------------------------

/// Java BlockTags.LOGS: family logs and woods, vanilla logs (Bedrock tag "log"), the data tag.
bool isLog(Block const& b) {
    if (auto const* e = entryOf(b)) {
        if (e->family == Family::Log || e->family == Family::Wood) return true;
        if (e->flag("log")) return true;
    }
    if (b.getBlockType().hasTag(HashedString("log"))) return true;
    return tags::has(tags::Registry::Block, "minecraft:logs", b.getTypeName());
}

/// Bedrock's vanilla leaves hold no Java distance: theirs is measured (Java's leaves keep it as a
/// state, which Java's getOptionalDistanceAt reads for any block with DISTANCE).
int vanillaLeafDistance(BlockSource& region, BlockPos const& start) {
    std::vector<std::pair<BlockPos, int>> frontier{{start, 0}};
    std::set<std::tuple<int, int, int>>   seen{{start.x, start.y, start.z}};
    for (std::size_t i = 0; i < frontier.size(); ++i) {
        auto [p, depth] = frontier[i];
        if (depth >= 7) continue;
        for (int d = 0; d < 6; ++d) {
            BlockPos     n = offset(p, d);
            Block const* b = blockAt(region, n);
            if (!b) continue;
            if (isLog(*b)) return depth + 1;
            if (!vanillaLeaves(*b) || !seen.insert({n.x, n.y, n.z}).second) continue;
            frontier.push_back({n, depth + 1});
        }
    }
    return 7;
}

/// Java LeavesBlock.getOptionalDistanceAt: 0 for logs, the distance of leaves (ours: 0 = not
/// computed yet counts as 7), nothing else.
std::optional<int> distanceAt(BlockSource& region, BlockPos const& pos, Block const& b) {
    if (isLog(b)) return 0;
    if (auto const* e = entryOf(b); e && e->family == Family::Leaves) {
        int d = intState(b, state::Distance).value_or(7);
        return d == 0 ? 7 : d;
    }
    if (vanillaLeaves(b)) return vanillaLeafDistance(region, pos);
    return std::nullopt;
}

int computeDistance(BlockSource& region, BlockPos const& pos) {
    int i = 7;
    for (int d = 0; d < 6; ++d) {
        BlockPos     n  = offset(pos, d);
        Block const* nb = blockAt(region, n);
        if (!nb) continue;
        i = std::min(i, distanceAt(region, n, *nb).value_or(7) + 1);
        if (i == 1) break;
    }
    return i;
}

bool decaying(Block const& b) { return !flagState(b, state::Persistent) && intState(b, state::Distance).value_or(7) == 7; }

// ---- saplings (Java SaplingBlock / TreeGrower) -----------------------------------------------------

/// Java's WorldGenRegion over the live world, for growing a tree from a sapling.
class LiveRegion final : public worldgen::Region {
public:
    LiveRegion(BlockSource& region, BlockPos const& at) : mRegion(region), mCx(at.x >> 4), mCz(at.z >> 4) {}
    int          centerChunkX() const override { return mCx; }
    int          centerChunkZ() const override { return mCz; }
    int          minY() const override { return mRegion.getMinHeight(); }
    int          maxY() const override { return mRegion.getMaxHeight(); }
    bool         isInside(int x, int z) const override { return mRegion.hasChunksAt(BlockPos{x, minY(), z}, 0, false); }
    Block const& getBlock(BlockPos const& pos) const override { return mRegion.getBlock(pos); }
    bool         setBlock(BlockPos const& pos, Block const& block, int) override {
        if (pos.y < minY() || pos.y >= maxY() || !mRegion.hasChunksAt(pos, 0, false)) return false;
        return game::setBlock(mRegion, pos, block);
    }
    int height(worldgen::Heightmap type, int x, int z) const override {
        bool leavesBlock = type != worldgen::Heightmap::MotionBlockingNoLeaves;
        for (int y = maxY() - 1; y >= minY(); --y) {
            Block const& b = mRegion.getBlock(BlockPos{x, y, z});
            if (b.isAir()) continue;
            if (!leavesBlock && b.getTypeName().find("leaves") != std::string::npos) continue;
            return y + 1;
        }
        return minY();
    }
    std::string  biome(BlockPos const& pos) const override { return worldgen::biomeAt(pos); }
    std::int64_t seed() const override { return static_cast<std::int64_t>(mRegion.getLevel().getSeed()); }
    int          dimension() const override { return static_cast<int>(mRegion.getDimensionId()); }

private:
    BlockSource& mRegion;
    int          mCx, mCz;
};

bool placeFeature(std::string const& feature, BlockSource& region, BlockPos const& pos, blockevents::RandomSource& random) {
    if (auto const& test = detail::treePlacer()) return test(feature, region, pos);
    LiveRegion live(region, pos);
    return features::place(feature, live, pos, random);
}

/// Java TreeGrower.isTwoByTwoSapling.
bool twoByTwo(BlockSource& region, BlockPos const& pos, BlockType const& sapling, int x, int z) {
    for (auto [dx, dz] : {std::pair{0, 0}, std::pair{1, 0}, std::pair{0, 1}, std::pair{1, 1}}) {
        Block const* b = blockAt(region, BlockPos{pos.x + x + dx, pos.y, pos.z + z + dz});
        if (!b || &b->getBlockType() != &sapling) return false;
    }
    return true;
}

/// Java TreeGrower.hasFlowers: a flower in the 5x3x5 box around the sapling.
bool hasFlowers(BlockSource& region, BlockPos const& pos) {
    for (int x = -2; x <= 2; ++x)
        for (int y = -1; y <= 1; ++y)
            for (int z = -2; z <= 2; ++z) {
                Block const* b = blockAt(region, BlockPos{pos.x + x, pos.y + y, pos.z + z});
                if (b && !b->isAir() && tags::has(tags::Registry::Block, "minecraft:flowers", b->getTypeName())) return true;
            }
    return false;
}

/// Java TreeGrower.growTree (params.grower; a plain params.feature is a grower with one tree).
bool growTree(BlockSource& region, BlockPos const& pos, Block const& sapling, Entry const& e, blockevents::RandomSource& random) {
    if (auto const* own = families_ext::growerOf(e.id)) return (*own)(region, pos, sapling, random);
    Json g = e.params.contains("grower") && e.params["grower"].is_object() ? e.params["grower"] : Json{{"feature", e.str("feature")}};
    auto str = [&](char const* key) { return g.contains(key) && g[key].is_string() ? g[key].get<std::string>() : std::string(); };
    if (auto structure = str("structure"); !structure.empty()) {
        // TF StructureTreeGrower: the structure's owner grows it
        if (auto const* grow = families_ext::structureGrowerOf(structure)) return (*grow)(region, pos, structure, random);
        static std::set<std::string> warned;
        if (warned.insert(structure).second) log().warn("families: no grower for the structure {} of {}", structure, e.id);
        return false;
    }
    float chance = g.value("secondary_chance", 0.0f);
    // getConfiguredMegaFeature: the secondary mega tree by chance, else the mega tree
    std::string mega;
    if (!str("secondary_mega_feature").empty() && random.nextFloat() < chance) mega = str("secondary_mega_feature");
    else mega = str("mega_feature");
    if (!mega.empty()) {
        auto const& type = sapling.getBlockType();
        for (int i = 0; i >= -1; --i) {
            for (int j = 0; j >= -1; --j) {
                if (!twoByTwo(region, pos, type, i, j)) continue;
                auto const* air = game::blockWith("minecraft:air");
                std::vector<BlockPos> four{{pos.x + i, pos.y, pos.z + j}, {pos.x + i + 1, pos.y, pos.z + j}, {pos.x + i, pos.y, pos.z + j + 1}, {pos.x + i + 1, pos.y, pos.z + j + 1}};
                std::vector<Block const*> before;
                for (auto const& p : four) {
                    before.push_back(blockAt(region, p));
                    if (air) game::setBlock(region, p, *air);
                }
                if (placeFeature(mega, region, four[0], random)) {
                    log().info("families: {} at {} {} {} grew {} (2x2)", e.id, pos.x, pos.y, pos.z, mega);
                    return true;
                }
                for (std::size_t k = 0; k < four.size(); ++k)
                    if (before[k]) game::setBlock(region, four[k], *before[k]);
                return false;
            }
        }
    }
    // getConfiguredFeature: the secondary tree by chance, flower variants next to flowers
    bool        flowers = hasFlowers(region, pos);
    std::string feature;
    if (random.nextFloat() < chance) {
        if (flowers && !str("secondary_flowers_feature").empty()) feature = str("secondary_flowers_feature");
        else if (!str("secondary_feature").empty()) feature = str("secondary_feature");
    }
    if (feature.empty()) feature = flowers && !str("flowers_feature").empty() ? str("flowers_feature") : str("feature");
    if (feature.empty()) return false;
    // the sapling makes way for the tree (its fluid stays); it is back when nothing grew
    Block const* liquid = &region.getExtraBlock(pos);
    if (liquid->isAir()) liquid = game::blockWith("minecraft:air");
    if (liquid) game::setBlock(region, pos, *liquid);
    if (placeFeature(feature, region, pos, random)) {
        log().info("families: {} at {} {} {} grew {}", e.id, pos.x, pos.y, pos.z, feature);
        return true;
    }
    game::setBlock(region, pos, sapling);
    log().info("families: {} at {} {} {} could not grow {}", e.id, pos.x, pos.y, pos.z, feature);
    return false;
}

/// Java Level.getMaxLocalRawBrightness: block light or sky light minus the sky darkening.
int maxLocalRawBrightness(BlockSource& region, BlockPos const& pos) {
    auto pair  = region.getBrightnessPair(pos);
    int  sky   = pair.sky->mValue;
    int  blk   = pair.block->mValue;
    auto level = ll::service::getLevel();
    if (level) {
        if (auto dim = level->getDimension(region.getDimensionId()).lock()) sky -= dim->mSkyDarken->mValue;
    }
    return std::max(blk, sky);
}

/// A RandomSource for bone meal (Java level.random).
class BoneMealRandom final : public blockevents::RandomSource {
public:
    void         setSeed(std::int64_t s) override { r.seed(static_cast<std::uint64_t>(s)); }
    int          nextInt() override { return static_cast<int>(r() >> 32); }
    int          nextInt(int bound) override { return bound <= 0 ? 0 : static_cast<int>(r() % static_cast<std::uint64_t>(bound)); }
    std::int64_t nextLong() override { return static_cast<std::int64_t>(r()); }
    bool         nextBoolean() override { return (r() >> 63) != 0; }
    float        nextFloat() override { return static_cast<float>(r() >> 40) * 0x1.0p-24f; }
    double       nextDouble() override { return static_cast<double>(r() >> 11) * 0x1.0p-53; }
    double       nextGaussian() override { return std::normal_distribution<double>(0, 1)(r); }
    std::unique_ptr<RandomSource> fork() override {
        auto f = std::make_unique<BoneMealRandom>();
        f->r.seed(r());
        return f;
    }
    std::unique_ptr<core::jrand::PositionalRandomFactory> forkPositional() override { return nullptr; }
    std::mt19937_64 r{std::random_device{}()};
};

// ---- potted plants -------------------------------------------------------------------------------------

Entry const* pottedFor(std::string const& plantId) {
    for (auto const* e : entriesOf(Family::Potted))
        if (e->str("plant") == plantId) return e;
    return nullptr;
}

/// The vanilla plants a flower pot takes (Java FlowerPotBlock.POTTED_BY_CONTENT, as the Bedrock
/// items, old and new names, plus Bedrock's newer pottable plants).
bool vanillaPottable(std::string_view id) {
    static constexpr std::string_view kPlants[] = {
        "minecraft:torchflower",        "minecraft:oak_sapling",       "minecraft:spruce_sapling",     "minecraft:birch_sapling",
        "minecraft:jungle_sapling",     "minecraft:acacia_sapling",    "minecraft:cherry_sapling",     "minecraft:dark_oak_sapling",
        "minecraft:pale_oak_sapling",   "minecraft:sapling",           "minecraft:mangrove_propagule", "minecraft:fern",
        "minecraft:tallgrass",          "minecraft:dandelion",         "minecraft:yellow_flower",      "minecraft:poppy",
        "minecraft:red_flower",         "minecraft:blue_orchid",       "minecraft:allium",             "minecraft:azure_bluet",
        "minecraft:red_tulip",          "minecraft:orange_tulip",      "minecraft:white_tulip",        "minecraft:pink_tulip",
        "minecraft:oxeye_daisy",        "minecraft:cornflower",        "minecraft:lily_of_the_valley", "minecraft:wither_rose",
        "minecraft:red_mushroom",       "minecraft:brown_mushroom",    "minecraft:deadbush",           "minecraft:dead_bush",
        "minecraft:cactus",             "minecraft:bamboo",            "minecraft:crimson_fungus",     "minecraft:warped_fungus",
        "minecraft:crimson_roots",      "minecraft:warped_roots",      "minecraft:azalea",             "minecraft:flowering_azalea",
        "minecraft:open_eyeblossom",    "minecraft:closed_eyeblossom",
    };
    return std::find(std::begin(kPlants), std::end(kPlants), id) != std::end(kPlants);
}

/// Bedrock's vanilla flower pot is empty (its update_bit is set while it holds a plant).
bool emptyVanillaPot(Block const& b) {
    if (b.getTypeName() != "minecraft:flower_pot") return false;
    auto bit = strState(b, "update_bit");
    return bit.empty() || bit == "0" || bit == "false";
}

// ---- stripping -----------------------------------------------------------------------------------------

bool isAxe(std::string const& id) {
    if (id.empty()) return false;
    if (tags::has(tags::Registry::Item, "minecraft:axes", id)) return true;
    return id.ends_with("_axe");
}

// ---- snowloggable --------------------------------------------------------------------------------------

/// Java SnowLayerBlock.canSurvive at `pos` (the block below holds snow).
bool snowCanSurvive(BlockSource& region, BlockPos const& pos) {
    Block const* below = blockAt(region, offset(pos, Down));
    if (!below || below->isAir()) return false;
    std::string const& id = below->getTypeName();
    if (id == "minecraft:ice" || id == "minecraft:packed_ice" || id == "minecraft:barrier") return false;
    if (id == "minecraft:honey_block" || id == "minecraft:soul_sand" || id == "minecraft:mud") return true;
    return sturdy(region, offset(pos, Down), Up);
}

/// Where the ray from `from` along `dir` (length 6) enters the box, nullopt when it misses.
std::optional<Vec3> clipBox(Vec3 const& from, Vec3 const& dir, AABB const& box) {
    float tmin = 0.f, tmax = 6.f;
    float o[3]{from.x, from.y, from.z}, d[3]{dir.x, dir.y, dir.z}, lo[3]{box.min.x, box.min.y, box.min.z}, hi[3]{box.max.x, box.max.y, box.max.z};
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(d[k]) < 1e-7f) {
            if (o[k] < lo[k] || o[k] > hi[k]) return std::nullopt;
            continue;
        }
        float t1 = (lo[k] - o[k]) / d[k], t2 = (hi[k] - o[k]) / d[k];
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return std::nullopt;
    }
    return Vec3{from.x + dir.x * tmin, from.y + dir.y * tmin, from.z + dir.z * tmin};
}

/// TF SnowLoggable.clip: the player's look against the block's outline (the plant's selection box
/// and its snow), the height of the hit inside the block.
std::optional<float> clipHeight(Player& player, BlockPos const& pos, Block const& block, Entry const& e, int layers) {
    Vec3 eye = player.getEyePos();
    Vec3 dir = player.getViewVector(1.0f);
    std::vector<AABB> boxes;
    // the plant's outline (params.shape, block pixels; else the block's visual shape)
    if (auto s = e.params.find("shape"); s != e.params.end() && s->is_array() && s->size() == 6) {
        auto v = [&](int i) { return (*s)[i].get<float>() / 16.f; };
        boxes.push_back(AABB{Vec3{pos.x + v(0), pos.y + v(1), pos.z + v(2)}, Vec3{pos.x + v(3), pos.y + v(4), pos.z + v(5)}});
    } else {
        AABB        buffer{};
        AABB const& outline = block.getBlockType().getVisualShape(block, buffer);
        boxes.push_back(AABB{Vec3{pos.x + outline.min.x, pos.y + outline.min.y, pos.z + outline.min.z}, Vec3{pos.x + outline.max.x, pos.y + outline.max.y, pos.z + outline.max.z}});
    }
    boxes.push_back(AABB{Vec3{float(pos.x), float(pos.y), float(pos.z)}, Vec3{pos.x + 1.f, pos.y + layers * 2 / 16.f, pos.z + 1.f}});
    std::optional<float> best, height;
    for (auto const& box : boxes) {
        auto hit = clipBox(eye, dir, box);
        if (!hit) continue;
        float dist = (hit->x - eye.x) * (hit->x - eye.x) + (hit->y - eye.y) * (hit->y - eye.y) + (hit->z - eye.z) * (hit->z - eye.z);
        if (!best || dist < *best) {
            best   = dist;
            height = hit->y - pos.y;
        }
    }
    return height;
}

} // namespace

int updateLeafDistance(BlockSource& region, BlockPos const& pos) {
    Block const* b = blockAt(region, pos);
    auto const*  e = b ? entryOf(*b) : nullptr;
    if (!e || e->family != Family::Leaves) return -1;
    int d = computeDistance(region, pos);
    if (auto const* n = withStates(*b, {{state::Distance, d}}); n && n != b) game::setBlock(region, pos, *n);
    return d;
}

void advanceSapling(BlockSource& region, BlockPos const& pos) {
    Block const* b = blockAt(region, pos);
    auto const*  e = b ? entryOf(*b) : nullptr;
    if (!e || e->family != Family::Sapling) return;
    if (intState(*b, state::Stage).value_or(0) == 0) {
        if (auto const* n = withStates(*b, {{state::Stage, 1}})) game::setBlock(region, pos, *n);
        return;
    }
    BoneMealRandom random;
    growTree(region, pos, *b, *e, random);
}

namespace detail {
/// Selftests: advance with a given random source (the grower's choices).
void advanceSaplingWith(BlockSource& region, BlockPos const& pos, blockevents::RandomSource& random) {
    Block const* b = blockAt(region, pos);
    auto const*  e = b ? entryOf(*b) : nullptr;
    if (!e || e->family != Family::Sapling) return;
    if (intState(*b, state::Stage).value_or(0) == 0) {
        if (auto const* n = withStates(*b, {{state::Stage, 1}})) game::setBlock(region, pos, *n);
        return;
    }
    growTree(region, pos, *b, *e, random);
}
} // namespace detail

void installPlants() {
    // ---- saplings: bone meal (Java BoneMealItem / SaplingBlock.isBonemealSuccess 0.45)
    for (auto const* e : entriesOf(Family::Sapling)) {
        ia::Match m;
        m.block = e->id;
        m.item  = "minecraft:bone_meal";
        handlerIds().push_back(ia::onUseOn(m, [](ia::UseOnContext const& c) {
            if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
            auto&    region = c.player.getDimensionBlockSource();
            BlockPos bp     = game::blockPos(c.pos);
            consumeHeld(c.player, c.item);
            // levelEvent 1505: the bone meal particles and sound
            particle(region, bp, "minecraft:crop_growth_emitter");
            bedrockSound(region, bp, "item.bone_meal.use");
            if (nextFloat() < 0.45f) advanceSapling(region, bp);
            return ia::Result::Consume;
        }));
    }
    // ---- potted plants: plant into a vanilla pot (FlowerPotBlock.useItemOn)
    handlerIds().push_back(ia::onUseOn({.block = "minecraft:flower_pot"}, [](ia::UseOnContext const& c) {
        // ServerPlayerGameMode.useItemOn: sneaking with an item skips the block (the item's own use)
        if (c.itemId.empty() || c.sneaking) return ia::Result::Pass;
        auto const* potted = pottedFor(c.itemId);
        if (!potted || !emptyVanillaPot(c.block)) return ia::Result::Pass;
        auto const* block = game::blockWith(potted->id);
        if (!block) return ia::Result::Pass;
        set(c.pos, *block);
        consumeHeld(c.player, c.item);
        log().info("families: {} potted {}", c.player.getRealName(), c.itemId);
        return ia::Result::Consume;
    }, ia::priority::High));
    for (auto const* e : entriesOf(Family::Potted)) {
        ia::Match m;
        m.block = e->id;
        handlerIds().push_back(ia::onUseOn(m, [](ia::UseOnContext const& c) {
            auto const* e = entryOf(c.block);
            if (!e || (c.sneaking && !c.itemId.empty())) return ia::Result::Pass; // sneaking with an item: the item's use
            if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
            // FlowerPotBlock.useItemOn: a plant (TF's or vanilla) on a full pot does nothing; any
            // other item takes the plant out (PASS_TO_DEFAULT_BLOCK_INTERACTION -> useWithoutItem)
            if (!c.itemId.empty() && (pottedFor(c.itemId) || vanillaPottable(c.itemId))) return ia::Result::Consume;
            if (!c.itemId.empty() && c.offhand) return ia::Result::Pass; // useWithoutItem: the main hand only
            // useWithoutItem: the plant back to the player (TF's SpecialFlowerPotBlock keeps it)
            if (!e->flag("special")) {
                if (auto plant = makeItem(e->str("plant")); !plant.isNull()) give(c.player, plant);
            }
            if (auto const* pot = game::blockWith("minecraft:flower_pot")) set(c.pos, *pot);
            log().info("families: {} emptied {}", c.player.getRealName(), e->id);
            return ia::Result::Consume;
        }));
    }
    // ---- stripping (Java AxeItem.useOn with the strippables)
    for (auto family : {Family::Log, Family::Wood, Family::Pillar}) {
        for (auto const* e : entriesOf(family)) {
            if (e->str("stripped").empty()) continue;
            ia::Match m;
            m.block = e->id;
            handlerIds().push_back(ia::onUseOn(m, [](ia::UseOnContext const& c) {
                if (!isAxe(c.itemId)) return ia::Result::Pass;
                // AxeItem.playerHasShieldUseIntent: a shield in the off hand, not sneaking
                if (!c.offhand && !c.sneaking && game::items::idOf(c.player.getOffhandSlot()) == "minecraft:shield") return ia::Result::Pass;
                if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
                auto const* e = entryOf(c.block);
                if (!e) return ia::Result::Pass;
                auto const* stripped = game::blockWith(e->str("stripped"));
                if (!stripped) return ia::Result::Pass;
                // AxeItem.getAxeStrippingState: the same axis
                auto axis = strState(c.block, state::Axis);
                if (!axis.empty()) {
                    if (auto const* s = withStates(*stripped, {{state::Axis, axis}})) stripped = s;
                }
                set(c.pos, *stripped);
                auto& region = c.player.getDimensionBlockSource();
                bedrockSound(region, game::blockPos(c.pos), "use.wood", 1.0f, 1.0f); // SoundEvents.AXE_STRIP
                if (!c.player.isCreative()) {
                    ia::mutableItem(c.item).hurtAndBreak(1, &c.player);
                    c.player.sendInventory(false);
                }
                log().info("families: {} stripped {} into {}", c.player.getRealName(), e->id, e->str("stripped"));
                return ia::Result::Consume;
            }));
        }
    }
    // ---- snowloggable (TF TFBushBlock.useItemOn with snow, SnowLoggable.handleBreakingLogic)
    for (auto const* e : entries()) {
        if (!e->has(Snowloggable)) continue;
        ia::Match m;
        m.block = e->id;
        m.item  = "minecraft:snow_layer";
        handlerIds().push_back(ia::onUseOn(m, [](ia::UseOnContext const& c) {
            auto&    region = c.player.getDimensionBlockSource();
            BlockPos bp     = game::blockPos(c.pos);
            int      layers = intState(c.block, state::Layers).value_or(0);
            if (layers >= 8 || !snowCanSurvive(region, bp)) return ia::Result::Pass;
            if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
            // SnowLoggable.isColliding: nobody inside the new snow
            AABB box{Vec3{float(bp.x), float(bp.y), float(bp.z)}, Vec3{bp.x + 1.f, bp.y + (layers + 1) * 2 / 16.f, bp.z + 1.f}};
            if (!region.isUnobstructedByEntities(box, nullptr)) return ia::Result::Deny;
            setStates(c.pos, {{state::Layers, layers + 1}});
            consumeHeld(c.player, c.item);
            // SoundEvents.SNOW_PLACE: the snow layer's place sound
            if (auto const* snow = game::blockWith("minecraft:snow_layer")) {
                region.getLevel().playSound(region, SharedTypes::Legacy::LevelSoundEvent::Place, Vec3{bp.x + .5f, bp.y + .5f, bp.z + .5f},
                                            static_cast<int>(snow->mNetworkId), ActorSoundIdentifier(c.player), false);
            }
            return ia::Result::Consume;
        }));
        ia::Match d;
        d.block = e->id;
        handlerIds().push_back(ia::onDestroy(d, [](ia::DestroyContext const& c) {
            int layers = intState(c.block, state::Layers).value_or(0);
            if (c.sneaking || layers <= 0) return ia::Result::Pass; // TFBushBlock: sneaking breaks it all
            auto&    region = c.player.getDimensionBlockSource();
            BlockPos bp     = game::blockPos(c.pos);
            auto const* se  = entryOf(c.block);
            if (!se) return ia::Result::Pass;
            auto     hit    = clipHeight(c.player, bp, c.block, *se, layers);
            Block const* plain = withStates(c.block, {{state::Layers, 0}});
            auto const*  snow  = game::blockWithStates("minecraft:snow_layer", {{"height", std::clamp(layers - 1, 0, 7)}});
            if (hit && *hit <= layers * 2 / 16.f + 1e-4f) {
                // the hit is in the snow: only the snow goes (levelEvent 2001 of snow)
                if (snow) breakEffect(region, bp, *snow);
                if (plain) game::setBlock(region, bp, *plain);
            } else {
                // the plant goes (its drops with the player's tool), its snow stays as a snow layer
                if (plain) breakEffect(region, bp, *plain);
                if (!c.player.isCreative() && plain) {
                    game::setBlock(region, bp, *plain);
                    plain->playerDestroy(c.player, bp);
                    if (ia::mutableItem(c.item).isDamageableItem()) {
                        ia::mutableItem(c.item).hurtAndBreak(1, &c.player);
                        c.player.sendInventory(false);
                    }
                }
                if (snow) game::setBlock(region, bp, *snow);
            }
            return ia::Result::Consume;
        }));
    }
    // ---- lightable (TF LightableBlock.tryLightCandles / FIRESTARTER_LIGHT)
    for (auto const* e : entries()) {
        if (!e->has(Lightable)) continue;
        ia::Match m;
        m.block = e->id;
        handlerIds().push_back(ia::onUseOn(m, [](ia::UseOnContext const& c) {
            auto     lighting = strState(c.block, state::Lighting);
            auto&    region   = c.player.getDimensionBlockSource();
            BlockPos bp       = game::blockPos(c.pos);
            if (c.itemId.empty()) {
                if (lighting == "none" || lighting.empty() || !mayBuild(c.player)) return ia::Result::Pass;
                if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
                // LightableBlock.extinguish: smoke, SoundEvents.CANDLE_EXTINGUISH
                setStates(c.pos, {{state::Lighting, "none"}});
                particle(region, bp, "minecraft:basic_smoke_particle");
                bedrockSound(region, bp, "extinguish.candle", 1.0f, 1.0f);
                return ia::Result::Consume;
            }
            bool flint = c.itemId == "minecraft:flint_and_steel", charge = c.itemId == "minecraft:fire_charge";
            if ((!flint && !charge) || lighting != "none") return ia::Result::Pass;
            if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
            setStates(c.pos, {{state::Lighting, "normal"}});
            if (flint) {
                // FlintAndSteelItem: SoundEvents.FLINTANDSTEEL_USE, pitch nextFloat() * 0.4 + 0.8
                bedrockSound(region, bp, "fire.ignite", 1.0f, nextFloat() * 0.4f + 0.8f);
                if (!c.player.isCreative()) {
                    ia::mutableItem(c.item).hurtAndBreak(1, &c.player);
                    c.player.sendInventory(false);
                }
            } else {
                // FireChargeItem: SoundEvents.FIRECHARGE_USE, pitch (nextFloat() - nextFloat()) * 0.2 + 1
                bedrockSound(region, bp, "mob.ghast.fireball", 1.0f, (nextFloat() - nextFloat()) * 0.2f + 1.0f);
                consumeHeld(c.player, c.item);
            }
            return ia::Result::Consume;
        }));
    }

    static bool registered = false;
    if (registered) return;
    registered = true;
    for (auto const* e : entriesOf(Family::Leaves)) {
        // Java LeavesBlock.randomTick: decay without a log within 6
        blockevents::onRandomTick(e->id, [](BlockSource& region, BlockPos const& pos, Block const& block, blockevents::RandomSource&) {
            if (intState(block, state::Distance).value_or(0) == 0) {
                updateLeafDistance(region, pos);
                return;
            }
            if (!decaying(block)) return;
            // dropResources + removeBlock: the drops, silently (no break particles or sound)
            std::string id = block.getTypeName();
            dropAndRemove(region, pos);
            log().debug("families: {} at {} {} {} decayed", id, pos.x, pos.y, pos.z);
        });
        // Java LeavesBlock.updateShape: a changed distance schedules the update (1 tick)
        auto update = [](BlockSource& region, BlockPos const& pos) {
            Block const* b = blockAt(region, pos);
            if (!b) return;
            int now = computeDistance(region, pos);
            if (intState(*b, state::Distance).value_or(0) == now) return;
            schedule(1, [gp = game::globalPos(region, pos)] {
                if (auto* r = regionOf(gp.dim)) updateLeafDistance(*r, game::blockPos(gp));
            });
        };
        blockevents::onNeighborChanged(e->id, [update](BlockSource& region, BlockPos const& pos, BlockPos const&) { update(region, pos); });
        blockevents::onPlaced(e->id, [update](BlockSource& region, BlockPos const& pos, Block const&, Actor*) { update(region, pos); });
    }
    for (auto const* e : entriesOf(Family::Sapling)) {
        // Java SaplingBlock.randomTick
        blockevents::onRandomTick(e->id, [](BlockSource& region, BlockPos const& pos, Block const& block, blockevents::RandomSource& random) {
            if (maxLocalRawBrightness(region, offset(pos, Up)) < 9 || random.nextInt(7) != 0) return;
            auto const* e = entryOf(block);
            if (!e) return;
            if (intState(block, state::Stage).value_or(0) == 0) {
                if (auto const* n = withStates(block, {{state::Stage, 1}})) game::setBlock(region, pos, *n);
                return;
            }
            growTree(region, pos, block, *e, random);
        });
    }
    for (auto const* e : entries()) {
        if (e->has(Snowloggable)) {
            // TF SnowLoggable.meltSnow (the melted snow drops what a snow layer broken without a
            // tool drops: nothing)
            blockevents::onRandomTick(e->id, [](BlockSource& region, BlockPos const& pos, Block const& block, blockevents::RandomSource&) {
                int layers = intState(block, state::Layers).value_or(0);
                if (layers <= 0 || layers >= 8 || region.getBrightnessPair(pos).block->mValue <= 11) return;
                if (auto const* plain = withStates(block, {{state::Layers, 0}}); plain && plain != &block) game::setBlock(region, pos, *plain);
            });
            // TFBushBlock.updateShape: the snow goes when it can't stay (with the snow's break effect)
            blockevents::onNeighborChanged(e->id, [](BlockSource& region, BlockPos const& pos, BlockPos const&) {
                Block const* b = blockAt(region, pos);
                if (!b || intState(*b, state::Layers).value_or(0) == 0 || snowCanSurvive(region, pos)) return;
                if (auto const* plain = withStates(*b, {{state::Layers, 0}})) game::setBlock(region, pos, *plain);
                if (auto const* snow = game::blockWith("minecraft:snow_layer")) breakEffect(region, pos, *snow);
            });
        }
        if (e->has(Lightable)) {
            // TF LightableBlock.lightCandlesWithProjectile: burning projectiles light it
            blockevents::onProjectileHit(e->id, [](BlockSource& region, BlockPos const& pos, Actor& projectile) {
                Block const* b = blockAt(region, pos);
                if (!b || !projectile.isOnFire() || strState(*b, state::Lighting) != "none") return;
                if (auto const* lit = withStates(*b, {{state::Lighting, "normal"}})) game::setBlock(region, pos, *lit);
            });
        }
    }
}

void uninstallPlants() {
    for (auto id : handlerIds()) ia::remove(id);
    handlerIds().clear();
}

} // namespace gf::feature::families
