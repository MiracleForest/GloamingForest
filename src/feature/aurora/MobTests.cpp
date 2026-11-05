// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// Selftests of U28's mobs (rule R12: "aurora.*", testPos dx 2800..2899): the attributes against
// F13's goldens, the snow guardian's gear, the ice crystal's 600-tick life, the unstable ice core's
// delayed explosion and block transformation, the stable ice core's snowballs, the Snow Queen's
// phases, shields, summons and beam damage, her death chest, and her spawner's height rule.
#include <algorithm>
#include <cmath>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/phys/AABB.h"

#include "feature/aurora/Mobs.h"
#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/entity/EntityInternal.h"
#include "feature/ext/entity/EntityExt.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace gf::feature::aurora::tests {

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

std::int64_t uidOf(Actor& a) { return a.getOrCreateUniqueID().rawID; }

std::vector<Actor*> around(core::GlobalPos const& pos, std::string const& type, float r) {
    std::vector<Actor*> out;
    auto*               region = game::regionOf(pos.dim);
    if (!region) return out;
    for (auto* a : fe::actorsIn(*region, pos.dim, AABB{Vec3{pos.x - r, pos.y - r, pos.z - r}, Vec3{pos.x + 1 + r, pos.y + 1 + r, pos.z + 1 + r}}))
        if (a && a->isAlive() && a->getTypeName() == type) out.push_back(a);
    return out;
}

void discardAround(core::GlobalPos const& pos, float r) {
    for (char const* t : {kSnowQueen, kSnowGuardian, kStableIceCore, kUnstableIceCore, kIceCrystal, "twilightforest:ice_snowball"})
        for (auto* a : around(pos, t, r)) a->remove();
}

core::GlobalPos at(core::GlobalPos p, int dx, int dy, int dz) { return {p.dim, p.x + dx, p.y + dy, p.z + dz}; }

} // namespace

GF_REGISTER_SELFTEST("aurora.mob_attributes", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(2820, 16);
    auto            base = core::GlobalPos{ctx.dimension(), box.minX + 4, box.minY + 1, box.minZ + 2};
    // F13's goldens (tests/golden/mobs.json): max health
    struct Row {
        char const* id;
        int         health;
    };
    std::vector<std::int64_t> spawned;
    int                       i = 0;
    for (auto r : {Row{kSnowGuardian, 10}, Row{kStableIceCore, 20}, Row{kUnstableIceCore, 20}, Row{kIceCrystal, 10}, Row{kSnowQueen, 200}}) {
        auto* m = spawnAt(at(base, (i++) * 2, 0, 0), r.id);
        ctx.require(m != nullptr, std::string("spawned ") + r.id);
        ctx.check(m->getMaxHealth() == r.health, std::string(r.id) + " max health " + std::to_string(m->getMaxHealth()));
        spawned.push_back(uidOf(*m));
    }
    co_await ctx.ticks(5);
    if (auto* g = mobById(spawned[0])) {
        auto id = game::items::idOf(g->getCarriedItem());
        ctx.check(id == "twilightforest:ironwood_sword" || id == "twilightforest:steeleaf_sword" || id == "twilightforest:knightmetal_sword",
                  "the snow guardian carries a TF sword: " + id);
    }
    if (auto* q = mobById(spawned[4])) {
        ctx.check(gf::entity::ext::parts(*q).size() == 7, "the Snow Queen has seven ice shields");
        ctx.check(queenPhaseForTest(*q) == 0, "she starts in the summon phase");
        ctx.check(queenSummonsForTest(*q) == 6, "with six summons");
    }
    for (auto uid : spawned)
        if (auto* a = fe::actorById(uid)) a->remove();
    discardAround(base, 20);
});

