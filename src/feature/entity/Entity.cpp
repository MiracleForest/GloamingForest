// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/EnforcedHomePoint.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Contract "entity" (F8): the behaviour registry and the one set of actor hooks of the mod
// (rule R8): tick (Actor::tick), death (Mob::die), persistence (Actor::save / Actor::load) and
// target changes (Mob::setTarget). Incoming damage reaches Behaviour::hurt through gf::combat and
// right-clicks through gf::interactions. Per-actor state is keyed by ActorUniqueID and looked up
// again every tick (rule R16); the persisted part (data(actor, key)) is written into the actor's
// own save tag, so it travels with the entity through chunk unloads and world reloads.

#include "feature/entity/Entity.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/legacy/ActorRuntimeID.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/SetActorMotionPacket.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/state/ActorPropertyUtils.h"
#include "mc/world/actor/state/PropertyComponent.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"

#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/entity/EntityInternal.h"
#include "feature/net/Net.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::entity {

namespace ia = gf::interactions;

// ---- java.util.Random -------------------------------------------------------------------------

void JavaRandom::setSeed(std::int64_t seed) {
    mSeed         = (seed ^ 0x5DEECE66DLL) & ((1LL << 48) - 1);
    mHaveGaussian = false;
}
int JavaRandom::next(int bits) {
    mSeed = (mSeed * 0x5DEECE66DLL + 0xBLL) & ((1LL << 48) - 1);
    return static_cast<int>(static_cast<std::int64_t>(static_cast<std::uint64_t>(mSeed) >> (48 - bits)));
}
int JavaRandom::nextInt() { return next(32); }
int JavaRandom::nextInt(int bound) {
    if (bound <= 0) return 0;
    if ((bound & -bound) == bound) return static_cast<int>((static_cast<std::int64_t>(bound) * next(31)) >> 31);
    int bits, val;
    do {
        bits = next(31);
        val  = bits % bound;
    } while (bits - val + (bound - 1) < 0);
    return val;
}
std::int64_t JavaRandom::nextLong() { return (static_cast<std::int64_t>(next(32)) << 32) + next(32); }
bool         JavaRandom::nextBoolean() { return next(1) != 0; }
float        JavaRandom::nextFloat() { return static_cast<float>(next(24)) / static_cast<float>(1 << 24); }
double       JavaRandom::nextDouble() {
    return static_cast<double>((static_cast<std::int64_t>(next(26)) << 27) + next(27)) * 0x1.0p-53;
}
double JavaRandom::nextGaussian() {
    if (mHaveGaussian) {
        mHaveGaussian = false;
        return mNextGaussian;
    }
    double v1, v2, s;
    do {
        v1 = 2 * nextDouble() - 1;
        v2 = 2 * nextDouble() - 1;
        s  = v1 * v1 + v2 * v2;
    } while (s >= 1 || s == 0);
    double multiplier = std::sqrt(-2 * std::log(s) / s);
    mNextGaussian     = v2 * multiplier;
    mHaveGaussian     = true;
    return v1 * multiplier;
}

std::int64_t freshSeed() {
    static std::mt19937_64 rng{std::random_device{}()};
    return static_cast<std::int64_t>(rng());
}

// ---- actors -----------------------------------------------------------------------------------

std::int64_t uidOf(Actor const& actor) { return actor.getOrCreateUniqueID().rawID; }

Actor* actorById(std::int64_t uid) {
    if (!uid) return nullptr;
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID id;
    id.rawID = uid;
    return level->fetchEntity(id, false);
}

Player* playerById(std::int64_t uid) {
    auto* a = actorById(uid);
    return a && a->isPlayer() ? static_cast<Player*>(a) : nullptr;
}

Mob* asMob(Actor& actor) { return actor.hasCategory(ActorCategory::Mob) ? static_cast<Mob*>(&actor) : nullptr; }

std::uint64_t currentTick() {
    auto level = ll::service::getLevel();
    return level ? level->getCurrentServerTick().tickID : 0;
}

bool isServerActor(Actor const& actor) {
    auto level = ll::service::getLevel();
    return level && &actor.getLevel() == level.as_ptr();
}

