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

/// The players that are not SimulatedPlayers (the e2e client's player).
[[nodiscard]] std::vector<Player*> realPlayers();
/// Sends "ui.fixture" `what` to a companion ({"show": true, "fog": true, ...}); false for vanilla players.
bool show(Player& player, nlohmann::json const& what);
/// The vanilla fallback of screen "ui.fixture" (a form).
void openFormFallback(Player& player);
/// The DDUI document of the fixture (vanilla players).
[[nodiscard]] nlohmann::json dduiDocument();

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
/// Removes the blocks the fixture placed for real players; returns how many.
int clearPlaced();

[[nodiscard]] int clicks();
[[nodiscard]] int keyPresses();
[[nodiscard]] int formsShown();

} // namespace gf::ui::fixture
