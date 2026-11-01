// Selftests of F8's "entity" contract (rule R12: prefix entity.*, dx 5800..5899) and the spike S4
// probes: behaviour dispatch, persisted data, entity properties read back through Molang, the
// goal selector, linked multipart parts (follow, damage routed to the parent, never saved) and the
// hostile mount. Fixtures: gf_test:boss and gf_test:boss_part (assets/test), vanilla mobs.

#include <cmath>

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/state/PropertyComponent.h"
#include "mc/world/actor/state/PropertyContainer.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/phys/AABB.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"

#include "feature/common/Interactions.h"
#include "feature/entity/Entity.h"
#include "feature/entity/EntityInternal.h"
#include "feature/ext/entity/EntityExt.h"
#include "game/Blocks.h"
#include "selftest/SelfTest.h"

namespace gf::feature::entity::tests {

using gf::selftest::Context;
namespace ge = gf::entity;

/// What the fixture behaviour of gf_test:boss saw.
struct Probe {
    int         spawns = 0, ticks = 0, aiSteps = 0, dies = 0, interacts = 0, loads = 0;
    std::string lastDieType;
};
Probe& probe() {
    static Probe p;
    return p;
}

namespace {

/// The fixture behaviour (gf_test:* ids only exist in development builds).
struct FixtureRegistration {
    FixtureRegistration() {
        ge::registerType(
            "gf_test:boss",
            {
                .onSpawn =
                    [](Actor& a) {
                        probe().spawns++;
                        // `/summon gf_test:boss gf_parts`: three linked parts (client e2e screenshots)
                        if (a.getNameTag() == "gf_parts")
                            if (auto* m = asMob(a)) {
                                ge::addPart(*m, "left", {0.8f, 0.8f}, [](Mob&) { return Vec3{2.0f, 0.5f, 0.0f}; });
                                ge::addPart(*m, "right", {0.8f, 0.8f}, [](Mob&) { return Vec3{-2.0f, 0.5f, 0.0f}; });
                                ge::addPart(*m, "head", {1.0f, 1.0f}, [](Mob&) { return Vec3{0.0f, 2.5f, 0.0f}; });
                            }
                    },
                .tick    = [](Actor&) { probe().ticks++; },
                .aiStep  = [](Actor&) { probe().aiSteps++; },
                .die =
                    [](Actor&, std::string const& type, Actor*) {
                        probe().dies++;
                        probe().lastDieType = type;
                    },
                .save = [](Actor&, ge::Json& out) { out["probe"] = "saved"; },
                .load = [](Actor&, ge::Json const& in) {
                    if (in.value("probe", "") == "saved") probe().loads++;
                },
                .interact =
                    [](Actor&, Player&) {
                        probe().interacts++;
                        return true;
                    },
            }
        );
    }
};
FixtureRegistration const fixtureRegistration;

using Difficulty = SharedTypes::Legacy::Difficulty;

/// Sets the world difficulty for a test and puts it back afterwards (the e2e world is peaceful).
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

Mob* spawnMob(core::GlobalPos const& at, std::string const& id, float dy = 0.0f) {
    auto* region = game::regionOf(at.dim);
    auto  level  = ll::service::getLevel();
    if (!region || !level) return nullptr;
    return level->getSpawner().spawnMob(*region, ActorDefinitionIdentifier(id), nullptr, Vec3{at.x + 0.5f, at.y + dy, at.z + 0.5f}, false, false, false);
}

Mob* mobById(std::int64_t uid) {
    auto* a = actorById(uid);
    return a ? asMob(*a) : nullptr;
}

void removeActor(std::int64_t uid) {
    if (auto* a = actorById(uid)) a->remove();
}

std::optional<std::variant<int, float, bool, std::string>> propertyOf(Actor& a, std::string const& name) {
    auto props = a.getEntityContext().tryGetComponent<PropertyComponent>();
    if (!props) return std::nullopt;
    return props->mPropertyContainer->getValueVariant(HashedString::computeHash(name));
}

double dist(Vec3 const& a, Vec3 const& b) {
    double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/// A goal with scripted canUse (selector semantics).
class ScriptGoal final : public ge::Goal {
public:
    explicit ScriptGoal(unsigned f) { flags = f; }
    bool canUse() override { return usable; }
    void start() override { starts++; }
    void stop() override { stops++; }
    void tick() override { ticks++; }
    bool usable = true;
    int  starts = 0, stops = 0, ticks = 0;
};

} // namespace

// java.util.Random parity (the goals consume random numbers as TF does)
GF_REGISTER_SELFTEST("entity.java_random", [](Context& ctx) {
    JavaRandom r0(0);
    ctx.check(r0.nextInt() == -1155484576, "new Random(0).nextInt() == -1155484576");
    JavaRandom r42(42);
    ctx.check(r42.nextInt() == -1170105035, "new Random(42).nextInt() == -1170105035");
    JavaRandom a(7), b(7);
    bool       same = true;
    for (int i = 0; i < 50; ++i) same &= a.nextInt(30) == b.nextInt(30);
    ctx.check(same, "equal seeds give equal sequences");
    JavaRandom c(1);
    bool       inRange = true;
    for (int i = 0; i < 200; ++i) {
        int   v = c.nextInt(10);
        float f = c.nextFloat();
        inRange &= v >= 0 && v < 10 && f >= 0.0f && f < 1.0f;
    }
    ctx.check(inRange, "nextInt(bound) and nextFloat stay in range");
});

GF_REGISTER_SELFTEST("entity.behaviour", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box = co_await ctx.flatArea(5800, 8);
    auto            at  = ctx.testPos(5802, 2);
    probe()             = {};
    auto* mob           = spawnMob(at, "gf_test:boss");
    ctx.require(mob != nullptr, "the fixture boss spawns");
    auto uid = uidOf(*mob);
    co_await ctx.ticks(5);
    mob = mobById(uid);
    ctx.require(mob != nullptr, "the fixture boss is still there");
    ctx.check(probe().spawns == 1, "onSpawn ran once (" + std::to_string(probe().spawns) + ")");
    ctx.check(probe().ticks >= 3, "tick ran every tick (" + std::to_string(probe().ticks) + ")");
    ctx.check(probe().aiSteps >= 3, "aiStep ran every tick (" + std::to_string(probe().aiSteps) + ")");
    // data(actor, key) and its save tag
    ge::data(*mob, "probe") = {{"n", 42}};
    ctx.check(ge::data(*mob, "probe").value("n", 0) == 42, "data(actor, key) keeps a document");
    CompoundTag tag;
    ctx.check(mob->save(tag), "the actor saves");
    ctx.check(tag.contains("GloamingForest"), "the save tag carries the GloamingForest document");
    // EnforcedHomePoint
    ge::setHome(*mob, at, 20);
    auto h = ge::home(*mob);
    ctx.check(h && h->pos == at && h->radius == 20, "setHome / home round trip");
    // Molang reads an entity property set natively (spike S4: properties drive animations)
    ge::setProperty(*mob, "gf_test:phase", 2);
    auto v = propertyOf(*mob, "gf_test:phase");
    ctx.check(v && std::holds_alternative<int>(*v) && std::get<int>(*v) == 2, "setProperty sets an int property");
    // a player's right-click (the interactions dispatch of GameMode::interact) reaches Behaviour::interact
    auto& sp = ctx.spawnPlayer();
    auto  spId = sp.getOrCreateUniqueID().rawID;
    sp.teleport(Vec3{at.x + 2.5f, float(at.y), at.z + 0.5f}, DimensionType(at.dim));
    co_await ctx.ticks(2);
    if (auto* p = playerById(spId); p && (mob = mobById(uid))) {
        std::string                 none, type = mob->getTypeName();
        gf::interactions::EntityContext c{*p, p->getSelectedItem(), none, *mob, type, {0.0, 0.0, 0.0}, false};
        ctx.check(gf::interactions::dispatchInteract(c) == gf::interactions::Result::Consume, "the interaction was consumed");
    }
    ctx.check(probe().interacts == 1, "Behaviour::interact ran on a right-click (" + std::to_string(probe().interacts) + ")");
    // death
    if ((mob = mobById(uid))) mob->kill();
    co_await ctx.ticks(2);
    ctx.check(probe().dies == 1, "Behaviour::die ran (" + std::to_string(probe().dies) + ", " + probe().lastDieType + ")");
    co_await ctx.ticks(25);
    removeActor(uid);
    (void)box;
});

GF_REGISTER_SELFTEST("entity.goals", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box = co_await ctx.flatArea(5810, 8);
    auto            at  = ctx.testPos(5812, 2);
    auto*           mob = spawnMob(at, "gf_test:boss");
    ctx.require(mob != nullptr, "the fixture boss spawns");
    auto uid = uidOf(*mob);
    // GoalSelector: two Move goals, the lower priority value wins; it gives way when it stops
    auto high = std::make_shared<ScriptGoal>(ge::Goal::Move);
    auto low  = std::make_shared<ScriptGoal>(ge::Goal::Move | ge::Goal::Look);
    auto free = std::make_shared<ScriptGoal>(0u);
    ge::addGoal(*mob, 1, high);
    ge::addGoal(*mob, 2, low);
    ge::addGoal(*mob, 3, free);
    co_await ctx.ticks(4);
    ctx.check(high->starts == 1 && high->ticks > 0, "the priority 1 goal runs");
    ctx.check(low->starts == 0, "the priority 2 goal sharing its flag waits");
    ctx.check(free->starts == 1, "a goal without flags runs alongside");
    high->usable = false;
    co_await ctx.ticks(4);
    ctx.check(high->stops == 1, "the priority 1 goal stopped when it could not continue");
    ctx.check(low->starts == 1, "the priority 2 goal took the flag");
    high->usable = true;
    co_await ctx.ticks(4);
    ctx.check(low->stops == 1 && high->starts == 2, "the priority 1 goal interrupts the priority 2 goal again");
    // TF's shared goals and their Java flags
    if (auto* m = mobById(uid)) {
        using G = ge::Goal;
        ctx.check(ge::goals::charge(*m, {})->flags == (G::Move | G::Look), "ChargeAttackGoal: MOVE, LOOK");
        ctx.check(ge::goals::breath(*m, {})->flags == (G::Move | G::Look | G::Jump), "BreathAttackGoal: MOVE, LOOK, JUMP");
        ctx.check(ge::goals::flockToSameKind(*m, 1.0)->flags == 0, "FlockToSameKindGoal: no flags");
        ctx.check(ge::goals::panicOnFlockDeath(*m, 1.0)->flags == G::Move, "PanicOnFlockDeathGoal: MOVE");
        ctx.check(ge::goals::avoidAny(*m, {}, 4.0f, 1.0, 1.2)->flags == G::Move, "AvoidAnyEntityGoal: MOVE");
        ctx.check(ge::goals::goHome(*m, 1.25)->flags == G::Move, "AttemptToGoHomeGoal: MOVE");
        ctx.check(ge::goals::alwaysWatchTarget(*m)->requiresUpdateEveryTick(), "AlwaysWatchTargetGoal ticks every tick");
        ctx.check(ge::goals::randomLookIfBored(*m)->flags == (G::Move | G::Look), "RandomLookAroundIfBoredGoal: MOVE, LOOK");
        ctx.check(ge::goals::simplifiedAttack(*m)->requiresUpdateEveryTick(), "SimplifiedAttackGoal ticks every tick");
        // AttemptToGoHomeGoal stays idle inside the home radius, wants to move outside it
        auto home = ge::goals::goHome(*m, 1.25);
        ge::setHome(*m, at, 20);
        ctx.check(!home->canUse(), "at home: AttemptToGoHomeGoal is idle");
        ge::setHome(*m, at.offset(60, 0, 0), 4);
        bool wants = false;
        for (int i = 0; i < 20 && !wants; ++i) wants = home->canUse();
        ctx.check(wants, "far from home: AttemptToGoHomeGoal finds a position towards it");
        // NoClipMoveControl pushes towards the wanted position
        ge::setMoveControl(*m, ge::MoveKind::NoClip);
        ge::ext::setWantedPosition(*m, Vec3{at.x + 0.5f, at.y + 10.0f, at.z + 0.5f}, 1.0);
        ctx.check(ge::ext::wantedPosition(*m).has_value(), "the NoClip controller has a wanted position");
    }
    co_await ctx.ticks(2);
    removeActor(uid);
    (void)box;
});

