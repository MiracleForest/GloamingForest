// The game's own chest screen for gf::inventory containers on vanilla clients (spike S3's
// container-entity route, docs/spikes/S3.md).
//
// A vanilla client shows a container screen only for a container it knows: a vanilla block actor
// or an entity with a container. Custom blocks have neither on the client (a data-driven block
// actor carries no container there), so a container opens through a proxy: an invisible, inert
// vanilla chest minecart spawned inside the block (with no hitbox, so other players still reach the
// block), titled with the container's title.
//   - A 27-slot container: the minecart's own chest screen.
//   - A double chest (two 27-slot halves, ChestBlock#combine: the right half first): the game's
//     large chest screen of 54 slots. The minecart's container is made 54 slots on the server
//     (FillingContainer#clearInventory(54)); the viewer's client gets a vanilla chest pair of its
//     own (UpdateBlockPacket + BlockActorDataPacket with pairx / pairz / pairlead, only to that
//     player) a few blocks under the halves, in the ground, and the server's ContainerOpenPacket
//     for the minecart is sent as one for that client-side chest (type Container at its position):
//     the client shows its large chest screen, the server keeps the minecart's container behind it
//     (the same item requests, LevelEntity container slots 0..53). When the screen closes the
//     client gets the real blocks back.
//
// While the screen is open there is one copy of the slots: the storage (BlockData or the injected
// native container, per half). The proxy mirrors it:
//   - the screen's changes (the game's item transactions, shift-clicks, drag-splitting) are
//     written to the storage as they happen (a content change listener on the proxy's container);
//   - every write through a handler from gf::inventory::find (the client mod's screen, the forms,
//     units) and every write to the injected native container itself (game code that sets its
//     slots: the hoppers, hopper minecarts, droppers and crafters F11's hooks let reach it,
//     src/feature/families/Hoppers.cpp) reaches the proxy at once (proxyMirror / proxyPull); a
//     BlockData container is out of their reach (no game container). Anything that changes the
//     proxy's own container (the screen, a hopper reaching the minecart) goes through the listener;
//   - code that changes the storage behind the handler (a unit's own get / set storage written
//     directly) is pulled in right before each item request of a viewer's screen (the client's
//     ItemStackRequestPacket) and every tick, and a slot changed on both sides keeps the storage's
//     stack (the proxy slot is reset; the listener checks it too). A Handler with its own get / set
//     should still write through find()'s handler while a screen may be open.
// So no screen shows a stack the storage no longer has, and nothing can be taken twice. When the
// block (either half) goes (broken, exploded, replaced) the session ends at once without touching
// the storage: the storage's copy drops, the proxy is emptied and removed. When the last viewer
// closes the screen (or leaves) the proxy is emptied and removed. Proxies carry a tag: one that
// outlived its session (a crash between an autosave and the close) is removed empty by the sweep.

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/deps/vanilla_components/AABBShapeComponent.h"
#include "mc/entity/components_json_legacy/ContainerComponent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/NetworkIdentifier.h"
#include "mc/network/Packet.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/network/packet/BlockActorDataPacket.h"
#include "mc/network/packet/ContainerClosePacket.h"
#include "mc/network/packet/ContainerOpenPacket.h"
#include "mc/network/packet/ItemStackRequestPacket.h"
#include "mc/network/packet/UpdateBlockPacket.h"
#include "mc/server/ServerPlayer.h"
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

/// The minecart's ContainerOpenPacket for a double chest's viewer, sent as one for its
/// client-side chest pair (set around Actor::openContainerComponent).
struct OpenRewrite {
    Player const* player = nullptr;
    BlockPos      pos;
    bool          done = false;
};
thread_local OpenRewrite* tOpenRewrite = nullptr;

/// Players (ActorUniqueID) whose client shows a client-side chest pair's screen now.
std::set<std::int64_t>& fakeScreens() {
    static std::set<std::int64_t> s;
    return s;
}

} // namespace

/// The minecart's ContainerOpenPacket of a double chest's viewer (see above).
void rewriteProxyPacket(Player const& player, Packet& packet) {
    if (tOpenRewrite && tOpenRewrite->player == &player && packet.getId() == MinecraftPacketIds::ContainerOpen) {
        auto& open           = static_cast<ContainerOpenPacket&>(packet);
        open.mType           = SharedTypes::Legacy::ContainerType::Container;
        open.mPos            = tOpenRewrite->pos;
        open.mEntityUniqueID = ActorUniqueID::INVALID_ID();
        tOpenRewrite->done   = true;
    }
}

