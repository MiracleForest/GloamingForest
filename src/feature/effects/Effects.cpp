// The effect part of contract "enchant+effects" (F9; spike S6).
//
// Bedrock's mob effects are a closed enum (MobEffectIds, the 38 vanilla clients know), so a custom
// effect (upstream's frosted) is native state of the actor with a vanilla *carrier* effect that clients
// see and that does the vanilla part of the work on their side (frosted: Slowness of the same
// amplifier, particles hidden: the client predicts the slower movement itself).
//
// Java's MobEffectInstance rules decide how applications combine (#update: a higher amplifier
// replaces and keeps the old one hidden when it lasts longer; the same amplifier extends; a lower,
// longer one waits hidden and resumes when the stronger one ends; #tick counts the hidden ones down
// too). The state survives unloads and restarts as an actor tag ("gf_fx|<id>|<amplifier>|<ticks>|
// <hidden>|<displaced>", rewritten on every change, every second and when the world saves) and
// ends on death; milk and /effect clear (Actor::removeAllEffects) end it as they end every effect
// on Java. A state read back from its tag may be up to a second old while the carrier the actor
// kept is exact: the state follows its carrier then (see resync).
//
// The carrier is the custom effect's own instance only while it is: a vanilla instance of the same
// effect (a Slowness potion, a stray's arrow) that is as strong and lasts as long is left alone (it
// carries the movement effect meanwhile); a weaker one is replaced and remembered, and put back with
// its remaining time when the custom effect ends; removing the custom effect removes the carrier
// only while the actor's instance is still the one the custom effect put there. A carrier lost on
// its own (an expired vanilla instance of the same id, a single /effect removal) is put back.
// Players with the client mod get "effects.sync" (the HUD row, client/item). Vanilla ids
// ("minecraft:slowness") go straight to Bedrock's effects.
#include "feature/effects/Effects.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffect.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/level/Level.h"

#include "feature/effects/EffectsInternal.h"
#include "feature/ext/item/Effects.h"
#include "feature/net/Net.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::effects {

namespace {

using internal::Active;
using internal::Displaced;

struct Store {
    std::recursive_mutex                                  mutex;
    std::map<std::string, Spec>                           specs;
    std::map<std::string, std::string>                    icons;  ///< ext::setIcon
    std::unordered_map<std::int64_t, std::vector<Active>> active; ///< by ActorUniqueID
    std::uint64_t                                         tick = 0;
};

Store& store() {
    static Store s;
    return s;
}

std::string fullId(std::string const& id) { return id.find(':') == std::string::npos ? "minecraft:" + id : id; }

std::int64_t uidOf(Actor const& actor) { return const_cast<Actor&>(actor).getOrCreateUniqueID().rawID; }

// ---- persistence -------------------------------------------------------------------------------------

std::string tagOf(Active const& a) {
    std::string out = std::string(internal::kTagPrefix) + a.id + "|" + std::to_string(a.amplifier) + "|" + std::to_string(a.ticks) + "|";
    for (std::size_t i = 0; i < a.hidden.size(); ++i)
        out += (i ? "," : "") + std::to_string(a.hidden[i].first) + ":" + std::to_string(a.hidden[i].second);
    out += "|";
    if (a.displaced)
        out += std::to_string(a.displaced->amplifier) + ":" + std::to_string(a.displaced->ticks) + ":" + (a.displaced->visible ? "1" : "0") + ":"
             + (a.displaced->ambient ? "1" : "0");
    return out;
}

std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    std::size_t                   start = 0;
    while (true) {
        auto p = s.find(sep, start);
        out.push_back(s.substr(start, p == std::string_view::npos ? std::string_view::npos : p - start));
        if (p == std::string_view::npos) break;
        start = p + 1;
    }
    return out;
}

bool toInt(std::string_view s, int& out) { return !s.empty() && std::from_chars(s.data(), s.data() + s.size(), out).ec == std::errc{}; }

