// Contract "inventory" (F10; backend chosen by spike S3, docs/spikes/S3.md).
//
// Containers on custom blocks. Two backends behind one Handler:
//   - native: a block with the `minecraft:block_entity` component gets a data-driven block actor
//     (format_version 1.26.x); for a registered container block, this module gives that actor a
//     game Container (one ContainerBlockActorComponent::tryCreateFor hook), saved with the block
//     actor (its own NBT, "GfItems");
//   - BlockData: any other block keeps its slots in its block data ("inventory": {items, loot}).
// Both are shown with the client mod's chest screen ("inventory.chest"; the server keeps what each
// player opened and refuses screen data / actions for anything else) and, on vanilla clients,
// with the game's chest screen through a chest minecart proxy (27 slots, VanillaContainer.cpp)
// or a form (take a stack, put stacks in) for other sizes: the client does not show the game's
// screen for a custom block's own container (S3; openNativeScreen keeps the experiment).
// Chest-like containers can be declared as data, <mod>/data/gf/containers/<u>.json:
//     [{"block": "twilightforest:twilight_oak_chest", "rows": 3, "title": "container.chest", "skin": ""}]
// which registers the storage, opens it on use (not while sneaking with an item) and drops the
// contents when the block is broken. A pending loot table (placeContainer / setLootTable) rolls
// on the first access, with the opener (Java's RandomizableContainer#unpackLootTable).

#include "feature/inventory/Inventory.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/safety/RedactableString.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/ContainerID.h"
#include "mc/world/DataDrivenBlockContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/DataDrivenBlockActor.h"
#include "mc/world/level/block/actor/component/ContainerBlockActorComponent.h"
#include "mc/world/level/block/actor/component/IBlockActorComponent.h"

#include "feature/common/BlockData.h"
#include "feature/common/Interactions.h"
#include "feature/common/Screens.h"
#include "feature/inventory/InventoryInternal.h"
#include "feature/loot/LootInternal.h"
#include "feature/net/Net.h"
#include "feature/ui/Ui.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

namespace gf::inventory {

namespace {

using Json = nlohmann::json;

// ---- native backend: a game Container inside the data-driven block actor ----------------------------

/// A plain slot container (SimpleContainer's behaviour; its constructor is not exported).
class GfBlockContainer final : public Container {
public:
    explicit GfBlockContainer(int size)
    : Container(SharedTypes::Legacy::ContainerType::Container),
      mItems(static_cast<std::size_t>(std::max(1, size))) {}

    ItemStack const& getItem(int slot) const override {
        if (slot < 0 || slot >= static_cast<int>(mItems.size())) return ItemStack::EMPTY_ITEM();
        return mItems[static_cast<std::size_t>(slot)];
    }
    void setItem(int slot, ItemStack const& item) override {
        if (slot < 0 || slot >= static_cast<int>(mItems.size())) return;
        mItems[static_cast<std::size_t>(slot)] = item;
        setContainerChanged(slot);
        // the chunk saves changed block actors: tell it this one changed (screen edits included)
        if (where) {
            if (auto* region = game::regionOf(where->dim); region && region->hasChunksAt(game::blockPos(*where), 0, false)) {
                if (auto* actor = region->getBlockEntity(game::blockPos(*where))) region->fireBlockEntityChanged(*actor);
            }
        }
    }
    int  getContainerSize() const override { return static_cast<int>(mItems.size()); }
    int  getMaxStackSize() const override { return 64; }
    void startOpen(Actor&) override {}
    void stopOpen(Actor&) override {}
    void serverInitItemStackIds(int slot, int count, std::function<void(int, ItemStack const&)> onNetIdChanged) override {
        for (int i = std::max(0, slot); i < slot + count && i < static_cast<int>(mItems.size()); ++i) {
            auto& s = mItems[static_cast<std::size_t>(i)];
            if (s.isNull()) continue;
            s.serverInitNetId();
            if (onNetIdChanged) onNetIdChanged(i, s);
        }
    }

    std::vector<ItemStack>   mItems;
    std::optional<GlobalPos> where; ///< the block, once looked up (nativeContainer)
};

struct GfContainerComponent;

/// The components this module made (the game owns and deletes them).
std::mutex& liveMutex() {
    static std::mutex m;
    return m;
}
std::set<GfContainerComponent const*>& live() {
    static std::set<GfContainerComponent const*> s;
    return s;
}

/// Same layout as ContainerBlockActorComponent (vtable + the container), saving the slots as
/// GloamingForest item JSON inside the block actor's tag.
struct GfContainerComponent final : IBlockActorComponent {
    explicit GfContainerComponent(int size) : container(std::make_unique<GfBlockContainer>(size)) {
        std::lock_guard lock(liveMutex());
        live().insert(this);
    }
    ~GfContainerComponent() override {
        std::lock_guard lock(liveMutex());
        live().erase(this);
    }
    GfContainerComponent(GfContainerComponent const&)            = delete;
    GfContainerComponent& operator=(GfContainerComponent const&) = delete;