/// A viewer's client closed the large chest screen of its chest pair: the server closes the
/// minecart's (the type the client names is the chest's).
void proxyClientClosed(Player& player, ContainerClosePacket& packet) {
    if (!fakeScreens().contains(player.getOrCreateUniqueID().rawID)) return;
    if (auto* manager = player.mContainerManager.get(); manager && packet.mContainerId == manager->getContainerId())
        packet.mContainerType = manager->getContainerType();
}

bool proxyFakeScreensOpen() { return !fakeScreens().empty(); }

} // namespace gf::inventory

// A client's item request (every take, place, swap, drop or craft of its open container screen):
// a proxy the player is looking at first takes in what the storage changed behind its handler
// since the last tick, so the request works on the storage's slots (a stale stack fails the
// request's own check and the client is resynced) and nothing is taken twice.
LL_TYPE_INSTANCE_HOOK(
    InventoryProxyRequestHook,
    ll::memory::HookPriority::Normal,
    ServerNetworkHandler,
    &ServerNetworkHandler::$handle,
    void,
    NetworkIdentifier const&      source,
    ItemStackRequestPacket const& packet
) {
    if (gf::inventory::vanillaProxyCount() > 0) {
        if (auto* player = thisFor<NetEventCallback>()->_getServerPlayer(source, packet.mSenderSubId))
            gf::inventory::proxyBeforeScreenRequest(*player);
    }
    origin(source, packet);
}

// A double chest's viewer closed its large chest screen (see proxyClientClosed).
LL_TYPE_INSTANCE_HOOK(
    InventoryProxyCloseHook,
    ll::memory::HookPriority::Normal,
    ServerNetworkHandler,
    &ServerNetworkHandler::$handle,
    void,
    NetworkIdentifier const&    source,
    ContainerClosePacket const& packet
) {
    if (gf::inventory::proxyFakeScreensOpen()) {
        if (auto* player = thisFor<NetEventCallback>()->_getServerPlayer(source, packet.mSenderSubId))
            gf::inventory::proxyClientClosed(*player, const_cast<ContainerClosePacket&>(packet));
    }
    origin(source, packet);
}

// The minecart's ContainerOpenPacket for a double chest's viewer (see rewriteProxyPacket).
LL_TYPE_INSTANCE_HOOK(
    InventoryProxyPacketHook,
    ll::memory::HookPriority::Normal,
    ServerPlayer,
    &ServerPlayer::$sendNetworkPacket,
    void,
    Packet& packet
) {
    gf::inventory::rewriteProxyPacket(*this, packet);
    origin(packet);
}

