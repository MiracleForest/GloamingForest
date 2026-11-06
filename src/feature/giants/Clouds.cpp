// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/CloudBlock.java
//   src/main/java/twilightforest/block/WispyCloudBlock.java
//   src/main/java/twilightforest/asmhooks/BlockHooks.java (isRainingAt)
//   src/main/java/twilightforest/client/event/CloudEvents.java (the vanilla clients' fallback)
//   net/minecraft/world/level/block/CauldronBlock.java, LayeredCauldronBlock.java (handlePrecipitation)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Cloud blocks. Fall damage on a cloud is 10 %; landing, walking and running on a cloud make
// cloud puffs (twilightforest:cloud_puff); the fluffy, rainy and snowy clouds' random ticks rain
// or snow on the first opaque block below them (cauldrons fill, snow layers grow, and, as Bedrock
// has no isRainingAt hook, the farmland moistens and fire goes out there: the listed deviation of
// docs/spec/giants.md 2.3). The client mod draws the rain / snow under clouds itself
// (src/client/giants); vanilla clients get rain streaks and splashes from the server.
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BrightnessPair.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/Weather.h"
#include "mc/world/phys/AABB.h"

#include "core/giants/Giants.h"
#include "feature/combat/Combat.h"
#include "feature/ext/giants/ParticleTo.h"
#include "feature/ext/giants/W0Internals.h"
#include "feature/families/BlockEvents.h"
#include "feature/fx/Fx.h"
#include "feature/giants/Giants.h"
#include "feature/net/Net.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::giants {

namespace cg = core::giants;
namespace fe = gf::feature::entity;

bool isCloud(std::string const& blockId) {
    return blockId == id::WispyCloud || blockId == id::FluffyCloud || blockId == id::RainyCloud || blockId == id::SnowyCloud;
}

int commonPrecipitationDistance() { return std::max(0, config().cloudBlockPrecipitationDistance); }

float cloudFallDamage(float amount) { return std::ceil(amount * 0.1f - 1e-4f); }

namespace {

float rainLevel(BlockSource& region) {
    auto* w = region.getDimension().mWeather.get();
    return w ? w->mRainLevel : 0.0f;
}

/// Biome#getPrecipitationAt: none without rain, snow when cold.
Precipitation biomePrecipitation(BlockSource& region, BlockPos const& pos) {
    Biome const& biome = region.getBiome(pos);
    if (!biome.mRain) return Precipitation::None;
    return biome.getTemperature(region, pos) < 0.15f ? Precipitation::Snow : Precipitation::Rain;
}

/// CauldronBlock.shouldHandlePrecipitation.
bool shouldHandlePrecipitation(core::jrand::RandomSource& r, Precipitation p) {
    if (p == Precipitation::Rain) return r.nextFloat() < 0.05f;
    if (p == Precipitation::Snow) return r.nextFloat() < 0.1f;
    return false;
}

/// Block#handlePrecipitation of the blocks that have one (cauldrons), plus the isRainingAt effects
/// Bedrock cannot get from a hook: farmland moisture, fire put out.
void handlePrecipitation(BlockSource& region, BlockPos const& pos, Precipitation p, core::jrand::RandomSource& r) {
    auto const& b   = region.getBlock(pos);
    auto        bid = game::blockId(b);
    if (bid == "minecraft:cauldron") {
        auto states = game::statesOf(b);
        int  level  = states.contains("fill_level") ? std::stoi(states["fill_level"]) : 0;
        auto liquid = states.contains("cauldron_liquid") ? states["cauldron_liquid"] : std::string("water");
        if (!shouldHandlePrecipitation(r, p)) return;
        if (level == 0) {
            // CauldronBlock: an empty cauldron becomes a water (rain) / powder snow (snow) cauldron of level 1
            auto const* filled = game::blockWithStates("minecraft:cauldron", {{"fill_level", 2}, {"cauldron_liquid", p == Precipitation::Rain ? "water" : "powder_snow"}});
            if (filled) game::setBlock(region, pos, *filled);
            return;
        }
        // LayeredCauldronBlock: +1 level (Bedrock fill levels 2 / 4 / 6) of its own precipitation
        bool same = (p == Precipitation::Rain && liquid == "water") || (p == Precipitation::Snow && liquid == "powder_snow");
        if (same && level < 6) {
            auto const* filled = game::blockWithStates("minecraft:cauldron", {{"fill_level", std::min(6, level + 2)}, {"cauldron_liquid", liquid}});
            if (filled) game::setBlock(region, pos, *filled);
        }
        return;
    }
    if (p == Precipitation::Rain && bid == "minecraft:farmland") {
        // FarmBlock#randomTick: isRainingAt(pos.above()) keeps it moist
        auto const* wet = game::blockWithStates("minecraft:farmland", {{"moisturized_amount", 7}});
        if (wet && &b != wet) game::setBlock(region, pos, *wet);
    }
    BlockPos above{pos.x, pos.y + 1, pos.z};
    auto const& a = region.getBlock(above);
    if (p == Precipitation::Rain && game::blockId(a) == "minecraft:fire") {
        // FireBlock#tick: rain puts it out with chance 0.2 + age * 0.03
        int age = game::intState(a, "age").value_or(0);
        if (r.nextFloat() < 0.2f + static_cast<float>(age) * 0.03f) game::clearBlock(region, above);
    }
}

/// CloudBlock.shouldSnow.
bool shouldSnow(BlockSource& region, BlockPos const& pos) {
    if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight()) return false;
    if (region.getBrightnessPair(pos).block.get().mValue >= 10) return false;
    auto const& b   = region.getBlock(pos);
    auto        bid = game::blockId(b);
    if (!(b.isAir() || bid == "minecraft:snow_layer")) return false;
    auto const* snow = game::blockWith("minecraft:snow_layer");
    return snow && snow->canSurvive(region, pos);
}

