// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
//
// U30 (finalcastle) selftests of the mobs part (rule R12: "finalcastle.*", testPos dx 3060..3089,
// dz 0..4): F13's goldens (tests/golden/mobs.json) of the adherent, harbinger cube, roving cube and
// plateau boss; the adherent's RangedAttackGoal shooting at a player; the roving cube's symbol rule
// (upstream's six-neighbour check) and CubeCenterOnSymbolGoal; the plateau boss in F8's boss kit and the
// final boss spawner that never spawns; the gazebo's WIP texts placed by the template marker
// handler and punched away; progression_end granted once.
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/ecs/gamerefs_entity/GameRefsEntity.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/actor/Mob.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/Attribute.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/phys/AABB.h"

#include "core/finalcastle/Mobs.h"
#include "core/jrand/JRand.h"
#include "feature/common/Interactions.h"
#include "feature/entity/Entity.h"
#include "feature/ext/finalcastle/W0Internals.h"
#include "feature/finalcastle/Mobs.h"
#include "feature/progression/Progression.h"
#include "game/Blocks.h"
#include "selftest/SelfTest.h"

namespace gf::feature::finalcastle::mobs::tests {

namespace {

using selftest::Context;
using Difficulty = SharedTypes::Legacy::Difficulty;
namespace fe     = gf::feature::entity;
namespace cf     = gf::core::finalcastle;
namespace ia     = gf::interactions;

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

core::GlobalPos at(core::GlobalPos p, int dx, int dy, int dz) { return {p.dim, p.x + dx, p.y + dy, p.z + dz}; }

Mob* spawnAt(core::GlobalPos const& p, std::string const& id) {
    auto* region = game::regionOf(p.dim);
    auto  level  = ll::service::getLevel();
    if (!region || !level) return nullptr;
    return level->getSpawner().spawnMob(*region, ActorDefinitionIdentifier(id), nullptr, Vec3{p.x + 0.5f, float(p.y), p.z + 0.5f}, false, false, false);
}

Mob* mobById(std::int64_t uid) {
    auto* a = fe::actorById(uid);
    return a ? fe::asMob(*a) : nullptr;
}

std::vector<Actor*> around(core::GlobalPos const& p, std::string const& type, float r) {
    std::vector<Actor*> out;
    auto*               region = game::regionOf(p.dim);
    if (!region) return out;
    for (auto* a : fe::actorsIn(*region, p.dim, AABB{Vec3{p.x - r, p.y - r, p.z - r}, Vec3{p.x + 1 + r, p.y + 1 + r, p.z + 1 + r}}))
        if (a && a->isAlive() && a->getTypeName() == type) out.push_back(a);
    return out;
}

void discardAround(core::GlobalPos const& p, float r) {
    for (char const* t : {id::Adherent, id::HarbingerCube, id::RovingCube, id::PlateauBoss, id::NatureBolt, id::WipText, id::WipPunch})
        for (auto* a : around(p, t, r)) a->remove();
}

float attribute(Mob& m, Attribute const& a) {
    auto        ref  = m.getAttribute(a);
    auto const* inst = static_cast<AttributeInstance const*>(ref.mPtr);
    return inst ? inst->mCurrentValue : -1.0f;
}

bool near(float a, float b, float eps = 0.011f) { return std::abs(a - b) < eps; }

} // namespace

// F13's goldens (tests/golden/mobs.json, upstream 4.8 with its default config): max health, the box,
// movement speed, follow range, attack damage, fire immunity and the eye height of the four mobs
GF_REGISTER_SELFTEST("finalcastle.mob_goldens", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(3060, 10);
    auto            base = core::GlobalPos{ctx.dimension(), box.minX + 1, box.minY, box.minZ + 2};
    struct Row {
        char const* id;
        float       health, width, height, movement, follow, attack;
        bool        fireImmune;
    };
    constexpr Row kRows[] = {
        {id::Adherent, 20.0f, 0.8f, 2.2f, 0.25f, 16.0f, 2.0f, false},
        {id::HarbingerCube, 40.0f, 1.9f, 2.4f, 0.23f, 16.0f, 2.0f, true},
        {id::RovingCube, 10.0f, 1.2f, 2.1f, 0.23f, 16.0f, 5.0f, false},
        {id::PlateauBoss, 20.0f, 1.0f, 1.0f, 0.7f, 16.0f, 2.0f, true},
    };
    int i = 0;
    for (auto const& r : kRows) {
        auto* m = spawnAt(at(base, (i++) * 2, 0, 0), r.id);
        ctx.require(m != nullptr, std::string("spawned ") + r.id);
        std::string const id = r.id;
        auto              b  = m->getAABB();
        ctx.check(near(float(m->getMaxHealth()), r.health), id + " max health " + std::to_string(m->getMaxHealth()));
        ctx.check(near(b.max.x - b.min.x, r.width) && near(b.max.y - b.min.y, r.height),
                  id + " box " + std::to_string(b.max.x - b.min.x) + " x " + std::to_string(b.max.y - b.min.y));
        ctx.check(near(attribute(*m, SharedAttributes::MOVEMENT_SPEED()), r.movement), id + " movement " + std::to_string(attribute(*m, SharedAttributes::MOVEMENT_SPEED())));
        ctx.check(near(attribute(*m, SharedAttributes::FOLLOW_RANGE()), r.follow), id + " follow range " + std::to_string(attribute(*m, SharedAttributes::FOLLOW_RANGE())));
        ctx.check(near(attribute(*m, SharedAttributes::ATTACK_DAMAGE()), r.attack), id + " attack " + std::to_string(attribute(*m, SharedAttributes::ATTACK_DAMAGE())));
        ctx.check(m->isFireImmune() == r.fireImmune, id + (r.fireImmune ? " is fire immune" : " is not fire immune"));
        m->remove();
    }
    discardAround(base, 12);
});

