// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/TFPortalBlock.java (getFluidState)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The Twilight portal is a little water: TFPortalBlock#getFluidState answers level-1 flowing water
// (Fluids.WATER.getFlowing(1, false), 1/9 of a block high). In Java every rule that asks for the
// fluid there sees water. Sugar cane and explosions are handled in Portal.cpp (and the block's
// explosion resistance); this file does what the water does to the entities in it and to the
// farmland around it, as the vanilla rules that read it (Entity#updateInWaterStateAndDoWaterCurrentPushing,
// LivingEntity#checkFallDamage, LivingEntity#aiStep's water sensitivity, FarmBlock#isNearWater):
//   - an entity whose box reaches below the water's surface counts as in water: its fall distance
//     resets (no fall damage when it lands in a portal pool), its fire goes out, it splashes when
//     it enters, and water-sensitive mobs (endermen, blazes, snow golems, striders) take drowning
//     damage;
//   - farmland with a portal within 4 blocks (its level or one above) stays moist.
// Water movement (swimming, the slower walk, items floating up) is the client's own physics on
// Bedrock and is not reproduced: a custom block has no fluid state the client could see.

#include <algorithm>
#include <cmath>
#include <random>
#include <unordered_set>

#include "ll/api/memory/Hook.h"

#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/FarmBlock.h"
#include "mc/world/level/block/block_events/BlockRandomTickEvent.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/dimension/Internal.h"
#include "feature/fx/Fx.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::dimension {

namespace {

/// FlowingFluid#getOwnHeight of level-1 water (amount / 9); a full block when the same fluid is
/// above (FlowingFluid#getHeight: hasSameAbove).
constexpr float kOwnHeight = 1.0f / 9.0f;

/// The entities in portal water this tick and the last (unique ids, rule R16).
struct WaterState {
    std::unordered_set<std::int64_t> now, before;
};
WaterState& water() {
    static WaterState w;
    return w;
}

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
float nextFloat() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng()); }

bool isWater(Block const& block) {
    auto id = game::blockId(block);
    return id == "minecraft:water" || id == "minecraft:flowing_water";
}

/// The water's height in the portal block at `p` (1 when water or another portal is above it).
float waterHeight(BlockSource& region, BlockPos const& p) {
    BlockPos above{p.x, p.y + 1, p.z};
    if (above.y >= region.getMaxHeight()) return kOwnHeight;
    Block const& a = region.getBlock(above);
    return isPortal(a) || isWater(a) || isWater(region.getExtraBlock(above)) ? 1.0f : kOwnHeight;
}

/// Entity#isSensitiveToWater (EnderMan, Blaze, SnowGolem, Strider).
bool sensitiveToWater(Actor const& actor) {
    switch (actor.getEntityTypeId()) {
    case ActorType::EnderMan:
    case ActorType::Blaze:
    case ActorType::SnowGolem:
    case ActorType::Strider:
        return true;
    default:
        return false;
    }
}

bool isBoat(Actor const& actor) {
    auto t = actor.getEntityTypeId();
    return t == ActorType::BoatRideable || t == ActorType::ChestBoatRideable;
}

/// Entity#doWaterSplashEffect: the splash sound and splash particles where the entity entered.
void splash(Actor& actor, BlockSource& region) {
    Vec3        at    = actor.getFeetPos();
    AABB const& box   = actor.getAABB();
    float       width = std::max(box.max.x - box.min.x, 0.1f);
    actor.playSound(region, SharedTypes::Legacy::LevelSoundEvent::Splash, at, -1);
    float y = std::floor(at.y) + 1.0f;
    int   n = static_cast<int>(1.0f + width * 20.0f);
    for (int i = 0; i < n; ++i) {
        float dx = (nextFloat() * 2.0f - 1.0f) * width;
        float dz = (nextFloat() * 2.0f - 1.0f) * width;
        fx::particle(region.getDimension(), "minecraft:splash", Vec3{at.x + dx, y, at.z + dz});
    }
}

} // namespace

