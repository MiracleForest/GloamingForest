#pragma once

// U22 (lichdecor) contract extension (rule R18, to be folded by A8): strong power from a custom
// block into one neighbour, through the game's circuit graph.
//
// upstream's DIM candelabra strongly powers its support block (CandelabraBlock#getDirectSignal). The
// redstone contract's registerEmitter only feeds gf::redstone::power() / signalFrom(); vanilla
// mechanisms around the support never see it. This extension puts a DirectionalProducerComponent
// on the custom block's position that connects only towards `towards`, so the support becomes a
// powered conductor of strength `strength` for the vanilla circuit (lamps, pistons, dust beside or
// under the support react). Diode side inputs never read it (the producer connects only to the
// support position, and Bedrock's comparator / repeater side inputs do not read powered blocks).
//
// The component is not part of the block (custom blocks own no circuit component): the circuit
// system forgets it when the chunk unloads. Callers re-apply it (`setDirectedPower` is idempotent)
// whenever they see the block again.

class BlockPos;
class BlockSource;

namespace gf::feature::ext::lichdecor {

/// Makes the block at `at` a producer of `strength` (1..15) into its neighbour on side `towards`
/// (FacingID from `at`); `strength` 0 removes the producer. Returns false when the dimension has
/// no circuit system or another (vanilla) component is at `at`.
bool setDirectedPower(BlockSource& region, BlockPos const& at, int towards, int strength);
/// The strength of the producer this extension keeps at `at` (0: none).
[[nodiscard]] int directedPower(BlockSource& region, BlockPos const& at);

} // namespace gf::feature::ext::lichdecor
