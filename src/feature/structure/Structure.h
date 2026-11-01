#pragma once

// Contract "structure" (header owned by F0, implemented by F7).
//
// The piece framework of TF's structures (StructurePiece / TFStructureComponent with per-chunk
// clipping), GloamingForest templates (rule R19: procedural pieces are exact translations,
// template-backed pieces use original .gft templates validated against TF's LGPL template
// definitions), processors, marker handlers, wood palettes, random block selectors, jigsaw
// assembly and speleothems.
//
//     class NagaCourtyardMain : public gf::structure::Piece { ... void postProcess(...) override; };
//     gf::structure::registerPieceType("twilightforest:TFNCMn", [](nlohmann::json const& saved) { ... });
//
// The shared bases are part of this contract, declared by F0 in feature/structure/bases/ and
// implemented by F7 (neutral stubs in bases/Bases.cpp until then): Component.h
// (TFStructureComponent, TFStructureDecorator, UtilityPiece, SortablePiece), ComponentOld.h
// (TFStructureComponentOld), TemplatePieces.h (TFStructureComponentTemplate, TwilightTemplate /
// DoubleTemplate / Jigsaw pieces, JigsawRecord), Maze.h (TFMaze), HollowHill.h
// (HollowHillComponent) and Tower.h (the 16 classes of the TowerWing family). Piece-local
// coordinates follow Java: (x, y, z) inside `bbox`, turned by `orientation` and `mirror`.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/world/level/BlockPos.h"

#include "core/GlobalPos.h"
#include "feature/worldgen/Worldgen.h"

class Block;
class BlockSource;

namespace gf::structure {

using core::BlockBox;
using worldgen::RandomSource;
using worldgen::Region;
using Json = nlohmann::json;

/// Java's Direction (horizontal) as a piece orientation.
enum class Orientation { North, East, South, West };
/// Java's Rotation.
enum class Rotation { None, Clockwise90, Clockwise180, Counterclockwise90 };
/// Java's Mirror.
enum class Mirror { None, LeftRight, FrontBack };

/// StructurePiece.BlockSelector / TF's random block selectors ("twilightforest:stronghold_stones").
class BlockSelector {
public:
    virtual ~BlockSelector() = default;
    /// Picks the block for (x, y, z) (`wall`: on the edge of the filled box).
    [[nodiscard]] virtual Block const& next(RandomSource& random, int x, int y, int z, bool wall) = 0;
};

/// Settings of a template placement (Java's StructurePlaceSettings).
struct TemplateSettings {
    Rotation                 rotation = Rotation::None;
    Mirror                   mirror   = Mirror::None;
    std::vector<std::string> processors; ///< processor list ids or inline processor ids
    bool                     ignoreEntities = false;
    std::optional<BlockBox>  clip;       ///< only blocks inside it are placed
    std::int64_t             seed = 0;   ///< processor randomness
};

class Piece {
public:
    explicit Piece(std::string typeId) : typeId(std::move(typeId)) {}
    virtual ~Piece() = default;

    std::string typeId;          ///< the registered piece type ("twilightforest:TFHedge")
    BlockBox    bbox;            ///< world-space bounds
    Orientation orientation = Orientation::North;
    Mirror      mirror      = Mirror::None;
    Rotation    rotation    = Rotation::None;
    int         genDepth    = 0;

    /// Places the part of the piece inside `chunkBox` (Java's postProcess; called once per chunk
    /// the piece intersects).
    virtual void postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) = 0;
    /// Java's addChildren: adds the pieces this one leads to.
    virtual void addChildren(Piece& start, std::vector<std::unique_ptr<Piece>>& pieces, RandomSource& random);
    /// Persisted fields beyond the common ones (StructurePiece#addAdditionalSaveData).
    [[nodiscard]] virtual Json save() const;
    virtual void               load(Json const& data);

