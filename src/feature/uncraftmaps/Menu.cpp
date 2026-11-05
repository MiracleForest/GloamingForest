// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/inventory/UncraftingMenu.java
//   src/main/java/twilightforest/inventory/UncraftingContainer.java
//   src/main/java/twilightforest/inventory/UncraftingInputContainer.java
//   src/main/java/twilightforest/inventory/slot/UncraftingSlot.java
//   src/main/java/twilightforest/inventory/slot/UncraftingResultSlot.java
//   src/main/java/twilightforest/network/UncraftingGuiPacket.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// UncraftingMenu on the server. AbstractContainerMenu#doClick is followed for the click types the
// screens send (pickup, quick move, swap, throw); the containers' change notifications are Java's:
// the input and the assembly grid call slotsChanged on every change, the uncrafting matrix and the
// result never do.
#include "feature/uncraftmaps/Menu.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <map>

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/world/actor/player/AbilitiesIndex.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"

#include "feature/enchant/Enchant.h"
#include "feature/enchant/EnchantInternal.h"
#include "feature/ext/item/Recipes.h"
#include "feature/progression/Progression.h"
#include "feature/tags/Tags.h"
#include "feature/uncraftmaps/Config.h"
#include "game/Items.h"
#include "mod/I18n.h"

namespace gf::feature::uncraftmaps {

namespace {

namespace items = game::items;
namespace cu    = core::uncraftmaps;
using recipes::CraftingRecipe;
using recipes::Ingredient;

constexpr char kBannedUncraftables[]   = "twilightforest:banned_uncraftables";
constexpr char kBannedIngredients[]    = "twilightforest:banned_uncrafting_ingredients";
constexpr char kIgnoresCost[]          = "twilightforest:uncrafting_ignores_cost";
constexpr char kUncraftingType[]       = "twilightforest:uncrafting";

bool      isEmpty(ItemStack const& s) { return items::isEmpty(s); }
int       count(ItemStack const& s) { return items::countOf(s); }
ItemStack withCount(ItemStack const& s, int n) { return n <= 0 ? ItemStack{} : items::withCount(s, n); }
bool      same(ItemStack const& a, ItemStack const& b) { return items::sameItemSameData(a, b); }
int       maxStack(ItemStack const& s) { return isEmpty(s) ? 64 : std::max(1, static_cast<int>(s.getMaxStackSize())); }
int       slotMax(ItemStack const& s) { return std::min(64, maxStack(s)); }
bool      itemTagged(ItemStack const& s, char const* tag) { return !isEmpty(s) && tags::has(tags::Registry::Item, tag, items::idOf(s)); }

bool isCrafting(CraftingRecipe const& r) { return r.type == "minecraft:crafting_shaped" || r.type == "minecraft:crafting_shapeless"; }
bool isShaped(CraftingRecipe const& r) { return r.type == "minecraft:crafting_shaped" || r.type == kUncraftingType; }

/// Ingredient#test for a stack.
bool matches(Ingredient const& ing, ItemStack const& stack) {
    if (ing.empty()) return isEmpty(stack);
    if (isEmpty(stack)) return false;
    auto id = items::idOf(stack);
    if (std::find(ing.items.begin(), ing.items.end(), id) != ing.items.end()) return true;
    return !ing.tag.empty() && tags::has(tags::Registry::Item, ing.tag, id);
}

/// Ingredient#getItems: the item ids it accepts (the tag's members after the listed items).
std::vector<std::string> optionsOf(Ingredient const& ing) {
    std::vector<std::string> out = ing.items;
    if (!ing.tag.empty())
        for (auto& m : tags::members(tags::Registry::Item, ing.tag))
            if (std::find(out.begin(), out.end(), m) == out.end()) out.push_back(std::move(m));
    return out;
}

/// Item#hasCraftingRemainingItem (Bedrock's crafting remainder) or a barrier: isIngredientProblematic.
Item const* remainderOf(ItemStack const& s) {
    if (isEmpty(s)) return nullptr;
    auto const* item = s.mItem.get();
    return item ? static_cast<Item const*>(item->mCraftingRemainingItem) : nullptr;
}
bool isIngredientProblematic(ItemStack const& s) { return (!isEmpty(s) && remainderOf(s) != nullptr) || items::idOf(s) == "minecraft:barrier"; }
bool isDamageableComponent(ItemStack const& s) { return !isEmpty(s) && !itemTagged(s, kIgnoresCost); }

int maxDamageOf(ItemStack const& s) {
    auto const* item = isEmpty(s) ? nullptr : s.mItem.get();
    return item ? item->getMaxDamage() : 0;
}

/// Item#isEnchantable: an item with an enchanting slot.
bool isEnchantable(ItemStack const& s) {
    auto const* item = isEmpty(s) ? nullptr : s.mItem.get();
    return item && item->getEnchantSlot() != 0;
}

/// The Bedrock name of a Java vanilla enchantment id.
std::string bedrockEnchantName(std::string const& id) {
    auto name = id.substr(id.find(':') + 1);
    if (name == "binding_curse") return "binding";
    if (name == "vanishing_curse") return "vanishing";
    if (name == "sweeping_edge") return "sweeping";
    return name;
}

/// ItemStack#supportsEnchantment.
bool supportsEnchantment(ItemStack const& s, std::string const& id) {
    if (isEmpty(s)) return false;
    if (enchant::internal::isCustom(id)) return enchant::internal::supports(id, items::idOf(s));
    auto const* e    = Enchant::getEnchantFromName(HashedString(bedrockEnchantName(id)));
    auto const* item = s.mItem.get();
    if (!e || !item) return false;
    int slot = item->getEnchantSlot();
    return (slot & (e->mPrimarySlots | e->mSecondarySlots)) != 0;
}

int weightOf(std::string const& id) {
    if (auto spec = enchant::internal::specOf(id)) return spec->weight;
    int w = cu::vanillaEnchantmentWeight(id);
    return w > 0 ? w : 1;
}

/// UncraftingMenu#isValidMatchForInput: the same kind of tool, or armour of the same slot.
bool isValidMatchForInput(ItemStack const& input, ItemStack const& result) {
    static char const* const kinds[] = {
        "minecraft:pickaxes", "minecraft:axes", "minecraft:shovels", "minecraft:hoes", "minecraft:swords",
        "c:tools/bow", "c:tools/crossbow", "c:tools/fishing_rod",
        // ArmorItem#getEquipmentSlot: the armour slot tags
        "minecraft:head_armor", "minecraft:chest_armor", "minecraft:leg_armor", "minecraft:foot_armor",
    };
    for (auto const* tag : kinds)
        if (itemTagged(input, tag) && itemTagged(result, tag)) return true;
    return false;
}

bool sameShapeless(CraftingRecipe const& r, std::vector<ItemStack const*> const& stacks) {
    if (r.ingredients.size() != stacks.size()) return false;
    std::vector<bool> used(stacks.size(), false);
    // a greedy match with backtracking over the few ingredients
    std::function<bool(std::size_t)> go = [&](std::size_t i) {
        if (i == r.ingredients.size()) return true;
        for (std::size_t k = 0; k < stacks.size(); ++k) {
            if (used[k] || !matches(r.ingredients[i], *stacks[k])) continue;
            used[k] = true;
            if (go(i + 1)) return true;
            used[k] = false;
        }
        return false;
    };
    return go(0);
}

/// ShapedRecipe#matches on the trimmed grid (CraftingInput), with the mirrored pattern.
bool shapedMatches(CraftingRecipe const& r, std::array<ItemStack, 9> const& grid) {
    int minX = 3, minY = 3, maxX = -1, maxY = -1;
    for (int i = 0; i < 9; ++i) {
        if (isEmpty(grid[static_cast<std::size_t>(i)])) continue;
        minX = std::min(minX, i % 3), maxX = std::max(maxX, i % 3);
        minY = std::min(minY, i / 3), maxY = std::max(maxY, i / 3);
    }
    if (maxX < 0) return false;
    int w = maxX - minX + 1, h = maxY - minY + 1;
    if (w != r.width || h != r.height || static_cast<int>(r.ingredients.size()) != w * h) return false;
    for (bool mirror : {false, true}) {
        bool ok = true;
        for (int y = 0; y < h && ok; ++y)
            for (int x = 0; x < w && ok; ++x) {
                int  px = mirror ? w - 1 - x : x;
                auto const& ing = r.ingredients[static_cast<std::size_t>(px + y * w)];
                ok = matches(ing, grid[static_cast<std::size_t>(minX + x + (minY + y) * 3)]);
            }
        if (ok) return true;
    }
    return false;
}

int levelsOf(Player const& player) {
    auto ref = player.getAttribute(Player::LEVEL());
    if (auto const* inst = static_cast<AttributeInstance const*>(ref.mPtr)) return static_cast<int>(inst->mCurrentValue);
    return 0;
}

std::map<std::int64_t, MenuState>& menus() {
    static std::map<std::int64_t, MenuState> m;
    return m;
}

} // namespace

// ---- containers --------------------------------------------------------------------------------------

ItemStack Menu::get(int slot) const {
    if (slot == kSlotInput) return mS.input;
    if (slot == kSlotResult) return mS.result;
    if (slot >= kSlotUncrafting && slot < kSlotAssembly) return mS.uncrafting[static_cast<std::size_t>(slot - kSlotUncrafting)];
    if (slot >= kSlotAssembly && slot < kSlotInventory) return mS.assembly[static_cast<std::size_t>(slot - kSlotAssembly)];
    if (slot >= kSlotInventory && slot < kSlotCount) {
        int k = slot - kSlotInventory;
        return mPlayer.getInventory().getItem(k < 27 ? 9 + k : k - 27);
    }
    return {};
}

void Menu::set(int slot, ItemStack const& stack) {
    if (slot == kSlotInput) {
        mS.input = stack;
        slotsChanged(Changed::Input);
    } else if (slot == kSlotResult) {
        mS.result = stack;
    } else if (slot >= kSlotUncrafting && slot < kSlotAssembly) {
        auto i           = static_cast<std::size_t>(slot - kSlotUncrafting);
        mS.uncrafting[i] = stack;
        if (isEmpty(stack)) mS.marked[i] = false;
    } else if (slot >= kSlotAssembly && slot < kSlotInventory) {
        mS.assembly[static_cast<std::size_t>(slot - kSlotAssembly)] = stack;
        slotsChanged(Changed::Assembly);
    } else if (slot >= kSlotInventory && slot < kSlotCount) {
        int k = slot - kSlotInventory;
        mPlayer.getInventory().setItem(k < 27 ? 9 + k : k - 27, stack);
    }
}

int  Menu::levels() const { return levelsOf(mPlayer); }
bool Menu::instabuild() const { return mPlayer.canUseAbility(AbilitiesIndex::Instabuild); }
bool Menu::assemblyEmpty() const {
    return std::all_of(mS.assembly.begin(), mS.assembly.end(), [](ItemStack const& s) { return isEmpty(s); });
}
bool Menu::uncraftingEmpty() const {
    return std::all_of(mS.uncrafting.begin(), mS.uncrafting.end(), [](ItemStack const& s) { return isEmpty(s); });
}
bool Menu::isUncraftingRecipe() const { return mS.ghost && mS.ghost->type == kUncraftingType; }
bool Menu::inputBanned() const { return itemTagged(mS.input, kBannedUncraftables); }

// ---- recipes -----------------------------------------------------------------------------------------

std::vector<CraftingRecipe const*> Menu::recipesFor(ItemStack const& input) {
    std::vector<CraftingRecipe const*> out;
    if (isEmpty(input)) return out;
    auto const& c      = config();
    auto        filter = recipeFilter();
    auto        id     = items::idOf(input);
    for (auto const* r : recipes::producing(id)) {
        if (!r || r->type == kUncraftingType) continue;
        // isRecipeSupported: shaped (any crafting recipe with enableShapelessCrafting)
        if (!(c.enableShapelessCrafting ? isCrafting(*r) : r->type == "minecraft:crafting_shaped")) continue;
        if (r->ingredients.empty()) continue; // isIncomplete / no ingredients
        bool incomplete = std::all_of(r->ingredients.begin(), r->ingredients.end(), [](Ingredient const& i) { return i.empty(); });
        if (incomplete) continue;
        // canCraftInDimensions(3, 3)
        if (r->type == "minecraft:crafting_shaped" ? (r->width > 3 || r->height > 3) : r->ingredients.size() > 9) continue;
        if (r->resultItem != id || count(input) < r->resultCount) continue;
        if (!cu::recipeAllowed(filter, r->id)) continue;
        out.push_back(r);
    }
    // the uncrafting recipes: UncraftingRecipe#isItemStackAnIngredient (the input item, count >= input_count)
    for (auto const* r : recipes::ofType(kUncraftingType)) {
        if (!r || !r->data.is_object()) continue;
        auto ing = recipes::ext::parseIngredient(r->data.value("input", Json()));
        if (!matches(ing, withCount(input, 1))) continue;
        // Bedrock's tipped arrow is minecraft:arrow with the potion as its aux value: the Java
        // tipped_arrow input takes arrows with a potion only
        auto javaInput = r->data.value("input", Json::object());
        if (javaInput.is_object() && javaInput.value("item", std::string()) == "minecraft:tipped_arrow" && input.getAuxValue() <= 0) continue;
        if (count(input) >= r->resultCount) out.push_back(r);
    }
    return out;
}

std::vector<CraftingRecipe const*> Menu::craftingRecipesFor(std::array<ItemStack, 9> const& grid) {
    std::vector<CraftingRecipe const*> out;
    std::vector<ItemStack const*>      stacks;
    for (auto const& s : grid)
        if (!isEmpty(s)) stacks.push_back(&s);
    if (stacks.empty()) return out;
    for (auto const* r : recipes::ofType("minecraft:crafting_shaped"))
        if (r && !r->resultItem.empty() && shapedMatches(*r, grid)) out.push_back(r);
    for (auto const* r : recipes::ofType("minecraft:crafting_shapeless"))
        if (r && !r->resultItem.empty() && sameShapeless(*r, stacks)) out.push_back(r);
    return out;
}

ItemStack Menu::assemble(CraftingRecipe const& recipe) {
    auto s = items::make(recipe.resultItem, std::max(1, recipe.resultCount));
    return s ? *s : ItemStack{};
}

/// UncraftingMenu#getIngredients: each ingredient's floorMod(ingredientsInCycle, n)-th item that is
/// not in #banned_uncrafting_ingredients (empty when none), one of it.
std::vector<ItemStack> Menu::ingredientsOf(CraftingRecipe const& recipe) const {
    std::vector<ItemStack> out;
    for (auto const& ing : recipe.ingredients) {
        std::vector<std::string> options;
        for (auto& id : optionsOf(ing))
            if (!tags::has(tags::Registry::Item, kBannedIngredients, id)) options.push_back(std::move(id));
        if (options.empty()) {
            out.emplace_back();
            continue;
        }
        auto const& id = options[static_cast<std::size_t>(cu::floorMod(mS.cycles.ingredients, static_cast<int>(options.size())))];
        auto        s  = items::make(id, 1);
        out.push_back(s ? *s : ItemStack{});
    }
    return out;
}

void Menu::chooseRecipe(std::array<ItemStack, 9> const& grid) {
    auto list = craftingRecipesFor(grid);
    if (list.empty()) {
        mS.result = ItemStack{};
        return;
    }
    // (Bedrock has no doLimitedCrafting gamerule: every recipe is known)
    auto const* recipe = list[static_cast<std::size_t>(cu::floorMod(mS.cycles.recipe, static_cast<int>(list.size())))];
    mS.result          = assemble(*recipe);
}

// ---- costs -------------------------------------------------------------------------------------------

int Menu::countDamageableParts() const {
    int n = 9;
    for (std::size_t i = 0; i < 9; ++i) {
        auto const& s = mS.uncrafting[i];
        if (isIngredientProblematic(s) || mS.marked[i] || !isDamageableComponent(s)) --n;
    }
    return n;
}

int Menu::countDamagedParts(ItemStack const& input) const {
    if (isEmpty(input)) return 0;
    return cu::damagedParts(countDamageableParts(), input.getDamageValue(), maxDamageOf(input));
}

int Menu::calculateUncraftingCost() const {
    cu::UncraftingCostInput in;
    in.assemblyEmpty     = assemblyEmpty();
    in.uncraftingRecipe  = isUncraftingRecipe();
    in.hasRecipe         = mS.ghost != nullptr;
    in.recipeCost        = mS.ghost && mS.ghost->data.is_object() ? mS.ghost->data.value("cost", -1) : -1;
    in.damageableParts   = countDamageableParts();
    in.multiplier        = config().uncraftingXpCostMultiplier;
    in.disableUncrafting = config().disableUncrafting;
    return cu::uncraftingCost(in);
}

int Menu::calculateRecraftingCost() const {
    cu::RecraftingCostInput in;
    in.inputPresent  = !isEmpty(mS.input);
    in.outputPresent = !isEmpty(mS.result);
    if (!in.inputPresent || !in.outputPresent) return 0;
    in.outputEnchantable = isEnchantable(mS.result);
    in.sameItem          = items::sameItem(mS.input, mS.result);
    in.filledAssembly    = static_cast<int>(std::count_if(mS.assembly.begin(), mS.assembly.end(), [](ItemStack const& s) { return !isEmpty(s); }));
    for (auto const& [id, lvl] : enchant::all(mS.input)) in.inputEnchantments.push_back({weightOf(id), lvl});
    in.damagedParts       = countDamagedParts(mS.input);
    in.resultEnchantments = static_cast<int>(enchant::all(mS.result).size());
    in.multiplier         = config().repairingXpCostMultiplier;
    return cu::recraftingCost(in);
}

// ---- slotsChanged ------------------------------------------------------------------------------------

void Menu::slotsChanged(Changed which) {
    if (which == Changed::Input) {
        // empty the whole grid to start with
        for (std::size_t i = 0; i < 9; ++i) {
            mS.uncrafting[i] = ItemStack{};
            mS.marked[i]     = false;
        }
        auto list = recipesFor(mS.input);
        int  size = static_cast<int>(list.size());
        if (size > 0 && !inputBanned()) {
            auto const* recipe = list[static_cast<std::size_t>(cu::floorMod(mS.cycles.unrecipe, size))];
            mS.ghost           = recipe;
            auto ingredients   = ingredientsOf(*recipe);
            auto layout        = cu::gridLayout(isShaped(*recipe), recipe->width, recipe->height, static_cast<int>(ingredients.size()));
            for (std::size_t k = 0; k < ingredients.size(); ++k) {
                int at = layout[k];
                if (at < 0) continue;
                mS.uncrafting[static_cast<std::size_t>(at)] = withCount(ingredients[k], isEmpty(ingredients[k]) ? 0 : 1);
            }
            // mark the appropriate number of damaged components
            if (mS.input.isDamaged()) {
                int damaged = countDamagedParts(mS.input);
                for (std::size_t i = 0; i < 9 && damaged > 0; ++i) {
                    if (isDamageableComponent(mS.uncrafting[i])) {
                        mS.marked[i] = true;
                        --damaged;
                    }
                }
            }
            // mark banned items
            for (std::size_t i = 0; i < 9; ++i)
                if (isIngredientProblematic(mS.uncrafting[i])) mS.marked[i] = true;
            // the number of items this recipe produces (the input count it needs)
            mS.numberOfInputItems = recipe->resultCount;
            mS.uncraftingCost     = calculateUncraftingCost();
            mS.recraftingCost     = 0;
        } else {
            mS.ghost              = nullptr;
            mS.numberOfInputItems = 0;
            mS.uncraftingCost     = 0;
        }
    }
    // the assembly grid or the input changed
    if (isEmpty(mS.input)) {
        chooseRecipe(mS.assembly);
    } else {
        mS.result         = ItemStack{};
        mS.uncraftingCost = calculateUncraftingCost();
    }
    mS.recraftingCost = 0;

    // repairing / recrafting: can both grids combined make the same kind of item as the input?
    if (!uncraftingEmpty() && !assemblyEmpty()) {
        std::array<ItemStack, 9> combine;
        for (std::size_t i = 0; i < 9; ++i) {
            if (!isEmpty(mS.assembly[i])) combine[i] = mS.assembly[i];
            else if (!isEmpty(mS.uncrafting[i]) && !mS.marked[i]) combine[i] = mS.uncrafting[i];
        }
        chooseRecipe(combine);
        if (!isEmpty(mS.result) && isValidMatchForInput(mS.input, mS.result)) {
            if (isEnchantable(mS.result)) {
                // the input's enchantments, then the result's own (pre-enchanted gear keeps them)
                auto enchants = enchant::all(mS.input);
                for (auto const& [id, lvl] : enchant::all(mS.result)) enchants[id] = lvl;
                std::erase_if(enchants, [&](auto const& e) { return !supportsEnchantment(mS.result, e.first); });
                for (auto const& [id, lvl] : enchant::all(mS.result)) enchant::set(mS.result, id, 0);
                for (auto const& [id, lvl] : enchants) enchant::set(mS.result, id, lvl);
            }
            mS.uncraftingCost = 0;
            mS.recraftingCost = calculateRecraftingCost();
        }
        // (otherwise TF leaves chooseRecipe's result of the combined grid in the result slot)
    }
}

// ---- slots -------------------------------------------------------------------------------------------

bool Menu::mayPickup(int slot) const {
    if (slot >= kSlotUncrafting && slot < kSlotAssembly) {
        // UncraftingSlot#mayPickup
        if (!assemblyEmpty()) return false;
        if (mS.marked[static_cast<std::size_t>(slot - kSlotUncrafting)]) return false;
        if (config().disableUncrafting && !isUncraftingRecipe()) return false;
        return mS.uncraftingCost <= levels() || instabuild();
    }
    return true;
}

bool Menu::mayPlace(int slot, ItemStack const&) const {
    if (slot == kSlotResult) return false;                                  // ResultSlot
    if (slot >= kSlotUncrafting && slot < kSlotAssembly) return false;      // UncraftingSlot
    return slot >= 0 && slot < kSlotCount;
}

ItemStack Menu::removeFrom(int slot, int n) {
    auto cur = get(slot);
    if (isEmpty(cur) || n <= 0) return {};
    n = std::min(n, count(cur));
    auto taken = withCount(cur, n);
    set(slot, withCount(cur, count(cur) - n));
    return taken;
}

/// Slot#tryRemove: allowModification = mayPickup && mayPlace(current); without it only the whole stack.
std::optional<ItemStack> Menu::tryRemove(int slot, int n, int decrement) {
    if (!mayPickup(slot)) return std::nullopt;
    auto cur = get(slot);
    if (!mayPlace(slot, cur) && decrement < count(cur)) return std::nullopt;
    n          = std::min(n, decrement);
    auto taken = removeFrom(slot, n);
    if (isEmpty(taken)) return std::nullopt;
    return taken;
}

ItemStack Menu::safeInsert(int slot, ItemStack stack, int increment) {
    if (isEmpty(stack) || !mayPlace(slot, stack)) return stack;
    auto cur = get(slot);
    int  i   = std::min(std::min(increment, count(stack)), slotMax(stack) - (isEmpty(cur) ? 0 : count(cur)));
    if (i <= 0) return stack;
    if (isEmpty(cur)) set(slot, withCount(stack, i));
    else if (same(cur, stack)) set(slot, withCount(cur, count(cur) + i));
    else return stack;
    return withCount(stack, count(stack) - i);
}

void Menu::onTake(int slot, ItemStack const& taken) {
    if (slot >= kSlotUncrafting && slot < kSlotAssembly) uncraftingTaken();
    else if (slot == kSlotResult) resultTaken(taken);
}

/// UncraftingSlot#onTake.
void Menu::uncraftingTaken() {
    int cost = mS.uncraftingCost;
    if (cost > 0) mPlayer.addLevels(-cost);
    // the remaining unmarked ingredients go into the (empty) assembly grid
    for (std::size_t i = 0; i < 9; ++i) {
        auto const& s = mS.uncrafting[i];
        if (!isEmpty(s) && !mS.marked[i]) set(kSlotAssembly + static_cast<int>(i), s);
    }
    if (!isEmpty(mS.input)) {
        progression::trigger(mPlayer, "twilightforest:uncraft_item", {{"item", items::idOf(mS.input)}, {"stack", items::toJson(mS.input)}});
        // DataComponents.CONTAINER: a shulker box's contents go back to the player
        if (auto const* tag = items::userData(mS.input); tag && tag->contains("Items")) {
            auto const& list = (*tag)["Items"];
            if (list.hold<ListTag>()) {
                for (auto const& entry : list.get<ListTag>()) {
                    if (!entry.hold<CompoundTag>()) continue;
                    auto s = ItemStack::fromTag(static_cast<CompoundTag const&>(*entry));
                    if (!isEmpty(s)) giveOrDrop(s);
                }
            }
        }
        removeFrom(kSlotInput, mS.numberOfInputItems);
    }
}

/// UncraftingResultSlot#onTake.
void Menu::resultTaken(ItemStack const& taken) {
    bool combined = true;
    for (auto const* r : craftingRecipesFor(mS.assembly)) {
        auto out = assemble(*r);
        if (same(out, taken)) {
            combined = false;
            break;
        }
    }
    std::map<std::size_t, ItemStack> remainders;
    if (combined) {
        int cost = mS.recraftingCost;
        if (cost > 0) mPlayer.addLevels(-cost);
        for (std::size_t i = 0; i < 9; ++i) {
            if (!isEmpty(mS.assembly[i])) continue;
            if (!mS.marked[i]) set(kSlotUncrafting + static_cast<int>(i), ItemStack{});
            else remainders[i] = mS.uncrafting[i];
        }
        removeFrom(kSlotInput, mS.numberOfInputItems);
    }
    // ResultSlot#onTake over the assembly grid: one of each ingredient, the remainders back
    for (std::size_t i = 0; i < 9; ++i) {
        auto cur = mS.assembly[i];
        if (isEmpty(cur)) continue;
        Item const* rem = remainderOf(cur);
        removeFrom(kSlotAssembly + static_cast<int>(i), 1);
        if (!rem) continue;
        auto remaining = items::make(rem->mFullName->getString(), 1);
        if (!remaining) continue;
        if (isEmpty(mS.assembly[i])) set(kSlotAssembly + static_cast<int>(i), *remaining);
        else if (!same(mS.assembly[i], *remaining)) giveOrDrop(*remaining);
    }
    for (auto const& [i, s] : remainders) set(kSlotAssembly + static_cast<int>(i), s);
}

void Menu::drop(ItemStack const& s) {
    if (!isEmpty(s)) mPlayer.drop(s, false);
}

void Menu::giveOrDrop(ItemStack stack) {
    if (isEmpty(stack)) return;
    if (!mPlayer.add(stack) && !isEmpty(stack)) mPlayer.drop(stack, false);
}

bool Menu::moveItemStackTo(ItemStack& stack, int start, int end, bool reverse) {
    bool moved = false;
    if (maxStack(stack) > 1) {
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
            if (!isEmpty(get(i)) || !mayPlace(i, stack)) continue;
            int n = std::min(count(stack), slotMax(stack));
            set(i, withCount(stack, n));
            stack = withCount(stack, count(stack) - n);
            moved = true;
            break;
        }
    }
    return moved;
}

