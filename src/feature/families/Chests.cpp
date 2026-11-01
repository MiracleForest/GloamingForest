// Block families (F11): chests and trapped chests (Java ChestBlock / TrappedChestBlock /
// ChestBlockEntity): facing, double chests (type single / left / right) on placement and when a
// neighbour changes, the storage and the screen, the players looking inside, the lid and the
// trapped chest's signal.
//
// Storage: each half keeps its own 27 slots (block data "chest": {items, loot}); the inventory
// contract's handler of a double chest is Java's CompoundContainer (DoubleBlockCombiner): slots
// 0..26 are the first half's (the RIGHT one), 27..53 the second's, from either half. A loot table
// (gf::loot::placeContainer, setLootTable) rolls into the half on its first access (Java
// RandomizableContainer: LootTable.fill). A broken half drops its own slots (Containers
// .dropContents).
// Screen: "families.chest" (gf::screens): the client mod's native chest screen (Java's ChestMenu:
// 3 or 6 rows over the player's inventory), a form for vanilla clients. The players inside are
// counted per half (Java ContainerOpenersCounter, checked every 5 ticks: gone, too far, other
// dimension): twilightforest:open_count opens the lid, plays the chest sounds and is the trapped
// chest's signal (its BP permutations: redstone_producer 0..15).

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/common/BlockData.h"
#include "feature/common/Interactions.h"
#include "feature/common/Screens.h"
#include "feature/families/BlockEvents.h"
#include "feature/families/Families.h"
#include "feature/inventory/Inventory.h"
#include "feature/loot/Loot.h"
#include "feature/net/Net.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::families {

namespace ia = interactions;
namespace bd = blockdata;