std::optional<Active> parseTag(std::string const& tag) {
    if (!tag.starts_with(internal::kTagPrefix)) return std::nullopt;
    auto parts = split(std::string_view(tag).substr(std::string_view(internal::kTagPrefix).size()), '|');
    if (parts.size() < 3) return std::nullopt;
    Active a;
    a.id = std::string(parts[0]);
    if (!toInt(parts[1], a.amplifier) || !toInt(parts[2], a.ticks)) return std::nullopt;
    if (parts.size() > 3 && !parts[3].empty())
        for (auto h : split(parts[3], ',')) {
            auto f = split(h, ':');
            int  amp = 0, ticks = 0;
            if (f.size() == 2 && toInt(f[0], amp) && toInt(f[1], ticks) && ticks > 0) a.hidden.emplace_back(amp, ticks);
        }
    if (parts.size() > 4 && !parts[4].empty()) {
        auto      f = split(parts[4], ':');
        Displaced d;
        if (f.size() == 4 && toInt(f[0], d.amplifier) && toInt(f[1], d.ticks)) {
            d.visible   = f[2] == "1";
            d.ambient   = f[3] == "1";
            a.displaced = d;
        }
    }
    a.reloaded = true;
    return a;
}

/// Removes every persistence tag of the actor, then writes the current ones.
void writeTags(Actor& actor, std::vector<Active> const& list) {
    for (auto const& t : actor.getTags())
        if (t.starts_with(internal::kTagPrefix)) actor.removeTag(t);
    for (auto const& a : list)
        if (a.ticks > 0) actor.addTag(tagOf(a));
}

/// A state read back from its tag (`a.reloaded`) against the carrier the actor kept: the tag is up to
/// kWriteInterval ticks old while the carrier ticked on until the actor unloaded or the world
/// stopped. The carrier is the custom effect's own when it has its amplifier and particle visibility
/// and a duration within that window; the state then follows it (the elapsed ticks come off the
/// hidden chain and the displaced instance too), so the carrier is recognised as ours afterwards.
/// Lock held; the spec must be registered (else it waits for the next tick).
void resync(Store& s, Actor const& actor, Active& a) {
    if (!a.reloaded) return;
    auto sp = s.specs.find(a.id);
    if (sp == s.specs.end()) return;
    a.reloaded = false;
    auto const& spec = sp->second;
    if (spec.carrier.empty()) return;
    auto vid = internal::vanillaId(spec.carrier);
    if (!vid) return;
    auto const* cur = actor.getEffect(*vid);
    if (!cur || static_cast<int>(cur->mAmplifier) != a.amplifier || cur->mEffectVisible != !spec.hideCarrierParticles) return;
    int elapsed = a.ticks - cur->mDuration->mValue;
    if (elapsed <= 0 || elapsed > internal::kWriteInterval + 2) return;
    a.ticks -= elapsed;
    for (auto& h : a.hidden) h.second -= elapsed;
    std::erase_if(a.hidden, [](auto const& h) { return h.second <= 0; });
    if (a.displaced && (a.displaced->ticks -= elapsed) <= 0) a.displaced.reset();
}

/// The in-memory list of an actor, loaded from its tags on first use (lock held).
std::vector<Active>& listOf(Store& s, Actor const& actor) {
    auto uid = uidOf(actor);
    auto it  = s.active.find(uid);
    if (it != s.active.end()) return it->second;
    std::vector<Active> list;
    if (actor.hasTags())
        for (auto const& t : actor.getTags())
            if (auto a = parseTag(t)) list.push_back(*a);
    for (auto& a : list) resync(s, actor, a);
    return s.active[uid] = std::move(list);
}

void syncCompanion(Actor& actor, std::vector<Active> const& list) {
    if (!actor.isPlayer()) return;
    auto& player = static_cast<Player&>(actor);
    if (!net::isCompanion(player)) return;
    nlohmann::json effects = nlohmann::json::array();
    for (auto const& a : list) effects.push_back({{"id", a.id}, {"amplifier", a.amplifier}, {"ticks", a.ticks}});
    net::send(player, "effects.sync", {{"effects", effects}});
}