// ---- cloud puffs ------------------------------------------------------------------------------------

/// The last server tick each entity stood on a cloud (landing = standing again after a gap).
std::unordered_map<std::int64_t, std::uint64_t>& standing() {
    static std::unordered_map<std::int64_t, std::uint64_t> m;
    return m;
}
/// The fall distance of the last fall onto a cloud (from the fall damage).
std::unordered_map<std::int64_t, float>& lastFall() {
    static std::unordered_map<std::int64_t, float> m;
    return m;
}

/// CloudBlock#addEntityMovementParticles (half the time).
void movementParticles(BlockSource& region, BlockPos const& pos, Actor& entity, bool jumping) {
    auto& r = random();
    if (r.nextBoolean()) return;
    Vec3 const v = entity.getPosDelta();
    auto const at = entity.getFeetPos();
    auto const& box = entity.getAABB();
    double width = static_cast<double>(box.max.x - box.min.x);
    double mul   = jumping ? 2.0 : 1.0;
    double x     = at.x + (r.nextDouble() - 0.5) * width * mul;
    double y     = at.y + 0.1;
    double z     = at.z + (r.nextDouble() - 0.5) * width * mul;
    BlockPos bp  = game::blockPosOf(at);
    if (bp.x != pos.x) x = std::clamp(x, static_cast<double>(pos.x), pos.x + 1.0);
    if (bp.z != pos.z) z = std::clamp(z, static_cast<double>(pos.z), pos.z + 1.0);
    gf::fx::particle(region.getDimension(), id::CloudPuff, Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
                     Vec3{v.x * -0.5f, static_cast<float>(0.015 * mul), v.z * -0.5f});
}

/// CloudBlock#addLandingEffects.
void landingParticles(BlockSource& region, Actor& living, float fallDistance) {
    auto&  r      = random();
    int    maxI   = std::clamp(static_cast<int>(fallDistance) * 2, 8, 40);
    auto const& box = living.getAABB();
    double bbWidth = static_cast<double>(box.max.x - box.min.x);
    auto const at  = living.getFeetPos();
    double y       = at.y + 0.1;
    double ySpeed  = 0.0005 * maxI;
    for (int i = 0; i < maxI; i++) {
        double xSpd = (r.nextDouble() - 0.5) * bbWidth * 2.5;
        double zSpd = (r.nextDouble() - 0.5) * bbWidth * 2.5;
        gf::fx::particle(region.getDimension(), id::CloudPuff, Vec3{static_cast<float>(at.x + xSpd), static_cast<float>(y), static_cast<float>(at.z + zSpd)},
                         Vec3{static_cast<float>(xSpd * 0.0035 * maxI), static_cast<float>(ySpeed), static_cast<float>(zSpd * 0.0035 * maxI)});
    }
}

void steppedOn(BlockSource& region, BlockPos const& pos, Actor& entity) {
    if (entity.isPlayer() && static_cast<Player&>(entity).isSpectator()) return;
    auto uid  = fe::uidOf(entity);
    auto now  = fe::currentTick();
    auto& s   = standing();
    auto  it  = s.find(uid);
    bool landed = it == s.end() || now - it->second > 1;
    s[uid]      = now;
    if (landed) {
        float fall = 0.0f;
        if (auto f = lastFall().find(uid); f != lastFall().end()) {
            fall = f->second;
            lastFall().erase(f);
        }
        if (fall > 0.0f || entity.getFallDistance() > 0.0f) landingParticles(region, entity, std::max(fall, entity.getFallDistance()));
        return;
    }
    // CloudBlock#stepOn: canSpawnCloudParticles (moving, or 1 in 20; even ticks), then the puffs
    Vec3 const v = entity.getPosDelta();
    if (v.x == 0.0f && v.z == 0.0f && random().nextInt(20) != 0) return;
    if (entity.mTickCount % 2 != 0) return;
    movementParticles(region, pos, entity, false);
}

