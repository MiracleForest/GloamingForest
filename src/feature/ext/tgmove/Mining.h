// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
#pragma once

// Contract extension of U12 (rule R18, folded by A8): mining speed modifiers of the wearer, for
// Java's player.submerged_mining_speed attribute (the Travellers' goggles' aquatic agility, +4
// multiply-total = x5 while the eyes are under water). No W0 contract changes a player's mining
// speed by what they wear (F9's destroySpeed is the held item's), so this hooks
// Player::getDestroyProgress (both builds: the client predicts the progress, the server checks it).
//
//     gf::ext::mining::addModifier([](Player& p, Block const& b) { return underwater(p) ? 5.0f : 1.0f; });

#include <functional>

class Block;
class Player;

namespace gf::ext::mining {

/// A factor on the player's destroy progress of `block` (1: unchanged); every modifier multiplies.
using ModifierFn = std::function<float(Player& player, Block const& block)>;
void addModifier(ModifierFn fn);

} // namespace gf::ext::mining
