// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// Selftests of U27's blocks and mobs (rule R12: "caves.*", testPos dx 2700..2799): trollsteinn
// faces by light and its comparator output, the trollber harvest, the unripe trollber ripening,
// the troll's rock (pick-up, throw, 6 thrown_block damage), the yeti's grab and throw of a
// SimulatedPlayer, the alpha yeti (projectile immunity until hit, rampage, tired, falling ice) and
// the falling ice's landing damage.
#include <algorithm>
#include <cmath>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/caves/Caves.h"
#include "feature/combat/Combat.h"
#include "feature/common/Redstone.h"
#include "feature/entity/Entity.h"
#include "feature/ext/caves/W0Internals.h"
#include "feature/ext/entity/EntityExt.h"
#include "game/Blocks.h"
#include "selftest/SelfTest.h"

namespace gf::feature::caves::tests {

namespace {

using selftest::Context;
using Difficulty = SharedTypes::Legacy::Difficulty;
namespace fe     = gf::feature::entity;

struct DifficultyScope {
    Difficulty previous = Difficulty::Peaceful;
    explicit DifficultyScope(Difficulty d) {
        if (auto level = ll::service::getLevel()) {
            previous = level->getDifficulty();
            level->setDifficulty(d);
        }
    }
    ~DifficultyScope() {
        if (auto level = ll::service::getLevel()) level->setDifficulty(previous);
    }
};

Mob* spawnAt(core::GlobalPos const& at, std::string const& id, float dx = 0.5f, float dy = 0.0f, float dz = 0.5f) {
    auto* region = game::regionOf(at.dim);
    auto  level  = ll::service::getLevel();
    if (!region || !level) return nullptr;
    return level->getSpawner().spawnMob(*region, ActorDefinitionIdentifier(id), nullptr, Vec3{at.x + dx, float(at.y) + dy, at.z + dz}, false, false, false);
}

Mob* mobById(std::int64_t uid) {
    auto* a = fe::actorById(uid);
    return a ? fe::asMob(*a) : nullptr;
}

std::vector<Actor*> around(core::GlobalPos const& pos, std::string const& type, float r) {
    std::vector<Actor*> out;
    auto*               region = game::regionOf(pos.dim);
    if (!region) return out;
    for (auto* a : fe::actorsIn(*region, pos.dim, AABB{Vec3{pos.x - r, pos.y - r, pos.z - r}, Vec3{pos.x + 1 + r, pos.y + 1 + r, pos.z + 1 + r}}))
        if (a && a->isAlive() && a->getTypeName() == type) out.push_back(a);
    return out;
}

void discardAround(core::GlobalPos const& pos, float r) {
    for (char const* t : {kTroll, kYeti, kAlphaYeti, kThrownBlock, kFallingIce, kThrownIce, "minecraft:pig", "minecraft:item"})
        for (auto* a : around(pos, t, r)) a->remove();
}

core::GlobalPos at(core::GlobalPos p, int dx, int dy, int dz) { return {p.dim, p.x + dx, p.y + dy, p.z + dz}; }
BlockPos        bp(core::GlobalPos const& p) { return BlockPos{p.x, p.y, p.z}; }

} // namespace

GF_REGISTER_SELFTEST("caves.trollsteinn_light", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box    = co_await ctx.flatArea(2730, 8);
    auto base   = core::GlobalPos{ctx.dimension(), box.minX + 3, box.minY + 1, box.minZ + 2};
    auto region = ctx.region();
    ctx.require(region != nullptr, "a region");
    // a trollsteinn in a stone shell open to the sky on top, lit by a glowstone on its east side
    for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 1; dz++)
            for (int dy = -1; dy <= 0; dy++) ctx.setBlock(at(base, dx, dy, dz), "minecraft:stone");
    ctx.setBlock(base, kTrollsteinn);
    ctx.setBlock(at(base, 2, 0, 0), "minecraft:air");
    ctx.setBlock(at(base, 1, 0, 0), "minecraft:glowstone");
    co_await ctx.ticks(5);
    trollsteinnRandomTick(*region, bp(base));
    auto const& b    = region->getBlock(bp(base));
    auto        st   = game::statesOf(b);
    auto        lit  = [&](char const* f) { return st[std::string("twilightforest:") + f] == "1" || st[std::string("twilightforest:") + f] == "true"; };
    ctx.check(lit("east"), "the face next to the glowstone is lit (light 15 > 7)");
    ctx.check(!lit("west") && !lit("down") && !lit("north"), "the faces against stone are dark");
    int up = maxLocalRawBrightness(*region, bp(at(base, 0, 1, 0)));
    ctx.check(lit("up") == (up > 7), "the top face follows the light above it (" + std::to_string(up) + ")");
    auto analog = trollsteinnComparator(*region, bp(base));
    ctx.check(analog && *analog == 15, "comparator output = the brightest neighbour (15): " + std::to_string(analog.value_or(-1)));
    // darkness: the glowstone gone, the top covered
    ctx.setBlock(at(base, 1, 0, 0), "minecraft:stone");
    ctx.setBlock(at(base, 0, 1, 0), "minecraft:stone");
    co_await ctx.ticks(5);
    trollsteinnRandomTick(*region, bp(base));
    st = game::statesOf(region->getBlock(bp(base)));
    ctx.check(!lit("east") && !lit("up"), "every face dark once the light is gone");
    for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 1; dz++)
            for (int dy = -1; dy <= 1; dy++) ctx.cleanupBlock(at(base, dx, dy, dz));
});

