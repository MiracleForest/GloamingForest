// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/util/StructureTemplateDefinitions.java
//   src/main/java/twilightforest/world/components/structures/TwilightJigsawPiece.java (addJigsaws)
//   src/main/java/twilightforest/world/components/structures/type/CampStructure.java (the jigsaw start)
//   src/main/java/twilightforest/util/WorldUtil.java (adjustForTerrain)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "feature/ext/structure/TemplateDefinitions.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <map>

#include "feature/structure/bases/BasesInternal.h"

namespace gf::structure::ext {

namespace {

/// Sets the thread's generation context for a scope (assemblies nest: restore the previous one).
class ContextScope {
public:
    explicit ContextScope(GenerationContext const& context) : mPrevious(bases::detail::generationContext()) {
        bases::detail::generationContext() = &context;
    }
    ~ContextScope() { bases::detail::generationContext() = mPrevious; }
    ContextScope(ContextScope const&)            = delete;
    ContextScope& operator=(ContextScope const&) = delete;

private:
    GenerationContext const* mPrevious;
};

} // namespace

FrontAndTop frontAndTop(bases::JigsawRecord const& record) { return static_cast<FrontAndTop>(bases::detail::frontAndTopOrdinal(record)); }

bases::JigsawRecord withOrientation(bases::JigsawRecord record, FrontAndTop orientation) {
    bases::detail::setFrontAndTop(record, static_cast<int>(orientation));
    return record;
}

FrontAndTop rotate(FrontAndTop orientation, Rotation rotation) {
    // Rotation#rotate(FrontAndTop): the horizontal one of front / top turns
    return frontAndTop(bases::detail::rotated(withOrientation(bases::JigsawRecord{}, orientation), rotation));
}

GenerationContext contextOf(Region& region, RandomSource& random, std::int64_t seed) {
    GenerationContext c;
    c.random              = &random;
    c.seed                = seed;
    c.firstOccupiedHeight = [&region](worldgen::Heightmap type, int x, int z) { return region.height(type, x, z) - 1; };
    return c;
}

std::optional<std::string> getRandomTemplate(RandomSource& random, std::string const& pool) {
    auto const* entries = pools::get(pool);
    if (!entries) return std::nullopt;
    auto const* entry = pools::random(*entries, random);
    if (!entry) return std::nullopt;
    return entry->templateId;
}

std::vector<std::string> getShuffledSequence(RandomSource& random, std::string const& pool) {
    auto const* entries = pools::get(pool);
    if (!entries) return {};
    // https://en.wikipedia.org/wiki/Reservoir_sampling: one key per entry in pool order, the
    // templates sorted by key (a HashMap keyed by template: a template listed twice keeps the
    // later key)
    std::map<std::string, double> keys;
    for (auto const& e : *entries) {
        double rand  = random.nextDouble();
        keys[e.templateId] = -std::log(rand) / static_cast<double>(e.weight);
    }
    std::vector<std::pair<std::string, double>> sorted(keys.begin(), keys.end());
    std::stable_sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) { return a.second < b.second; });
    std::vector<std::string> out;
    out.reserve(sorted.size());
    for (auto& [id, _] : sorted) out.push_back(id);
    return out;
}

std::unique_ptr<bases::JigsawPiece> initializeTemplateFromPool(std::string const& pool, BlockPos const& parentJunctionPos, FrontAndTop parentOrientation,
                                                               std::string const& selectName, RandomSource& random, int genDepth) {
    return bases::detail::initializeTemplateFromPool(pool, parentJunctionPos, withOrientation(bases::JigsawRecord{}, parentOrientation), selectName, random,
                                                     genDepth);
}

std::unique_ptr<bases::JigsawPiece> initializeTemplateFromPool(std::string const& pool, BlockPos const& parentJunctionPos, FrontAndTop parentOrientation,
                                                               std::string const& selectName, GenerationContext const& context, int genDepth,
                                                               bool parentProjectsTerrain) {
    if (!context.random) return nullptr;
    return bases::detail::initializeTemplateFromPool(pool, parentJunctionPos, withOrientation(bases::JigsawRecord{}, parentOrientation), selectName,
                                                     *context.random, genDepth, &context, parentProjectsTerrain);
}