bool inPortalWater(Actor& actor, BlockSource& region) {
    // Entity#updateFluidHeightAndDoFluidPushing: the box shrunk by 0.001, every block it reaches,
    // in water when the block's fluid surface is at or above the box's bottom
    AABB box = actor.getAABB();
    box.min  = box.min + Vec3(0.001f, 0.001f, 0.001f);
    box.max  = box.max - Vec3(0.001f, 0.001f, 0.001f);
    int x0 = static_cast<int>(std::floor(box.min.x)), x1 = static_cast<int>(std::ceil(box.max.x));
    int y0 = static_cast<int>(std::floor(box.min.y)), y1 = static_cast<int>(std::ceil(box.max.y));
    int z0 = static_cast<int>(std::floor(box.min.z)), z1 = static_cast<int>(std::ceil(box.max.z));
    y0     = std::max(y0, static_cast<int>(region.getMinHeight()));
    y1     = std::min(y1, static_cast<int>(region.getMaxHeight()));
    for (int x = x0; x < x1; ++x)
        for (int y = y0; y < y1; ++y)
            for (int z = z0; z < z1; ++z) {
                BlockPos p{x, y, z};
                if (!region.hasChunksAt(p, 0, false) || !isPortal(region.getBlock(p))) continue;
                if (static_cast<float>(y) + waterHeight(region, p) >= box.min.y) return true;
            }
    return false;
}

void portalWaterTick(Actor& actor, BlockSource& region) {
    if (!actor.isAlive() || !inPortalWater(actor, region)) return;
    // a boat's passengers stay dry above the water
    if (auto* vehicle = actor.getVehicle(); vehicle && isBoat(*vehicle)) return;
    auto  id = idOf(actor);
    auto& w  = water();
    if (!w.now.insert(id).second) return; // once a tick
    // Entity#updateInWaterStateAndDoWaterCurrentPushing: a splash when it enters (not on the tick
    // it was made), the fall distance resets, the fire goes out
    if (!w.before.contains(id) && actor.mTickCount > 1) splash(actor, region);
    actor.setFallDistance(0.0f);
    if (actor.isOnFire()) actor.stopFire();
    // LivingEntity#aiStep: isSensitiveToWater() && isInWaterRainOrBubble() -> hurt(drown, 1)
    if (sensitiveToWater(actor)) combat::hurt(actor, 1.0f, "minecraft:drown");
}

void portalWaterEndTick() {
    auto& w = water();
    std::swap(w.now, w.before);
    w.now.clear();
}

void portalWaterClear() {
    water().now.clear();
    water().before.clear();
}

bool portalWatersFarmland(BlockSource& region, BlockPos const& pos) {
    // FarmBlock#isNearWater: BlockPos.betweenClosed(pos.offset(-4, 0, -4), pos.offset(4, 1, 4))
    for (int dy = 0; dy <= 1; ++dy) {
        int y = pos.y + dy;
        if (y < region.getMinHeight() || y >= region.getMaxHeight()) continue;
        for (int dx = -4; dx <= 4; ++dx)
            for (int dz = -4; dz <= 4; ++dz) {
                BlockPos p{pos.x + dx, y, pos.z + dz};
                if (region.hasChunksAt(p, 0, false) && isPortal(region.getBlock(p))) return true;
            }
    }
    return false;
}

namespace {

/// Combat: fall damage of an entity in a portal's water (its fall distance resets there:
/// LivingEntity#checkFallDamage looks for water once more before the landing hurts).
void registerFallRule() {
    combat::onIncoming(combat::priority::High, [](combat::DamageInfo& d) {
        if (d.type != "minecraft:fall") return combat::Verdict::Pass;
        auto* region = game::regionOf(static_cast<int>(d.victim.getDimensionId()));
        if (!region || !inPortalWater(d.victim, *region)) return combat::Verdict::Pass;
        d.victim.setFallDistance(0.0f);
        log().debug("dimension: {} landed in a portal's water: no fall damage", d.victim.getTypeName());
        return combat::Verdict::Cancel;
    });
}

} // namespace

/// FarmBlock#randomTick with the portal's water: farmland near a portal is (made) fully moist, as
/// near any water; elsewhere the game's rule decides.
LL_TYPE_INSTANCE_HOOK(
    TwilightFarmlandHook,
    ll::memory::HookPriority::Normal,
    FarmBlock,
    &FarmBlock::randomTick,
    void,
    BlockEvents::BlockRandomTickEvent& event
) {
    BlockSource& region = event.mRegion;
    BlockPos     pos    = event.mPos;
    if (!portalWatersFarmland(region, pos)) return origin(event);
    Block const& state    = region.getBlock(pos);
    int          moisture = game::intState(state, "moisturized_amount").value_or(7);
    if (moisture >= 7) return;
    auto id = game::blockId(state);
    if (auto const* wet = game::blockWith(id, {{"moisturized_amount", 7}})) game::setBlock(region, pos, *wet);
}

void installPortalFluid() {
    static bool registered = false;
    if (!registered) {
        registered = true;
        registerFallRule(); // (the combat handlers live for the process)
    }
    TwilightFarmlandHook::hook();
}

void uninstallPortalFluid() {
    TwilightFarmlandHook::unhook();
    portalWaterClear();
}

} // namespace gf::feature::dimension
