// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/inventory/UncraftingMenu.java
//   src/main/java/twilightforest/inventory/UncraftingContainer.java
//   src/main/java/twilightforest/inventory/UncraftingInputContainer.java
//   src/main/java/twilightforest/inventory/slot/UncraftingSlot.java
//   src/main/java/twilightforest/inventory/slot/UncraftingResultSlot.java
//   src/main/java/twilightforest/inventory/slot/AssemblySlot.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// The uncrafting table's menu (UncraftingMenu), server side: one per player with the table open.
// The containers (the tinker input, the result, the hidden uncrafting matrix, the 3 x 3 assembly
// grid) live here; the player's inventory is the game's. Slot indices are Java's:
//   0 input, 1 result, 2..10 uncrafting matrix, 11..19 assembly grid, 20..46 inventory 9..35,
//   47..55 hotbar 0..8.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/world/item/ItemStack.h"

#include "core/GlobalPos.h"
#include "core/uncraftmaps/Uncrafting.h"
#include "feature/recipes/Recipes.h"

class Player;

namespace gf::feature::uncraftmaps {

using Json = nlohmann::json;

enum class Click { Pickup, QuickMove, Swap, Clone, Throw, PickupAll };

inline constexpr int kSlotInput      = 0;
inline constexpr int kSlotResult     = 1;
inline constexpr int kSlotUncrafting = 2;  ///< 2..10
inline constexpr int kSlotAssembly   = 11; ///< 11..19
inline constexpr int kSlotInventory  = 20; ///< 20..55
inline constexpr int kSlotCount      = 56;

/// UncraftingMenu's state of one player.
struct MenuState {
    core::GlobalPos pos;
    ItemStack       input;
    ItemStack       result;
    std::array<ItemStack, 9> uncrafting;
    std::array<bool, 9>      marked{}; ///< TwilightForestMarker on the uncrafting stack
    std::array<ItemStack, 9> assembly;
    ItemStack       carried;
    core::uncraftmaps::Cycles cycles;
    recipes::CraftingRecipe const* ghost = nullptr; ///< storedGhostRecipe
    int numberOfInputItems = 0;
    int uncraftingCost     = 0;
    int recraftingCost     = 0;
};

class Menu {
public:
    Menu(Player& player, MenuState& state) : mPlayer(player), mS(state) {}

    // ---- containers ------------------------------------------------------------------------------
    [[nodiscard]] ItemStack get(int slot) const;
    /// Slot#set (container setItem: the input and the assembly grid call slotsChanged).
    void set(int slot, ItemStack const& stack);

    /// UncraftingMenu#slotsChanged for the input ("input"), the assembly grid ("assembly").
    enum class Changed { Input, Assembly };
    void slotsChanged(Changed which);

    /// UncraftingMenu#clicked (AbstractContainerMenu#doClick with TF's checks).
    void clicked(int slot, int button, Click type);

    /// UncraftingGuiPacket.
    void guiButton(int type);

    /// UncraftingMenu#removed: the assembly grid, the input and the cursor stack go back to the player.
    void removed();

    /// The costs as the screen shows them.
    [[nodiscard]] int uncraftingCost() const { return mS.uncraftingCost; }
    [[nodiscard]] int recraftingCost() const { return mS.recraftingCost; }
    [[nodiscard]] bool isUncraftingRecipe() const;
    /// The input is in #twilightforest:banned_uncraftables (the screen's red tooltip).
    [[nodiscard]] bool inputBanned() const;

    /// Every uncrafting candidate for `input` (getRecipesFor), in order.
    [[nodiscard]] static std::vector<recipes::CraftingRecipe const*> recipesFor(ItemStack const& input);
    /// The crafting recipes matching a 3 x 3 grid (RecipeManager#getRecipesFor(CRAFTING)).
    [[nodiscard]] static std::vector<recipes::CraftingRecipe const*> craftingRecipesFor(std::array<ItemStack, 9> const& grid);
    /// A recipe's result stack.
    [[nodiscard]] static ItemStack assemble(recipes::CraftingRecipe const& recipe);

    [[nodiscard]] Player& player() { return mPlayer; }
    [[nodiscard]] MenuState& state() { return mS; }

private:
    [[nodiscard]] int  levels() const;
    [[nodiscard]] bool instabuild() const;
    [[nodiscard]] bool assemblyEmpty() const;
    [[nodiscard]] bool uncraftingEmpty() const;
    [[nodiscard]] int  countDamageableParts() const;
    [[nodiscard]] int  countDamagedParts(ItemStack const& input) const;
    [[nodiscard]] int  calculateUncraftingCost() const;
    [[nodiscard]] int  calculateRecraftingCost() const;
    void               chooseRecipe(std::array<ItemStack, 9> const& grid);
    std::vector<ItemStack> ingredientsOf(recipes::CraftingRecipe const& recipe) const;

    // slots
    [[nodiscard]] bool mayPickup(int slot) const;
    [[nodiscard]] bool mayPlace(int slot, ItemStack const& stack) const;
    void               onTake(int slot, ItemStack const& taken);
    ItemStack          safeInsert(int slot, ItemStack stack, int increment);
    std::optional<ItemStack> tryRemove(int slot, int count, int decrement);
    ItemStack          removeFrom(int slot, int count);
    bool               moveItemStackTo(ItemStack& stack, int start, int end, bool reverse);
    ItemStack          quickMoveStack(int slot);
    void               uncraftingTaken();
    void               resultTaken(ItemStack const& taken);
    void               drop(ItemStack const& stack);
    void               giveOrDrop(ItemStack stack);

    Player&    mPlayer;
    MenuState& mS;
};

/// The menu state of a player with the table open (nullptr: none).
[[nodiscard]] MenuState* openMenu(Player& player);
/// Opens the menu for `player` at `pos` (a previous one is closed first, its items returned).
MenuState& beginMenu(Player& player, core::GlobalPos const& pos);
/// Closes the player's menu (UncraftingMenu#removed); false when none was open.
bool endMenu(Player& player);
/// The screen data of the player's menu.
[[nodiscard]] Json menuData(Player& player, MenuState& state);

} // namespace gf::feature::uncraftmaps
