// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/enums/extensions/TFBoatTypeEnumExtension.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Contract extension of U4 woodwork (BoatDrops.h): Boat#getDropItem of TF's boat types on the game's
// boat. The game's destroy spawns the item of the boat's variant; the items that call spawned (the
// item actors that were not there before it) are the boat's own drop.

#include "feature/ext/woodwork/BoatDrops.h"

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

/// Whether `id` is the item of a game boat (any wood, raft, chest boat).
bool isBoatItem(std::string const& id) { return id.starts_with("minecraft:") && (id.ends_with("_boat") || id.ends_with("_raft")); }

/// Runs the game's destroy and replaces the boat item it spawned for a boat that has a replacement.
template <class Origin>
void destroyWith(Actor& boat, Origin&& origin) {
    auto const& fn = replacement();
    std::optional<std::string> tf;
    if (fn && !boat.getDimensionBlockSource().getLevel().isClientSide()) tf = fn(boat);
    if (!tf) {
        origin();
        return;
    }
    std::set<std::int64_t> before;
    for (auto* a : itemsAround(boat)) before.insert(idOf(*a));
    origin();
    auto& region = boat.getDimensionBlockSource();
    for (auto* a : itemsAround(boat)) {
        if (before.contains(idOf(*a))) continue; // there before this destroy: not its drop
        auto& stack = a->item();
        if (!isBoatItem(game::items::idOf(stack))) continue;
        Vec3 at     = a->getPosition();
        Vec3 motion = a->getPosDelta();
        int  delay  = a->pickupDelay();
        int  count  = stack.mCount;
        a->remove();
        if (auto s = game::items::make(*tf, count)) {
            // the same spot, motion and pickup delay as the game's drop
            if (auto* fresh = region.getLevel().getSpawner().spawnItem(region, *s, nullptr, at, 0)) {
                static_cast<ItemActor*>(fresh)->pickupDelay() = delay;
                const_cast<Vec3&>(fresh->getPosDelta())       = motion;
            }
        }
        log().info("woodwork: a destroyed {} dropped {}", boat.getTypeName(), *tf);
        return;
    }
}

} // namespace

// Boat#destroy is the game's own drop of a boat (no loot table, no contract event).
LL_TYPE_INSTANCE_HOOK(WoodworkExtBoatDestroyHook, ll::memory::HookPriority::Normal, Boat, &Boat::$destroy, void, ::Actor* source) {
    destroyWith(*this, [&] { origin(source); });
}

LL_TYPE_INSTANCE_HOOK(WoodworkExtChestBoatDestroyHook, ll::memory::HookPriority::Normal, ChestBoat, &ChestBoat::$destroy, void, ::Actor* source) {
    destroyWith(*this, [&] { origin(source); });
}

void setReplacement(ReplacementFn fn) { replacement() = std::move(fn); }

void install() {
    WoodworkExtBoatDestroyHook::hook();
    WoodworkExtChestBoatDestroyHook::hook();
}

void uninstall() {
    WoodworkExtChestBoatDestroyHook::unhook();
    WoodworkExtBoatDestroyHook::unhook();
}

} // namespace gf::woodwork_ext::boatdrops
