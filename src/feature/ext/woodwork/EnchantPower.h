#pragma once

// Contract extension of U4 woodwork (rule R18: new files only; A8 folds it into the data contract
// next to gf::tags). Java's EnchantingTableBlock counts every block of
// #minecraft:enchantment_power_provider on its ring; Bedrock's table counts only its own bookshelves
// (EnchantUtils::getBookCasePositions, which the enchanting menu's costs and the table's glyph
// particles both read). A provider adds the custom blocks that count:
//
//     gf::woodwork_ext::enchantpower::setProvider([](BlockSource& region, BlockPos const& table) {
//         return std::vector<BlockPos>{...};   // TF blocks of the tag on the ring, with free space between
//     });
//
// The positions are appended to the game's list (block positions; the game reads them as Vec3).
// Server and client thread (the client draws the glyphs from the same list). One provider (a later
// call replaces it; an empty one removes it).

#include <functional>
#include <vector>

class BlockPos;
class BlockSource;

namespace gf::woodwork_ext::enchantpower {

using ProviderFn = std::function<std::vector<BlockPos>(BlockSource& region, BlockPos const& table)>;
void setProvider(ProviderFn fn);

/// Installs / removes the EnchantUtils::getBookCasePositions hook.
void install();
void uninstall();

} // namespace gf::woodwork_ext::enchantpower
