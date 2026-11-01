// The game's own chest screen for gf::inventory containers on vanilla clients (spike S3's
// container-entity route, docs/spikes/S3.md).
//
// A vanilla client shows a container screen only for a container it knows: a vanilla block actor
// or an entity with a container. Custom blocks have neither on the client (a data-driven block
// actor carries no container there), so a 27-slot container opens through a proxy: an invisible,
// inert vanilla chest minecart spawned inside the block, titled with the container's title. While
// the screen is open the proxy and the storage (BlockData or the injected native container) are
// synced both ways every tick (a slot that changed on one side since the last sync is copied to
// the other), so the game's own item transactions, shift-clicks and drag-splitting work and other
// code (hoppers, the client mod's screens, units) sees the same slots. When the last viewer
// closes the screen (or leaves) the slots are synced once more and the proxy is emptied and
// removed. Proxies carry a tag: one that outlived its session (a crash between an autosave and the
// close) is removed empty by the sweep, the storage kept every synced change.

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/entity/components_json_legacy/ContainerComponent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/server/commands/CommandUtils.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/provider/SynchedActorDataAccess.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/inventory/FillingContainer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"

#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/inventory/InventoryInternal.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::inventory {

namespace {

constexpr char kProxyTag[]    = "gf_inventory_proxy";
constexpr int  kProxySlots    = 27; ///< a chest minecart's container
constexpr int  kOpenDelay     = 3;  ///< ticks: the client must have the proxy before its screen opens
constexpr int  kSweepInterval = 100;

struct Viewer {
    std::uint64_t                         openAt = 0;
    bool                                  opened = false;
    std::weak_ptr<ContainerManagerModel>  manager; ///< the screen the game opened for the proxy
};

struct Session {
    std::int64_t                     proxy = 0; ///< ActorUniqueID of the chest minecart
    std::vector<ItemStack>           snapshot;  ///< the slots as last synced
    std::map<std::int64_t, Viewer>   viewers;   ///< player ActorUniqueID -> viewer
};

std::map<GlobalPos, Session>& sessions() {
    static std::map<GlobalPos, Session> m;
    return m;
}

std::uint64_t nowTick() {
    auto level = ll::service::getLevel();
    return level ? static_cast<std::uint64_t>(level->getCurrentTick().tickID) : 0;
}

Actor* fetch(std::int64_t raw) {
    auto level = ll::service::getLevel();
    if (!level || !raw) return nullptr;
    ActorUniqueID id;
    id.rawID = raw;
    return level->fetchEntity(id, false);
}

Player* playerOf(std::int64_t raw) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID id;
    id.rawID = raw;
    return level->getPlayer(id);
}

FillingContainer* proxyContainer(Actor& proxy) {
    auto comp = proxy.getEntityContext().tryGetComponent<ContainerComponent>();
    if (!comp) return nullptr;
    return &comp->mContainer.get();
}

bool same(ItemStack const& a, ItemStack const& b) {
    bool ea = game::items::isEmpty(a), eb = game::items::isEmpty(b);
    if (ea || eb) return ea == eb;
    return game::items::sameItemSameData(a, b) && game::items::countOf(a) == game::items::countOf(b);
}

constexpr int kStorageReadInterval = 5; ///< ticks between reads of the storage (other code's changes)

/// Two-way sync of the proxy and the storage; false when the session cannot go on. The proxy is
/// compared every tick (cheap); the storage is read every kStorageReadInterval ticks or when
/// `readStorage` (the last sync).
bool sync(GlobalPos const& pos, Session& s, bool readStorage) {
    auto* region = game::regionOf(pos.dim);
    auto* proxy  = fetch(s.proxy);
    if (!region || !proxy) return false;
    auto h = find(*region, game::blockPos(pos));
    auto* c = proxyContainer(*proxy);
    if (!h || h->slots != kProxySlots || !c) return false;
    s.snapshot.resize(kProxySlots);
    std::vector<ItemStack> backend;
    if (readStorage) {
        backend = contents(pos);
        backend.resize(kProxySlots);
    }
    for (int i = 0; i < kProxySlots; ++i) {
        auto const& p    = c->getItem(i);
        auto&       snap = s.snapshot[static_cast<std::size_t>(i)];
        if (!same(p, snap)) { // the player changed it in the screen
            h->set(pos, i, p);
            snap = p;
        } else if (readStorage && !same(backend[static_cast<std::size_t>(i)], snap)) { // other code changed the storage
            c->setItem(i, backend[static_cast<std::size_t>(i)]);
            snap = backend[static_cast<std::size_t>(i)];
        }
    }
    return true;
}