void addJigsaws(bases::JigsawPiece& piece, bases::JigsawPiece& parent, bases::PieceList& pieces, GenerationContext const& context) {
    if (!context.random) return;
    ContextScope scope(context);
    piece.addJigsaws(parent, pieces, *context.random);
}

std::vector<std::unique_ptr<Piece>> assemble(std::string const& pool, BlockPos const& origin, int depth, GenerationContext const& context) {
    std::vector<std::unique_ptr<Piece>> pieces;
    if (!context.random) return pieces;
    RandomSource& random = *context.random;
    // CampStructure#findGenerationPoint: FrontAndTop.fromFrontAndTop(UP, Rotation.getRandom(random).rotate(SOUTH))
    bases::JigsawRecord oriented;
    oriented.frontVertical = true;
    oriented.frontUp       = true;
    oriented.top           = rotateDir(Orientation::South, rotationFromIndex(random.nextInt(4)));
    auto start             = bases::detail::initializeTemplateFromPool(pool, origin, oriented, pool, random, 0, &context, false);
    if (!start) return pieces;
    int& maxDepth = bases::detail::maxJigsawDepth();
    int  previous = maxDepth;
    maxDepth      = depth < 0 ? INT_MAX : depth;
    auto* raw     = start.get();
    pieces.push_back(std::move(start));
    addJigsaws(*raw, *raw, pieces, context);
    maxDepth = previous;
    return pieces;
}

bool projectsTerrain(bases::JigsawPiece const& piece) { return bases::detail::projectsTerrain(piece); }

int lerpDiscrete(float delta, int start, int end) {
    int i = end - start;
    return start + pure::mthFloorF(delta * static_cast<float>(i - 1)) + (delta > 0.0f ? 1 : 0);
}

int adjustForTerrain(GenerationContext const& context, int xMin, int zMin, int xMax, int zMax, int gridLength, worldgen::Heightmap type) {
    if (gridLength < 2 || !context.firstOccupiedHeight) return context.firstOccupiedHeight ? context.firstOccupiedHeight(type, xMin, zMin) : 0;
    int              subDivisions = gridLength - 1;
    std::vector<int> heights;
    heights.reserve(static_cast<std::size_t>(gridLength * gridLength));
    for (int zStep = 0; zStep <= subDivisions; zStep++) {
        int zPos = lerpDiscrete(static_cast<float>(zStep) / static_cast<float>(subDivisions), zMin, zMax);
        for (int xStep = 0; xStep <= subDivisions; xStep++) {
            int xPos = lerpDiscrete(static_cast<float>(xStep) / static_cast<float>(subDivisions), xMin, xMax);
            heights.push_back(context.firstOccupiedHeight(type, xPos, zPos));
        }
    }
    std::sort(heights.begin(), heights.end(), std::greater<int>()); // highest to lowest
    double weightedSum = 0, totalWeight = 0;
    for (std::size_t i = 0; i < heights.size(); i++) {
        double weight = static_cast<double>(i + 1);
        weightedSum += weight * heights[i];
        totalWeight += weight;
    }
    // Math.round(double): floor(x + 0.5)
    return static_cast<int>(std::floor(weightedSum / totalWeight + 0.5));
}

int adjustForTerrain(GenerationContext const& context, int xInCenterChunk, int zInCenterChunk, int radiusFromCenterChunk, int gridLength) {
    int chunkOriginX = xInCenterChunk & ~0b1111;
    int chunkOriginZ = zInCenterChunk & ~0b1111;
    return adjustForTerrain(context, chunkOriginX - radiusFromCenterChunk, chunkOriginZ - radiusFromCenterChunk, chunkOriginX + 15 + radiusFromCenterChunk,
                            chunkOriginZ + 15 + radiusFromCenterChunk, gridLength, worldgen::Heightmap::WorldSurfaceWg);
}

} // namespace gf::structure::ext