// ---- vanilla clients' weather under clouds -----------------------------------------------------------

struct Column {
    BlockPos      cloud;
    int           stopY = 0;
    Precipitation p     = Precipitation::None;
};
/// Per vanilla player: the raining clouds near it (CloudEvents' list, collected every 10 ticks).
std::unordered_map<std::int64_t, std::vector<Column>>& columns() {
    static std::unordered_map<std::int64_t, std::vector<Column>> m;
    return m;
}

/// CloudEvents#collectPrecipitationColumns around `at` (radius 8 for the server's fallback).
std::vector<Column> collect(BlockSource& region, BlockPos const& at, int distance) {
    std::vector<Column> out;
    constexpr int       r = 8;
    for (int dx = -r; dx <= r; ++dx)
        for (int dz = -r; dz <= r; ++dz) {
            for (int y = at.y - r; y < at.y + distance + r; ++y) {
                BlockPos p{at.x + dx, y, at.z + dz};
                if (y < region.getMinHeight() || y >= region.getMaxHeight() || !region.hasChunksAt(p, 0, false)) continue;
                auto bid = game::blockId(region.getBlock(p));
                if (!isCloud(bid)) continue;
                auto prec = cloudPrecipitation(region, p, bid);
                if (prec == Precipitation::None) continue;
                BlockPos below{p.x, p.y - 1, p.z};
                if (motionBlockingOpaque(region, below)) continue;
                int stop = cg::precipitationStopY(p.y, distance, [&](int yy) { return motionBlockingOpaque(region, BlockPos{p.x, yy, p.z}); });
                out.push_back({p, stop, prec});
                break; // the lowest raining cloud of the column
            }
        }
    return out;
}

void vanillaWeatherTick() {
    static std::uint64_t ticks = 0;
    ++ticks;
    int distance = commonPrecipitationDistance();
    auto level   = ll::service::getLevel();
    if (!level || distance <= 0) return;
    std::vector<std::int64_t> seen;
    level->forEachPlayer([&](Player& p) {
        if (p.isSimulatedPlayer() || net::isCompanion(p)) return true; // the client mod draws its own
        auto uid = fe::uidOf(p);
        seen.push_back(uid);
        auto& region = p.getDimensionBlockSource();
        auto  at     = game::blockPosOf(p.getFeetPos());
        auto& cols   = columns()[uid];
        if (ticks % 10 == 0) cols = collect(region, at, distance);
        if (cols.empty()) return true;
        auto& r = random();
        // a few drops a tick (TF draws sheets; the splashes are up to 100 a tick within 10 blocks)
        for (int i = 0; i < 4; ++i) {
            auto const& c = cols[static_cast<std::size_t>(r.nextInt(static_cast<int>(cols.size())))];
            float x = c.cloud.x + r.nextFloat(), z = c.cloud.z + r.nextFloat();
            float fall = static_cast<float>(c.cloud.y - c.stopY);
            char const* streak = c.p == Precipitation::Snow ? "gf:giants_cloud_snow" : "gf:giants_cloud_rain";
            gf::fx::giants_ext::vanillaParticleTo(p, streak, Vec3{x, static_cast<float>(c.cloud.y), z}, Vec3{0.0f, fall, 0.0f});
            if (c.p == Precipitation::Rain && i == 0) {
                BlockPos below{c.cloud.x, c.stopY - 1, c.cloud.z};
                auto     bid = game::blockId(region.getBlock(below));
                bool hot = bid == "minecraft:lava" || bid == "minecraft:magma" || bid == "minecraft:campfire" || bid == "minecraft:soul_campfire";
                gf::fx::giants_ext::vanillaParticleTo(p, hot ? "minecraft:smoke" : "minecraft:rain", Vec3{x, static_cast<float>(c.stopY), z});
            }
        }
        return true;
    });
    if (ticks % 200 == 0) std::erase_if(columns(), [&](auto const& e) { return std::find(seen.begin(), seen.end(), e.first) == seen.end(); });
}

} // namespace

bool motionBlockingOpaque(BlockSource& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    if (b.isAir()) return false;
    if (structure::blocks::blocksMotion(b)) return true;
    // a fluid (the block or its liquid layer)
    if (structure::blocks::isLiquid(b)) return true;
    return structure::blocks::isLiquid(region.getLiquidBlock(pos));
}

