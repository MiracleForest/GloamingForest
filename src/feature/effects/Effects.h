#pragma once

// Contract "enchant+effects", mob effects (header owned by F0, implemented by F9; spike S6).
//
// Bedrock's effect ids are fixed (vanilla clients know 38), so upstream's effect (frosted) is a native
// effect with a vanilla *carrier* effect that vanilla clients see (frosted: a hidden Slowness);
// the client mod shows the exact HUD icon.
//
//     gf::effects::registerType("gloamingforest:frosted", {.carrier = "minecraft:slowness", .color = 0x56CBFD});
//     gf::effects::apply(target, "gloamingforest:frosted", 100, 1);

#include <functional>
#include <string>

class Actor;

namespace gf::effects {

struct Spec {
    std::string carrier;          ///< the vanilla effect shown to vanilla clients ("" = none)
    bool        hideCarrierParticles = true;
    unsigned    color      = 0xFFFFFF;
    bool        beneficial = false;
    std::string nameKey;          ///< "effect.gloamingforest.frosted"
    /// Every tick while active (applyEffectTick), with the amplifier.
    std::function<void(Actor& actor, int amplifier)> tick;
    /// How often `tick` runs (shouldApplyEffectTickThisTick); 1 = every tick.
    int tickInterval = 1;
};
void registerType(std::string const& effectId, Spec spec);

/// Adds the effect for `ticks` with `amplifier` (0 = level I); also vanilla ids.
bool apply(Actor& actor, std::string const& effectId, int ticks, int amplifier);
[[nodiscard]] bool has(Actor const& actor, std::string const& effectId);
/// -1 when the effect is not active.
[[nodiscard]] int amplifier(Actor const& actor, std::string const& effectId);
void              remove(Actor& actor, std::string const& effectId);

} // namespace gf::effects
