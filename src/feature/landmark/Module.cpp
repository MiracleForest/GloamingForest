// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/events/EntityEvents.java (structureSpecialSpawns)
//   src/main/java/twilightforest/command/TFTeleportCommand.java (setup_structure_hunt's teleport)
//   src/main/resources/data/twilightforest/function/setup_structure_hunt.mcfunction
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The landmark unit's module (F6): descriptors at install, the start store per world, the
// protection handlers, the worldgen stages, the controlled spawns and the server tick.
//
// The function twilightforest:setup_structure_hunt is the BP function landmark/setup_structure_hunt
// (functions are parsed before mods add commands, so it cannot call /twilightforest): it tags the
// nearest player, switches it to spectator with night vision, and this module sends the tagged
// player to the Twilight Forest at the same x / z (TF: `execute in twilightforest:twilight_forest
// run tp @p ~ ~ ~`, with TFTeleportCommand's height scaling).
#include <atomic>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"

#include "feature/dimension/World.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/landmark/Internal.h"
#include "feature/spawn/Spawn.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::landmark {

namespace {

constexpr char kStructureHuntTag[] = "gf_landmark_structure_hunt";

/// EntityEvents#structureSpecialSpawns: the controlled list, else the structure's spawn override.
std::optional<std::vector<spawn::SpawnEntry>> specialSpawns(BlockSource& region, BlockPos const& pos, std::string const& category) {
    int  dim     = static_cast<int>(region.getDimensionId());
    auto convert = [](std::vector<Json> const& list) {
        std::vector<spawn::SpawnEntry> out;
        for (auto const& e : list)
            out.push_back({e.value("type", std::string{}), e.value("weight", 1), e.value("minCount", 1), e.value("maxCount", 1)});
        return out;
    };
    if (auto controlled = gf::landmark::ext::potentialSpawns(dim, category, pos.x, pos.y, pos.z)) return convert(*controlled);
    // Structure#spawnOverrides (StructureSpawnOverride: "full" = the start box, "piece" = a piece)
    for (auto const& rec : recordsIntersecting(dim, pos.x >> 4, pos.z >> 4)) {
        if (rec->placed) continue; // /place stores no start: no structure references the chunk
        auto const* inf = info(rec->start.key);
        if (!inf || rec->pieces.empty() || !inf->d.spawnOverrides.is_object() || !inf->d.spawnOverrides.contains(category)) continue;
        auto const& o      = inf->d.spawnOverrides[category];
        bool        inside = false;
        if (o.value("bounding_box", std::string{"piece"}) == "full") inside = rec->start.box.contains(pos.x, pos.y, pos.z);
        else inside = !pieceBoxesAt(*rec, pos.x, pos.y, pos.z).empty();
        if (!inside) continue;
        std::vector<Json> list;
        for (auto const& e : o.value("spawns", Json::array())) list.push_back(e);
        return convert(list);
    }
    return std::nullopt;
}

/// setup_structure_hunt: the tagged player goes to the Twilight Forest (TFTeleportCommand#run).
void structureHunt(Level& level) {
    std::vector<ActorUniqueID> tagged;
    level.forEachPlayer([&](Player& p) {
        if (p.hasTag(kStructureHuntTag)) tagged.push_back(p.getOrCreateUniqueID());
        return true;
    });
    for (auto id : tagged) {
        auto* p = level.getPlayer(id);
        if (!p) continue;
        p->removeTag(kStructureHuntTag);
        auto tf = gf::world::twilight();
        auto* from = game::regionOf(static_cast<int>(p->getDimensionId()));
        if (!tf || !from) {
            p->sendMessage(trFor(*p, "commands.tffeature.teleport.dimension_missing", "The Twilight Forest dimension is unavailable."));
            continue;
        }
        auto   pos = p->getFeetPos();
        double minY = from->getMinHeight(), maxY = from->getMaxHeight();
        double tfMin = gf::world::kMinY, tfMax = gf::world::kMinY + gf::world::kHeight;
        double y     = (pos.y - minY) / (maxY - minY) * (tfMax - tfMin) + tfMin;
        gf::world::teleport(*p, *tf, Vec3{pos.x, static_cast<float>(y), pos.z}, 0.0f);
        log().info("landmark: setup_structure_hunt sent {} to the Twilight Forest", p->getRealName());
    }
}

} // namespace

class LandmarkModule {
public:
    void install() {
        loadDescriptors();
        (void)registerFixture();
        installProtection();
        installHints();
        static bool once = false;
        if (!once) {
            once = true;
            registerWorldgenStages();
            spawn::onSpecialSpawn(specialSpawns);
            game::db::onWorldReady([] {
                resetProtection();
                resetHints();
                loadRecords();
                (void)levelSeed();
            });
            game::db::onWorldSave([] { flushRecords(); });
            game::db::onWorldClosed([] {
                flushRecords();
                clearRecords();
                resetProtection();
            });
        }
        if (game::db::available()) {
            loadRecords(); // BDS: the world exists when mods are enabled
            (void)levelSeed();
        }
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([this](auto&) {
            auto tick = ++mTicks;
            protectionTick(tick);
            auto level = ll::service::getLevel();
            if (!level) return;
            if (tick % 20 == 0) structureHunt(*level);
            if (tick % 200 == 0) flushRecords();
        });
    }

    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mTick);
        uninstallProtection();
        uninstallHints();
        flushRecords();
    }

private:
    ll::event::ListenerPtr     mTick;
    std::atomic<std::uint64_t> mTicks{0};
};

} // namespace gf::feature::landmark

GF_REGISTER_CONFIG("landmark", gf::feature::landmark::LandmarkConfig);
GF_REGISTER_MODULE(gf::feature::landmark::LandmarkModule);