// Spike S4 / e2e: linked multipart (3 parts follow, damage routes to the parent, never saved)
GF_REGISTER_SELFTEST("entity.multipart", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box = co_await ctx.flatArea(5820, 12);
    auto            at  = ctx.testPos(5824, 2);
    auto*           mob = spawnMob(at, "gf_test:boss");
    ctx.require(mob != nullptr, "the fixture boss spawns");
    auto uid = uidOf(*mob);
    std::vector<ge::PartHandle> parts;
    parts.push_back(ge::addPart(*mob, "left", {0.8f, 0.8f}, [](Mob&) { return Vec3{2.0f, 0.5f, 0.0f}; }));
    parts.push_back(ge::addPart(*mob, "right", {0.8f, 0.8f}, [](Mob&) { return Vec3{-2.0f, 0.5f, 0.0f}; }));
    parts.push_back(ge::addPart(*mob, "head", {1.0f, 1.0f}, [](Mob&) { return Vec3{0.0f, 2.5f, 0.0f}; }));
    for (int i = 0; i < 3; ++i) ctx.require(parts[i].valid() && ge::partActor(parts[i]), "part " + std::to_string(i) + " exists");
    ctx.check(ge::partActor(parts[0])->getTypeName() == "gf_test:boss_part", "the parent's own part type is used (gf_test:boss_part)");
    for (int i = 0; i < 3; ++i) {
        auto v = propertyOf(*ge::partActor(parts[i]), "gf_test:part");
        ctx.check(v && std::holds_alternative<int>(*v) && std::get<int>(*v) == i, "part " + std::to_string(i) + " has gf_test:part = " + std::to_string(i));
    }
    ctx.check(ge::ext::parts(*mob).size() == 3, "ext::parts lists 3 parts");
    // the parts follow the parent
    if ((mob = mobById(uid))) mob->teleportTo(Vec3{at.x + 3.5f, float(at.y), at.z + 0.5f}, false, 0, 0, false);
    co_await ctx.ticks(3);
    mob = mobById(uid);
    ctx.require(mob != nullptr, "the parent is still there");
    Vec3 base   = mob->getFeetPos();
    Vec3 offs[] = {{2.0f, 0.5f, 0.0f}, {-2.0f, 0.5f, 0.0f}, {0.0f, 2.5f, 0.0f}};
    for (int i = 0; i < 3; ++i) {
        auto* p = ge::partActor(parts[i]);
        ctx.require(p != nullptr, "part " + std::to_string(i) + " still exists");
        double d = dist(p->getFeetPos(), Vec3{base.x + offs[i].x, base.y + offs[i].y, base.z + offs[i].z});
        ctx.check(d < 0.6, "part " + std::to_string(i) + " follows the parent (" + std::to_string(d) + " off)");
    }
    // a part is never written to the world (Java's parts are not)
    CompoundTag tag;
    ctx.check(!ge::partActor(parts[0])->save(tag), "a part is not saved");
    ctx.check(ge::ext::parentOf(*ge::partActor(parts[0])) == mob, "ext::parentOf finds the parent");
    // a player's hit on a part lands on the parent
    auto& sp   = ctx.spawnPlayer();
    auto  spId = sp.getOrCreateUniqueID().rawID;
    auto  pp   = ge::partActor(parts[0])->getFeetPos();
    sp.teleport(Vec3{pp.x, pp.y - 0.5f, pp.z + 1.5f}, DimensionType(at.dim));
    co_await ctx.ticks(3);
    int parentBefore = mobById(uid)->getHealth();
    int partBefore   = ge::partActor(parts[0])->getHealth();
    if (auto* p = playerById(spId)) static_cast<SimulatedPlayer*>(p)->simulateAttack(ge::partActor(parts[0]));
    co_await ctx.ticks(2);
    mob = mobById(uid);
    ctx.require(mob != nullptr, "the parent survives one hit");
    ctx.check(mob->getHealth() < parentBefore, "the parent took the part's hit (" + std::to_string(parentBefore) + " -> " + std::to_string(mob->getHealth()) + ")");
    ctx.check(ge::partActor(parts[0])->getHealth() == partBefore, "the part itself took no damage");
    // removePart / the parts go with the parent
    ge::ext::removePart(parts[2]);
    co_await ctx.ticks(2);
    ctx.check(ge::ext::parts(*mobById(uid)).size() == 2, "ext::removePart removes one part");
    std::int64_t leftId = uidOf(*ge::partActor(parts[0]));
    removeActor(uid);
    co_await ctx.ticks(4);
    ctx.check(actorById(leftId) == nullptr, "the parts are removed with their parent");
    (void)box;
});

