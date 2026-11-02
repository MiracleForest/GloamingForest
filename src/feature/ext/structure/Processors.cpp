// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/processors/SpawnerProcessor.java (compile)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "feature/ext/structure/Processors.h"

#include "nlohmann/json.hpp"

namespace gf::structure::ext {

std::string compileSpawnerProcessor(int range, float maxWidth, std::vector<std::pair<std::string, int>> const& weights) {
    using Json = nlohmann::json;
    Json entities = Json::array();
    for (auto const& [id, weight] : weights) {
        // new SpawnData(entityInfo {id}, Optional.of(new CustomSpawnRules(new InclusiveRange<>(0, 7),
        // new InclusiveRange<>(0, 15))), Optional.empty())
        entities.push_back(Json{{"data",
                                 {{"entity", {{"id", id}}},
                                  {"custom_spawn_rules", {{"block_light_limit", Json::array({0, 7})}, {"sky_light_limit", Json::array({0, 15})}}}}},
                                {"weight", weight}});
    }
    Json p{{"processor_type", "twilightforest:spawner_processor"}, {"start_delay_factor", 0.25f}, {"entities", entities}};
    if (range > 0) p["range"] = range;
    if (maxWidth > 0) p["entity_width_max"] = maxWidth;
    return p.dump();
}

} // namespace gf::structure::ext
