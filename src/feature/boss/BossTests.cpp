// Selftests of F8's "boss" contract (rule R12: prefix entity.*, dx 5800..5899): the e2e fixture
// boss (gf_test:boss with 3 parts: parts follow, damage routes to the parent, the boss bar's
// BossEventPackets, the death chest filled from the loot buffer, the conquest), the conquest of a
// real landmark start (gf::landmark) with its structure_cleared and hurt_boss triggers, no portal
// travel for bosses and parts, peaceful turning the boss back into its spawner, the spawner
// variants (height rules, the Lich's floor), the sinister spawner, the trophy table and the bar
// colours; entity.client_fixture shows the fixture bosses to a real player for the client e2e's
// screenshots.

#include <algorithm>
#include <cmath>
#include <set>

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
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
#include "feature/dimension/World.h"
#include "feature/entity/Entity.h"
#include "feature/entity/EntityFixture.h"
#include "feature/entity/EntityInternal.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/inventory/Inventory.h"
#include "feature/ext/landmark/LandmarkTest.h" // entity.boss_conquest's forced start (ScopedStart)
#include "feature/landmark/Landmark.h"
#include "feature/net/Net.h"
#include "feature/progression/Progression.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
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

/// Spawns any actor (a projectile: not a mob) through the actor factory.
Actor* spawnActor(core::GlobalPos const& at, std::string const& id) {
    auto* region = game::regionOf(at.dim);
    auto  level  = ll::service::getLevel();
    if (!region || !level) return nullptr;
    auto owned = level->getActorFactory().createSpawnedActor(ActorDefinitionIdentifier(id), nullptr, Vec3{at.x + 0.5f, float(at.y), at.z + 0.5f}, Vec2{0.0f, 0.0f});
    return owned ? level->addEntity(*region, std::move(owned)) : nullptr;
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
    // set above (Java's buffer is empty before the death)
    int inChest = countOf(*container, "gf_test:fixture_item");
    ctx.check(inChest >= 1, "the death chest holds the loot buffer (" + std::to_string(inChest) + " fixture items)");
    // the conquest of the home structure at the home point (gf_test:fixture has no landmark start
    // here; entity.boss_conquest conquers a real one)
    auto conquest = lastConquest();
    ctx.check(conquest && conquest->first == home && conquest->second == "gf_test:fixture", "the conquest of the home structure was asked at the home point");
    ctx.check(!lastConquestFound(), "and found no start of it to conquer (gf_test:fixture is no landmark)");
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

namespace {

/// The unlocked fixture landmark of the landmark contract (development builds): conquerable like
/// the Naga courtyard (no progression lock, so it protects nothing), a 7x7x7 stone brick shell
/// around its center with a porch in front.
constexpr char const* kConquestLandmark = "gf_test:landmark_fixture_open";

/// The progression triggers fired while a TriggerRecorder lives. The contract's listeners cannot
/// be removed: one listener per trigger for the process, recording only while a recorder lives.
struct TriggerEvent {
    std::string    trigger;
    std::int64_t   player = 0;
    nlohmann::json data;
};
struct TriggerLog {
    bool                      on = false;
    std::vector<TriggerEvent> events;
};
TriggerLog& triggerLog() {
    static TriggerLog t;
    return t;
}
struct TriggerRecorder {
    TriggerRecorder() {
        static bool const listening = [] {
            for (auto const* id : {"gloamingforest:hurt_boss", "gloamingforest:structure_cleared"})
                gf::progression::onTrigger(id, [trigger = std::string(id)](Player& p, nlohmann::json const& data) {
                    if (triggerLog().on) triggerLog().events.push_back({trigger, p.getOrCreateUniqueID().rawID, data});
                });
            return true;
        }();
        (void)listening;
        triggerLog() = {.on = true, .events = {}};
    }
    ~TriggerRecorder() { triggerLog() = {}; }
    TriggerRecorder(TriggerRecorder const&)            = delete;
    TriggerRecorder& operator=(TriggerRecorder const&) = delete;
    /// The index of the first `trigger` fired for `player` (-1: none).
    [[nodiscard]] int find(std::string const& trigger, std::int64_t player) const {
        auto const& ev = triggerLog().events;
        for (std::size_t i = 0; i < ev.size(); ++i)
            if (ev[i].trigger == trigger && ev[i].player == player) return static_cast<int>(i);
        return -1;
    }
    [[nodiscard]] nlohmann::json data(int index) const { return index >= 0 ? triggerLog().events[index].data : nlohmann::json(); }
    /// How often `trigger` fired for `player`.
    [[nodiscard]] int count(std::string const& trigger, std::int64_t player) const {
        int n = 0;
        for (auto const& e : triggerLog().events) n += e.trigger == trigger && e.player == player ? 1 : 0;
        return n;
    }
};

} // namespace

namespace {

/// The item entities of `itemId` within `range` blocks of the block `pos` (removed; their count).
int collectDrops(core::GlobalPos const& pos, std::string const& itemId, float range) {
    auto level = ll::service::getLevel();
    if (!level) return 0;
    int n = 0;
    for (auto* a : level->getRuntimeActorList()) {
        if (!a || !a->isType(ActorType::ItemEntity) || static_cast<int>(a->getDimensionId()) != pos.dim) continue;
        auto const& p = a->getPosition();
        if (std::abs(p.x - (pos.x + 0.5f)) > range || std::abs(p.y - (pos.y + 0.5f)) > range || std::abs(p.z - (pos.z + 0.5f)) > range) continue;
        auto const& item = static_cast<ItemActor*>(a)->item();
        if (game::items::idOf(item) != itemId) continue;
        n += game::items::countOf(item);
        a->remove();
    }
    return n;
}

} // namespace

// BaseTFBoss#postmortem -> LandmarkUtil#markStructureConquered on a real landmark start (the landmark
// contract): the fixture boss's home structure is the unlocked fixture landmark while this test runs.
// A start forced around the boss's home point is conquered when the boss dies there;
// "structure_cleared" reaches the player within 32 blocks of the home point and not the one beyond;
// "hurt_boss" reaches the player who hurt the boss, before the conquest (LivingDeathEvent is posted at
// the start of LivingEntity#die, #postmortem runs after super.die); the death chest takes the
// landmark's floor under the home point. The forced start is a fresh one (a start an aborted run
// left is dropped first) and is dropped again when the test ends, so no start stays in the world
// and no flag is reset through setConquered (which fires structure_cleared whatever the flag).
GF_REGISTER_SELFTEST("entity.boss_conquest", [](Context& ctx) -> ll::coro::CoroTask<> {
    FixtureScope    fixture;
    DifficultyScope easy(Difficulty::Easy);
    ctx.require(gf::landmark::descriptor(kConquestLandmark) != nullptr, "the fixture landmark exists (the gf_test data is deployed)");
    ctx.require(gf::landmark::descriptor(kConquestLandmark)->conquerable, "and is conquerable");
    auto spec          = fe::tests::fixtureBossSpec();
    spec.homeStructure = kConquestLandmark;
    gf::boss::registerBoss(spec);
    struct SpecScope {
        ~SpecScope() { gf::boss::registerBoss(fe::tests::fixtureBossSpec()); }
    } specScope;
    resetTestRecords();
    auto box    = co_await ctx.flatArea(5805, 12);
    auto center = ctx.testPos(5809, 3);
    auto home   = center.offset(0, 1, 0); // on the shell's floor
    // the landmark start around the home point, fresh: a forced start an aborted run of an older
    // build left is dropped first (forceStart would keep its flag), then the scope forces one and
    // removes it again when the test ends, also when it aborts (F6's LandmarkTest.h)
    (void)gf::landmark::ext::forgetStartForTest(center, kConquestLandmark);
    std::optional<gf::landmark::ext::ScopedStart> forced;
    forced.emplace(kConquestLandmark, center);
    ctx.require(*forced && (*forced)->generated, "forceStart made the fixture landmark");
    auto start = gf::landmark::startAt(home.dim, game::blockPos(home));
    ctx.require(start.has_value() && start->key == kConquestLandmark, "the home point is inside the landmark start");
    ctx.check(!gf::landmark::isConquered(*start), "the start is not conquered before the fight");
    auto shellFloor = ctx.blockAt(center);
    ctx.check(!shellFloor.empty() && shellFloor != "minecraft:air", "the landmark's shell stands (its floor: " + shellFloor + ")");
    // a player inside with the boss, one 40 blocks away
    auto& near   = ctx.spawnPlayer("gf_conquest_near");
    auto  nearId = near.getOrCreateUniqueID().rawID;
    near.teleport(Vec3{home.x + 0.5f, float(home.y), home.z + 2.5f}, DimensionType(home.dim));
    auto& far   = ctx.spawnPlayer("gf_conquest_far");
    auto  farId = far.getOrCreateUniqueID().rawID;
    far.teleport(Vec3{home.x + 40.5f, float(home.y) + 1.0f, home.z + 0.5f}, DimensionType(home.dim));
    co_await ctx.ticks(3);
    auto* boss = spawnMob(home, "gf_test:boss");
    ctx.require(boss != nullptr, "the fixture boss spawns inside the landmark");
    auto uid = fe::uidOf(*boss);
    ge::setHome(*boss, home, 20);
    co_await ctx.ticks(2);
    // the player inside hurts it (MultiplayerInclusivityAttachment: qualified for hurt_boss)
    if (auto* p = fe::playerById(nearId); p && mobById(uid)) static_cast<SimulatedPlayer*>(p)->simulateAttack(mobById(uid));
    co_await ctx.ticks(2);
    boss = mobById(uid);
    ctx.require(boss != nullptr && boss->isAlive(), "the boss survives the hit");
    auto part = gf::boss::participants(*boss);
    ctx.check(part.size() == 1 && fe::uidOf(*part[0]) == nearId, "the player inside qualified for the fight");
    auto* f = fe::playerById(farId);
    ctx.require(f != nullptr, "the far player is there");
    auto fp = f->getFeetPos();
    ctx.check(std::abs(fp.x - (home.x + 0.5f)) > 34.0f, "the far player is beyond AABB(home).inflate(32) (x " + std::to_string(fp.x) + ", home " + std::to_string(home.x) + ")");
    TriggerRecorder triggers;
    boss->kill();
    co_await ctx.ticks(2);
    // the conquest: the start's flag, through markStructureConquered at the home point
    auto after = gf::landmark::startAt(home.dim, game::blockPos(home));
    ctx.require(after.has_value(), "the landmark start is still there");
    ctx.check(gf::landmark::isConquered(*after), "the boss's death conquered its home landmark");
    auto conquest = lastConquest();
    ctx.check(conquest && conquest->first == home && conquest->second == kConquestLandmark, "at the boss's home point, for its home structure");
    ctx.check(lastConquestFound(), "and the kit saw the start conquered");
    // the triggers
    int cleared = triggers.find("gloamingforest:structure_cleared", nearId);
    int hurt    = triggers.find("gloamingforest:hurt_boss", nearId);
    ctx.check(cleared >= 0 && triggers.data(cleared).value("structure", std::string()) == kConquestLandmark,
              "structure_cleared fired for the player within 32 blocks (" + triggers.data(cleared).dump() + ")");
    ctx.check(triggers.count("gloamingforest:structure_cleared", nearId) == 1, "once");
    ctx.check(triggers.find("gloamingforest:structure_cleared", farId) < 0, "and not for the player 40 blocks away");
    ctx.check(hurt >= 0 && triggers.data(hurt).value("entity", std::string()) == "gf_test:boss", "hurt_boss fired for the qualified player (" + triggers.data(hurt).dump() + ")");
    ctx.check(triggers.find("gloamingforest:hurt_boss", farId) < 0, "and not for the player who did not hurt the boss");
    ctx.check(hurt >= 0 && cleared > hurt, "hurt_boss (LivingDeathEvent) came before the conquest (postmortem)");
    // the death chest takes the landmark's floor under the home point
    for (int i = 0; i < 60 && !lastDeathChest(); ++i) co_await ctx.ticks(1);
    auto chest = lastDeathChest();
    ctx.require(chest.has_value(), "the death chest was placed");
    ctx.check(chest->x == home.x && chest->y == home.y - 1 && chest->z == home.z, "in the shell's floor under the home point");
    if (auto* c = chestAt(*chest))
        for (int i = 0; i < c->getContainerSize(); ++i) c->setItem(i, game::items::empty());
    // the start is dropped when the test ends: nothing of it stays in the world
    forced.reset();
    ctx.check(!gf::landmark::startAt(home.dim, game::blockPos(home)).has_value(), "the forced start is gone again");
    (void)box;
});

// Entity#canUsePortal: BaseTFBoss and upstream's parts refuse portals (canChangeDimensionsUsingPortal,
// which Bedrock's portals and the Gloaming portal ask); other actors keep the engine's answer unless
// a type is marked (gf::entity::ext::setCanUsePortal: RisingZombie; non-mob actors such as
// projectiles too: ChainBlock, CubeOfAnnihilation). Through F4's Gloaming portal: a boss standing
// in a lit portal is asked every tick and stays where it is.
GF_REGISTER_SELFTEST("entity.boss_portal", [](Context& ctx) -> ll::coro::CoroTask<> {
    FixtureScope    fixture;
    DifficultyScope easy(Difficulty::Easy);
    auto            box    = co_await ctx.flatArea(5841, 8);
    auto*           region = ctx.region();
    ctx.require(region != nullptr, "the test dimension");
    // a Gloaming portal first (its lightning must not strike the probes): a 2x2 pool in the grass
    // layer, ringed by grass blocks with plants on the sides (TFPortalBlock#tryToCreatePortal)
    int  y    = box.minY - 1;
    auto pool = core::GlobalPos{ctx.dimension(), box.minX + 4, y, box.minZ + 4};
    bool built = true;
    for (int dx = -1; dx <= 2; ++dx)
        for (int dz = -1; dz <= 2; ++dz) {
            bool inside = dx >= 0 && dx < 2 && dz >= 0 && dz < 2;
            bool corner = (dx == -1 || dx == 2) && (dz == -1 || dz == 2);
            auto ground = pool.offset(dx, 0, dz);
            auto id     = std::string(inside ? "minecraft:water" : "minecraft:grass_block");
            if (ctx.blockAt(ground) != id) ctx.setBlock(ground, id);
            built &= ctx.blockAt(ground) == id;
            if (!inside && !corner) {
                if (ctx.blockAt(ground.offset(0, 1, 0)) != "minecraft:short_grass") ctx.setBlock(ground.offset(0, 1, 0), "minecraft:short_grass");
                built &= ctx.blockAt(ground.offset(0, 1, 0)) == "minecraft:short_grass";
            }
        }
    ctx.require(built, "the portal pool is built");
    ctx.require(gf::portal::tryActivate(*region, game::blockPos(pool)), "the Gloaming portal lights");
    co_await ctx.ticks(10); // the lightning is gone
    auto at   = ctx.testPos(5842, 2);
    auto* boss = spawnMob(at, "gf_test:boss");
    auto* pig  = spawnMob(ctx.testPos(5844, 1), "minecraft:pig");
    ctx.require(boss && pig, "a fixture boss and a pig spawn");
    auto bossId = fe::uidOf(*boss), pigId = fe::uidOf(*pig);
    std::int64_t ballId = 0, inPortalId = 0;
    struct Cleanup {
        std::int64_t& ball;
        std::int64_t& inPortal;
        std::int64_t  boss, pig;
        ~Cleanup() {
            ge::ext::setCanUsePortal("minecraft:pig", true);
            ge::ext::setCanUsePortal("minecraft:snowball", true);
            fe::watchPortalQueriesForTest(0);
            for (auto id : {boss, pig, ball, inPortal})
                if (auto* a = id ? fe::actorById(id) : nullptr) a->remove();
        }
    } cleanup{ballId, inPortalId, bossId, pigId};
    auto handle = ge::addPart(*boss, "portal_probe", {0.8f, 0.8f}, [](Mob&) { return Vec3{0.0f, 2.5f, 0.0f}; });
    co_await ctx.ticks(2);
    boss = mobById(bossId);
    pig  = mobById(pigId);
    ctx.require(boss && pig, "both are there");
    ctx.check(!boss->canChangeDimensionsUsingPortal(), "an upstream boss never goes through a portal (BaseTFBoss#canUsePortal)");
    auto* partActor = ge::partActor(handle);
    ctx.require(partActor != nullptr, "the boss has its part");
    auto* partMob = fe::asMob(*partActor);
    ctx.check(partMob && !partMob->canChangeDimensionsUsingPortal(), "nor does its part (HydraPart, NagaSegment)");
    bool engine = pig->canChangeDimensionsUsingPortal();
    ctx.info(std::string("the engine lets a pig use portals: ") + (engine ? "yes" : "no"));
    ge::ext::setCanUsePortal("minecraft:pig", false);
    ctx.check(!pig->canChangeDimensionsUsingPortal(), "a type marked with setCanUsePortal(false) does not (RisingZombie)");
    ge::ext::setCanUsePortal("minecraft:pig", true);
    ctx.check(pig->canChangeDimensionsUsingPortal() == engine, "and gets the engine's answer back");
    pig->remove(); // before it wanders into the portal
    // a type that is no registered boss any more is the engine's again
    gf::feature::boss::unregisterBossForTest("gf_test:boss");
    bool unregistered = boss->canChangeDimensionsUsingPortal();
    gf::boss::registerBoss(fe::tests::fixtureBossSpec());
    ctx.check(!boss->canChangeDimensionsUsingPortal(), "registered again, it refuses again");
    ctx.info(std::string("an unregistered gf_test:boss may use portals: ") + (unregistered ? "yes" : "no"));
    // a non-mob actor (a projectile answers with Actor's own canChangeDimensionsUsingPortal)
    auto* ball = spawnActor(at.offset(0, 3, 2), "minecraft:snowball");
    ctx.require(ball != nullptr, "a snowball spawns");
    ballId          = fe::uidOf(*ball);
    bool ballEngine = ball->canChangeDimensionsUsingPortal();
    ge::ext::setCanUsePortal("minecraft:snowball", false);
    ctx.check(!ball->canChangeDimensionsUsingPortal(), "a marked projectile type refuses portals too (ChainBlock, CubeOfAnnihilation)");
    ge::ext::setCanUsePortal("minecraft:snowball", true);
    ctx.check(ball->canChangeDimensionsUsingPortal() == ballEngine, "and gets the engine's answer back");
    ball->remove();
    ballId = 0;
    // F4's Gloaming portal: a boss standing in the lit portal (getPortalTransitionTime 0 for
    // entities: one that may travel leaves on the next ticks) is asked every tick and stays
    auto* standing = spawnMob(pool, "gf_test:boss", 1.0f, 1.0f);
    ctx.require(standing != nullptr, "a fixture boss spawns in the portal");
    inPortalId = fe::uidOf(*standing);
    fe::watchPortalQueriesForTest(inPortalId);
    co_await ctx.ticks(40);
    int asked = fe::portalQueriesForTest();
    fe::watchPortalQueriesForTest(0);
    ctx.check(asked >= 20, "the Gloaming portal asked the boss standing in it (" + std::to_string(asked) + " times in 40 ticks)");
    auto* stayed = mobById(inPortalId);
    ctx.require(stayed != nullptr, "the boss is still here");
    auto sp = stayed->getFeetPos();
    ctx.check(static_cast<int>(stayed->getDimensionId()) == pool.dim && std::abs(sp.x - (pool.x + 1.0f)) < 1.0f && std::abs(sp.z - (pool.z + 1.0f)) < 1.0f,
              "in the portal, in its own dimension (" + std::to_string(sp.x) + " " + std::to_string(sp.y) + " " + std::to_string(sp.z) + ")");
    int lit = 0;
    for (int dx = 0; dx < 2; ++dx)
        for (int dz = 0; dz < 2; ++dz) lit += ctx.blockAt(pool.offset(dx, 0, dz)) == "gloamingforest:gloaming_portal" ? 1 : 0;
    ctx.check(lit == 4, "and the portal is still lit (" + std::to_string(lit) + " portal blocks)");
    (void)box;
});

// upstream's bosses leave their loot in one of upstream's chests (getDeathContainer: canopy, gloaming oak,
// mangrove, dark chest; the block families' chest blocks, containers of the inventory contract):
// the chest takes the loot buffer through gf::inventory.
GF_REGISTER_SELFTEST("entity.boss_tf_chest", [](Context& ctx) -> ll::coro::CoroTask<> {
    FixtureScope    fixture;
    DifficultyScope easy(Difficulty::Easy);
    struct LifecycleScope {
        ~LifecycleScope() { clearLifecycleForTest("gf_test:boss"); }
    } lifecycleScope;
    gf::boss::ext::Lifecycle lc;
    lc.deathContainer = [](Mob&, core::jrand::RandomSource&) { return std::string("gloamingforest:canopy_chest"); };
    gf::boss::ext::setLifecycle("gf_test:boss", lc);
    resetTestRecords();
    auto  box  = co_await ctx.flatArea(5846, 4);
    auto  home = ctx.testPos(5847, 2);
    auto* boss = spawnMob(home, "gf_test:boss");
    ctx.require(boss != nullptr, "the fixture boss spawns");
    auto uid = fe::uidOf(*boss);
    ge::setHome(*boss, home, 20);
    co_await ctx.ticks(2);
    if ((boss = mobById(uid))) boss->kill();
    for (int i = 0; i < 60 && !lastDeathChest(); ++i) co_await ctx.ticks(1);
    auto chest = lastDeathChest();
    ctx.require(chest.has_value(), "a death chest was placed");
    ctx.check(*chest == home.offset(0, -1, 0), "under the home point");
    ctx.check(ctx.blockAt(*chest) == "gloamingforest:canopy_chest", "of the type's death container (" + ctx.blockAt(*chest) + ")");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "the test dimension");
    auto states = game::statesOf(region->getBlock(game::blockPos(*chest)));
    ctx.check(states.contains("gloamingforest:facing"), "with its facing (" + states["gloamingforest:facing"] + ")");
    // the canopy chest is a container of the inventory contract (the block families' chest)
    auto h = gf::inventory::find(*region, game::blockPos(*chest));
    ctx.require(h.has_value() && h->get && h->set && h->slots >= 27, "gloamingforest:canopy_chest is an inventory container (the families' chest)");
    // the fixture table rolls gf_test:fixture_item into the buffer (the fill may wait a tick for
    // the container)
    int inChest = 0;
    for (int i = 0; i < 4 && inChest == 0; ++i) {
        if ((h = gf::inventory::find(*region, game::blockPos(*chest))))
            for (int s = 0; s < h->slots; ++s)
                if (game::items::idOf(h->get(*chest, s)) == "gf_test:fixture_item") inChest += game::items::countOf(h->get(*chest, s));
        if (inChest == 0) co_await ctx.ticks(1);
    }
    ctx.check(inChest >= 1, "the upstream chest holds the loot (" + std::to_string(inChest) + " fixture items)");
    ctx.check(pendingFillsForTest() == 0, "nothing waits for a container");
    ctx.check(collectDrops(*chest, "gf_test:fixture_item", 3.0f) == 0, "and nothing dropped");
    if ((h = gf::inventory::find(*region, game::blockPos(*chest))))
        for (int s = 0; s < h->slots; ++s) h->set(*chest, s, game::items::empty());
    (void)box;
});