GF_REGISTER_SELFTEST("aurora.ice_crystal_lifetime", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box = co_await ctx.flatArea(2840, 5);
    auto            pos = core::GlobalPos{ctx.dimension(), box.minX + 2, box.minY + 1, box.minZ + 2};
    auto*           summoned = spawnAt(pos, kIceCrystal);
    auto*           wild     = spawnAt(at(pos, 2, 0, 0), kIceCrystal);
    ctx.require(summoned && wild, "two ice crystals");
    markSummonedCrystal(*summoned);
    gf::entity::data(*summoned, "aurora:age") = 590;
    auto s = uidOf(*summoned), w = uidOf(*wild);
    co_await ctx.ticks(20);
    ctx.check(fe::actorById(s) == nullptr || !fe::actorById(s)->isAlive(), "the summoned crystal is gone after 600 ticks");
    ctx.check(fe::actorById(w) && fe::actorById(w)->isAlive(), "a crystal that was not summoned stays");
    if (auto* a = fe::actorById(w)) a->remove();
    discardAround(pos, 8);
});

GF_REGISTER_SELFTEST("aurora.unstable_ice_core", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box = co_await ctx.flatArea(2850, 12);
    auto            pos = core::GlobalPos{ctx.dimension(), box.minX + 6, box.minY + 1, box.minZ + 6};
    auto*           region = ctx.region();
    // a stone block (a redstone conductor: terracotta) and an ice block (stained glass) two blocks away
    auto stonePos = at(pos, 2, 1, 0), icePos = at(pos, -2, 1, 0);
    ctx.require(game::setBlock(*region, game::blockPos(stonePos), *game::blockWithStates("minecraft:stone", nlohmann::json::object())), "stone placed");
    ctx.require(game::setBlock(*region, game::blockPos(icePos), *game::blockWithStates("minecraft:ice", nlohmann::json::object())), "ice placed");
    auto* core = spawnAt(pos, kUnstableIceCore);
    ctx.require(core != nullptr, "an unstable ice core");
    auto uid = uidOf(*core);
    co_await ctx.ticks(2);
    if (auto* c = mobById(uid)) gf::combat::hurt(*c, 1000.0f, "minecraft:generic");
    co_await ctx.ticks(20);
    auto* dying = fe::actorById(uid);
    ctx.check(dying && dying->isAlive(), "the core waits 60 ticks after its death");
    co_await ctx.ticks(60);
    ctx.check(!fe::actorById(uid) || !fe::actorById(uid)->isAlive(), "then it explodes and is gone");
    auto stone = game::blockIdAt(*region, game::blockPos(stonePos));
    auto ice   = game::blockIdAt(*region, game::blockPos(icePos));
    ctx.check(stone.ends_with("_terracotta"), "the stone became terracotta: " + stone);
    ctx.check(ice.ends_with("_stained_glass"), "the ice became stained glass: " + ice);
    discardAround(pos, 10);
});

GF_REGISTER_SELFTEST("aurora.stable_ice_core_snowballs", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    static int      snowballHits = 0;
    static bool     registered   = false;
    if (!registered) {
        registered = true;
        gf::combat::onDealt(0, [](gf::combat::DamageInfo const& d) {
            if (d.type == "twilightforest:snowball_fight") snowballHits++;
        });
    }
    snowballHits = 0;
    auto box     = co_await ctx.flatArea(2860, 16);
    auto pos     = core::GlobalPos{ctx.dimension(), box.minX + 2, box.minY + 1, box.minZ + 8};
    auto* core   = spawnAt(pos, kStableIceCore);
    ctx.require(core != nullptr, "a stable ice core");
    auto  uid    = uidOf(*core);
    auto& player = ctx.player();
    player.setPlayerGameType(GameType::Survival);
    player.teleportTo(Vec3{pos.x + 7.5f, float(pos.y), pos.z + 0.5f}, true, 0, 1, false);
    if (auto* c = mobById(uid)) c->setTarget(&ctx.player());
    for (int i = 0; i < 20 && snowballHits == 0; i++) {
        co_await ctx.ticks(10);
        if (auto* c = mobById(uid)) c->setTarget(&ctx.player());
    }
    ctx.check(snowballHits > 0, "an ice snowball hit the player with snowball_fight damage");
    ctx.player().setPlayerGameType(GameType::Creative);
    if (auto* c = fe::actorById(uid)) c->remove();
    discardAround(pos, 16);
});

