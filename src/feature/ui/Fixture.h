#pragma once

// The F12 fixture, server side (selftests "ui.*" and the e2e scenarios): see Fixture.cpp.

#include <string>
#include <vector>

#include "nlohmann/json.hpp"

class BlockPos;
class BlockSource;
class Player;

namespace gf::ui::fixture {

/// The players that are not SimulatedPlayers (the e2e client's player).
[[nodiscard]] std::vector<Player*> realPlayers();
/// Sends "ui.fixture" `what` to a companion ({"show": true, "fog": true, ...}); false for vanilla players.
bool show(Player& player, nlohmann::json const& what);
/// The vanilla fallback of screen "ui.fixture" (a form).
void openFormFallback(Player& player);

/// Places a vanilla chest at `pos` with the custom name `name` (a containerTitleSkin title).
bool placeNamedChest(BlockSource& region, BlockPos const& pos, std::string const& name);
/// The custom name of the chest at `pos` ("" when none).
[[nodiscard]] std::string chestName(BlockSource& region, BlockPos const& pos);
/// Removes the chests placed for real players; returns how many.
int clearChests();

[[nodiscard]] int clicks();
[[nodiscard]] int keyPresses();
[[nodiscard]] int formsShown();

} // namespace gf::ui::fixture