GF_REGISTER_SELFTEST("entity.mount", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box   = co_await ctx.flatArea(5830, 8);
    auto            at    = ctx.testPos(5832, 2);
    auto*           horse = spawnMob(at, "minecraft:horse");
    ctx.require(horse != nullptr, "a horse spawns (a vanilla rideable mob)");
    auto  hid  = uidOf(*horse);
    auto& sp   = ctx.spawnPlayer();
    auto  spId = sp.getOrCreateUniqueID().rawID;
    sp.teleport(Vec3{at.x + 1.5f, float(at.y), at.z + 0.5f}, DimensionType(at.dim));
    co_await ctx.ticks(2);
    auto* p = playerById(spId);
    horse   = mobById(hid);
    ctx.require(p && horse, "player and mount present");
    ctx.check(ge::mount::grab(*horse, *p), "the mob grabs the player");
    ctx.check(p->getVehicle() == horse, "the player rides the mob");
    ctx.check(ge::ext::isRidingUnfriendly(*p), "isRidingUnfriendly");
    // dismounting by itself is undone (HostileMountEvents#preventMountDismount)
    p->stopRiding(true, false, false, false);
    co_await ctx.ticks(2);
    p     = playerById(spId);
    horse = mobById(hid);
    ctx.require(p && horse, "player and mount still present");
    ctx.check(p->getVehicle() == horse, "the player is put back on the hostile mount");
    // ThrowRiderGoal#stop: thrown with YetiThrowAttachment's state
    ge::mount::throwRider(*horse, Vec3{0.0f, 0.9f, 2.0f});
    ctx.check(p->getVehicle() == nullptr, "the rider was thrown off");
    ctx.check(ge::ext::thrown(*p), "the player is marked thrown");
    ctx.check(ge::ext::throwCooldown(*p) == 200, "the throw cooldown is 200 ticks");
    ctx.check(ge::ext::thrower(*p) == horse, "the thrower is the mob");
    co_await ctx.ticks(3);
    p = playerById(spId);
    ctx.require(p != nullptr, "the player is still there");
    ctx.check(p->getVehicle() == nullptr, "a thrown player is not grabbed back");
    ctx.check(ge::ext::throwCooldown(*p) < 200, "the cooldown counts down");
    removeActor(hid);
    (void)box;
});