/// UncraftingMenu#quickMoveStack.
ItemStack Menu::quickMoveStack(int slot) {
    auto stack = get(slot);
    if (isEmpty(stack)) return {};
    auto copy = stack;
    if (slot == kSlotInput) {
        if (!moveItemStackTo(stack, kSlotInventory, kSlotCount, false)) return {};
    } else if (slot == kSlotResult) {
        if (!moveItemStackTo(stack, kSlotInventory, kSlotCount, true)) return {};
    } else if (slot >= kSlotInventory) {
        if (!moveItemStackTo(stack, kSlotInput, kSlotInput + 1, false)) return {};
    } else if (slot >= kSlotAssembly) {
        if (!moveItemStackTo(stack, kSlotInventory, kSlotCount, false)) return {};
    } else {
        // the uncrafting matrix
        if (moveItemStackTo(stack, kSlotInventory, kSlotCount, false)) {
            set(slot, stack);
            onTake(slot, stack);
            return {};
        }
    }
    if (slot == kSlotResult) {
        // the result's stack moved out: its slot empties as Slot#set(EMPTY) does (no notification)
        mS.result = isEmpty(stack) ? ItemStack{} : stack;
    } else {
        set(slot, stack);
    }
    if (count(stack) == count(copy)) return {};
    onTake(slot, stack);
    if (slot == kSlotResult) drop(stack);
    return copy;
}

