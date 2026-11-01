// Adapted from LiteLDev/Levitate v0.1.0 (src/feature/common/Interactions.h), LGPL-3.0-or-later.
#pragma once

// Contract "interactions" (W0a): shared, additive dispatch of player interactions (rule R8).
//
// Every unit that reacts to right-clicks, item uses, block breaking or entity clicks registers a
// handler here instead of hooking GameMode itself (with separate hooks the first hook wins and the
// others never see the click). One hook per GameMode action calls the matching handlers by
// priority (higher first, then registration order) until one returns something other than `Pass`.
//
//     namespace ia = gf::feature::common::interactions;
//     ia::onUseOn({.block = "twilightforest:uncrafting_table"}, [](ia::UseOnContext const& c) {
//         ...;
//         return ia::Result::Consume;
//     });
//     ia::onUseOn(ia::Match::tag("twilightforest:mazestone"), mazebreakerRule);   // block tag
//     ia::onUse({.item = "twilightforest:ore_magnet"}, [](ia::UseContext const& c) { ...; return ia::Result::Consume; });
//
// - Server thread only; called for server players (network players and SimulatedPlayers alike),
//   never for the client's own prediction.
// - `Consume`: handled; vanilla does not run (no placement, no item use), the arm swings and the
//   player's inventory is resent (it undoes the client's prediction). `Deny`: like Consume
//   without the success (refused action). `Pass`: the next handler, then vanilla.
// - Landmark protection (F6) registers at `priority::Highest` for every block and entity.
// - Bedrock repeats use-on while the button is held (`firstEvent == false` for the repeats):
//   handlers that act once per click return `Consume` for the repeats too, without acting.
// - Handlers may edit the world directly (unlike a cancelled PlayerInteractBlockEvent, nothing
//   is rolled back), but must not keep `Player&` past the call; defer world edits that must not
//   happen inside the click with `nextTick`.
// - Tag matchers (`Match::tag`, `Match::itemTag`) ask gf::tags (feature/tags/Tags.h): block tags
//   for block events, entity type tags for entity events, item tags for the held item.
// - `onPlace`: after vanilla placed a block from a use-on that no handler took (orienting the
//   new block, Java's getStateForPlacement / setPlacedBy with the placer).
// - `onStartDestroy`: the first hit on a block: `Consume` / `Deny` stop the mining.
// - `onInteract` / `onAttack`: right-click / left-click on an entity; `Match::block` is the
//   entity's type id there.
// - `onUnusedUseOn`: after vanilla's use-on did not succeed (no block took the click and nothing
//   was placed): Java's Item#useOn that runs only when the block passes.
// - `onDestroyed` / `onAttacked`: after vanilla's destroyBlock / attack ran, with its result.
// - `UseContext::onBlock`: the item use comes from a use-on that no block took (Bedrock calls the
//   item use from there too); units that only want right-clicks in the air return `Pass` then.
// - `mutableItem(ctx.item)`: the held stack is the player's hand slot; handlers that change it in
//   place (counts, item data) use this instead of a copy.

#include <array>
#include <functional>
#include <string>

#include "core/GlobalPos.h"

class Actor;
class Block;
class ItemStack;
class Player;

namespace gf::feature::common::interactions {

using core::GlobalPos;
/// A point in block-local coordinates (0..1 on each axis inside the block) or world coordinates.
using Vec = std::array<double, 3>;

enum class Result { Pass, Consume, Deny };

namespace priority {
inline constexpr int Highest  = 1000; ///< landmark protection (F6)
inline constexpr int High     = 100;
inline constexpr int Normal   = 0;
inline constexpr int Low      = -100;
inline constexpr int Fallback = -1000; ///< generic behaviour
} // namespace priority

/// Which interactions a handler wants; an empty field matches anything ("" item = also an empty hand).
struct Match {
    std::string block;            ///< block id (entity type id for entity events)
    std::string item;             ///< held item id; use `emptyHand` for an empty hand only
    bool        emptyHand = false; ///< only when the hand is empty
    std::string blockTag;         ///< block tag (entity type tag for entity events), without '#'
    std::string itemTag;          ///< item tag of the held item, without '#'

