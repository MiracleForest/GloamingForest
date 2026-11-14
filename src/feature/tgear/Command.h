// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// The parameters of /gloamingforest travellers_gear (Command.cpp). Command parameter types need
// external linkage.
#pragma once

#include <string>

#include "ll/api/command/SoftEnum.h"

namespace gf::feature::tgear {

/// The soft enum of <modifier>: the keys of the gloamingforest:travellers_modifiers registry (upstream's
/// ResourceKeyArgument suggestions), filled when the command is registered.
enum class TravellersModifierKey {};

struct TravellersGearParams {
    ll::command::SoftEnum<TravellersModifierKey> modifier;
};

/// Any other word: upstream's "%s is not a valid Traveller's Gear Modifier" (the soft enum's parse would reject
/// it before the command runs).
struct TravellersGearAnyParams {
    std::string modifier;
};

} // namespace gf::feature::tgear