// ---- Java's MobEffectInstance rules ---------------------------------------------------------------------

/// MobEffectInstance#update of level `i` of a chain (0 = the active instance) with another
/// application; true when the active instance changed.
bool updateChain(std::vector<std::pair<int, int>>& chain, std::size_t i, int amplifier, int ticks) {
    if (i >= chain.size()) {
        chain.emplace_back(amplifier, ticks); // hiddenEffect = new MobEffectInstance(other)
        return false;
    }
    auto& cur = chain[i];
    if (amplifier > cur.first) {
        // a stronger, shorter one: this one waits behind it with its own hidden chain
        if (ticks < cur.second) chain.insert(chain.begin() + static_cast<std::ptrdiff_t>(i) + 1, cur);
        chain[i] = {amplifier, ticks};
        return i == 0;
    }
    if (cur.second < ticks) {
        if (amplifier == cur.first) {
            cur.second = ticks;
            return i == 0;
        }
        updateChain(chain, i + 1, amplifier, ticks);
    }
    return false;
}

// ---- carriers ----------------------------------------------------------------------------------------

bool sameInstance(MobEffectInstance const& inst, Spec const& spec, int amplifier, int ticks) {
    return inst.mAmplifier == amplifier && inst.mEffectVisible == !spec.hideCarrierParticles && std::abs(inst.mDuration->mValue - ticks) <= 2;
}

void addCarrierInstance(Actor& actor, unsigned vid, int ticks, int amplifier, bool visible, bool ambient) {
    MobEffectInstance inst(vid, EffectDuration{ticks});
    inst.mAmplifier     = amplifier;
    inst.mEffectVisible = visible;
    inst.mAmbient       = ambient;
    actor.addEffect(inst);
}

/// Puts the carrier of `a` on the actor: over its own previous carrier (`prevAmp` / `prevTicks`,
/// -1: none), never over a vanilla instance that is as strong and lasts as long; a weaker vanilla
/// instance is replaced and remembered in `a.displaced`.
void applyCarrier(Actor& actor, Spec const& spec, Active& a, int prevAmp = -1, int prevTicks = 0) {
    if (spec.carrier.empty()) return;
    auto vid = internal::vanillaId(spec.carrier);
    if (!vid) return;
    if (auto const* cur = actor.getEffect(*vid)) {
        bool ours = prevAmp >= 0 && sameInstance(*cur, spec, prevAmp, prevTicks);
        if (!ours) {
            int curTicks = cur->mDuration->mValue;
            if (cur->mAmplifier > a.amplifier || (cur->mAmplifier == a.amplifier && (curTicks < 0 || curTicks >= a.ticks))) return;
            if (!a.displaced) a.displaced = Displaced{cur->mAmplifier, curTicks, cur->mEffectVisible, cur->mAmbient};
        }
        // Bedrock keeps a stronger instance on addEffect: our own previous carrier goes first
        actor.removeEffect(static_cast<int>(*vid));
    }
    addCarrierInstance(actor, *vid, a.ticks, a.amplifier, !spec.hideCarrierParticles, false);
}

/// Takes the carrier of `a` off the actor (only while it is still its own) and puts back the
/// vanilla instance it displaced, with its remaining time.
void removeCarrier(Actor& actor, Spec const& spec, Active const& a) {
    if (spec.carrier.empty()) return;
    auto vid = internal::vanillaId(spec.carrier);
    if (!vid) return;
    if (auto const* cur = actor.getEffect(*vid); cur && sameInstance(*cur, spec, a.amplifier, a.ticks)) actor.removeEffect(static_cast<int>(*vid));
    if (a.displaced && a.displaced->ticks > 0 && !actor.getEffect(*vid))
        addCarrierInstance(actor, *vid, a.displaced->ticks, a.displaced->amplifier, a.displaced->visible, a.displaced->ambient);
}

} // namespace