GF_REGISTER_SELFTEST("aurora.snow_queen_fight", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box   = co_await ctx.flatArea(2870, 24, 24);
    auto            pos   = core::GlobalPos{ctx.dimension(), box.minX + 12, box.minY + 1, box.minZ + 12};
    auto*           queen = spawnAt(pos, kSnowQueen, 0.5f, 4.0f);
    ctx.require(queen != nullptr, "the Snow Queen");
    auto  uid    = uidOf(*queen);
    auto& player = ctx.player();
    player.setPlayerGameType(GameType::Survival);
    player.teleportTo(Vec3{pos.x + 6.5f, float(pos.y), pos.z + 0.5f}, true, 0, 1, false);
    // SUMMON: ice crystals appear near her target
    bool summoned = false;
    for (int i = 0; i < 40 && !summoned; i++) {
        if (auto* q = mobById(uid)) q->setTarget(&ctx.player());
        co_await ctx.ticks(10);
        auto* q  = mobById(uid);
        summoned = q && queenSummonsForTest(*q) < 6 && !around(pos, kIceCrystal, 30).empty();
    }
    ctx.check(summoned, "the summon phase brings ice crystals");
    for (auto* c : around(pos, kIceCrystal, 30)) ctx.check(gf::entity::data(*c, "aurora:max_age") == 600, "a summoned crystal dies in 30 seconds");
    // a hit on a shield is blocked
    auto* q = mobById(uid);
    ctx.require(q != nullptr, "the queen is still there");
    float before = static_cast<float>(q->getHealth());
    auto  parts  = gf::entity::ext::parts(*q);
    ctx.require(parts.size() == 7, "seven shields");
    if (auto* shield = gf::entity::partActor(parts[0])) gf::combat::hurt(*shield, 6.0f, "minecraft:player_attack", &ctx.player());
    co_await ctx.ticks(2);
    q = mobById(uid);
    ctx.require(q != nullptr, "the queen is still there");
    ctx.check(static_cast<float>(q->getHealth()) >= before, "a shield blocks the hit");
    // BEAM ends after 25 damage
    setQueenPhaseForTest(*q, 2);
    co_await ctx.ticks(2);
    for (int i = 0; i < 4; i++) {
        if (auto* qq = mobById(uid)) gf::combat::hurt(*qq, 7.0f, "minecraft:player_attack", &ctx.player());
        co_await ctx.ticks(12);
    }
    q = mobById(uid);
    ctx.require(q != nullptr, "the queen survives the test hits");
    ctx.check(queenPhaseForTest(*q) == 0, "25 damage while beaming sends her back to summoning");
    // DROP: 2..4 drops
    setQueenPhaseForTest(*q, 1);
    int maxDrops = 0;
    (void)queenDropsForTest(*q, &maxDrops);
    ctx.check(maxDrops >= 2 && maxDrops <= 4, "2..4 drops: " + std::to_string(maxDrops));
    ctx.player().setPlayerGameType(GameType::Creative);
    if (auto* qq = fe::actorById(uid)) qq->remove();
    discardAround(pos, 30);
});

GF_REGISTER_SELFTEST("aurora.snow_queen_spawner", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box     = co_await ctx.flatArea(2880, 12, 16);
    auto            spawner = core::GlobalPos{ctx.dimension(), box.minX + 6, box.minY + 8, box.minZ + 6};
    // a player below spawnerY - 4 does not wake her
    ctx.player().teleportTo(Vec3{spawner.x + 3.5f, float(box.minY + 1), spawner.z + 0.5f}, true, 0, 1, false);
    ctx.require(ctx.setBlock(spawner, "twilightforest:snow_queen_boss_spawner"), "spawner placed");
    co_await ctx.ticks(40);
    ctx.check(around(spawner, kSnowQueen, 12).empty(), "no queen for a player more than 4 below the spawner");
    ctx.player().teleportTo(Vec3{spawner.x + 3.5f, float(spawner.y - 2), spawner.z + 0.5f}, true, 0, 1, false);
    bool spawned = false;
    for (int i = 0; i < 20 && !spawned; i++) {
        co_await ctx.ticks(5);
        spawned = !around(spawner, kSnowQueen, 12).empty();
    }
    ctx.check(spawned, "the queen appears for a player within 9 and above spawnerY - 4");
    ctx.check(ctx.blockAt(spawner) != "twilightforest:snow_queen_boss_spawner", "the spawner is used up");
    discardAround(spawner, 30);
});

} // namespace gf::feature::aurora::tests
