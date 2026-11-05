// U15 contract extension (rule R18: new files, to be folded into F12's client.world by A8). Client
// build only.
//
// F12's SkyProvider gives a dimension its cloud height but not the clouds' colour, and Bedrock tints
// the sky and the clouds with its own sunrise / sunset colour (time 13000 in the Twilight Forest is
// Bedrock's dusk). TF's TwilightForestRenderInfo has no sunrise colour (getSunriseColor returns
// null) and its clouds take ClientLevel#getCloudColor. A view override sets, for the frame of a
// dimension, the colour the clouds are drawn in and hides the sunrise colour band
// (ViewRenderObject::mCloudState.mCloudColor, mViewData.mSunriseColor, set in
// LevelRendererCamera::$render before the game draws the frame).
//
//     gf::feature::ext::atmosphere::client::setViewOverride(tfDim, [] { return ViewOverride{.cloud = rgb, .noSunrise = true}; });

#pragma once

#ifdef GF_CLIENT

#include <array>
#include <functional>
#include <optional>

namespace gf::feature::ext::atmosphere::client {

struct ViewOverride {
    std::optional<std::array<float, 3>> cloud;           ///< the clouds' colour (rgb 0..1); nullopt: the game's
    bool                                noSunrise = false; ///< no sunrise / sunset colour band
};
using ViewFn = std::function<std::optional<ViewOverride>()>;
/// The override of `dimension` (engine id); replaces the previous one.
void setViewOverride(int dimension, ViewFn fn);
void clearViewOverride(int dimension);
/// Frames the override of the current dimension was applied in (selftests).
[[nodiscard]] unsigned long long viewOverrideFrames();

} // namespace gf::feature::ext::atmosphere::client

#endif // GF_CLIENT
