// Selftests of F8's "boss" contract (rule R12: prefix entity.*, dx 5800..5899): the e2e fixture
// boss (gf_test:boss with 3 parts: parts follow, damage routes to the parent, the boss bar's
// BossEventPackets, the death chest filled from the loot buffer, the conquest), peaceful turning
// the boss back into its spawner, the generic spawner, the trophy table and the bar colours.

#include <cmath>

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/world/phys/AABB.h"

#include "feature/boss/Boss.h"
#include "feature/boss/BossInternal.h"
#include "feature/entity/Entity.h"
#include "feature/entity/EntityInternal.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/landmark/Landmark.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace gf::feature::boss::tests {

using gf::selftest::Context;
namespace ge = gf::entity;
namespace fe = gf::feature::entity;
using Difficulty = SharedTypes::Legacy::Difficulty;

/// The fixture boss's bar colour: orange, which no vanilla BossBarColor has (red is nearest).
constexpr unsigned kFixtureRgb = 0xFF8000;

namespace {

struct FixtureRegistration {
    FixtureRegistration() {
        gf::boss::registerBoss({
            .entityId        = "gf_test:boss",
            .homeStructure   = "gf_test:fixture",
            .lootTable       = "gf_test:entities/boss",
            .deathChestBlock = "minecraft:chest",
            .bar             = {.rgb = kFixtureRgb, .darken = true, .fog = false},
            .deathTicks      = 20,
        });
        markInclusiveForTest("gf_test:boss");
    }
};
FixtureRegistration const fixtureRegistration;

struct DifficultyScope {
    Difficulty previous = Difficulty::Peaceful;
    explicit DifficultyScope(Difficulty d) {
        if (auto level = ll::service::getLevel()) {
            previous = level->getDifficulty();
            level->setDifficulty(d);
        }
    }
    void set(Difficulty d) {
        if (auto level = ll::service::getLevel()) level->setDifficulty(d);
    }
    ~DifficultyScope() {
        if (auto level = ll::service::getLevel()) level->setDifficulty(previous);
    }
};

Mob* spawnMob(core::GlobalPos const& at, std::string const& id) {
    auto* region = game::regionOf(at.dim);
    auto  level  = ll::service::getLevel();
    if (!region || !level) return nullptr;
    return level->getSpawner().spawnMob(*region, ActorDefinitionIdentifier(id), nullptr, Vec3{at.x + 0.5f, float(at.y), at.z + 0.5f}, false, false, false);
}

Mob* mobById(std::int64_t uid) {
    auto* a = fe::actorById(uid);
    return a ? fe::asMob(*a) : nullptr;
}

Container* chestAt(core::GlobalPos const& pos) {
    auto* region = game::regionOf(pos.dim);
    if (!region) return nullptr;
    auto* be = region->getBlockEntity(game::blockPos(pos));
    if (!be || be->getType() != BlockActorType::Chest) return nullptr;
    return static_cast<ChestBlockActor*>(be)->getContainer();
}

int countOf(Container& c, std::string const& id) {
    int n = 0;
    for (int i = 0; i < c.getContainerSize(); ++i)
        if (game::items::idOf(c.getItem(i)) == id) n += game::items::countOf(c.getItem(i));
    return n;
}

} // namespace

