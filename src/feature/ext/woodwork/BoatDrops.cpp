// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/enums/extensions/TFBoatTypeEnumExtension.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Contract extension of U4 woodwork (BoatDrops.h): Boat#getDropItem of upstream's boat types on the game's
// boat. The game's destroy spawns the item of the boat's variant (Boat::getWoodID: oak_boat,
// spruce_boat, ..., bamboo_raft; the chest boat's <wood>_chest_boat) and, for a chest boat, the
// chest's contents. Only a NEW item actor of exactly that item (one plain item, no user data) is
// the boat's own drop; at most one is replaced per boat, and a nested Boat::$destroy inside
// ChestBoat::$destroy does not replace again.

#include "feature/ext/woodwork/BoatDrops.h"

#include <array>
#include <set>
#include <utility>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/Boat.h"
#include "mc/world/actor/item/ChestBoat.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/phys/AABB.h"

#include "game/Items.h"
#include "mod/Log.h"

namespace gf::woodwork_ext::boatdrops {

namespace {

ReplacementFn& replacement() {
    static ReplacementFn fn;
    return fn;
}

/// Inside a replacing destroy (a nested Boat::$destroy of ChestBoat::$destroy runs plain).
thread_local bool tReplacing = false;

/// The item actors around the boat (the game drops the boat item at the boat).
std::vector<ItemActor*> itemsAround(Actor& boat) {
    std::vector<ItemActor*> out;
    auto& region = boat.getDimensionBlockSource();
    auto  pos    = boat.getPosition();
    AABB  box{Vec3{pos.x - 3.f, pos.y - 3.f, pos.z - 3.f}, Vec3{pos.x + 3.f, pos.y + 3.f, pos.z + 3.f}};
    for (Actor* a : region.getEntities(box, 0.0f)) {
        if (a && a->getTypeName() == "minecraft:item") out.push_back(static_cast<ItemActor*>(a));
    }
    return out;
}

std::int64_t idOf(Actor& a) { return a.getOrCreateUniqueID().rawID; }

/// Boat::getWoodID's wood names (the game's boat variants, in the order of the legacy boat aux).
constexpr std::array<char const*, 10> kWoods{"oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "mangrove", "bamboo", "cherry", "pale_oak"};

/// The item the game's destroy drops for `boat` ("" when its wood is unknown).
std::string expectedDrop(Actor& boat, bool chest) {
    auto wood = static_cast<std::size_t>(static_cast<Boat&>(boat).getWoodID());
    if (wood >= kWoods.size()) return {};
    std::string name = kWoods[wood];
    if (name == "bamboo") return chest ? "minecraft:bamboo_chest_raft" : "minecraft:bamboo_raft";
    return "minecraft:" + name + (chest ? "_chest_boat" : "_boat");
}

/// Whether `stack` can be the game's own boat drop: the item, one of it, nothing else on it.
bool isOwnDrop(ItemStack const& stack, std::string const& expected) {
    return game::items::idOf(stack) == expected && stack.mCount == 1 && !stack.mUserData;
}

/// Runs the game's destroy and replaces the boat item it spawned for a boat that has a replacement.
template <class Origin>
void destroyWith(Actor& boat, bool chest, Origin&& origin) {
    if (tReplacing) {
        origin();
        return;
    }
    auto const& fn = replacement();
    std::optional<std::string> tf;
    if (fn && !boat.getDimensionBlockSource().getLevel().isClientSide()) tf = fn(boat);
    std::string expected = tf ? expectedDrop(boat, chest) : std::string();
    if (!tf) {
        origin();
        return;
    }
    std::set<std::int64_t> before;
    for (auto* a : itemsAround(boat)) before.insert(idOf(*a));
    {
        struct Guard {
            Guard() { tReplacing = true; }
            ~Guard() { tReplacing = false; }
        } guard;
        origin();
    }
    auto& region = boat.getDimensionBlockSource();
    // the new item actors of the expected item; a chest boat also drops its chest's contents, so
    // the own drop is the plain stack (contents with data or several items are never taken). If the
    // chest held the same plain boat item, the stacks are identical: either one is the drop.
    // the exact item first; if the game's item is named otherwise (a wood the table spells
    // differently), the one new plain item of the boat kind (a chest boat's: a chest boat item)
    ItemActor* own = nullptr;
    for (auto* a : itemsAround(boat)) {
        if (!before.contains(idOf(*a)) && isOwnDrop(a->item(), expected)) {
            own = a;
            break;
        }
    }
    if (!own) {
        for (auto* a : itemsAround(boat)) {
            if (before.contains(idOf(*a))) continue;
            auto const& st = a->item();
            auto        id = game::items::idOf(st);
            bool kind = id.starts_with("minecraft:") && (chest ? (id.ends_with("_chest_boat") || id.ends_with("_chest_raft")) : (id.ends_with("_boat") || id.ends_with("_raft")) && id.find("_chest_") == std::string::npos);
            if (kind && st.mCount == 1) {
                own = a;
                log().warn("woodwork: the game dropped {} for a {} (expected {})", id, boat.getTypeName(), expected);
                break;
            }
        }
    }
    if (own) {
        auto* a = own;
        Vec3 at     = a->getPosition();
        Vec3 motion = a->getPosDelta();
        int  delay  = a->pickupDelay();
        a->remove();
        if (auto s = game::items::make(*tf, 1)) {
            // the same spot, motion and pickup delay as the game's drop
            if (auto* fresh = region.getLevel().getSpawner().spawnItem(region, *s, nullptr, at, 0)) {
                static_cast<ItemActor*>(fresh)->pickupDelay() = delay;
                const_cast<Vec3&>(fresh->getPosDelta())       = motion;
            }
        }
        log().info("woodwork: a destroyed {} dropped {}", boat.getTypeName(), *tf);
        return; // one per boat
    }
}

} // namespace

// Boat#destroy is the game's own drop of a boat (no loot table, no contract event).
LL_TYPE_INSTANCE_HOOK(WoodworkExtBoatDestroyHook, ll::memory::HookPriority::Normal, Boat, &Boat::$destroy, void, ::Actor* source) {
    destroyWith(*this, false, [&] { origin(source); });
}

LL_TYPE_INSTANCE_HOOK(WoodworkExtChestBoatDestroyHook, ll::memory::HookPriority::Normal, ChestBoat, &ChestBoat::$destroy, void, ::Actor* source) {
    destroyWith(*this, true, [&] { origin(source); });
}

void setReplacement(ReplacementFn fn) { replacement() = std::move(fn); }

std::string vanillaDropOf(Actor& boat) { return expectedDrop(boat, boat.getTypeName() == "minecraft:chest_boat" || boat.getTypeName().ends_with(":chest_boat")); }

void install() {
    WoodworkExtBoatDestroyHook::hook();
    WoodworkExtChestBoatDestroyHook::hook();
}

void uninstall() {
    WoodworkExtChestBoatDestroyHook::unhook();
    WoodworkExtBoatDestroyHook::unhook();
}

} // namespace gf::woodwork_ext::boatdrops