namespace {

constexpr char kKey[]    = "chest";
constexpr char kScreen[] = "families.chest";
constexpr int  kSlots    = 27;

std::vector<ia::HandlerId>& handlerIds() {
    static std::vector<ia::HandlerId> ids;
    return ids;
}

bool isChest(Entry const* e) { return e && (e->family == Family::Chest || e->family == Family::TrappedChest); }

std::string typeOf(Block const& b) {
    auto t = strState(b, state::Type);
    return t.empty() ? "single" : t;
}

/// Java ChestBlock.getConnectedDirection.
int connectedDirection(Block const& b) {
    int f = facingOf(b);
    return typeOf(b) == "left" ? clockwise(f) : counterClockwise(f);
}

/// Java ChestBlock.candidatePartnerFacing: the facing of a single chest of the same kind there.
std::optional<int> candidatePartnerFacing(BlockSource& region, BlockPos const& pos, std::string const& id, int dir) {
    Block const* b = blockAt(region, offset(pos, dir));
    if (!b || b->getTypeName() != id || typeOf(*b) != "single") return std::nullopt;
    return facingOf(*b);
}

/// Java ChestBlock.updateShape for the neighbour on `dir`.
void updateChest(BlockSource& region, BlockPos const& pos, int dir) {
    Block const* b = blockAt(region, pos);
    if (!b || !isChest(entryOf(*b))) return;
    Block const* n    = blockAt(region, offset(pos, dir));
    Json         s    = Json::object();
    bool         same = n && n->getTypeName() == b->getTypeName();
    if (same && horizontal(dir)) {
        auto nt = typeOf(*n);
        if (typeOf(*b) == "single" && nt != "single" && facingOf(*b) == facingOf(*n) && connectedDirection(*n) == opposite(dir)) {
            s[state::Type] = nt == "left" ? "right" : "left";
        }
    } else if (typeOf(*b) != "single" && connectedDirection(*b) == dir) {
        s[state::Type] = "single";
    }
    if (s.empty()) return;
    if (auto const* next = withStates(*b, s); next && next != b) game::setBlock(region, pos, *next);
}

/// Java ChestBlock.isBlockedChestByBlock: a redstone conductor (solid block) on top.
bool blocked(BlockSource& region, BlockPos const& pos) { return solidBlock(region, offset(pos, Up)); }

// ---- storage ----------------------------------------------------------------------------------------

Json storageOf(GlobalPos const& pos) {
    auto* region = regionOf(pos.dim);
    if (!region) return Json::object();
    auto const* j = bd::get(*region, game::blockPos(pos), kKey);
    return j && j->is_object() ? *j : Json::object();
}

void writeStorage(GlobalPos const& pos, Json const& data) {
    if (auto* region = regionOf(pos.dim)) bd::set(*region, game::blockPos(pos), kKey, data);
}

/// Java LootTable.fill: the rolled stacks shuffled into the empty slots, bigger stacks split to
/// fill them (shuffleAndSplitItems).
void fillLoot(Json& data, std::vector<ItemStack> stacks, std::int64_t seed) {
    std::mt19937_64 random(static_cast<std::uint64_t>(seed == 0 ? std::random_device{}() : seed));
    auto nextInt = [&](int lo, int hi) { return hi <= lo ? lo : lo + static_cast<int>(random() % static_cast<std::uint64_t>(hi - lo + 1)); };
    auto items   = data.value("items", Json::array());
    while (static_cast<int>(items.size()) < kSlots) items.push_back(nullptr);
    std::vector<int> slots;
    for (int i = 0; i < kSlots; ++i)
        if (items[i].is_null()) slots.push_back(i);
    std::shuffle(slots.begin(), slots.end(), random);
    std::vector<ItemStack> rest, big;
    for (auto& s : stacks) {
        if (game::items::isEmpty(s)) continue;
        (game::items::countOf(s) > 1 ? big : rest).push_back(s);
    }
    while (static_cast<int>(slots.size()) - static_cast<int>(rest.size()) - static_cast<int>(big.size()) > 0 && !big.empty()) {
        int       idx   = nextInt(0, static_cast<int>(big.size()) - 1);
        ItemStack stack = big[idx];
        big.erase(big.begin() + idx);
        int       n     = nextInt(1, game::items::countOf(stack) / 2);
        ItemStack part  = game::items::withCount(stack, n);
        stack           = game::items::withCount(stack, game::items::countOf(stack) - n);
        ((game::items::countOf(stack) > 1 && (random() & 1)) ? big : rest).push_back(stack);
        ((game::items::countOf(part) > 1 && (random() & 1)) ? big : rest).push_back(part);
    }
    for (auto& s : big) rest.push_back(s);
    std::shuffle(rest.begin(), rest.end(), random);
    for (auto const& s : rest) {
        if (slots.empty()) break;
        items[slots.back()] = game::items::toJson(s);
        slots.pop_back();
    }
    data["items"] = items;
}

/// Java RandomizableContainer.unpackLootTable: the half's pending loot table, on first access.
void unpackLoot(GlobalPos const& pos, Player* player) {
    auto data = storageOf(pos);
    if (!data.contains("loot") || !data["loot"].is_object()) return;
    auto table = data["loot"].value("table", std::string{});
    auto seed  = data["loot"].value("seed", std::int64_t{0});
    data.erase("loot");
    auto* region = regionOf(pos.dim);
    if (!region || table.empty()) {
        writeStorage(pos, data);
        return;
    }
    loot::LootContext ctx;
    ctx.region = region;
    ctx.origin = Vec3{pos.x + .5f, pos.y + .5f, pos.z + .5f};
    ctx.player = player;
    ctx.thisEntity = player;
    ctx.seed       = seed;
    fillLoot(data, loot::roll(table, ctx), seed);
    writeStorage(pos, data);
    log().info("families: the chest at {} {} {} rolled {}", pos.x, pos.y, pos.z, table);
}

ItemStack slotOf(GlobalPos const& half, int slot) {
    unpackLoot(half, nullptr);
    auto items = storageOf(half).value("items", Json::array());
    return slot >= 0 && slot < static_cast<int>(items.size()) && !items[slot].is_null() ? game::items::fromJson(items[slot]) : ItemStack{};
}

void setSlotOf(GlobalPos const& half, int slot, ItemStack const& stack) {
    if (slot < 0 || slot >= kSlots) return;
    unpackLoot(half, nullptr);
    auto data  = storageOf(half);
    auto items = data.value("items", Json::array());
    while (static_cast<int>(items.size()) < kSlots) items.push_back(nullptr);
    items[slot]   = game::items::isEmpty(stack) ? Json(nullptr) : game::items::toJson(stack);
    data["items"] = items;
    writeStorage(half, data);
}

/// The halves of the chest at `pos`, first (Java: the RIGHT half) first.
std::vector<GlobalPos> halvesOf(GlobalPos const& pos) {
    auto* region = regionOf(pos.dim);
    if (!region) return {pos};
    BlockPos     bp = game::blockPos(pos);
    Block const* b  = blockAt(*region, bp);
    auto         p  = chestPartner(*region, bp);
    if (!b || !p) return {pos};
    auto other = game::globalPos(pos.dim, *p);
    return typeOf(*b) == "right" ? std::vector<GlobalPos>{pos, other} : std::vector<GlobalPos>{other, pos};
}

/// CompoundContainer: slot -> (half, slot in the half).
std::optional<std::pair<GlobalPos, int>> resolve(GlobalPos const& pos, int slot) {
    auto halves = halvesOf(pos);
    if (slot < 0 || slot >= kSlots * static_cast<int>(halves.size())) return std::nullopt;
    return std::make_pair(halves[slot / kSlots], slot % kSlots);
}

ItemStack getSlot(GlobalPos const& pos, int slot) {
    auto r = resolve(pos, slot);
    return r ? slotOf(r->first, r->second) : ItemStack{};
}

void setSlot(GlobalPos const& pos, int slot, ItemStack const& stack) {
    if (auto r = resolve(pos, slot)) setSlotOf(r->first, r->second, stack);
}

int sizeOf(GlobalPos const& pos) { return kSlots * static_cast<int>(halvesOf(pos).size()); }

/// Java ItemHandlerHelper order: matching stacks first, then empty slots.
ItemStack insertInto(GlobalPos const& pos, ItemStack const& stack, bool simulate) {
    ItemStack rest = stack;
    int       size = sizeOf(pos);
    for (int pass = 0; pass < 2 && !game::items::isEmpty(rest); ++pass) {
        for (int i = 0; i < size && !game::items::isEmpty(rest); ++i) {
            auto cur = getSlot(pos, i);
            int  max = std::min(64, static_cast<int>(rest.getMaxStackSize()));
            if (pass == 0 && !game::items::isEmpty(cur) && game::items::sameItemSameData(cur, rest)) {
                int move = std::min(max - game::items::countOf(cur), game::items::countOf(rest));
                if (move <= 0) continue;
                if (!simulate) setSlot(pos, i, game::items::withCount(cur, game::items::countOf(cur) + move));
                rest = game::items::withCount(rest, game::items::countOf(rest) - move);
            } else if (pass == 1 && game::items::isEmpty(cur)) {
                int move = std::min(max, game::items::countOf(rest));
                if (!simulate) setSlot(pos, i, game::items::withCount(rest, move));
                rest = game::items::withCount(rest, game::items::countOf(rest) - move);
            }
        }
    }
    return rest;
}

// ---- the players inside (ContainerOpenersCounter) -------------------------------------------------------

struct Viewer {
    GlobalPos              opened;
    std::vector<GlobalPos> halves;
    ItemStack              carried; ///< the native screen's cursor stack
    bool                   native = false;
};
std::map<std::int64_t, Viewer>& viewers() {
    static std::map<std::int64_t, Viewer> m;
    return m;
}

Player* playerById(std::int64_t uid) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID id;
    id.rawID = uid;
    auto* a  = level->fetchEntity(id, false);
    return a && a->isPlayer() ? static_cast<Player*>(a) : nullptr;
}

