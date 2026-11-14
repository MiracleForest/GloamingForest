#pragma once

// Extension of contract "enchant+effects" (mob effects) by F9 (rule R18; to be folded into
// feature/effects/Effects.h by A8).
//
// The client mod draws a custom effect's icon (Java's MobEffectTextureManager sprite) on the HUD and
// in the inventory's effect list. By default the icon is the resource pack texture
// "textures/<namespace>/mob_effect/<path>" (where F1 imports upstream's textures/mob_effect/*.png:
// "textures/gloamingforest/mob_effect/frosted"); an owner may name another one:
//
//     gf::effects::ext::setIcon("gloamingforest:frosted", "textures/gloamingforest/mob_effect/frosted");

#include <string>

namespace gf::effects::ext {

/// The resource pack texture (18 x 18, as Java's mob effect sprites) drawn for `effectId`.
void setIcon(std::string const& effectId, std::string const& texture);

} // namespace gf::effects::ext