// A death container block that never becomes a container (here gf_test:fixture_block): the fill
// waits for its container while the chest's chunk is loaded, never while it is not, and is saved
// with the deaths; after its 20 loaded ticks the loot pops out where the block stands
// (IBossLootBuffer#depositDropsIntoChest's Block#popResource), so it is never lost.
GF_REGISTER_SELFTEST("entity.boss_chest_fallback", [](Context& ctx) -> ll::coro::CoroTask<> {
    FixtureScope    fixture;
    DifficultyScope easy(Difficulty::Easy);
    struct Scope {
        ~Scope() {
            clearLifecycleForTest("gf_test:boss");
            setUnloadedForTest(nullptr);
        }
    } scope;
    gf::boss::ext::Lifecycle lc;
    lc.deathContainer = [](Mob&, core::jrand::RandomSource&) { return std::string("gf_test:fixture_block"); };
    gf::boss::ext::setLifecycle("gf_test:boss", lc);
    resetTestRecords();
    auto  box  = co_await ctx.flatArea(5850, 4);
    auto  home = ctx.testPos(5851, 2);
    auto* boss = spawnMob(home, "gf_test:boss");
    ctx.require(boss != nullptr, "the fixture boss spawns");
    auto uid = fe::uidOf(*boss);
    ge::setHome(*boss, home, 20);
    co_await ctx.ticks(2);
    int fillsBefore = pendingFillsForTest();
    if ((boss = mobById(uid))) boss->kill();
    for (int i = 0; i < 60 && !lastDeathChest(); ++i) co_await ctx.ticks(1);
    auto chest = lastDeathChest();
    ctx.require(chest.has_value(), "the death container was placed");
    ctx.check(*chest == home.offset(0, -1, 0) && ctx.blockAt(*chest) == "gf_test:fixture_block", "under the home point (" + ctx.blockAt(*chest) + ")");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "the test dimension");
    ctx.require(!gf::inventory::find(*region, game::blockPos(*chest)).has_value(), "gf_test:fixture_block is no container");
    ctx.require(pendingFillsForTest() == fillsBefore + 1, "its fill waits for a container");
    // while the chunk is not loaded nothing counts down and nothing drops
    auto cx = chest->x >> 4, cz = chest->z >> 4, dim = chest->dim;
    setUnloadedForTest([cx, cz, dim](int d, BlockPos const& p) { return d == dim && (p.x >> 4) == cx && (p.z >> 4) == cz; });
    co_await ctx.ticks(30);
    ctx.check(pendingFillsForTest() == fillsBefore + 1, "the fill still waits after 30 ticks of an unloaded chunk (it has 20 loaded ones)");
    ctx.check(collectDrops(*chest, "gf_test:fixture_item", 3.0f) == 0, "and nothing dropped meanwhile");
    // it is saved with the deaths: a world closed now keeps it
    saveDeathsForTest();
    auto rec   = game::db::get("gf:entity:boss_deaths");
    auto fills = rec && rec->is_object() ? rec->value("fills", nlohmann::json::array()) : nlohmann::json::array();
    ctx.check(fills.is_array() && static_cast<int>(fills.size()) == fillsBefore + 1, "the world DB holds the waiting fill (" + fills.dump().substr(0, 120) + ")");
    forgetDeathsForTest();
    ctx.check(pendingFillsForTest() == 0, "forgotten in memory");
    loadDeathsForTest();
    ctx.check(pendingFillsForTest() == fillsBefore + 1, "and read back");
    // loaded again: after its 20 ticks the loot pops out at the block
    setUnloadedForTest(nullptr);
    int dropped = 0;
    for (int i = 0; i < 40 && dropped == 0; ++i) {
        co_await ctx.ticks(1);
        if (pendingFillsForTest() == fillsBefore) dropped += collectDrops(*chest, "gf_test:fixture_item", 3.0f);
    }
    ctx.check(pendingFillsForTest() == fillsBefore, "the fill gave up on the container");
    ctx.check(dropped >= 1, "the loot popped out at the block (" + std::to_string(dropped) + " fixture items)");
    saveDeathsForTest();
    (void)box;
});