    void save(CompoundTag& parentTag, SaveContext const&) const override {
        Json items = Json::array();
        for (auto const& s : container->mItems) items.push_back(game::items::toJson(s));
        parentTag.putString("GfItems", items.dump());
    }
    void load(CompoundTag const& parentTag, ILevel&, BlockActorVersion) override {
        if (!parentTag.contains("GfItems")) return;
        auto items = Json::parse(std::string(parentTag["GfItems"].get<StringTag>()), nullptr, false);
        if (!items.is_array()) return;
        for (std::size_t i = 0; i < items.size() && i < container->mItems.size(); ++i) container->mItems[i] = game::items::fromJson(items[i]);
    }

    std::unique_ptr<GfBlockContainer> container; ///< ContainerBlockActorComponent::mContainer
};
static_assert(sizeof(GfContainerComponent) == sizeof(ContainerBlockActorComponent));

// ---- registry --------------------------------------------------------------------------------------

struct Store {
    std::recursive_mutex            mutex;
    std::map<std::string, Handler>  handlers;
    std::map<std::string, Declared> declared;
    std::set<std::string>           interactionsFor; ///< blocks whose use / destroy handlers exist
    std::set<std::string>           defaultStorage;  ///< blocks whose slots are the default backend's
};

Store& store() {
    static Store s;
    return s;
}

constexpr char kDataKey[] = "inventory";

BlockSource* regionFor(GlobalPos const& pos) { return game::regionOf(pos.dim); }

GfBlockContainer* gfContainer(BlockActor* actor) {
    if (!actor || !actor->mContainer) return nullptr;
    auto* comp = reinterpret_cast<GfContainerComponent*>(actor->mContainer.get());
    {
        std::lock_guard lock(liveMutex());
        if (!live().contains(comp)) return nullptr; // the game's own container component
    }
    return comp->container.get();
}

int slotsOf(std::string const& blockId) {
    std::lock_guard lock(store().mutex);
    auto            it = store().handlers.find(blockId);
    return it == store().handlers.end() ? 0 : it->second.slots;
}

// BlockData backend
Json storageOf(GlobalPos const& pos) {
    auto const* doc = blockdata::get(pos);
    if (!doc || !doc->is_object() || !doc->contains(kDataKey)) return Json::object();
    auto const& data = (*doc)[kDataKey];
    return data.is_object() ? data : Json::object();
}

void writeStorage(GlobalPos const& pos, Json const& data) {
    auto* region = regionFor(pos);
    if (!region) return;
    blockdata::set(*region, game::blockPos(pos), kDataKey, data);
}

/// The slot stacks of `pos` (BlockData or native), `size` long.
std::vector<ItemStack> readSlots(GlobalPos const& pos, int size) {
    std::vector<ItemStack> out(static_cast<std::size_t>(std::max(0, size)));
    auto*                  region = regionFor(pos);
    if (!region) return out;
    if (auto* c = nativeContainer(*region, game::blockPos(pos))) {
        for (int i = 0; i < size && i < c->getContainerSize(); ++i) out[static_cast<std::size_t>(i)] = c->getItem(i);
        return out;
    }
    auto items = storageOf(pos).value("items", Json::array());
    for (std::size_t i = 0; i < items.size() && i < out.size(); ++i) out[i] = game::items::fromJson(items[i]);
    return out;
}

void writeSlot(GlobalPos const& pos, int slot, ItemStack const& stack, int size) {
    auto* region = regionFor(pos);
    if (!region || slot < 0 || slot >= size) return;
    if (auto* c = nativeContainer(*region, game::blockPos(pos))) {
        c->setItem(slot, stack);
        return;
    }
    auto data  = storageOf(pos);
    auto items = data.value("items", Json::array());
    while (static_cast<int>(items.size()) < size) items.push_back(nullptr);
    items[static_cast<std::size_t>(slot)] = game::items::isEmpty(stack) ? Json(nullptr) : game::items::toJson(stack);
    data["items"]                         = items;
    writeStorage(pos, data);
}

/// Fills in the default backend for the members a registration left empty.
Handler withDefaults(Handler h) {
    int  size       = h.slots;
    bool defaultGet = !h.get;
    if (!h.get)
        h.get = [size](GlobalPos const& pos, int slot) {
            unpackLoot(pos, nullptr);
            auto all = readSlots(pos, size);
            return slot >= 0 && slot < size ? all[static_cast<std::size_t>(slot)] : ItemStack{};
        };
    if (!h.set)
        h.set = [size](GlobalPos const& pos, int slot, ItemStack const& stack) {
            unpackLoot(pos, nullptr);
            writeSlot(pos, slot, stack, size);
        };
    auto get = h.get;
    auto set = h.set;
    // every slot at once: one read of the storage for the default backend
    auto getAll = [size, get, defaultGet](GlobalPos const& pos) {
        if (defaultGet) {
            unpackLoot(pos, nullptr);
            return readSlots(pos, size);
        }
        std::vector<ItemStack> all;
        for (int i = 0; i < size; ++i) all.push_back(get(pos, i));
        return all;
    };
    if (!h.insert)
        h.insert = [size, getAll, set](GlobalPos const& pos, ItemStack const& stack, bool simulate) {
            ItemStack rest = stack;
            auto      all  = getAll(pos);
            // merge into matching stacks first, then empty slots (Java's ItemHandlerHelper order)
            for (int pass = 0; pass < 2 && !game::items::isEmpty(rest); ++pass) {
                for (int i = 0; i < size && !game::items::isEmpty(rest); ++i) {
                    auto& cur = all[static_cast<std::size_t>(i)];
                    int  max = std::min(64, static_cast<int>(rest.getMaxStackSize()));
                    if (pass == 0 && !game::items::isEmpty(cur) && game::items::sameItemSameData(cur, rest)) {
                        int room = max - game::items::countOf(cur);
                        if (room <= 0) continue;
                        int move = std::min(room, game::items::countOf(rest));
                        cur      = game::items::withCount(cur, game::items::countOf(cur) + move);
                        if (!simulate) set(pos, i, cur);
                        rest = game::items::withCount(rest, game::items::countOf(rest) - move);
                    } else if (pass == 1 && game::items::isEmpty(cur)) {
                        int move = std::min(max, game::items::countOf(rest));
                        cur      = game::items::withCount(rest, move);
                        if (!simulate) set(pos, i, cur);
                        rest = game::items::withCount(rest, game::items::countOf(rest) - move);
                    }
                }
            }
            return rest;
        };
    if (!h.extract)
        h.extract = [get, set](GlobalPos const& pos, int slot, int count, bool simulate) {
            auto cur = get(pos, slot);
            if (game::items::isEmpty(cur) || count <= 0) return ItemStack{};
            int take = std::min(count, game::items::countOf(cur));
            if (!simulate) set(pos, slot, game::items::withCount(cur, game::items::countOf(cur) - take));
            return game::items::withCount(cur, take);
        };
    if (!h.setLootTable)
        h.setLootTable = [](GlobalPos const& pos, std::string const& tableId, std::int64_t seed) {
            auto data    = storageOf(pos);
            data["loot"] = {{"table", tableId}, {"seed", seed}};
            writeStorage(pos, data);
        };
    if (!h.open) {
        int rows = std::clamp((size + 8) / 9, 1, 6);
        h.open   = [rows](Player& player, GlobalPos const& pos) {
            auto*       region = regionFor(pos);
            std::string id     = region ? game::blockIdAt(*region, game::blockPos(pos)) : std::string{};
            auto const* d      = declared(id);
            return openChestLike(player, pos, d ? d->rows : rows, d && !d->title.empty() ? d->title : "container.chest");
        };
    }
    return h;
}

// ---- the chest-like screen (client mod) and the form (vanilla clients) ---------------------------

/// The container the server opened for a player (openChestLike): the only one its screen may
/// read or change (a client's screen context is never trusted for the position, rows or title).
struct OpenScreen {
    GlobalPos   pos;
    int         rows = 3;
    std::string title;
    ItemStack   carried; ///< the stack on the cursor of the native screen
    bool        valid = false;
};

std::map<std::int64_t, OpenScreen>& openScreens() {
    static std::map<std::int64_t, OpenScreen> m;
    return m;
}

std::optional<GlobalPos> posOf(Json const& ctx) {
    auto p = ctx.value("pos", Json::array());
    if (!p.is_array() || p.size() != 4) return std::nullopt;
    for (auto const& v : p)
        if (!v.is_number_integer()) return std::nullopt;
    return GlobalPos{p[0].get<int>(), p[1].get<int>(), p[2].get<int>(), p[3].get<int>()};
}

bool inReach(Player& player, GlobalPos const& pos) {
    if (static_cast<int>(player.getDimensionId()) != pos.dim) return false;
    auto const& at = player.getPosition();
    float       dx = at.x - (pos.x + .5f), dy = at.y - (pos.y + .5f), dz = at.z - (pos.z + .5f);
    return dx * dx + dy * dy + dz * dz <= 64.f; // Java: stillValid within 8 blocks
}

/// The screen the server opened for `player` when the client's context names it (same position),
/// nullptr otherwise.
OpenScreen* openedFor(Player& player, Json const& ctx) {
    auto it = openScreens().find(player.getOrCreateUniqueID().rawID);
    auto at = posOf(ctx);
    if (it == openScreens().end() || !it->second.valid || !at || !(it->second.pos == *at)) return nullptr;
    return &it->second;
}

/// Every slot of a container with one read of the storage (the default backend parses its block
/// data once instead of once per slot).
std::vector<ItemStack> slotsOf(GlobalPos const& pos, Handler const& h, std::string const& blockId) {
    bool defaultStorage;
    {
        std::lock_guard lock(store().mutex);
        defaultStorage = store().defaultStorage.contains(blockId);
    }
    if (defaultStorage) {
        unpackLoot(pos, nullptr);
        return readSlots(pos, h.slots);
    }
    std::vector<ItemStack> all;
    all.reserve(static_cast<std::size_t>(std::max(0, h.slots)));
    for (int i = 0; i < h.slots; ++i) all.push_back(h.get(pos, i));
    return all;
}

std::optional<Json> screenData(Player& player, Json const& ctx) {
    auto* open = openedFor(player, ctx);
    if (!open) return std::nullopt; // not a container this player opened
    auto pos    = open->pos;
    auto region = regionFor(pos);
    if (!region || !inReach(player, pos)) return std::nullopt;
    auto bp = game::blockPos(pos);
    auto h  = find(*region, bp);
    if (!h) return std::nullopt;
    Json items = Json::array();
    for (auto const& s : slotsOf(pos, *h, game::blockIdAt(*region, bp)))
        items.push_back(game::items::isEmpty(s) ? Json(nullptr) : game::items::toJson(s));
    return Json{
        {"title", trFor(player, open->title, "Chest")},
        {"rows", open->rows},
        {"items", items},
        {"carried", game::items::isEmpty(open->carried) ? Json(nullptr) : game::items::toJson(open->carried)},
    };
}

/// Java's ChestMenu clicks on the container slots (pickup / place / swap / split / quick move).
void screenAction(Player& player, Json const& ctx, Json const& action) {
    auto* opened = openedFor(player, ctx);
    if (!opened) return; // only the container the server opened for this player
    auto pos    = opened->pos;
    auto region = regionFor(pos);
    if (!region || !inReach(player, pos)) return;
    auto h = find(*region, game::blockPos(pos));
    if (!h) return;
    auto& open  = *opened;
    auto op     = action.value("op", std::string{});
    int  slot   = action.value("slot", -1);
    bool right  = action.value("button", std::string{"left"}) == "right";
    bool shift  = action.value("shift", false);
    auto& inv   = player.getInventory();
    if (op == "click" && slot >= 0 && slot < h->slots) {
        auto cur = h->get(pos, slot);
        if (shift) { // quick move into the player's inventory
            if (game::items::isEmpty(cur)) return;
            ItemStack moving = cur;
            if (player.add(moving) || game::items::isEmpty(moving)) h->set(pos, slot, ItemStack{});
            else h->set(pos, slot, moving);
        } else if (game::items::isEmpty(open.carried)) {
            if (game::items::isEmpty(cur)) return;
            int take     = right ? (game::items::countOf(cur) + 1) / 2 : game::items::countOf(cur);
            open.carried = game::items::withCount(cur, take);
            h->set(pos, slot, game::items::withCount(cur, game::items::countOf(cur) - take));
        } else if (game::items::isEmpty(cur)) {
            int put = right ? 1 : game::items::countOf(open.carried);
            h->set(pos, slot, game::items::withCount(open.carried, put));
            open.carried = game::items::withCount(open.carried, game::items::countOf(open.carried) - put);
        } else if (game::items::sameItemSameData(cur, open.carried)) {
            int max  = static_cast<int>(cur.getMaxStackSize());
            int room = max - game::items::countOf(cur);
            int put  = std::min(room, right ? 1 : game::items::countOf(open.carried));
            if (put <= 0) return;
            h->set(pos, slot, game::items::withCount(cur, game::items::countOf(cur) + put));
            open.carried = game::items::withCount(open.carried, game::items::countOf(open.carried) - put);
        } else {
            h->set(pos, slot, open.carried);
            open.carried = cur;
        }
    } else if (op == "inv_click" && slot >= 0 && slot < inv.getContainerSize()) {
        auto cur = inv.getItem(slot);
        if (shift) { // quick move into the container
            if (game::items::isEmpty(cur)) return;
            auto rest = h->insert(pos, cur, false);
            inv.setItem(slot, rest);
        } else if (game::items::isEmpty(open.carried)) {
            if (game::items::isEmpty(cur)) return;
            int take     = right ? (game::items::countOf(cur) + 1) / 2 : game::items::countOf(cur);
            open.carried = game::items::withCount(cur, take);
            inv.setItem(slot, game::items::withCount(cur, game::items::countOf(cur) - take));
        } else if (game::items::isEmpty(cur)) {
            int put = right ? 1 : game::items::countOf(open.carried);
            inv.setItem(slot, game::items::withCount(open.carried, put));
            open.carried = game::items::withCount(open.carried, game::items::countOf(open.carried) - put);
        } else if (game::items::sameItemSameData(cur, open.carried)) {
            int room = static_cast<int>(cur.getMaxStackSize()) - game::items::countOf(cur);
            int put  = std::min(room, right ? 1 : game::items::countOf(open.carried));
            if (put <= 0) return;
            inv.setItem(slot, game::items::withCount(cur, game::items::countOf(cur) + put));
            open.carried = game::items::withCount(open.carried, game::items::countOf(open.carried) - put);
        } else {
            inv.setItem(slot, open.carried);
            open.carried = cur;
        }
    }
    player.sendInventory(false);
}

/// Gives a stack back to a player (inventory, else at its feet).
void giveBack(Player& player, ItemStack stack) {
    if (game::items::isEmpty(stack)) return;
    if (!player.add(stack) && !game::items::isEmpty(stack))
        player.getLevel().getSpawner().spawnItem(player.getDimensionBlockSource(), stack, nullptr, player.getPosition(), 0);
    player.sendInventory(false);
}

void screenClosed(Player& player, Json const&) {
    auto it = openScreens().find(player.getOrCreateUniqueID().rawID);
    if (it == openScreens().end()) return;
    ItemStack carried = it->second.carried;
    openScreens().erase(it);
    giveBack(player, carried);
}

constexpr char kCarriedKey[] = "gf:inventory:carried:";

/// The world closes: every cursor stack waits in the world DB for its player's next join (the
/// players may already be saved: a stack given now could be lost).
void returnAllCarried() {
    for (auto& [raw, open] : openScreens()) {
        if (game::items::isEmpty(open.carried)) continue;
        game::db::put(kCarriedKey + std::to_string(raw), game::items::toJson(open.carried));
        open.carried = ItemStack{};
    }
    openScreens().clear();
}

/// A player joins: a cursor stack kept from a world that closed while its screen was open.
void returnStoredCarried(Player& player) {
    auto key = kCarriedKey + std::to_string(player.getOrCreateUniqueID().rawID);
    auto doc = game::db::get(key);
    if (!doc) return;
    game::db::erase(key);
    giveBack(player, game::items::fromJson(*doc));
    log().info("inventory: returned a cursor stack to {}", player.getRealName());
}

/// Vanilla clients without the game's container screen (more or fewer than 27 slots): a form
/// listing the stacks (a button takes one) and "Put in" (a second form lists the player's stacks;
/// a button moves one in, Java's quick move).
void screenForm(Player& player, Json const& ctx);

void putInForm(Player& player, GlobalPos const& pos, Json const& ctx) {
    ll::form::SimpleForm form;
    form.setTitle(trFor(player, "gf.combat.container.put_in", "Put in"));
    auto&            inv = player.getInventory();
    std::vector<int> slots; // button -> inventory slot
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        auto const& s = inv.getItem(i);
        if (game::items::isEmpty(s)) continue;
        slots.push_back(i);
        form.appendButton(game::items::displayName(s) + " x" + std::to_string(game::items::countOf(s)));
    }
    form.setContent(slots.empty() ? trFor(player, "gf.combat.container.nothing", "(nothing to put in)") : std::string{});
    // one callback (button callbacks are ignored with it): a choice moves that stack in, closing
    // the form goes back to the container's form
    form.sendTo(player, [pos, ctx, slots](Player& p, int choice, ll::form::FormCancelReason) {
        if (choice >= 0 && choice < static_cast<int>(slots.size())) {
            auto* r  = regionFor(pos);
            auto  hh = r && inReach(p, pos) ? find(*r, game::blockPos(pos)) : std::nullopt;
            if (!hh) return;
            int   i    = slots[static_cast<std::size_t>(choice)];
            auto& pinv = p.getInventory();
            if (i >= pinv.getContainerSize()) return;
            auto cur = pinv.getItem(i);
            if (!game::items::isEmpty(cur)) {
                pinv.setItem(i, hh->insert(pos, cur, false)); // what does not fit stays
                p.sendInventory(false);
                log().info("inventory: {} put {} into the container at {} {} {} (form)", p.getRealName(), game::items::idOf(cur), pos.x, pos.y, pos.z);
            }
        }
        screenForm(p, ctx);
    });
}

void screenForm(Player& player, Json const& ctx) {
    auto at = posOf(ctx);
    if (!at) return;
    auto pos    = *at;
    auto region = regionFor(pos);
    if (!region) return;
    auto bp = game::blockPos(pos);
    auto h  = find(*region, bp);
    if (!h) return;
    ll::form::SimpleForm form;
    form.setTitle(trFor(player, ctx.value("title", std::string{"container.chest"}), "Chest"));
    form.appendButton(trFor(player, "gf.combat.container.put_in", "Put in"), [pos, ctx](Player& p) {
        if (inReach(p, pos)) putInForm(p, pos, ctx);
    });
    int  used = 0;
    auto all  = slotsOf(pos, *h, game::blockIdAt(*region, bp));
    for (int i = 0; i < static_cast<int>(all.size()); ++i) {
        auto const& s = all[static_cast<std::size_t>(i)];
        if (game::items::isEmpty(s)) continue;
        ++used;
        form.appendButton(game::items::displayName(s) + " x" + std::to_string(game::items::countOf(s)), [pos, i, ctx](Player& p) {
            auto* r = regionFor(pos);
            if (!r || !inReach(p, pos)) return;
            auto hh = find(*r, game::blockPos(pos));
            if (!hh) return;
            auto taken = hh->extract(pos, i, 64, false);
            if (!p.add(taken) && !game::items::isEmpty(taken)) hh->insert(pos, taken, false);
            p.sendInventory(false);
            screenForm(p, ctx);
        });
    }
    form.setContent(used == 0 ? trFor(player, "gf.combat.container.empty", "(empty)") : std::string{});
    form.sendTo(player);
}

// ---- data-declared containers ------------------------------------------------------------------------

void spawnAll(BlockSource& region, BlockPos const& bp, std::vector<ItemStack> const& stuff) {
    Vec3 at{static_cast<float>(bp.x) + .5f, static_cast<float>(bp.y) + .5f, static_cast<float>(bp.z) + .5f};
    int  n = 0;
    for (auto const& s : stuff)
        if (!game::items::isEmpty(s)) {
            region.getLevel().getSpawner().spawnItem(region, s, nullptr, at, 10);
            ++n;
        }
    if (n) log().info("inventory: dropped {} stack(s) of the container at {} {} {}", n, bp.x, bp.y, bp.z);
}

/// A declared BlockData container's document is erased because its block changed (broken,
/// exploded, replaced): Java's Containers#dropContents in onRemove, a pending table rolled first.
void dropErased(GlobalPos const& pos, Json const& doc, std::string const& previousBlockId) {
    if (previousBlockId.empty() || !declared(previousBlockId) || !doc.is_object()) return;
    auto const& data = doc.contains(kDataKey) ? doc[kDataKey] : Json{};
    if (!data.is_object()) return;
    auto* region = regionFor(pos);
    if (!region) return;
    std::vector<ItemStack> stuff;
    for (auto const& item : data.value("items", Json::array())) stuff.push_back(game::items::fromJson(item));
    auto loot = data.value("loot", Json{});
    if (loot.is_object() && !loot.value("table", std::string{}).empty()) {
        loot::LootContext ctx;
        ctx.region = region;
        ctx.origin = Vec3{static_cast<float>(pos.x) + .5f, static_cast<float>(pos.y) + .5f, static_cast<float>(pos.z) + .5f};
        ctx.seed   = loot.value("seed", std::int64_t{0});
        for (auto& s : loot::roll(loot.value("table", std::string{}), ctx)) stuff.push_back(std::move(s));
    }
    spawnAll(*region, game::blockPos(pos), stuff);
}

void ensureInteractions(std::string const& blockId) {
    {
        std::lock_guard lock(store().mutex);
        if (!store().interactionsFor.insert(blockId).second) return;
    }
    namespace ia = interactions;
    ia::onUseOn({.block = blockId}, [](ia::UseOnContext const& c) {
        if (!declared(c.blockId)) return ia::Result::Pass;
        if (c.sneaking && !c.itemId.empty()) return ia::Result::Pass; // Java: sneaking with an item places
        if (!c.firstEvent) return ia::Result::Consume;
        auto h = handlerFor(c.blockId);
        if (h && h->open) h->open(c.player, c.pos);
        return ia::Result::Consume;
    });
    // Java: breaking unpacks the loot with the breaker first; the contents drop when the block
    // goes (dropErased for BlockData, the block actor's onRemoved for native containers)
    ia::onDestroy({.block = blockId}, [](ia::DestroyContext const& c) {
        if (declared(c.blockId)) unpackLoot(c.pos, &c.player);
        return ia::Result::Pass;
    });
}

void loadDeclared() {
    std::filesystem::path dir;
    try {
        dir = Mod::getInstance().getSelf().getModDir() / "data" / "gf" / "containers";
    } catch (...) {
        return;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return;
    int n = 0;
    for (auto const& f : std::filesystem::directory_iterator(dir, ec)) {
        if (f.path().extension() != ".json") continue;
        try {
            std::ifstream in(f.path());
            auto          j = Json::parse(in, nullptr, true, true);
            for (auto const& e : j.is_array() ? j : Json::array()) {
                Declared d;
                d.block = e.value("block", std::string{});
                d.rows  = std::clamp(e.value("rows", 3), 1, 6);
                d.slots = e.value("slots", d.rows * 9);
                d.title = e.value("title", std::string{"container.chest"});
                d.skin  = e.value("skin", std::string{});
                if (d.block.empty()) continue;
                declare(d);
                ++n;
            }
        } catch (std::exception const& ex) {
            log().error("inventory: bad container file {}: {}", f.path().string(), ex.what());
        }
    }
    if (n) log().info("inventory: {} data-declared container(s)", n);
}

std::int64_t freshSeed() {
    static std::mt19937_64 gen{std::random_device{}()};
    return static_cast<std::int64_t>(gen());
}

} // namespace

// ---- internal API ------------------------------------------------------------------------------------

Container* nativeContainer(BlockSource& region, BlockPos const& pos) {
    auto* actor = region.getBlockEntity(pos);
    if (!actor || actor->getType() != BlockActorType::DataDriven) return nullptr;
    int slots = slotsOf(game::blockIdAt(region, pos));
    if (slots <= 0) return nullptr;
    if (!actor->mContainer) {
        // a block actor made before the block was registered (registrations happen at mod load,
        // so only code registrations after a chunk loaded get here): give it an empty container
        actor->mContainer.reset(reinterpret_cast<ContainerBlockActorComponent*>(new GfContainerComponent(slots)));
        log().warn("inventory: the container at {} {} {} was created late (its saved slots were not read)", pos.x, pos.y, pos.z);
    }
    auto* c = gfContainer(actor);
    if (c && !c->where) c->where = game::globalPos(region, pos);
    return c;
}

int openNativeScreen(Player& player, GlobalPos const& pos, int containerType) {
    auto* region = regionFor(pos);
    if (!region || player.isClientSide()) return -1;
    auto bp = game::blockPos(pos);
    auto* c = nativeContainer(*region, bp);
    if (!c) return -1;
    auto id = static_cast<ServerPlayer&>(player).openUnmanagedContainer(static_cast<SharedTypes::Legacy::ContainerType>(containerType), bp);
    log().info("inventory: {} opened the native container at {} {} {} as type {} (id {})", player.getRealName(), pos.x, pos.y, pos.z, containerType, static_cast<int>(id));
    return static_cast<int>(id);
}

std::optional<Handler> handlerFor(std::string const& blockId) {
    std::lock_guard lock(store().mutex);
    auto            it = store().handlers.find(blockId);
    if (it == store().handlers.end()) return std::nullopt;
    return it->second;
}

void unregisterBlock(std::string const& blockId) {
    std::lock_guard lock(store().mutex);
    store().handlers.erase(blockId);
    store().declared.erase(blockId);
}

void declare(Declared const& d) {
    {
        std::lock_guard lock(store().mutex);
        store().declared[d.block] = d;
    }
    Handler h;
    h.slots = d.slots;
    registerBlock(d.block, h);
    ensureInteractions(d.block);
}

Declared const* declared(std::string const& blockId) {
    std::lock_guard lock(store().mutex);
    auto            it = store().declared.find(blockId);
    return it == store().declared.end() ? nullptr : &it->second;
}

std::vector<ItemStack> contents(GlobalPos const& pos) {
    auto* region = regionFor(pos);
    if (!region) return {};
    auto bp = game::blockPos(pos);
    auto h  = find(*region, bp);
    if (!h) return {};
    return slotsOf(pos, *h, game::blockIdAt(*region, bp));
}

std::optional<std::pair<std::string, std::int64_t>> pendingLoot(GlobalPos const& pos) {
    auto loot = storageOf(pos).value("loot", Json{});
    if (!loot.is_object() || loot.value("table", std::string{}).empty()) return std::nullopt;
    return std::make_pair(loot.value("table", std::string{}), loot.value("seed", std::int64_t{0}));
}

bool unpackLoot(GlobalPos const& pos, Player* opener) {
    auto pending = pendingLoot(pos);
    if (!pending) return false;
    auto* region = regionFor(pos);
    if (!region) return false;
    auto data = storageOf(pos);
    data.erase("loot");
    writeStorage(pos, data); // first: the fill below reads the slots through the handler
    auto h = find(*region, game::blockPos(pos));
    if (!h) return false;
    loot::LootContext ctx;
    ctx.region     = region;
    ctx.origin     = Vec3{static_cast<float>(pos.x) + .5f, static_cast<float>(pos.y) + .5f, static_cast<float>(pos.z) + .5f};
    ctx.thisEntity = opener;
    ctx.player     = opener;
    ctx.seed       = pending->second != 0 ? pending->second : freshSeed();
    auto slots     = readSlots(pos, h->slots);
    loot::fill(
        pending->first,
        ctx,
        h->slots,
        [&](int i) -> ItemStack const& { return slots[static_cast<std::size_t>(i)]; },
        [&](int i, ItemStack const& s) {
            slots[static_cast<std::size_t>(i)] = s;
            writeSlot(pos, i, s, h->slots);
        }
    );
    log().info("inventory: unpacked {} at {} {} {}", pending->first, pos.x, pos.y, pos.z);
    return true;
}

// ---- contract -------------------------------------------------------------------------------------

void registerBlock(std::string const& blockId, Handler handler) {
    std::lock_guard lock(store().mutex);
    if (handler.get) store().defaultStorage.erase(blockId);
    else store().defaultStorage.insert(blockId);
    store().handlers[blockId] = withDefaults(std::move(handler));
}

std::optional<Handler> find(BlockSource& region, BlockPos const& pos) {
    auto id = game::blockIdAt(region, pos);
    if (id.empty()) return std::nullopt;
    return handlerFor(id);
}

bool openChestLike(Player& player, GlobalPos const& pos, int rows, std::string const& titleKey) {
    auto* region = regionFor(pos);
    if (!region) return false;
    auto bp = game::blockPos(pos);
    if (!find(*region, bp)) return false;
    unpackLoot(pos, &player);
    rows = std::clamp(rows, 1, 6);
    std::string skin;
    if (auto const* d = declared(game::blockIdAt(*region, bp))) skin = d->skin;
    // Companions: the client mod's chest screen. Vanilla clients: the game's chest screen through
    // a chest minecart proxy for 27 slots (VanillaContainer.cpp; the client shows no screen for a
    // custom block's own container, spike S3), the form for other sizes. The title of the
    // vanilla screen goes through F12's container title skin convention (Declared::skin).
    bool companion = net::isCompanion(player);
    if (!companion) {
        auto title = trFor(player, titleKey, "Chest");
        if (!skin.empty()) title = ui::containerTitleSkin(title, skin);
        if (openVanillaContainer(player, pos, title)) return true;
    }
    // the server's record of what this player may see and change (screenData / screenAction)
    auto& open = openScreens()[player.getOrCreateUniqueID().rawID];
    open.pos   = pos;
    open.rows  = rows;
    open.title = titleKey;
    open.valid = true;
    Json ctx{{"pos", {pos.dim, pos.x, pos.y, pos.z}}, {"rows", rows}, {"title", titleKey}};
    bool ok = screens::open(player, "inventory.chest", ctx);
    if (!ok || !companion) { // a form needs no record; a refused screen leaves none
        auto it = openScreens().find(player.getOrCreateUniqueID().rawID);
        if (it != openScreens().end()) {
            ItemStack carried = it->second.carried;
            openScreens().erase(it);
            giveBack(player, carried);
        }
    }
    log().info("inventory: {} opened the container at {} {} {} ({})", player.getRealName(), pos.x, pos.y, pos.z, companion ? "client mod screen" : "form");
    return ok;
}

// ---- module ---------------------------------------------------------------------------------------

namespace {

class InventoryModule;

} // namespace

} // namespace gf::inventory

