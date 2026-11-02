// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/util/StructureTemplateDefinitions.java
//   src/main/java/twilightforest/world/components/structures/util/TemplatePoolInstance.java (HeightAdjustment)
//   src/main/java/twilightforest/world/components/structures/TwilightJigsawPiece.java (addJigsaws)
//   src/main/java/twilightforest/util/WorldUtil.java (adjustForTerrain)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// Contract extension of "structure" (rule R18: F7, new files only; folded into the contract by A8
// in W2b). TF's StructureTemplateDefinitions as W1 landmarks call it: the template pools built from
// twilight/template_definition (CourtyardMain U20, FinalCastle* U30 initializeTemplateFromPool;
// LichTowerUtil U21/U22 getRandomTemplate / getShuffledSequence; CampStructure U19), with the
// generation context jigsaw assembly needs for TemplatePoolInstance.height_adjustment.
//
//     auto ctx   = gf::structure::ext::GenerationContext{&random, worldgen::seed(), heights};
//     auto start = gf::structure::ext::initializeTemplateFromPool("twilightforest:camp/structure_start", pos,
//                      gf::structure::ext::FrontAndTop::UpSouth, "twilightforest:camp/structure_start", ctx, 0, false);
//     if (start) { auto* raw = start.get(); pieces.push_back(std::move(start)); ext::addJigsaws(*raw, *raw, pieces, ctx); }

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "feature/structure/bases/TemplatePieces.h"
#include "feature/worldgen/Worldgen.h"

namespace gf::structure::ext {

/// Java's FrontAndTop (ordinal order).
enum class FrontAndTop { DownEast, DownNorth, DownSouth, DownWest, UpEast, UpNorth, UpSouth, UpWest, WestUp, EastUp, NorthUp, SouthUp };
/// The FrontAndTop of a jigsaw record (JigsawRecord#orientation).
[[nodiscard]] FrontAndTop frontAndTop(bases::JigsawRecord const& record);
/// `record` with its orientation set to `orientation`.
[[nodiscard]] bases::JigsawRecord withOrientation(bases::JigsawRecord record, FrontAndTop orientation);
/// Rotation#rotate(FrontAndTop): FrontAndTop.fromFrontAndTop(rotate(front), rotate(top)).
[[nodiscard]] FrontAndTop rotate(FrontAndTop orientation, Rotation rotation);

/// Java's Structure.GenerationContext as jigsaw assembly uses it.
struct GenerationContext {
    RandomSource* random = nullptr; ///< context.random() (the WorldgenRandom addJigsaws reseeds)
    std::int64_t  seed   = 0;       ///< context.seed()
    /// ChunkGenerator#getFirstOccupiedHeight(x, z, type, heightAccessor, randomState): the y of the
    /// topmost block counted by heightmap `type` (the generator's estimate before the chunk exists).
    std::function<int(worldgen::Heightmap type, int x, int z)> firstOccupiedHeight;
};
/// A generation context over a region whose terrain exists (commands, tests, landmark::generateAt):
/// the first occupied height is the region's heightmap minus one.
[[nodiscard]] GenerationContext contextOf(Region& region, RandomSource& random, std::int64_t seed);

/// StructureTemplateDefinitions#getRandomTemplate: a template of pool `pool` (weighted), nullopt
/// when the pool is unknown or empty.
[[nodiscard]] std::optional<std::string> getRandomTemplate(RandomSource& random, std::string const& pool);
/// StructureTemplateDefinitions#getShuffledSequence: every template of `pool` in a weighted random
/// order (reservoir sampling, -log(nextDouble()) / weight ascending); empty for an unknown pool.
[[nodiscard]] std::vector<std::string> getShuffledSequence(RandomSource& random, std::string const& pool);

/// StructureTemplateDefinitions#initializeTemplateFromPool (the deprecated form without a
/// generation context: no height adjustment). nullptr when no template of the pool fits.
[[nodiscard]] std::unique_ptr<bases::JigsawPiece> initializeTemplateFromPool(std::string const& pool, BlockPos const& parentJunctionPos,
                                                                             FrontAndTop parentOrientation, std::string const& selectName,
                                                                             RandomSource& random, int genDepth);
/// StructureTemplateDefinitions#initializeTemplateFromPool with a generation context: the pool
/// entry's height_adjustment moves the template to the terrain (TemplatePoolInstance
/// #adjustContextForTerrain; at the parent's jigsaw when the parent projects onto the terrain).
[[nodiscard]] std::unique_ptr<bases::JigsawPiece> initializeTemplateFromPool(std::string const& pool, BlockPos const& parentJunctionPos,
                                                                             FrontAndTop parentOrientation, std::string const& selectName,
                                                                             GenerationContext const& context, int genDepth,
                                                                             bool parentProjectsTerrain);
/// TwilightJigsawPiece#addJigsaws(parent, accessor, context): grows `piece`'s spare jigsaws with
/// the context (its random reseeded from context.seed, height adjustment on every child).
void addJigsaws(bases::JigsawPiece& piece, bases::JigsawPiece& parent, bases::PieceList& pieces, GenerationContext const& context);
/// jigsaw::assemble with a generation context (CampStructure's start: FrontAndTop(UP,
/// Rotation.getRandom(random).rotate(SOUTH)), then addJigsaws).
[[nodiscard]] std::vector<std::unique_ptr<Piece>> assemble(std::string const& pool, BlockPos const& origin, int depth, GenerationContext const& context);
/// Whether a jigsaw piece projects onto the terrain (StructureTemplatePool.Projection.TERRAIN_MATCHING).
[[nodiscard]] bool projectsTerrain(bases::JigsawPiece const& piece);

/// WorldUtil.adjustForTerrain(context, xMin, zMin, xMax, zMax, gridLength, type): the
/// rank-weighted mean of a gridLength x gridLength grid of first occupied heights.
[[nodiscard]] int adjustForTerrain(GenerationContext const& context, int xMin, int zMin, int xMax, int zMax, int gridLength, worldgen::Heightmap type);
/// WorldUtil.adjustForTerrain(context, x, z, radiusFromCenterChunk, gridLength) over the chunk of
/// (x, z) widened by the radius (WORLD_SURFACE_WG).
[[nodiscard]] int adjustForTerrain(GenerationContext const& context, int xInCenterChunk, int zInCenterChunk, int radiusFromCenterChunk, int gridLength);
/// Mth.lerpDiscrete(delta, start, end).
[[nodiscard]] int lerpDiscrete(float delta, int start, int end);

} // namespace gf::structure::ext