namespace gf::inventory {

namespace {

constexpr char kProxyTag[]    = "gf_inventory_proxy";
constexpr int  kHalfSlots     = 27; ///< a chest minecart's container, one chest (half)
constexpr int  kOpenDelay     = 3;  ///< ticks: the client must have the proxy (and its chest pair) before its screen opens
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

/// A double chest's client-side vanilla chest pair (the large chest screen opens on it).
struct FakePair {
    BlockPos lead;  ///< pairlead: the screen's first 27 slots (the right half's)
    BlockPos other;
};

class ProxyListener;

struct Session {
    std::int64_t                   proxy = 0; ///< ActorUniqueID of the chest minecart
    std::vector<GlobalPos>         parts;     ///< the storage positions in slot order, 27 slots each
    std::vector<ItemStack>         snapshot;  ///< the slots as both sides last agreed
    std::map<std::int64_t, Viewer> viewers;   ///< player ActorUniqueID -> viewer
    std::unique_ptr<ProxyListener> listener;  ///< on the proxy's container
    std::optional<FakePair>        fake;      ///< a double chest: the viewers' client-side chest pair
    std::string                    title;
    [[nodiscard]] int              slots() const { return kHalfSlots * static_cast<int>(parts.size()); }
};

/// Sessions by their first part (the right half of a double chest).
std::map<GlobalPos, Session>& sessions() {
    static std::map<GlobalPos, Session> m;
    return m;
}

/// The session showing `pos` (any of its parts) and the slot offset of `pos` in it.
std::pair<std::map<GlobalPos, Session>::iterator, int> locate(GlobalPos const& pos) {
    for (auto it = sessions().begin(); it != sessions().end(); ++it)
        for (std::size_t k = 0; k < it->second.parts.size(); ++k)
            if (it->second.parts[k] == pos) return {it, static_cast<int>(k) * kHalfSlots};
    return {sessions().end(), 0};
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

/// The open session `key` (its first part) with its live proxy container (nullptr: none).
FillingContainer* sessionContainer(GlobalPos const& key, Session** out = nullptr) {
    auto it = sessions().find(key);
    if (it == sessions().end()) return nullptr;
    auto* proxy = fetch(it->second.proxy);
    auto* c     = proxy ? proxyContainer(*proxy) : nullptr;
    if (c && c->getContainerSize() < it->second.slots()) return nullptr;
    if (c && out) *out = &it->second;
    return c;
}

/// The handler of the block at `part` as registered (the storage itself), 27 slots.
std::optional<Handler> storageAt(GlobalPos const& part) {
    auto* region = game::regionOf(part.dim);
    if (!region) return std::nullopt;
    auto h = rawHandlerFor(game::blockIdAt(*region, game::blockPos(part)));
    if (!h || h->slots != kHalfSlots) return std::nullopt;
    return h;
}

/// Every slot of the session's storage in screen order (nullopt: a part is gone).
std::optional<std::vector<ItemStack>> storageSlots(Session const& s) {
    std::vector<ItemStack> out;
    for (auto const& part : s.parts) {
        if (!storageAt(part)) return std::nullopt;
        auto slots = contents(part);
        slots.resize(kHalfSlots);
        out.insert(out.end(), slots.begin(), slots.end());
    }
    return out;
}

/// The screen changed slot `slot` of the proxy (a player's item transaction): the storage takes it
/// at once, unless the storage's slot itself changed since both sides last agreed (code that wrote
/// the storage behind the handler, not pulled in yet): then the storage wins, as in reconcile, and
/// the proxy slot is reset to it.
void onProxyChanged(GlobalPos const& key, int slot) {
    if (tSyncing) return;
    Session* s = nullptr;
    auto*    c = sessionContainer(key, &s);
    if (!c || !s || slot < 0 || slot >= s->slots()) return;
    auto const& part  = s->parts[static_cast<std::size_t>(slot / kHalfSlots)];
    int         local = slot % kHalfSlots;
    auto        h     = storageAt(part);
    if (!h) return;
    SyncScope guard;
    s->snapshot.resize(static_cast<std::size_t>(s->slots()));
    auto&     snap   = s->snapshot[static_cast<std::size_t>(slot)];
    ItemStack now    = c->getItem(slot);
    auto      stored = h->get(part, local);
    if (!same(stored, snap)) {
        if (!same(now, stored)) c->setItem(slot, stored);
        snap = stored;
        log().warn(
            "inventory: slot {} of the container at {} {} {} changed behind its handler and in the vanilla screen at once: the storage's {} x{} stays",
            local,
            part.x,
            part.y,
            part.z,
            game::items::idOf(stored),
            game::items::countOf(stored)
        );
        return;
    }
    h->set(part, local, now);
    // what the storage holds now (a handler may keep something else): both sides show it, so the
    // next change of this slot is not taken for a conflict and the tick does not write it again
    auto after = h->get(part, local);
    if (!same(after, now)) c->setItem(slot, after);
    snap = after;
    log().debug("inventory: the vanilla screen changed slot {} of the container at {} {} {}", local, part.x, part.y, part.z);
}

class ProxyListener final : public ContainerContentChangeListener {
public:
    explicit ProxyListener(GlobalPos key) : mKey(key) {}
    void containerContentChanged(int slot) override { onProxyChanged(mKey, slot); }

private:
    GlobalPos mKey;
};

/// Every tick: the storage is read and compared with the proxy and the last agreed slots. A slot
/// only the proxy changed (a change no listener reported) goes to the storage; one the storage
/// changed (or both) is copied to the proxy: the storage wins. False when the session cannot go on.
bool reconcile(Session& s) {
    auto* proxy   = fetch(s.proxy);
    auto* c       = proxy ? proxyContainer(*proxy) : nullptr;
    auto  storage = storageSlots(s);
    int   n       = s.slots();
    if (!c || !storage || c->getContainerSize() < n) return false;
    s.snapshot.resize(static_cast<std::size_t>(n));
    SyncScope guard;
    for (int i = 0; i < n; ++i) {
        auto const& p    = c->getItem(i);
        auto const& st   = (*storage)[static_cast<std::size_t>(i)];
        auto&       snap = s.snapshot[static_cast<std::size_t>(i)];
        if (same(p, st)) {
            snap = st;
            continue;
        }
        if (!same(p, snap) && same(st, snap)) {
            auto const& part = s.parts[static_cast<std::size_t>(i / kHalfSlots)];
            if (auto h = storageAt(part)) h->set(part, i % kHalfSlots, p); // the screen changed it
            snap = p;
        } else {
            c->setItem(i, st); // the storage changed it (or both did: the storage wins)
            snap = st;
        }
    }
    return true;
}

// ---- a double chest's client-side chest pair ------------------------------------------------------

/// Sends `player` the block `runtimeId` at `pos` (only its client changes).
void sendBlock(Player& player, BlockPos const& pos, unsigned runtimeId) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::UpdateBlock);
    if (!packet) return;
    auto& pkt        = static_cast<UpdateBlockPacket&>(*packet);
    pkt.mPos         = pos;
    pkt.mLayer       = 0;
    pkt.mUpdateFlags = 0b1011; // neighbours | network | priority
    pkt.mRuntimeId   = runtimeId;
    pkt.sendTo(player);
}

std::string snbtString(std::string const& text) {
    std::string out = "\"";
    for (char ch : text) {
        if (ch == '"' || ch == '\\') out += '\\';
        out += ch;
    }
    return out + "\"";
}

/// Where a double chest's screen opens on the client: two positions 2 (else 3, 4) blocks under the
/// halves, in solid ground first (out of sight; else the first usable ones), never where the
/// server has a block actor (its client-side state could not be put back by a block alone).
/// nullopt when there is none.
std::optional<FakePair> fakePairFor(BlockSource& region, std::vector<GlobalPos> const& parts) {
    if (parts.size() != 2) return std::nullopt;
    std::optional<FakePair> usable;
    for (int down : {2, 3, 4}) {
        BlockPos a{parts[0].x, parts[0].y - down, parts[0].z}, b{parts[1].x, parts[1].y - down, parts[1].z};
        if (a.y < region.getMinHeight() || b.y < region.getMinHeight()) break;
        if (!region.hasChunksAt(a, 0, false) || !region.hasChunksAt(b, 0, false)) continue;
        if (region.getBlockEntity(a) || region.getBlockEntity(b)) continue;
        auto hidden = [&](BlockPos const& p) {
            auto const& block = region.getBlock(p);
            return !block.isAir() && block._isSolid();
        };
        if (hidden(a) && hidden(b)) return FakePair{a, b};
        if (!usable) usable = FakePair{a, b};
    }
    return usable;
}

/// Gives `player`'s client the vanilla chest pair of `f`, facing as the halves, named `title`.
void sendFake(Player& player, FakePair const& f, std::string const& facing, std::string const& title) {
    auto const* chest = game::blockWithStates("minecraft:chest", {{"minecraft:cardinal_direction", facing}});
    if (!chest) chest = game::blockWith("minecraft:chest");
    if (!chest) return;
    sendBlock(player, f.lead, chest->mNetworkId);
    sendBlock(player, f.other, chest->mNetworkId);
    for (bool lead : {true, false}) {
        auto const& at   = lead ? f.lead : f.other;
        auto const& pair = lead ? f.other : f.lead;
        auto        snbt = "{id:\"Chest\",x:" + std::to_string(at.x) + ",y:" + std::to_string(at.y) + ",z:" + std::to_string(at.z)
                + ",isMovable:1b,Findable:0b,pairlead:" + (lead ? "1b" : "0b") + ",pairx:" + std::to_string(pair.x)
                + ",pairz:" + std::to_string(pair.z) + ",CustomName:" + snbtString(title) + "}";
        auto tag = CompoundTag::fromSnbt(snbt);
        if (!tag) {
            log().error("inventory: the chest pair's block actor data did not parse: {}", snbt);
            continue;
        }
        auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::BlockActorData);
        if (!packet) continue;
        auto& pkt = static_cast<BlockActorDataPacket&>(*packet);
        pkt.mPos  = at;
        pkt.mData = std::move(*tag);
        pkt.sendTo(player);
    }
}

