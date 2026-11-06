// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/potions/FrostedEffect.java
//   src/main/java/twilightforest/enchantment/ApplyFrostedEffect.java
//   src/main/java/twilightforest/events/EntityEvents.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U10 (provisions): the frosted effect (colour 0x56CBFD, harmful).
// - Movement: the carrier is Slowness of the same amplifier (F9): Bedrock's Slowness is Java's
//   -0.15 x (amplifier + 1) ADD_MULTIPLIED_TOTAL, i.e. FrostedEffect's frosted_slowdown modifier.
// - Freezing: every tick the entity counts as in powder snow (+1 frozen tick) and an amplifier
//   above 0 adds `amplifier` more, capped at 140 (Entity#getTicksRequiredToFreeze); out of the
//   effect the counter thaws by 2 a tick (LivingEntity#aiStep). U10 keeps Java's counter and shows
//   it as Bedrock's freezing strength (the frozen screen overlay, ActorDataIDs::FreezingEffectStrength);
//   a fully frozen entity takes freeze damage every 40 ticks (1, 5 for
//   #minecraft:freeze_hurts_extra_types) as LivingEntity#aiStep deals it.
// - EntityEvents#reduceFrostedEffectIfOnFire: freeze damage while frosted gains amplifier / 2; fire
//   damage lowers the amplifier by one (the effect ends below 0), keeping the remaining time.
// - ApplyFrostedEffect#doChillAuraEffect (ext/provisions): not freeze-immune types, not wearers of
//   #minecraft:freeze_immune_wearables, not creative players.
// TF's IceLayer (ice cubes drawn on a frosted entity) is a client render layer: not drawn yet.
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/entity/components/FreezingComponent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDataIDs.h"
#include "mc/deps/vanilla_components/ActorDataDirtyFlagsComponent.h"
#include "mc/entity/components/SynchedActorDataComponent.h"
#include "mc/world/actor/DataItem.h"
#include "mc/world/actor/DataItem2.h"
#include "mc/world/actor/SynchedActorData.h"
#include "mc/world/actor/SynchedActorDataEntityWrapper.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "core/provisions/Provisions.h"
#include "feature/combat/Combat.h"
#include "feature/combat/CombatInternal.h"
#include "feature/effects/Effects.h"
#include "feature/effects/EffectsInternal.h"
#include "feature/ext/item/Effects.h"
#include "feature/ext/provisions/Provisions.h"
#include "feature/provisions/Internal.h"
#include "feature/tags/Tags.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::provisions {

namespace cp = core::provisions;
using Slot   = SharedTypes::Legacy::ArmorSlot;

namespace {

struct FrozenState {
    std::mutex                              mutex;
    std::unordered_map<std::int64_t, int>   ticks; ///< Java's frozen ticks of the actors that have any
};
FrozenState& frozen() {
    static FrozenState s;
    return s;
}

std::int64_t uidOf(Actor& a) { return a.getOrCreateUniqueID().rawID; }

std::string typeId(Actor& a) {
    auto t = a.getTypeName();
    return t.find(':') == std::string::npos ? "minecraft:" + t : t;
}

bool wearsFreezeImmune(Actor& a) {
    for (int i = 0; i < 4; ++i) {
        auto const& s = a.getArmor(static_cast<Slot>(i));
        if (game::items::isEmpty(s)) continue;
        if (::gf::tags::has(::gf::tags::Registry::Item, "minecraft:freeze_immune_wearables", game::items::idOf(s))) return true;
    }
    return false;
}

/// Bedrock's freezing strength (0..1) on the actor: the component and the synced data item.
void showFrozen(Actor& a, int ticks) {
    float strength = std::min(1.0f, static_cast<float>(ticks) / static_cast<float>(cp::kTicksRequiredToFreeze));
    try {
        if (auto fc = a.getEntityContext().tryGetComponent<FreezingComponent>()) fc->mFreezingEffectStrength = strength;
        auto& wrapper = a.mEntityData.get();
        auto& list    = wrapper.mData->mData.get().mItemsArray.get();
        auto  id      = static_cast<std::size_t>(ActorDataIDs::FreezingEffectStrength);
        if (id < list.size() && list[id] && list[id]->setData(float{strength})) wrapper.mDirtyFlags->mDirtyFlags.get().set(id);
    } catch (...) {}
}

/// FrostedEffect#applyEffectTick plus LivingEntity#aiStep's powder-snow rule and freeze damage.
void frostedTick(Actor& a, int amplifier) {
    if (a.isClientSide() || !a.isAlive()) return;
    int  t;
    bool can = canFreeze(a);
    {
        std::lock_guard lock(frozen().mutex);
        auto& v = frozen().ticks[uidOf(a)];
        v       = cp::frostedFrozenTick(v, amplifier, can);
        t       = v;
    }
    showFrozen(a, t);
    if (can && t >= cp::kTicksRequiredToFreeze && a.mTickCount % cp::kFreezeDamageInterval == 0) {
        bool extra = ::gf::tags::has(::gf::tags::Registry::EntityType, "minecraft:freeze_hurts_extra_types", typeId(a));
        ::gf::combat::hurt(a, cp::freezeDamage(extra), "minecraft:freeze");
    }
}

} // namespace

