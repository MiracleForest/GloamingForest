// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/client/renderer/block/DryingRackRenderer.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The item on a drying rack (DryingRackRenderer). A custom block has no block entity renderer every
// client runs, so each rack holding an item gets a display entity (gloamingforest:drying_rack_item:
// no body, no collision, invulnerable) that holds the item in its hand bone, placed where the
// renderer draws it: 0.4 towards the rack's facing side, 0.5 high for a block, 0.325 for
// #gloamingforest:lower_on_drying_rack, else 0.45, turned with the rack. Vanilla clients and the
// client mod see the same entity. Its unique id is kept in the rack's block data
// ("drying_rack_display"); the entity goes when the rack empties or the rack goes.

#include <cmath>
#include <map>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/ecs/gamerefs_entity/GameRefsEntity.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/phys/AABB.h"

#include "feature/common/BlockData.h"
#include "feature/loot/Loot.h"
#include "feature/net/Net.h"
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
    else if (tags::has(tags::Registry::Item, "gloamingforest:lower_on_drying_rack", id)) y = 0.325;
    if (id.ends_with("_banner")) y -= 0.4;
    else if (id == "minecraft:shield") y -= 0.1;
    // Direction#toYRot: south 0, west 90, north 180, east 270
    float yaw = facing == "south" ? 0.f : facing == "west" ? 90.f : facing == "east" ? 270.f : 180.f;
    return {Vec3{static_cast<float>(pos.x + 0.5 + 0.4 * step[0]), static_cast<float>(pos.y + y), static_cast<float>(pos.z + 0.5 + 0.4 * step[2])}, yaw};
}

/// The display entities standing in the rack's block (a display is always inside its rack's space).
std::vector<Actor*> displaysIn(BlockSource& region, BlockPos const& pos) {
    std::vector<Actor*> out;
    AABB box{Vec3{pos.x - 0.05f, pos.y - 0.45f, pos.z - 0.05f}, Vec3{pos.x + 1.05f, pos.y + 1.05f, pos.z + 1.05f}};
    for (Actor* a : region.getEntities(box, 0.0f)) {
        if (a && a->isAlive() && a->getTypeName() == kDisplayType) out.push_back(a);
    }
    return out;
}

/// The racks whose stored display was not found, since when (server tick): actors load after their
/// chunk, so a missing display gets a replacement only once it stayed missing for kLoadGrace ticks.
std::map<GlobalPos, std::int64_t>& missingSince() {
    static std::map<GlobalPos, std::int64_t> m;
    return m;
}
constexpr std::int64_t kLoadGrace = 60;

std::int64_t nowTick() {
    auto level = ll::service::getLevel();
    return level ? static_cast<std::int64_t>(level->getCurrentServerTick().tickID) : 0;
}

void removeDisplay(BlockSource& region, BlockPos const& pos) {
    if (auto* a = entityById(displayIdAt(region, pos))) a->remove();
    for (auto* a : displaysIn(region, pos)) a->remove();
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
        missingSince().erase(gp);
        return;
    }
    auto [where, yaw] = placement(pos, stateOf(region->getBlock(pos), "gloamingforest:facing"), r->item);
    std::int64_t stored = displayIdAt(*region, pos);
    Actor*       a      = entityById(stored);
    if (a && (!a->isAlive() || a->getTypeName() != kDisplayType)) a = nullptr;
    // displays already standing in the rack: the stored one, else the first is adopted (its id was
    // lost); every other one is a twin and goes
    auto standing = displaysIn(*region, pos);
    if (!a && !standing.empty()) {
        a = standing.front();
        bd::set(*region, pos, kDisplayKey, a->getOrCreateUniqueID().rawID);
    }
    for (auto* other : standing) {
        if (other != a) other->remove();
    }
    // a held item set later does not reach the clients that already see the entity: a new item
    // gets a new display entity (its spawn carries the item)
    if (a && !game::items::sameItemSameData(a->getCarriedItem(), r->item)) {
        a->remove();
        a = nullptr;
        stored = 0;
    }
    if (a) missingSince().erase(gp);
    if (!a && stored != 0) {
        // the stored display may still be loading with its chunk
        auto [it, fresh] = missingSince().try_emplace(gp, nowTick());
        if (fresh || nowTick() - it->second < kLoadGrace) return;
        missingSince().erase(it);
    }
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
        a->setCarriedItem(r->item);
        a->addTag("gf_drying_rack");
        bd::set(*region, pos, kDisplayKey, a->getOrCreateUniqueID().rawID);
    }
    auto const& p = a->getPosition();
    if (std::abs(p.x - where.x) > 0.01f || std::abs(p.y - where.y) > 0.01f || std::abs(p.z - where.z) > 0.01f || std::abs(a->getRotation().y - yaw) > 0.5f) {
        a->teleport(where, a->getDimensionId(), Vec2{0.f, yaw});
    }
    if (!game::items::sameItemSameData(a->getCarriedItem(), r->item)) a->setCarriedItem(r->item);
}

