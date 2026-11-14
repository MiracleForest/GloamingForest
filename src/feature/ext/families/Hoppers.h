#pragma once

// Contract extension of "families+blockevents" (F11, rule R18: new files only; A8 folds it, for
// example as a per-declaration flag of the inventory contract next to Declared::chest). Which
// native containers of the inventory contract the game's hoppers, hopper minecarts and droppers
// reach.
//
// Java's HopperBlockEntity#getContainerAt finds the block entity of a container block (a chest's
// through ChestBlock#getContainer); a block entity that is not a Container stays out of reach.
// src/feature/families/Hoppers.cpp shows the game's hoppers the native container (the data-driven
// block actor of minecraft:block_entity, F10's backend) of a block that is visible here: by default
// every block declared chest-like (the inventory contract's Declared::chest, ChestBlock's rules:
// upstream's chests and trapped chests, which tools/families declares); any other block only when a unit
// says so (Java: a Container block entity that is not a chest, such as the skull chest or the
// keepsake casket), and a unit may hide a chest-like one (Java: not a Container). Blocks whose slots
// are in block data (no minecraft:block_entity) stay invisible whatever is set here.
//
//     gf::families_ext::setHopperVisible("gloamingforest:keepsake_casket", true);

#include <string>

namespace gf::families_ext {

/// Sets whether hoppers, hopper minecarts and droppers reach the native container of `blockId`
/// (overrides the default; any thread, usually at install).
void setHopperVisible(std::string const& blockId, bool visible);
/// Forgets a setHopperVisible of `blockId`: the default again.
void resetHopperVisible(std::string const& blockId);
/// Whether hoppers reach the native container of `blockId`: the value set, else whether the block
/// is declared chest-like (the inventory contract's Declared::chest).
[[nodiscard]] bool hopperVisible(std::string const& blockId);

} // namespace gf::families_ext
