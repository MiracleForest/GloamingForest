// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// The parameters of /twilightforest travellers_gear (Command.cpp). Command parameter types need
// external linkage.
#pragma once

#include <string>

#include "ll/api/command/SoftEnum.h"

namespace gf::feature::tgear {

/// The soft enum of <modifier>: the keys of the twilightforest:travellers_modifiers registry (TF's
/// ResourceKeyArgument suggestions), filled when the command is registered.
enum class TravellersModifierKey {};

struct TravellersGearParams {
    ll::command::SoftEnum<TravellersModifierKey> modifier;
};

} // namespace gf::feature::tgear