// EntityEvents for a type of the inclusive tag that is no registered boss (a data-added member;
// here a pig marked inclusive): the player who hurts it qualifies (addQualifiedGroupPlayerIfNeeded),
// its death fires "hurt_boss" for that player (grantGroupAdvancementIfNeeded: any mob with the
// MULTIPLAYER_FIGHT attachment), and the qualified record goes with the mob; it gets no boss bar.
GF_REGISTER_SELFTEST("entity.boss_inclusive", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    struct InclusiveScope {
        InclusiveScope() { markInclusiveForTest("minecraft:pig"); }
        ~InclusiveScope() { unmarkInclusiveForTest("minecraft:pig"); }
    } inclusive;
    auto  box  = co_await ctx.flatArea(5858, 5);
    auto  at   = ctx.testPos(5859, 2);
    auto& sp   = ctx.spawnPlayer("gf_inclusive");
    auto  spId = sp.getOrCreateUniqueID().rawID;
    sp.teleport(Vec3{at.x + 0.5f, float(at.y), at.z + 0.5f}, DimensionType(at.dim));
    co_await ctx.ticks(2);
    auto* pig = spawnMob(at.offset(1, 0, 0), "minecraft:pig");
    ctx.require(pig != nullptr, "a pig spawns");
    auto pigId = fe::uidOf(*pig);
    struct Remove {
        std::int64_t uid;
        ~Remove() {
            if (auto* a = fe::actorById(uid)) a->remove();
        }
    } remove{pigId};
    co_await ctx.ticks(2);
    if (auto* p = fe::playerById(spId); p && mobById(pigId)) static_cast<SimulatedPlayer*>(p)->simulateAttack(mobById(pigId));
    co_await ctx.ticks(2);
    pig = mobById(pigId);
    ctx.require(pig != nullptr && pig->isAlive(), "the pig survives the hit");
    auto part = gf::boss::participants(*pig);
    ctx.check(part.size() == 1 && fe::uidOf(*part[0]) == spId, "the player who hurt it qualified (" + std::to_string(part.size()) + " participants)");
    ctx.check(!hasBar(pigId), "no boss bar for a mob that is no boss");
    TriggerRecorder triggers;
    pig->kill();
    co_await ctx.ticks(2);
    int hurt = triggers.find("gloamingforest:hurt_boss", spId);
    ctx.check(hurt >= 0 && triggers.data(hurt).value("entity", std::string()) == "minecraft:pig", "hurt_boss fired for the qualified player (" + triggers.data(hurt).dump() + ")");
    ctx.check(triggers.count("gloamingforest:hurt_boss", spId) == 1, "once");
    // the attachment goes with the mob: the record is dropped once the body is gone
    for (int i = 0; i < 60 && fe::actorById(pigId); ++i) co_await ctx.ticks(1);
    ctx.check(fe::actorById(pigId) == nullptr, "the dead pig is removed");
    for (int i = 0; i < 25 && qualifiedForTest(pigId); ++i) co_await ctx.ticks(1);
    ctx.check(!qualifiedForTest(pigId), "and its qualified players with it");
    // the pig's drops and experience do not stay behind
    if (auto* region = game::regionOf(at.dim))
        for (auto* a : region->getEntities(AABB{Vec3{at.x - 6.0f, at.y - 3.0f, at.z - 6.0f}, Vec3{at.x + 7.0f, at.y + 4.0f, at.z + 7.0f}}, 0.0f))
            if (a && (a->isType(ActorType::ItemEntity) || a->isType(ActorType::Experience))) a->remove();
    (void)box;
});
GF_REGISTER_SELFTEST("entity.boss_peaceful", [](Context& ctx) -> ll::coro::CoroTask<> {
    FixtureScope    fixture;
    DifficultyScope scope(Difficulty::Easy);
    // the fixture spawner exists only while this test runs (other tests place fixture blocks)
    SpawnerScope spawner;
    // EnforcedHomePoint#getHomeRadius of the type: the spawner gives it to the boss it spawns
    struct RadiusScope {
        RadiusScope() { gf::boss::ext::setHomeRadius("gf_test:boss", 20); }
        ~RadiusScope() { gf::boss::ext::setHomeRadius("gf_test:boss", 0); }
    } radius;
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
        ctx.check(h && h->radius == 20, "with the type's home radius (" + (h ? std::to_string(h->radius) : std::string("none")) + ")");
        ctx.check(ge::data(*b, "boss:spawner") == "gf_test:fixture_block", "the boss knows its spawner block");
        b->remove();
    }
    (void)box;
});