/// Ends a session: the last sync, then the proxy goes empty (nothing can drop from it).
void finish(GlobalPos const& pos, Session& s) {
    (void)sync(pos, s, true);
    if (auto* proxy = fetch(s.proxy)) {
        if (auto* c = proxyContainer(*proxy))
            for (int i = 0; i < c->getContainerSize(); ++i) c->setItem(i, ItemStack{});
        proxy->remove();
    }
    log().info("inventory: vanilla container screen at {} {} {} closed", pos.x, pos.y, pos.z);
}

bool isProxy(Actor const& a) { return a.isType(ActorType::MinecartChest) && a.hasTag(kProxyTag); }

void sweep(Level& level) {
    std::set<std::int64_t> live;
    for (auto const& [_, s] : sessions()) live.insert(s.proxy);
    for (auto* a : level.getRuntimeActorList()) {
        if (!a || !isProxy(*a) || live.contains(a->getOrCreateUniqueID().rawID)) continue;
        if (auto* c = proxyContainer(*a))
            for (int i = 0; i < c->getContainerSize(); ++i) c->setItem(i, ItemStack{});
        a->remove();
        log().info("inventory: removed a stale container proxy");
    }
}

void onTick() {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto now = nowTick();
    for (auto it = sessions().begin(); it != sessions().end();) {
        auto& [pos, s] = *it;
        auto* proxy    = fetch(s.proxy);
        for (auto v = s.viewers.begin(); v != s.viewers.end();) {
            auto* p = playerOf(v->first);
            if (!p || !proxy) {
                v = s.viewers.erase(v);
                continue;
            }
            auto& viewer = v->second;
            if (!viewer.opened) {
                if (now < viewer.openAt) {
                    ++v;
                    continue;
                }
                proxy->openContainerComponent(*p);
                viewer.opened  = true;
                viewer.manager = p->mContainerManager;
                log().info("inventory: {} opened the container at {} {} {} (vanilla screen)", p->getRealName(), pos.x, pos.y, pos.z);
                ++v;
                continue;
            }
            auto open = viewer.manager.lock();
            if (!open || p->mContainerManager.get() != open.get()) {
                v = s.viewers.erase(v); // closed (Esc, another screen) or never shown
                continue;
            }
            ++v;
        }
        if (s.viewers.empty() || !sync(pos, s, now % kStorageReadInterval == 0)) {
            finish(pos, s);
            it = sessions().erase(it);
            continue;
        }
        ++it;
    }
    if (now % kSweepInterval == 0) sweep(*level);
}

void closeAll() {
    for (auto& [pos, s] : sessions()) finish(pos, s);
    sessions().clear();
}

class VanillaContainerModule {
public:
    void install() {
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { onTick(); });
        static bool registered = false;
        if (registered) return;
        registered = true;
        // the proxy cannot be broken, hurt or opened by anyone else
        combat::onIncoming(combat::priority::Highest, [](combat::DamageInfo& d) {
            return isProxy(d.victim) ? combat::Verdict::Cancel : combat::Verdict::Pass;
        });
        namespace ia = interactions;
        ia::onInteract({.block = "minecraft:chest_minecart"}, [](ia::EntityContext const& c) {
            return isProxy(c.entity) ? ia::Result::Deny : ia::Result::Pass;
        }, ia::priority::Highest);
        ia::onAttack({.block = "minecraft:chest_minecart"}, [](ia::EntityContext const& c) {
            return isProxy(c.entity) ? ia::Result::Deny : ia::Result::Pass;
        }, ia::priority::Highest);
        game::db::onWorldSave([] {
            for (auto& [pos, s] : sessions()) (void)sync(pos, s, true);
        });
        game::db::onWorldClosed([] { closeAll(); });
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mTick);
        closeAll();
    }

