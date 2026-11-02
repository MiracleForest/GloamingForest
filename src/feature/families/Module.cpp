// Block families (F11): the module that wires the family behaviours, the block event dispatch and
// the sign text backend into the game, plus the climbable block types (BlockProperty::Climbable;
// the client mod does the same for the client's own block types, src/client/families).

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/level/block/BlockType.h"

#include "feature/families/Families.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::families {

namespace detail {
void installBlockEvents();
void uninstallBlockEvents();
void tickBlockEvents();
void resetBlockEvents();
void runQueues();
void clearQueues();
void clearDoors();
void chestsTick(int tick);
void clearChests();
void signsTick(int tick);
void clearSigns();
void refreshTypes();
} // namespace detail

void applyClimbable() {
    int count = 0;
    for (auto const* e : entries()) {
        if (!e->has(Climbable)) continue;
        auto type = BlockType::tryGetFromRegistry(HashedString(e->id));
        if (!type) continue;
        auto& t = const_cast<BlockType&>(*type);
        t.mProperties = static_cast<BlockProperty>(static_cast<std::uint64_t>(t.mProperties) | static_cast<std::uint64_t>(BlockProperty::Climbable));
        ++count;
    }
    if (count) log().info("families: {} climbable block types", count);
}

namespace {

class FamiliesModule {
public:
    void load() { loadRegistry(); }

    void install() {
        if (entries().empty()) loadRegistry();
        detail::installBlockEvents();
        installPlacement();
        installShapes();
        installDoors();
        installSwitches();
        installPlants();
        installChests();
        installSigns();
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([this](auto&) {
            ++mTicks;
            detail::runQueues();
            detail::tickBlockEvents();
            detail::chestsTick(mTicks);
            detail::signsTick(mTicks);
        });
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldReady([] {
                detail::refreshTypes();
                detail::resetBlockEvents();
                applyClimbable();
            });
            game::db::onWorldClosed([] {
                detail::clearQueues();
                detail::clearDoors();
                detail::clearChests();
                detail::clearSigns();
                detail::resetBlockEvents();
            });
        }
        if (game::db::available()) {
            // BDS: the world exists already when mods are enabled
            detail::refreshTypes();
            applyClimbable();
        }
        log().info("families: installed ({} family blocks)", entries().size());
    }

    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mTick);
        uninstallSigns();
        uninstallChests();
        uninstallPlants();
        uninstallSwitches();
        uninstallDoors();
        uninstallShapes();
        uninstallPlacement();
        detail::uninstallBlockEvents();
        detail::clearQueues();
    }

private:
    ll::event::ListenerPtr mTick;
    int                    mTicks = 0;
};

} // namespace

} // namespace gf::feature::families

GF_REGISTER_MODULE_ORDERED(gf::feature::families::FamiliesModule, ::gf::ModuleOrder::Contracts);
