// Adapted from LiteLDev/Levitate v0.1.0 (src/core/common/Transfer.h, key helpers), LGPL-3.0-or-later.
#pragma once

// World DB key helpers, independent of the game. Every world DB key is "gf:<unit>:<...>"
// (rule R16); the unit is the owning unit's directory name ("common" for the foundation).

#include <string>
#include <string_view>

namespace gf::core {

inline constexpr char kWorldDbPrefix[] = "gf:";

/// Chunk coordinate of a block coordinate (floor division by 16).
[[nodiscard]] constexpr int chunkOf(int block) { return block >= 0 ? block / 16 : -((-block + 15) / 16); }

/// "gf:<unit>:<rest>".
[[nodiscard]] std::string worldKey(std::string_view unit, std::string_view rest);
/// Whether `key` is a well-formed world DB key ("gf:<unit>:<rest>", unit [a-z0-9_]+, rest non-empty).
[[nodiscard]] bool validWorldKey(std::string_view key);
/// The unit of a world DB key ("" when malformed).
[[nodiscard]] std::string_view worldKeyUnit(std::string_view key);

/// World DB key of a block data chunk: "gf:common:bd:<dim>:<cx>:<cz>".
[[nodiscard]] std::string blockDataChunkKey(int dim, int cx, int cz);
/// Parses a key suffix "<dim>:<cx>:<cz>" (as `forEachKey` returns it).
[[nodiscard]] bool parseChunkSuffix(std::string_view suffix, int& dim, int& cx, int& cz);
/// Position key inside a chunk document: "x,y,z".
[[nodiscard]] std::string posKey(int x, int y, int z);
[[nodiscard]] bool        parsePosKey(std::string_view key, int& x, int& y, int& z);

} // namespace gf::core
