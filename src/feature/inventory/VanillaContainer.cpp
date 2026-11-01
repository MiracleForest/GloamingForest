// The game's own chest screen for gf::inventory containers on vanilla clients (spike S3's
// container-entity route, docs/spikes/S3.md).
//
// A vanilla client shows a container screen only for a container it knows: a vanilla block actor
// or an entity with a container. Custom blocks have neither on the client (a data-driven block
// actor carries no container there), so a 27-slot container opens through a proxy: an invisible,
// inert vanilla chest minecart spawned inside the block (with no hitbox, so other players still
// reach the block), titled with the container's title.
//
// While the screen is open there is one copy of the slots: the storage (BlockData or the injected
// native container). The proxy mirrors it:
//   - the screen's changes (the game's item transactions, shift-clicks, drag-splitting) are
//     written to the storage as they happen (a content change listener on the proxy's container);
//   - every write through a handler from gf::inventory::find (the client mod's screen, the forms,
//     units, hoppers into the native container) reaches the proxy at once (proxyMirror / proxyPull);
//   - code that changes the storage behind the handler is pulled in every tick, and a slot changed
//     on both sides keeps the storage's stack (the proxy slot is reset).
// So no screen shows a stack the storage no longer has, and nothing can be taken twice. When the
// block goes (broken, exploded, replaced) the session ends at once without touching the storage:
// the storage's copy drops, the proxy is emptied and removed. When the last viewer closes the
// screen (or leaves) the proxy is emptied and removed. Proxies carry a tag: one that outlived its
// session (a crash between an autosave and the close) is removed empty by the sweep.

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/vanilla_components/AABBShapeComponent.h"
#include "mc/entity/components_json_legacy/ContainerComponent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/server/commands/CommandUtils.h"
#include "mc/world/ContainerContentChangeListener.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/BuiltInActorComponents.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/provider/SynchedActorDataAccess.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/inventory/FillingContainer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ILevel.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

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

/// > 0 while this file itself writes the proxy or the storage (no echo).
thread_local int tSyncing = 0;
struct SyncScope {
    SyncScope() { ++tSyncing; }
    ~SyncScope() { --tSyncing; }
    SyncScope(SyncScope const&)            = delete;
    SyncScope& operator=(SyncScope const&) = delete;
};

struct Viewer {
    std::uint64_t                        openAt = 0;
    bool                                 opened = false;
    std::weak_ptr<ContainerManagerModel> manager; ///< the screen the game opened for the proxy
};

class ProxyListener;

