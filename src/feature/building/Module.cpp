// U5 (building): the unit's module (building blocks and boss collectibles) and its config section.

#include "feature/building/Building.h"

#include "feature/ext/building/Collision.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

GF_REGISTER_CONFIG("building", gf::feature::building::BuildingConfig);

namespace gf::feature::building {

namespace {

struct BuildingModule {
    // TF's banner patterns join Bedrock's registry after the vanilla ones, before a world exists, so
    // both builds give them the same ids (vanilla count + 0..8). BDS: at load (the vanilla registry is
    // built). Client: LeviLamina loads mods before the game's static initialisers, so the registry is
    // still empty at load and the patterns join at install (game start, no world yet). Banners carry
    // the pattern's nameID string on disk and over the network, never the index.
    void load() {
        int n = registerBannerPatterns();
        if (n >= 0) log().info("building: {} banner patterns registered", n);
    }
    void install() {
        if (!bannerPatternsRegistered()) {
            int n = registerBannerPatterns();
            if (n < 0) log().error("building: the banner pattern registry is empty at install: TF patterns not registered");
            else log().info("building: {} banner patterns registered", n);
        }
        logBannerIds();
        installBannerHooks();
        installTrophies();
        installNagastone();
        installRope();
        installStorage();
        // the rope's entity-dependent collision (contract extension, feature/ext/building/Collision.h);
        // the client builds its block registry per world
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldReady([] { ::gf::building::ext::refreshCollisionTypes(); });
        }
        ::gf::building::ext::refreshCollisionTypes();
        ::gf::building::ext::installCollision();
        log().info("building: installed");
    }
    void uninstall() {
        uninstallBannerHooks();
        uninstallTrophies();
        uninstallNagastone();
        uninstallRope();
        ::gf::building::ext::uninstallCollision();
    }
};

} // namespace

} // namespace gf::feature::building

GF_REGISTER_MODULE(gf::feature::building::BuildingModule);
