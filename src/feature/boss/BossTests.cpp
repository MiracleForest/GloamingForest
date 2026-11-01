// Selftests of F8's "boss" contract (rule R12: prefix entity.*, dx 5800..5899): the e2e fixture
// boss (gf_test:boss with 3 parts: parts follow, damage routes to the parent, the boss bar's
// BossEventPackets, the death chest filled from the loot buffer, the conquest), peaceful turning
// the boss back into its spawner, the spawner variants (height rules, the Lich's floor), the
// sinister spawner, the trophy table and the bar colours; entity.client_fixture shows the fixture
// bosses to a real player for the client e2e's screenshots.

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
#include "feature/entity/EntityFixture.h"
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
using fe::tests::FixtureScope;
using fe::tests::kFixtureRgb;
using Difficulty = SharedTypes::Legacy::Difficulty;

namespace {

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

/// A fixture spawner (gf_test:fixture_block spawning gf_test:boss) while the test runs.
struct SpawnerScope {
    explicit SpawnerScope(std::optional<SpawnerKind> kind = std::nullopt) {
        gf::boss::registerSpawner({.blockId = "gf_test:fixture_block", .entityId = "gf_test:boss", .range = 9});
        setSpawnerKindForTest("gf_test:fixture_block", kind);
    }
    ~SpawnerScope() {
        setSpawnerKindForTest("gf_test:fixture_block", std::nullopt);
        unregisterSpawnerForTest("gf_test:fixture_block");
    }
};

Mob* spawnMob(core::GlobalPos const& at, std::string const& id, float dx = 0.5f, float dz = 0.5f) {
    auto* region = game::regionOf(at.dim);
    auto  level  = ll::service::getLevel();
    if (!region || !level) return nullptr;
    return level->getSpawner().spawnMob(*region, ActorDefinitionIdentifier(id), nullptr, Vec3{at.x + dx, float(at.y), at.z + dz}, false, false, false);
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

/// The live actors of `type` around `pos`.
std::vector<Actor*> actorsAround(core::GlobalPos const& pos, std::string const& type, float r) {
    std::vector<Actor*> out;
    auto*               region = game::regionOf(pos.dim);
    if (!region) return out;
    for (auto* a : region->getEntities(AABB{Vec3{pos.x - r, pos.y - r, pos.z - r}, Vec3{pos.x + 1 + r, pos.y + 1 + r, pos.z + 1 + r}}, 0.0f))
        if (a && a->isAlive() && a->getTypeName() == type) out.push_back(a);
    return out;
}

} // namespace

// e2e (unit record): the fixture boss with 3 parts, its bar, its death chest and its conquest
GF_REGISTER_SELFTEST("entity.boss_lifecycle", [](Context& ctx) -> ll::coro::CoroTask<> {
    FixtureScope    fixture;
    DifficultyScope easy(Difficulty::Easy);
    resetTestRecords();
    auto  box     = co_await ctx.flatArea(5840, 12);
    auto  home    = ctx.testPos(5845, 2);
    auto& capture = ctx.captureBossEvent();
    auto& sp      = ctx.spawnPlayer();
    auto  spId    = sp.getOrCreateUniqueID().rawID;
    sp.teleport(Vec3{home.x + 0.5f, float(home.y), home.z + 3.5f}, DimensionType(home.dim));
    co_await ctx.ticks(2);
    auto floor = ctx.blockAt(home.offset(0, -1, 0));
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
    // EntityUtil#bossChestLocation: home.below(), the floor block replaced (a piston could move it)
    ctx.check(chest->x == home.x && chest->z == home.z, "the death chest is under the home point (column " + std::to_string(chest->x) + " " + std::to_string(chest->z) + ")");
    ctx.check(chest->y == home.y - 1, "the death chest replaced the floor block at home.y - 1 (y " + std::to_string(chest->y) + ", home " + std::to_string(home.y) + ", floor was " + floor + ")");
    auto* container = chestAt(*chest);
    ctx.require(container != nullptr, "the death chest is a container");
    // IBossLootBuffer#fill spreads the table's roll over the shuffled slots and may land on the slot
    // set above (Java's buffer is empty before the death); the stub loot contract rolls nothing
    int inChest = countOf(*container, "gf_test:fixture_item");
    ctx.check(inChest >= 1, "the death chest holds the loot buffer (" + std::to_string(inChest) + " fixture items)");
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
    // put the floor back (the chest empty first, so nothing drops)
    if (auto* c = chestAt(*chest))
        for (int i = 0; i < c->getContainerSize(); ++i) c->setItem(i, game::items::empty());
    if (auto* region = game::regionOf(chest->dim))
        if (auto const* b = game::blockWith(floor.empty() ? "minecraft:grass_block" : floor)) game::setBlock(*region, game::blockPos(*chest), *b);
    (void)box;
});

GF_REGISTER_SELFTEST("entity.boss_peaceful", [](Context& ctx) -> ll::coro::CoroTask<> {
    FixtureScope    fixture;
    DifficultyScope scope(Difficulty::Easy);
    // the fixture spawner exists only while this test runs (other tests place fixture blocks)
    SpawnerScope spawner;
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
        if (ctx.blockAt(home) != "gf_test:fixture_block")
            for (auto* a : actorsAround(home, "gf_test:boss", 3.0f)) spawned = fe::uidOf(*a);
    }
    ctx.check(spawned != 0, "the spawner spawned the boss");
    ctx.check(ctx.blockAt(home) == "minecraft:air", "the spawner destroyed itself");
    if (auto* b = mobById(spawned)) {
        auto h = ge::home(*b);
        ctx.check(h && h->pos == home, "the spawned boss's home is the spawner");
        ctx.check(ge::data(*b, "boss:spawner") == "gf_test:fixture_block", "the boss knows its spawner block");
        b->remove();
    }
    (void)box;
});

