#pragma once

// Java -> Bedrock ids of the vanilla content upstream's data names (F9 owns this table for its data
// runtimes: tags, data maps and recipes). upstream's tag, data map and recipe files are Java data: their
// `minecraft:` ids are Java's (minecraft:dirt_path, minecraft:nether_bricks, minecraft:cobweb,
// minecraft:zombified_piglin, ...), while the game and every caller of gf::tags / gf::datamaps /
// gf::recipes use Bedrock ids (minecraft:grass_path, minecraft:nether_brick, minecraft:web,
// minecraft:zombie_pigman). The runtimes rename the data's ids at load, by registry (the same Java
// id can name different things: the block minecraft:nether_bricks is Bedrock's block
// minecraft:nether_brick, the item minecraft:nether_brick is Bedrock's item minecraft:netherbrick).
// Queries are never renamed: they are Bedrock ids already (Bedrock's minecraft:snow is Java's
// snow_block), and the renaming is not idempotent, so it applies to Java data only.
//
// Every Bedrock id of the tables resolves in the game's registries (selftest item.java_ids_registry,
// which also resolves every vanilla id the loaded tags, data maps and recipes produce).
// Duplication for A8 to fold: F5 (features/Blocks.cpp) and F7 (structure/BlockStates.cpp
// bedrockId / builtinTags) keep block tables of their own for block states; the three belong in one
// game:: table.

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gf::tags::javaids {

/// The Bedrock ids of a Java id of registry directory `dir` ("block", "item", "entity_type"): one
/// or more for a renamed or split id (minecraft:water -> water, flowing_water), else the id itself.
[[nodiscard]] std::vector<std::string> bedrock(std::string_view dir, std::string const& javaId);
/// The first (main) Bedrock id of bedrock().
[[nodiscard]] std::string primary(std::string_view dir, std::string const& javaId);
/// The renamed ids of a registry directory (Java id -> Bedrock ids), sorted (selftests).
[[nodiscard]] std::vector<std::pair<std::string, std::vector<std::string>>> entries(std::string_view dir);

} // namespace gf::tags::javaids
