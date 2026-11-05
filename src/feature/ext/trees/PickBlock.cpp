// Contract extension for U3 (rule R18), see PickBlock.h. Inventory#setPickedItem and
// Inventory#getSuitableHotbarSlot are translated from Minecraft 1.21.1's behaviour as TF relies on it
// for HardenedDarkLeavesBlock#getCloneItemStack.
#include "feature/ext/trees/PickBlock.h"

#include <map>
#include <mutex>

#include "ll/api/memory/Hook.h"

#include "mc/network/NetworkIdentifier.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/network/packet/BlockPickRequestPacket.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::trees_ext {

namespace {
std::mutex& mapMutex() {
    static std::mutex m;
    return m;
}
std::map<std::string, std::string>& pickMap() {
    static std::map<std::string, std::string> m;
    return m;
}
} // namespace

void setPickItem(std::string const& blockId, std::string const& itemId) {
    std::lock_guard lock(mapMutex());
    pickMap()[blockId] = itemId;
}

std::string pickItem(std::string const& blockId) {
    std::lock_guard lock(mapMutex());
    auto            it = pickMap().find(blockId);
    return it == pickMap().end() ? std::string{} : it->second;
}

bool pickBlock(Player& player, BlockPos const& pos) {
    auto const& block  = player.getDimensionBlockSource().getBlock(pos);
    std::string itemId = pickItem(block.getTypeName());
    if (itemId.empty()) return false;
    auto picked = game::items::make(itemId, 1);
    if (!picked) return true;
    auto&     inv      = player.getInventory();
    int const selected = player.getSelectedItemSlot();
    // Inventory#getSuitableHotbarSlot: the selected slot when empty, else the next empty one, else
    // the selected slot
    auto suitable = [&] {
        if (game::items::isEmpty(inv.getItem(selected))) return selected;
        for (int k = 1; k < 9; ++k) {
            int i = (selected + k) % 9;
            if (game::items::isEmpty(inv.getItem(i))) return i;
        }
        return selected;
    };
    // Inventory#findSlotMatchingItem
    int found = -1;
    for (int i = 0; i < 36 && i < inv.getContainerSize(); ++i) {
        auto const& s = inv.getItem(i);
        if (!game::items::isEmpty(s) && game::items::sameItemSameData(s, *picked)) {
            found = i;
            break;
        }
    }
    if (found >= 0 && found < 9) {
        player.setSelectedSlot(found); // in the hotbar: select it
    } else if (found >= 9) {
        // Inventory#pickSlot: into the suitable hotbar slot, the hotbar's stack goes back
        int  target = suitable();
        auto inHand = inv.getItem(target);
        auto moving = inv.getItem(found);
        inv.setItem(target, moving);
        inv.setItem(found, inHand);
        player.setSelectedSlot(target);
    } else if (player.isCreative()) {
        int target = suitable();
        inv.setItem(target, *picked);
        player.setSelectedSlot(target);
    } else {
        return true; // survival without the item: nothing
    }
    player.sendInventory(true);
    return true;
}

} // namespace gf::trees_ext

LL_TYPE_INSTANCE_HOOK(
    TreesExtPickBlockHook,
    ll::memory::HookPriority::Normal,
    ServerNetworkHandler,
    &ServerNetworkHandler::$handle,
    void,
    NetworkIdentifier const&      source,
    BlockPickRequestPacket const& packet
) {
    if (auto* player = thisFor<NetEventCallback>()->_getServerPlayer(source, packet.mSenderSubId)) {
        try {
            if (gf::trees_ext::pickBlock(*player, packet.mPos)) return;
        } catch (std::exception const& e) {
            gf::log().error("trees: pick block failed: {}", e.what());
        }
    }
    origin(source, packet);
}

namespace gf::trees_ext {
namespace {
struct PickBlockModule {
    void install() { TreesExtPickBlockHook::hook(); }
    void uninstall() { TreesExtPickBlockHook::unhook(); }
};
} // namespace
} // namespace gf::trees_ext

GF_REGISTER_MODULE(gf::trees_ext::PickBlockModule);