void setMotion(Actor& actor, Vec3 const& motion) {
    const_cast<Vec3&>(actor.getPosDelta()) = motion;
    if (actor.isPlayer()) {
        auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::SetActorMotion);
        if (!packet) return;
        auto& pkt      = static_cast<SetActorMotionPacket&>(*packet);
        pkt.mRuntimeId = actor.getRuntimeID();
        pkt.mMotion    = motion;
        pkt.sendTo(static_cast<Player&>(actor));
    }
}

void push(Actor& actor, Vec3 const& delta) {
    auto v = actor.getPosDelta();
    setMotion(actor, Vec3{v.x + delta.x, v.y + delta.y, v.z + delta.z});
}

std::vector<Player*> playersNear(int dim, Vec3 const& center, double radius) {
    std::vector<Player*> out;
    auto                 level = ll::service::getLevel();
    if (!level) return out;
    double r2 = radius * radius;
    level->forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) != dim || !p.isAlive()) return true;
        auto   pos = p.getFeetPos();
        double dx  = pos.x - center.x, dy = pos.y - center.y, dz = pos.z - center.z;
        if (dx * dx + dy * dy + dz * dz <= r2) out.push_back(&p);
        return true;
    }));
    return out;
}

std::string damageTypeOf(int cause, bool byPlayer) {
    using C = SharedTypes::Legacy::ActorDamageCause;
    switch (static_cast<C>(cause)) {
    case C::Contact: return "minecraft:cactus";
    case C::EntityAttack: return byPlayer ? "minecraft:player_attack" : "minecraft:mob_attack";
    case C::Projectile: return "minecraft:arrow";
    case C::Suffocation: return "minecraft:in_wall";
    case C::Fall: return "minecraft:fall";
    case C::Fire: return "minecraft:in_fire";
    case C::FireTick: return "minecraft:on_fire";
    case C::Lava: return "minecraft:lava";
    case C::Drowning: return "minecraft:drown";
    case C::BlockExplosion: return "minecraft:explosion";
    case C::EntityExplosion: return byPlayer ? "minecraft:player_explosion" : "minecraft:explosion";
    case C::Void: return "minecraft:out_of_world";
    case C::Magic: return "minecraft:magic";
    case C::Wither: return "minecraft:wither";
    case C::Starve: return "minecraft:starve";
    case C::Anvil: return "minecraft:falling_anvil";
    case C::Thorns: return "minecraft:thorns";
    case C::FallingBlock: return "minecraft:falling_block";
    case C::FlyIntoWall: return "minecraft:fly_into_wall";
    case C::Magma: return "minecraft:hot_floor";
    case C::Fireworks: return "minecraft:fireworks";
    case C::Lightning: return "minecraft:lightning_bolt";
    case C::Freezing: return "minecraft:freeze";
    case C::Stalactite: return "minecraft:falling_stalactite";
    case C::Stalagmite: return "minecraft:stalagmite";
    case C::SonicBoom: return "minecraft:sonic_boom";
    case C::Campfire:
    case C::SoulCampfire: return "minecraft:campfire";
    case C::MaceSmash: return "minecraft:mace_smash";
    default: return "minecraft:generic_kill";
    }
}

Vec3 eyeOf(Actor const& actor) { return actor.getEyePos(); }

// ---- registry and state -----------------------------------------------------------------------

