// Armour trim materials of units (ext/item/Item.h, spike S6): appended to the level's trim registry
// as soon as it is initialised, so the TrimDataPacket every joining player gets lists them.
#include <algorithm>
#include <mutex>
#include <vector>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/item/registry/TrimMaterial.h"
#include "mc/world/item/registry/TrimMaterialRegistry.h"
#include "mc/world/level/Level.h"

#include "feature/ext/item/Item.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::item::ext {

namespace {

struct Store {
    std::mutex                    mutex;
    std::vector<TrimMaterialSpec> specs;
};

Store& store() {
    static Store s;
    return s;
}

/// Appends the registered materials the registry lacks (a copy of its first material with the
/// spec's id, colour and item: TrimMaterial has no exported assignment, only its copy constructor).
void appendTo(TrimMaterialRegistry& registry) {
    std::vector<TrimMaterialSpec> specs;
    {
        auto&           s = store();
        std::lock_guard lock(s.mutex);
        specs = s.specs;
    }
    auto& list = registry.mTrimMaterials.get();
    if (list.empty() || specs.empty()) return;
    int added = 0;
    for (auto const& spec : specs) {
        bool known = std::any_of(list.begin(), list.end(), [&](TrimMaterial const& m) { return m.mMaterialId->getString() == spec.id; });
        if (known) continue;
        TrimMaterial m(list.front());
        m.mMaterialId = HashedString(spec.id);
        m.mColor      = spec.color;
        m.mItemName   = HashedString(spec.itemId);
        list.push_back(m);
        ++added;
    }
    if (added > 0) log().info("item: {} trim materials appended to the level's registry", added);
}

void appendToLevel() {
    auto level = ll::service::getLevel();
    if (!level) return;
    if (auto registry = level->getTrimMaterialRegistry().lock()) appendTo(*registry);
}

} // namespace

void addTrimMaterial(TrimMaterialSpec const& spec) {
    {
        auto&           s = store();
        std::lock_guard lock(s.mutex);
        std::erase_if(s.specs, [&](TrimMaterialSpec const& e) { return e.id == spec.id; });
        s.specs.push_back(spec);
    }
    appendToLevel(); // a world already running: players who join from now on get it
}

bool hasTrimMaterial(std::string const& materialId) {
    auto level = ll::service::getLevel();
    if (!level) return false;
    auto registry = level->getTrimMaterialRegistry().lock();
    if (!registry) return false;
    auto const& list = registry->mTrimMaterials.get();
    return std::any_of(list.begin(), list.end(), [&](TrimMaterial const& m) { return m.mMaterialId->getString() == materialId; });
}

} // namespace gf::item::ext

// The level's trim registry is filled (a world starts): the units' materials join it before any
// player can join.
LL_TYPE_INSTANCE_HOOK(ItemTrimInitHook, ll::memory::HookPriority::Normal, TrimMaterialRegistry, &TrimMaterialRegistry::initializeServer, void) {
    origin();
    try {
        gf::item::ext::appendTo(*this);
    } catch (std::exception const& e) {
        gf::log().error("item: appending trim materials failed: {}", e.what());
    }
}

namespace gf::item::ext {
namespace {
class TrimsModule {
public:
    void install() {
        ItemTrimInitHook::hook();
        static bool registered = false;
        if (!registered) {
            registered = true;
            // BDS enables mods after the world exists: append at world ready (before players join)
            game::db::onWorldReady([] { appendToLevel(); });
        }
    }
    void uninstall() { ItemTrimInitHook::unhook(); }
};
} // namespace
} // namespace gf::item::ext

GF_REGISTER_MODULE_ORDERED(gf::item::ext::TrimsModule, gf::ModuleOrder::Contracts);