// parts added by a behaviour inside the actor's own tick (onSpawn), as the boss units do
GF_REGISTER_SELFTEST("entity.multipart_in_tick", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box = co_await ctx.flatArea(5826, 6);
    auto            at  = ctx.testPos(5828, 2);
    auto*           mob = spawnMob(at, "gf_test:boss");
    ctx.require(mob != nullptr, "the fixture boss spawns");
    mob->setNameTag("gf_parts");
    auto uid = uidOf(*mob);
    co_await ctx.ticks(4);
    mob = mobById(uid);
    ctx.require(mob != nullptr, "the boss is still there");
    auto parts = ge::ext::parts(*mob);
    ctx.check(parts.size() == 3, "onSpawn added 3 parts inside the tick (" + std::to_string(parts.size()) + ")");
    for (auto const& p : parts) ctx.check(ge::partActor(p) != nullptr, "part " + p.partId + " exists");
    removeActor(uid);
    co_await ctx.ticks(3);
    (void)box;
});

// a parent without its own `<id>_part` type gets F8's invisible hitbox part
GF_REGISTER_SELFTEST("entity.multipart_generic", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box   = co_await ctx.flatArea(5836, 4);
    auto            at    = ctx.testPos(5837, 2);
    auto*           horse = spawnMob(at, "minecraft:horse");
    ctx.require(horse != nullptr, "a horse spawns");
    auto  hid    = uidOf(*horse);
    auto  handle = ge::addPart(*horse, "probe", {0.5f, 0.5f}, [](Mob&) { return Vec3{0.0f, 2.0f, 0.0f}; });
    auto* part   = ge::partActor(handle);
    ctx.require(part != nullptr, "the part exists");
    ctx.check(part->getTypeName() == "gloamingforest:part", "the generic part type is used (" + part->getTypeName() + ")");
    auto partId = uidOf(*part);
    co_await ctx.ticks(2);
    removeActor(hid);
    co_await ctx.ticks(3);
    ctx.check(actorById(partId) == nullptr, "the generic part went with its parent");
    (void)box;
});

