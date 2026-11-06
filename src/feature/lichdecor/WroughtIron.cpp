// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/WroughtIronFenceBlock.java
//   src/main/java/twilightforest/item/WroughtIronFenceItem.java
//   src/main/java/twilightforest/events/EntityEvents.java (attachLeadToWroughtFence)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U22 (lichdecor): twilightforest:wrought_iron_fence. Its sides connect to walls, iron bars / panes
// and sturdy faces (not to other wrought iron fences: those are not sturdy and not IronBarsBlock);
// each connected side is full / top / bottom / middle by whether the fence above / below also
// connects that way; `post` is capped while the block above is replaceable, else post when this
// fence or any fence of the column "should be a post" (core/lichdecor/Arguments: shouldBePost),
// else none. The shape is recomputed on placement and on every neighbour change (Java's
// getStateForPlacement / updateShape). The item's sneak-use on the top fence toggles the finial; a
// lead on a fence with a post ties the player's leashed mobs to a leash knot there.

#include <string>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/ecs/gamerefs_entity/GameRefsEntity.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "core/lichdecor/Arguments.h"
#include "feature/common/Interactions.h"
#include "feature/families/BlockEvents.h"
#include "feature/item/Item.h"
#include "feature/lichdecor/Lichdecor.h"
#include "feature/lichdecor/TestAccess.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::lichdecor {

