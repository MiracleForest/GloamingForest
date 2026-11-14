#pragma once

// Contract extension of "features" for U3 (rule R18: new files; A8 folds it into F5's built-in
// vanilla feature types). Minecraft's own huge mushroom feature types, minecraft:huge_brown_mushroom
// and minecraft:huge_red_mushroom (AbstractHugeMushroomFeature, HugeBrownMushroomFeature,
// HugeRedMushroomFeature), are not among the features contract's built-in vanilla types; upstream's
// tree/selector/vanilla/vanilla_mushrooms (U3) places them by inline config. The helpers that map
// Java's six huge mushroom block faces to Bedrock's huge_mushroom_bits are shared with U3's canopy
// mushrooms.
//
// Missing on F5's side as well (routed to F5): the configured features minecraft:huge_brown_mushroom /
// minecraft:huge_red_mushroom (Minecraft's TreeFeatures data) that a feature id reference would need.

#include <string>

#include "nlohmann/json.hpp"

class Block;

namespace gf::trees_ext {

/// Java's huge mushroom block faces.
struct Faces {
    bool up = false, down = false, north = false, east = false, south = false, west = false;
};

/// A provider's huge mushroom state ({"state": {"Name", "Properties"}}).
struct MushroomState {
    std::string id;
    Faces       faces;
    bool        hasFaces = false;
};
[[nodiscard]] MushroomState mushroomStateOf(nlohmann::json const& provider);

/// Bedrock's huge_mushroom_bits for Java's faces of a cap (0 pores .. 9 cap corners, 14 all cap) or a
/// stem (10 stem sides, 15 all stem).
[[nodiscard]] int mushroomBits(Faces const& faces, bool stem);

/// The Bedrock block of a Java huge mushroom block with these faces (nullptr: unknown id).
[[nodiscard]] Block const* mushroomBlock(MushroomState const& state, Faces const& faces);

/// Registers the feature types minecraft:huge_brown_mushroom and minecraft:huge_red_mushroom.
void registerVanillaHugeMushrooms();

} // namespace gf::trees_ext