// The variants of BossSpawnerBlockEntity: the height rules (ur-ghast, snow queen, alpha yeti: the
// nearest player above pos.y - 4) and LichSpawnerBlockEntity's spawn point (down through the air
// below the spawner, then one up: spawn and home there); the floor rule of the death chest
GF_REGISTER_SELFTEST("entity.boss_spawner_rules", [](Context& ctx) -> ll::coro::CoroTask<> {
    FixtureScope    fixture;
    DifficultyScope easy(Difficulty::Easy);
    auto            box  = co_await ctx.flatArea(5856, 4);
    auto            home = ctx.testPos(5857, 2);
    auto&           sp   = ctx.spawnPlayer();
    auto            spId = sp.getOrCreateUniqueID().rawID;
    sp.teleport(Vec3{home.x + 0.5f, float(home.y), home.z + 0.5f}, DimensionType(home.dim));
    co_await ctx.ticks(4);
    auto* p = fe::playerById(spId);
    ctx.require(p != nullptr, "the test player is there");
    // the height rules (closestPlayer.getY() against the spawner's y)
    for (auto const* id : {"twilightforest:ur_ghast_boss_spawner", "twilightforest:snow_queen_boss_spawner", "twilightforest:alpha_yeti_boss_spawner"}) {
        auto rule = spawnerRuleForTest(id);
        ctx.require(rule.has_value(), std::string(id) + " is a spawner");
        ctx.check(!heightRuleAllows(rule->kind, 100, 96.0) && heightRuleAllows(rule->kind, 100, 96.5), std::string(id) + ": the nearest player must stand above y - 4");
    }
    auto mino = spawnerRuleForTest("twilightforest:minoshroom_boss_spawner");
    ctx.check(mino && heightRuleAllows(mino->kind, 100, 103.5) && !heightRuleAllows(mino->kind, 100, 104.0), "minoshroom: below y + 4");
    auto knights = spawnerRuleForTest("twilightforest:knight_phantom_boss_spawner");
    ctx.check(knights && !heightRuleAllows(knights->kind, 100, 98.0) && heightRuleAllows(knights->kind, 100, 98.5), "knight phantoms: above y - 2");
    auto hydra = spawnerRuleForTest("twilightforest:hydra_boss_spawner");
    ctx.check(hydra && heightRuleAllows(hydra->kind, 100, 0.0), "hydra: no height rule");
    // in the world: the player here wakes an ur-ghast spawner 3 above it (range 9)
    int      fy = static_cast<int>(std::floor(p->getFeetPos().y + 0.001f));
    BlockPos low{home.x, fy + 3, home.z};
    ctx.check(anyPlayerInRangeForTest(home.dim, low, "twilightforest:ur_ghast_boss_spawner"), "a player 3 below wakes the ur-ghast spawner");
    ctx.check(!anyPlayerInRangeForTest(home.dim, BlockPos{home.x + 40, fy, home.z}, "twilightforest:ur_ghast_boss_spawner"), "nobody within 9 blocks: it waits");
    // the death chest's floor rule (PushReaction != BLOCK)
    auto* region = ctx.region();
    ctx.require(region != nullptr, "the test dimension");
    auto stone = home.offset(2, 0, 2), obsidian = home.offset(3, 0, 2), bedrock = home.offset(2, 0, 3);
    ctx.require(ctx.setBlock(stone, "minecraft:stone") && ctx.setBlock(obsidian, "minecraft:obsidian") && ctx.setBlock(bedrock, "minecraft:bedrock"), "place the floor probes");
    ctx.check(pistonMovable(*region, game::blockPos(stone)), "stone: a piston moves it (the chest replaces it)");
    ctx.check(!pistonMovable(*region, game::blockPos(obsidian)), "obsidian: BLOCK");
    ctx.check(!pistonMovable(*region, game::blockPos(bedrock)), "bedrock: BLOCK");
    ctx.check(pistonMovable(*region, game::blockPos(home.offset(0, -1, 0))), "the flat area's grass floor: movable");
    // the Lich: spawner 3 above the floor, two air blocks between
    {
        SpawnerScope spawner(SpawnerKind::Lich);
        auto         at = home.offset(0, 3, 0);
        ctx.require(ctx.setBlock(at, "gf_test:fixture_block"), "place the lich-rule spawner");
        std::int64_t spawned = 0;
        float        y       = 0;
        for (int i = 0; i < 60 && !spawned; ++i) {
            co_await ctx.ticks(1);
            if (ctx.blockAt(at) != "gf_test:fixture_block")
                for (auto* a : actorsAround(home, "gf_test:boss", 4.0f)) spawned = fe::uidOf(*a), y = a->getFeetPos().y;
        }
        ctx.require(spawned != 0, "the lich-rule spawner spawned");
        auto* b = mobById(spawned);
        ctx.require(b != nullptr, "the spawned boss");
        auto h = ge::home(*b);
        ctx.check(h && h->pos == home.offset(0, 1, 0), "LichSpawnerBlockEntity: the home is one above the lowest air block (" + (h ? std::to_string(h->pos.y) : std::string("none")) + ", floor surface " + std::to_string(home.y) + ")");
        ctx.check(std::abs(y - float(home.y + 1)) < 0.5f, "and the lich spawned there (" + std::to_string(y) + ")");
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
    // the spawner block entities (BossSpawnerBlockEntity and its variants)
    struct Row {
        char const* block;
        char const* entity;
        int         range;
        char const* particle;
        SpawnerKind kind;
    };
    Row const spawners[] = {
        {"twilightforest:naga_boss_spawner", "twilightforest:naga", 50, "minecraft:crit", SpawnerKind::Default},
        {"twilightforest:lich_boss_spawner", "twilightforest:lich", 9, "twilightforest:ominous_flame", SpawnerKind::Lich},
        {"twilightforest:hydra_boss_spawner", "twilightforest:hydra", 50, "minecraft:flame", SpawnerKind::Default},
        {"twilightforest:ur_ghast_boss_spawner", "twilightforest:ur_ghast", 9, "minecraft:dust", SpawnerKind::AboveMinus4},
        {"twilightforest:knight_phantom_boss_spawner", "twilightforest:knight_phantom", 9, "twilightforest:ominous_flame", SpawnerKind::KnightPhantoms},
        {"twilightforest:snow_queen_boss_spawner", "twilightforest:snow_queen", 9, "twilightforest:snow", SpawnerKind::AboveMinus4},
        {"twilightforest:minoshroom_boss_spawner", "twilightforest:minoshroom", 9, "minecraft:crimson_spore", SpawnerKind::BelowPlus4},
        {"twilightforest:alpha_yeti_boss_spawner", "twilightforest:alpha_yeti", 9, "minecraft:falling_water", SpawnerKind::AboveMinus4},
        {"twilightforest:final_boss_boss_spawner", "twilightforest:plateau_boss", 9, "twilightforest:annihilate", SpawnerKind::Never},
    };
    for (auto const& r : spawners) {
        auto rule = spawnerRuleForTest(r.block);
        ctx.check(rule && rule->entityId == r.entity && rule->range == r.range && rule->particle == r.particle && rule->kind == r.kind, std::string(r.block) + ": " + r.entity + ", range " + std::to_string(r.range) + ", " + r.particle);
    }
    auto ghastRule = spawnerRuleForTest("twilightforest:ur_ghast_boss_spawner");
    ctx.check(ghastRule && ghastRule->particleParams.is_object() && ghastRule->particleParams["color"] == nlohmann::json({1.0, 0.0, 0.0}), "ur-ghast spawner: DustParticleOptions.REDSTONE (red, scale 1)");
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

// SinisterSpawnerLogic: a fixture block as the sinister spawner, gf_test:boss_part as its mob
GF_REGISTER_SELFTEST("entity.sinister_spawner", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    struct SinisterScope {
        SinisterScope() { registerSinisterBlockForTest("gf_test:fixture_block"); }
        ~SinisterScope() { unregisterSinisterBlockForTest("gf_test:fixture_block"); }
    } sinister;
    auto box = co_await ctx.flatArea(5885, 8);
    auto at  = ctx.testPos(5888, 2);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "the test dimension");
    ctx.require(ctx.setBlock(at, "gf_test:fixture_block"), "place the sinister spawner");
    auto bp = game::blockPos(at);
    auto s0 = gf::boss::ext::sinisterSpawner(*region, bp);
    ctx.require(s0.has_value(), "a sinister spawner block has a spawner (defaults)");
    ctx.check(s0->spawnDelay == 20 && s0->minSpawnDelay == 200 && s0->maxSpawnDelay == 800 && s0->spawnCount == 4 && s0->maxNearbyEntities == 6
                  && s0->requiredPlayerRange == 16 && s0->spawnRange == 4,
              "BaseSpawner's defaults (delay 20, 200..800, count 4, max 6, player range 16, range 4)");
    gf::boss::ext::SinisterSpawner cfg;
    cfg.potentials = {{"gf_test:boss_part", 1}};
    cfg.spawnDelay = 5;
    cfg.particles  = {"minecraft:flame"};
    ctx.require(gf::boss::ext::setSinisterSpawner(*region, bp, cfg), "configure the spawner");
    ctx.check(gf::boss::ext::addSinisterParticle(*region, bp, "minecraft:smoke") && !gf::boss::ext::addSinisterParticle(*region, bp, "minecraft:smoke"), "addParticle adds once");
    ctx.check(gf::boss::ext::removeSinisterParticle(*region, bp, "minecraft:smoke") && !gf::boss::ext::removeSinisterParticle(*region, bp, "minecraft:smoke"), "removeParticle removes once");
    ctx.check(gf::boss::ext::setSinisterLootTable(*region, bp, "gf_test:chests/fixture_chest") && gf::boss::ext::sinisterLootTable(*region, bp) == "gf_test:chests/fixture_chest", "the loot table is kept");
    ctx.check(!gf::boss::ext::sinisterSpawner(*region, game::blockPos(at.offset(1, 0, 0))).has_value(), "another block has no sinister spawner");
    int before = sinisterSpawnedForTest();
    auto& sp = ctx.spawnPlayer();
    sp.teleport(Vec3{at.x + 3.5f, float(at.y), at.z + 0.5f}, DimensionType(at.dim));
    for (int i = 0; i < 200 && sinisterSpawnedForTest() == before; ++i) co_await ctx.ticks(1);
    ctx.check(sinisterSpawnedForTest() > before, "the spawner spawned (" + std::to_string(sinisterSpawnedForTest() - before) + ")");
    auto mobs = actorsAround(at, "gf_test:boss_part", 6.0f);
    ctx.check(!mobs.empty(), "its mobs stand around it");
    for (auto* m : mobs) {
        auto f = m->getFeetPos();
        ctx.check(std::abs(f.x - (at.x + 0.5f)) <= 4.6f && std::abs(f.z - (at.z + 0.5f)) <= 4.6f, "a mob within the spawn range");
    }
    if (auto s1 = gf::boss::ext::sinisterSpawner(*region, bp))
        ctx.check(s1->spawnDelay >= 190 && s1->spawnDelay < 800, "the delay reset to 200..800 after spawning (" + std::to_string(s1->spawnDelay) + ")");
    for (auto* m : actorsAround(at, "gf_test:boss_part", 8.0f)) m->remove();
    (void)box;
});

// The fixture bosses for the client e2e (tools/e2e/client/scenarios/entity.ps1): the orange bar
// and a boss with 3 parts next to a real (connected) player, long enough for the screenshots; then
// peaceful sends both away (no home: BaseTFBoss#checkDespawn). Without a real player (server
// runs) it runs briefly at its own test position.
GF_REGISTER_SELFTEST("entity.client_fixture", [](Context& ctx) -> ll::coro::CoroTask<> {
    FixtureScope    fixture;
    DifficultyScope scope(Difficulty::Easy);
    Player*         real = nullptr;
    if (auto level = ll::service::getLevel())
        level->forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
            if (!p.isSimulatedPlayer()) real = &p;
            return real == nullptr;
        }));
    core::GlobalPos at;
    core::GlobalPos partsAt;
    int             hold = 10;
    if (real) {
        // in front of the player (in the screenshots): the bar boss 6 ahead and 2 to one side, the
        // parts boss 7 ahead and 3 to the other
        auto   f   = real->getFeetPos();
        auto   v   = real->getViewVector(1.0f);
        double len = std::sqrt(double(v.x) * v.x + double(v.z) * v.z);
        double fx = len > 1e-6 ? v.x / len : 0.0, fz = len > 1e-6 ? v.z / len : 1.0;
        auto   spot = [&](double ahead, double side) {
            return core::GlobalPos{static_cast<int>(real->getDimensionId()), static_cast<int>(std::floor(f.x + fx * ahead - fz * side)), static_cast<int>(std::floor(f.y)), static_cast<int>(std::floor(f.z + fz * ahead + fx * side))};
        };
        at      = spot(6.0, 2.0);
        partsAt = spot(7.0, -3.0);
        hold    = 400;
        ctx.info("showing the fixture bosses to " + real->getRealName());
    } else {
        auto box = co_await ctx.flatArea(5890, 4);
        at       = ctx.testPos(5891, 2);
        partsAt  = at.offset(1, 0, 2);
        auto& sp = ctx.spawnPlayer();
        sp.teleport(Vec3{at.x - 2.5f, float(at.y), at.z + 0.5f}, DimensionType(at.dim));
        (void)box;
    }
    auto* bar = spawnMob(at, "gf_test:boss");
    ctx.require(bar != nullptr, "the bar boss spawns");
    auto barId = fe::uidOf(*bar);
    ctx.info("bar boss ready");
    for (int i = 0; i < hold; ++i) {
        co_await ctx.ticks(1);
        ctx.require(mobById(barId) != nullptr, "the bar boss stays");
    }
    auto* parted = spawnMob(partsAt, "gf_test:boss");
    ctx.require(parted != nullptr, "the parts boss spawns");
    parted->setNameTag("gf_parts");
    auto partsId = fe::uidOf(*parted);
    bool three   = false;
    for (int i = 0; i < 20 && !three; ++i) {
        co_await ctx.ticks(1);
        if (auto* m = mobById(partsId)) three = ge::ext::parts(*m).size() == 3;
    }
    ctx.require(three, "the parts boss has 3 parts");
    ctx.info("parts boss ready");
    for (int i = 0; i < hold; ++i) co_await ctx.ticks(1);
    scope.set(Difficulty::Peaceful);
    co_await ctx.ticks(5);
    ctx.check(!mobById(barId) && !mobById(partsId), "on peaceful both fixture bosses left");
    if (auto* m = mobById(barId)) m->remove();
    if (auto* m = mobById(partsId)) m->remove();
});

} // namespace gf::feature::boss::tests