private:
    ll::event::ListenerPtr mTick;
};

} // namespace

bool openVanillaContainer(Player& player, GlobalPos const& pos, std::string const& title) {
    auto* region = game::regionOf(pos.dim);
    if (!region || player.isClientSide()) return false;
    auto bp = game::blockPos(pos);
    auto h  = find(*region, bp);
    if (!h || h->slots != kProxySlots) return false;
    // the client must have the proxy's chunk: only next to the player (Java's stillValid, 8
    // blocks); farther away the caller falls back to the form
    if (static_cast<int>(player.getDimensionId()) != pos.dim) return false;
    auto const& here = player.getPosition();
    float       dx = here.x - (pos.x + .5f), dy = here.y - (pos.y + .5f), dz = here.z - (pos.z + .5f);
    if (dx * dx + dy * dy + dz * dz > 64.f) return false;
    auto uid = player.getOrCreateUniqueID().rawID;
    if (auto it = sessions().find(pos); it != sessions().end() && fetch(it->second.proxy)) {
        it->second.viewers[uid] = Viewer{nowTick() + 1};
        return true;
    }
    ActorUniqueID id;
    Vec3          at{static_cast<float>(pos.x) + .5f, static_cast<float>(pos.y), static_cast<float>(pos.z) + .5f};
    auto*         proxy = CommandUtils::spawnEntityAt(*region, at, ActorDefinitionIdentifier("minecraft:chest_minecart"), id, nullptr);
    if (!proxy) return false;
    auto* c = proxyContainer(*proxy);
    if (!c || c->getContainerSize() < kProxySlots) {
        proxy->remove();
        return false;
    }
    proxy->addTag(kProxyTag);
    proxy->setInvisible(true);
    SynchedActorDataAccess::setActorFlag(proxy->getEntityContext(), ActorFlags::HasGravity, false);
    SynchedActorDataAccess::setActorFlag(proxy->getEntityContext(), ActorFlags::HasCollision, false);
    SynchedActorDataAccess::setActorFlag(proxy->getEntityContext(), ActorFlags::Noai, true);
    // the screen title is the entity's name (the container's custom name is not shown); the name
    // itself is never drawn above the hidden proxy
    proxy->setNameTag(title);
    SynchedActorDataAccess::setActorFlag(proxy->getEntityContext(), ActorFlags::CanShowName, false);
    SynchedActorDataAccess::setActorFlag(proxy->getEntityContext(), ActorFlags::AlwaysShowName, false);
    if (auto comp = proxy->getEntityContext().tryGetComponent<ContainerComponent>()) comp->setCustomName(title);
    Session s;
    s.proxy    = proxy->getOrCreateUniqueID().rawID;
    s.snapshot = contents(pos);
    s.snapshot.resize(kProxySlots);
    for (int i = 0; i < kProxySlots; ++i) c->setItem(i, s.snapshot[static_cast<std::size_t>(i)]);
    s.viewers[uid] = Viewer{nowTick() + kOpenDelay};
    sessions()[pos] = std::move(s);
    return true;
}

int vanillaProxyCount() { return static_cast<int>(sessions().size()); }

Container* vanillaProxyContainer(GlobalPos const& pos) {
    auto it = sessions().find(pos);
    if (it == sessions().end()) return nullptr;
    auto* proxy = fetch(it->second.proxy);
    return proxy ? proxyContainer(*proxy) : nullptr;
}

} // namespace gf::inventory

GF_REGISTER_MODULE_ORDERED(gf::inventory::VanillaContainerModule, gf::ModuleOrder::Contracts);