/// Container.stillValidBlockEntity: the block interaction range plus 4 from the player's eyes.
bool inReach(Player& player, GlobalPos const& pos) {
    if (static_cast<int>(player.getDimensionId()) != pos.dim) return false;
    Vec3  eye = player.getEyePos();
    float dx  = std::max({float(pos.x) - eye.x, 0.f, eye.x - (pos.x + 1.f)});
    float dy  = std::max({float(pos.y) - eye.y, 0.f, eye.y - (pos.y + 1.f)});
    float dz  = std::max({float(pos.z) - eye.z, 0.f, eye.z - (pos.z + 1.f)});
    float r   = (player.isCreative() ? 5.0f : 4.5f) + 4.0f;
    return dx * dx + dy * dy + dz * dz < r * r;
}

int countAt(GlobalPos const& half) {
    int n = 0;
    for (auto const& [uid, v] : viewers())
        if (std::any_of(v.halves.begin(), v.halves.end(), [&half](GlobalPos const& h) { return h == half; })) ++n;
    return n;
}

void soundAt(BlockSource& region, Vec3 const& at, char const* name) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!packet) return;
    auto& pkt   = static_cast<PlaySoundPacket&>(*packet);
    pkt.mName   = name;
    pkt.mPos    = BlockPos{static_cast<int>(std::floor(at.x * 8)), static_cast<int>(std::floor(at.y * 8)), static_cast<int>(std::floor(at.z * 8))};
    pkt.mVolume = 0.5f;
    pkt.mPitch  = wobblePitch();
    pkt.sendTo(BlockPos{static_cast<int>(std::floor(at.x)), static_cast<int>(std::floor(at.y)), static_cast<int>(std::floor(at.z))}, region.getDimensionId());
}