// e2e (unit record): the fixture boss with 3 parts, its bar, its death chest and its conquest
GF_REGISTER_SELFTEST("entity.boss_lifecycle", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    resetTestRecords();
    auto  box     = co_await ctx.flatArea(5840, 12);
    auto  home    = ctx.testPos(5845, 2);
    auto& capture = ctx.captureBossEvent();
    auto& sp      = ctx.spawnPlayer();
    auto  spId    = sp.getOrCreateUniqueID().rawID;
    sp.teleport(Vec3{home.x + 0.5f, float(home.y), home.z + 3.5f}, DimensionType(home.dim));
    co_await ctx.ticks(2);
    auto* boss = spawnMob(home, "gf_test:boss");
    ctx.require(boss != nullptr, "the fixture boss spawns");
    auto uid = fe::uidOf(*boss);
    ge::setHome(*boss, home, 20);
    std::vector<ge::PartHandle> parts;
    for (int i = 0; i < 3; ++i) {
        float dx = (i - 1) * 2.0f;
        parts.push_back(ge::addPart(*boss, "segment_" + std::to_string(i), {0.8f, 0.8f}, [dx](Mob&) { return Vec3{dx, 2.2f, 0.0f}; }));
    }
    co_await ctx.ticks(3);
    // the bar: a BossEventPacket "add" with the nearest vanilla colour
    bool added = false;
    for (auto const& e : capture.events())
        added |= e.bossId == uid && e.type == "add" && e.color == gf::boss::ext::nearestBarColor(kFixtureRgb) && !e.name.empty();
    ctx.check(added, "a BossEventPacket 'add' with the nearest colour (" + std::to_string(gf::boss::ext::nearestBarColor(kFixtureRgb)) + ") was sent");
    ctx.check(gf::boss::ext::nearestBarColor(kFixtureRgb) == 2, "orange is nearest to the red bar");
    boss = mobById(uid);
    ctx.require(boss != nullptr, "the boss is there");
    ctx.check(gf::boss::ext::barColor(*boss) == kFixtureRgb, "the exact RGB is kept for the client mod");
    // parts follow; a hit on a part lands on the boss and qualifies the player
    for (int i = 0; i < 3; ++i) ctx.require(ge::partActor(parts[i]) != nullptr, "part " + std::to_string(i) + " exists");
    auto bp = boss->getFeetPos();
    auto pp = ge::partActor(parts[1])->getFeetPos();
    ctx.check(std::abs(pp.y - (bp.y + 2.2f)) < 0.6f, "the parts follow the boss");
    int before = boss->getHealth();
    sp.teleport(Vec3{pp.x, pp.y - 1.0f, pp.z + 1.5f}, DimensionType(home.dim));
    co_await ctx.ticks(2);
    if (auto* p = fe::playerById(spId)) static_cast<SimulatedPlayer*>(p)->simulateAttack(ge::partActor(parts[1]));
    co_await ctx.ticks(3);
    boss = mobById(uid);
    ctx.require(boss != nullptr, "the boss survives a hit");
    ctx.check(boss->getHealth() < before, "the hit on a part damaged the boss");
    bool percent = false;
    for (auto const& e : capture.events()) percent |= e.bossId == uid && e.type == "update_percent" && e.percent < 1.0f;
    ctx.check(percent, "the bar's progress followed the health ('update_percent')");
    auto part = gf::boss::participants(*boss);
    ctx.check(part.size() == 1 && fe::uidOf(*part[0]) == spId, "the attacker qualified for the fight (participants)");
    // the loot buffer (IBossLootBuffer) goes into the death chest
    auto item = game::items::make("gf_test:fixture_item", 3);
    ctx.require(item.has_value(), "fixture item available");
    gf::boss::ext::setLootBufferSlot(*boss, 4, *item);
    ctx.check(game::items::idOf(gf::boss::ext::lootBuffer(*boss)[4]) == "gf_test:fixture_item", "the loot buffer keeps a slot");
    auto partId = fe::uidOf(*ge::partActor(parts[0]));
    boss->kill();
    co_await ctx.ticks(2);
    if (auto* dying = mobById(uid)) ctx.check(gf::boss::ext::deathTime(*dying) > 0, "the boss is dying (deathTime counts)");
    co_await ctx.ticks(30);
    auto chest = lastDeathChest();
    ctx.require(chest.has_value(), "a death chest was placed");
    ctx.cleanupBlock(*chest);
    ctx.check(chest->x == home.x && chest->z == home.z, "the death chest is under the home point (column " + std::to_string(chest->x) + " " + std::to_string(chest->z) + ")");
    auto* container = chestAt(*chest);
    ctx.require(container != nullptr, "the death chest is a container");
    ctx.check(countOf(*container, "gf_test:fixture_item") >= 3, "the death chest holds the loot buffer");
    // the conquest of the home landmark
    auto conquest = lastConquest();
    ctx.check(conquest && conquest->first == home && conquest->second == "gf_test:fixture", "the home landmark was conquered");
    if (auto start = gf::landmark::startAt(home.dim, game::blockPos(home))) ctx.check(start->conquered, "the landmark start is conquered");
    // the bar is gone, the parts with it
    bool removed = false;
    for (auto const& e : capture.events()) removed |= e.bossId == uid && e.type == "remove";
    ctx.check(removed, "the bar was removed ('remove')");
    ctx.check(!hasBar(uid), "no bar is left");
    ctx.check(fe::actorById(partId) == nullptr, "the parts went with the boss");
    (void)box;
});