    // ---- helpers in piece-local coordinates (StructurePiece / TFStructureComponentOld) --------
    /// getWorldX / getWorldY / getWorldZ (virtual: a few TF pieces override them).
    [[nodiscard]] virtual int worldX(int x, int z) const;
    [[nodiscard]] virtual int worldY(int y) const;
    [[nodiscard]] virtual int worldZ(int x, int z) const;
    /// StructurePiece#getWorldPos: the world position of piece-local (x, y, z).
    [[nodiscard]] BlockPos getWorldPos(int x, int y, int z) const;
    /// StructurePiece#getLocatorPosition: the world position of piece-local (0, 0, 0).
    [[nodiscard]] BlockPos getLocatorPosition() const;
    /// Turns a block for this piece's orientation and mirror.
    [[nodiscard]] Block const& oriented(Block const& block) const;
    virtual void placeBlock(Region& region, Block const& block, int x, int y, int z, BlockBox const& chunkBox);
    /// A block by id and state names ({"facing": "north", "half": "top"}), Java state names
    /// mapped to the frozen Bedrock states of docs/spec/blockstates.md.
    void placeBlock(Region& region, std::string const& blockId, Json const& states, int x, int y, int z, BlockBox const& chunkBox);
    [[nodiscard]] virtual Block const& getBlock(Region& region, int x, int y, int z, BlockBox const& chunkBox) const;
    /// generateBox (edge / inside blocks), generateAirBox and generateBox with a block selector.
    void fill(Region& region, BlockBox const& chunkBox, int x0, int y0, int z0, int x1, int y1, int z1, Block const& edge, Block const& inside, bool keepAir);
    void fillAir(Region& region, BlockBox const& chunkBox, int x0, int y0, int z0, int x1, int y1, int z1);
    void randomFill(Region& region, BlockBox const& chunkBox, int x0, int y0, int z0, int x1, int y1, int z1, bool alwaysReplace, RandomSource& random, BlockSelector& selector);
    /// generateMaybeBox: like `fill`, each block placed with probability `chance` (only over
    /// non-air with `requireNonAir`, only under open sky with `requireSkylight`).
    void generateMaybeBox(Region& region, BlockBox const& chunkBox, RandomSource& random, float chance, int x0, int y0, int z0, int x1, int y1, int z1,
                          Block const& edge, Block const& inside, bool requireNonAir, bool requireSkylight);
    /// maybeGenerateBlock: one block with probability `chance`.
    void maybeGenerateBlock(Region& region, BlockBox const& chunkBox, RandomSource& random, float chance, int x, int y, int z, Block const& block);
    /// fillColumnDown: `block` from (x, y, z) down through air and liquids.
    void fillColumnDown(Region& region, Block const& block, int x, int y, int z, BlockBox const& chunkBox);
    /// A chest-like container block with a loot table (through gf::loot::placeContainer).
    bool placeChest(Region& region, BlockBox const& chunkBox, RandomSource& random, int x, int y, int z, std::string const& blockId, std::string const& tableId);
    /// A mob spawner for `entityId` (vanilla spawner or a boss spawner block id).
    bool placeSpawner(Region& region, BlockBox const& chunkBox, int x, int y, int z, std::string const& entityId);
    /// A GloamingForest template at piece-local `origin`.
    bool placeTemplate(Region& region, BlockBox const& chunkBox, std::string const& templateId, BlockPos const& origin, TemplateSettings const& settings);
};

/// Rebuilds a piece from its saved data (persisted structure starts).
using Factory = std::function<std::unique_ptr<Piece>(Json const& saved)>;
void registerPieceType(std::string const& typeId, Factory factory);
/// A piece from saved data (nullptr: unknown type, one warning per id).
[[nodiscard]] std::unique_ptr<Piece> loadPiece(Json const& saved);

/// One block a processor or template sees (StructureTemplate.StructureBlockInfo).
struct BlockInfo {
    BlockPos     pos;
    Block const* block = nullptr;
    Json         data;  ///< block entity data (loot table, spawner entity, sign text...)
};
/// A structure processor ("twilightforest:nagastone_variants"): returns the replacement or
/// nullopt to drop the block.
using ProcessorFn = std::function<std::optional<BlockInfo>(Region& region, BlockInfo const& original, BlockInfo const& placed, TemplateSettings const& settings, Json const& config)>;
void registerProcessor(std::string const& typeId, ProcessorFn fn);
/// A template marker handler (TF's template_marker_handler_list; "loot", "drying_rack", ...).
using MarkerFn = std::function<void(Region& region, BlockPos const& pos, std::string const& marker, Json const& data, RandomSource& random)>;
void registerMarkerHandler(std::string const& id, MarkerFn fn);

namespace templates {
/// A loaded GloamingForest template (.gft.json).
struct Template {
    std::string id;
    int         sizeX = 0, sizeY = 0, sizeZ = 0;
    Json        data; ///< palette, RLE blocks, markers, jigsaw connectors, entities
};
/// The template `id` ("twilightforest:lich_tower/root/foyer"), nullptr when missing.
[[nodiscard]] Template const* get(std::string const& id);
/// Places a template with its processors; false when missing.
bool place(std::string const& id, Region& region, BlockPos const& origin, TemplateSettings const& settings);
} // namespace templates

namespace palettes {
/// Applies wood palette `paletteId` (twilight/wood_palettes) to a block (identity if no match).
[[nodiscard]] Block const& apply(std::string const& paletteId, Block const& block, RandomSource& random);
} // namespace palettes

namespace selectors {
/// A shared random block selector by id ("twilightforest:stronghold_stones"); nullptr if unknown.
[[nodiscard]] BlockSelector* get(std::string const& id);
} // namespace selectors

namespace jigsaw {
/// Assembles template pool `pool` from `origin` up to `depth` steps (Java's JigsawPlacement);
/// returns the placed pieces (deterministic for `random`).
[[nodiscard]] std::vector<std::unique_ptr<Piece>> assemble(std::string const& pool, BlockPos const& origin, int depth, RandomSource& random);
} // namespace jigsaw

namespace speleothems {
/// Places a stalactite / stalagmite of speleothem config `configId` (twilight/stalactites,
/// structure_speleothem_settings) hanging from or standing on `pos`.
bool place(Region& region, BlockPos const& pos, std::string const& configId, RandomSource& random, bool hanging);
} // namespace speleothems

} // namespace gf::structure
