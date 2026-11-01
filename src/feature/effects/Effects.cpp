// The effect part of contract "enchant+effects" (F9; spike S6).
//
// Bedrock's mob effects are a closed enum (MobEffectIds, the 38 vanilla clients know), so a custom
// effect (TF's frosted) is native state of the actor with a vanilla *carrier* effect that clients
// see and that does the vanilla part of the work on their side (frosted: Slowness of the same
// amplifier, particles hidden: the client predicts the slower movement itself). Java's rules of
// MobEffectInstance#update decide how a second application combines (a higher amplifier replaces,
// the same amplifier extends). The state survives unloads and restarts as an actor tag
// ("gf_fx|<id>|<amplifier>|<ticks>", rewritten every second) and ends on death; milk and /effect
// clear (Actor::removeAllEffects) end it as they end every effect on Java, while a carrier lost on
// its own (an expired vanilla instance of the same id) is put back. Players with the client mod get "effects.sync" for
// the HUD icon. Vanilla ids ("minecraft:slowness") go straight to Bedrock's effects.
#include "feature/effects/Effects.h"

#include <algorithm>
#include <charconv>
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
#include "feature/net/Net.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::effects {

namespace {

using internal::Active;

struct Store {
    std::recursive_mutex                                    mutex;
    std::map<std::string, Spec>                             specs;
    std::unordered_map<std::int64_t, std::vector<Active>>   active; ///< by ActorUniqueID
    std::uint64_t                                           tick = 0;
};

Store& store() {
    static Store s;
    return s;
}

std::string fullId(std::string const& id) { return id.find(':') == std::string::npos ? "minecraft:" + id : id; }

std::int64_t uidOf(Actor const& actor) { return const_cast<Actor&>(actor).getOrCreateUniqueID().rawID; }

std::string tagOf(Active const& a) { return std::string(internal::kTagPrefix) + a.id + "|" + std::to_string(a.amplifier) + "|" + std::to_string(a.ticks); }

std::optional<Active> parseTag(std::string const& tag) {
    if (!tag.starts_with(internal::kTagPrefix)) return std::nullopt;
    auto rest = std::string_view(tag).substr(std::string_view(internal::kTagPrefix).size());
    auto p1   = rest.find('|');
    auto p2   = p1 == std::string_view::npos ? p1 : rest.find('|', p1 + 1);
    if (p2 == std::string_view::npos) return std::nullopt;
    Active a;
    a.id      = std::string(rest.substr(0, p1));
    auto amp  = rest.substr(p1 + 1, p2 - p1 - 1);
    auto left = rest.substr(p2 + 1);
    if (std::from_chars(amp.data(), amp.data() + amp.size(), a.amplifier).ec != std::errc{}) return std::nullopt;
    if (std::from_chars(left.data(), left.data() + left.size(), a.ticks).ec != std::errc{}) return std::nullopt;
    return a;
}

/// Removes every persistence tag of the actor, then writes the current ones.
void writeTags(Actor& actor, std::vector<Active> const& list) {
    for (auto const& t : actor.getTags())
        if (t.starts_with(internal::kTagPrefix)) actor.removeTag(t);
    for (auto const& a : list)
        if (a.ticks > 0) actor.addTag(tagOf(a));
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

/// Adds / refreshes the carrier of a custom effect.
void applyCarrier(Actor& actor, Spec const& spec, int ticks, int amplifier) {
    if (spec.carrier.empty()) return;
    auto vid = internal::vanillaId(spec.carrier);
    if (!vid) return;
    MobEffectInstance inst(*vid, EffectDuration{ticks});
    inst.mAmplifier     = amplifier;
    inst.mEffectVisible = !spec.hideCarrierParticles;
    inst.mAmbient       = false;
    actor.addEffect(inst);
}

void removeCarrier(Actor& actor, Spec const& spec) {
    if (spec.carrier.empty()) return;
    if (auto vid = internal::vanillaId(spec.carrier)) actor.removeEffect(static_cast<int>(*vid));
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
    bool  changed = false;
    if (it == list.end()) {
        list.push_back({id, amplifier, ticks});
        changed = true;
    } else if (amplifier > it->amplifier) { // MobEffectInstance#update
        it->amplifier = amplifier;
        it->ticks     = ticks;
        changed       = true;
    } else if (amplifier == it->amplifier && ticks > it->ticks) {
        it->ticks = ticks;
        changed   = true;
    }
    if (!changed) return false;
    auto const& now = *std::find_if(list.begin(), list.end(), [&](Active const& a) { return a.id == id; });
    applyCarrier(actor, sp->second, now.ticks, now.amplifier);
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
    auto            n    = std::erase_if(list, [&](Active const& a) { return a.id == id; });
    if (n == 0) return;
    if (auto sp = s.specs.find(id); sp != s.specs.end()) removeCarrier(actor, sp->second);
    writeTags(actor, list);
    syncCompanion(actor, list);
}

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
        bool writeNow = s.tick % 20 == 0;
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
                auto const& spec = sp->second;
                // the carrier went away without the effect (an expired vanilla instance of the same
                // id, a single /effect removal): put it back; milk and /effect clear end both
                // (the removeAllEffects hook)
                if (!spec.carrier.empty() && a.ticks > 1) {
                    if (auto vid = vanillaId(spec.carrier); vid && !actor->getEffect(*vid)) applyCarrier(*actor, spec, a.ticks, a.amplifier);
                }
                if (spec.tick && a.ticks % spec.tickInterval == 0) calls.push_back({it->first, spec.tick, a.amplifier, a.id});
                if (--a.ticks <= 0) changed = true;
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
