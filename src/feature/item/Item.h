#pragma once

// Contract "item" (header owned by F0, implemented by F9; spike S6).
//
// upstream's items are BP items (imported skeleton) whose behaviour is native, registered by id. F9
// owns the engine hooks (item use, release, mining, inventory / armor ticks, tooltips, dispensers,
// durability bars); units only register:
//
//     gf::item::registerItem("gloamingforest:ore_magnet", {
//         .use = [](Player& p, ItemStack& s) { ...; return gf::item::Result::Consume; },
//     });
//     gf::item::addTooltip("gloamingforest:charm_of_life_1", [](ItemStack const&, std::vector<std::string>& lines, Player const*) { ... });
//
// Item data lives in the stack's user data (game/Items.h). Server thread unless noted.

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class ItemStack;
class Player;

namespace gf::item {

using Json = nlohmann::json;

/// Java's InteractionResult, reduced.
enum class Result { Pass, Success, Consume, Fail };

struct Behaviour {
    /// Right-click with the item (Java's use).
    std::function<Result(Player& player, ItemStack& stack)>                                       use;
    /// Every tick while the use button is held (onUseTick), with the remaining use ticks.
    std::function<void(Player& player, ItemStack& stack, int remainingTicks)>                     useTick;
    /// The use button was released early (releaseUsing), with the ticks it was held.
    std::function<void(Player& player, ItemStack& stack, int usedTicks)>                          release;
    /// The full use duration elapsed (finishUsingItem).
    std::function<void(Player& player, ItemStack& stack)>                                         finishUsing;
    /// The item hit `target` (hurtEnemy): true when it was used (durability etc. handled).
    std::function<bool(ItemStack& stack, Actor& target, Actor& attacker)>                         hurtEnemy;
    /// A block was mined with the item (mineBlock).
    std::function<bool(ItemStack& stack, BlockSource& region, BlockPos const& pos, Block const& block, Player& player)> mineBlock;
    /// Mining speed against a block (getDestroySpeed); nullopt = the item's default.
    std::function<std::optional<float>(ItemStack const& stack, Block const& block)>              destroySpeed;
    /// Every tick while in a player's inventory (inventoryTick).
    std::function<void(ItemStack& stack, Player& player, int slot, bool selected)>               inventoryTick;
    /// Every tick while worn (armor slot 0 head .. 3 feet).
    std::function<void(ItemStack& stack, Player& player, int armorSlot)>                          armorTick;
    std::function<void(Player& player, ItemStack const& stack, int armorSlot)>                    onEquip;
    std::function<void(Player& player, ItemStack const& stack, int armorSlot)>                    onUnequip;
    /// Extra attack damage against `target` (knightmetal vs armoured, ...).
    std::function<float(ItemStack const& stack, Actor& target, Player& attacker)>                 attackBonus;
    /// Whether `material` repairs the item in an anvil (isValidRepairItem).
    std::function<bool(ItemStack const& stack, ItemStack const& material)>                        repairWith;
};
void registerItem(std::string const& itemId, Behaviour behaviour);

/// GloamingForest item data stored in the stack (null when absent).
[[nodiscard]] Json getData(ItemStack const& stack, std::string const& key);
void               setData(ItemStack& stack, std::string const& key, Json const& value);

/// Starts a cooldown of `ticks` for item cooldown category `category` (the item id usually).
void               cooldown(Player& player, std::string const& category, int ticks);
[[nodiscard]] int  cooldownLeft(Player const& player, std::string const& category);

/// Adds tooltip / lore lines (server-written lore for vanilla clients, native tooltip with the
/// client mod). `viewer` may be null.
using TooltipFn = std::function<void(ItemStack const& stack, std::vector<std::string>& lines, Player const* viewer)>;
void addTooltip(std::string const& itemId, TooltipFn fn);

/// Dispenser behaviour (DispenseItemBehavior): `facing` is the dispenser's FacingID; true when
/// the item was used (the stack may be changed).
using DispenseFn = std::function<bool(BlockSource& region, BlockPos const& dispenser, int facing, ItemStack& stack)>;
void registerDispense(std::string const& itemId, DispenseFn fn);

/// The durability / dose bar (isBarVisible, getBarWidth, getBarColor): nullopt hides it.
struct Bar {
    float    fraction = 1.0f; ///< 0..1
    unsigned rgb      = 0x00FF00;
};
using BarFn = std::function<std::optional<Bar>(ItemStack const& stack)>;
void setBar(std::string const& itemId, BarFn fn);

} // namespace gf::item
