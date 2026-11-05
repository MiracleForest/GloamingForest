#pragma once

// U14 (progression): TF's 45 advancements, their criteria engine, the biome restrictions and
// their enforcement, the advancement screen and toasts. Internal header of the unit.
//
// The progression store and the trigger bus are F10's (feature/progression/Progression.h); this
// unit loads TF's advancement JSON (data/twilightforest/advancement/*.json), listens to every
// trigger the criteria name and grants the criteria through progression::grant.

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/progression/Criteria.h"
#include "core/progression/Rules.h"

class Actor;
class Player;
class ItemStack;

namespace gf::feature::progression_content {

using Json = nlohmann::json;

/// AdvancementType (frame).
enum class Frame { Task, Goal, Challenge };
[[nodiscard]] char const* frameName(Frame f);

struct Criterion {
    std::string name;
    std::string trigger;    ///< "minecraft:inventory_changed", "twilightforest:hurt_boss", ...
    Json        conditions; ///< the criterion's "conditions" object
};

struct Advancement {
    std::string              id;      ///< "twilightforest:progress_naga"
    std::string              parent;  ///< "" for the root
    bool                     hasDisplay = false;
    std::string              titleKey, descKey; ///< the components' translation keys
    Json                     title, description; ///< the text components (translate + with arguments)
    std::string              icon;              ///< item id
    Json                     iconComponents;    ///< the icon's components (variants)
    float                    x = 0, y = 0;      ///< TreeNodePosition's column and row
    bool                     placed = false;    ///< laid out (a displayed node under a displayed root)
    Frame                    frame          = Frame::Task;
    bool                     hidden         = false;
    bool                     showToast      = true;
    bool                     announceToChat = true;
    std::string              background;        ///< root only: a texture path
    std::vector<Criterion>   criteria;
    std::vector<std::vector<std::string>> requirements;
    int                      experience = 0;    ///< rewards.experience
};

/// The loaded advancements (TF's 45; empty before the mod dir is known), in display order.
[[nodiscard]] std::vector<Advancement> const& advancements();
[[nodiscard]] Advancement const* find(std::string const& id);

/// The biome restrictions (twilightforest:restrictions), by biome id.
[[nodiscard]] std::map<std::string, core::progression::Restriction> const& restrictions();
/// The restriction a player in `biome` is under (none once it is lifted).
[[nodiscard]] core::progression::Restriction const* restrictionFor(Player const& player, std::string const& biome);

// ---- the game views the criteria engine evaluates (Engine.cpp) ----------------------------------
[[nodiscard]] core::progression::Env const& env();
[[nodiscard]] core::progression::ItemView itemView(ItemStack const& stack);
[[nodiscard]] core::progression::EntityView entityView(Actor& actor, bool withVehicle = true);
[[nodiscard]] core::progression::EntityView playerView(Player& player);
/// The biome at an actor's feet ("" when unloaded).
[[nodiscard]] std::string biomeAt(Actor& actor);
/// The landmark structures whose pieces hold `actor`.
[[nodiscard]] std::vector<std::string> structuresAt(Actor& actor);

/// Re-tests the tick / location criteria of `player` (after a grant, once per second).
void evaluatePlayerCriteria(Player& player);
/// Tests the inventory_changed criteria against the player's inventory.
void evaluateInventory(Player& player);

/// The recipes recipe_crafted criteria resolve against (F9's data recipes, the game's BP recipes).
[[nodiscard]] core::progression::RecipeIndex const& recipeIndex();

/// While set, earning an advancement shows and awards nothing (no toast, chat line, experience
/// or message): progress the e2e demo restores (Presentation.cpp).
void setQuietGrants(bool quiet);

/// Number of criteria the engine grants by trigger id (tests).
[[nodiscard]] std::map<std::string, int> criteriaByTrigger();

/// The progress of `player` in an advancement: criteria done (of the requirements' groups) and
/// the total, as Java's AdvancementProgress (the groups met / all groups).
struct Progress {
    bool done   = false;
    int  met    = 0;
    int  groups = 0;
};
[[nodiscard]] Progress progressOf(Player const& player, Advancement const& adv);
/// Which advancements the tree shows `player` (Java's AdvancementVisibilityEvaluator): a node
/// that is done or has a done descendant, else the first rule found on it, its parent or its
/// grandparent (done: shown; hidden or without display: not shown) decides; none: not shown.
[[nodiscard]] std::set<std::string> visibleFor(Player const& player);

/// An advancement text component in `player`'s language (the arguments translated too).
[[nodiscard]] std::string textFor(Player const& player, Json const& component);
/// The icon as item JSON (game::items::toJson): the item with its components stored as item data
/// under the component ids (`twilightforest:e115_variant`, ...), so the icon keeps its variant.
[[nodiscard]] Json iconItem(Advancement const& adv);

/// Opens the advancement screen (native for companions, the form otherwise).
bool openAdvancements(Player& player);

/// The screen data of the advancement tree for `player` ("progression.advancements").
[[nodiscard]] Json screenData(Player& player);

} // namespace gf::feature::progression_content
