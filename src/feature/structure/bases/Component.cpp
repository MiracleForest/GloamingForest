// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/TFStructureComponent.java
//   src/main/java/twilightforest/world/components/structures/TFStructureDecorator.java
//   src/main/java/twilightforest/world/components/structures/UtilityPiece.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "feature/structure/bases/Component.h"

#include <map>
#include <mutex>

#include "feature/structure/Internal.h"

namespace gf::structure::bases {

Piece* findCollisionPiece(PieceList const& pieces, BlockBox const& box) {
    for (auto const& p : pieces)
        if (p && p->bbox.intersects(box)) return p.get();
    return nullptr;
}

namespace {
std::mutex                                                                 gDecoMutex;
std::map<std::string, std::function<std::unique_ptr<Decorator>()>>        gDecorators;
} // namespace

std::unique_ptr<Decorator> defaultDecorator() {
    auto d           = std::make_unique<Decorator>();
    d->blockState    = &blocks::java("minecraft:stone");
    d->accentState   = &blocks::java("minecraft:cobblestone");
    d->stairState    = &blocks::java("minecraft:stone_stairs");
    d->fenceState    = &blocks::java("minecraft:oak_fence");
    d->pillarState   = &blocks::java("minecraft:stone_bricks");
    d->platformState = &blocks::java("minecraft:stone_slab");
    d->floorState    = &blocks::java("minecraft:stone_bricks");
    d->roofState     = &blocks::java("minecraft:stone_bricks");
    d->randomBlocks  = selectors::get("twilightforest:stronghold_stones");
    return d;
}

std::unique_ptr<Decorator> decoratorFor(std::string const& decoString) {
    std::function<std::unique_ptr<Decorator>()> factory;
    {
        std::lock_guard lock(gDecoMutex);
        if (auto it = gDecorators.find(decoString); it != gDecorators.end()) factory = it->second;
    }
    if (factory) return factory();
    return defaultDecorator();
}

void registerDecorator(std::string const& decoString, std::function<std::unique_ptr<Decorator>()> factory) {
    std::lock_guard lock(gDecoMutex);
    gDecorators[decoString] = std::move(factory);
}

// ---- TFStructureComponent ---------------------------------------------------------------------------

Component::Component(std::string typeId, int depth, BlockBox const& box) : Piece(std::move(typeId)) {
    bbox        = box;
    genDepth    = depth;
    orientation = kNoOrientation; // StructurePiece: orientation null until set
    rotation    = Rotation::None;
}

Component::Component(std::string typeId, int depth, int x, int y, int z) : Component(std::move(typeId), depth, BlockBox{x, y, z, x, y, z}) {}

Json Component::save() const {
    Json j    = Piece::save();
    j["si"]   = spawnListIndex;
    j["deco"] = deco ? deco->decoString() : std::string{};
    j["rot"]  = rotationIndex(rotation);
    return j;
}

void Component::load(Json const& data) {
    Piece::load(data);
    spawnListIndex = data.value("si", 0);
    deco           = decoratorFor(data.value("deco", std::string{}));
    rotation       = rotationFromIndex(data.value("rot", 0) % 4);
}

void Component::placeBlockScheduled(Region& region, Block const& block, int x, int y, int z, BlockBox const& sbb) {
    // TFStructureComponent#placeBlock: mirror, rotate, set with UPDATE_CLIENTS. The post-processing
    // TF schedules (fence / stair shapes, liquids) is what Bedrock does on its own for placed blocks.
    BlockPos pos = getWorldPos(x, y, z);
    if (!sbb.contains(pos.x, pos.y, pos.z)) return;
    region.setBlock(pos, oriented(block), 2);
}

void Component::setInvisibleTextEntity(Region&, int, int, int, BlockBox const&, std::string const&, bool, float) {
    // debug text displays only (TFStructureComponent.shouldDebug() is false)
}

// ---- UtilityPiece -------------------------------------------------------------------------------------

UtilityPiece::UtilityPiece(int depth, BlockBox const& box, bool allow) : Piece("twilightforest:TFUtilityPiece"), allowFeatures(allow) {
    bbox        = box;
    genDepth    = depth;
    orientation = kNoOrientation;
}

void UtilityPiece::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) {}

Json UtilityPiece::save() const {
    Json j              = Piece::save();
    j["allow_features"] = allowFeatures;
    return j;
}

void UtilityPiece::load(Json const& data) {
    Piece::load(data);
    allowFeatures = data.value("allow_features", false);
}

} // namespace gf::structure::bases
