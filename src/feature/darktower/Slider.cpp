// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/SliderBlock.java
//   src/main/java/twilightforest/entity/SlideBlock.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "feature/darktower/Slider.h"

#include <cmath>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/darktower/Runtime.h"
#include "feature/darktower/Util.h"
#include "feature/entity/Entity.h"
#include "feature/families/BlockEvents.h"
#include "game/Blocks.h"
#include "game/Items.h"

namespace gf::feature::darktower::slider {

namespace dt = core::darktower;
using Facing = dt::Facing;

namespace {

constexpr int    kWarmup      = 20;
constexpr double kPlayerRange = 32.0;
constexpr char   kAxis[]      = "twilightforest:axis";
constexpr char   kDelay[]     = "twilightforest:delay";
constexpr char   kData[]      = "darktower:slide";

bool isWater(Block const& b);

bool anyPlayerInRange(BlockSource& region, BlockPos const& pos) { return nearestPlayer(dimOf(region), center(pos), kPlayerRange) != nullptr; }

bool sameState(BlockSource& region, BlockPos const& a, BlockPos const& b) { return &region.getBlock(a) == &region.getBlock(b); }

bool connectedRecursive(BlockSource& region, BlockPos const& pos, Facing dir, int depth = 0) {
    BlockPos d = offset(pos, dir);
    if (depth > 256 || !sameState(region, pos, d)) return false;
    return anyPlayerInRange(region, d) || connectedRecursive(region, d, dir, depth + 1);
}

bool connectedInRange(BlockSource& region, BlockPos const& pos) {
    auto axis = textState(region.getBlock(pos), kAxis);
    Facing a = Facing::Up, b = Facing::Down;
    if (axis == "x") a = Facing::West, b = Facing::East;
    if (axis == "z") a = Facing::North, b = Facing::South;
    return anyPlayerInRange(region, pos) || connectedRecursive(region, pos, a) || connectedRecursive(region, pos, b);
}

/// The block box at `pos` has a collision shape (Java's move() would collide with it).
bool collides(BlockSource& region, BlockPos const& pos) {
    Block const& b = region.getBlock(pos);
    if (b.isAir()) return false;
    std::vector<AABB> boxes;
    b.getBlockType().addCollisionShapes(b, region, pos, nullptr, boxes, {});
    return !boxes.empty();
}

/// A 0.98 box at `feet` (x, z centre; y bottom) overlaps a colliding block.
bool boxCollides(BlockSource& region, Vec3 const& feet) {
    double h = 0.49;
    int    x0 = int(std::floor(feet.x - h + 1e-4)), x1 = int(std::floor(feet.x + h - 1e-4));
    int    y0 = int(std::floor(feet.y + 1e-4)), y1 = int(std::floor(feet.y + 0.98 - 1e-4));
    int    z0 = int(std::floor(feet.z - h + 1e-4)), z1 = int(std::floor(feet.z + h - 1e-4));
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                if (collides(region, BlockPos{x, y, z})) return true;
    return false;
}

void sliderTick(BlockSource& region, BlockPos const& pos, Block const& block) {
    if (connectedInRange(region, pos)) {
        if (auto* e = spawnMob(region, id::SlideEntity, Vec3{pos.x + 0.5f, float(pos.y), pos.z + 0.5f})) {
            auto& d   = gf::entity::data(*e, kData);
            d         = Json::object();
            d["time"] = 0;
            d["axis"] = textState(block, kAxis);
            d["delay"] = game::intState(block, kDelay).value_or(0);
            // SlideBlock#myState: the whole state (Bedrock keeps waterlogging as the water of the
            // liquid layer, so that is part of it too)
            d["states"] = blockStates(block);
            d["water"]  = isWater(region.getLiquidBlock(pos));
            d["pos"]  = {pos.x + 0.5, double(pos.y), pos.z + 0.5};
            d["vel"]  = {0.0, 0.0, 0.0};
            d["home"] = {pos.x, pos.y, pos.z};
            // determineMoveDirection
            static constexpr Facing kY[] = {Facing::West, Facing::East, Facing::North, Facing::South};
            static constexpr Facing kX[] = {Facing::Down, Facing::Up, Facing::North, Facing::South};
            static constexpr Facing kZ[] = {Facing::Down, Facing::Up, Facing::West, Facing::East};
            auto        axis  = d["axis"].get<std::string>();
            Facing const* check = axis == "x" ? kX : axis == "z" ? kZ : kY;
            auto        opp   = [](Facing f) { return static_cast<Facing>(static_cast<int>(f) ^ 1); };
            int         dir   = static_cast<int>(Facing::Down);
            bool        found = false;
            for (int i = 0; i < 4 && !found; ++i)
                if (isAir(region, offset(pos, check[i])) && !isAir(region, offset(pos, opp(check[i])))) dir = static_cast<int>(check[i]), found = true;
            for (int i = 0; i < 4 && !found; ++i)
                if (isAir(region, offset(pos, check[i]))) dir = static_cast<int>(check[i]), found = true;
            d["dir"] = dir;
            int axisIndex = axis == "x" ? 1 : axis == "z" ? 2 : 0;
            gf::entity::setProperty(*e, "twilightforest:axis", axisIndex);
        }
    }
    scheduleUpdate(region, pos);
}

bool isWater(Block const& b) {
    auto id = game::blockId(b);
    return id == "minecraft:water" || id == "minecraft:flowing_water";
}

/// Level#isUnobstructed(state, pos, CollisionContext.empty()): no other entity that blocks building
/// intersects the slider's shape at `pos` (the 6 x 6 px column along its axis).
bool unobstructed(BlockSource& region, BlockPos const& pos, Actor& self, std::string const& axis) {
    float lo = 0.3125f, hi = 0.6875f;
    Vec3  mn{pos.x + lo, pos.y + lo, pos.z + lo}, mx{pos.x + hi, pos.y + hi, pos.z + hi};
    if (axis == "x") mn.x = float(pos.x), mx.x = pos.x + 1.0f;
    else if (axis == "z") mn.z = float(pos.z), mx.z = pos.z + 1.0f;
    else mn.y = float(pos.y), mx.y = pos.y + 1.0f;
    for (auto* a : actorsIn(region, AABB{mn, mx})) {
        if (a == &self || !a->isAlive() || a->isSpectator()) continue;
        auto t = typeOf(*a);
        // Entity#blocksBuilding: living entities, boats, minecarts...; not items, xp or projectiles
        if (t == "minecraft:item" || t == "minecraft:xp_orb" || t == "minecraft:arrow" || t == id::SlideEntity) continue;
        if (asMob(*a) || t == "minecraft:boat" || t == "minecraft:chest_boat" || t.find("minecart") != std::string::npos) return false;
    }
    return true;
}

void dropSelf(Actor& self) {
    if (auto stack = game::items::make(id::Slider)) self.spawnAtLocation(*stack, 0.0f);
}

void damageKnockback(BlockSource& region, Actor& self, Vec3 const& p) {
    AABB box{Vec3{p.x - 0.49f, p.y, p.z - 0.49f}, Vec3{p.x + 0.49f, p.y + 0.98f, p.z + 0.49f}};
    for (auto* a : actorsIn(region, box)) {
        if (a == &self || !asMob(*a) || typeOf(*a) == id::SlideEntity) continue;
        combat::hurt(*a, 5.0f, "twilightforest:slider");
        auto q = a->getFeetPos();
        knockback(*a, 2.0, (p.x - q.x) * 2.0, (p.z - q.z) * 2.0);
    }
}

void slideTick(Actor& self) {
    auto& d = gf::entity::data(self, kData);
    if (!d.is_object() || !d.contains("pos")) {
        self.remove();
        return;
    }
    auto& region = self.getDimensionBlockSource();
    int   time   = d["time"].get<int>() + 1;
    d["time"]    = time;
    Vec3  p{d["pos"][0].get<float>(), d["pos"][1].get<float>(), d["pos"][2].get<float>()};
    Vec3  v{d["vel"][0].get<float>(), d["vel"][1].get<float>(), d["vel"][2].get<float>()};
    auto  dir    = static_cast<Facing>(d["dir"].get<int>());
    bool  hit    = false;
    if (time > kWarmup) {
        auto o = dt::offsetOf(dir);
        v      = Vec3{v.x + o[0] * 0.04f, v.y + o[1] * 0.04f, v.z + o[2] * 0.04f};
        // Entity#move(SELF): sweep the box along the motion (steps of at most 0.49, so no block is
        // skipped at the slider's 1.6 blocks per tick) and clip it flush against the first block in
        // the way (vertical or horizontal collision)
        double len   = std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z);
        int    steps = std::max(1, static_cast<int>(std::ceil(len / 0.49)));
        double done  = 0.0;
        for (int i = 1; i <= steps && !hit; ++i) {
            double f = double(i) / steps;
            Vec3   q{float(p.x + v.x * f), float(p.y + v.y * f), float(p.z + v.z * f)};
            if (!boxCollides(region, q)) {
                done = f;
                continue;
            }
            hit       = true;
            double lo = done, hi = f;
            for (int k = 0; k < 16; ++k) {
                double mid = (lo + hi) / 2;
                if (boxCollides(region, Vec3{float(p.x + v.x * mid), float(p.y + v.y * mid), float(p.z + v.z * mid)})) hi = mid;
                else lo = mid;
            }
            done = lo;
        }
        p = Vec3{float(p.x + v.x * done), float(p.y + v.y * done), float(p.z + v.z * done)};
    }
    d["pos"] = {p.x, p.y, p.z};
    d["vel"] = {v.x, v.y, v.z};
    self.teleportTo(p, false, 0, 0, false);
    if (time % 5 == 0) soundAt(region, p, "block.twilightforest.slider.move", 1.0f, 0.9f + random().nextFloat() * 0.4f);
    BlockPos pos{int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z))};
    Json const  states = d.contains("states") ? d["states"] : Json{{kAxis, d["axis"].get<std::string>()}, {kDelay, d.value("delay", 0)}};
    bool const  water  = d.value("water", false);
    auto const* state  = game::blockWithStates(id::Slider, states);
    if (time == 1) {
        if (!state || &region.getBlock(pos) != state || isWater(region.getLiquidBlock(pos)) != water) {
            self.remove();
            return;
        }
        remove(region, pos);
    }
    if (time == kWarmup + 40) {
        d["vel"] = {0.0, 0.0, 0.0};
        d["dir"] = static_cast<int>(dir) ^ 1;
    }
    if (hit) {
        self.remove();
        // level.isUnobstructed(myState, pos, CollisionContext.empty()): no entity in the way of the
        // slider's shape there (the block that is there is replaced, as setBlockAndUpdate does)
        if (state && unobstructed(region, pos, self, d["axis"].get<std::string>())) {
            set(region, pos, id::Slider, states);
            if (water) {
                if (auto const* w = game::blockWithStates("minecraft:water", Json::object())) region.setExtraBlock(pos, *w, 3);
            }
        } else {
            dropSelf(self);
        }
        return;
    }
    if ((time > 100 && (pos.y < region.getMinHeight() + 1 || pos.y > region.getMaxHeight())) || time > 600) {
        dropSelf(self);
        self.remove();
        return;
    }
    damageKnockback(region, self, p);
}

} // namespace

