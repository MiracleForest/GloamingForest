// U5 (building): the unit's module (building blocks and boss collectibles) and its config section.

#include "feature/building/Building.h"

#include <cstdint>

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/level/block/BlockType.h"

#include "feature/ext/building/Collision.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

#ifdef GF_CLIENT
#include "client/input/Input.h"
#include "feature/net/Net.h"
#endif

GF_REGISTER_CONFIG("building", gf::feature::building::BuildingConfig);

namespace gf::feature::building {

void refreshBlockTypes(char const* why) {
    auto const* rope    = applyBlockProperties();
    bool        changed = ::gf::building::ext::refreshCollisionTypes();
    if (!changed) return;
    bool scaffolding = rope && (static_cast<std::uint64_t>(rope->mProperties) & static_cast<std::uint64_t>(BlockProperty::Scaffolding)) != 0;
    bool provider    = rope && ::gf::building::ext::providerOf(rope) != nullptr;
    // e2e (server and client remote-mod): the rope's type of this registry has both
    log().info("building: block types refreshed ({}): rope scaffolding {}, rope collision provider {}", why, scaffolding, provider);
}

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
        // the block properties (fiery InfiniBurn, rope Scaffolding) and the rope's entity-dependent
        // collision (contract extension, feature/ext/building/Collision.h) live on the block types of
        // the current registry. The BDS builds it once; the client builds it for every world: a local
        // world (onWorldReady) and a remote one, which never fires onWorldReady (WorldDb.h): the
        // companion handshake, and for any server (a vanilla one too) the local player's ticks, which
        // look the rope up once a second and refresh when its type is a new one.
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldReady([] { refreshBlockTypes("world ready"); });
#ifdef GF_CLIENT
            net::client::onConnected([] { refreshBlockTypes("companion connected"); });
            gf::client::movement::onLocalTick(-1000, [](gf::client::movement::MoveState&) {
                static std::uint32_t   tick = 0;
                static BlockType const* last = nullptr;
                if (tick++ % 20 != 0) return;
                auto type = BlockType::tryGetFromRegistry(HashedString("twilightforest:rope"));
                BlockType const* now = type ? &*type : nullptr;
                // a new registry may reuse the old type's address: its missing Scaffolding bit
                // tells it apart
                bool bare = now && (static_cast<std::uint64_t>(now->mProperties) & static_cast<std::uint64_t>(BlockProperty::Scaffolding)) == 0;
                if (now == last && !bare) return;
                last = now;
                if (now) refreshBlockTypes("client registry");
            });
#endif
        }
        refreshBlockTypes("install");
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
