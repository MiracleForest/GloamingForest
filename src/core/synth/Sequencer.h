// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Contract "synth" (F3): the tracker-style music sequencer.
#pragma once

// A track is JSON: tempo (bpm, rows per beat), instruments (patches with parameters), patterns
// of channels written as note cells ("C4", "F#3:0.6", "C4+E4+G4", "." continue, "-" off,
// "C4@bell" instrument override) and an order list ({"p": "A", "t": 5} transposes) that loops
// until the track's length. Every note renders its instrument patch with the parameters
// freq, note, vel and dur (seconds), so one patch library serves effects and music alike. The
// format is documented in tools/sounds/README.md.

#include <cstdint>
#include <string>
#include <vector>

#include "core/synth/Json.h"
#include "core/synth/Patch.h"

namespace gf::core::synth {

/// MIDI note number of a cell note ("C4" = 60, "A4" = 69, "Bb2", "F#3"); -1 when not a note.
[[nodiscard]] int parseNote(std::string const& text);

/// Renders a track at `sampleRate` (the track's own "rate" when 0).
[[nodiscard]] std::vector<float> renderTrack(Json const& track, int sampleRate, PatchLibrary const* library);

} // namespace gf::core::synth