// Adherent: RangedAttackGoal(1.0, 60, 10) -> performRangedAttack, a nature bolt shot by id
GF_REGISTER_SELFTEST("finalcastle.adherent_shoots", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(3070, 14);
    auto            pos  = core::GlobalPos{ctx.dimension(), box.minX + 2, box.minY, box.minZ + 2};
    auto*           adh  = spawnAt(pos, id::Adherent);
    ctx.require(adh != nullptr, "an adherent");
    auto uid    = fe::uidOf(*adh);
    int  before = adherentShotsForTest();
    ctx.player().setPlayerGameType(GameType::Survival);
    ctx.player().teleportTo(Vec3{pos.x + 7.5f, float(pos.y), pos.z + 0.5f}, true, 0, 1, false);
    bool boltSeen = false;
    for (int t = 0; t < 200 && adherentShotsForTest() == before; t++) {
        if (auto* m = mobById(uid)) m->setTarget(&ctx.player());
        co_await ctx.ticks(1);
        boltSeen = boltSeen || !around(pos, id::NatureBolt, 12).empty();
    }
    for (int t = 0; t < 5 && !boltSeen; t++) {
        co_await ctx.ticks(1);
        boltSeen = !around(pos, id::NatureBolt, 12).empty();
    }
    ctx.check(adherentShotsForTest() > before, "the adherent shot at its target within 200 ticks");
    if (boltSeen) ctx.info("a gloamingforest:nature_bolt flew");
    else ctx.info("no gloamingforest:nature_bolt seen (U17's projectile may not be registered)");
    ctx.player().setPlayerGameType(GameType::Creative);
    discardAround(pos, 14);
});

// RovingCube: CubeMoveToRedstoneSymbolsGoal's symbol is an air block with redstone wire on all six
// sides (upstream's check), so the flat ring of eight dust it was meant for never counts; then
// CubeCenterOnSymbolGoal walks a cube near a known symbol to its centre
GF_REGISTER_SELFTEST("finalcastle.roving_cube_symbol", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box    = co_await ctx.flatArea(3060, 10);
    auto*           region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto c = core::GlobalPos{ctx.dimension(), box.minX + 5, box.minY + 1, box.minZ + 2};
    // a ring of dust on stone around an air block
    for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 1; dz++) {
            ctx.setBlock(at(c, dx, -1, dz), "minecraft:stone");
            if (dx || dz) ctx.setBlock(at(c, dx, 0, dz), "minecraft:redstone_wire");
        }
    ctx.check(!isRedstoneSymbol(*region, game::blockPos(c)), "a flat ring of eight dust is no symbol (upstream checks all six sides)");
    // wire below and above as well (the upper wire is checked in the same tick, before it can pop off)
    ctx.setBlock(at(c, 0, -2, 0), "minecraft:stone");
    ctx.setBlock(at(c, 0, -1, 0), "minecraft:redstone_wire");
    ctx.check(!isRedstoneSymbol(*region, game::blockPos(c)), "five sides of wire are no symbol");
    ctx.setBlock(at(c, 0, 1, 0), "minecraft:redstone_wire");
    ctx.check(isRedstoneSymbol(*region, game::blockPos(c)), "wire on all six sides of an air block is a symbol");
    for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 1; dz++)
            for (int dy = 1; dy >= -1; dy--) game::clearBlock(*region, game::blockPos(at(c, dx, dy, dz)));
    co_await ctx.ticks(1);
    // CubeCenterOnSymbolGoal: a symbol 3 blocks away, the path done: the cube walks to its centre
    auto  start = core::GlobalPos{ctx.dimension(), box.minX + 2, box.minY, box.minZ + 2};
    auto* cube  = spawnAt(start, id::RovingCube);
    ctx.require(cube != nullptr, "a roving cube");
    auto     uid    = fe::uidOf(*cube);
    cf::IPos symbol{start.x + 3, start.y, start.z};
    co_await ctx.ticks(2); // its goals are added on its first tick
    setSymbolForTest(uid, symbol);
    auto*  p0    = mobById(uid);
    double first = 0;
    if (p0) first = cf::distanceFromSymbol(symbol, p0->getFeetPos().x, p0->getFeetPos().y, p0->getFeetPos().z);
    ctx.require(first > 0.25 && first < 10, "the cube starts near the symbol (" + std::to_string(first) + ")");
    double last = first;
    for (int t = 0; t < 100 && last > 0.6; t++) {
        co_await ctx.ticks(1);
        auto* m = mobById(uid);
        ctx.require(m != nullptr, "the cube is still there");
        last = cf::distanceFromSymbol(symbol, m->getFeetPos().x, m->getFeetPos().y, m->getFeetPos().z);
    }
    ctx.check(last < first - 1.5, "the cube walked to the symbol's centre (" + std::to_string(first) + " -> " + std::to_string(last) + ")");
    auto s = symbolOf(uid);
    ctx.check(s && s->found && s->pos == symbol, "the cube remembers its symbol");
    discardAround(start, 12);
});