// BaseTFBoss's overridable lifecycle (gf::boss::ext::Lifecycle), with the overrides upstream's bosses use:
// a shadow clone (Lich#shouldSpawnLoot / #shouldCreateSpawner false) leaves no chest, no conquest
// and no spawner; a longer death animation (Naga, Lich, Ur-Ghast, Hydra) keeps the dead body past
// Bedrock's 20 ticks with tickDeath running, then the death container of the type takes the loot
// and onPostRemoval runs; the Knight Phantoms' postmortem gives a knight's loot to another one and
// only the last knight deposits the chest; placeSpawner replaces the peaceful spawner placement.
namespace {
struct HookProbe {
    int          tickDeaths = 0, maxDeathTime = 0, postRemovals = 0, spawnersPlaced = 0;
    std::int64_t otherKnight = 0;
};
HookProbe& hookProbe() {
    static HookProbe p;
    return p;
}
bool flagged(Mob& m, char const* key) { return ge::data(m, key).is_boolean() && ge::data(m, key).get<bool>(); }

/// Empties the death chest at `chest` and puts the floor block back.
void restoreFloor(core::GlobalPos const& chest, std::string const& floor) {
    if (auto* c = chestAt(chest))
        for (int i = 0; i < c->getContainerSize(); ++i) c->setItem(i, game::items::empty());
    if (auto* region = game::regionOf(chest.dim))
        if (auto const* b = game::blockWith(floor.empty() ? "minecraft:grass_block" : floor)) game::setBlock(*region, game::blockPos(chest), *b);
}
} // namespace

