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
#include "mc/entity/components/IsDeadFlagComponent.h"
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
#include "mc/world/actor/item/FallingBlockActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/state/ActorPropertyUtils.h"
#include "mc/world/actor/state/PropertyComponent.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/entity/EntityInternal.h"
#include "feature/net/Net.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::entity {

namespace ia = gf::interactions;

// ---- random sources -----------------------------------------------------------------------------

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
        if (dx * dx + dy * dy + dz * dz < r2) out.push_back(&p); // Java's strict distance checks
        return true;
    }));
    return out;
}

std::vector<Actor*> actorsIn(BlockSource& region, int dim, AABB const& box) {
    auto out   = region.getEntities(box, 0.0f);
    auto level = ll::service::getLevel();
    if (!level) return out;
    level->forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) != dim || !p.isAlive()) return true;
        auto const& b = p.getAABB();
        bool        hit = b.min.x < box.max.x && b.max.x > box.min.x && b.min.y < box.max.y && b.max.y > box.min.y && b.min.z < box.max.z && b.max.z > box.min.z;
        if (hit && std::find(out.begin(), out.end(), &p) == out.end()) out.push_back(&p);
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
constexpr char const* kDiedKey = "entity:died"; ///< data(): the die hooks ran for this body (saved with it)

/// Notes an actor whose state companions get when they start seeing it (companion sync below).
void noteSynced(std::int64_t uid, ActorState const& s);

struct Registry {
    std::unordered_map<std::string, gf::entity::Behaviour>       behaviours;
    std::vector<InternalHooks>                                   internal;
    std::vector<std::function<bool(Mob&, Actor&)>>               targetFilters;
    std::vector<std::function<void()>>                           levelTicks;
    std::unordered_map<std::int64_t, std::unique_ptr<ActorState>> states;
    std::vector<std::int64_t>                                    removals;
    std::unordered_set<std::string>                              hostileMounts; ///< IHostileMount types
    std::unordered_set<std::string>                              noPortal;      ///< types whose canUsePortal is false (ext)
    std::int64_t                                                 portalWatch = 0;      ///< the actor whose portal queries are counted (selftests)
    int                                                          portalWatchCount = 0; ///< its portal queries so far
    /// Whether the tick hook looks at a type (one lookup per actor and tick instead of asking
    /// every registry); cleared when a registration changes.
    std::unordered_map<std::string, bool>                        interest;
};
Registry& reg() {
    static Registry r;
    return r;
}

bool computeInterest(std::string const& type) {
    auto& r = reg();
    if (r.behaviours.contains(type) || r.hostileMounts.contains(type)) return true;
    for (auto const& h : r.internal)
        if (h.interesting && h.interesting(type)) return true;
    return false;
}

bool interesting(std::string const& type) {
    auto& cache = reg().interest;
    if (auto it = cache.find(type); it != cache.end()) return it->second;
    bool yes = computeInterest(type);
    cache.emplace(type, yes);
    return yes;
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
    if (s.died && actor.isAlive() && actor.getHealth() > 0) s.died = false; // a respawned player
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
        if (s.hostileMount || r.hostileMounts.contains(type)) guarded("mount", type, [&] { tickMount(*mob, s); });
    }
    for (auto const& h : r.internal)
        if (h.tick) guarded("internal tick", type, [&] { h.tick(actor, s); });
}

/// Whether `mob` really died: the engine's death ran (dead flag, no health). A death the death
/// pipeline cancelled (gf::death::onLethal, F10's die hook inside this one) leaves it alive.
bool reallyDead(Mob& mob) {
    return mob.getEntityContext().hasComponent<IsDeadFlagComponent>() || mob.getHealth() <= 0 || !mob.isAlive();
}