GF_REGISTER_SELFTEST("entity.boss_peaceful", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope scope(Difficulty::Easy);
    // the fixture spawner exists only while this test runs (other tests place fixture blocks)
    struct SpawnerScope {
        SpawnerScope() { gf::boss::registerSpawner({.blockId = "gf_test:fixture_block", .entityId = "gf_test:boss", .range = 9}); }
        ~SpawnerScope() { unregisterSpawnerForTest("gf_test:fixture_block"); }
    } spawnerScope;
    auto box  = co_await ctx.flatArea(5860, 12);
    auto home = ctx.testPos(5863, 2);
    // peaceful: the boss turns back into its spawner at home (BaseTFBoss#checkDespawn)
    auto* boss = spawnMob(home.offset(2, 0, 0), "gf_test:boss");
    ctx.require(boss != nullptr, "the fixture boss spawns");
    auto uid = fe::uidOf(*boss);
    ge::setHome(*boss, home, 20);
    ctx.cleanupBlock(home);
    co_await ctx.ticks(2);
    scope.set(Difficulty::Peaceful);
    co_await ctx.ticks(3);
    ctx.check(ctx.blockAt(home) == "gf_test:fixture_block", "the spawner is back at home");
    ctx.check(fe::actorById(uid) == nullptr || !fe::actorById(uid)->isAlive(), "the boss is gone");
    // the spawner waits on peaceful, spawns when a player is in range otherwise, and breaks
    auto& sp = ctx.spawnPlayer();
    sp.teleport(Vec3{home.x + 3.5f, float(home.y), home.z + 0.5f}, DimensionType(home.dim));
    co_await ctx.ticks(25);
    ctx.check(ctx.blockAt(home) == "gf_test:fixture_block", "no spawn on peaceful");
    scope.set(Difficulty::Easy);
    std::int64_t spawned = 0;
    for (int i = 0; i < 60 && !spawned; ++i) {
        co_await ctx.ticks(1);
        if (ctx.blockAt(home) != "gf_test:fixture_block") {
            auto* region = game::regionOf(home.dim);
            if (!region) break;
            auto const& b = region->getBlock(game::blockPos(home));
            (void)b;
            for (auto* a : region->getEntities(AABB{Vec3{home.x - 3.0f, home.y - 3.0f, home.z - 3.0f}, Vec3{home.x + 4.0f, home.y + 4.0f, home.z + 4.0f}}, 0.0f))
                if (a && a->getTypeName() == "gf_test:boss" && a->isAlive()) spawned = fe::uidOf(*a);
        }
    }
    ctx.check(spawned != 0, "the spawner spawned the boss");
    ctx.check(ctx.blockAt(home) == "minecraft:air", "the spawner destroyed itself");
    if (auto* b = mobById(spawned)) {
        auto h = ge::home(*b);
        ctx.check(h && h->pos == home, "the spawned boss's home is the spawner");
        b->remove();
    }
    (void)box;
});