    /// Matches blocks (entities in entity events) of tag `tagId` ("twilightforest:mazestone").
    [[nodiscard]] static Match tag(std::string tagId) {
        Match m;
        m.blockTag = std::move(tagId);
        return m;
    }
    /// Matches a held item of tag `tagId`.
    [[nodiscard]] static Match itemTagged(std::string tagId) {
        Match m;
        m.itemTag = std::move(tagId);
        return m;
    }
};

/// Right-click on a block (GameMode::useItemOn).
struct UseOnContext {
    Player&            player;
    ItemStack const&   item;   ///< the held stack (may be empty)
    std::string const& itemId; ///< "" for an empty hand
    Block const&       block;
    std::string const& blockId;
    GlobalPos          pos;
    int                face; ///< FacingID: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east
    Vec                hit;  ///< block-local click position (0..1)
    bool               sneaking;
    bool               firstEvent;      ///< false for the repeats while the button is held
    bool               offhand = false; ///< the off-hand's use-on (`item` is the off-hand stack)
};

/// Using an item (GameMode::baseUseItem): a right-click in the air, or on a block that did not
/// take the click (vanilla use-on passed).
struct UseContext {
    Player&            player;
    ItemStack const&   item;
    std::string const& itemId;
    bool               sneaking;
    bool               onBlock = false; ///< called from a use-on that no block took
};

/// A player breaks a block (GameMode::destroyBlock). `Consume` / `Deny`: vanilla does not break it.
struct DestroyContext {
    Player&            player;
    ItemStack const&   item;
    std::string const& itemId;
    Block const&       block;
    std::string const& blockId;
    GlobalPos          pos;
    int                face;
    bool               sneaking;
};

/// A block placed by vanilla from a use-on that every handler passed.
struct PlaceContext {
    Player&            player;
    std::string const& itemId;  ///< the held item before the placement used it
    Block const&       block;   ///< the placed block
    std::string const& blockId; ///< its id
    GlobalPos          pos;     ///< where it was placed
    GlobalPos          clicked; ///< the clicked block (== pos when that was replaceable)
    int                face;    ///< the clicked face
    Vec                hit;     ///< click position, local to the clicked block (0..1)
    bool               sneaking;
    ItemStack const*   itemBefore = nullptr; ///< a copy of the held stack before the placement
};

/// Right- / left-click on an entity.
struct EntityContext {
    Player&            player;
    ItemStack const&   item;
    std::string const& itemId;
    Actor&             entity;
    std::string const& entityId; ///< the entity's type id ("twilightforest:quest_ram")
    Vec                at;       ///< world position of the click
    bool               sneaking;
};

using UseOnHandler     = std::function<Result(UseOnContext const&)>;
using UseHandler       = std::function<Result(UseContext const&)>;
using DestroyHandler   = std::function<Result(DestroyContext const&)>;
using PlaceHandler     = std::function<void(PlaceContext const&)>;
using EntityHandler    = std::function<Result(EntityContext const&)>;
using DestroyedHandler = std::function<void(DestroyContext const&, bool destroyed)>;
using AttackedHandler  = std::function<void(EntityContext const&, bool hit)>;
using HandlerId        = int;

/// The held stack of a context as the mutable hand slot it is (use-on, use, destroy, entity).
inline ItemStack& mutableItem(ItemStack const& contextItem) { return const_cast<ItemStack&>(contextItem); }

HandlerId onUseOn(Match match, UseOnHandler fn, int priority = priority::Normal);
/// `match.item` / `match.itemTag` select the item ("" = any item, an empty hand included).
HandlerId onUse(Match match, UseHandler fn, int priority = priority::Normal);
HandlerId onUse(std::string itemId, UseHandler fn, int priority = priority::Normal);
HandlerId onDestroy(Match match, DestroyHandler fn, int priority = priority::Normal);
/// After vanilla placed a block: `match.block` = the placed block, `match.item` = the held item.
HandlerId onPlace(Match match, PlaceHandler fn, int priority = priority::Normal);
/// First hit on a block (left-click); `Consume` / `Deny` stop the mining.
HandlerId onStartDestroy(Match match, DestroyHandler fn, int priority = priority::Normal);
/// Right-click on an entity; `match.block` = entity type id ("" = any).
HandlerId onInteract(Match match, EntityHandler fn, int priority = priority::Normal);
/// Attacking an entity; `Consume` = the attack is replaced, `Deny` = no attack.
HandlerId onAttack(Match match, EntityHandler fn, int priority = priority::Normal);
/// After vanilla's use-on did not succeed (and no handler took it); `Consume` / `Deny` turn the
/// click into a handled / refused one.
HandlerId onUnusedUseOn(Match match, UseOnHandler fn, int priority = priority::Normal);
/// After vanilla's destroyBlock ran (also when a handler did not stop it): `destroyed` is its
/// result. `ctx.block` / `ctx.blockId` are the block before the break.
HandlerId onDestroyed(Match match, DestroyedHandler fn, int priority = priority::Normal);
/// After vanilla's attack ran; `hit` is its result. `match.block` = entity type id.
HandlerId onAttacked(Match match, AttackedHandler fn, int priority = priority::Normal);
/// Runs `fn` on the next server tick (server thread).
void nextTick(std::function<void()> fn);
/// A player of the server level (not the client's own predicting LocalPlayer).
[[nodiscard]] bool isServerPlayer(Player& player);
/// Removes a handler (selftests, uninstall).
void remove(HandlerId id);
/// Whether `match` selects (block or entity id, item id); `entity` picks entity type tags.
[[nodiscard]] bool matches(Match const& match, std::string const& blockId, std::string const& itemId, bool entity = false);

/// Runs the handlers as the hooks do (selftests, other entry points). Returns the result of the
/// handler that took it, `Pass` if none did.
Result dispatchUseOn(UseOnContext const& ctx);
Result dispatchUse(UseContext const& ctx);
Result dispatchDestroy(DestroyContext const& ctx);
void   dispatchPlace(PlaceContext const& ctx);
Result dispatchStartDestroy(DestroyContext const& ctx);
Result dispatchInteract(EntityContext const& ctx);
Result dispatchAttack(EntityContext const& ctx);

} // namespace gf::feature::common::interactions

namespace gf {
/// Short alias used across units: gf::interactions::onUseOn(...).
namespace interactions = feature::common::interactions;
} // namespace gf
