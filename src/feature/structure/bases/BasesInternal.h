// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// F7 internals of the shared bases (not a contract).

#include <memory>
#include <string>

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
/// StructureTemplateDefinitions#initializeTemplateFromPool.
[[nodiscard]] std::unique_ptr<JigsawPiece> initializeTemplateFromPool(std::string const& pool, BlockPos const& parentJunctionPos,
                                                                      JigsawRecord const& parentOrientation, std::string const& selectName,
                                                                      RandomSource& random, int genDepth);
/// The deepest genDepth jigsaw::assemble lets processJigsaw reach (thread-local, default unlimited).
[[nodiscard]] int& maxJigsawDepth();

[[nodiscard]] TerrainAdjustment parseTerrainAdjustment(std::string const& name);
[[nodiscard]] char const*       terrainAdjustmentName(TerrainAdjustment t);

void registerPieceTypes();

} // namespace gf::structure::bases::detail
