// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U28's contract extension (rule R18, for A8 to fold into F12's client.render in W2b): a client-side
// material-instance swap. A block definition is one for every client, and a per-position colour
// (F12's tint::registerBlock) needs a tint method on the material, which vanilla clients would
// multiply with a biome colour. So a block's definition names a "baked" texture without a tint
// method (what vanilla clients draw: TF's texture times its item colour), and the client mod, when
// it resolves the block's materials (ClientBlockPipeline::MaterialRepository::initializeMaterials),
// swaps that texture for the untinted one with a tint method, which F12's tint resolver colours per
// position. Block items (minecraft:item_visual) are never swapped.
//
// Registered here (both builds; the swap itself is in the client build only):
//   swapTexture(baked, untinted): a material naming terrain texture `baked` draws `untinted`
//   with the water tint method in the client mod.

#include <string>

namespace gf::feature::ext::aurora::materials {

void swapTexture(std::string const& baked, std::string const& untinted);
/// The swaps made since the process started (client build; 0 on BDS): the client scenario's check.
[[nodiscard]] int swapsMade();

} // namespace gf::feature::ext::aurora::materials
