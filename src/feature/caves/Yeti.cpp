// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/monster/Yeti.java
//   src/main/java/twilightforest/entity/ai/goal/ThrowRiderGoal.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The yeti: an IHostileMount that grabs its target and throws it (ThrowRiderGoal over F8's hostile
// mount kit, which keeps the YetiThrowAttachment state and turns the fall into yeeted damage),
// angry once hurt by an entity (follow range 4 + 8, the angry pose on the model), and its spawn
// rule (yetiSnowyForestSpawnHandler). Float, stroll, look and the hurt-by target are BP behaviours.
#include "feature/caves/Caves.h"

#include <cmath>
#include <string>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/BrightnessPair.h"

#include "feature/caves/Goals.h"
#include "feature/common/Interactions.h"
#include "feature/entity/Entity.h"
#include "feature/ext/dimension/DimensionType.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/fx/Fx.h"
#include "feature/spawn/Spawn.h"
#include "feature/worldgen/Worldgen.h"
#include "mod/Log.h"

namespace gf::feature::caves {

namespace {

constexpr char kAngryKey[] = "caves:angry"; ///< Yeti Angry

void setAngry(Actor& yeti, bool angry) {
    gf::entity::data(yeti, kAngryKey) = angry;
    gf::entity::setProperty(yeti, "twilightforest:angry", angry);
}

std::string biomeAt(BlockSource& region, BlockPos const& pos) {
    auto id = gf::worldgen::biomeAt(pos);
    return id;
}

/// Yeti.isValidLightLevel.
bool isValidLightLevel(BlockSource& region, BlockPos const& pos, gf::spawn::RandomSource& random) {
    bool snowy = biomeAt(region, pos) == "twilightforest:snowy_forest";
    auto pair  = region.getBrightnessPair(pos);
    if (static_cast<int>(pair.sky->mValue) > random.nextInt(32)) return snowy;
    int darken = gf::world::ext::isThundering(region) ? 10 : gf::world::ext::skyDarken(region);
    int i      = std::max(static_cast<int>(pair.block->mValue), static_cast<int>(pair.sky->mValue) - darken);
    return i <= random.nextInt(8) || snowy;
}

} // namespace

bool yetiAngry(Actor& yeti) {
    auto const& a = gf::entity::data(yeti, kAngryKey);
    return a.is_boolean() && a.get<bool>();
}

/// The ThrowRiderGoal of the yeti and the alpha yeti, with their grab / throw sounds.
std::shared_ptr<ThrowRiderGoal> makeThrowRiderGoal(Mob& mob, bool alpha) {
    auto grab = [alpha](Mob& m) {
        auto& r = fe::randomOf(m);
        if (alpha) gf::fx::sound(m.getDimension(), "entity.twilightforest.alpha_yeti.grab", m.getFeetPos(), 4.0F, 0.75F + r.nextFloat() * 0.25F);
        else gf::fx::sound(m.getDimension(), "entity.twilightforest.yeti.grab", m.getFeetPos(), 1.0F, 1.25F + r.nextFloat() * 0.5F);
    };
    auto thr = [alpha](Mob& m) {
        auto& r = fe::randomOf(m);
        if (alpha) gf::fx::sound(m.getDimension(), "entity.twilightforest.alpha_yeti.throw", m.getFeetPos(), 4.0F, 0.75F + r.nextFloat() * 0.25F);
        else gf::fx::sound(m.getDimension(), "entity.twilightforest.yeti.throw", m.getFeetPos(), 1.0F, 1.25F + r.nextFloat() * 0.5F);
    };
    return std::make_shared<ThrowRiderGoal>(mob, 1.0, false, grab, thr);
}

void installYeti() {
    gf::entity::ext::markHostileMount(kYeti);
    // the BP's rideable seat is for the grab only: nobody climbs on a yeti (or an alpha yeti) by hand
    for (char const* id : {kYeti, kAlphaYeti}) {
        gf::feature::common::interactions::onInteract({.block = id}, [](gf::feature::common::interactions::EntityContext const& c) {
            using gf::feature::common::interactions::Result;
            if (c.player.getVehicle() == &c.entity || c.itemId == "minecraft:name_tag") return Result::Pass;
            return Result::Deny;
        });
    }
    gf::entity::Behaviour b;
    b.onSpawn = [](Actor& self) {
        auto* yeti = fe::asMob(self);
        if (!yeti) return;
        gf::entity::setProperty(self, "twilightforest:angry", yetiAngry(self));
        gf::entity::addGoal(*yeti, 1, makeThrowRiderGoal(*yeti, false));
        // NearestAttackableTargetGoal(Player, true): follow range 4, + 8 while angry
        gf::entity::addTargetGoal(*yeti, 2, std::make_shared<NearestPlayerTargetGoal>(*yeti, [](Mob& m) { return yetiAngry(m) ? 12.0 : 4.0; }));
    };
    b.aiStep = [](Actor& self) {
        // look at things in our jaws
        auto* yeti = fe::asMob(self);
        if (!yeti) return;
        if (auto* rider = yeti->getFirstPassenger()) mobs::lookAt(*yeti, *rider, 100.0F, 100.0F);
    };
    b.hurt = [](Actor& self, float&, std::string const&, Actor* attacker) {
        // become angry (a creative player's hit does not count)
        bool creative = attacker && attacker->isPlayer() && static_cast<Player*>(attacker)->isCreative();
        if (attacker && !creative && !yetiAngry(self)) {
            setAngry(self, true);
            gf::log().info("caves: yeti angry");
        }
        return true;
    };
    gf::entity::registerType(kYeti, b);
    // Yeti.yetiSnowyForestSpawnHandler (Mob#checkMobSpawnRules is the spawn kit's ground check)
    gf::spawn::registerPlacement(kYeti, gf::spawn::PlacementRule::OnGround,
                                 [](BlockSource& region, std::string const&, BlockPos const& pos, gf::spawn::SpawnReason, gf::spawn::RandomSource& random) {
                                     auto level = ll::service::getLevel();
                                     if (level && level->getDifficulty() == SharedTypes::Legacy::Difficulty::Peaceful) return false;
                                     if (biomeAt(region, pos) == "twilightforest:snowy_forest") return true;
                                     return isValidLightLevel(region, pos, random);
                                 });
}

} // namespace gf::feature::caves
