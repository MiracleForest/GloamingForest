// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/registration/biomes/BiomeHelper.java
//   src/main/java/twilightforest/init/TFBiomes.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The natural spawn tables of the 22 biomes (BiomeHelper's MobSpawnSettings builders, assigned per
// biome as TF's biome data pack does). The selftest biomes.spawn_tables checks them against the
// imported biome JSON entry for entry.

#include "feature/biomes/Biomes.h"

#include <map>

#include "mod/Log.h"

namespace gf::feature::biomes {

namespace {

using spawn::SpawnEntry;

struct Builder {
    BiomeSpawns s;
    Builder& probability(float p) {
        s.table.creatureProbability = p;
        return *this;
    }
    Builder& add(char const* category, char const* id, int weight, int min, int max) {
        s.table.byCategory[category].push_back(SpawnEntry{id, weight, min, max});
        return *this;
    }
    /// addMobCharge(type, charge, energyBudget)
    Builder& charge(char const* id, double charge, double energyBudget) {
        s.costs.push_back(SpawnCost{id, energyBudget, charge});
        return *this;
    }
};

// MobSpawnSettings.Builder's default creatureGenerationProbability
constexpr float kDefaultProbability = 0.1f;

BiomeSpawns none() { return Builder{}.probability(kDefaultProbability).s; }

BiomeSpawns penguinSpawning() {
    return Builder{}.probability(0.15f).add("creature", "twilightforest:penguin", 10, 2, 4).s;
}

BiomeSpawns darkForestSpawning() {
    return Builder{}
        .probability(0.05f)
        .add("monster", "minecraft:enderman", 2, 1, 2)
        .charge("minecraft:enderman", 0.75, 0.15)
        .add("monster", "minecraft:zombie", 5, 1, 2)
        .charge("minecraft:zombie", 0.7, 0.15)
        .add("monster", "minecraft:skeleton", 5, 1, 2)
        .charge("minecraft:skeleton", 0.8, 0.15)
        .add("monster", "twilightforest:mist_wolf", 5, 1, 1)
        .charge("twilightforest:mist_wolf", 0.75, 0.2)
        .add("monster", "twilightforest:skeleton_druid", 5, 1, 1)
        .charge("twilightforest:skeleton_druid", 0.8, 0.2)
        .add("monster", "twilightforest:king_spider", 1, 1, 1)
        .charge("twilightforest:king_spider", 0.85, 0.25)
        .add("monster", "twilightforest:kobold", 10, 1, 3)
        .charge("twilightforest:kobold", 0.7, 0.15)
        .add("monster", "minecraft:witch", 2, 1, 1)
        .charge("minecraft:witch", 0.75, 0.15)
        .s;
}

BiomeSpawns snowForestSpawning() {
    return Builder{}
        .probability(0.05f)
        .add("monster", "twilightforest:winter_wolf", 5, 1, 1)
        .charge("twilightforest:winter_wolf", 0.6, 0.15)
        .add("monster", "twilightforest:yeti", 5, 1, 1)
        .charge("twilightforest:yeti", 0.6, 0.15)
        .s;
}

BiomeSpawns ravenSpawning() { return Builder{}.probability(0.3f).add("creature", "twilightforest:raven", 10, 4, 4).s; }

BiomeSpawns swampSpawning() {
    return Builder{}
        .probability(kDefaultProbability)
        .add("monster", "minecraft:zombie", 10, 1, 2)
        .charge("minecraft:zombie", 0.7, 0.15)
        .add("monster", "twilightforest:mosquito_swarm", 10, 1, 1)
        .charge("twilightforest:mosquito_swarm", 0.7, 0.15)
        .s;
}

BiomeSpawns spookSpawning() {
    return Builder{}
        .probability(kDefaultProbability)
        .add("monster", "minecraft:spider", 50, 1, 2)
        .charge("minecraft:spider", 0.75, 0.25)
        .add("monster", "minecraft:skeleton", 20, 1, 2)
        .charge("minecraft:skeleton", 0.85, 0.25)
        .add("monster", "twilightforest:skeleton_druid", 5, 1, 1)
        .charge("twilightforest:skeleton_druid", 0.95, 0.25)
        .add("ambient", "minecraft:bat", 20, 2, 4)
        .s;
}

BiomeSpawns defaultMobSpawning() {
    return Builder{}
        .probability(0.15f)
        .add("creature", "twilightforest:bighorn_sheep", 12, 4, 4)
        .add("creature", "twilightforest:boar", 10, 4, 4)
        .add("creature", "minecraft:chicken", 10, 4, 4)
        .add("creature", "twilightforest:deer", 15, 4, 5)
        .add("creature", "minecraft:wolf", 5, 4, 4)
        .add("creature", "twilightforest:tiny_bird", 15, 4, 8)
        .add("creature", "twilightforest:squirrel", 10, 2, 4)
        .add("creature", "twilightforest:dwarf_rabbit", 10, 4, 5)
        .add("creature", "twilightforest:raven", 10, 1, 2)
        .s;
}

BiomeSpawns undergroundMobSpawning() {
    return Builder{}
        .probability(kDefaultProbability)
        .add("monster", "minecraft:spider", 10, 2, 3)
        .charge("minecraft:spider", 0.2, 0.15)
        .add("monster", "minecraft:zombie", 10, 1, 2)
        .charge("minecraft:zombie", 0.2, 0.15)
        .add("monster", "minecraft:skeleton", 10, 1, 1)
        .charge("minecraft:skeleton", 0.3, 0.15)
        .add("monster", "minecraft:creeper", 1, 1, 1)
        .charge("minecraft:creeper", 0.35, 0.15)
        .add("monster", "minecraft:slime", 10, 2, 4)
        .charge("minecraft:slime", 0.2, 0.15)
        .add("monster", "minecraft:enderman", 1, 1, 2)
        .charge("minecraft:enderman", 0.4, 0.15)
        .add("monster", "twilightforest:kobold", 10, 1, 3)
        .charge("twilightforest:kobold", 0.2, 0.15)
        .add("ambient", "minecraft:bat", 10, 1, 1)
        .s;
}

} // namespace

BiomeSpawns spawnsOf(std::string const& id) {
    static std::map<std::string, BiomeSpawns (*)()> const table = {
        {"twilightforest:clearing", defaultMobSpawning},
        {"twilightforest:dark_forest", darkForestSpawning},
        {"twilightforest:dark_forest_center", none},
        {"twilightforest:dense_forest", defaultMobSpawning},
        {"twilightforest:dense_mushroom_forest", defaultMobSpawning},
        {"twilightforest:enchanted_forest", defaultMobSpawning},
        {"twilightforest:final_plateau", ravenSpawning},
        {"twilightforest:fire_swamp", none},
        {"twilightforest:firefly_forest", defaultMobSpawning},
        {"twilightforest:forest", defaultMobSpawning},
        {"twilightforest:glacier", penguinSpawning},
        {"twilightforest:highlands", defaultMobSpawning},
        {"twilightforest:highlands_underground", undergroundMobSpawning},
        {"twilightforest:lake", defaultMobSpawning},
        {"twilightforest:mushroom_forest", defaultMobSpawning},
        {"twilightforest:oak_savannah", defaultMobSpawning},
        {"twilightforest:snowy_forest", snowForestSpawning},
        {"twilightforest:spooky_forest", spookSpawning},
        {"twilightforest:stream", defaultMobSpawning},
        {"twilightforest:swamp", swampSpawning},
        {"twilightforest:thornlands", none},
        {"twilightforest:underground", undergroundMobSpawning},
    };
    auto it = table.find(id);
    return it == table.end() ? none() : it->second();
}

void registerSpawns() {
    for (auto const& id : biomeIds()) spawn::setBiomeSpawns(id, spawnsOf(id).table);
    // the spawn costs register on the next server tick (BiomesModule), after every unit's install
}

} // namespace gf::feature::biomes
