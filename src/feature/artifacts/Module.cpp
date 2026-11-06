// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFItems.java (rarities)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U9 (artifacts): the unit's module. Registers the scepters and their bolt and zombie, the
// fortification shields, the charms and the keepsake casket, the potion flasks, the ore meter, the
// other artifacts and the four custom recipe types.
#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "feature/artifacts/Artifacts.h"
#include "feature/ext/item/Item.h"
#include "game/WorldDb.h"
#include "mod/Module.h"

namespace gf::feature::artifacts {

namespace {

class ArtifactsModule {
public:
    void install() {
        static bool once = false; // the contracts have no removal: register once per process
        if (!once) {
            once = true;
            registerRecipes();
            registerScepters();
            registerWandBolt();
            registerLoyalZombie();
            registerShields();
            registerCharms();
            registerCasket();
            registerFlasks();
            registerOreMeter();
            registerMisc();
            // TFItems: Rarity.UNCOMMON
            for (char const* id : {kCharmEffect, kLife1, kLife2, kKeeping1, kKeeping2, kKeeping3, kCrownSplinter, kEmperorsCloth, kEssence,
                                   kFortificationScepter, kGreaterFlask, kCasket, kLifedrainScepter, kMoonDial, kMysticCrown, kOreMeter, kPocketWatch,
                                   kTwilightScepter, kZombieScepter})
                if (std::string_view(id) != kCharmEffect) gf::item::ext::setRarity(id, gf::item::ext::Rarity::Uncommon);
        }
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([this](ll::event::ServerLevelTickEvent&) {
            if (++mTicks % 200 == 0) pruneRandoms();
        });
    }
    void uninstall() {
        if (mTick) ll::event::EventBus::getInstance().removeListener(mTick);
        mTick = nullptr;
    }

private:
    ll::event::ListenerPtr mTick;
    std::uint64_t          mTicks = 0;
};

} // namespace

} // namespace gf::feature::artifacts

GF_REGISTER_MODULE(gf::feature::artifacts::ArtifactsModule);
