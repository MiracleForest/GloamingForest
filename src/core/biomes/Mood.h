// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/registration/biomes/BiomeHelper.java (the mood settings)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U1 `biomes`, pure part: the biomes' mood sound (BiomeSpecialEffects#ambientMoodSettings, TF's
// AmbientMoodSettings.LEGACY_CAVE_SETTINGS: ambient.cave, tick delay 6000, block search extent 8,
// sound offset 2.0) as BiomeAmbientSoundsHandler#tick plays it: each tick one random block within
// the extent of the player's eyes adds to the player's moodiness when it is dark (no sky light: one
// tick of 1 / tickDelay per missing block light level) and takes from it when it sees the sky; at 1
// the sound plays from that block's direction, `offset` blocks further than the block.

namespace gf::core::biomes::mood {

struct Settings {
    int    tickDelay   = 6000;
    int    blockSearch = 8;
    double offset      = 2.0;
};

/// TF's mood settings (every TF biome).
inline constexpr Settings kTwilight{};

/// The sample block of a tick: the player's eye block plus `nextInt(2 * extent + 1) - extent` per
/// axis (Java draws x, y, z in that order).
struct Sample {
    int dx = 0, dy = 0, dz = 0;
};

/// One tick of BiomeAmbientSoundsHandler's mood part for the sample's light levels (`sky`, `block`:
/// 0..15, LightLayer.SKY / BLOCK at the sample). Updates `moodiness`; true when the sound plays now
/// (moodiness is reset to 0 then).
[[nodiscard]] bool step(float& moodiness, int sky, int block, Settings const& settings = kTwilight);

struct Vec {
    double x = 0, y = 0, z = 0;
};
/// Where the sound plays: from the player's eyes `eye` toward the centre of the sample block
/// `block`, at the block's distance plus the offset (SimpleSoundInstance.forAmbientMood's position).
[[nodiscard]] Vec soundPosition(Vec const& eye, int blockX, int blockY, int blockZ, Settings const& settings = kTwilight);

/// SimpleSoundInstance.forAmbientMood: volume 0.5, pitch 1.0.
inline constexpr float kVolume = 0.5f;
inline constexpr float kPitch  = 1.0f;

} // namespace gf::core::biomes::mood
