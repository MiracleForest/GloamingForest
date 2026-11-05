// GloamingForest - U11's extension hooks (CraftHooks.h): grindstone result and take, anvil refusal.
#include "feature/ext/tgear/CraftHooks.h"

#include <mutex>
#include <utility>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/inventory/network/ContainerScreenContext.h"
#include "mc/world/inventory/network/ItemStackNetIdVariant.h"
#include "mc/world/inventory/network/ItemStackNetResult.h"
#include "mc/world/inventory/network/crafting/CraftHandlerGrindstone.h"
#include "mc/world/inventory/network/crafting/ItemStackRequestActionCraftBase.h"
#include "mc/world/inventory/simulation/AllowedAnywhereConsume.h"
#include "mc/world/inventory/simulation/ContainerScreenValidation.h"
#include "mc/world/inventory/simulation/ContainerValidationCommitObject.h"
#include "mc/world/inventory/simulation/ExpectedAnywhereConsume.h"
#include "mc/world/inventory/simulation/ExpectedSlotConsume.h"
#include "mc/world/inventory/simulation/ContainerValidationCraftInputs.h"
#include "mc/world/inventory/simulation/ContainerValidationCraftResult.h"
#include "mc/world/inventory/simulation/validation/AnvilContainerScreenValidator.h"
#include "mc/world/inventory/simulation/validation/DynamicContainerValidation.h"
#include "mc/world/inventory/simulation/validation/ShulkerBoxContainerValidation.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/level/block/actor/ShulkerBoxBlockActor.h"
#include "mc/world/item/ItemStack.h"

#include "mod/Log.h"

namespace gf::ext::tgear {

namespace {

struct State {
    std::mutex                 mutex;
    std::vector<GrindResultFn> grind;
    std::vector<GrindTakenFn>  taken;
    std::vector<AnvilRefuseFn> anvil;
    std::vector<StorageRefuseFn> storage;
};

State& state() {
    static State s;
    return s;
}

template <class T>
std::vector<T> copyOf(std::vector<T> const& v) {
    std::lock_guard lock(state().mutex);
    return v;
}

} // namespace

void onGrindResult(GrindResultFn fn) {
    std::lock_guard lock(state().mutex);
    state().grind.push_back(std::move(fn));
}
void onGrindTaken(GrindTakenFn fn) {
    std::lock_guard lock(state().mutex);
    state().taken.push_back(std::move(fn));
}
void onAnvilRefuse(AnvilRefuseFn fn) {
    std::lock_guard lock(state().mutex);
    state().anvil.push_back(std::move(fn));
}

void onStorageRefuse(StorageRefuseFn fn) {
    std::lock_guard lock(state().mutex);
    state().storage.push_back(std::move(fn));
}

bool storageRefused(ItemStackBase const& item) {
    if (item.isNull()) return false;
    for (auto const& fn : copyOf(state().storage)) {
        try {
            if (fn(item)) return true;
        } catch (...) {}
    }
    return false;
}

std::optional<ItemStack> grindResult(ItemStack const& input, ItemStack const& additional) {
    for (auto const& fn : copyOf(state().grind)) {
        try {
            if (auto r = fn(input, additional)) return r;
        } catch (std::exception const& e) {
            gf::log().error("tgear: grindstone listener failed: {}", e.what());
        }
    }
    return std::nullopt;
}

void grindTaken(Player& player, ItemStack const& input, ItemStack const& additional) {
    for (auto const& fn : copyOf(state().taken)) {
        try {
            fn(player, input, additional);
        } catch (std::exception const& e) {
            gf::log().error("tgear: grindstone listener failed: {}", e.what());
        }
    }
}

bool anvilRefused(Player& player, ItemStack const& input, ItemStack const& material) {
    for (auto const& fn : copyOf(state().anvil)) {
        try {
            if (fn(player, input, material)) return true;
        } catch (std::exception const& e) {
            gf::log().error("tgear: anvil listener failed: {}", e.what());
        }
    }
    return false;
}

} // namespace gf::ext::tgear