/// Gives `player`'s client the real blocks of `f` back.
void restoreFake(Player& player, GlobalPos const& key, FakePair const& f) {
    fakeScreens().erase(player.getOrCreateUniqueID().rawID);
    auto* region = game::regionOf(key.dim);
    if (!region || static_cast<int>(player.getDimensionId()) != key.dim) return;
    for (auto const& at : {f.lead, f.other})
        if (region->hasChunksAt(at, 0, false)) sendBlock(player, at, region->getBlock(at).mNetworkId);
}

/// Ends a session: the proxy stops reporting, goes empty (nothing can drop from it) and is
/// removed; the viewers' clients get the real blocks of a chest pair back. `reconcileFirst`: a
/// normal close (the storage is still there).
void finish(GlobalPos const& key, Session& s, bool reconcileFirst) {
    if (reconcileFirst) (void)reconcile(s);
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
    if (s.fake)
        for (auto const& [uid, _] : s.viewers) {
            fakeScreens().erase(uid); // also a viewer that is gone
            if (auto* p = playerOf(uid)) restoreFake(*p, key, *s.fake);
        }
    log().info("inventory: vanilla container screen at {} {} {} closed", key.x, key.y, key.z);
}

bool isProxy(Actor const& a) { return a.isType(ActorType::MinecartChest) && a.hasTag(kProxyTag); }