LL_STATIC_HOOK(
    InventoryNativeContainerHook,
    ll::memory::HookPriority::Normal,
    &ContainerBlockActorComponent::tryCreateFor,
    std::unique_ptr<ContainerBlockActorComponent>,
    BlockType const& blockType
) {
    auto made = origin(blockType);
    if (made) return made;
    int slots = gf::inventory::slotsOf(blockType.getTypeName());
    if (slots <= 0) return made;
    auto* comp = new gf::inventory::GfContainerComponent(slots);
    return std::unique_ptr<ContainerBlockActorComponent>(reinterpret_cast<ContainerBlockActorComponent*>(comp));
}

// A native container's block is removed: its slots drop (Java's Containers#dropContents). The slots
// are emptied before the game's own removal runs, so nothing can drop twice.
LL_TYPE_INSTANCE_HOOK(
    InventoryNativeRemovedHook,
    ll::memory::HookPriority::Normal,
    DataDrivenBlockActor,
    &DataDrivenBlockActor::$onRemoved,
    void,
    BlockSource& region
) {
    auto* c = gf::inventory::gfContainer(this);
    if (!c || region.getLevel().isClientSide()) return origin(region);
    std::vector<ItemStack> stuff;
    for (auto& s : c->mItems) {
        if (!gf::game::items::isEmpty(s)) stuff.push_back(s);
        s = ItemStack{};
    }
    origin(region);
    gf::inventory::spawnAll(region, getPosition(), stuff);
}