namespace {

namespace cl = gf::core::lichdecor;
namespace ia = gf::interactions;

constexpr char kFence[] = "twilightforest:wrought_iron_fence";

bool isFence(Block const& b) { return game::blockId(b) == kFence; }

/// Block#isExceptionForConnection.
bool exceptionForConnection(std::string const& id) {
    if (tags::has(tags::Registry::Block, "minecraft:leaves", id) || id.ends_with("_leaves")) return true;
    if (id.find("shulker_box") != std::string::npos) return true;
    return id == "minecraft:barrier" || id == "minecraft:carved_pumpkin" || id == "minecraft:lit_pumpkin" || id == "minecraft:melon_block" ||
           id == "minecraft:pumpkin";
}

bool isBarsOrPane(std::string const& id) { return id == "minecraft:iron_bars" || id.ends_with("glass_pane"); }

/// connectsTo(state, isFaceSturdy(level, pos, side)): `face` is the neighbour's face toward the fence.
bool connectsTo(Block const& neighbour, int face) {
    auto id = game::blockId(neighbour);
    if (tags::has(tags::Registry::Block, "minecraft:walls", id) || id.ends_with("_wall") || id == "minecraft:cobblestone_wall") return true;
    if (isBarsOrPane(id)) return true;
    if (exceptionForConnection(id)) return false;
    return neighbour.canProvideFullSupport(static_cast<unsigned char>(face));
}

cl::FenceSide sideOf(Block const& b, int face) {
    static char const* const names[] = {"", "", "north_fence", "south_fence", "west_fence", "east_fence"};
    return cl::fenceSideOf(state(b, names[face])).value_or(cl::FenceSide::None);
}

/// makeFenceState(direction).
cl::FenceSide makeFenceState(BlockSource& region, BlockPos const& pos, int dir) {
    BlockPos up = relative(pos, 1), down = relative(pos, 0);
    auto     cont = [&](BlockPos const& at) {
        BlockPos     n  = relative(at, dir);
        Block const& ns = region.getBlock(n);
        return isFence(region.getBlock(at)) && (isFence(ns) || connectsTo(ns, oppositeFace(dir)));
    };
    return cl::fenceSide(cont(up), cont(down));
}

bool shouldBePost(Block const& b) { return cl::shouldBePost(sideOf(b, 2), sideOf(b, 5), sideOf(b, 3), sideOf(b, 4)); }

/// makePost(level, state, pos, allowCap).
cl::PostState makePost(BlockSource& region, Block const& state0, BlockPos const& pos, bool allowCap, bool ownShouldBePost) {
    // shouldAnyBePost: the column above (a capped one counts), then below
    bool any = false;
    for (BlockPos p = relative(pos, 1);; p = relative(p, 1)) {
        auto const& b = region.getBlock(p);
        if (!isFence(b)) break;
        if (shouldBePost(b) || state(b, "post") == "capped") {
            any = true;
            break;
        }
        if (p.y > pos.y + 384) break;
    }
    if (!any) {
        for (BlockPos p = relative(pos, 0);; p = relative(p, 0)) {
            auto const& b = region.getBlock(p);
            if (!isFence(b)) break;
            if (shouldBePost(b)) {
                any = true;
                break;
            }
            if (p.y < pos.y - 384) break;
        }
    }
    if (allowCap && state(state0, "post") == "capped" && game::replaceable(region, relative(pos, 1))) return cl::PostState::Capped;
    if (any) return cl::PostState::Post;
    return ownShouldBePost ? cl::PostState::Post : cl::PostState::None;
}

/// fenceShape over all four sides (getStateForPlacement / updateShape).
Json computeShape(BlockSource& region, BlockPos const& pos, Block const& current) {
    Json changes = Json::object();
    cl::FenceSide sides[6]{};
    for (int dir = 2; dir < 6; ++dir) {
        Block const& n = region.getBlock(relative(pos, dir));
        sides[dir]     = connectsTo(n, oppositeFace(dir)) ? makeFenceState(region, pos, dir) : cl::FenceSide::None;
    }
    changes["north_fence"] = cl::fenceSideName(sides[2]);
    changes["south_fence"] = cl::fenceSideName(sides[3]);
    changes["west_fence"]  = cl::fenceSideName(sides[4]);
    changes["east_fence"]  = cl::fenceSideName(sides[5]);
    bool own               = cl::shouldBePost(sides[2], sides[5], sides[3], sides[4]);
    changes["post"]        = cl::postStateName(makePost(region, current, pos, true, own));
    return changes;
}

void update(BlockSource& region, BlockPos const& pos) {
    auto const& block = region.getBlock(pos);
    if (!isFence(block)) return;
    Json shape = computeShape(region, pos, block);
    bool same  = true;
    for (auto const& [k, v] : shape.items()) same = same && state(block, k) == v.get<std::string>();
    if (!same) setStates(region, pos, block, shape);
}

Level* level() { return ll::service::getLevel().as_ptr(); }

Actor* spawnKnot(BlockSource& region, BlockPos const& post) {
    auto* lvl = level();
    if (!lvl) return nullptr;
    auto owned = lvl->getActorFactory().createSpawnedActor(ActorDefinitionIdentifier("minecraft:leash_knot"), nullptr,
                                                           Vec3{post.x + 0.5F, float(post.y), post.z + 0.5F}, Vec2{0.0F, 0.0F});
    return owned ? lvl->addEntity(region, std::move(owned)) : nullptr;
}

/// LeadItem#bindPlayerMobs: the mobs within 7 blocks leashed to the player go to a knot on the post.
bool bindPlayerMobs(Player& player, BlockSource& region, BlockPos const& pos) {
    AABB box{Vec3{pos.x - 7.0F, pos.y - 7.0F, pos.z - 7.0F}, Vec3{pos.x + 8.0F, pos.y + 8.0F, pos.z + 8.0F}};
    auto   playerId = player.getOrCreateUniqueID();
    Actor* knot     = nullptr;
    bool   any      = false;
    for (Actor* a : region.fetchEntities(static_cast<Actor const*>(nullptr), box, false, false)) {
        if (!a || a->getLeashHolder() != playerId) continue;
        if (!knot) knot = spawnKnot(region, pos);
        if (!knot) return false;
        a->setLeashHolder(knot->getOrCreateUniqueID());
        any = true;
    }
    return any;
}

class WroughtIronModule {
public:
    void install() {
        static bool once = false;
        if (once) return;
        once = true;
        gf::blockevents::onPlaced(kFence, [](BlockSource& region, BlockPos const& pos, Block const&, Actor*) { update(region, pos); });
        gf::blockevents::onNeighborChanged(kFence, [](BlockSource& region, BlockPos const& pos, BlockPos const&) { update(region, pos); });
        // WroughtIronFenceItem#useOn: sneak-use on the top fence of a column toggles the finial
        ia::onUseOn({.block = kFence, .item = kFence}, [](ia::UseOnContext const& c) {
            if (!c.sneaking) return ia::Result::Pass;
            auto* region = game::regionOf(c.pos.dim);
            if (!region) return ia::Result::Pass;
            BlockPos pos = game::blockPos(c.pos);
            if (isFence(region->getBlock(relative(pos, 1)))) return ia::Result::Pass;
            if (!c.firstEvent) return ia::Result::Consume;
            auto const& block  = region->getBlock(pos);
            bool        capped = state(block, "post") == "capped";
            if (capped || game::replaceable(*region, relative(pos, 1))) {
                auto post = capped ? makePost(*region, block, pos, false, shouldBePost(block)) : cl::PostState::Capped;
                setStates(*region, pos, block, Json{{"post", cl::postStateName(post)}});
            }
            playSound(*region, Vec3{pos.x + 0.5F, pos.y + 0.5F, pos.z + 0.5F}, "block.twilightforest.wrought_iron_fence.extend", 0.35F, randomFloat() * 0.1F + 0.75F);
            return ia::Result::Consume;
        }, ia::priority::High);
        // attachLeadToWroughtFence
        ia::onUseOn({.block = kFence, .item = "minecraft:lead"}, [](ia::UseOnContext const& c) {
            if (state(c.block, "post") == "none") return ia::Result::Pass;
            if (!c.firstEvent) return ia::Result::Consume;
            auto* region = game::regionOf(c.pos.dim);
            if (region) bindPlayerMobs(c.player, *region, game::blockPos(c.pos));
            return ia::Result::Consume;
        });
        gf::item::addTooltip(kFence, [](ItemStack const&, std::vector<std::string>& lines, Player const* viewer) {
            auto text = viewer ? trFor(*viewer, "block.twilightforest.wrought_iron_fence.cap", "Shift + right-click the same block to place finial")
                               : tr("block.twilightforest.wrought_iron_fence.cap", "Shift + right-click the same block to place finial");
            lines.push_back(std::string("\xC2\xA7") + "7" + text);
        });
    }
    void uninstall() {}
};

} // namespace

/// Selftest access: recomputes the fence shape at `pos`.
void updateFenceForTest(BlockSource& region, BlockPos const& pos) { update(region, pos); }

} // namespace gf::feature::lichdecor

GF_REGISTER_MODULE(gf::feature::lichdecor::WroughtIronModule);
