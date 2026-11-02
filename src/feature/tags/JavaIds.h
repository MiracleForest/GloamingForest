#pragma once

// Java -> Bedrock ids of the vanilla content TF's data names (F9 owns this table for its data
// runtimes: tags and data maps). TF's tag and data map files are Java data: their `minecraft:` ids are
// Java's (minecraft:dirt_path, minecraft:nether_bricks, minecraft:zombified_piglin, ...), while the
// game and every caller of gf::tags / gf::datamaps use Bedrock ids (minecraft:grass_path,
// minecraft:nether_brick, minecraft:zombie_pigman). The runtimes rename the data's ids at load, by
// registry (the same Java id can name different things: the block minecraft:nether_bricks is
// Bedrock's block minecraft:nether_brick, the item minecraft:nether_brick is Bedrock's item
// minecraft:netherbrick). Queries are never renamed: they are Bedrock ids already (Bedrock's
// minecraft:snow is Java's snow_block). F5 (features/Blocks.cpp) and F7 (structure/BlockStates.cpp)
// keep block tables of their own for block states; A8 may fold the three into one game:: table.

#include <string>
#include <string_view>
#include <vector>

namespace gf::tags::javaids {

/// The Bedrock ids of a Java id of registry directory `dir` ("block", "item", "entity_type"): one
/// or more for a renamed or split id (minecraft:water -> water, flowing_water), else the id itself.
[[nodiscard]] std::vector<std::string> bedrock(std::string_view dir, std::string const& javaId);
/// The first (main) Bedrock id of bedrock().
[[nodiscard]] std::string primary(std::string_view dir, std::string const& javaId);

} // namespace gf::tags::javaids