struct Session {
    std::int64_t                   proxy = 0; ///< ActorUniqueID of the chest minecart
    std::vector<ItemStack>         snapshot;  ///< the slots as both sides last agreed
    std::map<std::int64_t, Viewer> viewers;   ///< player ActorUniqueID -> viewer
    std::unique_ptr<ProxyListener> listener;  ///< on the proxy's container
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

/// The open session of `pos` with its live proxy container (nullptr: none).
FillingContainer* sessionContainer(GlobalPos const& pos, Session** out = nullptr) {
    auto it = sessions().find(pos);
    if (it == sessions().end()) return nullptr;
    auto* proxy = fetch(it->second.proxy);
    auto* c     = proxy ? proxyContainer(*proxy) : nullptr;
    if (c && out) *out = &it->second;
    return c;
}

/// The handler of the block at `pos` as registered (the storage itself), 27 slots.
std::optional<Handler> storageOf(GlobalPos const& pos) {
    auto* region = game::regionOf(pos.dim);
    if (!region) return std::nullopt;
    auto h = rawHandlerFor(game::blockIdAt(*region, game::blockPos(pos)));
    if (!h || h->slots != kProxySlots) return std::nullopt;
    return h;
}

/// The screen changed slot `slot` of the proxy (a player's item transaction): the storage takes it
/// at once.
void onProxyChanged(GlobalPos const& pos, int slot) {
    if (tSyncing || slot < 0 || slot >= kProxySlots) return;
    Session* s = nullptr;
    auto*    c = sessionContainer(pos, &s);
    auto     h = storageOf(pos);
    if (!c || !s || !h) return;
    SyncScope   guard;
    auto const& now = c->getItem(slot);
    h->set(pos, slot, now);
    s->snapshot[static_cast<std::size_t>(slot)] = now;
    log().debug("inventory: the vanilla screen changed slot {} of the container at {} {} {}", slot, pos.x, pos.y, pos.z);
}

class ProxyListener final : public ContainerContentChangeListener {
public:
    explicit ProxyListener(GlobalPos pos) : mPos(pos) {}
    void containerContentChanged(int slot) override { onProxyChanged(mPos, slot); }

private:
    GlobalPos mPos;
};

/// Every tick: the storage is read and compared with the proxy and the last agreed slots. A slot
/// only the proxy changed (a change no listener reported) goes to the storage; one the storage
/// changed (or both) is copied to the proxy: the storage wins. False when the session cannot go on.
bool reconcile(GlobalPos const& pos, Session& s) {
    auto* proxy = fetch(s.proxy);
    auto* c     = proxy ? proxyContainer(*proxy) : nullptr;
    auto  h     = storageOf(pos);
    if (!c || !h) return false;
    auto storage = contents(pos);
    storage.resize(kProxySlots);
    s.snapshot.resize(kProxySlots);
    SyncScope guard;
    for (int i = 0; i < kProxySlots; ++i) {
        auto const& p    = c->getItem(i);
        auto const& st   = storage[static_cast<std::size_t>(i)];
        auto&       snap = s.snapshot[static_cast<std::size_t>(i)];
        if (same(p, st)) {
            snap = st;
            continue;
        }
        if (!same(p, snap) && same(st, snap)) {
            h->set(pos, i, p); // the screen changed it
            snap = p;
        } else {
            c->setItem(i, st); // the storage changed it (or both did: the storage wins)
            snap = st;
        }
    }
    return true;
}

/// Ends a session: the proxy stops reporting, goes empty (nothing can drop from it) and is
/// removed. `reconcileFirst`: a normal close (the storage is still there).
void finish(GlobalPos const& pos, Session& s, bool reconcileFirst) {
    if (reconcileFirst) (void)reconcile(pos, s);
    // the listener leaves the container first, even when the proxy is being removed already (its
    // container may still report changes until it is gone); an actor that is gone has no container
    ActorUniqueID id;
    id.rawID   = s.proxy;
    auto level = ll::service::getLevel();
    if (auto* any = level ? level->fetchEntity(id, true) : nullptr) {
        if (auto* c = proxyContainer(*any)) {
            if (s.listener) c->removeContentChangeListener(s.listener.get());
            SyncScope guard;
            for (int i = 0; i < c->getContainerSize(); ++i) c->setItem(i, ItemStack{});
        }
        if (fetch(s.proxy)) any->remove();
    }
    s.listener.reset();
    log().info("inventory: vanilla container screen at {} {} {} closed", pos.x, pos.y, pos.z);
}

bool isProxy(Actor const& a) { return a.isType(ActorType::MinecartChest) && a.hasTag(kProxyTag); }

/// The container position of a proxy entity (nullopt: not a live session's proxy).
std::optional<GlobalPos> sessionOf(Actor& proxy) {
    auto raw = proxy.getOrCreateUniqueID().rawID;
    for (auto const& [pos, s] : sessions())
        if (s.proxy == raw) return pos;
    return std::nullopt;
}

void sweep(Level& level) {
    std::set<std::int64_t> live;
    for (auto const& [_, s] : sessions()) live.insert(s.proxy);
    for (auto* a : level.getRuntimeActorList()) {
        if (!a || !isProxy(*a) || live.contains(a->getOrCreateUniqueID().rawID)) continue;
        if (auto* c = proxyContainer(*a)) {
            SyncScope guard;
            for (int i = 0; i < c->getContainerSize(); ++i) c->setItem(i, ItemStack{});
        }
        a->remove();
        log().info("inventory: removed a stale container proxy");
    }
}

void onTick() {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto                   now = nowTick();
    std::vector<GlobalPos> changed;
    for (auto it = sessions().begin(); it != sessions().end();) {
        auto& [pos, s] = *it;
        auto* proxy    = fetch(s.proxy);
        auto  before   = s.viewers.size();
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
        if (s.viewers.size() != before) changed.push_back(pos);
        if (s.viewers.empty() || !reconcile(pos, s)) {
            finish(pos, s, s.viewers.empty()); // the last viewer left; or the storage is gone
            changed.push_back(pos);
            it = sessions().erase(it);
            continue;
        }
        ++it;
    }
    for (auto const& pos : changed) recountViewers(pos);
    if (now % kSweepInterval == 0) sweep(*level);
}

void closeAll() {
    for (auto& [pos, s] : sessions()) finish(pos, s, true);
    sessions().clear();
}

class VanillaContainerModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mTick     = bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { onTick(); });
        // the block of an open session changes (broken, exploded, replaced, a unit's own storage):
        // the session ends at once, before any later click can reach the proxy
        mBlock = bus.emplaceListener<ll::event::BlockChangedEvent>([](ll::event::BlockChangedEvent& ev) {
            if (sessions().empty() || ev.layer() != 0) return;
            auto& region = ev.blockSource();
            if (region.getILevel().isClientSide() || &ev.previousBlock().getBlockType() == &ev.newBlock().getBlockType()) return;
            auto pos = game::globalPos(region, ev.pos());
            if (sessions().contains(pos)) proxyEnd(pos);
        });
        static bool registered = false;
        if (registered) return;
        registered = true;
        // the proxy cannot be broken or hurt; using it opens the container it stands for
        combat::onIncoming(combat::priority::Highest, [](combat::DamageInfo& d) {
            return isProxy(d.victim) ? combat::Verdict::Cancel : combat::Verdict::Pass;
        });
        namespace ia = interactions;
        ia::onInteract({.block = "minecraft:chest_minecart"}, [](ia::EntityContext const& c) {
            if (!isProxy(c.entity)) return ia::Result::Pass;
            if (auto pos = sessionOf(c.entity)) {
                auto* region = game::regionOf(pos->dim);
                auto  h      = region ? find(*region, game::blockPos(*pos)) : std::nullopt;
                if (h && h->open) h->open(c.player, *pos); // joins the session (or the client mod's screen)
            }
            return ia::Result::Deny;
        }, ia::priority::Highest);
        ia::onAttack({.block = "minecraft:chest_minecart"}, [](ia::EntityContext const& c) {
            return isProxy(c.entity) ? ia::Result::Deny : ia::Result::Pass;
        }, ia::priority::Highest);
        game::db::onWorldClosed([] { closeAll(); });
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        bus.removeListener(mTick);
        bus.removeListener(mBlock);
        closeAll();
    }