namespace {

constexpr char const* kSaveTag = "GloamingForest";

struct Registry {
    std::unordered_map<std::string, gf::entity::Behaviour>       behaviours;
    std::vector<InternalHooks>                                   internal;
    std::vector<std::function<bool(Mob&, Actor&)>>               targetFilters;
    std::vector<std::function<void()>>                           levelTicks;
    std::unordered_map<std::int64_t, std::unique_ptr<ActorState>> states;
    std::vector<std::int64_t>                                    removals;
};
Registry& reg() {
    static Registry r;
    return r;
}

bool interesting(std::string const& type) {
    auto& r = reg();
    if (r.behaviours.contains(type)) return true;
    for (auto const& h : r.internal)
        if (h.interesting && h.interesting(type)) return true;
    return false;
}

/// One error line per (place, type): a failing unit callback must not flood the log.
void reportError(char const* where, std::string const& type, char const* what) {
    static std::set<std::string> seen;
    if (seen.insert(std::string(where) + "|" + type).second) log().error("entity: {} of {} failed: {}", where, type, what);
}

template <class Fn>
void guarded(char const* where, std::string const& type, Fn&& fn) {
    try {
        fn();
    } catch (std::exception const& e) {
        reportError(where, type, e.what());
    } catch (...) {
        reportError(where, type, "unknown exception");
    }
}

/// Every server tick: the actor's behaviour, the internal hooks, goals, parts and mount.
void onActorTick(Actor& actor) {
    if (!isServerActor(actor)) return;
    auto&        r    = reg();
    std::int64_t uid  = uidOf(actor);
    auto         it   = r.states.find(uid);
    auto const&  type = actor.getTypeName();
    if (it == r.states.end() && !interesting(type)) return;
    ActorState& s = it != r.states.end() ? *it->second : stateOf(actor);
    s.lastSeen    = currentTick();
    if (s.typeId.empty()) s.typeId = type;
    auto bit                       = r.behaviours.find(type);
    gf::entity::Behaviour const* b = bit != r.behaviours.end() ? &bit->second : nullptr;
    if (!s.spawned) {
        s.spawned = true;
        if (b && b->onSpawn) guarded("onSpawn", type, [&] { b->onSpawn(actor); });
        for (auto const& h : r.internal)
            if (h.spawn) guarded("spawn", type, [&] { h.spawn(actor, s); });
    }
    if (b && b->tick) guarded("tick", type, [&] { b->tick(actor); });
    if (!actor.isAlive() && !asMob(actor)) return;
    if (auto* mob = asMob(actor)) {
        if (mob->isAlive()) guarded("goals", type, [&] { tickGoals(*mob, s); });
        if (b && b->aiStep) guarded("aiStep", type, [&] { b->aiStep(actor); });
        if (!s.parts.empty()) guarded("parts", type, [&] { followParts(*mob, s); });
        if (s.hostileMount) guarded("mount", type, [&] { tickMount(*mob, s); });
    }
    for (auto const& h : r.internal)
        if (h.tick) guarded("internal tick", type, [&] { h.tick(actor, s); });
}

void onActorDie(Mob& mob, ActorDamageSource const& source) {
    if (!isServerActor(mob)) return;
    auto&       r    = reg();
    auto const& type = mob.getTypeName();
    if (!r.states.contains(uidOf(mob)) && !interesting(type)) return;
    Actor* killer = nullptr;
    if (source.isEntitySource() || source.isChildEntitySource()) killer = actorById(source.getEntityUniqueID().rawID);
    bool        byPlayer = killer && killer->isPlayer();
    std::string dtype    = damageTypeOf(static_cast<int>(source.mCause), byPlayer);
    if (auto bit = r.behaviours.find(type); bit != r.behaviours.end() && bit->second.die)
        guarded("die", type, [&] { bit->second.die(mob, dtype, killer); });
    for (auto const& h : r.internal)
        if (h.die) guarded("internal die", type, [&] { h.die(mob, dtype, killer); });
}

bool shouldSave(Actor const& actor) {
    for (auto const& h : reg().internal)
        if (h.save && !h.save(actor)) return false;
    return true;
}

void writeSave(Actor const& actor, CompoundTag& tag) {
    auto& r  = reg();
    auto  it = r.states.find(uidOf(actor));
    if (it == r.states.end()) return;
    auto& s  = *it->second;
    auto  ty = actor.getTypeName();
    if (auto bit = r.behaviours.find(ty); bit != r.behaviours.end() && bit->second.save) {
        Json extra = Json::object();
        guarded("save", ty, [&] { bit->second.save(const_cast<Actor&>(actor), extra); });
        if (!extra.is_null() && !extra.empty()) s.data["entity:save"] = std::move(extra);
    }
    for (auto const& h : r.internal)
        if (h.persist) guarded("persist", ty, [&] { h.persist(actor, s); });
    if (s.data.is_object() && !s.data.empty()) tag.putString(kSaveTag, s.data.dump());
}

void readSave(Actor& actor, CompoundTag const& tag) {
    if (!tag.contains(kSaveTag)) return;
    auto const& v = tag[kSaveTag];
    if (!v.hold(Tag::Type::String)) return;
    auto j = Json::parse(std::string(v.get<StringTag>()), nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;
    auto& s  = stateOf(actor);
    s.data   = std::move(j);
    s.loaded = true;
    auto ty  = actor.getTypeName();
    if (auto bit = reg().behaviours.find(ty); bit != reg().behaviours.end() && bit->second.load) {
        Json extra = s.data.contains("entity:save") ? s.data["entity:save"] : Json::object();
        guarded("load", ty, [&] { bit->second.load(actor, extra); });
    }
}

bool targetAllowed(Mob& mob, Actor* target) {
    if (!target) return true;
    for (auto const& f : reg().targetFilters)
        if (f && !f(mob, *target)) return false;
    return true;
}

/// Drops the state of actors that are gone (removed or unloaded long enough).
void sweep() {
    auto& r   = reg();
    auto  now = currentTick();
    std::erase_if(r.states, [&](auto const& kv) {
        if (actorById(kv.first)) return false;
        return now > kv.second->lastSeen + 200;
    });
}

std::vector<ll::event::ListenerPtr>& listeners() {
    static std::vector<ll::event::ListenerPtr> l;
    return l;
}
std::vector<ia::HandlerId>& handlers() {
    static std::vector<ia::HandlerId> h;
    return h;
}

} // namespace

ActorState& stateOf(Actor& actor) {
    auto& r   = reg();
    auto  uid = uidOf(actor);
    auto& p   = r.states[uid];
    if (!p) {
        p           = std::make_unique<ActorState>();
        p->typeId   = actor.getTypeName();
        p->lastSeen = currentTick();
    }
    return *p;
}

ActorState* findState(std::int64_t uid) {
    auto& r  = reg();
    auto  it = r.states.find(uid);
    return it == r.states.end() ? nullptr : it->second.get();
}

JavaRandom& randomOf(Actor& actor) {
    auto& s = stateOf(actor);
    if (!s.random) s.random = std::make_unique<JavaRandom>(freshSeed());
    return *s.random;
}

void addInternalHooks(InternalHooks hooks) { reg().internal.push_back(std::move(hooks)); }
void onLevelTick(std::function<void()> fn) { reg().levelTicks.push_back(std::move(fn)); }
void removeLater(std::int64_t uid) { reg().removals.push_back(uid); }
bool hasBehaviour(std::string const& typeId) { return reg().behaviours.contains(typeId); }

bool dispatchHurt(Actor& actor, float& amount, std::string const& damageType, Actor* attacker) {
    auto& r  = reg();
    auto  it = r.behaviours.find(actor.getTypeName());
    if (it == r.behaviours.end() || !it->second.hurt) return true;
    bool go = true;
    guarded("hurt", it->first, [&] { go = it->second.hurt(actor, amount, damageType, attacker); });
    return go;
}

} // namespace gf::feature::entity

