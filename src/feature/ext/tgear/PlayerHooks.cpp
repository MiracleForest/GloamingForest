// GloamingForest - U11's extension hook (PlayerHooks.h): armour damage of mobs and server players.
#include "feature/ext/tgear/PlayerHooks.h"

#include <mutex>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "mod/Log.h"

namespace gf::ext::tgear {

namespace {

struct State {
    std::mutex                  mutex;
    std::vector<ArmorHurtPreFn> pre;
    std::vector<ArmorHurtFn>    post;
};

State& state() {
    static State s;
    return s;
}

thread_local int gDepth = 0; ///< nested hurtArmorSlots calls (the ServerPlayer override calls Mob's)

} // namespace

void onArmorHurtPre(ArmorHurtPreFn fn) {
    std::lock_guard lock(state().mutex);
    state().pre.push_back(std::move(fn));
}

void onArmorHurt(ArmorHurtFn fn) {
    std::lock_guard lock(state().mutex);
    state().post.push_back(std::move(fn));
}

/// Runs the listeners around `call` (the original hurtArmorSlots) for the outermost call only.
template <class Call>
void around(Mob& wearer, int damage, std::bitset<5> slots, Call&& call) {
    if (gDepth > 0) {
        call(slots);
        return;
    }
    ++gDepth;
    try {
        std::vector<ArmorHurtPreFn> pre;
        std::vector<ArmorHurtFn>    post;
        {
            std::lock_guard lock(state().mutex);
            pre  = state().pre;
            post = state().post;
        }
        for (auto const& fn : pre) {
            try {
                fn(wearer, damage, slots);
            } catch (std::exception const& e) {
                gf::log().error("tgear: armour hurt listener failed: {}", e.what());
            }
        }
        std::array<ItemStack, 4> before;
        for (int i = 0; i < 4; ++i) before[i] = wearer.getArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(i));
        call(slots);
        for (auto const& fn : post) {
            try {
                fn(wearer, before);
            } catch (std::exception const& e) {
                gf::log().error("tgear: armour hurt listener failed: {}", e.what());
            }
        }
    } catch (...) {
        --gDepth;
        throw;
    }
    --gDepth;
}

} // namespace gf::ext::tgear

namespace {

LL_TYPE_INSTANCE_HOOK(
    TgearArmorHurtHook,
    ll::memory::HookPriority::Normal,
    ServerPlayer,
    &ServerPlayer::$hurtArmorSlots,
    void,
    ::ActorDamageSource const& source,
    int                        dmg,
    ::std::bitset<5> const     hurtSlots
) {
    gf::ext::tgear::around(*this, dmg, hurtSlots, [&](std::bitset<5> slots) { origin(source, dmg, slots); });
}

LL_TYPE_INSTANCE_HOOK(
    TgearMobArmorHurtHook,
    ll::memory::HookPriority::Normal,
    Mob,
    &Mob::$hurtArmorSlots,
    void,
    ::ActorDamageSource const& source,
    int                        dmg,
    ::std::bitset<5> const     hurtSlots
) {
    gf::ext::tgear::around(*this, dmg, hurtSlots, [&](std::bitset<5> slots) { origin(source, dmg, slots); });
}

} // namespace

namespace gf::ext::tgear {

void installHooks() {
    TgearArmorHurtHook::hook();
    TgearMobArmorHurtHook::hook();
}
void uninstallHooks() {
    TgearArmorHurtHook::unhook();
    TgearMobArmorHurtHook::unhook();
}

} // namespace gf::ext::tgear
