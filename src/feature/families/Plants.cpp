// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/SpecialFlowerPotBlock.java
//   src/main/java/twilightforest/block/SnowLoggable.java
//   src/main/java/twilightforest/block/TFBushBlock.java
//   src/main/java/twilightforest/block/LightableBlock.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Block families (F11): leaves (Java LeavesBlock: distance to a log, persistence, decay), saplings
// (SaplingBlock: two stages, the tree is params.feature placed through gf::features, bone meal),
// potted plants (FlowerPotBlock and TF's SpecialFlowerPotBlock), log / wood stripping (AxeItem),
// TF's snowloggable blocks (snow layers inside the block) and TF's lightable blocks.

#include <algorithm>
#include <cmath>
#include <random>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/Actor.h"
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

#include "feature/common/Interactions.h"
#include "feature/families/BlockEvents.h"
#include "feature/families/Families.h"
#include "feature/features/Features.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::families {

namespace ia = interactions;

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

void particle(BlockSource& region, BlockPos const& pos, std::string const& effect) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto dim = level->getDimension(region.getDimensionId()).lock();
    if (!dim) return;
    level->spawnParticleEffect(effect, Vec3{pos.x + .5f, pos.y + .5f, pos.z + .5f}, dim.get());
}

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

/// Java LeavesBlock.getOptionalDistanceAt: 0 for logs, the distance of leaves (ours: 0 = not
/// computed yet counts as 7), nothing else.
std::optional<int> distanceAt(Block const& b) {
    if (isLog(b)) return 0;
    if (auto const* e = entryOf(b); e && e->family == Family::Leaves) {
        int d = intState(b, state::Distance).value_or(7);
        return d == 0 ? 7 : d;
    }
    return std::nullopt;
}

int computeDistance(BlockSource& region, BlockPos const& pos) {
    int i = 7;
    for (int d = 0; d < 6; ++d) {
        Block const* n = blockAt(region, offset(pos, d));
        if (!n) continue;
        i = std::min(i, distanceAt(*n).value_or(7) + 1);
        if (i == 1) break;
    }
    return i;
}

bool decaying(Block const& b) {
    return intState(b, state::Persistent).value_or(0) == 0 && intState(b, state::Distance).value_or(7) == 7;
}

// ---- saplings ----------------------------------------------------------------------------------------

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

/// Java Level.getMaxLocalRawBrightness: block light or sky light minus the sky darkening.
int maxLocalRawBrightness(BlockSource& region, BlockPos const& pos) {
    auto pair = region.getBrightnessPair(pos);
    int  sky  = pair.sky->mValue;
    int  blk  = pair.block->mValue;
    auto level = ll::service::getLevel();
    if (level) {
        if (auto dim = level->getDimension(region.getDimensionId()).lock()) sky -= dim->mSkyDarken->mValue;
    }
    return std::max(blk, sky);
}

// ---- potted plants -------------------------------------------------------------------------------------

Entry const* pottedFor(std::string const& plantId) {
    for (auto const* e : entriesOf(Family::Potted))
        if (e->str("plant") == plantId) return e;
    return nullptr;
}

/// Bedrock's vanilla flower pot is empty (its update_bit is set while it holds a plant).
bool emptyVanillaPot(Block const& b) {
    if (b.getTypeName() != "minecraft:flower_pot") return false;
    auto bit = strState(b, "update_bit");
    return bit.empty() || bit == "0";
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
    // Java TreeGrower.growTree: the sapling makes way for the tree; it is back when nothing grew
    auto feature = e->str("feature");
    if (feature.empty()) return;
    Block const* sapling = b;
    game::clearBlock(region, pos);
    LiveRegion live(region, pos);
    struct Rand final : core::jrand::RandomSource {
        void         setSeed(std::int64_t s) override { r.seed(static_cast<std::uint64_t>(s)); }
        int          nextInt() override { return static_cast<int>(r() >> 32); }
        int          nextInt(int bound) override { return bound <= 0 ? 0 : static_cast<int>(r() % static_cast<std::uint64_t>(bound)); }
        std::int64_t nextLong() override { return static_cast<std::int64_t>(r()); }
        bool         nextBoolean() override { return (r() >> 63) != 0; }
        float        nextFloat() override { return static_cast<float>(r() >> 40) * 0x1.0p-24f; }
        double       nextDouble() override { return static_cast<double>(r() >> 11) * 0x1.0p-53; }
        double       nextGaussian() override { return std::normal_distribution<double>(0, 1)(r); }
        std::unique_ptr<core::jrand::RandomSource> fork() override {
            auto f = std::make_unique<Rand>();
            f->r.seed(r());
            return f;
        }
        std::unique_ptr<core::jrand::PositionalRandomFactory> forkPositional() override { return nullptr; }
        std::mt19937_64 r{std::random_device{}()};
    } random;
    bool grown = features::place(feature, live, pos, random);
    if (!grown) {
        game::setBlock(region, pos, *sapling);
        log().info("families: {} at {} {} {} could not grow {}", e->id, pos.x, pos.y, pos.z, feature);
    } else {
        log().info("families: {} at {} {} {} grew {}", e->id, pos.x, pos.y, pos.z, feature);
    }
}