/// The counters of entities no longer frosted thaw (2 a tick) and are forgotten at 0.
void thawFrozen() {
    auto level = ll::service::getLevel();
    if (!level) return;
    std::vector<std::pair<std::int64_t, int>> update;
    {
        std::lock_guard lock(frozen().mutex);
        for (auto const& [uid, t] : frozen().ticks) update.emplace_back(uid, t);
    }
    for (auto& [uid, t] : update) {
        auto* a = level->fetchEntity(ActorUniqueID{uid}, false);
        if (!a || !a->isAlive()) {
            std::lock_guard lock(frozen().mutex);
            frozen().ticks.erase(uid);
            continue;
        }
        if (::gf::effects::has(*a, ids::kFrosted)) continue; // the effect's tick counts
        int n = cp::thawTick(t);
        showFrozen(*a, n);
        std::lock_guard lock(frozen().mutex);
        if (n == 0) frozen().ticks.erase(uid);
        else frozen().ticks[uid] = n;
    }
}

namespace {

int remainingTicks(Actor& a, std::string const& id) {
    for (auto const& e : ::gf::effects::internal::activeOf(a))
        if (e.id == id) return e.ticks;
    return 0;
}

/// EntityEvents#reduceFrostedEffectIfOnFire.
::gf::combat::Verdict onIncoming(::gf::combat::DamageInfo& d) {
    int amp = ::gf::effects::amplifier(d.victim, ids::kFrosted);
    if (amp < 0) return ::gf::combat::Verdict::Pass;
    if (d.type == "minecraft:freeze") {
        d.amount = cp::frostedFreezeDamage(d.amount, amp);
    } else if (::gf::combat::damageTypeHasTag(d.type, "minecraft:is_fire")) {
        // LivingEntity#hurt returns before the event for a fire-immune entity
        if (::gf::effects::has(d.victim, "minecraft:fire_resistance") || d.victim.isFireImmune()) return ::gf::combat::Verdict::Pass;
        int ticks = remainingTicks(d.victim, ids::kFrosted);
        ::gf::effects::remove(d.victim, ids::kFrosted);
        int next = cp::frostedAfterFire(amp);
        if (next >= 0 && ticks > 0) ::gf::effects::apply(d.victim, ids::kFrosted, ticks, next);
    }
    return ::gf::combat::Verdict::Pass;
}


} // namespace

int frozenTicks(Actor& actor) {
    std::lock_guard lock(frozen().mutex);
    auto            it = frozen().ticks.find(uidOf(actor));
    return it == frozen().ticks.end() ? 0 : it->second;
}

bool canFreeze(Actor& a) {
    if (::gf::tags::has(::gf::tags::Registry::EntityType, "minecraft:freeze_immune_entity_types", typeId(a))) return false;
    if (a.isPlayer() && a.isSpectator()) return false;
    return !wearsFreezeImmune(a);
}

void installFrosted() {
    ::gf::effects::registerType(
        ids::kFrosted,
        {.carrier              = "minecraft:slowness",
         .hideCarrierParticles = true,
         .color                = cp::kFrostedColor,
         .beneficial           = false,
         .nameKey              = "effect.twilightforest.frosted",
         .tick                 = &frostedTick,
         .tickInterval         = 1}
    );
    ::gf::effects::ext::setIcon(ids::kFrosted, "textures/twilightforest/mob_effect/frosted");
    ::gf::combat::onIncoming(::gf::combat::priority::Low, &onIncoming);
}

} // namespace gf::feature::provisions

namespace gf::provisions::ext {

bool doChillAuraEffect(Actor& victim, int duration, int amplifier, bool shouldHit) {
    namespace fp = gf::feature::provisions;
    if (!shouldHit) return false;
    if (gf::tags::has(gf::tags::Registry::EntityType, "minecraft:freeze_immune_entity_types", fp::typeId(victim))) return false;
    if (fp::wearsFreezeImmune(victim)) return false;
    if (victim.isPlayer() && static_cast<Player&>(victim).isCreative()) return false;
    gf::effects::apply(victim, kFrosted, duration, amplifier);
    return true;
}

} // namespace gf::provisions::ext