/// ContainerOpenersCounter.openerCountChanged for a half: the state (lid, signal) and, from the
/// first / RIGHT half, the open / close sound (ChestBlockEntity.playSound, centred on a double).
void countChanged(GlobalPos const& half) {
    auto* region = regionOf(half.dim);
    if (!region) return;
    BlockPos     bp = game::blockPos(half);
    Block const* b  = blockAt(*region, bp);
    if (!b || !isChest(entryOf(*b))) return;
    int now = std::min(15, countAt(half));
    int was = intState(*b, state::OpenCount).value_or(0);
    if (now == was) return;
    setStates(*region, bp, {{state::OpenCount, now}});
    if ((was == 0) != (now == 0) && typeOf(*b) != "left") {
        Vec3 at{bp.x + .5f, bp.y + .5f, bp.z + .5f};
        if (typeOf(*b) == "right") {
            int d = connectedDirection(*b);
            at.x += kDx[d] * .5f;
            at.z += kDz[d] * .5f;
        }
        soundAt(*region, at, now > 0 ? "random.chestopen" : "random.chestclosed");
    }
}

/// AbstractContainerMenu.removed: the cursor stack goes back to the player (dropped at the chest
/// when the player is gone already).
void returnCarried(std::int64_t uid, Viewer const& v) {
    if (game::items::isEmpty(v.carried)) return;
    if (auto* p = playerById(uid)) {
        give(*p, v.carried);
    } else if (auto* region = regionOf(v.opened.dim)) {
        drop(*region, game::blockPos(v.opened), v.carried);
    }
}

void removeViewer(std::int64_t uid) {
    auto it = viewers().find(uid);
    if (it == viewers().end()) return;
    Viewer v = std::move(it->second);
    viewers().erase(it);
    returnCarried(uid, v);
    for (auto const& h : v.halves) countChanged(h);
}

ll::event::ListenerPtr& disconnectListener() {
    static ll::event::ListenerPtr l;
    return l;
}

void addViewer(Player& player, GlobalPos const& opened, bool native) {
    auto uid = player.getOrCreateUniqueID().rawID;
    removeViewer(uid); // one container at a time
    auto& v  = viewers()[uid];
    v.opened = opened;
    v.halves = halvesOf(opened);
    v.native = native;
    for (auto const& h : v.halves) countChanged(h);
}

/// ContainerOpenersCounter.recheckOpeners (every 5 ticks while open).
void recheck() {
    std::vector<std::int64_t> gone;
    for (auto const& [uid, v] : viewers()) {
        auto* p = playerById(uid);
        bool  ok = p && p->isAlive();
        for (auto const& h : v.halves) {
            auto const* b = blockAt(h);
            if (!b || !isChest(entryOf(*b))) ok = false;
        }
        if (ok && !inReach(*p, v.opened)) ok = false;
        if (!ok) {
            if (p && v.native) screens::close(*p, kScreen);
            gone.push_back(uid);
        }
    }
    for (auto uid : gone) removeViewer(uid);
}

// ---- the screen -------------------------------------------------------------------------------------

GlobalPos posOf(Json const& ctx) {
    auto p = ctx.value("pos", Json::array());
    if (!p.is_array() || p.size() != 4) return {};
    return GlobalPos{p[0].get<int>(), p[1].get<int>(), p[2].get<int>(), p[3].get<int>()};
}

std::optional<Json> screenData(Player& player, Json const& ctx) {
    auto        pos = posOf(ctx);
    auto const* b   = blockAt(pos);
    if (!b || !isChest(entryOf(*b)) || !inReach(player, pos)) return std::nullopt;
    int  size  = sizeOf(pos);
    Json items = Json::array();
    for (int i = 0; i < size; ++i) {
        auto s = getSlot(pos, i);
        items.push_back(game::items::isEmpty(s) ? Json(nullptr) : game::items::toJson(s));
    }
    auto  it      = viewers().find(player.getOrCreateUniqueID().rawID);
    Json  carried = it != viewers().end() && !game::items::isEmpty(it->second.carried) ? game::items::toJson(it->second.carried) : Json(nullptr);
    bool  dbl     = size > kSlots;
    return Json{
        {"title", dbl ? trFor(player, "container.chestDouble", "Large Chest") : trFor(player, "container.chest", "Chest")},
        {"rows", dbl ? 6 : 3},
        {"items", items},
        {"carried", carried},
    };
}

