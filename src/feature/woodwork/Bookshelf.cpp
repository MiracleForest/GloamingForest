// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFBlocks.java (canopy_bookshelf)
//   src/main/java/twilightforest/data/tags/BlockTagGenerator.java (#minecraft:enchantment_power_provider)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The canopy bookshelf counts as a bookshelf for enchanting tables: Java's EnchantingTableBlock
// counts every block of #minecraft:enchantment_power_provider at BOOKSHELF_OFFSETS (x, z in -2..2
// on the ring, y 0..1) whose block halfway to the table is #minecraft:enchantment_power_transmitter
// (replaceable). Bedrock's table counts its own bookshelves only (EnchantUtils::getBookCasePositions):
// the TF members of the tag are added to that list, which the game counts for the enchantment
// costs and draws the table's glyph particles from.

#include <algorithm>
#include <cmath>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/tags/Tags.h"
#include "feature/woodwork/Woodwork.h"
#include "game/Blocks.h"

namespace gf::feature::woodwork {

namespace {

/// A TF block of #minecraft:enchantment_power_provider (the vanilla ones are the game's own).
bool tfPowerProvider(std::string const& id) {
    return id.starts_with(kNs) && tags::has(tags::Registry::Block, "minecraft:enchantment_power_provider", id);
}

} // namespace

/// The TF bookshelves around a table at `table` (Java's isValidBookShelf), as block positions.
std::vector<BlockPos> tfBookshelvesAround(BlockSource& region, BlockPos const& table) {
    std::vector<BlockPos> out;
    for (int y = 0; y <= 1; ++y) {
        for (int x = -2; x <= 2; ++x) {
            for (int z = -2; z <= 2; ++z) {
                if (std::abs(x) != 2 && std::abs(z) != 2) continue;
                BlockPos shelf{table.x + x, table.y + y, table.z + z};
                if (!tfPowerProvider(game::blockId(region.getBlock(shelf)))) continue;
                BlockPos between{table.x + x / 2, table.y + y, table.z + z / 2};
                if (!game::replaceable(region, between)) continue;
                out.push_back(shelf);
            }
        }
    }
    return out;
}

} // namespace gf::feature::woodwork

LL_STATIC_HOOK(
    WoodworkBookcaseHook,
    ll::memory::HookPriority::Normal,
    &EnchantUtils::getBookCasePositions,
    std::vector<Vec3>,
    BlockSource& source,
    Vec3 const&  pos
) {
    auto list = origin(source, pos);
    BlockPos table{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)), static_cast<int>(std::floor(pos.z))};
    for (auto const& shelf : gf::feature::woodwork::tfBookshelvesAround(source, table)) {
        list.push_back(Vec3{static_cast<float>(shelf.x), static_cast<float>(shelf.y), static_cast<float>(shelf.z)});
    }
    return list;
}

namespace gf::feature::woodwork {

void installBookshelves() { WoodworkBookcaseHook::hook(); }
void uninstallBookshelves() { WoodworkBookcaseHook::unhook(); }

} // namespace gf::feature::woodwork
