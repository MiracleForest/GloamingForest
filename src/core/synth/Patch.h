// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Contract "synth" (F3): the patch interpreter (procedural sound recipes -> PCM).
#pragma once

// A patch is a JSON description of a sound: parameters, layers (oscillators, noise, FM, plucked
// strings, modal resonators, grain clouds, nested patches) with envelopes, filters, formants,
// drive and repetition, then patch-wide effects (reverb, echo, chorus, drive, filter, crush,
// reverse) and peak normalisation. The language is documented in tools/sounds/README.md.
//
// Numbers may be expressions: "$pitch*220", "rand(0.9,1.1)", "semi(-3)", "choose(1,2,4)".
// rand() draws from the render's seed, so variants of one patch differ by seed only and every
// render is reproducible bit for bit.

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/synth/Json.h"

namespace gf::core::synth {

/// Named patches ("creature/growl" -> JSON) for nested "patch" layers and sequencer instruments.
class PatchLibrary {
public:
    void                      add(std::string name, Json patch) { mPatches[std::move(name)] = std::move(patch); }
    [[nodiscard]] Json const* find(std::string const& name) const {
        auto it = mPatches.find(name);
        return it == mPatches.end() ? nullptr : &it->second;
    }
    [[nodiscard]] std::size_t size() const { return mPatches.size(); }

private:
    std::map<std::string, Json> mPatches;
};

struct PatchError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// Renders `patch` (an object, or a string naming a library patch) with parameter overrides
/// `params` (object of numbers/expressions) at `sampleRate`. Throws PatchError on bad input.
[[nodiscard]] std::vector<float> renderPatch(
    Json const&         patch,
    Json const&         params,
    std::uint64_t       seed,
    int                 sampleRate,
    PatchLibrary const* library = nullptr
);

/// Evaluates an expression with the given parameters (tests and tools).
[[nodiscard]] double
evalExpression(std::string const& expr, std::map<std::string, double> const& params, std::uint64_t seed = 0);

namespace detail {
/// Patch-wide post-processing shared with the sequencer: effects, normalisation, trim, fade.
void finish(
    std::vector<double>&                 buf,
    Json const&                          spec,
    std::map<std::string, double> const& params,
    std::uint64_t                        seed,
    int                                  rate,
    bool                                 trim
);
} // namespace detail

} // namespace gf::core::synth