/// Java ChestMenu clicks (AbstractContainerMenu.doClick: pickup / place / swap / split, quick move).
void screenAction(Player& player, Json const& ctx, Json const& action) {
    auto pos = posOf(ctx);
    auto it  = viewers().find(player.getOrCreateUniqueID().rawID);
    if (it == viewers().end()) return;
    auto& carried = it->second.carried;
    auto  op      = action.value("op", std::string{});
    int   slot    = action.value("slot", -1);
    bool  right   = action.value("button", std::string{"left"}) == "right";
    bool  shift   = action.value("shift", false);
    auto& inv     = player.getInventory();
    auto  click   = [&](ItemStack cur, auto const& put) {
        if (game::items::isEmpty(carried)) {
            if (game::items::isEmpty(cur)) return;
            int take = right ? (game::items::countOf(cur) + 1) / 2 : game::items::countOf(cur);
            carried  = game::items::withCount(cur, take);
            put(game::items::withCount(cur, game::items::countOf(cur) - take));
        } else if (game::items::isEmpty(cur)) {
            int n = right ? 1 : game::items::countOf(carried);
            put(game::items::withCount(carried, n));
            carried = game::items::withCount(carried, game::items::countOf(carried) - n);
        } else if (game::items::sameItemSameData(cur, carried)) {
            int room = static_cast<int>(cur.getMaxStackSize()) - game::items::countOf(cur);
            int n    = std::min(room, right ? 1 : game::items::countOf(carried));
            if (n <= 0) return;
            put(game::items::withCount(cur, game::items::countOf(cur) + n));
            carried = game::items::withCount(carried, game::items::countOf(carried) - n);
        } else {
            put(carried);
            carried = cur;
        }
    };
    if (op == "click" && slot >= 0 && slot < sizeOf(pos)) {
        auto cur = getSlot(pos, slot);
        if (shift) { // ChestMenu.quickMoveStack: into the player's inventory
            if (game::items::isEmpty(cur)) return;
            ItemStack moving = cur;
            if (player.add(moving) || game::items::isEmpty(moving)) setSlot(pos, slot, ItemStack{});
            else setSlot(pos, slot, moving);
        } else {
            click(cur, [&](ItemStack const& s) { setSlot(pos, slot, s); });
        }
    } else if (op == "inv_click" && slot >= 0 && slot < inv.getContainerSize()) {
        auto cur = inv.getItem(slot);
        if (shift) { // into the chest
            if (game::items::isEmpty(cur)) return;
            inv.setItem(slot, insertInto(pos, cur, false));
        } else {
            click(cur, [&](ItemStack const& s) { inv.setItem(slot, s); });
        }
    }
    player.sendInventory(false);
}

/// Vanilla clients: a form listing the stacks (a button takes one) and a button putting the held
/// stack in.
void screenForm(Player& player, Json const& ctx) {
    auto pos = posOf(ctx);
    auto data = screenData(player, ctx);
    if (!data) {
        removeViewer(player.getOrCreateUniqueID().rawID);
        return;
    }
    ll::form::SimpleForm form;
    form.setTitle((*data)["title"].get<std::string>());
    std::vector<int> slots;
    form.appendButton(trFor(player, "gf.families.chest.put", "Put the held stack in"));
    int size = sizeOf(pos);
    for (int i = 0; i < size; ++i) {
        auto s = getSlot(pos, i);
        if (game::items::isEmpty(s)) continue;
        slots.push_back(i);
        form.appendButton(game::items::displayName(s) + " x" + std::to_string(game::items::countOf(s)));
    }
    form.sendTo(player, [pos, slots, ctx](Player& p, int index, ll::form::FormCancelReason) {
        if (index < 0) { // closed: the player leaves the chest
            removeViewer(p.getOrCreateUniqueID().rawID);
            return;
        }
        if (!inReach(p, pos)) {
            removeViewer(p.getOrCreateUniqueID().rawID);
            return;
        }
        if (index == 0) {
            auto& held = const_cast<ItemStack&>(p.getSelectedItem());
            if (!game::items::isEmpty(held)) {
                held = insertInto(pos, held, false);
                p.sendInventory(false);
            }
        } else if (index - 1 < static_cast<int>(slots.size())) {
            int  slot  = slots[index - 1];
            auto taken = getSlot(pos, slot);
            setSlot(pos, slot, ItemStack{});
            ItemStack rest = taken;
            if (!p.add(rest) && !game::items::isEmpty(rest)) setSlot(pos, slot, rest);
            p.sendInventory(false);
        }
        screenForm(p, ctx);
    });
}

