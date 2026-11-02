#pragma once

// Block families (F11), client mod: the sign text colours (Java DyeColor.getTextColor and
// SignRenderer.getDarkColor), shared by the text drawn on the signs (SignText.cpp) and the sign
// editor (Screens.cpp), and the text the server sent for the signs ("families.sign_text").
// Client build only.

#ifdef GF_CLIENT

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Actor;

namespace gf::client::families {

/// Java DyeColor.getTextColor (0xRRGGBB); black for an unknown dye.
[[nodiscard]] unsigned dyeTextColor(std::string_view dye);

/// Java SignRenderer.getDarkColor (0xRRGGBB): black text that glows has the outline 0xF0EBCC,
/// any other text the dye's colour x 0.4 per channel.
[[nodiscard]] unsigned signDarkColor(unsigned textColor, bool glowing);

/// One side of a sign as the server sent it.
struct SignSide {
    std::array<std::string, 4> lines;
    std::string                color   = "black";
    bool                       glowing = false;
    [[nodiscard]] bool         empty() const;
};
struct SignText {
    SignSide                  front, back;
    std::vector<std::int64_t> display; ///< the unique ids of the sign's display entities (their name tags hidden)
};

/// The text the server sent for the sign at (dim, x, y, z) (nullopt: none).
[[nodiscard]] std::optional<SignText> signTextAt(int dim, int x, int y, int z);
/// The number of signs whose text the client knows (tests).
[[nodiscard]] std::size_t signTextCount();
/// The client mod hides the name tag of `actor`: a display entity (gloamingforest:sign_text) of the
/// client's level that the text of a sign names, while that text is not empty.
[[nodiscard]] bool hidesSignName(Actor const& actor);

} // namespace gf::client::families

#endif // GF_CLIENT