GF_REGISTER_SELFTEST("entity.boss_tables", [](Context& ctx) {
    // BossVariant: 10 rows, floor and wall trophies, the comparator strengths of TFBlocks
    char const* variants[] = {"naga", "lich", "hydra", "ur_ghast", "knight_phantom", "snow_queen", "minoshroom", "alpha_yeti", "quest_ram", "final_boss"};
    int         rows       = 0;
    for (auto const* v : variants) rows += gf::boss::trophyVariant(v).has_value() ? 1 : 0;
    ctx.check(rows == 10, "10 BossVariant rows (" + std::to_string(rows) + ")");
    auto naga = gf::boss::trophyVariant("twilightforest:naga_trophy");
    ctx.check(naga && naga->sound == "entity.twilightforest.naga.rattle" && naga->volume == 1.25f && naga->pitch == 1.2f && naga->comparator == 5, "naga trophy: rattle 1.25 / 1.2, comparator 5");
    auto wall = gf::boss::trophyVariant("twilightforest:snow_queen_wall_trophy");
    ctx.check(wall && wall->sound == "entity.twilightforest.snow_queen.ambient" && wall->comparator == 0, "a wall trophy gives no comparator signal");
    auto ghast = gf::boss::trophyVariant("twilightforest:ur_ghast_trophy");
    ctx.check(ghast && ghast->pitch == 0.6f && ghast->comparator == 13, "ur-ghast trophy: pitch 0.6, comparator 13");
    ctx.check(!gf::boss::trophyVariant("twilightforest:oak_log").has_value(), "no variant for other blocks");
    auto roar = gf::boss::ext::trophyRareSound("twilightforest:alpha_yeti_trophy");
    ctx.check(roar && roar->oneIn == 50 && roar->sound == "entity.twilightforest.alpha_yeti.roar", "the alpha yeti trophy roars once in 50");
    // the 8-colour fallback
    ctx.check(gf::boss::ext::nearestBarColor(0xFF0000) == 2, "red -> RED");
    ctx.check(gf::boss::ext::nearestBarColor(0xFFFFFF) == 7, "white -> WHITE");
    ctx.check(gf::boss::ext::nearestBarColor(0x00B7EC) == 1, "blue -> BLUE");
    ctx.check(gf::boss::ext::nearestBarColor(0xEC00B8) == 0, "pink -> PINK");
    ctx.check(gf::boss::ext::nearestBarColor(0xE9EC00) == 4, "yellow -> YELLOW");
    ctx.check(gf::boss::ext::nearestBarColor(0x5E9916) == 3, "the naga's green -> GREEN");
    // config "bosses" defaults (TFConfig)
    ctx.check(gf::boss::ext::dropChests(), "bossesSpawnDropChests defaults to true");
    ctx.check(!gf::boss::ext::adjustsHealth() && !gf::boss::ext::adjustsLootRolls(), "multiplayerFightAdjuster defaults to NONE");
    // minions
    (void)ctx;
});

GF_REGISTER_SELFTEST("entity.boss_minion", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  box = co_await ctx.flatArea(5880, 6);
    auto  at  = ctx.testPos(5881, 2);
    auto* mob = spawnMob(at, "gf_test:boss_part");
    ctx.require(mob != nullptr, "a fixture actor spawns");
    auto uid = fe::uidOf(*mob);
    ctx.check(!gf::boss::isMinion(*mob), "not a minion by default");
    gf::boss::setMinion(*mob, true);
    ctx.check(gf::boss::isMinion(*mob), "setMinion marks it");
    gf::boss::setMinion(*mob, false);
    ctx.check(!gf::boss::isMinion(*mob), "and unmarks it");
    co_await ctx.ticks(1);
    if (auto* a = fe::actorById(uid)) a->remove();
    (void)box;
});

} // namespace gf::feature::boss::tests