/// ChestBlock.useWithoutItem -> openMenu: the menu of the chest (both halves of a double).
bool openChest(Player& player, GlobalPos const& pos) {
    auto* region = regionOf(pos.dim);
    if (!region) return false;
    BlockPos     bp = game::blockPos(pos);
    Block const* b  = blockAt(*region, bp);
    if (!b || !isChest(entryOf(*b))) return false;
    auto partner = chestPartner(*region, bp);
    if (blocked(*region, bp) || (partner && blocked(*region, *partner))) return false; // isChestBlockedAt
    for (auto const& h : halvesOf(pos)) unpackLoot(h, &player);
    bool native = net::isCompanion(player);
    addViewer(player, pos, native);
    Json ctx{{"pos", {pos.dim, pos.x, pos.y, pos.z}}};
    if (!screens::open(player, kScreen, ctx)) {
        removeViewer(player.getOrCreateUniqueID().rawID);
        return false;
    }
    log().info("families: {} opened the chest at {} {} {} ({} slots)", player.getRealName(), pos.x, pos.y, pos.z, sizeOf(pos));
    return true;
}

} // namespace

std::optional<BlockPos> chestPartner(BlockSource& region, BlockPos const& pos) {
    Block const* b = blockAt(region, pos);
    if (!b || !isChest(entryOf(*b)) || typeOf(*b) == "single") return std::nullopt;
    BlockPos     p = offset(pos, connectedDirection(*b));
    Block const* o = blockAt(region, p);
    // DoubleBlockCombiner.combineWithNeigbour: the same block, the other type, the same facing
    if (!o || o->getTypeName() != b->getTypeName() || typeOf(*o) == "single" || typeOf(*o) == typeOf(*b) || facingOf(*o) != facingOf(*b)) return std::nullopt;
    return p;
}

void pairChest(BlockSource& region, BlockPos const& pos, int facing, bool sneaking, std::optional<int> clickedFace) {
    // Java ChestBlock.getStateForPlacement
    Block const* b = blockAt(region, pos);
    if (!b || !isChest(entryOf(*b))) return;
    std::string const& id   = b->getTypeName();
    std::string        type = "single";
    int                dir  = facing;
    if (clickedFace && horizontal(*clickedFace) && sneaking) {
        auto d = candidatePartnerFacing(region, pos, id, opposite(*clickedFace));
        if (d && axisOf(*d) != axisOf(*clickedFace)) {
            dir  = *d;
            type = counterClockwise(*d) == opposite(*clickedFace) ? "right" : "left";
        }
    }
    if (type == "single" && !sneaking) {
        if (auto d = candidatePartnerFacing(region, pos, id, clockwise(dir)); d && *d == dir) type = "left";
        else if (auto d2 = candidatePartnerFacing(region, pos, id, counterClockwise(dir)); d2 && *d2 == dir) type = "right";
    }
    if (auto const* next = withStates(*b, {{state::Facing, dirName(dir)}, {state::Type, type}}); next && next != b) game::setBlock(region, pos, *next);
}

namespace detail {
void placeChest(ia::PlaceContext const& c) {
    auto* region = regionOf(c.pos.dim);
    if (!region) return;
    // Java: the chest faces the player; sneaking against a chest's side joins that one
    pairChest(*region, game::blockPos(c.pos), opposite(lookDir(c.player)), c.sneaking, c.face);
    if (auto const* b = blockAt(*region, game::blockPos(c.pos))) log().info("families: {} placed at {} {} {} ({})", c.blockId, c.pos.x, c.pos.y, c.pos.z, typeOf(*b));
}
/// Selftests: a player starts / stops looking inside (the screen's open and close).
void chestViewer(Player& player, GlobalPos const& pos, bool open) {
    if (open) addViewer(player, pos, false);
    else removeViewer(player.getOrCreateUniqueID().rawID);
}
/// The compound container's slots (selftests: the inventory contract's handler of the kit).
int       chestSize(GlobalPos const& pos) { return sizeOf(pos); }
ItemStack chestGet(GlobalPos const& pos, int slot) { return getSlot(pos, slot); }
void      chestSet(GlobalPos const& pos, int slot, ItemStack const& stack) { setSlot(pos, slot, stack); }
void chestsTick(int tick) {
    if (tick % 5 == 0 && !viewers().empty()) recheck();
}
void clearChests() {
    // the world goes: cursor stacks back to the players still there
    for (auto const& [uid, v] : viewers())
        if (auto* p = playerById(uid); p && !game::items::isEmpty(v.carried)) give(*p, v.carried);
    viewers().clear();
}
} // namespace detail