void onActorDie(Mob& mob, ActorDamageSource const& source) {
    if (!isServerActor(mob)) return;
    auto&       r    = reg();
    auto const& type = mob.getTypeName();
    if (!r.states.contains(uidOf(mob)) && !interesting(type)) return;
    // once per life: the engine may call die again for a death already handled, also for a dead
    // body saved and loaded again with its chunk (the flag travels in the save tag)
    auto& state = stateOf(mob);
    if (state.died || !reallyDead(mob)) return;
    state.died = true;
    if (!mob.isPlayer() && state.data.is_object()) state.data[kDiedKey] = true;
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
        bool ok    = false;
        guarded("save", ty, [&] {
            bit->second.save(const_cast<Actor&>(actor), extra);
            ok = true;
        });
        // the save replaces the previous one: nothing saved now means nothing loaded later (a save
        // that failed keeps the last good one)
        if (ok && !extra.is_null() && !extra.empty()) s.data["entity:save"] = std::move(extra);
        else if (ok && s.data.is_object()) s.data.erase("entity:save");
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
    if (s.data.value(kDiedKey, false)) s.died = true; // a dead body saved with its chunk
    noteSynced(uidOf(actor), s);
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

// ---- companion sync (SynchedEntityData: every value again on startSeenByPlayer) -----------------

namespace {

constexpr char const* kSyncedKey  = "entity:synced"; ///< data(): the keys gf::entity::sync sent (persisted)
constexpr double      kTrackRange = 160.0;           ///< Java's tracking range of the bosses (10 chunks)

/// The companions that got an actor's synced state, by actor unique id.
std::unordered_map<std::int64_t, std::set<std::int64_t>>& viewers() {
    static std::unordered_map<std::int64_t, std::set<std::int64_t>> v;
    return v;
}

/// The actors with synced state (looked at every tick for new viewers).
std::set<std::int64_t>& syncedActors() {
    static std::set<std::int64_t> s;
    return s;
}

bool hasSyncedState(ActorState const& s) {
    if (!s.parts.empty()) return true;
    if (!s.data.is_object()) return false;
    auto it = s.data.find(kSyncedKey);
    return it != s.data.end() && it->is_array() && !it->empty();
}

void noteSynced(std::int64_t uid, ActorState const& s) {
    if (hasSyncedState(s)) syncedActors().insert(uid);
}

/// Every synced value of `actor` and its part list, for a companion that starts seeing it.
void sendAll(Player& player, Actor& actor, ActorState& s) {
    if (s.data.is_object())
        if (auto it = s.data.find(kSyncedKey); it != s.data.end() && it->is_array())
            for (auto const& key : *it)
                if (key.is_string()) gf::net::send(player, "entity.data", dataMessage(actor, s, key.get<std::string>()));
    if (!s.parts.empty())
        if (auto* mob = asMob(actor)) gf::net::send(player, "entity.multipart", partsMessage(*mob, s));
}

/// Looks for the companions that started seeing an actor with synced state (every tick, so the
/// state reaches a companion with the actor, as Java's SynchedEntityData does with the spawn
/// packet; at once for a companion that just joined). An actor that is not loaded forgets its
/// viewers: they get everything again when it comes back.
void trackAll() {
    auto& set = syncedActors();
    for (auto it = set.begin(); it != set.end();) {
        auto* state = findState(*it);
        if (!state || !hasSyncedState(*state)) {
            viewers().erase(*it);
            it = set.erase(it);
            continue;
        }
        if (auto* actor = actorById(*it)) updateViewers(*actor, *state, nullptr);
        else viewers().erase(*it);
        ++it;
    }
}

} // namespace

Json dataMessage(Actor& actor, ActorState const& s, std::string const& key) {
    Json value;
    if (s.data.is_object())
        if (auto it = s.data.find(key); it != s.data.end()) value = *it;
    return {
        {"id",   uidOf(actor)                                         },
        {"rid",  static_cast<std::int64_t>(actor.getRuntimeID().rawID)},
        {"key",  key                                                  },
        {"data", std::move(value)                                     },
    };
}

void updateViewers(Actor& actor, ActorState& s, std::function<void(Player&)> const& change) {
    syncedActors().insert(uidOf(actor));
    auto&       seen    = viewers()[uidOf(actor)];
    auto        near    = playersNear(static_cast<int>(actor.getDimensionId()), actor.getFeetPos(), kTrackRange);
    std::size_t inRange = 0;
    for (auto* p : near) {
        if (!gf::net::isCompanion(*p)) continue;
        auto pid = uidOf(*p);
        ++inRange;
        if (seen.insert(pid).second) sendAll(*p, actor, s); // startSeenByPlayer: everything (the change included)
        else if (change) change(*p);
    }
    // a companion that left the range gets everything again when it comes back (the set is
    // rebuilt only when someone left)
    if (seen.size() != inRange)
        std::erase_if(seen, [&](std::int64_t pid) {
            return std::none_of(near.begin(), near.end(), [&](Player* p) { return uidOf(*p) == pid && gf::net::isCompanion(*p); });
        });
}

std::size_t viewerCountForTest(std::int64_t uid) {
    auto it = viewers().find(uid);
    return it != viewers().end() ? it->second.size() : 0;
}

void addInternalHooks(InternalHooks hooks) {
    reg().internal.push_back(std::move(hooks));
    invalidateInterest();
}
void invalidateInterest() { reg().interest.clear(); }
bool portalAllowed(Actor const& actor) {
    if (actor.isPlayer() || !isServerActor(actor)) return true;
    auto& r = reg();
    if (r.portalWatch != 0 && uidOf(actor) == r.portalWatch) ++r.portalWatchCount;
    if (!r.noPortal.empty() && r.noPortal.contains(actor.getTypeName())) return false;
    for (auto const& h : r.internal)
        if (h.usePortal && !h.usePortal(actor)) return false;
    return true;
}
void watchPortalQueriesForTest(std::int64_t uid) {
    reg().portalWatch      = uid;
    reg().portalWatchCount = 0;
}
int portalQueriesForTest() { return reg().portalWatchCount; }
void onLevelTick(std::function<void()> fn) { reg().levelTicks.push_back(std::move(fn)); }
void removeLater(std::int64_t uid) { reg().removals.push_back(uid); }
bool hasBehaviour(std::string const& typeId) { return reg().behaviours.contains(typeId); }
void unregisterTypeForTest(std::string const& typeId) {
    reg().behaviours.erase(typeId);
    invalidateInterest();
}
bool hostileMountType(std::string const& typeId) { return reg().hostileMounts.contains(typeId); }
void unmarkHostileMountForTest(std::string const& typeId) {
    reg().hostileMounts.erase(typeId);
    invalidateInterest();
}

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

// Mob::$die is also hooked by F10's death pipeline (gf::death, src/feature/death): whichever hook
// is outermost, F8 dispatches only after the engine's death really ran (a death that onLethal
// cancelled leaves the mob alive and dispatches nothing), once per life. A8 folds the two into one
// die hook (rule R8), F8 then dispatching from the death pipeline's post-death step.
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

// Entity#canUsePortal: TF's bosses (BaseTFBoss) and parts (HydraPart, NagaSegment, TFPart) never go
// through a portal, nor do the types marked with ext::setCanUsePortal (RisingZombie, ChainBlock,
// CubeOfAnnihilation). Bedrock's portals and the Twilight portal ask the actor's
// canChangeDimensionsUsingPortal: Mob, FallingBlockActor and Player override Actor's, every other
// actor (projectiles, items, vehicles) answers with Actor's own; all three non-player ones are
// hooked. (F4's Twilight portal lets an actor that carries a passenger through without asking it.)
LL_TYPE_INSTANCE_HOOK(EntityPortalHook, ll::memory::HookPriority::Normal, Mob, &Mob::$canChangeDimensionsUsingPortal, bool) {
    return gf::feature::entity::portalAllowed(*this) && origin(); // refused types never reach the engine's rule
}
LL_TYPE_INSTANCE_HOOK(ActorPortalHook, ll::memory::HookPriority::Normal, Actor, &Actor::$canChangeDimensionsUsingPortal, bool) {
    return gf::feature::entity::portalAllowed(*this) && origin(); // refused types never reach the engine's rule
}
LL_TYPE_INSTANCE_HOOK(
    FallingBlockPortalHook,
    ll::memory::HookPriority::Normal,
    FallingBlockActor,
    &FallingBlockActor::$canChangeDimensionsUsingPortal,
    bool
) {
    return gf::feature::entity::portalAllowed(*this) && origin(); // refused types never reach the engine's rule
}

// ---- contract -----------------------------------------------------------------------------------

namespace gf::entity {

namespace fe = gf::feature::entity;

void registerType(std::string const& entityId, Behaviour behaviour) {
    auto& slot = fe::reg().behaviours[entityId];
    if (slot.onSpawn || slot.tick || slot.aiStep || slot.hurt || slot.die || slot.save || slot.load || slot.interact)
        log().warn("entity: behaviour of {} registered twice (the later one replaces it)", entityId);
    slot = std::move(behaviour);
    fe::invalidateInterest();
}

Json& data(Actor& actor, std::string const& key) {
    auto& s = fe::stateOf(actor);
    if (!s.data.is_object()) s.data = Json::object();
    return s.data[key];
}

// SynchedEntityData#set: the change to the companions that see the actor (Java's tracking: the
// server view distance, 10 chunks); the key stays synced, so a companion that starts seeing the
// actor later (or joins later) gets its value then (startSeenByPlayer, fe::updateViewers).
void sync(Actor& actor, std::string const& key) {
    auto& s = fe::stateOf(actor);
    if (!s.data.is_object()) s.data = Json::object();
    auto& keys = s.data[fe::kSyncedKey];
    if (!keys.is_array()) keys = Json::array();
    if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
    auto msg = fe::dataMessage(actor, s, key);
    fe::updateViewers(actor, s, [&](Player& p) { gf::net::send(p, "entity.data", msg); });
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

namespace ext {

void markHostileMount(std::string const& entityId) {
    fe::reg().hostileMounts.insert(entityId);
    fe::invalidateInterest();
}

void setCanUsePortal(std::string const& entityId, bool canUse) {
    if (canUse) fe::reg().noPortal.erase(entityId);
    else fe::reg().noPortal.insert(entityId);
}

} // namespace ext

} // namespace gf::entity

namespace gf::feature::entity {
void dispatchDieForTest(Mob& mob, ActorDamageSource const& source) { onActorDie(mob, source); }
} // namespace gf::feature::entity

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
        EntityPortalHook::hook();
        ActorPortalHook::hook();
        FallingBlockPortalHook::hook();
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
        // the level tick listeners, the companion join listener and the world lifecycle have no
        // removal: registered once per process
        static bool lifecycle = false;
        if (!std::exchange(lifecycle, true)) {
            game::db::onWorldClosed([] {
                reg().states.clear();
                viewers().clear();
                syncedActors().clear();
            });
            // startSeenByPlayer for the companions that came into range
            onLevelTick([] { trackAll(); });
            // a companion that just joined gets everything it sees now
            gf::net::onCompanionJoined([](Player& player) {
                auto pid = uidOf(player);
                for (auto& [uid, seen] : viewers()) seen.erase(pid);
                trackAll();
            });
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
        FallingBlockPortalHook::unhook();
        ActorPortalHook::unhook();
        EntityPortalHook::unhook();
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
