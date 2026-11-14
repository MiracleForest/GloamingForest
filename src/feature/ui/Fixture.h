#pragma once

// The F12 fixture, server side (selftests "ui.*" and the e2e scenarios): see Fixture.cpp.

#include <cstddef>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

class BlockPos;
class BlockSource;
class ItemStack;
class Player;

namespace gf::ui::fixture {

/// Whether the fixture is on: development builds only (the gf_test packs are deployed next to the
/// mod). Production builds register no fixture screen, key, HUD layer or world renderer.
[[nodiscard]] bool enabled();

/// The tag that opts a real player into the fixture's real-player steps (`/tag @s add gf_ui_fixture`).
/// The client scenario adds it after its `ui.*` run and removes it at the end; the `ui.*` and full
/// runs never touch an untagged player (rule R12), so `/gloamingforest selftest` in a world with
/// real players leaves their position, inventory and screens alone.
inline constexpr char kPlayerTag[] = "gf_ui_fixture";

/// The real players (not SimulatedPlayers) that carry kPlayerTag.
[[nodiscard]] std::vector<Player*> fixturePlayers();
/// Sends "ui.fixture" `what` to a companion ({"show": true, "fog": true, ...}); false for vanilla players.
bool show(Player& player, nlohmann::json const& what);
/// The vanilla fallback of screen "ui.fixture" (a form).
void openFormFallback(Player& player);
/// The DDUI document of the fixture (vanilla players).
[[nodiscard]] nlohmann::json dduiDocument();

// ---- what the real-player steps change, and how it is undone (ui.fixture_clear) -----------------

/// Remembers where `player` is before the fixture moves it (dimension, feet position, rotation),
/// once until restorePlayers. What is remembered is kept in memory and in the world DB
/// (gf:ui:fixture_players; real players only), so a session that ends before ui.fixture_clear
/// (a crash, a harness restart) is put back by the next session's restorePlayers.
void rememberPlayer(Player& player);
/// Remembers the stack of inventory slot `slot` before a step changes it (once per slot until
/// restorePlayers; the player is remembered too).
void rememberSlot(Player& player, int slot);
/// Remembers the helmet before a step changes it (once until restorePlayers).
void rememberHead(Player& player);
/// Puts `stack` into the selected hotbar slot, that slot remembered first.
void setHeld(Player& player, ItemStack const& stack);
/// Puts `stack` on the head, the helmet remembered first.
void setHead(Player& player, ItemStack const& stack);
/// Puts `stack` into armour slot `slot` (1 chest, 2 legs, 3 feet; 0 is setHead), the slot's stack
/// remembered first (once until restorePlayers).
void setArmorPiece(Player& player, int slot, ItemStack const& stack);
/// The item data key (game::items::setData) of the armour the client fixture hides on its wearer
/// (render::hideArmor; upstream's emperor's cloth): the head step dresses the players in a chestplate with
/// it and boots without it.
inline constexpr char kHiddenArmorData[] = "gf_ui_hidden_armor";
/// Puts every remembered player that is in the level back (helmet, armour, every remembered slot, position
/// and rotation); players not in the level stay remembered. Returns how many players were restored.
int restorePlayers();
/// The players remembered now (memory and world DB).
[[nodiscard]] std::size_t savedPlayerCount();

/// The stage of the real-player steps: a 3 x 5 x 5 box in the ui selftest row (testPos(6260),
/// dz 0..4; rule R12) with a grass floor at y - 1 and air above; the player stands on `origin` (its
/// feet block, the north end) facing south, the chest / item frame goes two blocks south of it.
/// The first call keeps the box's blocks (in memory and in the world DB, gf:ui:fixture_stage) and
/// clears it; false when the region is not loaded.
bool prepareStage(BlockSource& region, BlockPos const& origin);
/// Moves `player` (remembered first) onto the stage origin, facing south and looking at
/// `targetHeight` blocks above the floor two blocks ahead.
void standOnStage(Player& player, BlockSource const& region, BlockPos const& origin, float targetHeight);
/// Puts the stage's blocks back as they were before prepareStage (item frames emptied first, so
/// nothing drops); returns the blocks changed. A stage that is not loaded keeps its snapshot for a
/// later clear; prepareStage then reuses that snapshot at the same place (and refuses a new stage
/// elsewhere).
int clearStage();
/// Drops the stage without putting it back (memory and world DB; selftests).
void discardStage();
/// Empties the item frame at `pos` (ItemFrameBlockActor::onRemoved drops the framed item when the
/// block is replaced); false when there is no frame or it holds nothing.
bool emptyFrame(BlockSource& region, BlockPos const& pos);
/// The names of the stage's simulated players (placeNameTagPlayers): the client fixture hides the
/// first one's name tag (render::hideNameTag by type and predicate) and leaves the second one's drawn.
inline constexpr char kHiddenNameTag[] = "GF_UI_hidden_tag";
inline constexpr char kShownNameTag[]  = "GF_UI_shown_tag";
/// Two simulated players at the south end of the stage (z + 4, x - 1 and x + 1), named kHiddenNameTag
/// and kShownNameTag (a player's name tag is always drawn), facing north; returns how many are there
/// (earlier ones are moved there).
int placeNameTagPlayers(BlockSource& region, BlockPos const& origin);
/// Disconnects those simulated players; clearStage and prepareStage do it too. Returns how many went.
int removeNameTagPlayers();
/// The item entities within `radius` blocks of `origin` (selftests: nothing dropped).
[[nodiscard]] std::size_t itemEntitiesNear(BlockSource& region, BlockPos const& origin, int radius);

// ---- blocks ----------------------------------------------------------------------------------

/// Places a vanilla chest at `pos` with the custom name `name` (a containerTitleSkin title; "" for
/// none).
bool placeNamedChest(BlockSource& region, BlockPos const& pos, std::string const& name);
/// The custom name of the chest at `pos` ("" when none).
[[nodiscard]] std::string chestName(BlockSource& region, BlockPos const& pos);
/// Places an item frame at `pos` facing north (a stone block south of it holds it) with `itemId`
/// in it. False when a block can't be placed or the item is unknown.
bool placeFrame(BlockSource& region, BlockPos const& pos, std::string const& itemId);
/// The same with a stack (a filled map with its map id).
bool placeFrame(BlockSource& region, BlockPos const& pos, ItemStack const& item);
/// The item id in the item frame at `pos` ("" when none).
[[nodiscard]] std::string frameItem(BlockSource& region, BlockPos const& pos);

// ---- input edges (the client scenario's jump) ------------------------------------------------

/// Logs the next `count` input edges (gf::movement::onInputEdge) of fixture players, one line each
/// ("ui fixture: edge <Edge> of <player>"); 0 stops.
void logEdges(int count);

// ---- the respawn step (gamerules) ----------------------------------------------------------------

/// Turns doimmediaterespawn and keepinventory on for the respawn step, their values before kept in the
/// world DB (gf:ui:fixture_rules; the first values stay until restoreRespawnRules); false when a value
/// could not be read or a command failed.
bool setRespawnRules();
/// Puts the two gamerules back to the kept values (also after a session that ended before); returns
/// how many were kept (0: nothing to put back).
int restoreRespawnRules();

[[nodiscard]] int clicks();
[[nodiscard]] int keyPresses();
[[nodiscard]] int formsShown();

} // namespace gf::ui::fixture
