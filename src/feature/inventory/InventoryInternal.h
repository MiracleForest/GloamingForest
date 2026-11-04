#pragma once

// F10-internal parts of gf::inventory (the native container backend, the vanilla chest screen
// proxy, loot, selftests).

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/world/level/BlockPos.h"

#include "feature/inventory/Inventory.h"

class BlockActor;
class Container;

namespace gf::inventory {

/// A chest-like container declared as data (<mod>/data/gf/containers/<u>.json) or in code.
struct Declared {
    std::string block;
    int         rows  = 3;
    int         slots = 27;
    std::string title; ///< translation key
    std::string skin;  ///< JSON-UI / native skin id ("" = vanilla chest)
    /// ChestBlock's rules: no screen while a redstone conductor or a sitting cat is above
    /// (ChestBlock#isChestBlockedAt), the opening stat (`stat`).
    bool        chest = true;
    /// Double chests: a half whose chest type state is left / right opens with its partner as one
    /// 54-slot container (ChestBlock#combine: the right half first), titled `doubleTitle`.
    bool        pairs = false;
    std::string doubleTitle = "container.chestDouble";
    /// Played at the first opener / after the last one (ChestBlockEntity's ContainerOpenersCounter);
    /// "" = silent.
    std::string openSound  = "minecraft:block.chest.open";
    std::string closeSound = "minecraft:block.chest.close";
    /// Awarded to the opener (Stats.OPEN_CHEST; TrappedChestBlock: minecraft:trigger_trapped_chest).
    std::string stat = "minecraft:open_chest";
    /// Comparator output (redstone::registerAnalog): Java's getAnalogOutputSignal of a container
    /// block, AbstractContainerMenu#getRedstoneSignalFromContainer over its slots (a double chest's
    /// halves together; 0 while a chest is blocked: ChestBlock#getContainer finds none).
    bool comparator = true;
};

/// The registered handler of `blockId` with its defaults filled (nullopt: not a container). Its
/// writes reach an open vanilla chest screen proxy at once (`find` returns the same).
[[nodiscard]] std::optional<Handler> handlerFor(std::string const& blockId);
/// The handler as registered (defaults filled), without the proxy routing: what the proxy itself
/// syncs with.
[[nodiscard]] std::optional<Handler> rawHandlerFor(std::string const& blockId);
/// Removes a registration (selftests).
void unregisterBlock(std::string const& blockId);
/// Declares a chest-like container (what the data files do): storage, opening on use, contents
/// dropped when broken.
void declare(Declared const& d);
/// The declaration of `blockId` (nullopt: none).
[[nodiscard]] std::optional<Declared> declared(std::string const& blockId);

/// The game container a data-driven block actor carries for `pos` (the native backend; nullptr
/// when the block has no `minecraft:block_entity` or is not a registered container).
[[nodiscard]] Container* nativeContainer(BlockSource& region, BlockPos const& pos);
/// Spike S3: asks the server to open the game's container screen of the injected container at
/// `pos` as ContainerType `containerType` (0 Container, 37 DataDrivenContainer); the ContainerID or -1.
int openNativeScreen(Player& player, GlobalPos const& pos, int containerType);
/// Every slot of the container at `pos` (empty when none), read once.
[[nodiscard]] std::vector<ItemStack> contents(GlobalPos const& pos);
/// Rolls a pending loot table of the container at `pos` into it (RandomizableContainer#unpackLootTable).
bool unpackLoot(GlobalPos const& pos, Player* opener);
/// The pending loot table at `pos` (selftests).
[[nodiscard]] std::optional<std::pair<std::string, std::int64_t>> pendingLoot(GlobalPos const& pos);

// ---- the halves of a container (double chests) ------------------------------------------------------

/// The container positions a screen of `pos` shows, in slot order: {pos}, or for a paired half of
/// a declared double chest {right half, left half} (ChestBlock#combine). Empty when `pos` is not a
/// container.
[[nodiscard]] std::vector<GlobalPos> partsOf(GlobalPos const& pos);
/// ChestBlock#isChestBlockedAt for every part of a declared chest: a redstone conductor or a
/// sitting cat right above.
[[nodiscard]] bool chestBlocked(GlobalPos const& pos);
/// Container#stillValidBlockEntity's reach: the player's eye within its block interaction range + 4
/// of the block (every screen route uses it).
[[nodiscard]] bool withinReach(Player& player, GlobalPos const& pos);
/// The horizontal facing of the chest-like block at `pos` ("north", "south", "west", "east"; its
/// facing / cardinal_direction state, "north" without one).
[[nodiscard]] std::string chestFacingOf(GlobalPos const& pos);
/// `player` opened the declared chest at `pos` (ChestBlock#useWithoutItem once its menu opened):
/// the opening stat is awarded and the nearby piglins are angered (angerNearbyPiglins(player,
/// true)). Nothing for a declared container that is not a chest (Declared::chest false).
void chestOpenedBy(Player& player, GlobalPos const& pos);
/// PiglinAi#angerNearbyPiglins (ChestBlock#useWithoutItem when a declared chest opened): the idle
/// adult piglins (not brutes) whose box meets the player's box inflated by 16 and, with
/// `angerOnlyIfCanSee`, that see the player take the player as their target (Bedrock's piglin then
/// turns angry for 30 s: Java's ANGRY_AT for 600 ticks). Java has no gold armour exception here;
/// creative and spectator players anger none (Sensor#isEntityAttackableIgnoringLineOfSight). Baby
/// piglins are skipped: Bedrock's have no attack and would only pass the anger on. Returns how many.
int angerNearbyPiglins(Player& player, bool angerOnlyIfCanSee);
/// The comparator output of the declared container at `pos` (Declared::comparator; nullopt: not a
/// declared container with one), as redstone::analogOutput reads it.
[[nodiscard]] std::optional<int> comparatorSignal(GlobalPos const& pos);

// ---- vanilla clients: the game's chest screen through a chest minecart (VanillaContainer.cpp) ------

/// Vanilla clients: the game's own chest screen for the 27-slot container at `pos`, titled
/// `title`, through an invisible chest minecart; for a paired half of a double chest the game's
/// large chest screen of both halves (54 slots, the right half first) on a chest pair of the
/// player's client (VanillaContainer.cpp). While it is open the proxy and the storage hold the same
/// slots: the screen's changes are written to the storage as they happen, every write through the
/// handler (`find`) reaches the proxy at once, and other code's changes behind the handler are
/// pulled in before each item request of a viewer and every tick (the storage wins a slot changed
/// on both sides). A player already in the session whose screen closed opens it again. False when
/// a part is not 27 slots, the player is out of reach or the proxy (or the chest pair's place)
/// cannot be made: the caller shows the form.
bool openVanillaContainer(Player& player, GlobalPos const& pos, std::string const& title);
/// The position of the client-side chest pair (its lead chest) a double chest's large chest
/// screen opens on (nullopt: no double chest session shows `pos`). Selftests.
[[nodiscard]] std::optional<BlockPos> vanillaProxyChestPair(GlobalPos const& pos);
/// A viewer's client sent an item request (ItemStackRequestPacket): the proxies it looks at take in
/// the storage's slots first (the storage wins a slot changed on both sides).
void proxyBeforeScreenRequest(Player& player);
/// Container proxies open now (selftests).
[[nodiscard]] int vanillaProxyCount();
/// The proxy's container for `pos` (selftests; nullptr when none is open).
[[nodiscard]] Container* vanillaProxyContainer(GlobalPos const& pos);
/// The proxy entity of `pos` (selftests; nullptr when none is open).
[[nodiscard]] Actor* vanillaProxyActor(GlobalPos const& pos);
/// Whether a proxy is open for `pos`.
[[nodiscard]] bool proxyOpen(GlobalPos const& pos);
/// The storage of `pos` now holds `stack` in `slot`: an open proxy shows it at once.
void proxyMirror(GlobalPos const& pos, int slot, ItemStack const& stack);
/// The storage of `pos` changed (several slots): an open proxy is refreshed from it at once.
void proxyPull(GlobalPos const& pos);
/// Ends the proxy session of `pos` now without touching the storage (the block is gone: the
/// storage's copy drops, the proxy is emptied).
void proxyEnd(GlobalPos const& pos);
/// The players (ActorUniqueID) with the proxy screen of `pos` open or opening.
[[nodiscard]] std::set<std::int64_t> proxyViewers(GlobalPos const& pos);

// ---- openers (ContainerOpenersCounter) ---------------------------------------------------------------

/// The number of players with a screen of `pos` open (the client mod's screen, the proxy, the form).
[[nodiscard]] int viewerCount(GlobalPos const& pos);
/// Counts the openers of `pos` again (a screen opened or closed): the open / close sounds of a
/// declared container at the first opener and after the last one, and the viewer listeners.
void recountViewers(GlobalPos const& pos);
/// The positions whose openers are counted now (selftests).
[[nodiscard]] std::vector<GlobalPos> countedPositions();
/// Open / close sounds played so far (selftests).
struct SoundCounters {
    int opened = 0;
    int closed = 0;
};
[[nodiscard]] SoundCounters soundCounters();

// ---- the client mod's chest screen -----------------------------------------------------------------

/// The container screen the server opened for `player` (its parts; empty: none). Selftests.
[[nodiscard]] std::vector<GlobalPos> openedParts(Player& player);
/// The stack on the cursor of `player`'s chest screen (selftests).
[[nodiscard]] ItemStack carriedOf(Player& player);
/// The client mod chest screens refreshed so far because a container they show changed
/// (feature/ext/combat/InventoryChanges.h; selftests).
[[nodiscard]] std::uint64_t screenRefreshes();

} // namespace gf::inventory