namespace {

namespace tx = gf::ext::tgear;
using GrindResultPair = ::std::pair<::ItemStack, ::ItemStackNetIdVariant>;

LL_STATIC_HOOK(
    TgearGrindResultHook,
    ll::memory::HookPriority::Normal,
    &CraftHandlerGrindstone::getResultItem,
    GrindResultPair,
    ::ItemStack const& input,
    ::ItemStack const& additional
) {
    auto result = origin(input, additional);
    if (auto mine = tx::grindResult(input, additional)) result.first = *mine;
    return result;
}

LL_TYPE_INSTANCE_HOOK(
    TgearGrindCraftHook,
    ll::memory::HookPriority::Normal,
    CraftHandlerGrindstone,
    &CraftHandlerGrindstone::$_handleCraftAction,
    ::ItemStackNetResult,
    ::ItemStackRequestActionCraftBase const& requestAction
) {
    Player&   player = mPlayer;
    auto&     ui     = player.mPlayerUIContainer.get();
    ItemStack input  = ui.getItem(static_cast<int>(PlayerUISlot::GrindstoneInput));
    ItemStack add    = ui.getItem(static_cast<int>(PlayerUISlot::GrindstoneAdditional));
    auto      r      = origin(requestAction);
    if (r == ItemStackNetResult::Success && !player.isClientSide()) tx::grindTaken(player, input, add);
    return r;
}

/// Empties an anvil result the listeners refuse: no result and nothing consumed, so the take request fails
/// and the client's slots go back.
void refuseAnvil(ContainerScreenContext const& screenContext, ContainerValidationCraftResult& result) noexcept {
    try {
        Player* player = screenContext.mPlayer;
        if (!player) return;
        auto& ui = player->mPlayerUIContainer.get();
        if (!tx::anvilRefused(*player, ui.getItem(static_cast<int>(PlayerUISlot::AnvilInput)), ui.getItem(static_cast<int>(PlayerUISlot::AnvilMaterial)))) return;
        result.mResults->clear();
        result.mExpectedSlotConsumes->clear();
        result.mExpectedAnywhereConsumes->clear();
        result.mAllowedAnywhereConsumes->clear();
        result.mCommitObjects->clear();
    } catch (...) {
    }
}

LL_TYPE_INSTANCE_HOOK(
    TgearAnvilResultHook,
    ll::memory::HookPriority::Normal,
    AnvilContainerScreenValidator,
    &AnvilContainerScreenValidator::$getCraftResult,
    ::ContainerValidationCraftResult,
    ::ContainerScreenContext const&                     screenContext,
    ::ContainerScreenValidation&                        screenValidation,
    ::std::unique_ptr<::ContainerValidationCraftInputs> craftInputs,
    uchar const                                         numCrafts
) {
    // the result lives in the return slot (no copy, no unwinding: the game exports no destructor of it)
    auto result = origin(screenContext, screenValidation, std::move(craftInputs), numCrafts);
    refuseAnvil(screenContext, result);
    return result;
}

LL_TYPE_INSTANCE_HOOK(
    TgearShulkerSlotHook,
    ll::memory::HookPriority::Normal,
    ShulkerBoxContainerValidation,
    &ShulkerBoxContainerValidation::$isItemAllowedInSlot,
    bool,
    ::ContainerScreenContext const& screenContext,
    int const                       slot,
    ::ItemStackBase const&          item,
    int const                       amount,
    bool                            internal
) {
    if (tx::storageRefused(item)) return false;
    return origin(screenContext, slot, item, amount, internal);
}

LL_TYPE_INSTANCE_HOOK(
    TgearBundleSlotHook,
    ll::memory::HookPriority::Normal,
    DynamicContainerValidation,
    &DynamicContainerValidation::$isItemAllowedInSlot,
    bool,
    ::ContainerScreenContext const& screenContext,
    int const                       slot,
    ::ItemStackBase const&          item,
    int const                       amount,
    bool                            internal
) {
    if (tx::storageRefused(item)) return false;
    return origin(screenContext, slot, item, amount, internal);
}

LL_TYPE_INSTANCE_HOOK(
    TgearShulkerHopperHook,
    ll::memory::HookPriority::Normal,
    ShulkerBoxBlockActor,
    &ShulkerBoxBlockActor::$canPushInItem,
    bool,
    int                slot,
    int                face,
    ::ItemStack const& item
) {
    if (tx::storageRefused(item)) return false;
    return origin(slot, face, item);
}

} // namespace

namespace gf::ext::tgear {

void installCraftHooks() {
    TgearShulkerSlotHook::hook();
    TgearBundleSlotHook::hook();
    TgearShulkerHopperHook::hook();
    TgearGrindResultHook::hook();
    TgearGrindCraftHook::hook();
    TgearAnvilResultHook::hook();
}
void uninstallCraftHooks() {
    TgearShulkerHopperHook::unhook();
    TgearBundleSlotHook::unhook();
    TgearShulkerSlotHook::unhook();
    TgearAnvilResultHook::unhook();
    TgearGrindCraftHook::unhook();
    TgearGrindResultHook::unhook();
}

} // namespace gf::ext::tgear