void registerType(std::string const& effectId, Spec spec) {
    auto&           s  = store();
    std::lock_guard lock(s.mutex);
    auto            id = fullId(effectId);
    if (spec.nameKey.empty()) spec.nameKey = "effect." + id.substr(0, id.find(':')) + "." + id.substr(id.find(':') + 1);
    if (spec.tickInterval < 1) spec.tickInterval = 1;
    s.specs[id] = std::move(spec);
}

bool apply(Actor& actor, std::string const& effectId, int ticks, int amplifier) {
    auto id = fullId(effectId);
    if (ticks <= 0 || !actor.isAlive()) return false;
    if (auto vid = internal::vanillaId(id)) {
        MobEffectInstance inst(*vid, EffectDuration{ticks});
        inst.mAmplifier = std::max(0, amplifier);
        actor.addEffect(inst);
        return true;
    }
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto            sp = s.specs.find(id);
    if (sp == s.specs.end()) {
        static std::set<std::string> warned;
        if (warned.insert(id).second) log().warn("effects: unknown effect {}", id);
        return false;
    }
    amplifier  = std::clamp(amplifier, 0, 255);
    auto& list = listOf(s, actor);
    auto  it   = std::find_if(list.begin(), list.end(), [&](Active const& a) { return a.id == id; });
    if (it == list.end()) {
        list.push_back({id, amplifier, ticks, {}, std::nullopt});
        applyCarrier(actor, sp->second, list.back());
    } else {
        // MobEffectInstance#update over the active instance and its hidden chain
        std::vector<std::pair<int, int>> chain{{it->amplifier, it->ticks}};
        chain.insert(chain.end(), it->hidden.begin(), it->hidden.end());
        int  prevAmp = it->amplifier, prevTicks = it->ticks;
        bool changed = updateChain(chain, 0, amplifier, ticks);
        it->amplifier = chain.front().first;
        it->ticks     = chain.front().second;
        it->hidden.assign(chain.begin() + 1, chain.end());
        if (!changed) {
            writeTags(actor, list); // a hidden application still persists
            return false;
        }
        applyCarrier(actor, sp->second, *it, prevAmp, prevTicks);
    }
    writeTags(actor, list);
    syncCompanion(actor, list);
    return true;
}

bool has(Actor const& actor, std::string const& effectId) { return amplifier(actor, effectId) >= 0; }

int amplifier(Actor const& actor, std::string const& effectId) {
    auto id = fullId(effectId);
    if (auto vid = internal::vanillaId(id)) {
        auto const* inst = actor.getEffect(*vid);
        return inst ? static_cast<int>(inst->mAmplifier) : -1;
    }
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    for (auto const& a : listOf(s, actor))
        if (a.id == id && a.ticks > 0) return a.amplifier;
    return -1;
}

void remove(Actor& actor, std::string const& effectId) {
    auto id = fullId(effectId);
    if (auto vid = internal::vanillaId(id)) {
        actor.removeEffect(static_cast<int>(*vid));
        return;
    }
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto&           list = listOf(s, actor);
    auto            it   = std::find_if(list.begin(), list.end(), [&](Active const& a) { return a.id == id; });
    if (it == list.end()) return;
    Active removed = *it;
    list.erase(it);
    if (auto sp = s.specs.find(id); sp != s.specs.end()) removeCarrier(actor, sp->second, removed);
    writeTags(actor, list);
    syncCompanion(actor, list);
}

namespace ext {
void setIcon(std::string const& effectId, std::string const& texture) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    s.icons[fullId(effectId)] = texture;
}
} // namespace ext

namespace internal {

std::vector<Active> activeOf(Actor const& actor) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    return listOf(s, actor);
}

std::optional<Spec> specOf(std::string const& effectId) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto            it = s.specs.find(fullId(effectId));
    if (it == s.specs.end()) return std::nullopt;
    return it->second;
}

