// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// The structure kit's module: registers TF's processors, template marker handlers and the shared
// piece types, loads the twilight/* data and the templates when a world is ready, and runs the
// block entity work queued by world generation (loot, spawners, signs, entities) on the server
// thread once its chunks are loaded.
#include <memory>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "feature/structure/Internal.h"
#include "feature/structure/bases/BasesInternal.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::structure {

namespace s = gf::structure;

class StructureModule {
public:
    void install() {
        s::processors::registerBuiltins();
        s::markers::registerBuiltins();
        s::bases::detail::registerPieceTypes();
        if (!mHooked) {
            mHooked = true;
            game::db::onWorldReady([] {
                s::data::reload();
                s::blockentities::load();
            });
            game::db::onWorldSave([] { s::blockentities::save(); });
            game::db::onWorldClosed([] {
                s::outlines::shutdown();
                s::spawners::clear();
                s::blocks::clearCaches();
                s::blockentities::load();
            });
        }
        if (game::db::available()) {
            s::data::reload();
            s::blockentities::load();
        }
        // one tick listener for the kit: queued block entity work, spawner mob sizes and the dev
        // command outlines (which return at once while nothing is displayed)
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            s::blockentities::flush();
            s::spawners::tick();
            s::outlines::tick();
        });
        s::spawners::install();
        s::blockentities::install();
    }
    void uninstall() {
        if (mTick) ll::event::EventBus::getInstance().removeListener(mTick);
        mTick.reset();
        s::spawners::uninstall();
        s::blockentities::uninstall();
        s::outlines::shutdown();
        s::blockentities::save();
    }

private:
    bool                         mHooked = false;
    ll::event::ListenerPtr       mTick;
};

} // namespace gf::feature::structure

GF_REGISTER_MODULE_ORDERED(gf::feature::structure::StructureModule, gf::ModuleOrder::Contracts);
