// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/processors/SpawnerProcessor.java (compile)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// Contract extension of "structure" (rule R18: F7, new files only; folded into the contract by A8
// in W2b). upstream builds some processors in code rather than in data: SpawnerProcessor.compile is how
// the lich tower revamp (LichTowerUtil roomSpawners / centralSpawners, U21 / U22) makes its
// spawner processors. The result is a TemplateSettings::processors entry (inline processor JSON).

#include <string>
#include <utility>
#include <vector>

namespace gf::structure::ext {

/// SpawnerProcessor.compile(range, maxWidth, weightMap): a gloamingforest:spawner_processor with
/// SpawnRange `range` (none when <= 0), start delay factor 0.25, entity_width_max `maxWidth` (none
/// when <= 0) and one SpawnData per entry of `weights` (entity id, weight; in the map's iteration
/// order, as upstream's Object2IntArrayMap), each with the custom spawn rules block light 0..7 and sky
/// light 0..15. Spawners placed through it keep those rules: their mobs spawn only where the block
/// light is 0..7 (BaseSpawner with CustomSpawnRules).
[[nodiscard]] std::string compileSpawnerProcessor(int range, float maxWidth, std::vector<std::pair<std::string, int>> const& weights);

} // namespace gf::structure::ext