GF_REGISTER_SELFTEST("entity.boss_lifecycle_hooks", [](Context& ctx) -> ll::coro::CoroTask<> {
    FixtureScope    fixture;
    DifficultyScope scope(Difficulty::Easy);
    SpawnerScope    spawner; // the fixture boss's spawner block for the peaceful steps
    struct LifecycleScope {
        ~LifecycleScope() { clearLifecycleForTest("gf_test:boss"); }
    } lifecycleScope;
    hookProbe() = {};
    gf::boss::ext::Lifecycle lc;
    lc.shouldSpawnLoot     = [](Mob& m) { return !flagged(m, "gf_clone"); };
    lc.shouldCreateSpawner = [](Mob& m) { return !flagged(m, "gf_clone"); };
    lc.tickDeath           = [](Mob& m, int t) {
        if (!flagged(m, "gf_long")) return;
        hookProbe().tickDeaths++;
        hookProbe().maxDeathTime = std::max(hookProbe().maxDeathTime, t);
    };
    lc.deathAnimationFinished = [](Mob& m, int t) { return t >= (flagged(m, "gf_long") ? 60 : 20); };
    lc.deathContainer         = [](Mob& m, core::jrand::RandomSource&) { return std::string(flagged(m, "gf_long") ? "minecraft:trapped_chest" : "minecraft:chest"); };
    lc.onPostRemoval          = [](Mob&) { hookProbe().postRemovals++; };
    lc.placeSpawner           = [](Mob& m, BlockPos const& pos) {
        hookProbe().spawnersPlaced++;
        gf::boss::ext::placeBossSpawner(m, pos);
    };
    // KnightPhantom#postmortem: while another knight lives it gets the loot, the last one deposits
    lc.postmortem = [](Mob& m, std::string const& type, Actor* killer) {
        if (!flagged(m, "gf_knight")) return gf::boss::ext::defaultPostmortem(m, type, killer);
        auto* other = mobById(hookProbe().otherKnight);
        if (other && other != &m && other->isAlive()) {
            // two slots: the other knight's own roll may overwrite one of them (IBossLootBuffer#fill
            // spreads over all 27 slots, as in Java)
            auto item = game::items::make("gf_test:fixture_item", 2);
            if (item) gf::boss::ext::setLootBufferSlot(*other, 0, *item);
            if (item) gf::boss::ext::setLootBufferSlot(*other, 1, *item);
            return;
        }
        gf::boss::ext::defaultPostmortem(m, type, killer);
        ge::data(m, "gf_over") = true; // IT_IS_OVER
    };
    lc.shouldDeposit = [](Mob& m) { return !flagged(m, "gf_knight") || flagged(m, "gf_over"); };
    gf::boss::ext::setLifecycle("gf_test:boss", lc);
    resetTestRecords();
    auto box = co_await ctx.flatArea(5866, 12);
    // (1) a shadow clone dies: no loot, no conquest, no chest
    auto homeA = ctx.testPos(5867, 1);
    auto floor = ctx.blockAt(homeA.offset(0, -1, 0));
    auto* a     = spawnMob(homeA, "gf_test:boss");
    ctx.require(a != nullptr, "the clone spawns");
    auto aId = fe::uidOf(*a);
    ge::setHome(*a, homeA, 20);
    ge::data(*a, "gf_clone") = true;
    co_await ctx.ticks(2);
    if ((a = mobById(aId))) a->kill();
    co_await ctx.ticks(32);
    ctx.check(!lastDeathChest().has_value(), "a clone's death places no chest (shouldSpawnLoot false)");
    ctx.check(!lastConquest().has_value(), "and conquers nothing");
    ctx.check(ctx.blockAt(homeA.offset(0, -1, 0)) == floor, "the floor under its home is untouched");
    ctx.check(mobById(aId) == nullptr, "the clone is gone");
    // (2) peaceful: a clone puts no spawner back, the real boss does (through placeSpawner)
    auto homeC = ctx.testPos(5869, 3);
    auto homeD = ctx.testPos(5871, 3);
    ctx.cleanupBlock(homeC);
    ctx.cleanupBlock(homeD);
    auto* c = spawnMob(homeC.offset(1, 0, 0), "gf_test:boss");
    auto* d = spawnMob(homeD.offset(1, 0, 0), "gf_test:boss");
    ctx.require(c && d, "the peaceful probes spawn");
    auto cId = fe::uidOf(*c), dId = fe::uidOf(*d);
    ge::setHome(*c, homeC, 20);
    ge::setHome(*d, homeD, 20);
    ge::data(*c, "gf_clone") = true;
    co_await ctx.ticks(2);
    scope.set(Difficulty::Peaceful);
    co_await ctx.ticks(3);
    ctx.check(ctx.blockAt(homeC) != "gf_test:fixture_block", "a clone leaves no spawner on peaceful (shouldCreateSpawner false)");
    ctx.check(ctx.blockAt(homeD) == "gf_test:fixture_block" && hookProbe().spawnersPlaced == 1, "the boss turns back into its spawner through placeSpawner");
    ctx.check(!mobById(cId) && !mobById(dId), "both are gone");
    scope.set(Difficulty::Easy);
    ctx.require(ctx.setBlock(homeD, "minecraft:air"), "clear the placed spawner");
    // (3) a long death animation: the body stays past 20 ticks, then the type's container
    resetTestRecords();
    auto homeB = ctx.testPos(5872, 1);
    auto floorB = ctx.blockAt(homeB.offset(0, -1, 0));
    auto* b     = spawnMob(homeB, "gf_test:boss");
    ctx.require(b != nullptr, "the long-dying boss spawns");
    auto bId = fe::uidOf(*b);
    ge::setHome(*b, homeB, 20);
    ge::data(*b, "gf_long") = true;
    co_await ctx.ticks(2);
    int held = heldDeathTicksForTest();
    if ((b = mobById(bId))) b->kill();
    co_await ctx.ticks(40);
    b = mobById(bId);
    ctx.check(b != nullptr, "the dead body is still there after 40 ticks (Bedrock removes it after 20)");
    if (b) ctx.check(gf::boss::ext::deathTime(*b) >= 35 && !b->isAlive(), "dying, deathTime " + std::to_string(gf::boss::ext::deathTime(*b)));
    ctx.check(heldDeathTicksForTest() > held, "the engine's death ticks were held (" + std::to_string(heldDeathTicksForTest() - held) + ")");
    ctx.check(!lastDeathChest().has_value(), "no chest before the animation finished");
    for (int i = 0; i < 40 && mobById(bId); ++i) co_await ctx.ticks(1);
    ctx.check(mobById(bId) == nullptr, "the body is removed once the animation finished");
    ctx.check(hookProbe().tickDeaths >= 59 && hookProbe().maxDeathTime >= 60, "tickDeath ran every dying tick (" + std::to_string(hookProbe().tickDeaths) + ", up to " + std::to_string(hookProbe().maxDeathTime) + ")");
    auto chestB = lastDeathChest();
    ctx.check(chestB && chestB->x == homeB.x && chestB->y == homeB.y - 1 && chestB->z == homeB.z, "the death chest is at the home point");
    if (chestB) ctx.check(ctx.blockAt(*chestB) == "minecraft:trapped_chest", "of the type's deathContainer (" + ctx.blockAt(*chestB) + ")");
    ctx.check(hookProbe().postRemovals >= 1, "onPostRemoval ran");
    if (chestB) restoreFloor(*chestB, floorB);
    // (4) the Knight Phantoms: the first knight's loot goes to the other, only the last deposits
    resetTestRecords();
    auto homeK = ctx.testPos(5875, 1);
    auto floorK = ctx.blockAt(homeK.offset(0, -1, 0));
    auto* k1    = spawnMob(homeK.offset(-2, 0, 2), "gf_test:boss");
    auto* k2    = spawnMob(homeK.offset(2, 0, 2), "gf_test:boss");
    ctx.require(k1 && k2, "two knights spawn");
    auto k1Id = fe::uidOf(*k1), k2Id = fe::uidOf(*k2);
    for (auto* k : {k1, k2}) {
        ge::setHome(*k, homeK, 20);
        ge::data(*k, "gf_knight") = true;
    }
    hookProbe().otherKnight = k2Id;
    co_await ctx.ticks(2);
    if ((k1 = mobById(k1Id))) k1->kill();
    co_await ctx.ticks(30);
    ctx.check(!lastDeathChest().has_value(), "a knight that is not the last leaves no chest (shouldDeposit)");
    ctx.check(!lastConquest().has_value(), "and conquers nothing yet");
    k2 = mobById(k2Id);
    ctx.require(k2 != nullptr, "the other knight lives");
    ctx.check(game::items::idOf(gf::boss::ext::lootBuffer(*k2)[0]) == "gf_test:fixture_item" && game::items::idOf(gf::boss::ext::lootBuffer(*k2)[1]) == "gf_test:fixture_item", "it got the dead knight's loot");
    k2->kill();
    co_await ctx.ticks(30);
    auto chestK = lastDeathChest();
    ctx.require(chestK.has_value(), "the last knight deposits the chest");
    ctx.check(lastConquest().has_value(), "and the stronghold is conquered");
    if (auto* container = chestAt(*chestK)) ctx.check(countOf(*container, "gf_test:fixture_item") >= 3, "the chest holds the shared loot (" + std::to_string(countOf(*container, "gf_test:fixture_item")) + " fixture items)");
    restoreFloor(*chestK, floorK);
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
    for (auto const* id : {"gloamingforest:ur_ghast_boss_spawner", "gloamingforest:snow_queen_boss_spawner", "gloamingforest:alpha_yeti_boss_spawner"}) {
        auto rule = spawnerRuleForTest(id);
        ctx.require(rule.has_value(), std::string(id) + " is a spawner");
        ctx.check(!heightRuleAllows(rule->kind, 100, 96.0) && heightRuleAllows(rule->kind, 100, 96.5), std::string(id) + ": the nearest player must stand above y - 4");
    }
    auto mino = spawnerRuleForTest("gloamingforest:minoshroom_boss_spawner");
    ctx.check(mino && heightRuleAllows(mino->kind, 100, 103.5) && !heightRuleAllows(mino->kind, 100, 104.0), "minoshroom: below y + 4");
    auto knights = spawnerRuleForTest("gloamingforest:knight_phantom_boss_spawner");
    ctx.check(knights && !heightRuleAllows(knights->kind, 100, 98.0) && heightRuleAllows(knights->kind, 100, 98.5), "knight phantoms: above y - 2");
    auto hydra = spawnerRuleForTest("gloamingforest:hydra_boss_spawner");
    ctx.check(hydra && heightRuleAllows(hydra->kind, 100, 0.0), "hydra: no height rule");
    // in the world: the player here wakes an ur-ghast spawner 3 above it (range 9)
    int      fy = static_cast<int>(std::floor(p->getFeetPos().y + 0.001f));
    BlockPos low{home.x, fy + 3, home.z};
    ctx.check(anyPlayerInRangeForTest(home.dim, low, "gloamingforest:ur_ghast_boss_spawner"), "a player 3 below wakes the ur-ghast spawner");
    ctx.check(!anyPlayerInRangeForTest(home.dim, BlockPos{home.x + 40, fy, home.z}, "gloamingforest:ur_ghast_boss_spawner"), "nobody within 9 blocks: it waits");
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

namespace {
/// The fixture boss with a death animation of `ticks` while the scope lives (back to the fixture's
/// 20 after; declare it after the FixtureScope).
struct DeathTicksScope {
    explicit DeathTicksScope(int ticks) { reg(ticks); }
    ~DeathTicksScope() { reg(20); }
    static void reg(int ticks) {
        gf::boss::registerBoss({
            .entityId        = "gf_test:boss",
            .homeStructure   = "gf_test:fixture",
            .lootTable       = "gf_test:entities/boss",
            .deathChestBlock = "minecraft:chest",
            .bar             = {.rgb = kFixtureRgb, .darken = true, .fog = false},
            .deathTicks      = ticks,
        });
    }
};

bool hasDeathTime(std::vector<core::Envelope> const& msgs, std::int64_t uid) {
    for (auto const& m : msgs)
        if (m.channel == "boss.death_time" && m.payload.value("id", std::int64_t{0}) == uid) return true;
    return false;
}

/// The positions that count as unloaded chunks in a test (dying bodies, death chests).
struct UnloadedScope {
    std::vector<BlockPos> positions;
    UnloadedScope() {
        setUnloadedForTest([this](int, BlockPos const& pos) { return std::find(positions.begin(), positions.end(), pos) != positions.end(); });
    }
    ~UnloadedScope() { setUnloadedForTest(nullptr); }
};
} // namespace

// upstream saves a dying boss with its chunk (dyingInventory, deathTime) and finishes BaseTFBoss#tickDeath
// when the chunk loads again, so its death chest always appears. Here a boss dies with a long death
// animation: a companion that comes into range of the dying boss gets its death time although the
// bar is hidden (KnightPhantom hides knights 1-5); the body leaves the level and its chunk counts
// as unloaded: the death waits (no death time, no chest); the record survives the world DB (save,
// forget, load); the chunk loads without the body: the death goes on without it; the chest's chunk
// is still unloaded: the chest waits, then appears with the loot buffer when it loads. A boss
// without a home leaves its chest where its body is at the end (EntityUtil#bossChestLocation).
GF_REGISTER_SELFTEST("entity.boss_unload", [](Context& ctx) -> ll::coro::CoroTask<> {
    FixtureScope    fixture;
    DeathTicksScope longDeath(200);
    DifficultyScope easy(Difficulty::Easy);
    UnloadedScope   unloaded;
    resetTestRecords();
    auto box   = co_await ctx.flatArea(5896, 4);
    auto home  = ctx.testPos(5897, 1);
    auto floor = ctx.blockAt(home.offset(0, -1, 0));
    auto* boss = spawnMob(home.offset(2, 0, 0), "gf_test:boss");
    ctx.require(boss != nullptr, "the fixture boss spawns");
    auto uid = fe::uidOf(*boss);
    ge::setHome(*boss, home, 20);
    // a companion far away; the bar hidden (KnightPhantom#startSeenByPlayer: knights 1-5)
    auto& far   = ctx.spawnPlayer("gf_unload_far");
    auto  farId = far.getOrCreateUniqueID().rawID;
    far.teleport(Vec3{home.x + 200.5f, float(home.y), home.z + 0.5f}, DimensionType(home.dim));
    struct CompanionScope {
        std::int64_t id;
        ~CompanionScope() {
            if (auto* p = fe::playerById(id)) gf::net::setTestCompanion(*p, false);
        }
    } companion{farId};
    co_await ctx.ticks(3);
    if (auto* p = fe::playerById(farId)) gf::net::setTestCompanion(*p, true);
    if ((boss = mobById(uid))) gf::boss::bar(*boss).setVisible(false);
    if ((boss = mobById(uid))) boss->kill();
    co_await ctx.ticks(3);
    ctx.require(dyingForTest(uid).has_value(), "the boss is dying");
    // the buffer as the dying body holds it goes into the chest (3 diamonds: not in the loot table)
    auto item = game::items::make("minecraft:diamond", 3);
    ctx.require(item.has_value(), "diamonds available");
    if ((boss = mobById(uid))) gf::boss::ext::setLootBufferSlot(*boss, 4, *item);
    auto* p = fe::playerById(farId);
    ctx.require(p != nullptr, "the far companion is there");
    (void)gf::net::takeTestMessages(*p);
    p->teleport(Vec3{home.x + 0.5f, float(home.y), home.z + 3.5f}, DimensionType(home.dim));
    bool got = false;
    for (int i = 0; i < 15 && !got; ++i) {
        co_await ctx.ticks(1);
        if ((p = fe::playerById(farId))) got = hasDeathTime(gf::net::takeTestMessages(*p), uid);
    }
    ctx.check(got, "a companion that starts seeing a dying boss with a hidden bar gets its death time (UpdateDeathTimePacket)");
    // the body's chunk unloads with the body: the death waits
    auto* body = mobById(uid);
    ctx.require(body != nullptr, "the dead body is there");
    auto bodyAt  = game::blockPosOf(body->getFeetPos());
    auto chestPos = BlockPos{home.x, home.y - 1, home.z};
    unloaded.positions = {bodyAt, chestPos};
    body->remove();
    co_await ctx.ticks(2);
    auto paused = dyingForTest(uid);
    ctx.require(paused.has_value(), "the record stays while the body's chunk is unloaded");
    co_await ctx.ticks(30);
    auto still = dyingForTest(uid);
    ctx.check(still && still->deathTime == paused->deathTime, "no death time passes while the chunk is unloaded (" + std::to_string(still ? still->deathTime : -1) + ")");
    ctx.check(!lastDeathChest().has_value() && ctx.blockAt(home.offset(0, -1, 0)) == floor, "no chest, and no loot popped into the unloaded region");
    // the world save and load (onWorldSave / onWorldReady)
    saveDeathsForTest();
    forgetDeathsForTest();
    ctx.check(!dyingForTest(uid).has_value(), "forgotten in memory");
    loadDeathsForTest();
    auto loaded = dyingForTest(uid);
    ctx.require(loaded.has_value(), "the dying boss came back from the world DB");
    ctx.check(loaded->deathTime == paused->deathTime && loaded->finish == 200 && loaded->bufferStacks >= 1, "with its death time, animation length and loot buffer (" + std::to_string(loaded->bufferStacks) + " stacks)");
    // the body's chunk loads without the body: after the grace period the death goes on
    unloaded.positions = {chestPos};
    for (int i = 0; i < 60; ++i) co_await ctx.ticks(1);
    auto going = dyingForTest(uid);
    ctx.require(going.has_value(), "the death goes on");
    ctx.check(going->bodiless && going->deathTime > paused->deathTime, "without its body (death time " + std::to_string(going->deathTime) + ")");
    // the animation ends while the chest's chunk is not loaded: the chest waits
    setDeathFinishForTest(uid, going->deathTime + 2);
    co_await ctx.ticks(5);
    ctx.check(!dyingForTest(uid).has_value(), "the death finished");
    ctx.check(pendingDepositsForTest() == 1, "the death chest waits for its chunk");
    ctx.check(!lastDeathChest().has_value() && ctx.blockAt(home.offset(0, -1, 0)) == floor, "nothing placed in the unloaded chunk");
    saveDeathsForTest(); // the waiting chest goes through the world DB too
    forgetDeathsForTest();
    loadDeathsForTest();
    ctx.check(pendingDepositsForTest() == 1, "the waiting chest came back from the world DB");
    // its chunk loads: the chest appears with the buffer
    unloaded.positions.clear();
    co_await ctx.ticks(3);
    ctx.check(pendingDepositsForTest() == 0, "the waiting chest was placed");
    auto chest = lastDeathChest();
    ctx.require(chest.has_value(), "a death chest was placed");
    ctx.check(*chest == home.offset(0, -1, 0), "at home.below()");
    if (auto* c = chestAt(*chest)) ctx.check(countOf(*c, "minecraft:diamond") == 3, "holding the loot buffer the dying body held (" + std::to_string(countOf(*c, "minecraft:diamond")) + " diamonds)");
    restoreFloor(*chest, floor);
    // without a home: the chest goes where the body is at the end of its animation
    resetTestRecords();
    auto* b = spawnMob(home.offset(0, 0, 2), "gf_test:boss");
    ctx.require(b != nullptr, "a boss without a home spawns");
    auto bId = fe::uidOf(*b);
    co_await ctx.ticks(2);
    if ((b = mobById(bId))) b->kill();
    co_await ctx.ticks(3);
    auto moved = home.offset(2, 0, 2);
    if ((b = mobById(bId))) b->moveTo(Vec3{moved.x + 0.5f, float(moved.y), moved.z + 0.5f}, b->getRotation());
    co_await ctx.ticks(3);
    if (auto d = dyingForTest(bId)) setDeathFinishForTest(bId, d->deathTime + 2);
    co_await ctx.ticks(6);
    auto chestB = lastDeathChest();
    ctx.require(chestB.has_value(), "the homeless boss left a death chest");
    ctx.check(chestB->x == moved.x && chestB->z == moved.z, "where its body was at the end (column " + std::to_string(chestB->x) + " " + std::to_string(chestB->z) + ", moved to " + std::to_string(moved.x) + " " + std::to_string(moved.z) + ")");
    if (auto* c = chestAt(*chestB))
        for (int i = 0; i < c->getContainerSize(); ++i) c->setItem(i, game::items::empty());
    ctx.setBlock(*chestB, "minecraft:air");
    (void)box;
});

// The world save and load for real (server scenario: write, Restart-Server, read): a fixture boss
// dies with a very long animation; after the restart its death is resumed from the world DB and,
// given its end, places the death chest with its loot under its home. entity.boss_dying_write
// leaves a probe record (gf:entity:dying_probe); entity.boss_dying_read (after the restart) collects
// it. Without a probe (a plain "entity.*" run sorts the read first) the read has nothing to check;
// a probe no read collects is dropped after two minutes.
namespace {
constexpr char const* kDyingProbeKey  = "gf:entity:dying_probe";
constexpr char const* kDyingProbeType = "gf_test:boss_part";
constexpr int         kDyingProbeTtl = 2400;

void dropProbe(nlohmann::json const& rec) {
    auto uid = rec.value("uid", std::int64_t{0});
    dropDyingForTest(uid);
    if (auto* a = fe::actorById(uid)) a->remove();
}

struct ProbeExpiry {
    std::int64_t  uid   = 0;
    long long     nonce = -1;
    std::uint64_t until = 0;
};
/// The probe a read has not collected yet (uid 0: none), dropped after kDyingProbeTtl ticks.
ProbeExpiry& probeExpiry() {
    static ProbeExpiry e;
    static bool        once = false;
    if (!std::exchange(once, true))
        fe::onLevelTick([] {
            auto& p = e;
            if (!p.uid || fe::currentTick() < p.until) return;
            if (auto r = game::db::get(kDyingProbeKey); r && r->value("nonce", -1LL) == p.nonce) {
                dropProbe(*r);
                game::db::erase(kDyingProbeKey);
            } else if (dyingForTest(p.uid)) {
                dropDyingForTest(p.uid);
            }
            p = {};
        });
    return e;
}
} // namespace

GF_REGISTER_SELFTEST("entity.boss_dying_write", [](Context& ctx) -> ll::coro::CoroTask<> {
    // the probe is a boss of its own type (gf_test:boss_part registered as a boss while this test
    // runs), so the lifecycle overrides other tests give gf_test:boss do not end its death
    struct ProbeBossScope {
        ProbeBossScope() {
            gf::boss::registerBoss({
                .entityId        = kDyingProbeType,
                .homeStructure   = "gf_test:fixture",
                .lootTable       = "gf_test:entities/boss",
                .deathChestBlock = "minecraft:chest",
                .bar             = {.rgb = kFixtureRgb},
                .deathTicks      = 1000000,
            });
        }
        ~ProbeBossScope() { unregisterBossForTest(kDyingProbeType); }
    } probeBoss;
    DifficultyScope easy(Difficulty::Easy);
    auto home = ctx.testPos(5898, 4);
    ctx.require(co_await ctx.ensureLoaded(home), "test area loaded");
    if (auto old = game::db::get(kDyingProbeKey)) dropProbe(*old); // one probe at a time
    auto* boss = spawnMob(home, kDyingProbeType);
    ctx.require(boss != nullptr, "the probe boss spawns");
    auto uid = fe::uidOf(*boss);
    ge::setHome(*boss, home, 20);
    co_await ctx.ticks(2);
    if ((boss = mobById(uid))) boss->kill();
    co_await ctx.ticks(5);
    ctx.require(dyingForTest(uid).has_value(), "the probe boss is dying");
    // 5 diamonds in the dying body's buffer (not in the loot table)
    auto item = game::items::make("minecraft:diamond", 5);
    ctx.require(item.has_value(), "diamonds available");
    if ((boss = mobById(uid))) gf::boss::ext::setLootBufferSlot(*boss, 7, *item);
    auto nonce = static_cast<long long>(fe::freshSeed() & 0x7fffffff);
    nlohmann::json rec{{"nonce", nonce}, {"uid", uid}, {"home", {home.dim, home.x, home.y, home.z}}, {"floor", ctx.blockAt(home.offset(0, -1, 0))}};
    game::db::put(kDyingProbeKey, rec);
    saveDeathsForTest();
    // dropped if no read collects it (the 'full' run): one level tick listener for all probes
    probeExpiry() = {uid, nonce, fe::currentTick() + kDyingProbeTtl};
    ctx.info("dying boss written nonce=" + std::to_string(nonce));
});

GF_REGISTER_SELFTEST("entity.boss_dying_read", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto rec = game::db::get(kDyingProbeKey);
    if (!rec) {
        ctx.info("no dying probe (entity.boss_dying_write did not run before): nothing to check");
        co_return;
    }
    game::db::erase(kDyingProbeKey);
    auto uid   = rec->value("uid", std::int64_t{0});
    auto nonce = rec->value("nonce", -1LL);
    auto h     = rec->value("home", nlohmann::json::array());
    ctx.require(uid != 0 && h.is_array() && h.size() == 4, "the probe record is complete");
    core::GlobalPos home{h[0].get<int>(), h[1].get<int>(), h[2].get<int>(), h[3].get<int>()};
    auto            floor = rec->value("floor", std::string("minecraft:grass_block"));
    ctx.require(co_await ctx.ensureLoaded(home), "test area loaded");
    auto d = dyingForTest(uid);
    ctx.require(d.has_value(), "the dying boss came back from the world save");
    ctx.check(d->finish == 1000000 && d->bufferStacks >= 1, "with its animation length and loot buffer");
    ctx.info(std::string("the body ") + (fe::actorById(uid) ? "came back with its chunk" : "did not come back with its chunk"));
    resetTestRecords();
    setDeathFinishForTest(uid, d->deathTime + 5);
    for (int i = 0; i < 120 && !lastDeathChest(); ++i) co_await ctx.ticks(1);
    auto chest = lastDeathChest();
    ctx.require(chest.has_value(), "the resumed death placed its chest");
    ctx.check(*chest == home.offset(0, -1, 0), "under the home point");
    int items = 0;
    if (auto* c = chestAt(*chest)) items = countOf(*c, "minecraft:diamond");
    ctx.check(items == 5, "holding the loot buffer saved with the death (" + std::to_string(items) + " diamonds)");
    restoreFloor(*chest, floor);
    if (ctx.failures().empty()) ctx.info("dying boss resumed nonce=" + std::to_string(nonce));
});

GF_REGISTER_SELFTEST("entity.boss_tables", [](Context& ctx) {
    // BossVariant: 10 rows, floor and wall trophies, the comparator strengths of TFBlocks
    char const* variants[] = {"naga", "lich", "hydra", "ur_ghast", "knight_phantom", "snow_queen", "minoshroom", "alpha_yeti", "quest_ram", "final_boss"};
    int         rows       = 0;
    for (auto const* v : variants) rows += gf::boss::trophyVariant(v).has_value() ? 1 : 0;
    ctx.check(rows == 10, "10 BossVariant rows (" + std::to_string(rows) + ")");
    auto naga = gf::boss::trophyVariant("gloamingforest:naga_trophy");
    ctx.check(naga && naga->sound == "entity.gloamingforest.naga.rattle" && naga->volume == 1.25f && naga->pitch == 1.2f && naga->comparator == 5, "naga trophy: rattle 1.25 / 1.2, comparator 5");
    auto wall = gf::boss::trophyVariant("gloamingforest:snow_queen_wall_trophy");
    ctx.check(wall && wall->sound == "entity.gloamingforest.snow_queen.ambient" && wall->comparator == 0, "a wall trophy gives no comparator signal");
    auto ghast = gf::boss::trophyVariant("gloamingforest:ur_ghast_trophy");
    ctx.check(ghast && ghast->pitch == 0.6f && ghast->comparator == 13, "ur-ghast trophy: pitch 0.6, comparator 13");
    ctx.check(!gf::boss::trophyVariant("gloamingforest:oak_log").has_value(), "no variant for other blocks");
    auto roar = gf::boss::ext::trophyRareSound("gloamingforest:alpha_yeti_trophy");
    ctx.check(roar && roar->oneIn == 50 && roar->sound == "entity.gloamingforest.alpha_yeti.roar", "the alpha yeti trophy roars once in 50");
    // AbstractTrophyBlock#createParticle: counts and particle options
    auto ghastP = gf::boss::ext::trophyParticle("gloamingforest:ur_ghast_trophy");
    ctx.check(ghastP && ghastP->particle == "minecraft:dust" && ghastP->count == 10 && ghastP->params.value("color", nlohmann::json()) == nlohmann::json({1.0, 0.0, 0.0}) && ghastP->params.value("scale", 0.0) == 1.0, "ur-ghast trophy: 10 red dust (DustParticleOptions.REDSTONE)");
    auto knightP = gf::boss::ext::trophyParticle("gloamingforest:knight_phantom_wall_trophy");
    ctx.check(knightP && knightP->particle == "minecraft:item" && knightP->params.value("item", std::string()) == "gloamingforest:knightmetal_sword", "knight phantom trophy: knightmetal sword item particles");
    auto minoP = gf::boss::ext::trophyParticle("gloamingforest:minoshroom_trophy");
    ctx.check(minoP && minoP->particle == "minecraft:block" && minoP->params.value("block", std::string()) == "below", "minoshroom trophy: block particles of the block below");
    auto ramP = gf::boss::ext::trophyParticle("gloamingforest:quest_ram_trophy");
    ctx.check(ramP && ramP->params.value("color", std::string()) == "random", "quest ram trophy: random colours (ColorParticleOption)");
    auto lichP = gf::boss::ext::trophyParticle("gloamingforest:lich_trophy"), queenP = gf::boss::ext::trophyParticle("gloamingforest:snow_queen_trophy");
    ctx.check(lichP && lichP->count == 5 && queenP && queenP->count == 20, "5 angry lich particles, 20 snow warnings");
    ctx.check(!gf::boss::ext::trophyParticle("gloamingforest:hydra_trophy") && !gf::boss::ext::trophyParticle("gloamingforest:final_boss_trophy"), "no particles for the hydra and final boss trophies");
    if (auto* region = game::regionOf(ctx.testPos(5856).dim)) {
        auto     at = ctx.testPos(5856);
        BlockPos bp{at.x, at.y, at.z};
        auto     resolved = gf::boss::ext::trophyParticle("gloamingforest:minoshroom_wall_trophy", region, &bp);
        ctx.check(resolved && resolved->params.value("block", std::string()) == ctx.blockAt(at.offset(0, -1, 0)), "with a position: the block under the trophy (" + (resolved ? resolved->params.dump() : std::string()) + ")");
    }
    // the spawner block entities (BossSpawnerBlockEntity and its variants)
    struct Row {
        char const* block;
        char const* entity;
        int         range;
        char const* particle;
        SpawnerKind kind;
    };
    Row const spawners[] = {
        {"gloamingforest:naga_boss_spawner", "gloamingforest:naga", 50, "minecraft:crit", SpawnerKind::Default},
        {"gloamingforest:lich_boss_spawner", "gloamingforest:lich", 9, "gloamingforest:ominous_flame", SpawnerKind::Lich},
        {"gloamingforest:hydra_boss_spawner", "gloamingforest:hydra", 50, "minecraft:flame", SpawnerKind::Default},
        {"gloamingforest:ur_ghast_boss_spawner", "gloamingforest:ur_ghast", 9, "minecraft:dust", SpawnerKind::AboveMinus4},
        {"gloamingforest:knight_phantom_boss_spawner", "gloamingforest:knight_phantom", 9, "gloamingforest:ominous_flame", SpawnerKind::KnightPhantoms},
        {"gloamingforest:snow_queen_boss_spawner", "gloamingforest:snow_queen", 9, "gloamingforest:snow", SpawnerKind::AboveMinus4},
        {"gloamingforest:minoshroom_boss_spawner", "gloamingforest:minoshroom", 9, "minecraft:crimson_spore", SpawnerKind::BelowPlus4},
        {"gloamingforest:alpha_yeti_boss_spawner", "gloamingforest:alpha_yeti", 9, "minecraft:falling_water", SpawnerKind::AboveMinus4},
        {"gloamingforest:final_boss_boss_spawner", "gloamingforest:plateau_boss", 9, "gloamingforest:annihilate", SpawnerKind::Never},
    };
    for (auto const& r : spawners) {
        auto rule = spawnerRuleForTest(r.block);
        ctx.check(rule && rule->entityId == r.entity && rule->range == r.range && rule->particle == r.particle && rule->kind == r.kind, std::string(r.block) + ": " + r.entity + ", range " + std::to_string(r.range) + ", " + r.particle);
    }
    auto ghastRule = spawnerRuleForTest("gloamingforest:ur_ghast_boss_spawner");
    ctx.check(ghastRule && ghastRule->particleParams.is_object() && ghastRule->particleParams["color"] == nlohmann::json({1.0, 0.0, 0.0}), "ur-ghast spawner: DustParticleOptions.REDSTONE (red, scale 1)");
    // the 8-colour fallback
    ctx.check(gf::boss::ext::nearestBarColor(0xFF0000) == 2, "red -> RED");
    ctx.check(gf::boss::ext::nearestBarColor(0xFFFFFF) == 7, "white -> WHITE");
    ctx.check(gf::boss::ext::nearestBarColor(0x00B7EC) == 1, "blue -> BLUE");
    ctx.check(gf::boss::ext::nearestBarColor(0xEC00B8) == 0, "pink -> PINK");
    ctx.check(gf::boss::ext::nearestBarColor(0xE9EC00) == 4, "yellow -> YELLOW");
    ctx.check(gf::boss::ext::nearestBarColor(0x5E9916) == 3, "the naga's green -> GREEN");
    // the tags the kit reads come from the tag data (gf::tags): EntityTagGenerator
    // .MULTIPLAYER_INCLUSIVE_ENTITIES (the qualified players and the health adjuster) and
    // ItemTagGenerator.KOBOLD_PACIFICATION_BREADS (the kobold's goal)
    using gf::tags::Registry;
    auto inclusive = gf::tags::members(Registry::EntityType, "gloamingforest:multiplayer_inclusive_entities");
    std::vector<std::string> const tfInclusive{"gloamingforest:alpha_yeti", "gloamingforest:hydra",        "gloamingforest:lich",
                                               "gloamingforest:minoshroom", "gloamingforest:naga",         "gloamingforest:plateau_boss",
                                               "gloamingforest:snow_queen", "gloamingforest:ur_ghast"};
    ctx.check(inclusive == tfInclusive, "multiplayer_inclusive_entities holds upstream's 8 bosses (" + nlohmann::json(inclusive).dump() + ")");
    ctx.check(!gf::tags::has(Registry::EntityType, "gloamingforest:multiplayer_inclusive_entities", "gloamingforest:knight_phantom"), "the knight phantoms are not in it");
    ctx.check(gf::tags::has(Registry::Item, "gloamingforest:kobold_pacification_breads", "minecraft:bread"), "kobold_pacification_breads holds bread");
    // the death chest's facing: Direction.Plane.HORIZONTAL.getRandomDirection, one draw over north,
    // east, south, west, in the chest's own facing state (the vanilla chest's
    // minecraft:cardinal_direction; gloamingforest:facing on upstream's chests, the bosses' containers)
    static char const* const horizontal[] = {"north", "east", "south", "west"};
    std::set<std::string>    tfFacings;
    bool                     javaOrder = true;
    fe::JavaRandom seeds(42); // a death's seed is the boss random's nextLong (the first nextInt(4) of
                              // small consecutive seeds is the same)
    for (int n = 0; n < 32; ++n) {
        auto           seed = seeds.nextLong();
        fe::JavaRandom java(seed);
        auto           expected = horizontal[java.nextInt(4)];
        auto [tfState, tfValue] = deathChestFacingForTest("gloamingforest:canopy_chest", seed);
        auto [vState, vValue]   = deathChestFacingForTest("minecraft:chest", seed);
        javaOrder &= tfState == "gloamingforest:facing" && tfValue == expected && vState == "minecraft:cardinal_direction" && vValue == expected;
        tfFacings.insert(tfValue);
    }
    ctx.check(javaOrder, "the death chests face the direction Java draws (gloamingforest:facing on upstream's chests)");
    ctx.check(tfFacings.size() == 4, "an upstream death chest faces every horizontal direction (" + std::to_string(tfFacings.size()) + " seen)");
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
    // SpawnPlacements#checkSpawnRules (SPAWNER): a monster needs a dark spot and a difficulty, a
    // mob without placement rules spawns anywhere
    {
        auto probe = at.offset(-2, 1, 2);
        ctx.require(ctx.setBlock(at.offset(-1, 1, 2), "minecraft:glowstone"), "place a light next to the rule probe");
        co_await ctx.ticks(3); // the light spreads
        ctx.check(!spawnRulesAllowForTest(*region, game::blockPos(probe), true), "no monster next to a light (block light above the dimension's limit)");
        ctx.check(spawnRulesAllowForTest(*region, game::blockPos(probe), false), "a mob without spawn rules spawns there");
        ctx.require(ctx.setBlock(at.offset(-1, 1, 2), "minecraft:air"), "remove the light");
    }
    gf::boss::ext::SinisterSpawner cfg;
    // a pig: no monster light rule in the daylight test area (upstream's spawners spawn monsters in dark rooms)
    cfg.potentials = {{"minecraft:pig", 1}};
    cfg.spawnDelay = 40; // the lazy scan (one step a tick) finds its spots meanwhile
    cfg.particles  = {"minecraft:flame"};
    ctx.require(gf::boss::ext::setSinisterSpawner(*region, bp, cfg), "configure the spawner");
    ctx.check(gf::boss::ext::addSinisterParticle(*region, bp, "minecraft:smoke") && !gf::boss::ext::addSinisterParticle(*region, bp, "minecraft:smoke"), "addParticle adds once");
    ctx.check(gf::boss::ext::removeSinisterParticle(*region, bp, "minecraft:smoke") && !gf::boss::ext::removeSinisterParticle(*region, bp, "minecraft:smoke"), "removeParticle removes once");
    ctx.check(gf::boss::ext::setSinisterLootTable(*region, bp, "gf_test:chests/fixture_chest") && gf::boss::ext::sinisterLootTable(*region, bp) == "gf_test:chests/fixture_chest", "the loot table is kept");
    ctx.check(!gf::boss::ext::sinisterSpawner(*region, game::blockPos(at.offset(1, 0, 0))).has_value(), "another block has no sinister spawner");
    int before = sinisterSpawnedForTest();
    auto& sp = ctx.spawnPlayer();
    sp.teleport(Vec3{at.x + 3.5f, float(at.y), at.z + 0.5f}, DimensionType(at.dim));
    for (int i = 0; i < 300 && sinisterSpawnedForTest() == before; ++i) co_await ctx.ticks(1);
    ctx.check(sinisterSpawnedForTest() > before, "the spawner spawned (" + std::to_string(sinisterSpawnedForTest() - before) + ")");
    auto mobs = actorsAround(at, "minecraft:pig", 6.0f);
    ctx.check(!mobs.empty(), "its mobs stand around it");
    for (auto* m : mobs) {
        auto f = m->getFeetPos();
        ctx.check(std::abs(f.x - (at.x + 0.5f)) <= 4.6f && std::abs(f.z - (at.z + 0.5f)) <= 4.6f, "a mob within the spawn range");
    }
    if (auto s1 = gf::boss::ext::sinisterSpawner(*region, bp))
        ctx.check(s1->spawnDelay >= 190 && s1->spawnDelay < 800, "the delay reset to 200..800 after spawning (" + std::to_string(s1->spawnDelay) + ")");
    for (auto* m : actorsAround(at, "minecraft:pig", 8.0f)) m->remove();
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
    // the Naga's bar overlay (BossBarOverlay.NOTCHED_10): the client mod draws the notches
    gf::boss::ext::setBarStyle(*bar, kFixtureRgb, gf::boss::ext::Overlay::Notched10);
    // a synced string list (Hydra#HEAD_NAMES) for the client mod's readers
    ge::ext::setStringList(*bar, "gf_test:heads", {"left", "center", "right"});
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
    // a boss with a 100-tick death animation (the Naga's, the Lich's): its body stays in the level
    // past Bedrock's 20 ticks; no loot (a clone-like lifecycle: no death chest in the client world)
    {
        gf::boss::ext::Lifecycle lc;
        lc.shouldSpawnLoot        = [](Mob&) { return false; };
        lc.deathAnimationFinished = [](Mob&, int t) { return t >= 100; };
        gf::boss::ext::setLifecycle("gf_test:boss", lc);
        struct Clear {
            ~Clear() { clearLifecycleForTest("gf_test:boss"); }
        } clear;
        core::GlobalPos dyingAt = at.offset(1, 0, 1);
        if (real) {
            // between the two other bosses, 5 ahead of the player
            auto f = real->getFeetPos();
            auto v = real->getViewVector(1.0f);
            double len = std::sqrt(double(v.x) * v.x + double(v.z) * v.z);
            double fx = len > 1e-6 ? v.x / len : 0.0, fz = len > 1e-6 ? v.z / len : 1.0;
            dyingAt = core::GlobalPos{at.dim, static_cast<int>(std::floor(f.x + fx * 5.0)), static_cast<int>(std::floor(f.y)), static_cast<int>(std::floor(f.z + fz * 5.0))};
        }
        auto* dying = spawnMob(dyingAt, "gf_test:boss");
        ctx.require(dying != nullptr, "the dying boss spawns");
        dying->setNameTag("gf_dying");
        auto dyingId = fe::uidOf(*dying);
        co_await ctx.ticks(10);
        if ((dying = mobById(dyingId))) dying->kill();
        co_await ctx.ticks(40);
        ctx.check(mobById(dyingId) != nullptr, "the dying boss's body is still there 2 seconds after its death");
        ctx.info("dying boss ready");
        for (int i = 0; i < 80 && mobById(dyingId); ++i) co_await ctx.ticks(1);
        ctx.check(mobById(dyingId) == nullptr, "the body is gone once its animation finished");
    }
    scope.set(Difficulty::Peaceful);
    co_await ctx.ticks(5);
    ctx.check(!mobById(barId) && !mobById(partsId), "on peaceful both fixture bosses left");
    if (auto* m = mobById(barId)) m->remove();
    if (auto* m = mobById(partsId)) m->remove();
});

} // namespace gf::feature::boss::tests
