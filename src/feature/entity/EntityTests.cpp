// Selftests of F8's "entity" contract (rule R12: prefix entity.*, dx 5800..5899) and the spike S4
// probes: behaviour dispatch, persisted data, entity properties read back through Molang, the
// goal selector and upstream's goals at work, the pathfinding block rules of the random positions,
// linked multipart parts (follow, damage routed to the parent, never saved) and the hostile mount.
// Fixtures: gf_test:boss (behaviour and boss only while a FixtureScope lives) and gf_test:boss_part
// (assets/test), vanilla mobs.

#include <algorithm>
#include <cmath>
#include <type_traits>

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/deps/vanilla_components/Hitbox.h"
#include "mc/deps/vanilla_components/HitboxComponent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFactory.h"
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

#include "core/jrand/JRand.h"
#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/entity/Entity.h"
#include "feature/entity/EntityFixture.h"
#include "feature/entity/EntityInternal.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/entity/Handles.h"
#include "feature/net/Net.h"
#include "feature/ext/entity/World.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace gf::feature::entity::tests {

using gf::selftest::Context;
namespace ge = gf::entity;

namespace {

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
    void set(Difficulty d) {
        if (auto level = ll::service::getLevel()) level->setDifficulty(d);
    }
    ~DifficultyScope() {
        if (auto level = ll::service::getLevel()) level->setDifficulty(previous);
    }
};

Mob* spawnMob(core::GlobalPos const& at, std::string const& id, float dy = 0.0f, float dx = 0.5f, float dz = 0.5f) {
    auto* region = game::regionOf(at.dim);
    auto  level  = ll::service::getLevel();
    if (!region || !level) return nullptr;
    return level->getSpawner().spawnMob(*region, ActorDefinitionIdentifier(id), nullptr, Vec3{at.x + dx, at.y + dy, at.z + dz}, false, false, false);
}

Mob* mobById(std::int64_t uid) {
    auto* a = actorById(uid);
    return a ? asMob(*a) : nullptr;
}

void removeActor(std::int64_t uid) {
    if (auto* a = actorById(uid)) a->remove();
}

