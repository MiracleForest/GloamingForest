// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/registration/biomes/BiomeHelper.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The spawn costs of TF's biomes (BiomeHelper's addMobCharge: the dark forest's endermen and
// skeletons, the swamp's zombies and mosquito swarms, the spooky forest's skeletons and spiders, the
// snowy forest's winter wolves and yetis, the undergrounds' creepers and endermen) applied to the
// natural spawner as Minecraft 1.21.1 does (NaturalSpawner.SpawnState#canSpawn).
//
// F5's spawn contract has no spawn cost: its natural spawner asks gf::spawn::onSpecialSpawn for the
// list of every pack position (getRandomSpawnMobAt for the pack's first position, canSpawnMobAt's
// "the entry is still in the list here" for every member). In a biome with costs this answers the
// biome's list without the types whose charge would exceed their energy budget at the position, so
// a member is refused exactly where Java's canSpawn refuses it. The potential is rebuilt once per
// tick from the mobs of the dimension (SpawnState: every non-persistent mob whose type has a cost in
// the biome at its block adds that charge at its block), as NaturalSpawner#createState does.
// Deviations (F5 has no hook for them): the mobs spawned during a tick add their charge from the
// next tick on (Java: SpawnState#afterSpawn at once), and when the type over budget is the one a
// pack would have picked at its first position another type of the list is picked instead (Java
// picks it and then refuses every member).

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components/PersistentComponent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"

#include "core/biomes/SpawnCost.h"
#include "feature/biomes/Biomes.h"
#include "feature/worldgen/Worldgen.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::biomes {

namespace {

struct Cost {
    double charge = 0, energyBudget = 0;
};

/// biome id -> entity id -> cost (immutable after the first use).
std::unordered_map<std::string, std::unordered_map<std::string, Cost>> const& costs() {
    static auto const table = [] {
        std::unordered_map<std::string, std::unordered_map<std::string, Cost>> t;
        for (auto const& id : biomeIds())
            for (auto const& c : spawnsOf(id).costs) t[id][c.entityId] = Cost{c.charge, c.energyBudget};
        return t;
    }();
    return table;
}

/// spawnsOf(biome).table, built once per biome (the natural spawner asks per pack member).
spawn::Table const& tableOf(std::string const& biome) {
    static auto const tables = [] {
        std::unordered_map<std::string, spawn::Table> t;
        for (auto const& id : biomeIds()) t[id] = spawnsOf(id).table;
        return t;
    }();
    static spawn::Table const empty;
    auto                      it = tables.find(biome);
    return it == tables.end() ? empty : it->second;
}

Cost const* costOf(std::string const& biome, std::string const& entity) {
    auto b = costs().find(biome);
    if (b == costs().end()) return nullptr;
    auto e = b->second.find(entity);
    return e == b->second.end() ? nullptr : &e->second;
}

/// Mob#isPersistenceRequired / requiresCustomPersistence on Bedrock (as F5's spawner counts mobs).
bool persistent(Actor& a) { return a.getEntityContext().hasComponent<PersistentComponent>() || !a.getNameTag().empty(); }

struct Potential {
    std::uint64_t                    tick  = ~std::uint64_t(0);
    std::uint64_t                    world = ~std::uint64_t(0);
    int                              dim   = -1;
    core::biomes::PotentialCalculator calc;
};

/// The spawn potential of dimension `dim` this tick (NaturalSpawner#createState).
core::biomes::PotentialCalculator const& potentialOf(int dim) {
    static Potential p;
    auto             level = ll::service::getLevel();
    std::uint64_t    tick  = level ? static_cast<std::uint64_t>(level->getCurrentTick().tickID) : 0;
    std::uint64_t    world = worldGeneration();
    if (p.tick == tick && p.dim == dim && p.world == world) return p.calc;
    p.tick  = tick;
    p.dim   = dim;
    p.world = world;
    p.calc.clear();
    if (!level) return p.calc;
    for (Actor* a : level->getRuntimeActorList()) {
        if (!a || static_cast<int>(a->getDimensionId()) != dim || a->isPlayer() || persistent(*a)) continue;
        BlockPos at = game::blockPosOf(a->getFeetPos());
        if (auto const* c = costOf(worldgen::biomeAt(at), a->getTypeName())) p.calc.addCharge(at.x, at.y, at.z, c->charge);
    }
    return p.calc;
}

} // namespace

std::vector<spawn::SpawnEntry> costFiltered(std::string const& biome, std::string const& category, core::biomes::PotentialCalculator const& potential, BlockPos const& pos) {
    auto const&                    table = tableOf(biome);
    std::vector<spawn::SpawnEntry> out;
    auto                           list = table.byCategory.find(category);
    if (list == table.byCategory.end()) return out;
    for (auto const& e : list->second) {
        auto const* c = costOf(biome, e.entityId);
        if (!c || core::biomes::withinBudget(potential, pos.x, pos.y, pos.z, c->charge, c->energyBudget)) out.push_back(e);
    }
    return out;
}

void registerSpawnCosts() {
    static bool registered = false;
    if (registered) return;
    registered = true;
    spawn::onSpecialSpawn([](BlockSource& region, BlockPos const& pos, std::string const& category) -> std::optional<std::vector<spawn::SpawnEntry>> {
        // SpawnState#canSpawn: getRoughBiome(pos) (the biome source's biome, as F5 reads it)
        std::string biome = worldgen::biomeAt(pos);
        auto        b     = costs().find(biome);
        if (b == costs().end() || b->second.empty()) return std::nullopt;
        auto const& table = tableOf(biome);
        auto        it    = table.byCategory.find(category);
        if (it == table.byCategory.end()) return std::nullopt;
        bool anyCost = false;
        for (auto const& e : it->second) anyCost = anyCost || b->second.contains(e.entityId);
        if (!anyCost) return std::nullopt;
        return costFiltered(biome, category, potentialOf(static_cast<int>(region.getDimensionId())), pos);
    });
    gf::log().info("biomes: spawn costs applied to the natural spawner ({} biomes with costs)", costs().size());
}

} // namespace gf::feature::biomes