Precipitation cloudPrecipitation(BlockSource& region, BlockPos const& pos, std::string const& blockId) {
    if (blockId == id::RainyCloud) return Precipitation::Rain;
    if (blockId == id::SnowyCloud) return Precipitation::Snow;
    if (blockId == id::FluffyCloud) {
        if (rainLevel(region) > 0.0f) return biomePrecipitation(region, pos);
        return Precipitation::None;
    }
    return Precipitation::None; // the wispy cloud
}

void cloudRandomTick(BlockSource& region, BlockPos const& pos, std::string const& blockId, core::jrand::RandomSource& r) {
    int distance = commonPrecipitationDistance();
    if (!region.hasChunksAt(pos, 1, false) || distance == 0) return;
    auto p = cloudPrecipitation(region, pos, blockId);
    if (p == Precipitation::None) return;
    int highest = cg::highestRainyBlock(pos.y, distance, [&](int y) { return motionBlockingOpaque(region, BlockPos{pos.x, y, pos.z}); });
    if (highest <= region.getMinHeight()) return;
    if (p == Precipitation::Snow) {
        // GameRules.RULE_SNOW_ACCUMULATION_HEIGHT: Bedrock has no such rule, Java's default 1
        int      snowHeight = 1;
        BlockPos snowOn{pos.x, highest + 1, pos.z};
        if (snowHeight > 0 && shouldSnow(region, snowOn)) {
            auto const& s = region.getBlock(snowOn);
            if (game::blockId(s) == "minecraft:snow_layer") {
                int k = game::intState(s, "height").value_or(0) + 1; // Bedrock height 0 = Java layers 1
                if (k < std::min(snowHeight, 8)) {
                    if (auto const* more = game::blockWithStates("minecraft:snow_layer", {{"height", k}})) game::setBlock(region, snowOn, *more);
                }
            } else if (auto const* snow = game::blockWith("minecraft:snow_layer")) {
                game::setBlock(region, snowOn, *snow);
            }
        }
    }
    handlePrecipitation(region, BlockPos{pos.x, highest, pos.z}, p, r);
}

bool rainingFromCloudAt(BlockSource& region, BlockPos const& pos) {
    int distance = commonPrecipitationDistance();
    if (distance <= 0 || !region.hasChunksAt(pos, 0, false)) return false;
    return cg::rainingAbove(pos.y, distance, [&](int y) {
        BlockPos p{pos.x, y, pos.z};
        if (y >= region.getMaxHeight()) return cg::RainScan::Other;
        auto bid = game::blockId(region.getBlock(p));
        if (isCloud(bid) && cloudPrecipitation(region, p, bid) == Precipitation::Rain) return cg::RainScan::Raining;
        if (motionBlockingOpaque(region, p)) return cg::RainScan::Opaque;
        return cg::RainScan::Other;
    });
}

void installClouds() {
    for (char const* cloud : {id::FluffyCloud, id::RainyCloud, id::SnowyCloud}) {
        std::string idStr = cloud;
        gf::blockevents::onRandomTick(cloud, [idStr](BlockSource& region, BlockPos const& pos, Block const&, gf::blockevents::RandomSource& r) {
            cloudRandomTick(region, pos, idStr, r);
        });
    }
    for (char const* cloud : {id::WispyCloud, id::FluffyCloud, id::RainyCloud, id::SnowyCloud})
        gf::blockevents::onSteppedOn(cloud, [](BlockSource& region, BlockPos const& pos, Actor& entity) { steppedOn(region, pos, entity); });
    // CloudBlock#fallOn: causeFallDamage(fallDistance, 0.1F)
    gf::combat::onIncoming(gf::combat::priority::Normal, [](gf::combat::DamageInfo& d) {
        if (d.type != "minecraft:fall") return gf::combat::Verdict::Pass;
        auto&    region = d.victim.getDimensionBlockSource();
        auto     feet   = d.victim.getFeetPos();
        BlockPos under  = game::blockPosOf(Vec3{feet.x, feet.y - 0.2f, feet.z});
        if (!isCloud(game::blockId(region.getBlock(under)))) return gf::combat::Verdict::Pass;
        lastFall()[fe::uidOf(d.victim)] = d.amount + 3.0f; // Java's fall distance behind the damage
        d.amount = cloudFallDamage(d.amount);
        return d.amount <= 0.0f ? gf::combat::Verdict::Cancel : gf::combat::Verdict::Pass;
    });
    fe::onLevelTick([] {
        vanillaWeatherTick();
        auto now = fe::currentTick();
        if (now % 200 == 0) std::erase_if(standing(), [now](auto const& e) { return now - e.second > 200; });
    });
}

} // namespace gf::feature::giants