// ---- clicks ------------------------------------------------------------------------------------------

void Menu::clicked(int slot, int button, Click type) {
    // taking from an empty assembly slot while the grid is empty takes the ghost ingredient
    if (slot >= kSlotAssembly && slot < kSlotInventory && isEmpty(mS.carried) && isEmpty(get(slot))) {
        bool hotbarEmpty = type != Click::Swap || (button >= 0 && button < 9 && isEmpty(mPlayer.getInventory().getItem(button)))
                           || (button == 40 && isEmpty(mPlayer.getOffhandSlot()));
        if (assemblyEmpty() && hotbarEmpty) slot -= 9;
    }
    // the result needs the recrafting levels
    if (slot == kSlotResult && calculateRecraftingCost() > levels() && !instabuild()) return;
    if (slot >= kSlotUncrafting && slot < kSlotAssembly) {
        if (config().disableUncrafting && !isUncraftingRecipe()) return;
        if (calculateUncraftingCost() > levels() && !instabuild()) return;
        auto const& in = mS.uncrafting[static_cast<std::size_t>(slot - kSlotUncrafting)];
        if (isEmpty(in) || mS.marked[static_cast<std::size_t>(slot - kSlotUncrafting)]) return;
    }

    auto& carried = mS.carried;
    if ((type == Click::Pickup || type == Click::QuickMove) && (button == 0 || button == 1)) {
        bool primary = button == 0;
        if (slot == -999) {
            if (isEmpty(carried)) return;
            if (primary) {
                drop(carried);
                carried = ItemStack{};
            } else {
                drop(withCount(carried, 1));
                carried = withCount(carried, count(carried) - 1);
            }
        } else if (type == Click::QuickMove) {
            if (slot < 0 || slot >= kSlotCount || !mayPickup(slot)) return;
            auto moved = quickMoveStack(slot);
            while (!isEmpty(moved) && items::sameItem(get(slot), moved)) moved = quickMoveStack(slot);
        } else {
            if (slot < 0 || slot >= kSlotCount) return;
            auto cur = get(slot);
            if (isEmpty(cur)) {
                if (!isEmpty(carried)) carried = safeInsert(slot, carried, primary ? count(carried) : 1);
            } else if (mayPickup(slot)) {
                if (isEmpty(carried)) {
                    int  n     = primary ? count(cur) : (count(cur) + 1) / 2;
                    auto taken = tryRemove(slot, n, std::numeric_limits<int>::max());
                    if (taken) {
                        carried = *taken;
                        onTake(slot, *taken);
                    }
                } else if (mayPlace(slot, carried)) {
                    if (same(cur, carried)) {
                        carried = safeInsert(slot, carried, primary ? count(carried) : 1);
                    } else if (count(carried) <= slotMax(carried)) {
                        auto put = carried;
                        carried  = cur;
                        set(slot, put);
                    }
                } else if (same(cur, carried)) {
                    auto taken = tryRemove(slot, count(cur), maxStack(carried) - count(carried));
                    if (taken) {
                        carried = withCount(carried, count(carried) + count(*taken));
                        onTake(slot, *taken);
                    }
                }
            }
        }
    } else if (type == Click::Swap && ((button >= 0 && button < 9) || button == 40)) {
        if (slot < 0 || slot >= kSlotCount) return;
        auto hand    = button == 40 ? ItemStack(mPlayer.getOffhandSlot()) : ItemStack(mPlayer.getInventory().getItem(button));
        auto setHand = [&](ItemStack const& s) {
            if (button == 40) mPlayer.setOffhandSlot(s);
            else mPlayer.getInventory().setItem(button, s);
        };
        auto cur = get(slot);
        if (isEmpty(hand) && isEmpty(cur)) return;
        if (isEmpty(hand)) {
            if (mayPickup(slot)) {
                setHand(cur);
                set(slot, ItemStack{});
                onTake(slot, cur);
            }
        } else if (isEmpty(cur)) {
            if (mayPlace(slot, hand)) {
                int j = slotMax(hand);
                if (count(hand) > j) {
                    set(slot, withCount(hand, j));
                    setHand(withCount(hand, count(hand) - j));
                } else {
                    setHand(ItemStack{});
                    set(slot, hand);
                }
            }
        } else if (mayPickup(slot) && mayPlace(slot, hand)) {
            int k = slotMax(hand);
            if (count(hand) > k) {
                set(slot, withCount(hand, k));
                onTake(slot, cur);
                giveOrDrop(cur);
                setHand(withCount(hand, count(hand) - k));
            } else {
                setHand(cur);
                set(slot, hand);
                onTake(slot, cur);
            }
        }
    } else if (type == Click::Throw) {
        if (!isEmpty(carried) || slot < 0 || slot >= kSlotCount) return;
        auto cur = get(slot);
        if (isEmpty(cur)) return;
        auto taken = tryRemove(slot, button == 0 ? 1 : count(cur), std::numeric_limits<int>::max());
        if (taken) {
            onTake(slot, *taken);
            drop(*taken);
        }
    } else if (type == Click::Clone) {
        if (!instabuild() || !isEmpty(carried) || slot < 0 || slot >= kSlotCount) return;
        auto cur = get(slot);
        if (!isEmpty(cur)) carried = withCount(cur, maxStack(cur));
    }

    // just trigger this event whenever the input slot is clicked for any reason
    if (slot == kSlotInput) slotsChanged(Changed::Input);
}