void installChests() {
    for (auto family : {Family::Chest, Family::TrappedChest}) {
        for (auto const* e : entriesOf(family)) {
            ia::Match m;
            m.block = e->id;
            handlerIds().push_back(ia::onUseOn(m, [](ia::UseOnContext const& c) {
                if (c.sneaking && !c.itemId.empty()) return ia::Result::Pass;
                if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
                openChest(c.player, c.pos);
                return ia::Result::Consume;
            }));
        }
    }
    // a player leaving with a chest open: its cursor stack back to its inventory first
    disconnectListener() = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerDisconnectEvent>([](ll::event::PlayerDisconnectEvent& ev) {
        removeViewer(ev.self().getOrCreateUniqueID().rawID);
    });
    static bool registered = false;
    if (registered) return;
    registered = true;
    screens::registerType({
        .id     = kScreen,
        .data   = screenData,
        .action = screenAction,
        .form   = screenForm,
        .closed = [](Player& player, Json const&) { removeViewer(player.getOrCreateUniqueID().rawID); },
    });
    // Containers.dropContents: a broken half drops its own slots
    bd::onErase([](GlobalPos const& pos, Json const& doc, std::string const& previous) {
        if (previous.empty() || !doc.contains(kKey) || !isChest(entryOf(previous))) return;
        auto* region = regionOf(pos.dim);
        if (!region) return;
        BlockPos bp    = game::blockPos(pos);
        auto     items = doc[kKey].value("items", Json::array());
        for (auto const& j : items)
            if (!j.is_null()) drop(*region, bp, game::items::fromJson(j));
    });
    for (auto family : {Family::Chest, Family::TrappedChest}) {
        for (auto const* e : entriesOf(family)) {
            inventory::Handler h;
            h.slots   = kSlots;
            h.get     = [](GlobalPos const& pos, int slot) { return getSlot(pos, slot); };
            h.set     = [](GlobalPos const& pos, int slot, ItemStack const& stack) { setSlot(pos, slot, stack); };
            h.insert  = [](GlobalPos const& pos, ItemStack const& stack, bool simulate) { return insertInto(pos, stack, simulate); };
            h.extract = [](GlobalPos const& pos, int slot, int count, bool simulate) {
                auto cur = getSlot(pos, slot);
                if (game::items::isEmpty(cur) || count <= 0) return ItemStack{};
                int take = std::min(count, game::items::countOf(cur));
                if (!simulate) setSlot(pos, slot, game::items::withCount(cur, game::items::countOf(cur) - take));
                return game::items::withCount(cur, take);
            };
            h.setLootTable = [](GlobalPos const& pos, std::string const& table, std::int64_t seed) {
                auto data    = storageOf(pos);
                data["loot"] = {{"table", table}, {"seed", seed}};
                writeStorage(pos, data);
            };
            h.open = [](Player& player, GlobalPos const& pos) { return openChest(player, pos); };
            inventory::registerBlock(e->id, std::move(h));
            blockevents::onNeighborChanged(e->id, [](BlockSource& region, BlockPos const& pos, BlockPos const& from) {
                for (int d : kHorizontal) {
                    BlockPos n = offset(pos, d);
                    if (n.x == from.x && n.y == from.y && n.z == from.z) updateChest(region, pos, d);
                }
            });
        }
    }
}

void uninstallChests() {
    for (auto id : handlerIds()) ia::remove(id);
    handlerIds().clear();
    if (disconnectListener()) {
        ll::event::EventBus::getInstance().removeListener(disconnectListener());
        disconnectListener() = nullptr;
    }
    detail::clearChests();
}

} // namespace gf::feature::families
