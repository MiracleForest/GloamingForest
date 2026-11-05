// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U28's contract extension (rule R18, for A8 to fold in W2b): NeoForge's
// IBlockExtension#getBeaconColorMultiplier for custom blocks. Bedrock colours a beacon beam only for
// its own stained glass; a block registered here colours the beam above a beacon as Java does
// (BeaconBlockEntity#tick: the first colour replaces the beacon's white, every further change is
// the average of the section's colour and the new one). Server and client builds: the beam
// sections are rebuilt after the engine's BeaconBlockActor::generateBeamSections.

#include <functional>
#include <string>

class BlockPos;

namespace gf::feature::ext::aurora::beacon {

/// The colour multiplier (0xRRGGBB) of `blockId` at a position.
using ColorFn = std::function<unsigned(BlockPos const& pos)>;
void registerColorMultiplier(std::string const& blockId, ColorFn fn);

} // namespace gf::feature::ext::aurora::beacon
