// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// F7 internals of the shared bases (not a contract).

#include <memory>
#include <string>

#include "feature/ext/structure/TemplateDefinitions.h"
#include "feature/structure/Internal.h"
#include "feature/structure/bases/TemplatePieces.h"

namespace gf::structure::bases::detail {

/// PlaceSettings -> the template placement it describes.
[[nodiscard]] templates::Placement placement(PlaceSettings const& settings, BlockPos const& templatePosition, BlockPos const& pivotPos,
                                             std::optional<BlockBox> clip);
/// StructureTemplate#getSize(rotation) of template `id` (x / z swapped for quarter turns); {0,0,0}
/// when the template is missing.
[[nodiscard]] BlockPos templateSize(std::string const& id, Rotation rotation);

/// FrontAndTop helpers over JigsawRecord.
[[nodiscard]] JigsawRecord recordFrom(templates::Connector const& c);
[[nodiscard]] JigsawRecord rotated(JigsawRecord record, Rotation rotation);
[[nodiscard]] int          frontAndTopOrdinal(JigsawRecord const& r);
void                       setFrontAndTop(JigsawRecord& r, int ordinal);
[[nodiscard]] Json         recordToJson(JigsawRecord const& r);
[[nodiscard]] JigsawRecord recordFromJson(Json const& j);

/// TwilightJigsawPiece.defaultForTemplate (pool instance settings and randomized processors).
[[nodiscard]] std::unique_ptr<JigsawPiece> defaultForTemplate(int genDepth, std::string const& templateId, JigsawPlaceContext const& context,
                                                              Json const& instance, RandomSource& random);
/// StructureTemplateDefinitions#initializeTemplateFromPool; with a generation context the pool
/// entry's height_adjustment applies (TemplatePoolInstance#adjustContextForTerrain).
[[nodiscard]] std::unique_ptr<JigsawPiece> initializeTemplateFromPool(std::string const& pool, BlockPos const& parentJunctionPos,
                                                                      JigsawRecord const& parentOrientation, std::string const& selectName,
                                                                      RandomSource& random, int genDepth,
                                                                      ext::GenerationContext const* context = nullptr,
                                                                      bool parentProjectsTerrain = false);
/// The generation context of the jigsaw assembly running on this thread (nullptr: none).
[[nodiscard]] ext::GenerationContext const*& generationContext();
/// TemplatePoolInstance.HeightAdjustment#adjustForTerrain.
[[nodiscard]] JigsawPlaceContext adjustContextForTerrain(JigsawPlaceContext context, Json const& heightAdjustment, ext::GenerationContext const& gen,
                                                         bool parentProjectsTerrain);
/// Whether a jigsaw piece places with Projection.TERRAIN_MATCHING.
[[nodiscard]] bool projectsTerrain(JigsawPiece const& piece);
/// The deepest genDepth jigsaw::assemble lets processJigsaw reach (thread-local, default unlimited).
[[nodiscard]] int& maxJigsawDepth();
/// A world seed for TwilightJigsawPiece#addJigsaws in place of worldgen::seed() (thread-local; tests).
[[nodiscard]] std::optional<std::int64_t>& jigsawSeedOverride();

[[nodiscard]] TerrainAdjustment parseTerrainAdjustment(std::string const& name);
[[nodiscard]] char const*       terrainAdjustmentName(TerrainAdjustment t);

void registerPieceTypes();

} // namespace gf::structure::bases::detail