// ---- hooks --------------------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(EntityTickHook, ll::memory::HookPriority::Normal, Actor, &Actor::tick, bool, BlockSource& region) {
    bool r = origin(region);
    if (!mRemoved) gf::feature::entity::onActorTick(*this);
    return r;
}

LL_TYPE_INSTANCE_HOOK(EntityDieHook, ll::memory::HookPriority::Normal, Mob, &Mob::$die, void, ActorDamageSource const& source) {
    origin(source);
    gf::feature::entity::onActorDie(*this, source);
}

LL_TYPE_INSTANCE_HOOK(EntitySaveHook, ll::memory::HookPriority::Normal, Actor, &Actor::save, bool, CompoundTag& tag) {
    if (gf::feature::entity::isServerActor(*this) && !gf::feature::entity::shouldSave(*this)) return false;
    bool ok = origin(tag);
    if (ok && gf::feature::entity::isServerActor(*this)) gf::feature::entity::writeSave(*this, tag);
    return ok;
}

LL_TYPE_INSTANCE_HOOK(
    EntityLoadHook,
    ll::memory::HookPriority::Normal,
    Actor,
    &Actor::$load,
    bool,
    CompoundTag const& tag,
    DataLoadHelper&    helper
) {
    bool ok = origin(tag, helper);
    if (ok && gf::feature::entity::isServerActor(*this)) gf::feature::entity::readSave(*this, tag);
    return ok;
}

