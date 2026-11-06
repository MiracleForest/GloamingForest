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
// a member is refused exactly where Java's canSpawn refuses it. The potential is built from the
// mobs near the dimension's players (SpawnState: every non-persistent mob whose type has a cost in
// the biome at its block adds that charge at its block), as NaturalSpawner#createState does, and
// rebuilt as soon as the level's entity count changes, so a mob the spawner just added counts for
// the next member at once (SpawnState#afterSpawn).
// The handler registers on the first server tick, after every unit's install: F5 takes the first
// answer, so a structure's own spawn list (another unit's special spawn) is used as it is.
// Deviation (F5 asks the same question to pick a pack's type and to check each member): when the
// type over budget is the one a pack would have picked at its first position, another type of the
// list is picked instead (Java picks it and then refuses every member).

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/event/world/SpawnMobEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/ecs/gamerefs_entity/GameRefsEntity.h"
#include "mc/entity/components/PersistentComponent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/phys/AABB.h"
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

/// Whether `entity` has a spawn cost in any biome (the others never add to a potential).
bool anyCost(std::string const& entity) {
    static auto const types = [] {
        std::unordered_set<std::string> t;
        for (auto const& [biome, byType] : costs())
            for (auto const& [type, cost] : byType) t.insert(type);
        return t;
    }();
    return types.contains(entity);
}

/// Mob#isPersistenceRequired / requiresCustomPersistence on Bedrock (as F5's spawner counts mobs).
bool persistent(Actor& a) { return a.getEntityContext().hasComponent<PersistentComponent>() || !a.getNameTag().empty(); }

struct Potential {
    std::uint64_t                     tick   = ~std::uint64_t(0);
    std::uint64_t                     world  = ~std::uint64_t(0);
    std::uint64_t                     spawns = ~std::uint64_t(0);
    int                               dim    = -1;
    core::biomes::PotentialCalculator calc;
};

/// Mobs spawned so far (SpawnedMobEvent): a spawn changes the potential (SpawnState#afterSpawn).
std::atomic<std::uint64_t> gSpawns{0};
/// The server thread (the thread of the server level's ticks).
std::atomic<std::thread::id> gServerThread{};

/// The spawn potential of `region`'s dimension now (NaturalSpawner#createState, kept up to date as
/// SpawnState#afterSpawn does): every non-persistent mob near a player of the dimension (F5's
/// spawner counts the mobs of the same boxes) whose type has a cost in the biome at its block adds
/// its charge there. Rebuilt on every tick (mobs moved, despawned) and after every mob spawn (a mob
/// the spawner added a moment ago counts at once); a despawn cannot happen inside a spawner pass
/// (both run on the server thread, the spawner's pass in one go). Server thread only.
core::biomes::PotentialCalculator const& potentialOf(BlockSource& region) {
    static Potential p;
    auto             level  = ll::service::getLevel();
    int              dim    = static_cast<int>(region.getDimensionId());
    std::uint64_t    tick   = level ? static_cast<std::uint64_t>(level->getCurrentTick().tickID) : 0;
    std::uint64_t    world  = worldGeneration();
    std::uint64_t    spawns = gSpawns.load(std::memory_order_acquire);
    if (p.tick == tick && p.dim == dim && p.world == world && p.spawns == spawns) return p.calc;
    p.tick   = tick;
    p.dim    = dim;
    p.world  = world;
    p.spawns = spawns;
    p.calc.clear();
    if (!level) return p.calc;
    std::vector<Vec3> players;
    level->forEachPlayer([&](Player& player) {
        if (static_cast<int>(player.getDimensionId()) == dim) players.push_back(player.getFeetPos());
        return true;
    });
    std::unordered_set<Actor const*> seen; // a mob near several players counts once
    for (auto const& at : players) {
        AABB box{Vec3{at.x - 160, static_cast<float>(region.getMinHeight()), at.z - 160}, Vec3{at.x + 160, static_cast<float>(region.getMaxHeight()), at.z + 160}};
        for (Actor* a : region.fetchEntities(static_cast<Actor const*>(nullptr), box, false, false)) {
            if (!a || a->isPlayer() || !seen.insert(a).second || persistent(*a)) continue;
            if (!anyCost(a->getTypeName())) continue;
            BlockPos pos = game::blockPosOf(a->getFeetPos());
            if (auto const* c = costOf(worldgen::biomeAt(pos), a->getTypeName())) p.calc.addCharge(pos.x, pos.y, pos.z, c->charge);
        }
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
    static ll::event::ListenerPtr spawned = ll::event::EventBus::getInstance().emplaceListener<ll::event::SpawnedMobEvent>([](auto&) {
        gSpawns.fetch_add(1, std::memory_order_acq_rel);
    });
    static ll::event::ListenerPtr ticks = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
        gServerThread.store(std::this_thread::get_id(), std::memory_order_release);
    });
    spawn::onSpecialSpawn([](BlockSource& region, BlockPos const& pos, std::string const& category) -> std::optional<std::vector<spawn::SpawnEntry>> {
        // spawn costs belong to the natural spawner's SpawnState (server thread); chunk-generation
        // spawns (NaturalSpawner#spawnMobsForChunkGeneration) have none, wherever F5 asks for them
        if (std::this_thread::get_id() != gServerThread.load(std::memory_order_acquire)) return std::nullopt;
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
        // nothing over budget: pass (the spawner keeps the list it has; later special spawns are asked)
        auto out = costFiltered(biome, category, potentialOf(region), pos);
        if (out.size() == it->second.size()) return std::nullopt;
        return out;
    });
    gf::log().info("biomes: spawn costs applied to the natural spawner ({} biomes with costs)", costs().size());
}

} // namespace gf::feature::biomes