std::optional<unsigned> vanillaId(std::string const& effectId) {
    auto id = fullId(effectId);
    if (!id.starts_with("minecraft:")) return std::nullopt;
    auto name = id.substr(10);
    if (name == "hero_of_the_village") name = "village_hero";
    if (auto const* e = MobEffect::getByName(name)) return static_cast<unsigned>(e->mId);
    return std::nullopt;
}

bool carrierIsOurs(Actor const& actor, Spec const& spec, Active const& active) {
    if (spec.carrier.empty()) return false;
    auto vid = vanillaId(spec.carrier);
    if (!vid) return false;
    auto const* cur = actor.getEffect(*vid);
    return cur && sameInstance(*cur, spec, active.amplifier, active.ticks);
}

std::string iconOf(std::string const& effectId) {
    auto id = fullId(effectId);
    {
        auto&           s = store();
        std::lock_guard lock(s.mutex);
        if (auto it = s.icons.find(id); it != s.icons.end()) return it->second;
    }
    auto colon = id.find(':');
    return "textures/" + id.substr(0, colon) + "/mob_effect/" + id.substr(colon + 1);
}

void unregisterType(std::string const& effectId) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    s.specs.erase(fullId(effectId));
    s.icons.erase(fullId(effectId));
}

void forget(Actor const& actor) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    s.active.erase(uidOf(actor));
}

void tickAll() {
    auto level = ll::service::getLevel();
    if (!level) return;
    struct Call {
        std::int64_t                     uid;
        std::function<void(Actor&, int)> fn;
        int                              amplifier;
        std::string                      id;
    };
    std::vector<Call> calls; // the effects' ticks run after the pass: they may apply / remove effects
    {
        auto&           s = store();
        std::lock_guard lock(s.mutex);
        ++s.tick;
        bool writeNow = s.tick % internal::kWriteInterval == 0;
        for (auto it = s.active.begin(); it != s.active.end();) {
            auto& list = it->second;
            if (list.empty()) {
                it = s.active.erase(it);
                continue;
            }
            Actor* actor = level->fetchEntity(ActorUniqueID{it->first}, false);
            if (!actor) {
                // unloaded: its tags keep the state; it is read again when it comes back
                it = s.active.erase(it);
                continue;
            }
            if (!actor->isAlive()) {
                // Java clears the effects of the dead (a respawned player keeps its tags on Bedrock)
                writeTags(*actor, {});
                syncCompanion(*actor, {});
                it = s.active.erase(it);
                continue;
            }
            bool changed = false;
            for (auto& a : list) {
                auto sp = s.specs.find(a.id);
                if (sp == s.specs.end()) {
                    a.ticks = 0;
                    changed = true;
                    continue;
                }
                resync(s, *actor, a); // a state read back before its spec was registered
                auto const& spec = sp->second;
                // the carrier went away without the effect (an expired vanilla instance that carried
                // it meanwhile, a single /effect removal): put it back; milk and /effect clear end both
                if (!spec.carrier.empty() && a.ticks > 1) {
                    if (auto vid = vanillaId(spec.carrier); vid && !actor->getEffect(*vid)) applyCarrier(*actor, spec, a);
                }
                // MobEffectInstance#tick: the effect tick with the remaining duration, then the
                // countdown of this instance, its hidden ones and the vanilla instance it displaced
                if (spec.tick && a.ticks % spec.tickInterval == 0) calls.push_back({it->first, spec.tick, a.amplifier, a.id});
                for (auto& h : a.hidden) --h.second;
                std::erase_if(a.hidden, [](auto const& h) { return h.second <= 0; });
                if (a.displaced && --a.displaced->ticks <= 0) a.displaced.reset();
                if (--a.ticks <= 0) {
                    changed = true;
                    if (!a.hidden.empty()) {
                        // the hidden instance takes over (setDetailsFrom(hiddenEffect))
                        int prevAmp = a.amplifier, prevTicks = a.ticks;
                        a.amplifier = a.hidden.front().first;
                        a.ticks     = a.hidden.front().second;
                        a.hidden.erase(a.hidden.begin());
                        applyCarrier(*actor, spec, a, prevAmp, prevTicks);
                    } else {
                        removeCarrier(*actor, spec, a);
                    }
                }
            }
            if (changed) {
                std::erase_if(list, [](Active const& a) { return a.ticks <= 0; });
                writeTags(*actor, list);
                syncCompanion(*actor, list);
            } else if (writeNow) {
                writeTags(*actor, list);
            }
            ++it;
        }
    }
    for (auto const& c : calls) {
        Actor* actor = level->fetchEntity(ActorUniqueID{c.uid}, false);
        if (!actor || !actor->isAlive()) continue;
        try {
            c.fn(*actor, c.amplifier);
        } catch (std::exception const& e) {
            log().error("effects: tick of {} failed: {}", c.id, e.what());
        }
    }
}

