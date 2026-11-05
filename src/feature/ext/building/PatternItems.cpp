// Extension of contract "recipes" (loom) by U5 (see PatternItems.h).

#include "feature/ext/building/PatternItems.h"

#include <atomic>
#include <map>
#include <mutex>

#include "ll/api/memory/Hook.h"

#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/inventory/network/ContainerScreenContext.h"
#include "mc/world/inventory/simulation/validation/LoomMaterialContainerValidation.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/level/block/actor/BannerPattern.h"

#include "mod/Log.h"

namespace gf::building::ext {

namespace {

struct Registry {
    std::mutex                         mutex;
    std::map<std::string, std::string> itemOfPattern; ///< pattern nameID -> item id
    std::map<std::string, std::string> patternOfItem; ///< item id -> pattern nameID
};

Registry& reg() {
    static Registry r;
    return r;
}

} // namespace

void registerPatternItem(std::string const& patternNameId, std::string const& itemId) {
    std::lock_guard lock(reg().mutex);
    reg().itemOfPattern[patternNameId] = itemId;
    reg().patternOfItem[itemId]        = patternNameId;
}

std::string patternItemOf(std::string const& patternNameId) {
    std::lock_guard lock(reg().mutex);
    auto            it = reg().itemOfPattern.find(patternNameId);
    return it == reg().itemOfPattern.end() ? std::string() : it->second;
}

bool isPatternItem(std::string const& itemId) {
    std::lock_guard lock(reg().mutex);
    return reg().patternOfItem.contains(itemId);
}

} // namespace gf::building::ext

namespace {

namespace bx = gf::building::ext;

LL_TYPE_INSTANCE_HOOK(PatternItemMatchHook, ll::memory::HookPriority::Normal, BannerPattern, &BannerPattern::matchesPatternItem, bool, ItemStackBase const& item) {
    auto own = bx::patternItemOf(*this->mNameID);
    if (!own.empty()) return !item.isNull() && item.getTypeName() == own;
    // a registered item unlocks no other pattern (a vanilla pattern never takes it)
    if (!item.isNull() && bx::isPatternItem(item.getTypeName())) return false;
    return origin(item);
}

LL_TYPE_INSTANCE_HOOK(
    PatternItemSlotHook,
    ll::memory::HookPriority::Normal,
    LoomMaterialContainerValidation,
    &LoomMaterialContainerValidation::$isItemAllowedInSlot,
    bool,
    ContainerScreenContext const& screenContext,
    int const                     slot,
    ItemStackBase const&          item,
    int const                     amount,
    bool                          flag
) {
    // BannerPatternItem in LoomMenu's pattern slot: the registered items too, on a loom screen and
    // in the material container's own slot (PlayerUISlot::LoomMaterial, the container's offset)
    if (!item.isNull() && bx::isPatternItem(item.getTypeName())) {
        bool loom   = screenContext.mScreenContainerType == SharedTypes::Legacy::ContainerType::Loom;
        int  offset = this->getContainerOffset(screenContext);
        bool own    = slot == offset || slot == static_cast<int>(PlayerUISlot::LoomMaterial);
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true))
            gf::log().info("building: loom pattern slot check (screen {}, slot {}, offset {})", static_cast<int>(screenContext.mScreenContainerType), slot, offset);
        if (loom && own) return true;
    }
    return origin(screenContext, slot, item, amount, flag);
}

} // namespace

namespace gf::building::ext {

void installPatternItems() {
    PatternItemMatchHook::hook();
    PatternItemSlotHook::hook();
}

void uninstallPatternItems() {
    PatternItemMatchHook::unhook();
    PatternItemSlotHook::unhook();
}

} // namespace gf::building::ext
