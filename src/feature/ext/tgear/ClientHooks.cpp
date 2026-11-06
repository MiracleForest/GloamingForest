// GloamingForest - U11's client extension hooks (ClientHooks.h): the local turn and container clicks.
#ifdef GF_CLIENT

#include "feature/ext/tgear/ClientHooks.h"

#include <atomic>
#include <mutex>

#include "ll/api/memory/Hook.h"

#include "mc/client/gui/screens/controllers/ContainerScreenController.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/world/containers/SlotData.h"
#include "mc/world/item/ItemStack.h"

namespace gf::ext::tgear::client {

namespace {
std::atomic<float>         gTurn{1.f};
std::mutex                 gClickMutex;
SlotClickFn                gClick;

bool askClick(ContainerScreenController& c, std::string const& collection, int index) {
    SlotClickFn fn;
    {
        std::lock_guard lock(gClickMutex);
        fn = gClick;
    }
    if (!fn) return false;
    try {
        auto const& cursorSlot = ContainerScreenController::mCursorSlotData();
        auto const& slot       = c._getItemStack(collection, index);
        auto const& cursor     = c._getItemStack(cursorSlot.mCollectionName.get(), static_cast<int>(cursorSlot.mCollectionIndex));
        return fn(collection, index, slot, cursor);
    } catch (...) {
        return false;
    }
}
} // namespace

void setSlotClickHandler(SlotClickFn fn) {
    std::lock_guard lock(gClickMutex);
    gClick = std::move(fn);
}

void setTurnScale(float scale) { gTurn.store(scale > 0 ? scale : 1.f); }

} // namespace gf::ext::tgear::client

namespace {

namespace tc = gf::ext::tgear::client;

LL_TYPE_INSTANCE_HOOK(TgearTurnHook, ll::memory::HookPriority::Normal, LocalPlayer, &LocalPlayer::localPlayerTurn, void, ::Vec2 const& deltaRot) {
    float s = tc::gTurn.load(std::memory_order_relaxed);
    if (s == 1.f) return origin(deltaRot);
    Vec2 scaled{deltaRot.x * s, deltaRot.y * s};
    origin(scaled);
}

LL_TYPE_INSTANCE_HOOK(TgearPlaceOneHook, ll::memory::HookPriority::Normal, ContainerScreenController, &ContainerScreenController::$_handlePlaceOne, void, ::std::string const& collectionName, int index) {
    if (tc::askClick(*this, collectionName, index)) return;
    origin(collectionName, index);
}

LL_TYPE_INSTANCE_HOOK(TgearTakeHalfHook, ll::memory::HookPriority::Normal, ContainerScreenController, &ContainerScreenController::$_handleTakeHalf, void, ::std::string const& collectionName, int index) {
    if (tc::askClick(*this, collectionName, index)) return;
    origin(collectionName, index);
}

} // namespace

namespace gf::ext::tgear::client {

void installHooks() {
    TgearPlaceOneHook::hook();
    TgearTakeHalfHook::hook();
    TgearTurnHook::hook();
}
void uninstallHooks() {
    TgearPlaceOneHook::unhook();
    TgearTakeHalfHook::unhook();
    TgearTurnHook::unhook();
}

} // namespace gf::ext::tgear::client

#endif // GF_CLIENT