// ---- the client mod's renderer ("woodwork.rack") --------------------------------------------------------

constexpr char kChannel[] = "woodwork.rack";

Json posJson(GlobalPos const& p) { return Json::array({p.dim, p.x, p.y, p.z}); }

/// The rack at `gp` as the client mod draws it (null: nothing to draw).
Json rackEntry(GlobalPos const& gp) {
    auto* region = game::regionOf(gp.dim);
    if (!region) return nullptr;
    BlockPos pos = game::blockPos(gp);
    if (!region->hasChunksAt(pos, 0, false)) return nullptr;
    auto r = at(gp);
    if (!r || game::items::isEmpty(r->item)) return nullptr;
    auto facing = stateOf(region->getBlock(pos), "gloamingforest:facing");
    auto id     = game::items::idOf(r->item);
    return Json{
        {"pos", posJson(gp)},
        {"facing", facing.empty() ? "north" : facing},
        {"item", game::items::toJson(r->item)},
        {"lower", tags::has(tags::Registry::Item, "gloamingforest:lower_on_drying_rack", id)},
        {"display", displayIdAt(*region, pos)},
    };
}

template <class Fn>
void forCompanions(Fn&& fn) {
    auto level = ll::service::getLevel();
    if (!level) return;
    level->forEachPlayer([&](Player& p) {
        if (net::isCompanion(p)) fn(p);
        return true;
    });
}

/// Tells every companion what the rack at `gp` shows now.
void sync(GlobalPos const& gp) {
    auto entry = rackEntry(gp);
    Json msg   = entry.is_null() ? Json{{"gone", Json::array({posJson(gp)})}} : Json{{"racks", Json::array({entry})}};
    forCompanions([&](Player& p) { net::send(p, kChannel, msg); });
}

/// Every rack with an item, for a companion that joined.
void syncAll(Player& player) {
    Json racks = Json::array();
    for (auto const& gp : activeRacks()) {
        auto e = rackEntry(gp);
        if (!e.is_null()) racks.push_back(std::move(e));
    }
    net::send(player, kChannel, Json{{"reset", true}, {"racks", racks}});
}

} // namespace

void installDisplay() {
    static bool once = false;
    if (once) return;
    once = true;
    onChange([](GlobalPos const& pos) {
        refresh(pos);
        sync(pos);
    });
    net::onCompanionJoined([](Player& p) { syncAll(p); });
    // the rack went: its display goes with it (block data is erased when the block type changes)
    bd::onErase([](GlobalPos const& pos, Json const& doc, std::string const&) {
        missingSince().erase(pos);
        if (doc.contains(kDisplayKey) || doc.contains(kKey)) {
            Json msg{{"gone", Json::array({posJson(pos)})}};
            forCompanions([&](Player& p) { net::send(p, kChannel, msg); });
        }
        if (!doc.contains(kDisplayKey) || !doc[kDisplayKey].is_number_integer()) return;
        if (auto* a = entityById(doc[kDisplayKey].get<std::int64_t>())) a->remove();
        if (auto* region = game::regionOf(pos.dim)) {
            for (auto* a : displaysIn(*region, game::blockPos(pos))) a->remove();
        }
    });
    // the display holds a copy of the rack's item: it never drops it (a /kill bypasses its damage
    // sensor; the BP also sets the hand's drop chance to 0)
    loot::onEntityDrops([](Actor& dead, std::vector<ItemStack>& drops, loot::LootContext const&) {
        if (dead.getTypeName() == kDisplayType) drops.clear();
    });
}

void refreshDisplay(GlobalPos const& pos) {
    auto before = displayIdAt(pos);
    refresh(pos);
    // a display that came or went (loaded late, replaced) is named to the companions that hide it
    if (displayIdAt(pos) != before) sync(pos);
}

std::int64_t displayIdAt(GlobalPos const& pos) {
    auto* region = game::regionOf(pos.dim);
    return region ? displayIdAt(*region, game::blockPos(pos)) : 0;
}

} // namespace gf::feature::woodwork::rack
