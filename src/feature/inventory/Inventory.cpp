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
// with the game's chest screen through a chest minecart proxy (27 slots; a double chest as the
// game's large chest screen of 54 slots on a client-side chest pair, VanillaContainer.cpp) or a
// form (take a stack, put stacks in) for other sizes: the client does not show the game's screen
// for a custom block's own container (S3; openNativeScreen keeps the experiment).
// While a proxy is open, every write through a handler from `find` reaches it at once, so the
// screen never shows a stack the storage no longer has.
//
// The client mod's screen follows Java's ChestMenu (AbstractContainerMenu#doClick): pickup, quick
// move (ChestMenu#quickMoveStack with moveItemStackTo's order), swap with the hotbar keys and the
// off-hand key, clone (creative), throw (Q / Ctrl+Q), drag-splitting (quick craft), double-click
// collecting (pickup all) and a click outside the panel dropping the cursor stack. The menu's slot
// indices are Java's: the container slots, the player's main inventory (inventory slots 9..35),
// then the hotbar (0..8).
//
// Chest-like containers can be declared as data, <mod>/data/gf/containers/<u>.json:
//     [{"block": "twilightforest:twilight_oak_chest", "rows": 3, "title": "container.chest", "skin": "",
//       "chest": true, "pairs": true, "double_title": "container.chestDouble",
//       "sounds": {"open": "minecraft:block.chest.open", "close": "minecraft:block.chest.close"},
//       "stat": "minecraft:open_chest", "comparator": true}]
// which registers the storage, opens it on use (not while sneaking with an item; ChestBlock#
// isChestBlockedAt: not while a redstone conductor or a sitting cat is above, `chest`), awards the
// stat, plays the open / close sounds at the first opener and after the last one (ChestBlockEntity's
// ContainerOpenersCounter: the right half of a double chest plays them between the halves, the left
// half none), pairs a half whose chest type state is left / right with its neighbour into one
// 54-slot screen (ChestBlock#combine: the right half's slots first, `pairs`), gives vanilla
// comparators Java's container signal (redstone::registerAnalog, `comparator`) and drops the
// contents when the block is broken. A pending loot table (placeContainer / setLootTable) rolls on
// the first access (Java's RandomizableContainer#unpackLootTable: the opener's luck; a comparator
// reading the container unpacks it too, as Java's getItem does). Hoppers do not see custom
// containers (spike S3).

#include "feature/inventory/Inventory.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <random>
#include <set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/common/Brightness.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components_json_legacy/AngryComponent.h"
#include "mc/util/VariantParameterList.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/level/ShapeType.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/safety/RedactableString.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/ContainerID.h"
#include "mc/world/DataDrivenBlockContainer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/DataDrivenBlockActor.h"
#include "mc/world/level/block/actor/component/ContainerBlockActorComponent.h"
#include "mc/world/level/block/actor/component/IBlockActorComponent.h"
#include "mc/world/phys/AABB.h"

#include "feature/common/BlockData.h"
#include "feature/common/Interactions.h"
#include "feature/common/Redstone.h"
#include "feature/common/Screens.h"
#include "feature/ext/combat/InventoryViewers.h"
#include "feature/fx/Fx.h"
#include "feature/inventory/InventoryInternal.h"
#include "feature/loot/LootInternal.h"
#include "feature/net/Net.h"
#include "feature/stats/Stats.h"
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
        if (where) {
            // the chunk saves changed block actors: tell it this one changed (screen edits included)
            if (auto* region = game::regionOf(where->dim); region && region->hasChunksAt(game::blockPos(*where), 0, false)) {
                if (auto* actor = region->getBlockEntity(game::blockPos(*where))) region->fireBlockEntityChanged(*actor);
            }
            // game code writing the container directly (not hoppers: they do not see it, S3): an
            // open proxy shows it at once
            if (proxyOpen(*where)) proxyMirror(*where, slot, item);
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
    std::map<std::string, Handler>  handlers; ///< as registered, defaults filled (raw)
    std::map<std::string, Declared> declared;
    std::set<std::string>           interactionsFor; ///< blocks whose use / destroy handlers exist
    std::set<std::string>           defaultStorage;  ///< blocks whose slots are the default backend's
    std::set<std::string>           analogFor;       ///< blocks whose comparator output this module registered
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
            auto        d      = declared(id);
            return openChestLike(player, pos, d ? d->rows : rows, d && !d->title.empty() ? d->title : "container.chest");
        };
    }
    return h;
}

