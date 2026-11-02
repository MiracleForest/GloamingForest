#pragma once

// Helpers shared by F10's selftests (CombatTests.cpp, ContractTests.cpp).

#include <string>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "core/GlobalPos.h"
#include "game/Items.h"

namespace gf::combat::testutil {

/// The items of `id` in `list`.
inline int countIn(std::vector<ItemStack> const& list, std::string const& id) {
    int n = 0;
    for (auto const& s : list)
        if (game::items::idOf(s) == id) n += game::items::countOf(s);
    return n;
}

/// The items of `id` in the player's inventory.
inline int countInInventory(Player& p, std::string const& id) {
    int   n   = 0;
    auto& inv = p.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i)
        if (game::items::idOf(inv.getItem(i)) == id) n += game::items::countOf(inv.getItem(i));
    return n;
}

/// Empties the player's inventory and off-hand.
inline void clearInventory(Player& p) {
    auto& inv = p.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) inv.setItem(i, ItemStack{});
    p.setOffhandSlot(ItemStack{});
}

/// The item entities within `radius` of `pos`: their stacks, and they are removed (no leftovers in
/// the shared test row).
inline std::vector<ItemStack> takeItemEntitiesNear(core::GlobalPos const& pos, float radius) {
    std::vector<ItemStack> out;
    auto                   level = ll::service::getLevel();
    if (!level) return out;
    for (auto* a : level->getRuntimeActorList()) {
        if (!a || !a->isType(ActorType::ItemEntity) || static_cast<int>(a->getDimensionId()) != pos.dim) continue;
        auto const& at = a->getPosition();
        float       dx = at.x - (pos.x + .5f), dy = at.y - (pos.y + .5f), dz = at.z - (pos.z + .5f);
        if (dx * dx + dy * dy + dz * dz > radius * radius) continue;
        out.push_back(static_cast<ItemActor*>(a)->item());
        a->remove();
    }
    return out;
}

} // namespace gf::combat::testutil
