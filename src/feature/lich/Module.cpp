// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/loot/conditions/IsMinionCondition.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U21 (lich): the unit's module. Registers the Lich (boss kit, spawner, lifecycle), its minions,
// the Death Tome, the rising zombie, the three projectiles, the lich tower and the
// gloamingforest:is_minion loot condition.

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "mc/world/actor/Actor.h"

#include "feature/boss/Boss.h"
#include "feature/lich/Common.h"
#include "feature/lich/Edits.h"
#include "feature/lich/Lich.h"
#include "feature/lich/Pending.h"
#include "feature/lich/Tower.h"
#include "feature/loot/Loot.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::lich {

namespace {

class LichModule {
public:
    void install() {
        static bool once = false; // the contracts have no removal: register once per process
        if (!once) {
            once = true;
            registerLich();
            registerMobs();
            registerProjectiles();
            tower::registerTower();
            edits::registerEdits();
            game::db::onWorldReady([] { pending::load(); });
            game::db::onWorldSave([] { pending::save(); });
            // the client opens many worlds in one process: nothing of a closed world stays queued
            game::db::onWorldClosed([] {
                pending::clear();
                forgetAllRandoms();
            });
            // IsMinionCondition(inverse): THIS_ENTITY is a Carminite Ghastling whose isMinion() == !inverse
            gf::loot::registerCondition("gloamingforest:is_minion", [](gf::loot::LootContext const& ctx, Json const& config, gf::loot::RandomSource&) {
                bool inverse = config.is_object() && config.value("inverse", false);
                if (!ctx.thisEntity || ctx.thisEntity->getTypeName() != "gloamingforest:carminite_ghastling") return false;
                return gf::boss::isMinion(*ctx.thisEntity) == !inverse;
            });
        }
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](ll::event::ServerLevelTickEvent&) {
            levelTick();
        });
    }
    void uninstall() {
        if (mTick) ll::event::EventBus::getInstance().removeListener(mTick);
        mTick = nullptr;
    }

private:
    ll::event::ListenerPtr mTick;
};

} // namespace

} // namespace gf::feature::lich

GF_REGISTER_MODULE(gf::feature::lich::LichModule);