LL_TYPE_INSTANCE_HOOK(EntitySetTargetHook, ll::memory::HookPriority::Normal, Mob, &Mob::$setTarget, void, Actor* target) {
    if (target && gf::feature::entity::isServerActor(*this) && !gf::feature::entity::targetAllowed(*this, target)) {
        origin(nullptr);
        return;
    }
    origin(target);
}

// ---- contract -----------------------------------------------------------------------------------

namespace gf::entity {

namespace fe = gf::feature::entity;

void registerType(std::string const& entityId, Behaviour behaviour) {
    auto& slot = fe::reg().behaviours[entityId];
    if (slot.onSpawn || slot.tick || slot.aiStep || slot.hurt || slot.die || slot.save || slot.load || slot.interact)
        log().warn("entity: behaviour of {} registered twice (the later one replaces it)", entityId);
    slot = std::move(behaviour);
}

Json& data(Actor& actor, std::string const& key) {
    auto& s = fe::stateOf(actor);
    if (!s.data.is_object()) s.data = Json::object();
    return s.data[key];
}

void sync(Actor& actor, std::string const& key) {
    auto& s = fe::stateOf(actor);
    Json  msg{
        {"id",   fe::uidOf(actor)                                     },
        {"rid",  static_cast<std::int64_t>(actor.getRuntimeID().rawID)},
        {"key",  key                                                  },
        {"data", s.data.contains(key) ? s.data[key] : Json()          },
    };
    // the players that see the actor (Java's tracking: the server view distance, 10 chunks)
    for (auto* p : fe::playersNear(static_cast<int>(actor.getDimensionId()), actor.getFeetPos(), 160.0))
        if (gf::net::isCompanion(*p)) gf::net::send(*p, "entity.data", msg);
}

void setProperty(Actor& actor, std::string const& name, PropertyValue const& value) {
    auto props = actor.getEntityContext().tryGetComponent<PropertyComponent>();
    if (!props) {
        static std::set<std::string> warned;
        if (warned.insert(actor.getTypeName()).second) log().warn("entity: {} declares no properties ({})", actor.getTypeName(), name);
        return;
    }
    std::variant<int, float, bool, std::string> v;
    std::visit([&](auto const& x) { v = x; }, value);
    if (!ActorPropertyUtils::tryQueueVariantValue(*props, actor, HashedString::computeHash(name), v)) {
        static std::set<std::string> warned;
        if (warned.insert(actor.getTypeName() + "|" + name).second)
            log().warn("entity: {} has no property {} of that type", actor.getTypeName(), name);
        return;
    }
    actor._applyPendingPropertyChanges();
}

void addGoal(Mob& mob, int priority, GoalPtr goal) {
    if (!goal) return;
    auto& s = fe::stateOf(mob);
    if (!s.goals) s.goals = std::make_unique<fe::GoalSelector>();
    s.goals->add(priority, std::move(goal));
}

void addTargetGoal(Mob& mob, int priority, GoalPtr goal) {
    if (!goal) return;
    auto& s = fe::stateOf(mob);
    if (!s.targets) s.targets = std::make_unique<fe::GoalSelector>();
    s.targets->add(priority, std::move(goal));
}

void setMoveControl(Mob& mob, MoveKind kind) {
    auto& s    = fe::stateOf(mob);
    s.moveKind = kind;
    s.moveFn   = nullptr;
}

void setMoveControl(Mob& mob, std::function<void(Mob& mob)> control) {
    auto& s    = fe::stateOf(mob);
    s.moveKind = std::nullopt;
    s.moveFn   = std::move(control);
}

// EnforcedHomePoint#saveHomePointToNbt / setRestrictionPoint: persisted with the actor.
void setHome(Mob& mob, core::GlobalPos const& home, int radius) {
    data(mob, "entity:home") = Json{
        {"dim",    home.dim},
        {"x",      home.x  },
        {"y",      home.y  },
        {"z",      home.z  },
        {"radius", radius  },
    };
}

std::optional<Home> home(Mob const& mob) {
    auto* s = fe::findState(fe::uidOf(mob));
    if (!s || !s->data.is_object() || !s->data.contains("entity:home")) return std::nullopt;
    auto const& h = s->data["entity:home"];
    if (!h.is_object()) return std::nullopt;
    Home out;
    out.pos    = {h.value("dim", 0), h.value("x", 0), h.value("y", 0), h.value("z", 0)};
    out.radius = h.value("radius", 0);
    return out;
}

void addTargetFilter(std::function<bool(Mob& attacker, Actor& candidate)> fn) {
    if (fn) fe::reg().targetFilters.push_back(std::move(fn));
}

void seedForTest(Actor& actor, std::int64_t seed) {
    auto& s = fe::stateOf(actor);
    if (!s.random) s.random = std::make_unique<fe::JavaRandom>(seed);
    else s.random->setSeed(seed);
}

} // namespace gf::entity