void installPlants() {
    // ---- player placement: persistent leaves (Java getStateForPlacement), lit / snowy defaults
    for (auto const* e : entriesOf(Family::Leaves)) {
        ia::Match m;
        m.block = e->id;
        handlerIds().push_back(ia::onPlace(m, [](ia::PlaceContext const& c) {
            auto* region = regionOf(c.pos.dim);
            if (!region) return;
            BlockPos bp = game::blockPos(c.pos);
            setStates(c.pos, {{state::Persistent, 1}, {state::Distance, computeDistance(*region, bp)}});
        }));
    }
    // ---- saplings: bone meal (Java BoneMealItem / SaplingBlock.isBonemealSuccess 0.45)
    for (auto const* e : entriesOf(Family::Sapling)) {
        ia::Match m;
        m.block = e->id;
        m.item  = "minecraft:bone_meal";
        handlerIds().push_back(ia::onUseOn(m, [](ia::UseOnContext const& c) {
            auto& region = c.player.getDimensionBlockSource();
            BlockPos bp  = game::blockPos(c.pos);
            consumeHeld(c.player, c.item);
            particle(region, bp, "minecraft:crop_growth_emitter");
            if (nextFloat() < 0.45f) advanceSapling(region, bp);
            return ia::Result::Consume;
        }));
    }
    // ---- potted plants: plant into a vanilla pot, take it out
    handlerIds().push_back(ia::onUseOn({.block = "minecraft:flower_pot"}, [](ia::UseOnContext const& c) {
        if (c.itemId.empty()) return ia::Result::Pass;
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
            if (!e) return ia::Result::Pass;
            // Java FlowerPotBlock.useItemOn: another plant does nothing on a full pot
            if (!c.itemId.empty() && pottedFor(c.itemId)) return ia::Result::Consume;
            // Java useWithoutItem: the plant back to the player (TF's special pots keep it)
            if (!e->flag("special")) {
                if (auto plant = makeItem(e->str("plant")); !plant.isNull()) give(c.player, plant);
            }
            if (auto const* pot = game::blockWith("minecraft:flower_pot")) set(c.pos, *pot);
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
                auto const* e = entryOf(c.block);
                if (!e) return ia::Result::Pass;
                auto const* stripped = game::blockWith(e->str("stripped"));
                if (!stripped) return ia::Result::Pass;
                // keep the axis
                auto face = strState(c.block, state::BlockFace);
                if (!face.empty()) {
                    if (auto const* s = withStates(*stripped, {{state::BlockFace, face}})) stripped = s;
                }
                set(c.pos, *stripped);
                auto& region = c.player.getDimensionBlockSource();
                bedrockSound(region, game::blockPos(c.pos), "use.wood", 1.0f, 0.8f);
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
            auto& region = c.player.getDimensionBlockSource();
            BlockPos bp  = game::blockPos(c.pos);
            int layers   = intState(c.block, state::Layers).value_or(0);
            if (layers >= 8 || !snowCanSurvive(region, bp)) return ia::Result::Pass;
            setStates(c.pos, {{state::Layers, layers + 1}});
            consumeHeld(c.player, c.item);
            bedrockSound(region, bp, "dig.snow");
            return ia::Result::Consume;
        }));
        ia::Match d;
        d.block = e->id;
        handlerIds().push_back(ia::onDestroy(d, [](ia::DestroyContext const& c) {
            int layers = intState(c.block, state::Layers).value_or(0);
            if (c.sneaking || layers <= 0) return ia::Result::Pass;
            // breaking the block above its snow: the block drops, its snow stays as a snow layer
            auto& region = c.player.getDimensionBlockSource();
            BlockPos bp  = game::blockPos(c.pos);
            if (!c.player.isCreative()) {
                if (auto const* plain = withStates(c.block, {{state::Layers, 0}})) {
                    game::setBlock(region, bp, *plain);
                    region.getLevel().destroyBlock(region, bp, true, BlockChangeContext{});
                }
            }
            if (auto const* snow = game::blockWithStates("minecraft:snow_layer", {{"height", std::clamp(layers - 1, 0, 7)}})) game::setBlock(region, bp, *snow);
            return ia::Result::Consume;
        }));
    }
    // ---- lightable (TF LightableBlock.tryLightCandles / FIRESTARTER_LIGHT)
    for (auto const* e : entries()) {
        if (!e->has(Lightable)) continue;
        ia::Match m;
        m.block = e->id;
        handlerIds().push_back(ia::onUseOn(m, [](ia::UseOnContext const& c) {
            auto lighting = strState(c.block, state::Lighting);
            auto& region  = c.player.getDimensionBlockSource();
            BlockPos bp   = game::blockPos(c.pos);
            if (c.itemId.empty()) {
                if (lighting == "none" || lighting.empty()) return ia::Result::Pass;
                if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
                setStates(c.pos, {{state::Lighting, "none"}});
                particle(region, bp, "minecraft:basic_smoke_particle");
                bedrockSound(region, bp, "extinguish.candle");
                return ia::Result::Consume;
            }
            bool flint = c.itemId == "minecraft:flint_and_steel", charge = c.itemId == "minecraft:fire_charge";
            if ((!flint && !charge) || lighting != "none") return ia::Result::Pass;
            setStates(c.pos, {{state::Lighting, "normal"}});
            if (flint) {
                bedrockSound(region, bp, "fire.ignite");
                if (!c.player.isCreative()) {
                    ia::mutableItem(c.item).hurtAndBreak(1, &c.player);
                    c.player.sendInventory(false);
                }
            } else {
                bedrockSound(region, bp, "mob.ghast.fireball");
                consumeHeld(c.player, c.item);
            }
            return ia::Result::Consume;
        }));
    }

    static bool registered = false;
    if (registered) return;
    registered = true;
    for (auto const* e : entriesOf(Family::Leaves)) {
        // Java LeavesBlock.randomTick: decay without a log within 6 blocks
        blockevents::onRandomTick(e->id, [](BlockSource& region, BlockPos const& pos, Block const& block, blockevents::RandomSource&) {
            if (intState(block, state::Distance).value_or(0) == 0) {
                updateLeafDistance(region, pos);
                return;
            }
            if (!decaying(block)) return;
            region.getLevel().destroyBlock(region, pos, true, BlockChangeContext{});
            log().info("families: {} at {} {} {} decayed", block.getTypeName(), pos.x, pos.y, pos.z);
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
        blockevents::onRandomTick(e->id, [](BlockSource& region, BlockPos const& pos, Block const&, blockevents::RandomSource& random) {
            if (maxLocalRawBrightness(region, offset(pos, Up)) >= 9 && random.nextInt(7) == 0) advanceSapling(region, pos);
        });
    }
    for (auto const* e : entries()) {
        if (e->has(Snowloggable)) {
            // TF SnowLoggable.meltSnow
            // (the melted snow drops what a snow layer broken without a tool drops: nothing)
            blockevents::onRandomTick(e->id, [](BlockSource& region, BlockPos const& pos, Block const& block, blockevents::RandomSource&) {
                int layers = intState(block, state::Layers).value_or(0);
                if (layers <= 0 || layers >= 8 || region.getBrightnessPair(pos).block->mValue <= 11) return;
                if (auto const* plain = withStates(block, {{state::Layers, 0}}); plain && plain != &block) game::setBlock(region, pos, *plain);
            });
            // TFBushBlock.updateShape: the snow goes when it can't stay
            blockevents::onNeighborChanged(e->id, [](BlockSource& region, BlockPos const& pos, BlockPos const&) {
                Block const* b = blockAt(region, pos);
                if (!b || intState(*b, state::Layers).value_or(0) == 0 || snowCanSurvive(region, pos)) return;
                if (auto const* plain = withStates(*b, {{state::Layers, 0}})) game::setBlock(region, pos, *plain);
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