private:
    ll::event::ListenerPtr mTick;
    ll::event::ListenerPtr mBlock;
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
        if (!it->second.viewers.contains(uid)) it->second.viewers[uid] = Viewer{nowTick() + 1};
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
    auto& ctx = proxy->getEntityContext();
    SynchedActorDataAccess::setActorFlag(ctx, ActorFlags::HasGravity, false);
    SynchedActorDataAccess::setActorFlag(ctx, ActorFlags::HasCollision, false);
    SynchedActorDataAccess::setActorFlag(ctx, ActorFlags::Noai, true);
    // no hitbox: other players' crosshair goes through to the block (they can still open, mine and
    // place against it)
    if (auto* shape = proxy->mBuiltInComponents->mAABBShapeComponent.get()) {
        shape->mBBDim = Vec2{0.f, 0.f};
        proxy->_refreshAABB();
    }
    SynchedActorDataAccess::setBoundingBoxScale(ctx, 0.f);
    // the screen title is the entity's name (the container's custom name is not shown); the name
    // itself is never drawn above the hidden proxy
    proxy->setNameTag(title);
    SynchedActorDataAccess::setActorFlag(ctx, ActorFlags::CanShowName, false);
    SynchedActorDataAccess::setActorFlag(ctx, ActorFlags::AlwaysShowName, false);
    if (auto comp = ctx.tryGetComponent<ContainerComponent>()) comp->setCustomName(title);
    Session s;
    s.proxy    = proxy->getOrCreateUniqueID().rawID;
    s.snapshot = contents(pos);
    s.snapshot.resize(kProxySlots);
    {
        SyncScope guard;
        for (int i = 0; i < kProxySlots; ++i) c->setItem(i, s.snapshot[static_cast<std::size_t>(i)]);
    }
    s.listener = std::make_unique<ProxyListener>(pos);
    c->addContentChangeListener(s.listener.get());
    s.viewers[uid]  = Viewer{nowTick() + kOpenDelay};
    sessions()[pos] = std::move(s);
    return true;
}

int vanillaProxyCount() { return static_cast<int>(sessions().size()); }

Container* vanillaProxyContainer(GlobalPos const& pos) { return sessionContainer(pos); }

Actor* vanillaProxyActor(GlobalPos const& pos) {
    auto it = sessions().find(pos);
    return it == sessions().end() ? nullptr : fetch(it->second.proxy);
}

bool proxyOpen(GlobalPos const& pos) { return !sessions().empty() && sessions().contains(pos); }

void proxyMirror(GlobalPos const& pos, int slot, ItemStack const& stack) {
    if (tSyncing || slot < 0 || slot >= kProxySlots) return;
    Session* s = nullptr;
    auto*    c = sessionContainer(pos, &s);
    if (!c || !s) return;
    SyncScope guard;
    if (!same(c->getItem(slot), stack)) c->setItem(slot, stack);
    s->snapshot.resize(kProxySlots);
    s->snapshot[static_cast<std::size_t>(slot)] = stack;
}

void proxyPull(GlobalPos const& pos) {
    if (tSyncing || !proxyOpen(pos)) return;
    auto storage = contents(pos); // may unpack a pending table (which pulls on its own)
    storage.resize(kProxySlots);
    Session* s = nullptr;
    auto*    c = sessionContainer(pos, &s);
    if (!c || !s) return;
    SyncScope guard;
    s->snapshot.resize(kProxySlots);
    for (int i = 0; i < kProxySlots; ++i) {
        auto const& st = storage[static_cast<std::size_t>(i)];
        if (!same(c->getItem(i), st)) c->setItem(i, st);
        s->snapshot[static_cast<std::size_t>(i)] = st;
    }
}

void proxyEnd(GlobalPos const& pos) {
    auto it = sessions().find(pos);
    if (it == sessions().end()) return;
    finish(pos, it->second, false);
    sessions().erase(it);
    recountViewers(pos);
}

std::set<std::int64_t> proxyViewers(GlobalPos const& pos) {
    std::set<std::int64_t> out;
    auto                   it = sessions().find(pos);
    if (it == sessions().end()) return out;
    for (auto const& [uid, _] : it->second.viewers) out.insert(uid);
    return out;
}

} // namespace gf::inventory

GF_REGISTER_MODULE_ORDERED(gf::inventory::VanillaContainerModule, gf::ModuleOrder::Contracts);