/// Spawns any actor (a boat, a minecart: not a mob) through the actor factory.
Actor* spawnActor(core::GlobalPos const& at, std::string const& id, float dx = 0.5f, float dz = 0.5f) {
    auto* region = game::regionOf(at.dim);
    auto  level  = ll::service::getLevel();
    if (!region || !level) return nullptr;
    auto owned = level->getActorFactory().createSpawnedActor(ActorDefinitionIdentifier(id), nullptr, Vec3{at.x + dx, float(at.y), at.z + dz}, Vec2{0.0f, 0.0f});
    return owned ? level->addEntity(*region, std::move(owned)) : nullptr;
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

namespace {
/// An independent copy of java.util.Random (the reference F8 used before F5's LegacyRandom was on
/// main): entity.java_random checks that the goals' random source agrees with it bit for bit.
class ReferenceJavaRandom {
public:
    explicit ReferenceJavaRandom(std::int64_t seed) : mSeed((seed ^ 0x5DEECE66DLL) & ((1LL << 48) - 1)) {}
    int next(int bits) {
        mSeed = (mSeed * 0x5DEECE66DLL + 0xBLL) & ((1LL << 48) - 1);
        return static_cast<int>(static_cast<std::int64_t>(static_cast<std::uint64_t>(mSeed) >> (48 - bits)));
    }
    int nextInt() { return next(32); }
    int nextInt(int bound) {
        if ((bound & -bound) == bound) return static_cast<int>((static_cast<std::int64_t>(bound) * next(31)) >> 31);
        int bits, val;
        do {
            bits = next(31);
            val  = bits % bound;
        } while (bits - val + (bound - 1) < 0);
        return val;
    }
    std::int64_t nextLong() { return static_cast<std::int64_t>(static_cast<std::uint64_t>(static_cast<std::int64_t>(next(32))) << 32) + next(32); }
    bool         nextBoolean() { return next(1) != 0; }
    float        nextFloat() { return static_cast<float>(next(24)) / static_cast<float>(1 << 24); }
    double       nextDouble() { return static_cast<double>((static_cast<std::int64_t>(next(26)) << 27) + next(27)) * 0x1.0p-53; }
    double       nextGaussian() {
        if (mHave) {
            mHave = false;
            return mNext;
        }
        double v1, v2, s;
        do {
            v1 = 2 * nextDouble() - 1;
            v2 = 2 * nextDouble() - 1;
            s  = v1 * v1 + v2 * v2;
        } while (s >= 1 || s == 0);
        double m = std::sqrt(-2 * std::log(s) / s);
        mNext    = v2 * m;
        mHave    = true;
        return v1 * m;
    }

private:
    std::int64_t mSeed;
    double       mNext = 0;
    bool         mHave = false;
};
} // namespace

// java.util.Random parity (the goals consume random numbers as upstream does): JavaRandom is F5's
// gf::core::jrand::LegacyRandom; it must give Java's values and agree with an independent copy
GF_REGISTER_SELFTEST("entity.java_random", [](Context& ctx) {
    static_assert(std::is_same_v<JavaRandom, core::jrand::LegacyRandom>, "the goals use the W0 contract's LegacyRandom");
    JavaRandom r0(0);
    ctx.check(r0.nextInt() == -1155484576, "new Random(0).nextInt() == -1155484576");
    JavaRandom r42(42);
    ctx.check(r42.nextInt() == -1170105035, "new Random(42).nextInt() == -1170105035");
    ctx.check(JavaRandom(0).nextLong() == -4962768465676381896LL, "new Random(0).nextLong() == -4962768465676381896");
    ctx.check(JavaRandom(0).nextDouble() == 0.730967787376657, "new Random(0).nextDouble() == 0.730967787376657");
    ctx.check(JavaRandom(0).nextGaussian() == 0.8025330637390305, "new Random(0).nextGaussian() == 0.8025330637390305");
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
    // a RandomSource of the W0 contract: fork / forkPositional as LegacyRandomSource
    JavaRandom f1(99), f2(99);
    auto       k1 = f1.fork(), k2 = f2.fork();
    ctx.check(k1->nextLong() == k2->nextLong(), "fork() is deterministic");
    auto p1 = JavaRandom(5).forkPositional(), p2 = JavaRandom(5).forkPositional();
    ctx.check(p1->at(1, 2, 3)->nextInt() == p2->at(1, 2, 3)->nextInt() && p1->fromHashOf("a")->nextInt() == p2->fromHashOf("a")->nextInt(), "forkPositional() is deterministic");
    // bit for bit against the independent copy, over every member the goals and the boss kit use
    for (std::int64_t seed : {std::int64_t{0}, std::int64_t{1234}, std::int64_t{-987654321012345LL}, std::int64_t{0x5DEECE66DLL}}) {
        JavaRandom          j(seed);
        ReferenceJavaRandom r(seed);
        bool                agree = true;
        for (int i = 0; i < 200 && agree; ++i) {
            int bound = 1 + i * 7 % 300; // powers of two and others
            agree &= j.nextInt(bound) == r.nextInt(bound) && j.nextInt(64) == r.nextInt(64) && j.nextInt() == r.nextInt();
            agree &= j.nextLong() == r.nextLong() && j.nextBoolean() == r.nextBoolean() && j.nextFloat() == r.nextFloat();
            agree &= j.nextDouble() == r.nextDouble() && j.nextGaussian() == r.nextGaussian();
        }
        ctx.check(agree, "gf::core::jrand::LegacyRandom matches java.util.Random for seed " + std::to_string(seed));
    }
    // fork() is LegacyRandomSource#fork: a new source seeded with nextLong()
    JavaRandom          fj(77);
    ReferenceJavaRandom fr(77);
    auto                forked = fj.fork();
    ReferenceJavaRandom expect(fr.nextLong());
    ctx.check(forked->nextInt() == expect.nextInt(), "fork() seeds the new source with nextLong()");
});

GF_REGISTER_SELFTEST("entity.behaviour", [](Context& ctx) -> ll::coro::CoroTask<> {
    FixtureScope    fixture;
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
    auto saved = [&](CompoundTag const& t) {
        if (!t.contains("GloamingForest")) return ge::Json();
        auto j = ge::Json::parse(std::string(t["GloamingForest"].get<StringTag>()), nullptr, false);
        return j.is_object() && j.contains("entity:save") ? j["entity:save"] : ge::Json();
    };
    ctx.check(saved(tag).value("probe", "") == "saved", "Behaviour::save writes its document");
    // a later save with nothing to save drops the earlier one (no stale load after a reload)
    ge::data(*mob, "probe_skip_save") = true;
    CompoundTag again;
    ctx.check(mob->save(again) && saved(again).is_null(), "an empty Behaviour::save erases the previous save");
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
    // a die that leaves the mob alive (gf::death::onLethal kept it) dispatches nothing
    if ((mob = mobById(uid))) {
        ActorDamageSource source(SharedTypes::Legacy::ActorDamageCause::Override, "");
        dispatchDieForTest(*mob, source);
    }
    ctx.check(probe().dies == 0, "no Behaviour::die for a mob that is still alive");
    // death, once
    if ((mob = mobById(uid))) mob->kill();
    co_await ctx.ticks(2);
    ctx.check(probe().dies == 1, "Behaviour::die ran (" + std::to_string(probe().dies) + ", " + probe().lastDieType + ")");
    if ((mob = mobById(uid))) {
        ActorDamageSource source(SharedTypes::Legacy::ActorDamageCause::Override, "");
        dispatchDieForTest(*mob, source);
        ctx.check(probe().dies == 1, "a second die of the same death dispatches nothing");
    }
    co_await ctx.ticks(25);
    removeActor(uid);
    (void)box;
});

// Registrations of one type add up (U16's vanilla cat / wolf / parrot behaviours next to another
// unit's), run in registration order, and come off by handle. Synchronous: no tick runs while the
// probe behaviours of minecraft:player are registered.
GF_REGISTER_SELFTEST("entity.behaviour_additive", [](Context& ctx) {
    std::string const type = "minecraft:player";
    auto&             self = ctx.player();
    std::size_t const base = behaviourCount(type);
    std::vector<int>  order;
    auto a = ge::ext::addBehaviour(type, {.hurt = [&](Actor&, float& amount, std::string const&, Actor*) {
                                              order.push_back(1);
                                              amount *= 2.0f;
                                              return true;
                                          }});
    auto b = ge::ext::addBehaviour(type, {.hurt = [&](Actor&, float& amount, std::string const&, Actor*) {
                                              order.push_back(2);
                                              amount += 1.0f;
                                              return amount < 10.0f;
                                          }});
    ctx.check(a != 0 && b != 0 && a != b, "every registration gets its own handle");
    ctx.check(behaviourCount(type) == base + 2, "two registrations of one type are both kept");
    float amount = 3.0f;
    bool  go     = dispatchHurt(self, amount, "minecraft:generic", nullptr);
    ctx.check(go && amount == 7.0f, "both hurt hooks ran, each on the amount the one before left (" + std::to_string(amount) + ")");
    ctx.check(order == std::vector<int>({1, 2}), "the hooks run in registration order");
    order.clear();
    amount = 5.0f;
    ctx.check(!dispatchHurt(self, amount, "minecraft:generic", nullptr), "one refusal cancels the damage");
    ctx.check(ge::ext::removeBehaviour(a), "removeBehaviour finds its registration");
    ctx.check(!ge::ext::removeBehaviour(a), "a second removal of the same handle finds nothing");
    order.clear();
    amount = 3.0f;
    (void)dispatchHurt(self, amount, "minecraft:generic", nullptr);
    ctx.check(order == std::vector<int>({2}) && amount == 4.0f, "a removed registration's hooks no longer run");
    ctx.check(ge::ext::removeBehaviour(b) && behaviourCount(type) == base, "the type is back to its own registrations");
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
    // GoalSelector#disabledFlags (a controlling mob passenger): goals with the flag stop / wait
    {
        GoalSelector sel;
        auto         move = std::make_shared<ScriptGoal>(ge::Goal::Move);
        auto         look = std::make_shared<ScriptGoal>(ge::Goal::Look);
        sel.add(1, move);
        sel.add(2, look);
        sel.tick(0);
        ctx.check(move->starts == 1 && look->starts == 1, "both goals run without disabled flags");
        sel.tick(ge::Goal::Move);
        ctx.check(move->stops == 1 && look->stops == 0, "a disabled MOVE stops the MOVE goal only");
        sel.tick(ge::Goal::Move);
        ctx.check(move->starts == 1, "a goal with a disabled flag does not start");
        sel.tick(0);
        ctx.check(move->starts == 2, "it starts again when the flag is back");
    }
    if (auto* m = mobById(uid)) ctx.check(controlFlagsDisabled(*m) == 0, "no passenger: no disabled flags (Mob#updateControlFlags)");
    // upstream's shared goals and their Java flags
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

// ChargeAttackGoal at work: canUse between 4 and 8 blocks, a 15..44 tick windup with the charging
// flag, then the move to the point 2.1 blocks beyond the target
GF_REGISTER_SELFTEST("entity.goal_charge", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box = co_await ctx.flatArea(5852, 8);
    auto            at  = ctx.testPos(5852, 2);
    auto*           mob = spawnMob(at, "gf_test:boss");
    ctx.require(mob != nullptr, "the charger spawns");
    auto  uid  = uidOf(*mob);
    auto& sp   = ctx.spawnPlayer();
    auto  spId = sp.getOrCreateUniqueID().rawID;
    sp.teleport(Vec3{at.x + 6.5f, float(at.y), at.z + 0.5f}, DimensionType(at.dim));
    co_await ctx.ticks(4);
    mob     = mobById(uid);
    auto* p = playerById(spId);
    ctx.require(mob && p, "charger and target present");
    ge::seedForTest(*mob, 1234);
    mob->setTarget(p);
    auto goal = ge::goals::charge(*mob, {.speed = 2.0});
    bool use  = false;
    for (int i = 0; i < 200 && !use; ++i) use = goal->canUse();
    ctx.require(use, "ChargeAttackGoal#canUse (1 in 10) with a target 6 blocks away on the ground");
    auto a = mob->getFeetPos(), b = p->getFeetPos();
    double vx = b.x - a.x, vz = b.z - a.z, d = std::sqrt(vx * vx + vz * vz);
    Vec3   expected{float(a.x + vx / d * (d + 2.1)), b.y, float(a.z + vz / d * (d + 2.1))};
    ctx.check(!ge::ext::wantedPosition(*mob).has_value(), "no move before the charge");
    goal->start();
    int windup = 0;
    bool charging = false;
    while (windup < 60) {
        goal->tick();
        ++windup;
        mob = mobById(uid);
        ctx.require(mob != nullptr, "the charger is still there");
        if (ge::ext::isCharging(*mob)) charging = true;
        if (ge::ext::wantedPosition(*mob)) break;
        co_await ctx.ticks(1);
    }
    ctx.check(charging, "the charging flag is set during the windup (ITFCharger#setCharging)");
    ctx.check(windup >= 15 && windup <= 44, "the windup lasts 15..44 ticks (" + std::to_string(windup) + ")");
    auto wanted = ge::ext::wantedPosition(*mob);
    ctx.require(wanted.has_value(), "the charge moves (no navigation: the NoClip wanted position)");
    ctx.check(dist(*wanted, expected) < 0.05, "the charge point lies 2.1 blocks beyond the target (" + std::to_string(dist(*wanted, expected)) + " off)");
    goal->stop();
    ctx.check(!ge::ext::isCharging(*mobById(uid)), "the flag is cleared when the charge stops");
    removeActor(uid);
    (void)box;
});

// BreathAttackGoal at work: canUse on the last attacker within range (7 % chance), the breathing
// flag, and the breath attack on what the head looks at after five ticks
GF_REGISTER_SELFTEST("entity.goal_breath", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box = co_await ctx.flatArea(5854, 8);
    auto            at  = ctx.testPos(5855, 2);
    auto*           mob = spawnMob(at, "gf_test:boss");
    ctx.require(mob != nullptr, "the breather spawns");
    auto  uid  = uidOf(*mob);
    auto& sp   = ctx.spawnPlayer();
    auto  spId = sp.getOrCreateUniqueID().rawID;
    sp.teleport(Vec3{at.x + 3.5f, float(at.y), at.z + 0.5f}, DimensionType(at.dim));
    co_await ctx.ticks(4);
    mob     = mobById(uid);
    auto* p = playerById(spId);
    ctx.require(mob && p, "breather and target present");
    int          breaths = 0;
    std::int64_t hit     = 0;
    ge::ext::onBreathAttack("gf_test:boss", [&](Mob&, Actor& target) {
        ++breaths;
        hit = uidOf(target);
    });
    struct Unregister {
        ~Unregister() { ge::ext::onBreathAttack("gf_test:boss", nullptr); }
    } unregister;
    ge::seedForTest(*mob, 4321);
    mob->setLastHurtByMob(p);
    auto goal = ge::goals::breath(*mob, {.range = 5.0f, .durationTicks = 40, .chance = 0.07f});
    bool use  = false;
    for (int i = 0; i < 400 && !use; ++i) use = goal->canUse();
    ctx.require(use, "BreathAttackGoal#canUse with the last attacker 3 blocks away");
    goal->start();
    ctx.check(ge::ext::isBreathing(*mobById(uid)), "the breathing flag is set (IBreathAttacker#setBreathing)");
    for (int i = 0; i < 5; ++i) goal->tick();
    ctx.check(breaths == 0, "no breath damage in the first five ticks");
    for (int i = 0; i < 5; ++i) {
        co_await ctx.ticks(1);
        if (!mobById(uid)) break;
        goal->tick();
    }
    ctx.check(breaths > 0, "the breath attack runs on the target (" + std::to_string(breaths) + ")");
    ctx.check(hit == spId, "the breath hits what the head looks at");
    goal->stop();
    if (auto* m = mobById(uid)) ctx.check(!ge::ext::isBreathing(*m), "the flag is cleared when the breath stops");
    removeActor(uid);
    (void)box;
});

// The block rules of DefaultRandomPos: stable destinations, path types and their maluses, the
// legacy solid floor of the sinister spawner and the walk target values
GF_REGISTER_SELFTEST("entity.goal_blocks", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box = co_await ctx.flatArea(5803, 6);
    auto            at  = ctx.testPos(5804, 1);
    auto*           region = ctx.region();
    ctx.require(region != nullptr, "the test dimension");
    auto place = [&](int dx, int dy, int dz, char const* id) {
        auto p = at.offset(dx, dy, dz);
        ctx.require(ctx.setBlock(p, id), std::string("place ") + id);
        return game::blockPos(p);
    };
    auto stone = place(0, 0, 0, "minecraft:stone");
    auto glass = place(1, 0, 0, "minecraft:glass");
    auto lava  = place(2, 0, 0, "minecraft:lava");
    auto fence = place(3, 0, 0, "minecraft:oak_fence");
    auto leaf  = place(0, 0, 2, "minecraft:oak_leaves");
    auto cact  = place(1, 0, 2, "minecraft:cactus");
    auto carp  = place(2, 0, 2, "minecraft:white_carpet");
    auto slab  = place(3, 0, 2, "minecraft:smooth_stone_slab");
    BlockPos above{stone.x, stone.y + 1, stone.z};
    ctx.check(mobs::stableDestination(*region, above), "stone below: a stable destination (isSolidRender)");
    ctx.check(!mobs::stableDestination(*region, BlockPos{glass.x, glass.y + 1, glass.z}), "glass below: not stable");
    ctx.check(!mobs::stableDestination(*region, BlockPos{slab.x, slab.y + 1, slab.z}), "a slab below: not stable");
    ctx.check(mobs::pathTypeFromState(*region, lava) == mobs::PathType::Lava, "lava: LAVA");
    ctx.check(mobs::pathTypeFromState(*region, fence) == mobs::PathType::Fence, "a fence: FENCE");
    ctx.check(mobs::pathTypeFromState(*region, leaf) == mobs::PathType::Leaves, "leaves: LEAVES");
    ctx.check(mobs::pathTypeFromState(*region, cact) == mobs::PathType::DamageOther, "a cactus: DAMAGE_OTHER");
    ctx.check(mobs::pathTypeFromState(*region, stone) == mobs::PathType::Blocked, "stone: BLOCKED");
    ctx.check(mobs::pathTypeStatic(*region, above) == mobs::PathType::Walkable, "air on stone: WALKABLE");
    ctx.check(mobs::pathTypeStatic(*region, BlockPos{lava.x, lava.y + 1, lava.z}) == mobs::PathType::Open, "air on lava: OPEN (and no stable floor)");
    BlockPos nextToLava{glass.x, glass.y + 1, glass.z};
    ctx.check(mobs::pathTypeStatic(*region, nextToLava) == mobs::PathType::DangerFire, "air on glass next to lava: DANGER_FIRE");
    ctx.check(mobs::hasMalus(*region, nextToLava), "DANGER_FIRE has a malus (8)");
    ctx.check(!mobs::hasMalus(*region, above), "air on stone has no malus");
    ctx.check(mobs::hasMalus(*region, BlockPos{fence.x, fence.y, fence.z}), "a fence has a malus (-1)");
    ctx.check(mobs::legacySolid(*region, stone) && !mobs::legacySolid(*region, carp), "legacy solid: stone yes, a carpet no");
    ctx.check(mobs::emptyCollision(*region, BlockPos{above.x, above.y + 1, above.z}) && !mobs::emptyCollision(*region, stone), "collision: air none, stone a box");
    if (auto* m = spawnMob(at.offset(4, 0, 4), "gf_test:boss")) {
        ctx.check(mobs::isMonster(*m), "gf_test:boss (family monster) counts as a Monster");
        auto v = mobs::walkTargetValue(*m, above);
        ctx.check(v >= -0.5f && v <= 0.5f, "a Monster's walk target value is 0.5 - brightness (" + std::to_string(v) + ")");
        m->remove();
    }
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
    // the parent turns its parts (a Naga segment's yaw / pitch)
    ge::ext::setPartRotation(parts[0], 90.0f, 10.0f);
    co_await ctx.ticks(2);
    mob = mobById(uid);
    ctx.require(mob != nullptr && ge::partActor(parts[0]) != nullptr, "parent and part still there");
    auto rot = ge::partActor(parts[0])->getRotation();
    ctx.check(std::abs(rot.y - 90.0f) < 0.5f && std::abs(rot.x - 10.0f) < 0.5f, "setPartRotation turns the part (" + std::to_string(rot.y) + ", " + std::to_string(rot.x) + ")");
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

// Damage other than a player's melee on a part (explosions, projectiles, magic) goes to the parent
// through the combat pipeline; environmental damage (suffocation, fire, falls, ...) reaches neither
GF_REGISTER_SELFTEST("entity.multipart_damage", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box = co_await ctx.flatArea(5814, 6);
    auto            at  = ctx.testPos(5816, 2);
    auto*           mob = spawnMob(at, "gf_test:boss");
    ctx.require(mob != nullptr, "the fixture boss spawns");
    auto uid  = uidOf(*mob);
    auto part = ge::addPart(*mob, "body", {1.0f, 1.0f}, [](Mob&) { return Vec3{0.0f, 2.2f, 0.0f}; });
    ctx.require(ge::partActor(part) != nullptr, "the part exists");
    co_await ctx.ticks(2);
    int before = mobById(uid)->getHealth();
    gf::combat::hurt(*ge::partActor(part), 4.0f, "minecraft:in_wall");
    gf::combat::hurt(*ge::partActor(part), 4.0f, "minecraft:on_fire");
    co_await ctx.ticks(1);
    ctx.check(mobById(uid)->getHealth() == before, "environmental damage on a part does not reach the parent");
    gf::combat::hurt(*ge::partActor(part), 4.0f, "minecraft:explosion");
    co_await ctx.ticks(1);
    ctx.check(mobById(uid)->getHealth() < before, "an explosion on a part hurts the parent (" + std::to_string(before) + " -> " + std::to_string(mobById(uid)->getHealth()) + ")");
    removeActor(uid);
    co_await ctx.ticks(2);
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
    bool back = false;
    for (int i = 0; i < 10 && !back; ++i) {
        co_await ctx.ticks(1);
        p     = playerById(spId);
        horse = mobById(hid);
        ctx.require(p && horse, "player and mount still present");
        back = p->getVehicle() == horse;
    }
    ctx.check(back, "the player is put back on the hostile mount");
    // no teleporting away (preventTeleportingOffHostileMounts: an ender pearl, a command, ...)
    int  blocked = blockedTeleportsForTest();
    auto before  = p->getFeetPos();
    p->teleportTo(Vec3{before.x + 40.0f, before.y, before.z}, true, 1 /* projectile */, 0, false);
    co_await ctx.ticks(2);
    p     = playerById(spId);
    horse = mobById(hid);
    ctx.require(p && horse, "player and mount still present after the teleport");
    ctx.check(blockedTeleportsForTest() > blocked, "the teleport was cancelled");
    ctx.check(p->getVehicle() == horse && dist(p->getFeetPos(), before) < 4.0, "the rider stayed on its mount");
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

// IHostileMount as a type marker: whoever gets on a marked mob rides unfriendly
GF_REGISTER_SELFTEST("entity.mount_type", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box   = co_await ctx.flatArea(5833, 8);
    auto            at    = ctx.testPos(5835, 2);
    // a pig: an untamed horse would buck its rider off by itself (vanilla taming), a pig does not
    struct MarkScope {
        MarkScope() { ge::ext::markHostileMount("minecraft:pig"); }
        ~MarkScope() { unmarkHostileMountForTest("minecraft:pig"); }
    } mark;
    auto* mount = spawnMob(at, "minecraft:pig");
    ctx.require(mount != nullptr, "a pig spawns");
    auto  mid  = uidOf(*mount);
    auto& sp   = ctx.spawnPlayer();
    auto  spId = sp.getOrCreateUniqueID().rawID;
    sp.teleport(Vec3{at.x + 1.5f, float(at.y), at.z + 0.5f}, DimensionType(at.dim));
    co_await ctx.ticks(2);
    auto* p = playerById(spId);
    mount  = mobById(mid);
    ctx.require(p && mount, "player and pig present");
    ctx.check(p->startRiding(*mount, true) && p->getVehicle() == mount, "the player gets on by itself");
    ctx.check(ge::ext::isRidingUnfriendly(*p), "a marked IHostileMount type: riding unfriendly without a grab");
    // the pig takes the rider that got on by itself as held on its own next tick (tickMount); a
    // dismount before that tick is one Java would not see either (the player rode for no tick).
    // Both waits count server ticks (currentTick), not awaits, so a slow server gets as many.
    bool held = false;
    for (auto end = currentTick() + 20; !held && currentTick() < end;) {
        co_await ctx.ticks(1);
        auto* s = findState(mid);
        held    = s && s->grabbed == spId;
    }
    ctx.require(held, "the pig holds the player that got on by itself");
    // preventMountDismount: the dismount itself is cancelled (Actor::stopRiding), no tick needed
    p     = playerById(spId);
    mount = mobById(mid);
    ctx.require(p && mount, "player and pig still present");
    auto counters = mountCountersForTest();
    p->stopRiding(true, false, false, false);
    ctx.check(mountCountersForTest().cancelledDismounts == counters.cancelledDismounts + 1, "the dismount was cancelled");
    ctx.check(p->getVehicle() == mount, "it can't get off either");
    // a dismount that bypasses stopRiding: the pig puts the player back on its next ticks, and the
    // player counts as held (no teleport) until it is back
    leaveSeatForTest(*p);
    ctx.check(p->getVehicle() == nullptr, "the player is off the seat");
    ctx.check(heldUnfriendly(*p), "a player off the seat while held still counts as held");
    int  blocked = blockedTeleportsForTest();
    auto before  = p->getFeetPos();
    p->teleportTo(Vec3{before.x + 40.0f, before.y, before.z}, true, 1 /* projectile */, 0, false);
    ctx.check(blockedTeleportsForTest() == blocked + 1 && dist(p->getFeetPos(), before) < 4.0, "a player off the seat while held can't teleport");
    counters  = mountCountersForTest();
    bool back = false;
    for (auto end = currentTick() + 20; !back && currentTick() < end;) {
        co_await ctx.ticks(1);
        p     = playerById(spId);
        mount = mobById(mid);
        ctx.require(p && mount, "player and pig still present");
        back = p->getVehicle() == mount;
    }
    auto after = mountCountersForTest();
    ctx.info("put-back: " + std::to_string(after.putBackAttempts - counters.putBackAttempts) + " attempts, " + std::to_string(after.failedPutBacks - counters.failedPutBacks) + " failed");
    ctx.check(back, "a rider off the seat is put back");
    ge::ext::hostileDismount(*p);
    co_await ctx.ticks(2);
    if ((p = playerById(spId))) ctx.check(p->getVehicle() == nullptr, "hostileDismount lets it go");
    // any living rider (EntityTeleportEvent of a LivingEntity): a pig in a boat marked as a hostile
    // mount is not teleported away, by a command nor by a random (enderman / chorus) teleport
    {
        struct BoatMark {
            BoatMark() { ge::ext::markHostileMount("minecraft:boat"); }
            ~BoatMark() { unmarkHostileMountForTest("minecraft:boat"); }
        } boatMark;
        auto* boat = spawnActor(at.offset(3, 0, 3), "minecraft:boat");
        auto* pig  = spawnMob(at.offset(3, 0, 1), "minecraft:pig");
        ctx.require(boat && pig, "a boat and a pig spawn");
        auto boatId = uidOf(*boat), pigId = uidOf(*pig);
        ctx.require(pig->startRiding(*boat, true) && pig->getVehicle() == boat, "the pig sits in the boat");
        ctx.check(ge::ext::isRidingUnfriendly(*pig), "a mob riding a hostile mount rides unfriendly");
        int  blocked = blockedTeleportsForTest();
        auto before  = pig->getFeetPos();
        pig->teleportTo(Vec3{before.x + 30.0f, before.y, before.z}, true, 3 /* command */, 0, false);
        bool hopped = pig->tryTeleportTo(Vec3{before.x + 20.0f, before.y, before.z}, false, true, 2 /* chorus fruit */, 0);
        co_await ctx.ticks(2);
        pig = mobById(pigId);
        ctx.require(pig != nullptr, "the pig is still there");
        ctx.check(blockedTeleportsForTest() >= blocked + 2 && !hopped, "both teleports were cancelled (" + std::to_string(blockedTeleportsForTest() - blocked) + ")");
        ctx.check(dist(pig->getFeetPos(), before) < 4.0, "the rider stayed with its mount");
        removeActor(pigId);
        removeActor(boatId);
    }
    removeActor(mid);
    (void)box;
});

// parts added by a behaviour inside the actor's own tick (onSpawn), as the boss units do
GF_REGISTER_SELFTEST("entity.multipart_in_tick", [](Context& ctx) -> ll::coro::CoroTask<> {
    FixtureScope    fixture;
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

// Spike S4, custom hit test: Bedrock's HitboxComponent (minecraft:custom_hit_test) keeps extra
// pick boxes on one actor (the hoglin declares one), moved with it by the engine. A pick on such a
// box reaches the server as a hit on the actor itself, without telling which box: upstream's
// attackEntityFromPart needs the part, so the parts stay linked actors (their own pick boxes).
GF_REGISTER_SELFTEST("entity.spike_hitbox", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box = co_await ctx.flatArea(5836, 4);
    auto            at  = ctx.testPos(5837, 2);
    auto*           hog = spawnMob(at, "minecraft:hoglin");
    ctx.require(hog != nullptr, "a hoglin spawns");
    auto uid = uidOf(*hog);
    co_await ctx.ticks(2);
    hog = mobById(uid);
    ctx.require(hog != nullptr, "the hoglin is still there");
    auto hits = hog->getEntityContext().tryGetComponent<HitboxComponent>();
    ctx.require(static_cast<bool>(hits), "the hoglin has a HitboxComponent (minecraft:custom_hit_test)");
    auto const& list = hits->mHitboxes.get();
    ctx.check(list.size() == 1, "one custom hit box (" + std::to_string(list.size()) + ")");
    if (!list.empty()) {
        auto const& aabb = list[0].mAabb.get();
        auto        feet = hog->getFeetPos();
        ctx.info("hoglin hit box " + std::to_string(aabb.max.x - aabb.min.x) + " x " + std::to_string(aabb.max.y - aabb.min.y) + " at y " + std::to_string(aabb.min.y - feet.y));
        ctx.check(aabb.min.x <= feet.x && aabb.max.x >= feet.x && aabb.min.z <= feet.z && aabb.max.z >= feet.z, "the box follows the actor (world coordinates around it)");
    }
    removeActor(uid);
    (void)box;
});

// a parent without its own `<id>_part` type gets F8's invisible hitbox part
GF_REGISTER_SELFTEST("entity.multipart_generic", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box   = co_await ctx.flatArea(5839, 4);
    auto            at    = ctx.testPos(5839, 2);
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

// The move controllers: NoClip steers a mob that has an engine navigation too (its goals' moveTo
// sets the wanted position instead of a path), and FlyingMoveControl moves a mob F8 steers
// towards its wanted position, climbing without gravity
GF_REGISTER_SELFTEST("entity.move_controls", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box = co_await ctx.flatArea(5848, 6);
    auto            at  = ctx.testPos(5849, 2);
    // NoClip on a pig (a mob with a NavigationComponent)
    auto* pig = spawnMob(at, "minecraft:pig");
    ctx.require(pig != nullptr, "a pig spawns");
    auto pigId = uidOf(*pig);
    ge::setMoveControl(*pig, ge::MoveKind::NoClip);
    Vec3 goal{at.x + 3.5f, at.y + 2.0f, at.z + 0.5f};
    mobs::moveTo(*pig, goal, 1.0);
    auto wanted = ge::ext::wantedPosition(*pig);
    ctx.check(wanted && dist(*wanted, goal) < 0.01, "moveTo of a NoClip mob sets the NoClipMoveControl's wanted position");
    ctx.check(!mobs::navigationDone(*pig), "and the move is under way");
    removeActor(pigId);
    // FlyingMoveControl on actors without navigation: the engine integrates their motion by type
    // (a no-gravity fixture moves along the ground plane, an armor stand only vertically), so the
    // horizontal steering is read on the fixture and the climb against gravity on the armor stand
    struct Flight {
        std::int64_t id = 0;
        Vec3         start{}, goal{};
    };
    auto launch = [&](char const* type, core::GlobalPos const& p, float dy) -> Flight {
        auto* m = spawnMob(p, type, dy);
        if (!m) return {};
        ge::setMoveControl(*m, ge::MoveKind::Flying);
        auto s = m->getFeetPos();
        Vec3 g{s.x + 3.0f, s.y + 2.0f, s.z};
        ge::ext::setWantedPosition(*m, g, 1.0);
        return {uidOf(*m), s, g};
    };
    // Attributes.FLYING_SPEED in the air (FlyingMoveControl): the type's (DeathTome 0.6), else the
    // attribute's default 0.4
    struct FlyScope {
        FlyScope() { ge::ext::setFlyingSpeed("gf_test:boss_part", 0.6); }
        ~FlyScope() { ge::ext::setFlyingSpeed("gf_test:boss_part", 0.0); }
    } fly;
    auto glide = launch("gf_test:boss_part", at.offset(0, 0, 2), 1.0f);
    auto climb = launch("minecraft:armor_stand", at.offset(2, 0, 4), 0.0f);
    ctx.require(glide.id && climb.id, "the flyers spawn");
    if (auto* gm = mobById(glide.id)) ctx.check(ge::ext::flyingSpeed(*gm) == 0.6, "the flying speed of a type that set one (0.6)");
    if (auto* cm = mobById(climb.id)) {
        double fs = ge::ext::flyingSpeed(*cm);
        ctx.check(fs == 0.4, "a type without one flies with the attribute's default 0.4 (" + std::to_string(fs) + ")");
    }
    for (int i = 0; i < 60; ++i) co_await ctx.ticks(1);
    auto* g = mobById(glide.id);
    auto* c = mobById(climb.id);
    ctx.require(g && c, "the flyers are still there");
    auto gp = g->getFeetPos(), cp = c->getFeetPos();
    double gx0 = std::abs(glide.goal.x - glide.start.x), gx1 = std::abs(glide.goal.x - gp.x);
    ctx.check(gx1 < gx0 - 1.0, "it flew towards the wanted position (" + std::to_string(gx0) + " -> " + std::to_string(gx1) + " blocks)");
    ctx.check(cp.y > climb.start.y + 0.3f, "climbing against gravity (" + std::to_string(cp.y - climb.start.y) + ")");
    removeActor(glide.id);
    removeActor(climb.id);
    (void)box;
});

// SynchedEntityData#startSeenByPlayer: every synced value (a string list here) and the part list
// reach a companion that joins after they were set, and one that comes into range later; a change
// reaches the companions that already see the actor
namespace {
bool hasList(std::vector<core::Envelope> const& msgs, std::int64_t uid, std::string const& key, std::vector<std::string> const& list) {
    for (auto const& m : msgs)
        if (m.channel == "entity.data" && m.payload.value("id", std::int64_t{0}) == uid && m.payload.value("key", std::string()) == key && m.payload.value("data", ge::Json()) == ge::Json(list))
            return true;
    return false;
}
bool hasParts(std::vector<core::Envelope> const& msgs, std::int64_t uid, std::size_t count) {
    for (auto const& m : msgs)
        if (m.channel == "entity.multipart" && m.payload.value("parent", std::int64_t{0}) == uid && m.payload.value("parts", ge::Json::array()).size() == count) return true;
    return false;
}
} // namespace

GF_REGISTER_SELFTEST("entity.sync_late", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto at = ctx.testPos(5884, 2);
    ctx.require(co_await ctx.ensureLoaded(at), "test area loaded");
    auto* mob = spawnMob(at, "gf_test:boss_part");
    ctx.require(mob != nullptr, "the synced actor spawns");
    auto              uid = uidOf(*mob);
    std::string const key = "gf_test:heads";
    ge::ext::setStringList(*mob, key, {"left", "center", "right"});
    ctx.require(ge::addPart(*mob, "probe", {0.5f, 0.5f}, [](Mob&) { return Vec3{0.0f, 2.0f, 0.0f}; }).valid(), "the actor gets a part");
    std::vector<std::int64_t> companions;
    struct CompanionScope {
        std::vector<std::int64_t>& ids;
        ~CompanionScope() {
            for (auto id : ids)
                if (auto* p = playerById(id)) gf::net::setTestCompanion(*p, false);
        }
    } scope{companions};
    // a companion that joins after the list was set
    auto& near   = ctx.spawnPlayer("gf_sync_near");
    auto  nearId = near.getOrCreateUniqueID().rawID;
    near.teleport(Vec3{at.x + 3.5f, float(at.y), at.z + 0.5f}, DimensionType(at.dim));
    co_await ctx.ticks(2);
    auto* p = playerById(nearId);
    ctx.require(p != nullptr, "the joining player is there");
    companions.push_back(nearId);
    gf::net::joinTestCompanion(*p);
    auto msgs = gf::net::takeTestMessages(*p);
    ctx.check(hasList(msgs, uid, key, {"left", "center", "right"}), "a companion that joins after setStringList gets the list");
    ctx.check(hasParts(msgs, uid, 1), "and the part list");
    // a change reaches it once
    if (auto* m = mobById(uid)) ge::ext::setStringList(*m, key, {"left"});
    if ((p = playerById(nearId))) {
        msgs = gf::net::takeTestMessages(*p);
        ctx.check(hasList(msgs, uid, key, {"left"}) && !hasParts(msgs, uid, 1), "a change reaches a companion that sees the actor (the change only)");
    }
    // a companion out of range gets the state when it comes into range
    auto& far   = ctx.spawnPlayer("gf_sync_far");
    auto  farId = far.getOrCreateUniqueID().rawID;
    far.teleport(Vec3{at.x + 200.5f, float(at.y), at.z + 0.5f}, DimensionType(at.dim));
    co_await ctx.ticks(2);
    ctx.require((p = playerById(farId)) != nullptr, "the far player is there");
    companions.push_back(farId);
    gf::net::setTestCompanion(*p, true);
    co_await ctx.ticks(25);
    ctx.require((p = playerById(farId)) != nullptr, "the far player is still there");
    msgs = gf::net::takeTestMessages(*p);
    ctx.check(!hasList(msgs, uid, key, {"left"}), "nothing for a companion 200 blocks away");
    p->teleport(Vec3{at.x + 4.5f, float(at.y), at.z + 1.5f}, DimensionType(at.dim));
    co_await ctx.ticks(3); // looked at every tick: the state comes with the actor (Java: with its spawn packet)
    ctx.require((p = playerById(farId)) != nullptr, "the far player came near");
    msgs = gf::net::takeTestMessages(*p);
    ctx.check(hasList(msgs, uid, key, {"left"}) && hasParts(msgs, uid, 1), "a companion that comes into range gets the list and the parts within 3 ticks (startSeenByPlayer)");
    ctx.check(viewerCountForTest(uid) == 2, "both companions see the actor (" + std::to_string(viewerCountForTest(uid)) + ")");
    removeActor(uid);
    co_await ctx.ticks(2);
});

// The persisted data survives a world reload (server scenario: write, Restart-Server, read).
// entity.persist_write leaves a marker actor and a world DB record; entity.persist_read (after the
// restart) requires the marker with the same nonce and removes both. Without a record (a plain
// "entity.*" run sorts the read before the write) the read has nothing to check; a marker no read
// collects (the 'full' run) is removed after two minutes.
namespace {
constexpr char const* kPersistTag = "gf_entity_persist";
constexpr char const* kPersistKey = "gf:entity:persist";
constexpr int         kPersistTtl = 2400;

AABB around(core::GlobalPos const& at) {
    return AABB{Vec3{at.x - 4.0f, at.y - 8.0f, at.z - 4.0f}, Vec3{at.x + 5.0f, at.y + 8.0f, at.z + 5.0f}};
}

struct PersistExpiry {
    std::int64_t  uid = 0;
    long long     nonce = 0;
    std::uint64_t until = 0;
};
std::vector<PersistExpiry>& expiries() {
    static std::vector<PersistExpiry> v;
    static bool                       once = false;
    if (!std::exchange(once, true))
        onLevelTick([] {
            auto& list = v;
            auto  now  = currentTick();
            std::erase_if(list, [&](PersistExpiry const& e) {
                if (now < e.until) return false;
                if (auto* a = actorById(e.uid)) a->remove();
                if (auto rec = game::db::get(kPersistKey); rec && rec->value("nonce", -1LL) == e.nonce) game::db::erase(kPersistKey);
                return true;
            });
        });
    return v;
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
    game::db::put(kPersistKey, {{"nonce", nonce}, {"pos", {at.dim, at.x, at.y, at.z}}});
    expiries().push_back({uidOf(*marker), nonce, currentTick() + kPersistTtl});
    ctx.info("persist marker written nonce=" + std::to_string(nonce));
    co_await ctx.ticks(2);
});

GF_REGISTER_SELFTEST("entity.persist_read", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto record = game::db::get(kPersistKey);
    if (!record) {
        ctx.info("no persist marker (entity.persist_write did not run before): nothing to check");
        co_return;
    }
    game::db::erase(kPersistKey);
    auto nonce = record->value("nonce", -1LL);
    auto p     = record->value("pos", ge::Json::array());
    ctx.require(nonce >= 0 && p.is_array() && p.size() == 4, "the marker record is complete");
    core::GlobalPos at{p[0].get<int>(), p[1].get<int>(), p[2].get<int>(), p[3].get<int>()};
    ctx.require(co_await ctx.ensureLoaded(at), "test area loaded");
    std::int64_t markerId = 0;
    for (int i = 0; i < 60 && !markerId; ++i) {
        co_await ctx.ticks(1);
        if (auto* region = game::regionOf(at.dim))
            for (auto* a : region->getEntities(around(at), 0.0f))
                if (a && a->hasTag(kPersistTag)) markerId = uidOf(*a);
    }
    auto* marker = actorById(markerId);
    ctx.require(marker != nullptr, "the persist marker written before came back");
    auto seen = ge::data(*marker, "persist").value("nonce", -1LL);
    std::optional<ge::Home> h;
    if (auto* m = asMob(*marker)) h = ge::home(*m);
    ctx.check(seen == nonce, "the marker's data came back from the save (" + std::to_string(seen) + ")");
    ctx.check(h && h->pos == at && h->radius == 7, "the marker's home came back from the save");
    if (seen == nonce && ctx.failures().empty()) ctx.info("persist marker verified nonce=" + std::to_string(nonce));
    marker->remove();
});


// Monster#shouldDespawnInPeaceful through the kit: every non-boss upstream monster is marked, bosses and
// the loyal zombie are not; a marked type is removed at its first tick on Peaceful (no death) and
// kept on Easy
GF_REGISTER_SELFTEST("entity.peaceful", [](Context& ctx) -> ll::coro::CoroTask<> {
    for (auto const* id : {"gloamingforest:redcap", "gloamingforest:hostile_wolf", "gloamingforest:carminite_ghastguard",
                           "gloamingforest:kobold", "gloamingforest:yeti", "gloamingforest:wraith"})
        ctx.check(ge::ext::despawnsInPeaceful(id), std::string(id) + " leaves on Peaceful");
    for (auto const* id : {"gloamingforest:naga", "gloamingforest:alpha_yeti", "gloamingforest:loyal_zombie", "gloamingforest:boar",
                           "gloamingforest:penguin"})
        ctx.check(!ge::ext::despawnsInPeaceful(id), std::string(id) + " stays on Peaceful (boss kit / not a monster)");
    struct Unmark {
        ~Unmark() { ge::ext::setDespawnInPeaceful("gf_test:boss", false); }
    } unmark;
    ge::ext::setDespawnInPeaceful("gf_test:boss", true);
    ctx.check(ge::ext::despawnsInPeaceful("gf_test:boss"), "a unit can mark a type");
    DifficultyScope scope(Difficulty::Easy);
    auto            at   = ctx.testPos(5878, 2);
    auto*           kept = spawnMob(at, "gf_test:boss");
    ctx.require(kept != nullptr, "the fixture spawns on Easy");
    auto keptId = uidOf(*kept);
    co_await ctx.ticks(3);
    ctx.check(mobById(keptId) != nullptr, "a marked mob stays on Easy");
    scope.set(Difficulty::Peaceful);
    co_await ctx.ticks(3);
    auto* gone = mobById(keptId);
    ctx.check(gone == nullptr || !gone->isAlive(), "a marked mob leaves on Peaceful");
    if (gone) gone->remove();
    ge::ext::setDespawnInPeaceful("gf_test:boss", false);
    auto* stays = spawnMob(at, "gf_test:boss");
    ctx.require(stays != nullptr, "the fixture spawns");
    auto staysId = uidOf(*stays);
    co_await ctx.ticks(3);
    ctx.check(mobById(staysId) != nullptr, "an unmarked type stays on Peaceful");
    removeActor(staysId);
});

// EntityUtil#canDestroyBlock (SpareContainers) and ChargeAttackGoal's "no block entity" rule
// (SpareBlockEntities), and destroyBlocks over a box
GF_REGISTER_SELFTEST("entity.block_breaking", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box    = co_await ctx.flatArea(5880, 6);
    auto at     = ctx.testPos(5880, 1).offset(0, 1, 0);
    auto region = ctx.region();
    ctx.require(region != nullptr, "the test dimension");
    auto place = [&](int dx, char const* id) {
        auto p = at.offset(dx, 0, 0);
        ctx.require(ctx.setBlock(p, id), std::string("place ") + id);
        return game::blockPos(p);
    };
    auto stone   = place(0, "minecraft:stone");
    auto chest   = place(1, "minecraft:chest");
    auto sign    = place(2, "minecraft:standing_sign");
    auto bedrock = place(3, "minecraft:bedrock");
    auto gloamingChest = place(4, "gloamingforest:gloaming_oak_chest");
    auto jar     = place(5, "gloamingforest:firefly_jar");
    BlockPos air{stone.x, stone.y + 1, stone.z};
    using R = ge::ext::BreakRule;
    ctx.check(ge::ext::canDestroyBlock(*region, stone, R::SpareContainers), "stone breaks");
    ctx.check(!ge::ext::canDestroyBlock(*region, air, R::SpareContainers), "air never");
    ctx.check(!ge::ext::canDestroyBlock(*region, bedrock, R::SpareContainers), "bedrock never (hardness < 0)");
    ctx.check(!ge::ext::canDestroyBlock(*region, chest, R::SpareContainers), "a container is spared");
    ctx.check(ge::ext::canDestroyBlock(*region, sign, R::SpareContainers), "a sign breaks (not a Container)");
    ctx.check(!ge::ext::canDestroyBlock(*region, sign, R::SpareBlockEntities), "the charge spares a sign (a block entity)");
    ctx.check(!ge::ext::canDestroyBlock(*region, gloamingChest, R::SpareContainers), "an upstream chest is spared (gf::inventory container)");
    ctx.check(ge::ext::canDestroyBlock(*region, jar, R::SpareContainers), "a firefly jar breaks for the hydra (no Container)");
    ctx.check(!ge::ext::canDestroyBlock(*region, jar, R::SpareBlockEntities), "the charge spares a firefly jar (Java block entity)");
    int broken = ge::ext::destroyBlocks(*region, stone, BlockPos{jar.x, jar.y, jar.z}, R::SpareBlockEntities, false);
    ctx.check(broken == 1, "the charge breaks only the stone (" + std::to_string(broken) + ")");
    ctx.check(ctx.blockAt(at) == "minecraft:air", "the stone is gone: " + ctx.blockAt(at));
    ctx.check(ctx.blockAt(at.offset(2, 0, 0)) == "minecraft:standing_sign", "the sign stays");
    broken = ge::ext::destroyBlocks(*region, stone, BlockPos{jar.x, jar.y, jar.z}, R::SpareContainers, false);
    ctx.check(broken == 2, "the hydra's rule breaks the sign and the jar (" + std::to_string(broken) + ")");
    ctx.check(ctx.blockAt(at.offset(1, 0, 0)) == "minecraft:chest", "the chest stays");
    ctx.check(ctx.blockAt(at.offset(4, 0, 0)) == "gloamingforest:gloaming_oak_chest", "the upstream chest stays");
    (void)box;
});

} // namespace gf::feature::entity::tests