GF_REGISTER_SELFTEST("caves.troll_roots", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box    = co_await ctx.flatArea(2740, 8);
    auto ceil   = core::GlobalPos{ctx.dimension(), box.minX + 3, box.minY + 4, box.minZ + 2};
    auto region = ctx.region();
    ctx.require(region != nullptr, "a region");
    ctx.setBlock(ceil, "minecraft:stone");
    ctx.setBlock(at(ceil, 2, 0, 0), "minecraft:stone");
    ctx.check(canPlaceRootBelow(*region, bp(ceil)), "base stone holds a troll root");
    ctx.setBlock(at(ceil, 0, -1, 0), kTrollber);
    ctx.setBlock(at(ceil, 2, -1, 0), kUnripeTrollber);
    ctx.check(canPlaceRootBelow(*region, bp(at(ceil, 0, -1, 0))), "a troll root holds another");
    // ripening in daylight (raw brightness >= 6 under the stone)
    int light = maxLocalRawBrightness(*region, bp(at(ceil, 2, -1, 0)));
    bool ripe = unripeTrollberRandomTick(*region, bp(at(ceil, 2, -1, 0)));
    ctx.check(ripe == (light >= 6), "an unripe trollber ripens at light >= 6 (light " + std::to_string(light) + ")");
    if (ripe) ctx.check(ctx.blockAt(at(ceil, 2, -1, 0)) == kTrollber, "it is a trollber now");
    // the harvest: trollvidr and a torchberries item
    ctx.require(harvestTrollber(ctx.player(), at(ceil, 0, -1, 0)), "the trollber is picked");
    co_await ctx.ticks(2);
    ctx.check(ctx.blockAt(at(ceil, 0, -1, 0)) == kTrollvidr, "the trollber turned into trollvidr");
    ctx.check(!around(at(ceil, 0, -1, 0), "minecraft:item", 2.0f).empty(), "torchberries dropped");
    discardAround(ceil, 4.0f);
    for (int dx = 0; dx <= 2; dx++)
        for (int dy = -1; dy <= 0; dy++) ctx.cleanupBlock(at(ceil, dx, dy, 0));
});

