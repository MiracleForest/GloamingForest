#pragma once

// Contract "landmark" (header owned by F0, implemented by F6). The pure grid maths are in
// core/landmark/Landmark.h.
//
// Landmarks are TF's structures with a center on the landmark grid (LegacyLandmarkPlacements,
// LandmarkGridPlacement / AvoidLandmarkGridPlacement). F6 reserves every site and keeps the starts
// persistent; the structure units (U18-U30) register a generator for their key:
//
//     gf::landmark::registerGenerator("twilightforest:lich_tower", {
//         .layout = [](gf::landmark::LayoutContext& ctx) { ctx.pieces.push_back(...); return true; },
//     });
//     if (gf::landmark::isProtectedFor(player, pos)) ...;              // ProgressionEvents
//     gf::landmark::setConquered(dim, pos, "twilightforest:lich_tower", true);  // structure_cleared
//
// Protection runs at interactions priority Highest (F6). Descriptors carry the data of TF's
// structure records: icon, lock advancements, hint, decoration clearance, controlled spawns
// (dark tower floors included), spawn overrides, conquerable.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/GlobalPos.h"
#include "feature/structure/Structure.h"

class BlockPos;
class BlockSource;
class Player;

namespace gf::landmark {

using core::BlockBox;
using core::GlobalPos;
using Json = nlohmann::json;

/// A chunk coordinate.
struct ChunkPos {
    int x = 0, z = 0;
    friend bool operator==(ChunkPos const&, ChunkPos const&) = default;
};

/// TF's per-structure record (DecorationClearance, StructureHints, ControlledSpawns,
/// ProgressionStructure, ConquerableStructure, the structure's map icon).
struct Descriptor {
    std::string              key;                  ///< "twilightforest:lich_tower"
    std::string              icon;                 ///< map decoration id
    std::vector<std::string> lockAdvancements;     ///< required advancements (progression lock)
    std::string              hintBookKey;          ///< "twilightforest.book.lich_tower" (F6 owns the book keys)
    bool                     conquerable  = false;
    bool                     surfaceDecorationsAllowed = false; ///< DecorationClearance
    bool                     undergroundDecorationsAllowed = true;
    int                      clearanceChunks = 0;
    Json                     controlledSpawns;     ///< per floor / area spawn lists
    Json                     spawnOverrides;
    Json                     extra;                ///< anything else of the TF record
};

/// A placed (or reserved) landmark start, persisted per world.
struct Start {
    std::string            key;
    int                    dimension = 0;
    GlobalPos              center;
    BlockBox               box;            ///< union of the pieces
    std::vector<BlockBox>  pieceBoxes;
    std::int64_t           seed = 0;
    bool                   conquered = false;
    bool                   generated = false; ///< false while no generator exists for the key
    Json                   data;
};

/// What a generator's layout step gets.
struct LayoutContext {
    Start&                                         start;
    structure::RandomSource&                       random;
    std::vector<std::unique_ptr<structure::Piece>> pieces; ///< filled by the layout
};

struct Generator {
    /// Builds the pieces of a start (Java's Structure#findGenerationPoint + generatePieces).
    std::function<bool(LayoutContext& ctx)> layout;
    /// Optional terrain shaping before noise (CustomDensitySource / beardifier equivalent).
    std::function<double(Start const& start, int x, int y, int z)> terraform;
    /// Optional: which controlled-spawn list applies at a position (dark tower floors, ...).
    std::function<int(Start const& start, BlockPos const& pos)> spawnIndex;
    /// Optional: whether the piece containing `pos` is protected (default: every piece).
    std::function<bool(Start const& start, BlockPos const& pos)> pieceProtected;
};

/// The descriptor of `key` (nullptr: not a landmark).
[[nodiscard]] Descriptor const* descriptor(std::string const& key);
void registerGenerator(std::string const& key, Generator generator);

/// The start whose box contains `pos` in `dimension` (nullopt: none).
[[nodiscard]] std::optional<Start> startAt(int dimension, BlockPos const& pos);
/// Every start whose box intersects chunk `chunk`.
[[nodiscard]] std::vector<Start> startsIntersecting(int dimension, ChunkPos chunk);
[[nodiscard]] bool isConquered(Start const& start);
/// Marks the start at `pos` conquered (or not); conquering fires the structure_cleared trigger
/// for players within 32 blocks (gf::progression).
void setConquered(int dimension, BlockPos const& pos, std::string const& key, bool conquered);
/// Whether `pos` is protected against `player` (locked landmark, ProgressionEvents rules).
[[nodiscard]] bool isProtectedFor(Player const& player, BlockPos const& pos);
/// Gives `player` the hint book of `start` and spawns a kobold carrying it (TF's hint system),
/// rate limited; true when a hint was shown.
bool trySpawnHint(Player& player, Start const& start, BlockPos const& pos);
/// Generates landmark `key` at `pos` now (commands, saplings: hollow_oak_sapling -> hollow_tree).
bool generateAt(std::string const& key, BlockSource& region, BlockPos const& pos);
/// Creates (and generates) a start of `key` at `pos` without the grid (selftests, /tf place).
[[nodiscard]] std::optional<Start> forceStart(std::string const& key, GlobalPos const& pos);
/// The tfEnforcedProgression gamerule (locked landmarks and biomes enforced).
[[nodiscard]] bool enforcedProgression();
/// Adds a terrain shaper for `key` (TrunkUnderDensityFunction, hollow hill hollows, ...).
void addTerraformer(std::string const& key, std::function<double(Start const& start, int x, int y, int z)> fn);
/// Kinds of decoration a chunk may suppress (DecorationClearance).
enum class DecoKind { Surface, Underground, Trees, Lakes };
[[nodiscard]] bool decorationAllowed(ChunkPos chunk, DecoKind kind);

} // namespace gf::landmark