void clearAll(Actor& actor) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto&           list = listOf(s, actor);
    if (list.empty()) return;
    // milk / effect clear end every vanilla effect too: nothing displaced comes back
    list.clear();
    writeTags(actor, list);
    syncCompanion(actor, list);
}

} // namespace internal

namespace {

/// Actors that come back with persistence tags (loaded chunks, a restart): their effects resume.
void hydrate() {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto& s = store();
    for (auto* actor : level->getRuntimeActorList()) {
        if (!actor || !actor->hasTags()) continue;
        std::lock_guard lock(s.mutex);
        if (s.active.contains(uidOf(*actor))) continue;
        bool any = false;
        for (auto const& t : actor->getTags())
            if (t.starts_with(internal::kTagPrefix)) {
                any = true;
                break;
            }
        if (any) {
            auto& list = listOf(s, *actor);
            syncCompanion(*actor, list);
        }
    }
}

} // namespace
} // namespace gf::effects

// Milk and /effect clear (Actor::removeAllEffects): Java's removeAllEffects ends the custom effects too.
LL_TYPE_INSTANCE_HOOK(EffectsRemoveAllHook, ll::memory::HookPriority::Normal, Actor, &Actor::removeAllEffects, void) {
    if (!isClientSide()) {
        try {
            gf::effects::internal::clearAll(*this);
        } catch (std::exception const& e) {
            gf::log().error("effects: clearing custom effects failed: {}", e.what());
        }
    }
    origin();
}

namespace gf::effects {
namespace {

class EffectsModule {
public:
    void install() {
        EffectsRemoveAllHook::hook();
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([this](auto&) {
            try {
                internal::tickAll();
                if (++mTicks % 100 == 1) hydrate();
            } catch (std::exception const& e) {
                log().error("effects: tick failed: {}", e.what());
            }
        });
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldSave([] {
                // the saved actors carry the exact state (the tags are otherwise up to a second old)
                auto level = ll::service::getLevel();
                if (!level) return;
                auto&           s = store();
                std::lock_guard lock(s.mutex);
                for (auto const& [uid, list] : s.active)
                    if (Actor* actor = level->fetchEntity(ActorUniqueID{uid}, false); actor && actor->isAlive()) writeTags(*actor, list);
            });
            game::db::onWorldClosed([] {
                auto&           s = store();
                std::lock_guard lock(s.mutex);
                s.active.clear();
            });
            net::onCompanionJoined([](Player& p) {
                auto&           s = store();
                std::lock_guard lock(s.mutex);
                syncCompanion(p, listOf(s, p));
            });
        }
    }
    void uninstall() {
        EffectsRemoveAllHook::unhook();
        ll::event::EventBus::getInstance().removeListener(mTick);
        auto&           s = store();
        std::lock_guard lock(s.mutex);
        s.active.clear();
    }

private:
    ll::event::ListenerPtr mTick;
    std::uint64_t          mTicks = 0;
};

} // namespace

} // namespace gf::effects

GF_REGISTER_MODULE_ORDERED(gf::effects::EffectsModule, gf::ModuleOrder::Contracts);