// The persisted data survives a world reload (server scenario: write, Restart-Server, read).
namespace {
constexpr char const* kPersistTag = "gf_entity_persist";

AABB around(core::GlobalPos const& at) {
    return AABB{Vec3{at.x - 4.0f, at.y - 8.0f, at.z - 4.0f}, Vec3{at.x + 5.0f, at.y + 8.0f, at.z + 5.0f}};
}
} // namespace

GF_REGISTER_SELFTEST("entity.persist_write", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto at = ctx.testPos(5895, 4);
    ctx.require(co_await ctx.ensureLoaded(at), "test area loaded");
    auto* region = game::regionOf(at.dim);
    ctx.require(region != nullptr, "test dimension");
    // one marker at a time
    for (auto* a : region->getEntities(around(at), 0.0f))
        if (a && a->hasTag(kPersistTag)) a->remove();
    auto* marker = spawnMob(at, "gf_test:boss_part", 1.0f);
    ctx.require(marker != nullptr, "the marker actor spawns");
    marker->addTag(kPersistTag);
    auto nonce = static_cast<long long>(freshSeed() & 0x7fffffff);
    ge::data(*marker, "persist") = {{"nonce", nonce}};
    ge::setHome(*marker, at, 7);
    ctx.info("persist marker written nonce=" + std::to_string(nonce));
    co_await ctx.ticks(2);
});

GF_REGISTER_SELFTEST("entity.persist_read", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto at = ctx.testPos(5895, 4);
    ctx.require(co_await ctx.ensureLoaded(at), "test area loaded");
    std::int64_t markerId = 0;
    for (int i = 0; i < 60 && !markerId; ++i) {
        co_await ctx.ticks(1);
        if (auto* region = game::regionOf(at.dim))
            for (auto* a : region->getEntities(around(at), 0.0f))
                if (a && a->hasTag(kPersistTag)) markerId = uidOf(*a);
    }
    auto* marker = actorById(markerId);
    if (!marker) {
        ctx.info("no persist marker");
        co_return;
    }
    auto nonce = ge::data(*marker, "persist").value("nonce", -1LL);
    std::optional<ge::Home> h;
    if (auto* m = asMob(*marker)) h = ge::home(*m);
    ctx.check(nonce >= 0, "the marker's data came back from the save");
    ctx.check(h && h->pos == at && h->radius == 7, "the marker's home came back from the save");
    ctx.info("persist marker verified nonce=" + std::to_string(nonce));
    marker->remove();
});

} // namespace gf::feature::entity::tests