/// The container position of a proxy entity (nullopt: not a live session's proxy).
std::optional<GlobalPos> sessionOf(Actor& proxy) {
    auto raw = proxy.getOrCreateUniqueID().rawID;
    for (auto const& [key, s] : sessions())
        if (s.proxy == raw) return key;
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

/// Opens the proxy's screen for a viewer: a double chest's on the viewer's client-side chest pair.
bool openFor(Actor& proxy, Player& p, Session const& s) {
    if (!s.fake) {
        proxy.openContainerComponent(p);
        return true;
    }
    OpenRewrite rewrite{&p, s.fake->lead};
    tOpenRewrite = &rewrite;
    proxy.openContainerComponent(p);
    tOpenRewrite = nullptr;
    if (!rewrite.done) {
        log().error("inventory: the large chest screen of {} did not go out (no ContainerOpenPacket)", p.getRealName());
        if (p.mContainerManager.get()) static_cast<ServerPlayer&>(p).doDeleteContainerManager(false);
        return false;
    }
    fakeScreens().insert(p.getOrCreateUniqueID().rawID);
    return true;
}

void onTick() {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto                   now = nowTick();
    std::vector<GlobalPos> changed;
    for (auto it = sessions().begin(); it != sessions().end();) {
        auto& [key, s] = *it;
        auto* proxy    = fetch(s.proxy);
        auto  before   = s.viewers.size();
        for (auto v = s.viewers.begin(); v != s.viewers.end();) {
            auto* p = playerOf(v->first);
            if (!p || !proxy) {
                if (p && s.fake) restoreFake(*p, key, *s.fake);
                if (!p) fakeScreens().erase(v->first);
                v = s.viewers.erase(v);
                continue;
            }
            auto& viewer = v->second;
            if (!viewer.opened) {
                if (now < viewer.openAt) {
                    ++v;
                    continue;
                }
                if (!openFor(*proxy, *p, s)) {
                    if (s.fake) restoreFake(*p, key, *s.fake);
                    v = s.viewers.erase(v);
                    continue;
                }
                viewer.opened  = true;
                viewer.manager = p->mContainerManager;
                log().info(
                    "inventory: {} opened the container at {} {} {} (vanilla screen{})",
                    p->getRealName(),
                    key.x,
                    key.y,
                    key.z,
                    s.fake ? ", large chest" : ""
                );
                ++v;
                continue;
            }
            auto open = viewer.manager.lock();
            if (!open || p->mContainerManager.get() != open.get()) {
                if (s.fake) restoreFake(*p, key, *s.fake);
                v = s.viewers.erase(v); // closed (Esc, another screen) or never shown
                continue;
            }
            ++v;
        }
        if (s.viewers.size() != before) changed.insert(changed.end(), s.parts.begin(), s.parts.end());
        if (s.viewers.empty() || !reconcile(s)) {
            finish(key, s, s.viewers.empty()); // the last viewer left; or the storage is gone
            changed.insert(changed.end(), s.parts.begin(), s.parts.end());
            it = sessions().erase(it);
            continue;
        }
        ++it;
    }
    for (auto const& pos : changed) recountViewers(pos);
    if (now % kSweepInterval == 0) sweep(*level);
}

void closeAll() {
    for (auto& [key, s] : sessions()) finish(key, s, true);
    sessions().clear();
    fakeScreens().clear();
}

class VanillaContainerModule {
public:
    void install() {
        InventoryProxyRequestHook::hook();
        InventoryProxyCloseHook::hook();
        InventoryProxyPacketHook::hook();
        auto& bus = ll::event::EventBus::getInstance();
        mTick     = bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { onTick(); });
        // the block of an open session changes (broken, exploded, replaced, a unit's own storage):
        // the session ends at once, before any later click can reach the proxy
        mBlock = bus.emplaceListener<ll::event::BlockChangedEvent>([](ll::event::BlockChangedEvent& ev) {
            if (sessions().empty() || ev.layer() != 0) return;
            auto& region = ev.blockSource();
            if (region.getILevel().isClientSide() || &ev.previousBlock().getBlockType() == &ev.newBlock().getBlockType()) return;
            auto pos = game::globalPos(region, ev.pos());
            if (proxyOpen(pos)) proxyEnd(pos);
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
                // joins the session (or the client mod's screen), as a use of the block would
                if (h && h->open && h->open(c.player, *pos)) chestOpenedBy(c.player, *pos);
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
        InventoryProxyPacketHook::unhook();
        InventoryProxyCloseHook::unhook();
        InventoryProxyRequestHook::unhook();
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
    // the parts the screen shows (a double chest: the right half first), 27 slots each
    auto parts = partsOf(pos);
    if (parts.empty() || parts.size() > 2) return false;
    for (auto const& part : parts)
        if (!storageAt(part)) return false;
    // the client must have the proxy's chunk: only within the reach every screen route uses
    // (Java's stillValid); farther away the caller falls back to the form
    if (!withinReach(player, pos)) return false;
    auto uid = player.getOrCreateUniqueID().rawID;
    auto key = parts.front();
    if (auto it = sessions().find(key); it != sessions().end() && fetch(it->second.proxy)) {
        auto& s       = it->second;
        auto& viewers = s.viewers;
        auto  v       = viewers.find(uid);
        // a viewer whose screen already closed (Esc, then a use again before the next tick) opens
        // anew: the tick would otherwise drop it as closed and end the session
        bool closed = false;
        if (v != viewers.end() && v->second.opened) {
            auto open = v->second.manager.lock();
            closed    = !open || player.mContainerManager.get() != open.get();
        }
        if (v == viewers.end() || closed) {
            if (s.fake) sendFake(player, *s.fake, chestFacingOf(key), s.title);
            viewers[uid] = Viewer{nowTick() + (s.fake ? kOpenDelay : 1)};
        }
        return true;
    }
    // a double chest: its large chest screen opens on a chest pair of the viewer's client
    std::optional<FakePair> fake;
    if (parts.size() == 2) {
        fake = fakePairFor(*region, parts);
        if (!fake) return false;
    }
    ActorUniqueID id;
    Vec3          at{static_cast<float>(key.x) + .5f, static_cast<float>(key.y), static_cast<float>(key.z) + .5f};
    auto*         proxy = CommandUtils::spawnEntityAt(*region, at, ActorDefinitionIdentifier("minecraft:chest_minecart"), id, nullptr);
    if (!proxy) return false;
    auto* c     = proxyContainer(*proxy);
    int   slots = kHalfSlots * static_cast<int>(parts.size());
    // a double chest: the minecart's container takes both halves (the server's side of the screen)
    if (c && c->getContainerSize() < slots) (void)c->clearInventory(slots);
    if (!c || c->getContainerSize() < slots) {
        log().warn("inventory: no {}-slot container proxy (got {})", slots, c ? c->getContainerSize() : 0);
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
    // itself is never drawn above the hidden proxy. A double chest's title is its chest pair's name.
    proxy->setNameTag(title);
    SynchedActorDataAccess::setActorFlag(ctx, ActorFlags::CanShowName, false);
    SynchedActorDataAccess::setActorFlag(ctx, ActorFlags::AlwaysShowName, false);
    if (auto comp = ctx.tryGetComponent<ContainerComponent>()) comp->setCustomName(title);
    Session s;
    s.proxy = proxy->getOrCreateUniqueID().rawID;
    s.parts = parts;
    s.fake  = fake;
    s.title = title;
    if (auto storage = storageSlots(s)) s.snapshot = std::move(*storage);
    s.snapshot.resize(static_cast<std::size_t>(slots));
    {
        SyncScope guard;
        for (int i = 0; i < slots; ++i) c->setItem(i, s.snapshot[static_cast<std::size_t>(i)]);
    }
    s.listener = std::make_unique<ProxyListener>(key);
    c->addContentChangeListener(s.listener.get());
    if (fake) sendFake(player, *fake, chestFacingOf(key), title);
    s.viewers[uid]  = Viewer{nowTick() + kOpenDelay};
    sessions()[key] = std::move(s);
    return true;
}

int vanillaProxyCount() { return static_cast<int>(sessions().size()); }

Container* vanillaProxyContainer(GlobalPos const& pos) {
    auto [it, offset] = locate(pos);
    return it == sessions().end() ? nullptr : sessionContainer(it->first);
}

Actor* vanillaProxyActor(GlobalPos const& pos) {
    auto [it, offset] = locate(pos);
    return it == sessions().end() ? nullptr : fetch(it->second.proxy);
}

std::optional<BlockPos> vanillaProxyChestPair(GlobalPos const& pos) {
    auto [it, offset] = locate(pos);
    if (it == sessions().end() || !it->second.fake) return std::nullopt;
    return it->second.fake->lead;
}

bool proxyOpen(GlobalPos const& pos) { return !sessions().empty() && locate(pos).first != sessions().end(); }

void proxyMirror(GlobalPos const& pos, int slot, ItemStack const& stack) {
    if (tSyncing || slot < 0 || slot >= kHalfSlots) return;
    auto [it, offset] = locate(pos);
    if (it == sessions().end()) return;
    Session* s = nullptr;
    auto*    c = sessionContainer(it->first, &s);
    if (!c || !s) return;
    SyncScope guard;
    // the storage's own form of the stack (a handler or the item JSON may keep it slightly
    // differently): the listener compares the storage with this snapshot
    auto h      = storageAt(pos);
    auto stored = h ? h->get(pos, slot) : stack;
    int  i      = offset + slot;
    if (!same(c->getItem(i), stored)) c->setItem(i, stored);
    s->snapshot.resize(static_cast<std::size_t>(s->slots()));
    s->snapshot[static_cast<std::size_t>(i)] = stored;
}

void proxyPull(GlobalPos const& pos) {
    if (tSyncing || !proxyOpen(pos)) return;
    auto [it, offset] = locate(pos);
    if (it == sessions().end()) return;
    auto key     = it->first;
    auto storage = storageSlots(it->second); // may unpack a pending table (which pulls on its own)
    if (!storage) return;
    Session* s = nullptr;
    auto*    c = sessionContainer(key, &s);
    if (!c || !s) return;
    SyncScope guard;
    int n = s->slots();
    storage->resize(static_cast<std::size_t>(n));
    s->snapshot.resize(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        auto const& st = (*storage)[static_cast<std::size_t>(i)];
        if (!same(c->getItem(i), st)) c->setItem(i, st);
        s->snapshot[static_cast<std::size_t>(i)] = st;
    }
}

void proxyEnd(GlobalPos const& pos) {
    auto [it, offset] = locate(pos);
    if (it == sessions().end()) return;
    auto parts = it->second.parts;
    finish(it->first, it->second, false);
    sessions().erase(it);
    for (auto const& p : parts) recountViewers(p);
}

void proxyBeforeScreenRequest(Player& player) {
    if (sessions().empty()) return;
    auto uid = player.getOrCreateUniqueID().rawID;
    for (auto& [key, s] : sessions())
        if (s.viewers.contains(uid)) (void)reconcile(s); // a session that cannot go on ends on the tick
}

std::set<std::int64_t> proxyViewers(GlobalPos const& pos) {
    std::set<std::int64_t> out;
    auto [it, offset] = locate(pos);
    if (it == sessions().end()) return out;
    for (auto const& [uid, _] : it->second.viewers) out.insert(uid);
    return out;
}

} // namespace gf::inventory

GF_REGISTER_MODULE_ORDERED(gf::inventory::VanillaContainerModule, gf::ModuleOrder::Contracts);
