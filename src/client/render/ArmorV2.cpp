// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/ASMHooks.java (ArmorHooks#cancelArmorRendering)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Contract "client.render" (F12): hideArmor for the v2 actor renderer. Bedrock 1.26.51 draws
// players (and every data-driven actor) with DataDrivenRendererV2, which bakes the worn stacks'
// attachables itself (ActorAttachableDerivitiveData::mAttachedItems) without asking
// DataDrivenModel::shouldRenderAttachableOnActor (the v1 check Render.cpp hooks). The worn stacks a
// hideArmor function hides are taken out of the actor's armour container (the client's copy) when
// the renderer takes the actor for the frame (tryAddActor) and put back when the frame is generated
// (generateFrame): the renderer sees an empty slot, nothing else does (no listener runs, no packet
// goes out, the client tick does not run in between).

#ifdef GF_CLIENT

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/renderer/actor/v2/DataDrivenRendererV2.h"
#include "mc/client/renderer/actor/v2/GameplayRelatedRenderData.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components/ActorEquipmentComponent.h"
#include "mc/world/SimpleContainer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/item/ItemStack.h"

#include "feature/ext/ui/Client.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::render {

/// Whether any hideArmor function is registered (Render.cpp).
[[nodiscard]] bool armorFiltersRegistered();

namespace {

struct Taken {
    void const*      renderer  = nullptr; ///< the DataDrivenRendererV2 that took the actor
    SimpleContainer* container = nullptr;
    std::size_t      slot      = 0;
    ItemStack        stack;
};

struct ArmorV2State {
    std::mutex                 mutex;
    std::vector<Taken>         taken; ///< the stacks out of their slots for the frame being built
    std::atomic<std::uint64_t> hidden{0};
};

ArmorV2State& state() {
    static auto* s = new ArmorV2State(); // the render thread may outlive static destruction
    return *s;
}

/// Takes the hidden worn stacks of `actor` out of its armour container until `renderer` generates
/// its frame.
void takeHiddenArmor(void const* renderer, Actor& actor) {
    auto* equipment = actor.getEntityContext().tryGetComponent<ActorEquipmentComponent>().as_ptr();
    if (!equipment || !equipment->mArmor.get()) return;
    auto& container = *equipment->mArmor.get();
    auto& items     = container.mItems.get();
    for (std::size_t i = 0; i < items.size(); ++i) {
        auto& s = items[i];
        if (game::items::isEmpty(s)) continue;
        bool hide = false;
        try {
            hide = armorHidden(s);
        } catch (...) {}
        if (!hide) continue;
        auto&           st = state();
        std::lock_guard lock(st.mutex);
        if (st.hidden.fetch_add(1, std::memory_order_relaxed) == 0)
            log().info("render: worn armour hidden from the v2 actor renderer ({})", game::items::idOf(s));
        st.taken.push_back({renderer, &container, i, s});
        s = ItemStack::EMPTY_ITEM();
    }
}

/// Puts back the stacks `renderer` took (every renderer's: nullptr), only those of `container` when given.
void restoreArmor(void const* renderer, SimpleContainer const* container = nullptr) {
    auto&           st = state();
    std::lock_guard lock(st.mutex);
    std::erase_if(st.taken, [&](Taken& t) {
        if (renderer && t.renderer != renderer) return false;
        if (container && t.container != container) return false;
        auto& items = t.container->mItems.get();
        if (t.slot < items.size() && game::items::isEmpty(items[t.slot])) items[t.slot] = std::move(t.stack);
        return true;
    });
}

/// Forgets the taken stacks without writing them back (the client's level is gone with its actors).
void forgetArmor() {
    auto&           st = state();
    std::lock_guard lock(st.mutex);
    st.taken.clear();
}

/// The armour container of `actor` (nullptr: none).
SimpleContainer const* armourOf(Actor& actor) {
    auto* equipment = actor.getEntityContext().tryGetComponent<ActorEquipmentComponent>().as_ptr();
    return equipment ? equipment->mArmor.get() : nullptr;
}

} // namespace

std::uint64_t armorV2HiddenCount() { return state().hidden.load(std::memory_order_relaxed); }

} // namespace gf::client::render

LL_TYPE_INSTANCE_HOOK(
    RenderArmorV2AddHook,
    ll::memory::HookPriority::Normal,
    DataDrivenRendererV2,
    &DataDrivenRendererV2::tryAddActor,
    bool,
    Actor&                                actor,
    std::shared_ptr<ActorRenderer> const& renderer,
    ShadowContext                         shadowContext
) {
    namespace r = gf::client::render;
    bool took   = false;
    if (r::armorFiltersRegistered()) {
        try {
            r::takeHiddenArmor(this, actor);
            took = true;
        } catch (...) {}
    }
    bool added = origin(actor, renderer, shadowContext);
    // refused: no frame of this renderer draws the actor, its stacks go back now
    if (took && !added) {
        try {
            r::restoreArmor(this, r::armourOf(actor));
        } catch (...) {}
    }
    return added;
}

LL_TYPE_INSTANCE_HOOK(
    RenderArmorV2FrameHook,
    ll::memory::HookPriority::Normal,
    DataDrivenRendererV2,
    &DataDrivenRendererV2::generateFrame,
    uint,
    GameplayRelatedRenderData const& renderData
) {
    struct Restore {
        void const* self;
        ~Restore() { gf::client::render::restoreArmor(self); }
    } restore{this};
    return origin(renderData);
}

namespace gf::client::render {
namespace {
class ArmorV2Module {
public:
    void install() {
        RenderArmorV2AddHook::hook();
        RenderArmorV2FrameHook::hook();
        if (!mExit)
            mExit = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) { forgetArmor(); });
    }
    void uninstall() {
        RenderArmorV2AddHook::unhook();
        RenderArmorV2FrameHook::unhook();
        restoreArmor(nullptr);
        if (mExit) ll::event::EventBus::getInstance().removeListener(mExit);
        mExit = nullptr;
    }

private:
    ll::event::ListenerPtr mExit;
};
} // namespace
} // namespace gf::client::render

GF_REGISTER_MODULE_ORDERED(gf::client::render::ArmorV2Module, gf::ModuleOrder::Contracts);

#endif // GF_CLIENT