// ---- module ---------------------------------------------------------------------------------------

namespace gf::feature::entity {
namespace {

class EntityModule {
public:
    void install() {
        EntityTickHook::hook();
        EntityDieHook::hook();
        EntitySaveHook::hook();
        EntityLoadHook::hook();
        EntitySetTargetHook::hook();
        auto& bus = ll::event::EventBus::getInstance();
        listeners().push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](ll::event::ServerLevelTickEvent&) {
            for (auto const& fn : reg().levelTicks) guarded("level tick", "-", fn);
            if (!reg().removals.empty()) {
                auto ids = std::move(reg().removals);
                reg().removals.clear();
                for (auto id : ids)
                    if (auto* a = actorById(id)) a->remove();
            }
            if (currentTick() % 100 == 0) sweep();
        }));
        // Behaviour::interact (Java's mobInteract); a part forwards to its parent (TFPart#interact)
        handlers().push_back(ia::onInteract({}, [](ia::EntityContext const& c) {
            Actor* target = &c.entity;
            if (auto parent = parentOfPart(c.entity)) target = actorById(parent);
            if (!target) return ia::Result::Pass;
            auto it = reg().behaviours.find(target->getTypeName());
            if (it == reg().behaviours.end() || !it->second.interact) return ia::Result::Pass;
            bool used = false;
            guarded("interact", it->first, [&] { used = it->second.interact(*target, c.player); });
            return used ? ia::Result::Consume : ia::Result::Pass;
        }));
        // Behaviour::hurt runs inside the combat pipeline (rule R8: gf::combat owns the hurt hook;
        // the contract has no removal, so the handler is registered once per process)
        static bool combatRegistered = false;
        if (!std::exchange(combatRegistered, true)) gf::combat::onIncoming(gf::combat::priority::Normal, [](gf::combat::DamageInfo& d) {
            if (!hasBehaviour(d.victim.getTypeName())) return gf::combat::Verdict::Pass;
            return dispatchHurt(d.victim, d.amount, d.type, d.attacker) ? gf::combat::Verdict::Pass : gf::combat::Verdict::Cancel;
        });
        installMultipart();
        installMounts();
        static bool lifecycle = false;
        if (!std::exchange(lifecycle, true)) {
            game::db::onWorldClosed([] { reg().states.clear(); });
        }
    }
    void uninstall() {
        uninstallMounts();
        uninstallMultipart();
        for (auto id : handlers()) ia::remove(id);
        handlers().clear();
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : listeners()) bus.removeListener(l);
        listeners().clear();
        EntitySetTargetHook::unhook();
        EntityLoadHook::unhook();
        EntitySaveHook::unhook();
        EntityDieHook::unhook();
        EntityTickHook::unhook();
    }
};

} // namespace
} // namespace gf::feature::entity

GF_REGISTER_MODULE_ORDERED(gf::feature::entity::EntityModule, gf::ModuleOrder::Contracts);
