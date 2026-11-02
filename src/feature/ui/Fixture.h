#pragma once

// The F12 fixture, server side (selftests "ui.*" and the e2e scenarios): see Fixture.cpp.

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
/// runs never touch an untagged player (rule R12), so `/twilightforest selftest` in a world with
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

/// Remembers what the fixture is about to change on `player`, once until restorePlayers: dimension,
/// feet position, rotation, the selected hotbar slot and its stack, the helmet.
void rememberPlayer(Player& player);
/// Puts every remembered player back (helmet, the stack of the remembered slot, position and
/// rotation); returns how many players were restored.
int restorePlayers();

/// The stage of the real-player steps: a 3 x 5 x 5 box in the ui selftest row (testPos(6260),
/// dz 0..4; rule R12) with a grass floor at y - 1 and air above; the player stands on `origin` (its
/// feet block, the north end) facing south, the chest / item frame goes two blocks south of it.
/// The first call keeps the box's blocks (in memory and in the world DB, gf:ui:fixture_stage) and
/// clears it; false when the region is not loaded.
bool prepareStage(BlockSource& region, BlockPos const& origin);
/// Moves `player` (remembered first) onto the stage origin, facing south and looking at
/// `targetHeight` blocks above the floor two blocks ahead.
void standOnStage(Player& player, BlockSource const& region, BlockPos const& origin, float targetHeight);
/// Puts the stage's blocks back as they were before prepareStage; returns the blocks changed.
int clearStage();

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

[[nodiscard]] int clicks();
[[nodiscard]] int keyPresses();
[[nodiscard]] int formsShown();

} // namespace gf::ui::fixture