// PlateauBoss in F8's boss kit; FinalBossSpawnerBlockEntity: range 9, the annihilate particle, and
// spawnMyBoss never spawns (the block stays, no boss)
GF_REGISTER_SELFTEST("finalcastle.final_boss_spawner", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(3080, 10);
    auto            pos  = core::GlobalPos{ctx.dimension(), box.minX + 4, box.minY, box.minZ + 2};
    auto            rule = gf::feature::boss::spawnerRuleForTest(id::BossSpawner);
    ctx.require(rule.has_value(), "the final boss spawner is registered");
    ctx.check(rule->entityId == id::PlateauBoss, "it names the plateau boss (" + rule->entityId + ")");
    ctx.check(rule->range == 9, "its range is 9 (" + std::to_string(rule->range) + ")");
    ctx.check(rule->particle == id::Annihilate, "its particle is gloamingforest:annihilate (" + rule->particle + ")");
    ctx.require(ctx.setBlock(pos, id::BossSpawner), "the spawner is placed");
    ctx.player().setPlayerGameType(GameType::Survival);
    ctx.player().teleportTo(Vec3{pos.x + 3.5f, float(pos.y), pos.z + 0.5f}, true, 0, 1, false);
    co_await ctx.ticks(60);
    ctx.check(ctx.blockAt(pos) == id::BossSpawner, "the spawner is still there after 3 seconds with a player 3 blocks away");
    ctx.check(around(pos, id::PlateauBoss, 12).empty(), "no plateau boss spawned");
    ctx.player().setPlayerGameType(GameType::Creative);
    // the boss itself: a registered boss gets the kit's bar
    auto* boss = spawnAt(at(pos, -3, 0, 0), id::PlateauBoss);
    ctx.require(boss != nullptr, "a plateau boss");
    auto uid = fe::uidOf(*boss);
    co_await ctx.ticks(3);
    ctx.check(gf::feature::boss::hasBar(uid), "the plateau boss has the boss kit's bar");
    discardAround(pos, 12);
});