/// The handler units and screens use: every write reaches an open vanilla chest screen proxy at
/// once (one copy of the slots while it is open).
Handler routed(Handler raw) {
    Handler h = raw;
    h.set     = [raw](GlobalPos const& pos, int slot, ItemStack const& stack) {
        raw.set(pos, slot, stack);
        // the stack written (a handler that stores something else is reconciled next tick)
        if (proxyOpen(pos)) proxyMirror(pos, slot, stack);
    };
    h.insert = [raw](GlobalPos const& pos, ItemStack const& stack, bool simulate) {
        auto rest = raw.insert(pos, stack, simulate);
        if (!simulate && proxyOpen(pos)) proxyPull(pos);
        return rest;
    };
    h.extract = [raw](GlobalPos const& pos, int slot, int count, bool simulate) {
        auto taken = raw.extract(pos, slot, count, simulate);
        if (!simulate && proxyOpen(pos)) proxyPull(pos);
        return taken;
    };
    return h;
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

// ---- chests: halves, the blocked rule ----------------------------------------------------------------

enum class ChestType { Single, Left, Right };

struct Half {
    ChestType type   = ChestType::Single;
    int       facing = 2; ///< FacingID: 2 north, 3 south, 4 west, 5 east
};

/// The chest type / facing states of a block (TF's ChestBlock TYPE and FACING, as the families kit
/// or the import name them).
Half halfOf(Block const& block) {
    Half h;
    for (auto const& [name, value] : game::statesOf(block)) {
        auto key = name.substr(name.find(':') == std::string::npos ? 0 : name.find(':') + 1);
        if (key == "type" || key == "chest_type") {
            if (value == "left") h.type = ChestType::Left;
            else if (value == "right") h.type = ChestType::Right;
        } else if (key == "facing" || key == "cardinal_direction") {
            if (value == "north") h.facing = 2;
            else if (value == "south") h.facing = 3;
            else if (value == "west") h.facing = 4;
            else if (value == "east") h.facing = 5;
        }
    }
    return h;
}

/// Direction#getClockWise / getCounterClockWise of a horizontal FacingID.
int clockWise(int f) { return f == 2 ? 5 : f == 5 ? 3 : f == 3 ? 4 : 2; }
int counterClockWise(int f) { return f == 2 ? 4 : f == 4 ? 3 : f == 3 ? 5 : 2; }

BlockPos step(BlockPos const& p, int facing) {
    switch (facing) {
    case 2: return {p.x, p.y, p.z - 1};
    case 3: return {p.x, p.y, p.z + 1};
    case 4: return {p.x - 1, p.y, p.z};
    case 5: return {p.x + 1, p.y, p.z};
    default: return p;
    }
}

/// ChestBlock#getConnectedDirection: the partner of a left half is clockwise of its facing.
int connectedDirection(Half const& h) { return h.type == ChestType::Left ? clockWise(h.facing) : counterClockWise(h.facing); }

/// Java's isRedstoneConductor, near enough: a solid, full block (as feature/common/Redstone.cpp)
/// that lets no light through (glass, leaves and the like are no conductors).
bool conductor(Block const& b) {
    return !b.isAir() && b._isSolid() && b.canProvideFullSupport(1) && b.canProvideFullSupport(0) && b.mBlockType->mLightBlock->mValue >= 15;
}

/// ChestBlock#isChestBlockedAt for one position.
bool blockedAt(BlockSource& region, BlockPos const& bp) {
    BlockPos above{bp.x, bp.y + 1, bp.z};
    if (conductor(region.getBlock(above))) return true; // isBlockedChestByBlock
    // isCatSittingOnChest: a sitting cat in the block above
    AABB box{Vec3{static_cast<float>(bp.x), static_cast<float>(bp.y + 1), static_cast<float>(bp.z)}, Vec3{static_cast<float>(bp.x + 1), static_cast<float>(bp.y + 2), static_cast<float>(bp.z + 1)}};
    bool sitting = false;
    for (auto cat : region.fetchEntities(ActorType::Cat, box, nullptr, [](Actor*) { return true; }))
        if (cat->isSitting()) sitting = true;
    return sitting;
}

// ---- openers (ContainerOpenersCounter) ---------------------------------------------------------------

/// The container the server opened for a player (openChestLike): the only one its screen may
/// read or change (a client's screen context is never trusted for the position, rows or title).
struct OpenScreen {
    GlobalPos              pos;   ///< the position the screen was opened for (the context's)
    std::vector<GlobalPos> parts; ///< the containers it shows, in slot order
    int                    rows = 3;
    std::string            title;
    ItemStack              carried; ///< the stack on the cursor of the native screen
    bool                   valid = false;
};

std::map<std::int64_t, OpenScreen>& openScreens() {
    static std::map<std::int64_t, OpenScreen> m;
    return m;
}

/// Vanilla clients with the form open: player -> the parts it shows.
std::map<std::int64_t, std::vector<GlobalPos>>& formViewers() {
    static std::map<std::int64_t, std::vector<GlobalPos>> m;
    return m;
}

struct Openers {
    std::map<GlobalPos, int>                    counts; ///< as last counted
    SoundCounters                               sounds;
    std::map<std::string, std::vector<ViewersFn>> listeners;
};

Openers& openers() {
    static Openers o;
    return o;
}

/// ChestBlockEntity#playSound: a single chest at its centre, a right half between the halves, a
/// left half none; volume 0.5, pitch 0.9..1.0.
void playOpenerSound(GlobalPos const& pos, Declared const& d, bool open) {
    auto const& event = open ? d.openSound : d.closeSound;
    if (event.empty()) return;
    auto* region = regionFor(pos);
    if (!region) return;
    auto bp = game::blockPos(pos);
    Vec3 at{static_cast<float>(pos.x) + .5f, static_cast<float>(pos.y) + .5f, static_cast<float>(pos.z) + .5f};
    if (d.pairs) {
        auto half = halfOf(region->getBlock(bp));
        if (half.type == ChestType::Left) return;
        if (half.type == ChestType::Right) {
            auto other = step(bp, connectedDirection(half));
            at.x += static_cast<float>(other.x - bp.x) * .5f;
            at.z += static_cast<float>(other.z - bp.z) * .5f;
        }
    }
    static thread_local std::mt19937          rng{std::random_device{}()};
    std::uniform_real_distribution<float> unit(0.f, 1.f);
    fx::sound(region->getDimension(), event, at, .5f, unit(rng) * .1f + .9f);
    if (open) ++openers().sounds.opened;
    else ++openers().sounds.closed;
}

std::set<GlobalPos> positionsWithOpeners() {
    std::set<GlobalPos> out;
    for (auto const& [pos, _] : openers().counts) out.insert(pos);
    for (auto const& [_, open] : openScreens())
        if (open.valid) out.insert(open.parts.begin(), open.parts.end());
    for (auto const& [_, parts] : formViewers()) out.insert(parts.begin(), parts.end());
    return out;
}

// ---- the chest-like screen (client mod) --------------------------------------------------------------

std::optional<GlobalPos> posOf(Json const& ctx) {
    auto p = ctx.value("pos", Json::array());
    if (!p.is_array() || p.size() != 4) return std::nullopt;
    for (auto const& v : p)
        if (!v.is_number_integer()) return std::nullopt;
    return GlobalPos{p[0].get<int>(), p[1].get<int>(), p[2].get<int>(), p[3].get<int>()};
}

/// Container#stillValidBlockEntity (1.21): the eye within the block interaction range + 4 of the
/// block's box (Bedrock players' position is the eye).
bool inReach(Player& player, GlobalPos const& pos) {
    if (static_cast<int>(player.getDimensionId()) != pos.dim) return false;
    auto const& eye   = player.getPosition();
    float       range = (player.isCreative() ? 5.f : 4.5f) + 4.f;
    float       dx = eye.x - std::clamp(eye.x, static_cast<float>(pos.x), static_cast<float>(pos.x) + 1.f);
    float       dy = eye.y - std::clamp(eye.y, static_cast<float>(pos.y), static_cast<float>(pos.y) + 1.f);
    float       dz = eye.z - std::clamp(eye.z, static_cast<float>(pos.z), static_cast<float>(pos.z) + 1.f);
    return dx * dx + dy * dy + dz * dz < range * range;
}

/// The screen the server opened for `player` when the client's context names it (same position),
/// nullptr otherwise.
OpenScreen* openedFor(Player& player, Json const& ctx) {
    auto it = openScreens().find(player.getOrCreateUniqueID().rawID);
    auto at = posOf(ctx);
    if (it == openScreens().end() || !it->second.valid || !at || !(it->second.pos == *at)) return nullptr;
    return &it->second;
}

/// Gives a stack back to a player (inventory, else at its feet).
void giveBack(Player& player, ItemStack stack) {
    if (game::items::isEmpty(stack)) return;
    if (!player.add(stack) && !game::items::isEmpty(stack))
        player.getLevel().getSpawner().spawnItem(player.getDimensionBlockSource(), stack, nullptr, player.getPosition(), 0);
    player.sendInventory(false);
}

/// Java's ClickType.
enum class Click { Pickup, QuickMove, Swap, Clone, Throw, QuickCraft, PickupAll };

/// ChestMenu over the opened parts and the player's inventory (AbstractContainerMenu#doClick).
class Menu {
public:
    Menu(Player& player, OpenScreen& open) : mPlayer(player), mOpen(open) {
        for (auto const& p : open.parts) {
            auto* region = regionFor(p);
            auto  h      = region ? find(*region, game::blockPos(p)) : std::nullopt;
            if (!h) {
                mValid = false;
                return;
            }
            mHandlers.push_back(*h);
            mContainerSlots += h->slots;
        }
        mContainerSlots = std::min(mContainerSlots, open.rows * 9);
    }

    [[nodiscard]] bool valid() const { return mValid && !mHandlers.empty(); }
    [[nodiscard]] int  containerSlots() const { return mContainerSlots; }
    [[nodiscard]] int  size() const { return mContainerSlots + 36; }

    /// The menu index of inventory slot `k` (0..35).
    [[nodiscard]] int indexOfInventory(int k) const { return k >= 9 ? mContainerSlots + (k - 9) : mContainerSlots + 27 + k; }

    [[nodiscard]] ItemStack get(int i) const {
        if (i < 0 || i >= size()) return {};
        if (i < mContainerSlots) {
            auto [part, local] = partOf(i);
            return mHandlers[part].get(mOpen.parts[part], local);
        }
        return mPlayer.getInventory().getItem(inventorySlot(i));
    }
    void set(int i, ItemStack const& s) {
        if (i < 0 || i >= size()) return;
        if (i < mContainerSlots) {
            auto [part, local] = partOf(i);
            mHandlers[part].set(mOpen.parts[part], local, s);
            return;
        }
        mPlayer.getInventory().setItem(inventorySlot(i), s);
    }

    /// AbstractContainerMenu#doClick for one click (quick craft: `quickCraft`).
    void click(int slotId, int button, Click type) {
        auto& carried = mOpen.carried;
        if ((type == Click::Pickup || type == Click::QuickMove) && (button == 0 || button == 1)) {
            bool primary = button == 0;
            if (slotId == -999) {
                if (isEmpty(carried)) return;
                if (primary) {
                    drop(carried);
                    carried = ItemStack{};
                } else {
                    drop(withCount(carried, 1));
                    carried = withCount(carried, count(carried) - 1);
                }
            } else if (type == Click::QuickMove) {
                if (slotId < 0 || slotId >= size()) return;
                auto moved = quickMoveStack(slotId);
                while (!isEmpty(moved) && game::items::sameItem(get(slotId), moved)) moved = quickMoveStack(slotId);
            } else {
                if (slotId < 0 || slotId >= size()) return;
                auto cur = get(slotId);
                if (isEmpty(cur)) {
                    if (!isEmpty(carried)) carried = safeInsert(slotId, carried, primary ? count(carried) : 1);
                } else if (isEmpty(carried)) {
                    int n   = primary ? count(cur) : (count(cur) + 1) / 2;
                    carried = tryRemove(slotId, n, std::numeric_limits<int>::max());
                } else if (same(cur, carried)) {
                    carried = safeInsert(slotId, carried, primary ? count(carried) : 1);
                } else if (count(carried) <= slotMax(carried)) {
                    set(slotId, carried);
                    carried = cur;
                }
            }
        } else if (type == Click::Swap && ((button >= 0 && button < 9) || button == 40)) {
            if (slotId < 0 || slotId >= size()) return;
            auto hand = button == 40 ? ItemStack(mPlayer.getOffhandSlot()) : ItemStack(mPlayer.getInventory().getItem(button));
            auto cur  = get(slotId);
            if (isEmpty(hand) && isEmpty(cur)) return;
            if (isEmpty(hand)) {
                setHand(button, cur);
                set(slotId, ItemStack{});
            } else if (isEmpty(cur)) {
                int j = slotMax(hand);
                if (count(hand) > j) {
                    set(slotId, withCount(hand, j));
                    setHand(button, withCount(hand, count(hand) - j));
                } else {
                    setHand(button, ItemStack{});
                    set(slotId, hand);
                }
            } else {
                int k = slotMax(hand);
                if (count(hand) > k) {
                    set(slotId, withCount(hand, k));
                    setHand(button, withCount(hand, count(hand) - k));
                    ItemStack rest = cur;
                    if (!mPlayer.add(rest) && !isEmpty(rest)) drop(rest);
                } else {
                    setHand(button, cur);
                    set(slotId, hand);
                }
            }
        } else if (type == Click::Clone) {
            if (!mPlayer.isCreative() || !isEmpty(carried) || slotId < 0 || slotId >= size()) return;
            auto cur = get(slotId);
            if (!isEmpty(cur)) carried = withCount(cur, maxStack(cur));
        } else if (type == Click::Throw) {
            if (!isEmpty(carried) || slotId < 0 || slotId >= size()) return;
            auto cur = get(slotId);
            if (isEmpty(cur)) return;
            drop(tryRemove(slotId, button == 0 ? 1 : count(cur), std::numeric_limits<int>::max()));
        } else if (type == Click::PickupAll) {
            if (slotId < 0 || slotId >= size()) return;
            if (isEmpty(carried) || !isEmpty(get(slotId))) return;
            int start = button == 0 ? 0 : size() - 1;
            int dir   = button == 0 ? 1 : -1;
            for (int pass = 0; pass < 2; ++pass) {
                for (int k = start; k >= 0 && k < size() && count(carried) < maxStack(carried); k += dir) {
                    auto s = get(k);
                    if (isEmpty(s) || !canQuickReplace(k, carried, true)) continue;
                    if (pass == 0 && count(s) == maxStack(s)) continue; // full stacks last
                    auto taken = tryRemove(k, count(s), maxStack(carried) - count(carried));
                    carried    = withCount(carried, count(carried) + count(taken));
                }
            }
        }
    }

    /// QUICK_CRAFT from its start to its end (the client sends the slots of one drag at once):
    /// `type` 0 split evenly, 1 one each, 2 a full stack each (creative).
    void quickCraft(int type, std::vector<int> const& slots) {
        auto& carried = mOpen.carried;
        if (isEmpty(carried)) return;
        if (type != 0 && type != 1 && !(type == 2 && mPlayer.isCreative())) return; // isValidQuickcraftType
        std::vector<int> chosen;
        for (int s : slots) {
            if (s < 0 || s >= size() || std::find(chosen.begin(), chosen.end(), s) != chosen.end()) continue;
            if (canQuickReplace(s, carried, true) && (type == 2 || count(carried) > static_cast<int>(chosen.size()))) chosen.push_back(s);
        }
        if (chosen.empty()) return;
        if (chosen.size() == 1) {
            click(chosen.front(), type, Click::Pickup);
            return;
        }
        ItemStack proto = carried;
        int       left  = count(carried);
        int       n     = static_cast<int>(chosen.size());
        for (int s : chosen) {
            if (!canQuickReplace(s, carried, true) || !(type == 2 || count(carried) >= n)) continue;
            int had = isEmpty(get(s)) ? 0 : count(get(s));
            int cap = std::min(maxStack(proto), slotMax(proto));
            int put = type == 0 ? static_cast<int>(std::floor(static_cast<float>(count(proto)) / static_cast<float>(n))) : type == 1 ? 1 : maxStack(proto);
            int now = std::min(put + had, cap);
            left -= now - had;
            set(s, withCount(proto, now));
        }
        carried = withCount(proto, left);
    }

private:
    static bool      isEmpty(ItemStack const& s) { return game::items::isEmpty(s); }
    static int       count(ItemStack const& s) { return game::items::countOf(s); }
    static ItemStack withCount(ItemStack const& s, int n) { return n <= 0 ? ItemStack{} : game::items::withCount(s, n); }
    static bool      same(ItemStack const& a, ItemStack const& b) { return game::items::sameItemSameData(a, b); }
    static int       maxStack(ItemStack const& s) { return isEmpty(s) ? 64 : std::max(1, static_cast<int>(s.getMaxStackSize())); }
    /// Slot#getMaxStackSize(stack): the container's and the stack's limit.
    static int       slotMax(ItemStack const& s) { return std::min(64, maxStack(s)); }

    [[nodiscard]] int inventorySlot(int i) const {
        int j = i - mContainerSlots;
        return j < 27 ? 9 + j : j - 27;
    }
    [[nodiscard]] std::pair<std::size_t, int> partOf(int i) const {
        for (std::size_t p = 0; p < mHandlers.size(); ++p) {
            if (i < mHandlers[p].slots) return {p, i};
            i -= mHandlers[p].slots;
        }
        return {mHandlers.size() - 1, i};
    }
    void setHand(int button, ItemStack const& s) {
        if (button == 40) mPlayer.setOffhandSlot(s);
        else mPlayer.getInventory().setItem(button, s);
    }
    void drop(ItemStack const& s) {
        if (!isEmpty(s)) mPlayer.drop(s, false); // Player#drop(stack, true): thrown ahead
    }

    /// AbstractContainerMenu#canItemQuickReplace.
    [[nodiscard]] bool canQuickReplace(int slot, ItemStack const& stack, bool stackSizeMatters) const {
        auto cur = get(slot);
        if (!isEmpty(cur) && same(stack, cur)) return count(cur) + (stackSizeMatters ? 0 : count(stack)) <= maxStack(stack);
        return isEmpty(cur);
    }
    /// Slot#safeInsert: what does not fit is returned.
    ItemStack safeInsert(int slot, ItemStack stack, int increment) {
        if (isEmpty(stack)) return stack;
        auto cur = get(slot);
        int  i   = std::min(std::min(increment, count(stack)), slotMax(stack) - (isEmpty(cur) ? 0 : count(cur)));
        if (i <= 0) return stack;
        if (isEmpty(cur)) set(slot, withCount(stack, i));
        else if (same(cur, stack)) set(slot, withCount(cur, count(cur) + i));
        else return stack;
        return withCount(stack, count(stack) - i);
    }
    /// Slot#tryRemove / safeTake: up to min(n, decrement) items out of the slot.
    ItemStack tryRemove(int slot, int n, int decrement) {
        auto cur = get(slot);
        if (isEmpty(cur)) return {};
        n = std::min({n, decrement, count(cur)});
        if (n <= 0) return {};
        set(slot, withCount(cur, count(cur) - n));
        return withCount(cur, n);
    }
    /// AbstractContainerMenu#moveItemStackTo over [start, end).
    bool moveItemStackTo(ItemStack& stack, int start, int end, bool reverse) {
        bool moved = false;
        if (maxStack(stack) > 1) { // isStackable
            for (int i = reverse ? end - 1 : start; !isEmpty(stack) && (reverse ? i >= start : i < end); i += reverse ? -1 : 1) {
                auto cur = get(i);
                if (isEmpty(cur) || !same(stack, cur)) continue;
                int j = count(cur) + count(stack), k = slotMax(cur);
                if (j <= k) {
                    stack = ItemStack{};
                    set(i, withCount(cur, j));
                    moved = true;
                } else if (count(cur) < k) {
                    stack = withCount(stack, count(stack) - (k - count(cur)));
                    set(i, withCount(cur, k));
                    moved = true;
                }
            }
        }
        if (!isEmpty(stack)) {
            for (int i = reverse ? end - 1 : start; reverse ? i >= start : i < end; i += reverse ? -1 : 1) {
                if (!isEmpty(get(i))) continue;
                int l = slotMax(stack);
                int n = std::min(count(stack), l);
                set(i, withCount(stack, n));
                stack = withCount(stack, count(stack) - n);
                moved = true;
                break;
            }
        }
        return moved;
    }
    /// ChestMenu#quickMoveStack: container slots go to the inventory from its end, inventory slots
    /// into the container from its start; returns the stack as it was (empty: nothing moved).
    ItemStack quickMoveStack(int index) {
        auto stack = get(index);
        if (isEmpty(stack)) return {};
        auto copy = stack;
        bool ok   = index < mContainerSlots ? moveItemStackTo(stack, mContainerSlots, size(), true) : moveItemStackTo(stack, 0, mContainerSlots, false);
        if (!ok) return {};
        set(index, stack);
        return copy;
    }

    Player&              mPlayer;
    OpenScreen&          mOpen;
    std::vector<Handler> mHandlers;
    int                  mContainerSlots = 0;
    bool                 mValid          = true;
};

/// Every slot the parts of a screen show, in slot order.
std::vector<ItemStack> partsSlots(std::vector<GlobalPos> const& parts) {
    std::vector<ItemStack> all;
    for (auto const& p : parts) {
        auto* region = regionFor(p);
        if (!region) continue;
        auto bp = game::blockPos(p);
        auto h  = find(*region, bp);
        if (!h) continue;
        auto s = slotsOf(p, *h, game::blockIdAt(*region, bp));
        all.insert(all.end(), s.begin(), s.end());
    }
    return all;
}

/// The parts of an open screen are all still there, in reach (ChestMenu#stillValid).
bool stillValid(Player& player, OpenScreen const& open) {
    if (open.parts.empty()) return false;
    for (auto const& p : open.parts) {
        auto* region = regionFor(p);
        if (!region || !inReach(player, p) || !find(*region, game::blockPos(p))) return false;
    }
    return true;
}

/// Forgets the chest screen the server opened for `player` (the stack on its cursor goes back to
/// it); `closeClient`: tells the client mod to close the screen too.
void closeRecord(Player& player, bool closeClient) {
    auto it = openScreens().find(player.getOrCreateUniqueID().rawID);
    if (it == openScreens().end()) return;
    ItemStack carried = it->second.carried;
    auto      parts   = it->second.parts;
    openScreens().erase(it);
    if (closeClient) screens::close(player, "inventory.chest");
    giveBack(player, carried);
    for (auto const& p : parts) recountViewers(p);
}

std::optional<Json> screenData(Player& player, Json const& ctx) {
    auto* open = openedFor(player, ctx);
    if (!open || !stillValid(player, *open)) {
        // not a container this player opened (or no longer valid): the screen framework closes the
        // player's native screen, so the record of the one it had open goes too
        closeRecord(player, false);
        return std::nullopt;
    }
    Json items = Json::array();
    auto all   = partsSlots(open->parts);
    int  shown = std::min(static_cast<int>(all.size()), open->rows * 9);
    for (int i = 0; i < shown; ++i) items.push_back(game::items::isEmpty(all[static_cast<std::size_t>(i)]) ? Json(nullptr) : game::items::toJson(all[static_cast<std::size_t>(i)]));
    return Json{
        {"title", trFor(player, open->title, open->parts.size() > 1 ? "Large Chest" : "Chest")},
        {"rows", open->rows},
        {"slots", shown},
        {"items", items},
        {"carried", game::items::isEmpty(open->carried) ? Json(nullptr) : game::items::toJson(open->carried)},
    };
}

Click clickTypeOf(std::string const& t) {
    if (t == "quick_move") return Click::QuickMove;
    if (t == "swap") return Click::Swap;
    if (t == "clone") return Click::Clone;
    if (t == "throw") return Click::Throw;
    if (t == "quick_craft") return Click::QuickCraft;
    if (t == "pickup_all") return Click::PickupAll;
    return Click::Pickup;
}

/// A click of the client mod's chest screen: {op: "click", slot (menu index, -999 outside),
/// button (0 left, 1 right; the hotbar slot / 40 for swap; the quick craft type), type (pickup,
/// quick_move, swap, clone, throw, quick_craft, pickup_all), slots (quick_craft)}. The short form
/// {op: click | inv_click, slot (container / inventory slot), button: left | right, shift} is a
/// pickup or quick move.
void screenAction(Player& player, Json const& ctx, Json const& action) {
    auto* opened = openedFor(player, ctx);
    if (!opened || !stillValid(player, *opened)) return; // only the container the server opened for this player
    Menu menu(player, *opened);
    if (!menu.valid()) return;
    auto op = action.value("op", std::string{});
    if (op != "click" && op != "inv_click") return;
    int  slot = action.value("slot", -1);
    auto b    = action.contains("button") ? action["button"] : Json(0);
    if (b.is_string()) {
        // the short form
        int button = b.get<std::string>() == "right" ? 1 : 0;
        if (op == "inv_click") {
            if (slot < 0 || slot >= 36) return;
            slot = menu.indexOfInventory(slot);
        } else if (slot < 0 || slot >= menu.containerSlots()) {
            return;
        }
        menu.click(slot, button, action.value("shift", false) ? Click::QuickMove : Click::Pickup);
    } else if (b.is_number_integer()) {
        auto type = clickTypeOf(action.value("type", std::string{"pickup"}));
        if (type == Click::QuickCraft) {
            std::vector<int> slots;
            for (auto const& s : action.value("slots", Json::array()))
                if (s.is_number_integer()) slots.push_back(s.get<int>());
            menu.quickCraft(b.get<int>(), slots);
        } else {
            menu.click(slot, b.get<int>(), type);
        }
    }
    player.sendInventory(false);
}

void screenClosed(Player& player, Json const&) { closeRecord(player, false); }

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
    formViewers().clear();
    openers().counts.clear();
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

// ---- vanilla clients without the game's container screen: forms --------------------------------------

/// The form fallback (more or fewer than 27 slots, double chests): a form listing the stacks (a
/// button takes one) and "Put in" (a second form lists the player's stacks; a button moves one
/// in, Java's quick move). The form counts as an open screen until it is closed.
void screenForm(Player& player, Json const& ctx);

void formClosed(Player& player) {
    auto it = formViewers().find(player.getOrCreateUniqueID().rawID);
    if (it == formViewers().end()) return;
    auto parts = it->second;
    formViewers().erase(it);
    for (auto const& p : parts) recountViewers(p);
}

void putInForm(Player& player, std::vector<GlobalPos> const& parts, Json const& ctx) {
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
    // one callback (button callbacks are ignored with it): a choice moves that stack in (into the
    // parts in order), closing the form goes back to the container's form
    form.sendTo(player, [parts, ctx, slots](Player& p, int choice, ll::form::FormCancelReason) {
        if (choice >= 0 && choice < static_cast<int>(slots.size())) {
            int   i    = slots[static_cast<std::size_t>(choice)];
            auto& pinv = p.getInventory();
            if (i < pinv.getContainerSize()) {
                auto cur = pinv.getItem(i);
                for (auto const& pos : parts) {
                    auto* r  = regionFor(pos);
                    auto  hh = r && inReach(p, pos) ? find(*r, game::blockPos(pos)) : std::nullopt;
                    if (!hh || game::items::isEmpty(cur)) continue;
                    cur = hh->insert(pos, cur, false); // what does not fit stays
                }
                if (game::items::countOf(cur) != game::items::countOf(pinv.getItem(i))) {
                    log().info("inventory: {} put {} into the container at {} {} {} (form)", p.getRealName(), game::items::idOf(pinv.getItem(i)), parts.front().x, parts.front().y, parts.front().z);
                    pinv.setItem(i, cur);
                    p.sendInventory(false);
                }
            }
        }
        screenForm(p, ctx);
    });
}

void screenForm(Player& player, Json const& ctx) {
    auto at = posOf(ctx);
    if (!at) return formClosed(player);
    auto parts = partsOf(*at);
    bool ok    = !parts.empty();
    for (auto const& p : parts) ok = ok && inReach(player, p);
    if (!ok) return formClosed(player);
    auto uid           = player.getOrCreateUniqueID().rawID;
    bool first         = !formViewers().contains(uid);
    formViewers()[uid] = parts;
    if (first)
        for (auto const& p : parts) recountViewers(p);
    ll::form::SimpleForm form;
    form.setTitle(trFor(player, ctx.value("title", std::string{"container.chest"}), parts.size() > 1 ? "Large Chest" : "Chest"));
    form.appendButton(trFor(player, "gf.combat.container.put_in", "Put in"));
    std::vector<std::pair<std::size_t, int>> buttons; // button 1.. -> (part, slot)
    int                                      used = 0;
    for (std::size_t part = 0; part < parts.size(); ++part) {
        auto* region = regionFor(parts[part]);
        auto  bp     = game::blockPos(parts[part]);
        auto  h      = region ? find(*region, bp) : std::nullopt;
        if (!h) continue;
        auto all = slotsOf(parts[part], *h, game::blockIdAt(*region, bp));
        for (int i = 0; i < static_cast<int>(all.size()); ++i) {
            auto const& s = all[static_cast<std::size_t>(i)];
            if (game::items::isEmpty(s)) continue;
            ++used;
            buttons.emplace_back(part, i);
            form.appendButton(game::items::displayName(s) + " x" + std::to_string(game::items::countOf(s)));
        }
    }
    form.setContent(used == 0 ? trFor(player, "gf.combat.container.empty", "(empty)") : std::string{});
    form.sendTo(player, [parts, buttons, ctx](Player& p, int choice, ll::form::FormCancelReason) {
        if (choice < 0) return formClosed(p); // closed: no longer an opener
        if (choice == 0) {
            if (inReach(p, parts.front())) putInForm(p, parts, ctx);
            else formClosed(p);
            return;
        }
        auto index = static_cast<std::size_t>(choice - 1);
        if (index < buttons.size()) {
            auto [part, slot] = buttons[index];
            auto const& pos   = parts[part];
            auto*       r     = regionFor(pos);
            auto        hh    = r && inReach(p, pos) ? find(*r, game::blockPos(pos)) : std::nullopt;
            if (hh) {
                auto taken = hh->extract(pos, slot, 64, false);
                // what the inventory has no room for goes back in; what the container has no room
                // for any more (another writer filled the slot meanwhile) drops at the player's feet
                if (!p.add(taken) && !game::items::isEmpty(taken)) giveBack(p, hh->insert(pos, taken, false));
                p.sendInventory(false);
            }
        }
        screenForm(p, ctx);
    });
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
/// An open proxy ends without touching the storage (it only mirrors it), so the storage's copy is
/// the one that drops.
void dropErased(GlobalPos const& pos, Json const& doc, std::string const& previousBlockId) {
    if (proxyOpen(pos)) proxyEnd(pos);
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
    // ChestBlock#useWithoutItem: the screen (refused while blocked), the stat
    ia::onUseOn({.block = blockId}, [](ia::UseOnContext const& c) {
        auto d = declared(c.blockId);
        if (!d) return ia::Result::Pass;
        // Java (ServerPlayerGameMode#useItemOn): sneaking with an item in either hand skips the
        // block's use (the item places / is used)
        bool emptyHands = c.itemId.empty() && game::items::isEmpty(c.player.getOffhandSlot());
        if (c.sneaking && !emptyHands) return ia::Result::Pass;
        if (!c.firstEvent) return ia::Result::Consume;
        auto h = handlerFor(c.blockId);
        if (h && h->open && h->open(c.player, c.pos)) chestOpenedBy(c.player, c.pos);
        return ia::Result::Consume;
    });
    // Java: breaking unpacks the loot with the breaker first; the contents drop when the block
    // goes (dropErased for BlockData, the block actor's onRemoved for native containers)
    ia::onDestroy({.block = blockId}, [](ia::DestroyContext const& c) {
        if (declared(c.blockId)) unpackLoot(c.pos, &c.player);
        return ia::Result::Pass;
    });
}

Declared parseDeclared(Json const& e) {
    Declared d;
    d.block       = e.value("block", std::string{});
    d.rows        = std::clamp(e.value("rows", 3), 1, 6);
    d.slots       = e.value("slots", d.rows * 9);
    d.title       = e.value("title", std::string{"container.chest"});
    d.skin        = e.value("skin", std::string{});
    d.chest       = e.value("chest", true);
    d.pairs       = e.value("pairs", false);
    d.doubleTitle = e.value("double_title", d.doubleTitle);
    if (auto s = e.find("sounds"); s != e.end()) {
        if (s->is_null()) d.openSound = d.closeSound = {};
        else if (s->is_object()) {
            d.openSound  = s->value("open", std::string{});
            d.closeSound = s->value("close", std::string{});
        }
    }
    d.stat       = e.value("stat", d.stat);
    d.comparator = e.value("comparator", d.comparator);
    return d;
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
                auto d = parseDeclared(e);
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

std::optional<Handler> rawHandlerFor(std::string const& blockId) {
    std::lock_guard lock(store().mutex);
    auto            it = store().handlers.find(blockId);
    if (it == store().handlers.end()) return std::nullopt;
    return it->second;
}

std::optional<Handler> handlerFor(std::string const& blockId) {
    auto raw = rawHandlerFor(blockId);
    if (!raw) return std::nullopt;
    return routed(std::move(*raw));
}

void unregisterBlock(std::string const& blockId) {
    bool analog;
    {
        std::lock_guard lock(store().mutex);
        store().handlers.erase(blockId);
        store().declared.erase(blockId);
        store().defaultStorage.erase(blockId);
        analog = store().analogFor.erase(blockId) > 0;
    }
    if (analog) redstone::unregisterAnalog(blockId);
}

void declare(Declared const& d) {
    bool hadAnalog;
    {
        std::lock_guard lock(store().mutex);
        store().declared[d.block] = d;
        hadAnalog = d.comparator ? !store().analogFor.insert(d.block).second : store().analogFor.erase(d.block) > 0;
    }
    Handler h;
    h.slots = d.slots;
    registerBlock(d.block, h);
    ensureInteractions(d.block);
    // ChestBlock#hasAnalogOutputSignal / getAnalogOutputSignal: vanilla comparators read it through
    // the redstone contract's comparator hook
    if (d.comparator && !hadAnalog)
        redstone::registerAnalog(d.block, [](BlockSource& region, BlockPos const& bp, Block const&) {
            return comparatorSignal(game::globalPos(region, bp));
        });
    else if (!d.comparator && hadAnalog) redstone::unregisterAnalog(d.block);
}

std::optional<Declared> declared(std::string const& blockId) {
    std::lock_guard lock(store().mutex);
    auto            it = store().declared.find(blockId);
    if (it == store().declared.end()) return std::nullopt;
    return it->second;
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
    auto h = rawHandlerFor(game::blockIdAt(*region, game::blockPos(pos)));
    if (!h) return false;
    // RandomizableContainer#unpackLootTable: THIS_ENTITY is the opener, with the opener's luck;
    // no killer player (killed_by_player fails)
    loot::LootContext ctx;
    ctx.region     = region;
    ctx.origin     = Vec3{static_cast<float>(pos.x) + .5f, static_cast<float>(pos.y) + .5f, static_cast<float>(pos.z) + .5f};
    ctx.thisEntity = opener;
    ctx.luck       = loot::luckOf(opener);
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
    if (proxyOpen(pos)) proxyPull(pos);
    log().info("inventory: unpacked {} at {} {} {}", pending->first, pos.x, pos.y, pos.z);
    return true;
}

std::vector<GlobalPos> partsOf(GlobalPos const& pos) {
    auto* region = regionFor(pos);
    if (!region) return {};
    auto bp = game::blockPos(pos);
    auto id = game::blockIdAt(*region, bp);
    if (!rawHandlerFor(id)) return {};
    auto d = declared(id);
    if (!d || !d->pairs) return {pos};
    auto half = halfOf(region->getBlock(bp));
    if (half.type == ChestType::Single) return {pos};
    auto other = step(bp, connectedDirection(half));
    if (!region->hasChunksAt(other, 0, false) || game::blockIdAt(*region, other) != id) return {pos};
    auto partner = halfOf(region->getBlock(other));
    bool paired  = partner.facing == half.facing && partner.type != ChestType::Single && partner.type != half.type;
    if (!paired) return {pos};
    auto otherPos = game::globalPos(*region, other);
    // DoubleBlockCombiner: the right half is FIRST (the upper 27 slots)
    return half.type == ChestType::Right ? std::vector<GlobalPos>{pos, otherPos} : std::vector<GlobalPos>{otherPos, pos};
}

bool withinReach(Player& player, GlobalPos const& pos) { return inReach(player, pos); }

std::string chestFacingOf(GlobalPos const& pos) {
    auto* region = regionFor(pos);
    if (!region) return "north";
    switch (halfOf(region->getBlock(game::blockPos(pos))).facing) {
    case 3: return "south";
    case 4: return "west";
    case 5: return "east";
    default: return "north";
    }
}

void chestOpenedBy(Player& player, GlobalPos const& pos) {
    auto* region = regionFor(pos);
    auto  d      = region ? declared(game::blockIdAt(*region, game::blockPos(pos))) : std::nullopt;
    if (!d || !d->chest) return;
    // ChestBlock#useWithoutItem once the menu opened: player.awardStat(getOpenChestStat()),
    // PiglinAi.angerNearbyPiglins(player, true)
    if (!d->stat.empty()) stats::award(player, d->stat);
    (void)angerNearbyPiglins(player, true);
}

int angerNearbyPiglins(Player& player, bool angerOnlyIfCanSee) {
    // Sensor#isEntityAttackableIgnoringLineOfSight: no anger at a creative or spectator player
    if (player.isClientSide() || !player.isAlive() || player.isCreative() || player.isSpectator()) return 0;
    auto& region = player.getDimensionBlockSource();
    auto  box    = player.getAABB();
    // player.getBoundingBox().inflate(16.0), getEntitiesOfClass(Piglin.class): piglins, not brutes
    AABB  area{box.min - Vec3{16.f, 16.f, 16.f}, box.max + Vec3{16.f, 16.f, 16.f}};
    auto  eyes  = player.getEyePos();
    auto  level = ll::service::getLevel();
    int   n     = 0;
    for (auto* a : region.getEntities(area, 0.0f)) {
        if (!a || !a->isType(ActorType::Piglin) || !a->isAlive() || a->isBaby()) continue;
        auto& piglin = static_cast<Mob&>(*a);
        // PiglinAi#isIdle: not fighting (a target), admiring an item or celebrating
        ActorUniqueID target = piglin.mTargetId;
        if (target.rawID != ActorUniqueID::INVALID_ID().rawID && level && level->fetchEntity(target, false)) continue;
        if (piglin.getStatusFlag(ActorFlags::Admiring) || piglin.getStatusFlag(ActorFlags::Celebrating) || piglin.getStatusFlag(ActorFlags::CelebratingSpecial)) continue;
        // BehaviorUtils#canSee: the player is one of the piglin's visible entities
        if (angerOnlyIfCanSee && !piglin.canSee(eyes, ShapeType::Collision)) continue;
        // PiglinAi#setAngerTarget (ANGRY_AT for 600 ticks): the target; Bedrock's piglin turns
        // angry on a new target (minecraft:on_target_acquired, its 30 s minecraft:angry)
        piglin.setTarget(&player);
        if (!piglin.getEntityContext().hasComponent<AngryComponent>()) {
            VariantParameterList params{};
            params.mSelf = &piglin;
            (void)piglin.executeEvent("become_angry_event", params);
        }
        ++n;
    }
    if (n) log().info("inventory: {} angered {} piglin(s) by opening a chest", player.getRealName(), n);
    return n;
}

bool chestBlocked(GlobalPos const& pos) {
    auto* region = regionFor(pos);
    if (!region) return false;
    auto d = declared(game::blockIdAt(*region, game::blockPos(pos)));
    if (!d || !d->chest) return false;
    for (auto const& p : partsOf(pos))
        if (blockedAt(*region, game::blockPos(p))) return true;
    return false;
}

std::optional<int> comparatorSignal(GlobalPos const& pos) {
    auto* region = regionFor(pos);
    if (!region) return std::nullopt;
    auto id = game::blockIdAt(*region, game::blockPos(pos));
    auto d  = declared(id);
    if (!d || !d->comparator) return std::nullopt;
    // a comparator's circuit component asks whenever it is evaluated: one reading per position and
    // tick (Java's comparators read the container once per scheduled tick too)
    static std::uint64_t            memoTick = std::numeric_limits<std::uint64_t>::max();
    static std::map<GlobalPos, int> memo;
    auto                            level = ll::service::getLevel();
    std::uint64_t                   now   = level ? static_cast<std::uint64_t>(level->getCurrentTick().tickID) : 0;
    if (now != memoTick) {
        memo.clear();
        memoTick = now;
    }
    if (auto it = memo.find(pos); it != memo.end()) return it->second;
    int signal = 0;
    // ChestBlock#getContainer(state, level, pos, false): none while either half is blocked (0)
    if (!(d->chest && chestBlocked(pos))) {
        // AbstractContainerMenu#getRedstoneSignalFromContainer over the (compound) container
        float f    = 0.f;
        int   size = 0;
        for (auto const& p : partsOf(pos)) {
            auto* r = regionFor(p);
            auto  h = r ? rawHandlerFor(game::blockIdAt(*r, game::blockPos(p))) : std::nullopt;
            if (!h) continue;
            size += h->slots;
            // RandomizableContainer#getItem unpacks a pending table first (as the screen does)
            for (auto const& s : slotsOf(p, *h, id)) {
                if (game::items::isEmpty(s)) continue;
                // Container#getMaxStackSize(stack): min(99, the stack's own limit)
                int max = std::clamp(static_cast<int>(s.getMaxStackSize()), 1, 99);
                f += static_cast<float>(game::items::countOf(s)) / static_cast<float>(max);
            }
        }
        if (size > 0) {
            f /= static_cast<float>(size);
            // Mth.lerpDiscrete(f, 0, 15)
            signal = static_cast<int>(std::floor(f * 14.f)) + (f > 0.f ? 1 : 0);
        }
    }
    memo[pos] = signal;
    return signal;
}

int viewerCount(GlobalPos const& pos) {
    // (no std::find: MSVC's vectorised find refuses a 16-byte element)
    auto has = [&](std::vector<GlobalPos> const& parts) { return std::any_of(parts.begin(), parts.end(), [&](GlobalPos const& p) { return p == pos; }); };
    std::set<std::int64_t> players;
    for (auto const& [uid, open] : openScreens())
        if (open.valid && has(open.parts)) players.insert(uid);
    for (auto const& [uid, parts] : formViewers())
        if (has(parts)) players.insert(uid);
    for (auto uid : proxyViewers(pos)) players.insert(uid);
    return static_cast<int>(players.size());
}

void recountViewers(GlobalPos const& pos) {
    auto& o      = openers();
    int   now    = viewerCount(pos);
    auto  it     = o.counts.find(pos);
    int   before = it == o.counts.end() ? 0 : it->second;
    if (now == before) return;
    if (now == 0) o.counts.erase(pos);
    else o.counts[pos] = now;
    auto* region = regionFor(pos);
    if (!region || !region->hasChunksAt(game::blockPos(pos), 0, false)) return;
    auto id = game::blockIdAt(*region, game::blockPos(pos));
    if (auto d = declared(id)) {
        if (before == 0 && now > 0) playOpenerSound(pos, *d, true);
        else if (before > 0 && now == 0) playOpenerSound(pos, *d, false);
    }
    std::vector<ViewersFn> fns;
    if (auto l = o.listeners.find(id); l != o.listeners.end()) fns = l->second;
    for (auto const& fn : fns) {
        try {
            fn(pos, now);
        } catch (std::exception const& ex) {
            log().error("inventory: viewers listener failed: {}", ex.what());
        }
    }
}

std::vector<GlobalPos> countedPositions() {
    std::vector<GlobalPos> out;
    for (auto const& [pos, _] : openers().counts) out.push_back(pos);
    return out;
}

SoundCounters soundCounters() { return openers().sounds; }

std::vector<GlobalPos> openedParts(Player& player) {
    auto it = openScreens().find(player.getOrCreateUniqueID().rawID);
    return it == openScreens().end() || !it->second.valid ? std::vector<GlobalPos>{} : it->second.parts;
}

ItemStack carriedOf(Player& player) {
    auto it = openScreens().find(player.getOrCreateUniqueID().rawID);
    return it == openScreens().end() ? ItemStack{} : it->second.carried;
}

// ---- contract extension (feature/ext/combat/InventoryViewers.h) -----------------------------------

int viewers(GlobalPos const& pos) { return viewerCount(pos); }

void onViewersChanged(std::string const& blockId, ViewersFn fn) { openers().listeners[blockId].push_back(std::move(fn)); }

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
    auto d = declared(game::blockIdAt(*region, bp));
    // ChestBlock#getMenuProvider: no screen while either half is blocked
    if (chestBlocked(pos)) {
        log().info("inventory: the container at {} {} {} is blocked", pos.x, pos.y, pos.z);
        return false;
    }
    auto parts = partsOf(pos);
    for (auto const& p : parts) unpackLoot(p, &player);
    std::string title = titleKey;
    if (parts.size() > 1) {
        rows  = 6;
        title = d ? d->doubleTitle : "container.chestDouble";
    }
    rows = std::clamp(rows, 1, 6);
    std::string skin = d ? d->skin : std::string{};
    auto        uid  = player.getOrCreateUniqueID().rawID;
    // a screen open for another container closes first (one menu per player)
    closeRecord(player, false);
    // Companions: the client mod's chest screen. Vanilla clients: the game's chest screen through
    // a chest minecart proxy (VanillaContainer.cpp; the client shows no screen for a custom block's
    // own container, spike S3): a 27-slot container as the minecart's chest screen, a double chest
    // as the game's large chest screen of a client-side chest pair; the form for other sizes (or
    // when no proxy can be made). The title of the vanilla screen goes through F12's container
    // title skin convention (Declared::skin).
    bool companion = net::isCompanion(player);
    if (!companion) {
        auto text = trFor(player, title, parts.size() > 1 ? "Large Chest" : "Chest");
        if (!skin.empty()) text = ui::containerTitleSkin(text, skin);
        if (openVanillaContainer(player, pos, text)) {
            for (auto const& p : parts) recountViewers(p);
            return true;
        }
    }
    Json ctx{{"pos", {pos.dim, pos.x, pos.y, pos.z}}, {"rows", rows}, {"title", title}};
    bool ok = true;
    if (companion) {
        // the server's record of what this player may see and change (screenData / screenAction)
        auto& open = openScreens()[uid];
        open.pos   = pos;
        open.parts = parts;
        open.rows  = rows;
        open.title = title;
        open.valid = true;
        ok         = screens::open(player, "inventory.chest", ctx);
        if (!ok) openScreens().erase(uid); // a refused screen leaves no record
    } else {
        screenForm(player, ctx); // the form (it counts as an opener while it is up)
    }
    for (auto const& p : parts) recountViewers(p);
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

// A native container's block is removed: its slots drop (Java's Containers#dropContents). An open
// proxy ends first without touching the slots (it only mirrors them); the slots are emptied before
// the game's own removal runs, so nothing can drop twice.
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
    if (c->where && gf::inventory::proxyOpen(*c->where)) gf::inventory::proxyEnd(*c->where);
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

void onDisconnect(Player& player) {
    screenClosed(player, Json::object());
    formClosed(player);
}

/// Every tick (Player#tick: an invalid menu closes): the client mod's screens whose container is
/// gone or out of reach close, and the openers of every counted position are counted again.
void onTick() {
    auto level = ll::service::getLevel();
    if (!level) return;
    std::vector<std::int64_t> gone;
    for (auto& [uid, open] : openScreens()) {
        ActorUniqueID id;
        id.rawID = uid;
        auto* p  = level->getPlayer(id);
        if (!p || !open.valid || !stillValid(*p, open)) gone.push_back(uid);
    }
    for (auto uid : gone) {
        ActorUniqueID id;
        id.rawID = uid;
        if (auto* p = level->getPlayer(id)) closeRecord(*p, true);
        else openScreens().erase(uid); // left: the disconnect returned the cursor stack
    }
    // a form left open by a player who went away no longer counts (its buttons check the reach)
    std::erase_if(formViewers(), [&](auto const& e) {
        ActorUniqueID id;
        id.rawID = e.first;
        auto* p  = level->getPlayer(id);
        return !p || std::any_of(e.second.begin(), e.second.end(), [&](GlobalPos const& pos) { return !inReach(*p, pos); });
    });
    for (auto const& pos : positionsWithOpeners()) recountViewers(pos);
}

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
        auto& bus = ll::event::EventBus::getInstance();
        // a player who leaves with a stack on the chest screen's cursor gets it back first; one
        // kept from a world that closed comes back on the next join
        mDisconnect = bus.emplaceListener<ll::event::PlayerDisconnectEvent>([](ll::event::PlayerDisconnectEvent& ev) { onDisconnect(ev.self()); });
        mJoin       = bus.emplaceListener<ll::event::PlayerJoinEvent>([](ll::event::PlayerJoinEvent& ev) {
            if (!ev.self().isClientSide()) returnStoredCarried(ev.self());
        });
        mTick = bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { onTick(); });
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
        auto& bus = ll::event::EventBus::getInstance();
        bus.removeListener(mDisconnect);
        bus.removeListener(mJoin);
        bus.removeListener(mTick);
        InventoryNativeRemovedHook::unhook();
        if (mHooked) InventoryNativeContainerHook::unhook();
        mHooked = false;
    }

private:
    bool                   mHooked = false;
    ll::event::ListenerPtr mDisconnect;
    ll::event::ListenerPtr mJoin;
    ll::event::ListenerPtr mTick;
};

} // namespace
} // namespace gf::inventory

GF_REGISTER_MODULE_ORDERED(gf::inventory::InventoryModule, gf::ModuleOrder::Contracts);
