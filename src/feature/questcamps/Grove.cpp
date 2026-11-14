// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/QuestGrove.java
//   src/main/java/gloamingforest/world/components/structures/type/QuestGroveStructure.java
//   src/main/java/gloamingforest/util/features/FeaturePlacers.java (placeEntity)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The quest grove (docs/spec/questcamps.md 2): one template piece (GloamingQuest1) 12 blocks west and
// north of the generation point, placed 2 below its stored position (placePieceAdjusted(-2)), with
// the Questing Ram and the offering droppers at its data markers.

#include <algorithm>

#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/questcamps/W0Internals.h"
#include "feature/landmark/Landmark.h"
#include "feature/questcamps/Questcamps.h"
#include "feature/structure/bases/TemplatePieces.h"
#include "mod/Log.h"

namespace gf::feature::questcamps {

namespace st = gf::structure;
namespace sb = gf::structure::bases;

namespace {

/// QuestGroveStructure.LENGTH.
constexpr int kLength = 27;
constexpr char kTemplate[] = "gloamingforest:quest_grove";
/// MOSSY_BRICK_DECAY: TargetedRotProcessor({mossy_stone_bricks}, 0.5f).
constexpr char kMossyDecay[] =
    R"({"processor_type":"gloamingforest:targeted_rot","blocks_to_rot":[{"Name":"minecraft:mossy_stone_bricks"}],"integrity":0.5})";
constexpr char kStoneBricks[] = "gloamingforest:stone_brick_variants";

class GrovePiece final : public sb::TemplatePiece {
public:
    explicit GrovePiece(BlockPos const& templatePosition)
    : TemplatePiece(kGrovePiece, 0, kTemplate, settings(), templatePosition) {}
    explicit GrovePiece(Json const& saved) : TemplatePiece(kGrovePiece, 0, kTemplate, settings(), BlockPos{0, 0, 0}) { load(saved); }

    /// A piece read back from a save: readSettings(tag) + StoneBricksVariants only (the rot
    /// processor is not restored: upstream's quirk, kept).
    void load(Json const& data) override {
        TemplatePiece::load(data);
        placeSettings.processors = {kStoneBricks};
    }

    /// placePieceAdjusted(-2): the template goes 2 below the stored position; box and position are
    /// put back afterwards.
    void postProcess(st::Region& region, st::RandomSource& random, st::BlockBox const& chunkBox, BlockPos const& pivot) override {
        BlockPos original = templatePosition;
        auto     box      = bbox;
        templatePosition.y -= 2;
        TemplatePiece::postProcess(region, random, chunkBox, BlockPos{pivot.x, pivot.y - 2, pivot.z});
        templatePosition = original;
        bbox             = box;
    }

protected:
    void handleDataMarker(std::string const& label, BlockPos const& pos, st::Region& region, st::RandomSource& random, st::BlockBox const& chunkBox,
                          st::Rotation rotation) override {
        if (!chunkBox.contains(pos.x, pos.y, pos.z)) return;
        if (label == "quest_ram") {
            // FeaturePlacers.placeEntity: the ram at the block's center, yaw 0, persistent, its
            // restriction point the marker; the marker becomes air
            st::blockentities::entity(region, pos.x + 0.5, pos.y, pos.z + 0.5, kRam, Json{{"Rotation", Json::array({0.0, 0.0})}});
            queueHome(region.dimension(), pos);
            region.setBlock(pos, st::blocks::air(), 2);
        } else if (label == "dispenser") {
            // a dropper facing rotation.rotate(NORTH), filled from quest_grove_dropper (seed random.nextLong())
            auto facing = st::rotateDir(st::Orientation::North, rotation);
            region.setBlock(pos, st::blocks::java("minecraft:dropper", Json{{"facing", st::blocks::directionName(facing)}, {"triggered", "false"}}), 16 | 4 | 2);
            st::blockentities::lootTable(region, pos, "gloamingforest:quest_grove_dropper", random.nextLong());
        }
    }

private:
    static sb::PlaceSettings settings() {
        auto s       = sb::makeSettings(st::Rotation::None);
        s.processors = {kMossyDecay, kStoneBricks};
        return s;
    }
};

} // namespace

std::unique_ptr<st::Piece> makeGrove(int x, int y, int z) {
    // QuestGroveStructure#getFirstPiece: (x - LENGTH / 2 + 1, y + 2, z - LENGTH / 2 + 1)
    return std::make_unique<GrovePiece>(BlockPos{x - kLength / 2 + 1, y + 2, z - kLength / 2 + 1});
}

void installGrove() {
    st::registerPieceType(kGrovePiece, [](Json const& saved) -> std::unique_ptr<st::Piece> { return std::make_unique<GrovePiece>(saved); });
    gf::landmark::registerGenerator(kGrove, gf::landmark::Generator{
                                                .layout =
                                                    [](gf::landmark::LayoutContext& ctx) {
                                                        auto const& c = ctx.start.center;
                                                        ctx.pieces.push_back(makeGrove(c.x, c.y, c.z));
                                                        return true;
                                                    },
                                                .terraform =
                                                    [](gf::landmark::Start const& start, int x, int y, int z) -> double {
                                                        // the structure's BEARD_THIN around the stored piece box
                                                        if (start.pieceBoxes.empty()) return 0.0;
                                                        return gf::landmark::ext::boxDensity(start.pieceBoxes, 0, 0, "beard_thin", x, y, z);
                                                    },
                                            });
}

} // namespace gf::feature::questcamps
