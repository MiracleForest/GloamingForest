#pragma once

// Contract extension of U4 woodwork (rule R18: new files only; A8 folds it into F9's data maps).
// Java burns every member of the vanilla fuel item tags (AbstractFurnaceBlockEntity#buildFuels,
// NeoForge's neoforge:furnace_fuels data map of vanilla: #minecraft:logs 300, #minecraft:planks 300,
// #minecraft:wooden_slabs 150, ...), TF's wood sets included; the imported neoforge:furnace_fuels map
// holds only TF's own rows (#twilightforest:banisters), so F9's furnace hook gives the TF wood items
// no fuel value. A provider gives the burn time (Java ticks) of the items that have no data map row:
//
//     gf::woodwork_ext::fuel::setProvider([](std::string const& itemId) -> std::optional<int> { ... });
//
// The value is scaled from coal's (Java's 1600 ticks) as F9 does, so it follows the engine's own
// unit and furnace speed. Items with a neoforge:furnace_fuels row and vanilla items keep their value.
// Server and client thread (the client mod's fuel slot checks). One provider.

#include <functional>
#include <optional>
#include <string>

namespace gf::woodwork_ext::fuel {

using ProviderFn = std::function<std::optional<int>(std::string const& itemId)>;
void setProvider(ProviderFn fn);
/// The burn time the provider gives `itemId` (nullopt: none), for tests.
[[nodiscard]] std::optional<int> burnTicks(std::string const& itemId);

/// Installs / removes the FurnaceBlockActor::getBurnDuration hook.
void install();
void uninstall();

} // namespace gf::woodwork_ext::fuel
