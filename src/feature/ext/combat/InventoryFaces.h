#pragma once

// Contract extension of "inventory" by F10 (rule R18; to be folded into
// feature/inventory/Inventory.h by A8 as optional members of gf::inventory::Handler).
//
// WorldlyContainer: a container block whose automation access depends on the side (TF's cinder
// furnace is a FurnaceBlockEntity: UP reaches the input slot 0, the sides the fuel slot 1, DOWN the
// result 2 and the fuel 1; only an empty bucket leaves the fuel slot downwards). The rules of a
// block id apply to the game's hoppers, hopper minecarts and droppers reaching its native container
// (the block's minecraft:block_entity, F10's backend; a family chest's double view included), and
// code asks faceAllows for its own automation.
//
//     gf::inventory::ext::setFaceRules("twilightforest:cinder_furnace", {
//         .slotsForFace = [](auto const&, int face) { return face == 0 ? std::vector{2, 1} : face == 1 ? std::vector{0} : std::vector{1}; },
//     });
//
// Faces are Direction's order: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east, the side of the
// container block the hopper / dropper touches (a hopper above the block pushes through 1, one
// below it pulls through 0).

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/GlobalPos.h"

class ItemStack;

namespace gf::inventory::ext {

struct FaceRules {
    /// WorldlyContainer#getSlotsForFace (empty function: every slot through every side).
    std::function<std::vector<int>(core::GlobalPos const& pos, int face)>                       slotsForFace;
    /// WorldlyContainer#canPlaceItemThroughFace (empty: yes; asked for the slots of slotsForFace).
    std::function<bool(core::GlobalPos const& pos, int slot, ItemStack const& stack, int face)> canPlaceThroughFace;
    /// WorldlyContainer#canTakeItemThroughFace (empty: yes).
    std::function<bool(core::GlobalPos const& pos, int slot, ItemStack const& stack, int face)> canTakeThroughFace;
};

/// Sets the side rules of the containers of `blockId` (replaces earlier ones; server thread, usually
/// at install).
void setFaceRules(std::string const& blockId, FaceRules rules);
/// Removes them: a plain Container again.
void clearFaceRules(std::string const& blockId);
/// The rules of `blockId` (nullopt: none).
[[nodiscard]] std::optional<FaceRules> faceRules(std::string const& blockId);
/// Whether automation may put `stack` into (`place`) or take it out of `slot` of the container of
/// `blockId` at `pos` through side `face` (HopperBlockEntity#canPlaceItemInContainer /
/// #canTakeItemFromContainer); a face outside 0..5 or a block without rules: yes.
[[nodiscard]] bool faceAllows(std::string const& blockId, core::GlobalPos const& pos, int slot, ItemStack const& stack, int face, bool place);

} // namespace gf::inventory::ext