namespace gf::inventory {
namespace {

void onDisconnect(Player& player) { screenClosed(player, Json::object()); }

class InventoryModule {
public:
    /// At mod load, before any world: block actors of chunks loaded with the world need the
    /// registrations (the native container is made when the actor is).
    void load() {
        InventoryNativeContainerHook::hook();
        mHooked = true;
        loadDeclared();
    }
    void install() {
        if (!mHooked) InventoryNativeContainerHook::hook();
        mHooked = true;
        InventoryNativeRemovedHook::hook();
        // a player who leaves with a stack on the chest screen's cursor gets it back first; one
        // kept from a world that closed comes back on the next join
        mDisconnect = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerDisconnectEvent>(
            [](ll::event::PlayerDisconnectEvent& ev) { onDisconnect(ev.self()); }
        );
        mJoin = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerJoinEvent>([](ll::event::PlayerJoinEvent& ev) {
            if (!ev.self().isClientSide()) returnStoredCarried(ev.self());
        });
        static bool registered = false;
        if (registered) return;
        registered = true;
        screens::registerType({
            .id     = "inventory.chest",
            .data   = screenData,
            .action = screenAction,
            .form   = screenForm,
            .closed = screenClosed,
        });
        blockdata::onErase(dropErased);
        game::db::onWorldClosed([] { returnAllCarried(); });
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mDisconnect);
        ll::event::EventBus::getInstance().removeListener(mJoin);
        InventoryNativeRemovedHook::unhook();
        if (mHooked) InventoryNativeContainerHook::unhook();
        mHooked = false;
    }

private:
    bool                   mHooked = false;
    ll::event::ListenerPtr mDisconnect;
    ll::event::ListenerPtr mJoin;
};

} // namespace
} // namespace gf::inventory

GF_REGISTER_MODULE_ORDERED(gf::inventory::InventoryModule, gf::ModuleOrder::Contracts);
