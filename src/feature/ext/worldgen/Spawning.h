#pragma once

// Contract extension of "spawn" (F5, rule R18; to be folded into Spawn.h by A8).
//
// TF's landmark controlled spawns (EntityEvents#gatherPotentialSpawns) need two answers that the
// landmark contract's Start does not carry:
// - the spawn list index at a position (EntityEvents#getSpawnListIndexAt: the highest
//   SpawnIndexProvider index of the pieces containing it; -1 or nothing: no controlled monsters).
//   Without a registered function the start's data "spawn_indices" is used (one int per entry of
//   Start::pieceBoxes, null for a piece that is not a SpawnIndexProvider), else every piece has
//   index 0 (TFStructureComponent's default). F6 / the structure units register their pieces'
//   indices per landmark key (dark tower roof 1, final castle floors 1..3, boss rooms -1, ...).
// - an extra area check of the landmark (HollowHillStructure#canSpawnMob, the half dome of the
//   hollow hills, is built in for the three hollow hill keys).
//
//     gf::spawn::ext::registerSpawnIndex("twilightforest:dark_tower", [](auto const& start, BlockPos const& pos) { ... });

#include <functional>
#include <string>

#include "feature/landmark/Landmark.h"

class BlockPos;

namespace gf::spawn::ext {

using SpawnIndexFn = std::function<int(landmark::Start const& start, BlockPos const& pos)>;
/// Replaces the spawn list index lookup of landmark `key`.
void registerSpawnIndex(std::string const& landmarkKey, SpawnIndexFn fn);

using SpawnAreaFn = std::function<bool(landmark::Start const& start, BlockPos const& pos)>;
/// Adds a condition monsters of landmark `key` must meet (false: the controlled list does not apply).
void registerSpawnArea(std::string const& landmarkKey, SpawnAreaFn fn);

} // namespace gf::spawn::ext