GF_REGISTER_SELFTEST("caves.troll_rock", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(2750, 16);
    auto            base = core::GlobalPos{ctx.dimension(), box.minX + 4, box.minY + 1, box.minZ + 2};
    // a stone ring two blocks out, three high: the take-block step finds stone around the troll
    for (int dx = -2; dx <= 2; dx++)
        for (int dz = -2; dz <= 2; dz++) {
            if (std::max(std::abs(dx), std::abs(dz)) != 2) continue;
            for (int dy = 0; dy <= 2; dy++) ctx.setBlock(at(base, dx, dy, dz), "minecraft:stone");
        }
    auto* troll = spawnAt(base, kTroll);
    ctx.require(troll != nullptr, "a troll spawns");
    auto trollId = troll->getOrCreateUniqueID().rawID;
    auto& player = ctx.player();
    player.setPlayerGameType(GameType::Survival);
    troll->setTarget(&player);
    setTrollRockCooldownForTest(*troll, 0);
    bool got = false;
    for (int i = 0; i < 80 && !got; i++) {
        co_await ctx.ticks(1);
        auto* t = mobById(trollId);
        if (!t) break;
        t->setTarget(&ctx.player());
        got = trollHasRock(*t);
    }
    ctx.check(got, "the troll takes a stone block within 80 ticks");
    // the throw: a pig in front takes 6 thrown_block damage
    auto* t = mobById(trollId);
    ctx.require(t != nullptr, "the troll is still there");
    giveTrollRockForTest(*t, "minecraft:granite");
    auto pigAt = at(base, 0, 0, 4);
    // the ring away: the 1 x 1 block flies clear
    for (int dx = -2; dx <= 2; dx++)
        for (int dz = -2; dz <= 2; dz++)
            for (int dy = 0; dy <= 2; dy++) ctx.setBlock(at(base, dx, dy, dz), "minecraft:air");
    auto* pig = spawnAt(pigAt, "minecraft:pig");
    ctx.require(pig != nullptr, "a pig spawns");
    auto  pigId  = pig->getOrCreateUniqueID().rawID;
    float before = pig->getHealth();
    trollThrowForTest(*t, *pig);
    ctx.check(!trollHasRock(*t), "the rock is gone from the troll");
    auto blocks = around(base, kThrownBlock, 4.0f);
    ctx.check(!blocks.empty() && thrownBlockState(*blocks.front()) == "minecraft:granite", "a thrown block carrying granite");
    bool hit = false;
    for (int i = 0; i < 40 && !hit; i++) {
        co_await ctx.ticks(1);
        auto* p = mobById(pigId);
        hit     = !p || p->getHealth() <= before - 6.0f + 0.01f;
    }
    ctx.check(hit, "the thrown block deals 6 damage");
    discardAround(base, 12.0f);
    for (int dx = -2; dx <= 2; dx++)
        for (int dz = -2; dz <= 2; dz++)
            for (int dy = 0; dy <= 2; dy++) ctx.cleanupBlock(at(base, dx, dy, dz));
});

GF_REGISTER_SELFTEST("caves.yeti_throw", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(2760, 24);
    auto            base = core::GlobalPos{ctx.dimension(), box.minX + 8, box.minY + 1, box.minZ + 2};
    auto&           sim  = ctx.spawnPlayer("caves_yeti_victim");
    auto            pid  = sim.getOrCreateUniqueID().rawID;
    sim.setPlayerGameType(GameType::Survival);
    sim.teleportTo(Vec3{base.x + 2.5f, float(base.y), base.z + 0.5f}, false, 0, 1, false);
    auto* yeti = spawnAt(base, kYeti);
    ctx.require(yeti != nullptr, "a yeti spawns");
    auto yetiId = yeti->getOrCreateUniqueID().rawID;
    bool grabbed = false, thrown = false;
    float startY = 0;
    for (int i = 0; i < 300 && !thrown; i++) {
        co_await ctx.ticks(1);
        auto* y = mobById(yetiId);
        auto* p = fe::playerById(pid);
        if (!y || !p) break;
        if (!grabbed) y->setTarget(p);
        if (!grabbed && p->getVehicle() == y) {
            grabbed = true;
            startY  = p->getFeetPos().y;
        }
        if (grabbed && gf::entity::ext::throwCooldown(*p) > 0) thrown = true;
    }
    ctx.check(grabbed, "the yeti grabs the player");
    ctx.check(thrown, "and throws it (the throw cooldown runs)");
    if (thrown) {
        auto* p = fe::playerById(pid);
        ctx.require(p != nullptr, "the player is still there");
        ctx.check(gf::entity::ext::throwCooldown(*p) > 150 && gf::entity::ext::throwCooldown(*p) <= 200, "cooldown 200");
        ctx.check(p->getVehicle() == nullptr, "the player is off the yeti");
        co_await ctx.ticks(3);
        p = fe::playerById(pid);
        ctx.check(p && (p->getFeetPos().y > startY - 1.0f || !p->isOnGround()), "the throw moves the player (motion applied)");
    }
    discardAround(base, 20.0f);
});

