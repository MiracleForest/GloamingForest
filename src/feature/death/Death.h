#pragma once

// Contract "death" (header owned by F0, implemented by F10; rule R8).
//
// The death pipeline (TF's CharmEvents host): lethal damage first asks the `onLethal` handlers
// (charm of life: survive), then keepers decide which items a dying player keeps (charms of
// keeping, phantom armour, the kept_on_death tag), the remaining drops can be taken (keepsake
// casket), and kept items come back on respawn.
//
//     gf::death::onLethal(0, [](Actor& victim, gf::combat::DamageInfo const& d) { return useCharmOfLife(victim); });
//     gf::death::registerKeeper(0, [](Player& p, ItemStack& s) { return isPhantomArmor(s); });

#include <functional>
#include <vector>

#include "feature/combat/Combat.h"

class Actor;
class ItemStack;
class Player;

namespace gf::death {

/// Lethal damage is about to kill `victim`: return true to survive (the handler sets the health).
using LethalFn = std::function<bool(Actor& victim, combat::DamageInfo const& damage)>;
void onLethal(int priority, LethalFn fn);

/// A dying player's stack: return true to keep it through death (returned on respawn).
using KeeperFn = std::function<bool(Player& player, ItemStack& stack)>;
void registerKeeper(int priority, KeeperFn fn);

/// The drops left after keepers: a handler may take (move out of) any of them.
using DropsFn = std::function<void(Player& player, std::vector<ItemStack>& drops)>;
void onDropsRemaining(DropsFn fn);

/// After the player respawned and got the kept items back.
using RespawnFn = std::function<void(Player& player)>;
void onRespawn(RespawnFn fn);

} // namespace gf::death