void Menu::guiButton(int type) {
    if (!cu::applyGuiButton(mS.cycles, type, config().disableIngredientSwitching)) return;
    slotsChanged(type < 4 ? Changed::Input : Changed::Assembly);
}

void Menu::removed() {
    if (!isEmpty(mS.carried)) {
        giveOrDrop(mS.carried);
        mS.carried = ItemStack{};
    }
    for (auto& s : mS.assembly) {
        giveOrDrop(s);
        s = ItemStack{};
    }
    giveOrDrop(mS.input);
    mS.input  = ItemStack{};
    mS.result = ItemStack{};
    for (std::size_t i = 0; i < 9; ++i) mS.uncrafting[i] = ItemStack{}, mS.marked[i] = false;
    mPlayer.sendInventory(false);
}

// ---- registry ----------------------------------------------------------------------------------------

MenuState* openMenu(Player& player) {
    auto it = menus().find(player.getOrCreateUniqueID().rawID);
    return it == menus().end() ? nullptr : &it->second;
}

MenuState& beginMenu(Player& player, core::GlobalPos const& pos) {
    endMenu(player);
    auto& s = menus()[player.getOrCreateUniqueID().rawID];
    s       = MenuState{};
    s.pos   = pos;
    Menu(player, s).slotsChanged(Menu::Changed::Assembly); // the constructor's slotsChanged(assemblyMatrix)
    return s;
}