GF_REGISTER_SELFTEST("caves.alpha_yeti", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(2770, 24, 40);
    auto            base = core::GlobalPos{ctx.dimension(), box.minX + 10, box.minY + 1, box.minZ + 2};
    auto*           yeti = spawnAt(base, kAlphaYeti);
    ctx.require(yeti != nullptr, "an alpha yeti spawns");
    auto  id = yeti->getOrCreateUniqueID().rawID;
    auto& player = ctx.player();
    player.setPlayerGameType(GameType::Survival);
    float h0 = yeti->getHealth();
    gf::combat::hurt(*yeti, 5.0f, "minecraft:arrow", &player, &player);
    co_await ctx.ticks(1);
    yeti = mobById(id);
    ctx.require(yeti != nullptr, "the yeti is still there");
    ctx.check(yeti->getHealth() >= h0 - 0.01f && !alphaCanRampage(*yeti), "projectiles do no harm in the ranged phase");
    gf::combat::hurt(*yeti, 5.0f, "minecraft:player_attack", &player, &player);
    co_await ctx.ticks(1);
    yeti = mobById(id);
    ctx.require(yeti != nullptr, "the yeti is still there");
    ctx.check(yeti->getHealth() < h0 && alphaCanRampage(*yeti), "a melee hit lands and lets it rampage");
    // falling ice: an ice block 6 above the yeti's eyes with air below it
    auto iceAt = at(base, 3, 12, 0);
    ctx.setBlock(iceAt, "minecraft:packed_ice");
    BlockPos probe{iceAt.x, iceAt.y - 8, iceAt.z};
    ctx.check(alphaMakeBlockFallAbove(*yeti, probe, 5), "the ice above becomes falling ice");
    ctx.check(ctx.blockAt(iceAt) == "minecraft:air", "its block is gone");
    ctx.check(!around(iceAt, kFallingIce, 2.0f).empty(), "a falling ice entity hangs there");
    // the rampage: with a target it starts after 10 checks
    bool rampaged = false;
    for (int i = 0; i < 120 && !rampaged; i++) {
        co_await ctx.ticks(1);
        auto* y = mobById(id);
        if (!y) break;
        y->setTarget(&ctx.player());
        rampaged = alphaRampaging(*y);
    }
    ctx.check(rampaged, "it rampages");
    bool tired = false;
    // (a goal without requiresUpdateEveryTick ticks every other tick: 180 goal ticks are 360 game ticks)
    for (int i = 0; i < 420 && !tired; i++) {
        co_await ctx.ticks(1);
        auto* y = mobById(id);
        if (!y) break;
        tired = alphaTired(*y);
    }
    ctx.check(tired, "after 180 ticks it is tired");
    if (auto* y = mobById(id)) ctx.check(!alphaCanRampage(*y), "tired: no rampage left");
    discardAround(base, 30.0f);
    ctx.cleanupBlock(iceAt);
});

GF_REGISTER_SELFTEST("caves.falling_ice", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope hard(Difficulty::Hard);
    auto            box    = co_await ctx.flatArea(2780, 8, 24);
    auto            base   = core::GlobalPos{ctx.dimension(), box.minX + 3, box.minY + 1, box.minZ + 2};
    auto*           region = ctx.region();
    auto*           pig    = spawnAt(base, "minecraft:pig");
    ctx.require(region && pig, "a pig spawns");
    auto  pigId  = pig->getOrCreateUniqueID().rawID;
    float before = pig->getHealth();
    auto* ice    = spawnFallingIce(*region, bp(at(base, 0, 12, 0)), "minecraft:packed_ice", 2);
    ctx.require(ice != nullptr, "a falling ice spawns");
    bool landed = false;
    for (int i = 0; i < 60 && !landed; i++) {
        co_await ctx.ticks(1);
        landed = around(at(base, 0, 6, 0), kFallingIce, 8.0f).empty();
    }
    ctx.check(landed, "the ice lands");
    auto* p = mobById(pigId);
    // a 12 block fall: ceil(12 - 5) = 7 x 2 (hard) = 14
    ctx.check(!p || p->getHealth() < before, "the pig under it is hurt");
    discardAround(base, 6.0f);
    ctx.cleanupBlock(base);
    ctx.cleanupBlock(at(base, 0, 1, 0));
});

} // namespace gf::feature::caves::tests
