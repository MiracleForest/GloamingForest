// GloamingForest - U11's extension hook (PlayerHooks.h): armour damage of server players.
#include "feature/ext/tgear/PlayerHooks.h"

#include <mutex>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "mod/Log.h"

namespace gf::ext::tgear {

namespace {

struct State {
    std::mutex               mutex;
    std::vector<ArmorHurtFn> armor;
};

State& state() {
    static State s;
    return s;
}

} // namespace

void onArmorHurt(ArmorHurtFn fn) {
    std::lock_guard lock(state().mutex);
    state().armor.push_back(std::move(fn));
}

std::vector<ArmorHurtFn> armorListeners() {
    std::lock_guard lock(state().mutex);
    return state().armor;
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
    std::array<ItemStack, 4> before;
    for (int i = 0; i < 4; ++i) before[i] = getArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(i));
    origin(source, dmg, hurtSlots);
    for (auto const& fn : gf::ext::tgear::armorListeners()) {
        try {
            fn(*this, before);
        } catch (std::exception const& e) {
            gf::log().error("tgear: armour hurt listener failed: {}", e.what());
        }
    }
}

} // namespace

namespace gf::ext::tgear {

void installHooks() { TgearArmorHurtHook::hook(); }
void uninstallHooks() { TgearArmorHurtHook::unhook(); }

} // namespace gf::ext::tgear