bool endMenu(Player& player) {
    auto it = menus().find(player.getOrCreateUniqueID().rawID);
    if (it == menus().end()) return false;
    Menu(player, it->second).removed();
    menus().erase(it);
    return true;
}

Json menuData(Player& player, MenuState& s) {
    Menu m(player, s);
    auto j = [](ItemStack const& st) { return isEmpty(st) ? Json(nullptr) : items::toJson(st); };
    Json uncrafting = Json::array(), assembly = Json::array(), marked = Json::array();
    for (std::size_t i = 0; i < 9; ++i) {
        uncrafting.push_back(j(s.uncrafting[i]));
        assembly.push_back(j(s.assembly[i]));
        marked.push_back(s.marked[i]);
    }
    auto const& c = config();
    return {
        {"title", trFor(player, "container.twilightforest.uncrafting_table", "Uncrafting Table")},
        {"input", j(s.input)},
        {"result", j(s.result)},
        {"uncrafting", uncrafting},
        {"marked", marked},
        {"assembly", assembly},
        {"carried", j(s.carried)},
        {"uncraftingCost", s.uncraftingCost},
        {"recraftingCost", s.recraftingCost},
        {"levels", levelsOf(player)},
        {"creative", player.canUseAbility(AbilitiesIndex::Instabuild)},
        {"inputBanned", m.inputBanned()},
        {"disableUncrafting", c.disableUncrafting},
        {"disableIngredientSwitching", c.disableIngredientSwitching},
    };
}

} // namespace gf::feature::uncraftmaps
