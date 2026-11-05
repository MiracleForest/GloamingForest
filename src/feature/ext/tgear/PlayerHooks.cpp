// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/asm/hooks/PlayerHooks.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U11's extension hooks (PlayerHooks.h).
#include "feature/ext/tgear/PlayerHooks.h"

#include <mutex>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/AttributeInstanceRef.h"
#include "mc/world/attribute/MutableAttributeWithContext.h"
#include "mc/world/item/ItemStack.h"

#include "mod/Log.h"

namespace gf::ext::tgear {

namespace {

struct State {
    std::mutex               mutex;
    std::vector<ArmorHurtFn> armor;
    ExhaustionDivisorFn      divisor;
};

State& state() {
    static State s;
    return s;
}

AttributeInstance* exhaustionInstance(Player& player) {
    auto attr = player.getMutableAttribute(Player::EXHAUSTION());
    return attr.mInstance->mPtr;
}

} // namespace

float exhaustion(Player const& player) {
    auto ref = player.getAttribute(Player::EXHAUSTION());
    if (auto const* inst = static_cast<AttributeInstance const*>(ref.mPtr)) return inst->mCurrentValue;
    return 0;
}

void setExhaustion(Player& player, float value) {
    if (auto* inst = exhaustionInstance(player)) inst->mCurrentValue = value;
}

void onArmorHurt(ArmorHurtFn fn) {
    std::lock_guard lock(state().mutex);
    state().armor.push_back(std::move(fn));
}

void setMovementExhaustionDivisor(ExhaustionDivisorFn fn) {
    std::lock_guard lock(state().mutex);
    state().divisor = std::move(fn);
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
    std::vector<gf::ext::tgear::ArmorHurtFn> fns;
    {
        std::lock_guard lock(gf::ext::tgear::state().mutex);
        fns = gf::ext::tgear::state().armor;
    }
    for (auto const& fn : fns) {
        try {
            fn(*this, before);
        } catch (std::exception const& e) {
            gf::log().error("tgear: armour hurt listener failed: {}", e.what());
        }
    }
}

LL_TYPE_INSTANCE_HOOK(
    TgearMovementStatsHook,
    ll::memory::HookPriority::Normal,
    ServerPlayer,
    &ServerPlayer::$checkMovementStats,
    void,
    ::Vec3 const& d
) {
    gf::ext::tgear::ExhaustionDivisorFn divisor;
    {
        std::lock_guard lock(gf::ext::tgear::state().mutex);
        divisor = gf::ext::tgear::state().divisor;
    }
    float factor = divisor ? divisor(*this) : 1.f;
    if (factor == 1.f || factor <= 0.f) return origin(d);
    float was = gf::ext::tgear::exhaustion(*this);
    origin(d);
    float now = gf::ext::tgear::exhaustion(*this);
    // the exhaustion attribute wraps at 4 (one saturation / hunger point): only scale a plain increase
    if (now > was) gf::ext::tgear::setExhaustion(*this, was + (now - was) / factor);
}

} // namespace

namespace gf::ext::tgear {

void installHooks() {
    TgearArmorHurtHook::hook();
    TgearMovementStatsHook::hook();
}

void uninstallHooks() {
    TgearArmorHurtHook::unhook();
    TgearMovementStatsHook::unhook();
}

} // namespace gf::ext::tgear
