// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/loot/conditions/IsMinionCondition.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U21 (lich): the unit's module. Registers the Lich (boss kit, spawner, lifecycle), its minions,
// the Death Tome, the rising zombie, the three projectiles, the lich tower and the
// twilightforest:is_minion loot condition.

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "mc/world/actor/Actor.h"

#include "feature/boss/Boss.h"
#include "feature/lich/Common.h"
#include "feature/lich/Lich.h"
#include "feature/lich/Tower.h"
#include "feature/loot/Loot.h"
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
            // IsMinionCondition(inverse): THIS_ENTITY is a Carminite Ghastling whose isMinion() == !inverse
            gf::loot::registerCondition("twilightforest:is_minion", [](gf::loot::LootContext const& ctx, Json const& config, gf::loot::RandomSource&) {
                bool inverse = config.is_object() && config.value("inverse", false);
                if (!ctx.thisEntity || ctx.thisEntity->getTypeName() != "twilightforest:carminite_ghastling") return false;
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
