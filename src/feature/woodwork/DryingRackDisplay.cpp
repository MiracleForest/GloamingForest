// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/renderer/block/DryingRackRenderer.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The item on a drying rack (DryingRackRenderer). A custom block has no block entity renderer every
// client runs, so each rack holding an item gets a display entity (gloamingforest:drying_rack_item:
// no body, no collision, invulnerable) that holds the item in its hand bone, placed where the
// renderer draws it: 0.4 towards the rack's facing side, 0.5 high for a block, 0.325 for
// #twilightforest:lower_on_drying_rack, else 0.45, turned with the rack. Vanilla clients and the
// client mod see the same entity. Its unique id is kept in the rack's block data
// ("drying_rack_display"); the entity goes when the rack empties or the rack goes.

#include <cmath>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/ecs/gamerefs_entity/GameRefsEntity.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"

#include "feature/common/BlockData.h"
#include "feature/tags/Tags.h"
#include "feature/woodwork/DryingRack.h"
#include "feature/woodwork/Woodwork.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::woodwork::rack {

namespace bd = blockdata;

namespace {

constexpr char kDisplayKey[]  = "drying_rack_display";
constexpr char kDisplayType[] = "gloamingforest:drying_rack_item";

Actor* entityById(std::int64_t id) {
    if (!id) return nullptr;
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID uid;
    uid.rawID = id;
    return level->fetchEntity(uid, false);
}

std::int64_t displayIdAt(BlockSource& region, BlockPos const& pos) {
    auto const* v = bd::get(region, pos, kDisplayKey);
    return v && v->is_number_integer() ? v->get<std::int64_t>() : 0;
}

/// DryingRackRenderer's translation and yaw for the item.
std::pair<Vec3, float> placement(BlockPos const& pos, std::string const& facing, ItemStack const& item) {
    int  face = faceOf(facing);
    auto step = faceStep(face < 0 ? 2 : face);
    auto id   = game::items::idOf(item);
    double y  = 0.45;
    if (item.mBlock) y = 0.5; // a 3D block model
    else if (tags::has(tags::Registry::Item, "twilightforest:lower_on_drying_rack", id)) y = 0.325;
    if (id.ends_with("_banner")) y -= 0.4;
    else if (id == "minecraft:shield") y -= 0.1;
    // Direction#toYRot: south 0, west 90, north 180, east 270
    float yaw = facing == "south" ? 0.f : facing == "west" ? 90.f : facing == "east" ? 270.f : 180.f;
    return {Vec3{static_cast<float>(pos.x + 0.5 + 0.4 * step[0]), static_cast<float>(pos.y + y), static_cast<float>(pos.z + 0.5 + 0.4 * step[2])}, yaw};
}

void removeDisplay(BlockSource& region, BlockPos const& pos) {
    if (auto* a = entityById(displayIdAt(region, pos))) a->remove();
    if (bd::get(region, pos, kDisplayKey)) bd::set(region, pos, kDisplayKey, nullptr);
}

/// The rack at `gp` shows its item (or nothing).
void refresh(GlobalPos const& gp) {
    auto* region = game::regionOf(gp.dim);
    if (!region) return;
    BlockPos pos = game::blockPos(gp);
    if (!region->hasChunksAt(pos, 0, false)) return;
    auto r = at(gp);
    if (!r) return; // not a rack (any more): the erase listener removes the display
    if (game::items::isEmpty(r->item)) {
        removeDisplay(*region, pos);
        return;
    }
    auto [where, yaw] = placement(pos, stateOf(region->getBlock(pos), "twilightforest:facing"), r->item);
    Actor* a = entityById(displayIdAt(*region, pos));
    if (a && (!a->isAlive() || a->getTypeName() != kDisplayType)) a = nullptr;
    if (!a) {
        auto& level = region->getLevel();
        auto  ctx   = level.getActorFactory().createSpawnedActor(ActorDefinitionIdentifier(std::string(kDisplayType)), nullptr, where, Vec2{0.f, yaw});
        if (!ctx) {
            static bool warned = false;
            if (!warned) log().warn("woodwork: cannot spawn {} (behavior pack missing?)", kDisplayType);
            warned = true;
            return;
        }
        a = level.addEntity(*region, std::move(ctx));
        if (!a) return;
        a->addTag("gf_drying_rack");
        bd::set(*region, pos, kDisplayKey, a->getOrCreateUniqueID().rawID);
    }
    auto const& p = a->getPosition();
    if (std::abs(p.x - where.x) > 0.01f || std::abs(p.y - where.y) > 0.01f || std::abs(p.z - where.z) > 0.01f || std::abs(a->getRotation().y - yaw) > 0.5f) {
        a->teleport(where, a->getDimensionId(), Vec2{0.f, yaw});
    }
    if (!game::items::sameItemSameData(a->getCarriedItem(), r->item)) a->setCarriedItem(r->item);
}

} // namespace

void installDisplay() {
    static bool once = false;
    if (once) return;
    once = true;
    onChange([](GlobalPos const& pos) { refresh(pos); });
    // the rack went: its display goes with it (block data is erased when the block type changes)
    bd::onErase([](GlobalPos const& pos, Json const& doc, std::string const&) {
        if (!doc.contains(kDisplayKey) || !doc[kDisplayKey].is_number_integer()) return;
        if (auto* a = entityById(doc[kDisplayKey].get<std::int64_t>())) a->remove();
    });
}

void refreshDisplay(GlobalPos const& pos) { refresh(pos); }

} // namespace gf::feature::woodwork::rack