// willBeAddingFinalBossSoon through the template marker handler: four lines at y + 2.3 / 1.3 / 1.0 /
// 0.7 over the block centre, the 2.0 x 3.6 punch box from y - 0.3; one attack on it removes the
// lines inside and itself (removeCastleTextIfAttacked), a text carrier outside stays
GF_REGISTER_SELFTEST("finalcastle.wip_texts", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  box    = co_await ctx.flatArea(3080, 10);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto pos = core::GlobalPos{ctx.dimension(), box.minX + 4, box.minY + 1, box.minZ + 2};
    auto fn  = gf::structure::markerFor(id::WipMarker);
    ctx.require(fn != nullptr, "the final_castle_wip marker handler is registered");
    {
        gf::structure::BlockSourceRegion live(*region, game::blockPos(pos));
        core::jrand::LegacyRandom        random(0);
        (*fn)(live, game::blockPos(pos), id::WipMarker, nlohmann::json::object(), random);
    }
    for (int t = 0; t < 40 && (pendingWipForTest() > 0 || around(pos, id::WipPunch, 4).empty()); t++) co_await ctx.ticks(1);
    auto texts   = around(pos, id::WipText, 4);
    auto punches = around(pos, id::WipPunch, 4);
    ctx.require(texts.size() == 4, "four text carriers (" + std::to_string(texts.size()) + ")");
    ctx.require(punches.size() == 1, "one punch carrier (" + std::to_string(punches.size()) + ")");
    std::map<std::string, float> heights;
    for (auto* t : texts) {
        auto p = t->getFeetPos();
        heights[t->getNameTag()] = p.y - float(pos.y);
        ctx.check(near(p.x, pos.x + 0.5f, 0.02f) && near(p.z, pos.z + 0.5f, 0.02f), "a line at the block centre (" + t->getNameTag() + ")");
    }
    for (auto const& line : cf::kWipLines) {
        auto it = heights.find(line.text);
        ctx.check(it != heights.end() && near(it->second, line.yOffset, 0.02f),
                  std::string("'") + line.text + "' at +" + std::to_string(line.yOffset) + (it != heights.end() ? " (" + std::to_string(it->second) + ")" : " (missing)"));
    }
    auto pb = punches[0]->getAABB();
    ctx.check(near(pb.max.x - pb.min.x, 2.0f, 0.02f) && near(pb.max.y - pb.min.y, 3.6f, 0.02f),
              "the punch box is 2.0 x 3.6 (" + std::to_string(pb.max.x - pb.min.x) + " x " + std::to_string(pb.max.y - pb.min.y) + ")");
    ctx.check(near(pb.min.y, pos.y - 0.3f, 0.02f) && near((pb.min.x + pb.max.x) / 2, pos.x + 0.5f, 0.02f), "the punch box starts 0.3 below the block, centred on it");
    // a line outside the box is not upstream's to remove
    auto spawnOutside = [&]() -> Actor* {
        auto owner = region->getLevel().getActorFactory().createSpawnedActor(ActorDefinitionIdentifier(std::string(id::WipText)), nullptr,
                                                                             Vec3{pos.x + 3.5f, pos.y + 1.0f, pos.z + 0.5f}, Vec2{0.0f, 0.0f});
        return owner ? region->getLevel().addEntity(*region, std::move(owner)) : nullptr;
    };
    auto* outside = spawnOutside();
    ctx.require(outside != nullptr, "a text carrier outside the box");
    auto outsideUid = fe::uidOf(*outside);
    auto punchUid   = fe::uidOf(*punches[0]);
    // the test player punches the box
    auto&            player = ctx.player();
    ItemStack const& held   = player.getSelectedItem();
    std::string      heldId;
    std::string      punchType = id::WipPunch;
    ia::EntityContext attack{player, held, heldId, *punches[0], punchType, {pos.x + 0.5, pos.y + 1.0, pos.z + 0.5}, false};
    ctx.check(ia::dispatchAttack(attack) == ia::Result::Deny, "the attack itself does nothing (Deny)");
    co_await ctx.ticks(3);
    ctx.check(around(pos, id::WipText, 2).empty(), "every line inside the box is gone");
    ctx.check(fe::actorById(punchUid) == nullptr, "the punch box is gone");
    ctx.check(fe::actorById(outsideUid) != nullptr, "the line outside the box stays");
    discardAround(pos, 8);
});

// EntityEvents#alertPlayerCastleIsWIP: the message goes out when progression_end is earned, once.
// U14 (progression_content/Presentation.cpp) already sends it from its advancement presentation,
// so U30 adds no second copy; this checks the grant fires once for repeated grants.
GF_REGISTER_SELFTEST("finalcastle.progression_end_once", [](Context& ctx) -> ll::coro::CoroTask<> {
    static std::map<std::int64_t, int> granted;
    static bool                        registered = false;
    if (!registered) {
        registered = true;
        gf::progression::onGranted([](Player& p, std::string const& adv) {
            if (adv == "gloamingforest:progression_end") granted[p.getOrCreateUniqueID().rawID]++;
        });
    }
    auto& sp  = ctx.spawnPlayer("fc_progression_end");
    auto  uid = sp.getOrCreateUniqueID().rawID;
    granted.erase(uid);
    gf::progression::revoke(sp, "gloamingforest:progression_end");
    co_await ctx.ticks(1);
    auto* p = fe::playerById(uid);
    ctx.require(p != nullptr, "the player");
    ctx.require(!gf::progression::has(*p, "gloamingforest:progression_end"), "progression_end is not earned yet");
    gf::progression::grant(*p, "gloamingforest:progression_end");
    co_await ctx.ticks(1);
    p = fe::playerById(uid);
    ctx.require(p != nullptr, "the player");
    ctx.check(gf::progression::has(*p, "gloamingforest:progression_end"), "progression_end is earned");
    gf::progression::grant(*p, "gloamingforest:progression_end");
    co_await ctx.ticks(1);
    ctx.check(granted[uid] == 1, "earned once, so the WIP message goes out once (" + std::to_string(granted[uid]) + " grants)");
    if (auto* q = fe::playerById(uid)) gf::progression::revoke(*q, "gloamingforest:progression_end");
});

} // namespace gf::feature::finalcastle::mobs::tests
