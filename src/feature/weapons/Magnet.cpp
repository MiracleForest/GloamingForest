// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/item/OreMagnetItem.java
//   src/main/java/gloamingforest/events/ToolEvents.java (refreshOreMagnetCache)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// gloamingforest:ore_magnet: released after more than 10 use ticks it casts a 32-block voxel line
// from the eye along the look (then the eight 10-degree offsets in upstream's order until something
// moved): the first #gloamingforest:ore_magnet/ore_safe_replace_block block is the base, the first
// ore after it (an ore of a c:ores_in_ground/<g> tag with a c:ore_bearing_ground/<g> tag, not in
// #gloamingforest:ore_magnet/ignored_ores; ancient debris -> netherrack) is pulled: up to 24
// connected blocks of it move by base - ore, leaving their ground block behind.

#include "feature/weapons/Weapons.h"

#include <map>

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/fx/Fx.h"
#include "feature/item/Item.h"
#include "feature/tags/Tags.h"
#include "feature/weapons/World.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::weapons {

namespace {

using gf::tags::Registry;

/// OreMagnetItem#MAGNET_ORE_TO_BLOCK_REPLACEMENTS (ToolEvents#refreshOreMagnetCache), built at
/// first use from the tag runtime (the c: ore tags the vanilla bridge answers).
std::map<std::string, std::string> const& oreToGround() {
    static std::map<std::string, std::string> const m = [] {
        std::map<std::string, std::string> out;
        for (char const* g : {"stone", "deepslate", "netherrack", "root"}) {
            auto ores    = gf::tags::members(Registry::Block, std::string("c:ores_in_ground/") + g);
            auto grounds = gf::tags::members(Registry::Block, std::string("c:ore_bearing_ground/") + g);
            for (auto const& ground : grounds)
                for (auto const& ore : ores)
                    if (!gf::tags::has(Registry::Block, "gloamingforest:ore_magnet/ignored_ores", ore)) out[ore] = ground;
        }
        if (!gf::tags::has(Registry::Block, "gloamingforest:ore_magnet/ignored_ores", "minecraft:ancient_debris") && !out.contains("minecraft:ancient_debris"))
            out["minecraft:ancient_debris"] = "minecraft:netherrack";
        return out;
    }();
    return m;
}

bool safeReplace(std::string const& id) { return gf::tags::has(Registry::Block, "gloamingforest:ore_magnet/ore_safe_replace_block", id); }

} // namespace

int magnetLine(BlockSource& region, BlockPos const& use, BlockPos const& dest) {
    std::optional<cw::I3> base, found;
    Block const*          ore = nullptr;
    std::string           replacement;
    for (auto const& c : cw::voxelLine({use.x, use.y, use.z}, {dest.x, dest.y, dest.z})) {
        BlockPos     p{c.x, c.y, c.z};
        Block const& b  = region.getBlock(p);
        auto         id = game::blockId(b);
        if (!base) {
            if (safeReplace(id)) base = c;
        } else if (!found && !b.isAir()) {
            auto it = oreToGround().find(id);
            if (it != oreToGround().end() && !region.getBlockEntity(p)) {
                ore         = &b;
                replacement = it->second;
                found       = c;
            }
        }
    }
    if (!base || !found || !ore) return 0;
    auto vein = cw::findVein(*found, [&](cw::I3 const& p) { return &region.getBlock(BlockPos{p.x, p.y, p.z}) == ore; });
    int  ox = base->x - found->x, oy = base->y - found->y, oz = base->z - found->z;
    auto const* ground = game::blockWith(replacement);
    if (!ground) ground = game::blockWith("minecraft:stone");
    int moved = 0;
    for (auto const& c : vein) {
        BlockPos     to{c.x + ox, c.y + oy, c.z + oz};
        Block const& there = region.getBlock(to);
        if (safeReplace(game::blockId(there)) || game::replaceable(region, to) || there.isAir()) {
            game::setBlock(region, BlockPos{c.x, c.y, c.z}, *ground);
            game::setBlock(region, to, *ore);
            ++moved;
        }
    }
    return moved;
}

int magnetUse(Player& player) {
    auto& region = player.getDimensionBlockSource();
    auto  eye    = eyeOf(player);
    auto  rot    = player.getRotation(); // x pitch, y yaw
    int   moved  = 0;
    for (auto const& [yaw, pitch] : cw::magnetTries()) {
        auto look = cw::offsetLook(rot.y, rot.x, yaw, pitch);
        auto src  = cw::containing(v3(eye));
        auto dst  = cw::containing({eye.x + look.x * 32.0, eye.y + look.y * 32.0, eye.z + look.z * 32.0});
        moved     = magnetLine(region, BlockPos{src.x, src.y, src.z}, BlockPos{dst.x, dst.y, dst.z});
        if (moved > 0) break;
    }
    return moved;
}

void registerMagnet() {
    gf::item::registerItem(
        kOreMagnet,
        {
            .use     = [](Player&, ItemStack&) { return gf::item::Result::Pass; },
            .release =
                [](Player& player, ItemStack& stack, int used) {
                    if (used <= 10) return;
                    int moved = magnetUse(player);
                    if (moved > 0) {
                        hurtAndBreak(stack, moved, &player);
                        gf::fx::sound(player.getDimension(), "item.gloamingforest.magnet.grab", player.getFeetPos(), 1.0F, 1.0F);
                        log().info("weapons: {}'s ore magnet moved {} blocks", player.getRealName(), moved);
                    }
                },
        }
    );
}

} // namespace gf::feature::weapons