void scheduleUpdate(BlockSource& region, BlockPos const& pos) {
    int delay = game::intState(region.getBlock(pos), kDelay).value_or(0);
    auto* l   = level();
    runtime::schedule(region, pos, dt::sliderTickDelay(l ? static_cast<std::int64_t>(l->getCurrentServerTick().tickID) : 0, delay));
}

void knockback(Actor& target, double strength, double x, double z) {
    if (target.isSpectator()) return;
    while (x * x + z * z < 1.0E-5) {
        x = (random().nextDouble() - random().nextDouble()) * 0.01;
        z = (random().nextDouble() - random().nextDouble()) * 0.01;
    }
    double len = std::sqrt(x * x + z * z);
    double vx = x / len * strength, vz = z / len * strength;
    auto   old = target.getPosDelta();
    bool   onGround = target.isOnGround();
    setMotion(target, Vec3{float(old.x / 2.0 - vx), onGround ? float(std::min(0.4, old.y / 2.0 + strength)) : old.y, float(old.z / 2.0 - vz)});
}

void install() {
    runtime::onTick(id::Slider, sliderTick);
    blockevents::onPlaced(id::Slider, [](BlockSource& r, BlockPos const& p, Block const&, Actor*) { scheduleUpdate(r, p); });
    blockevents::onEntityInside(id::Slider, [](BlockSource& region, BlockPos const& pos, Actor& e) {
        if (typeOf(e) == id::SlideEntity) return;
        combat::hurt(e, 5.0f, "twilightforest:slider");
        if (asMob(e)) {
            auto q = e.getFeetPos();
            knockback(e, 2.0, (pos.x + 0.5 - q.x) * 2.0, (pos.z + 0.5 - q.z) * 2.0);
        }
    });
    gf::entity::registerType(id::SlideEntity, {.tick = slideTick});
}

} // namespace gf::feature::darktower::slider
