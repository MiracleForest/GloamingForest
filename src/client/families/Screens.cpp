// Block families (F11), client mod: the native screens of the families (gf::screens types opened
// by the server, src/feature/families):
// - "families.sign": Java's SignEditScreen / HangingSignEditScreen: the title, the sign drawn
//   large with its four lines on it (typing stops at Java's line width: 90 px, hanging signs 60 px)
//   and Done; the lines go to the server as they change and are kept when the screen closes
//   (SignEditScreen.removed).
// The chests of the families are the inventory contract's containers: their screen is F10's
// "inventory.chest" (src/client/inventory).

#ifdef GF_CLIENT

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>

#include "mc/locale/I18n.h"
#include "mc/locale/Localization.h"

#include "client/Client.h"
#include "client/families/SignBoard.h"
#include "client/screen/base/Screen.h"
#include "feature/families/SignFont.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::families {

namespace {

using screen::Button;
using screen::Json;
using screen::Rect;
namespace color = screen::color;

/// A bevelled button with a label.
class TextButton : public screen::Widget {
public:
    TextButton(Rect r, std::string text, std::function<void()> onClick) : Widget(r), text(std::move(text)), onClick(std::move(onClick)) {}
    std::string           text;
    std::function<void()> onClick;
    void render(screen::Canvas& c, float mx, float my) override {
        c.bevel(rect, color::kButton, color::kPanelLight, color::kPanelDark, false);
        if (hovered(mx, my)) c.fill(rect, color::kHover);
        c.text(rect.x + rect.w / 2, rect.y + (rect.h - 8) / 2, text, color::kText, screen::Align::Centre);
    }
    bool click(float mx, float my, Button b, bool) override {
        if (b != Button::Left || !hovered(mx, my)) return false;
        if (onClick) onClick();
        return true;
    }
};

// Java's default font advances: the server cuts the lines with the same table (SignFont.h)
using feature::families::signfont::charWidth;
using feature::families::signfont::lineWidth;

/// A text of the mod's translations, else the game's own (Bedrock's texts know Java's gui.done and
/// sign.edit), else the English fallback.
std::string uiText(char const* key, char const* fallback) {
    auto mod = ctr(key, "\x01");
    if (mod != "\x01") return mod;
    try {
        auto& i18n = getI18n();
        auto  text = i18n.get(key, {}, i18n.getCurrentLanguage().get());
        if (!text.empty() && text != key) return text;
    } catch (...) {}
    return fallback;
}

/// The editor's title: Java's key (title_key) in the player's language, else the server's text.
std::string titleOf(Json const& data) {
    auto fallback = data.value("title", std::string{"Edit Sign Message"});
    auto key      = data.value("title_key", std::string{});
    return key.empty() ? fallback : uiText(key.c_str(), fallback.c_str());
}

/// Java DyeColor.getTextColor.
unsigned dyeTextColor(std::string const& dye) {
    static std::map<std::string, unsigned> const colours{
        {"white", 0xFFFFFF}, {"orange", 0xFF681F}, {"magenta", 0xFF00FF}, {"light_blue", 0x9AC0CD}, {"yellow", 0xFFFF00}, {"lime", 0xBFFF00},
        {"pink", 0xFF69B4},  {"gray", 0x808080},   {"light_gray", 0xD3D3D3}, {"cyan", 0x00FFFF},    {"purple", 0xA020F0}, {"blue", 0x0000FF},
        {"brown", 0x8B4513}, {"green", 0x00FF00},  {"red", 0xFF0000},       {"black", 0x000000},
    };
    auto it = colours.find(dye);
    return it == colours.end() ? 0 : it->second;
}

/// The colour SignEditScreen writes with: the dye's colour when glowing (black glows 0xF0EBCC),
/// else AbstractSignRenderer.getDarkColor (the dye's colour x 0.4).
unsigned signTextColor(std::string const& dye, bool glowing) {
    unsigned c = dyeTextColor(dye);
    if (glowing) return 0xFF000000 | (c == 0 ? 0xF0EBCC : c);
    auto dark = [&](int shift) { return static_cast<unsigned>(((c >> shift) & 0xFF) * 0.4) << shift; };
    return 0xFF000000 | dark(16) | dark(8) | dark(0);
}

/// Java's SignEditScreen / HangingSignEditScreen (AbstractSignEditScreen): the sign drawn large in
/// the middle (the board, and the stick of a standing sign, from the sign's entity texture; a
/// hanging sign's GUI texture x 4.5), the four lines on it in the sign's colour with a blinking
/// cursor on the line being edited (Enter: the next line), the title above and Done below. The
/// screen has no panel of its own: its origin is the middle of the screen, where the sign is.
class SignEditScreen final : public screen::Screen {
public:
    SignEditScreen(Json const& data, screen::SendFn send)
    : Screen("", 0, 0),
      mSend(std::move(send)),
      mData(data),
      mTitle(titleOf(data)),
      mTexture(data.value("texture", std::string{})),
      mMaxWidth(data.value("max_width", 90)),
      mHanging(data.value("hanging", false)),
      mStanding(data.value("standing", false)),
      mColor(signTextColor(data.value("color", std::string{"black"}), data.value("glowing", false))) {}

    void init() override {
        auto lines = mData.value("lines", Json::array());
        for (int i = 0; i < 4; ++i) {
            std::string current = i < static_cast<int>(lines.size()) && lines[i].is_string() ? lines[i].get<std::string>() : "";
            // the lines are drawn on the sign (renderExtra): the fields only take the keys, and
            // their box is the whole screen so a click never leaves the line being edited
            auto& f     = add<screen::TextField>(Rect{-10000, -10000, 20000, 20000}, current);
            f.visible   = false;
            f.maxLength = 384;
            f.onChange  = [this](std::string const&) { sendLines("lines"); };
            // SignEditScreen.keyPressed: Enter goes to the next line (line + 1 & 3)
            f.onSubmit = [this, i](std::string const&) {
                sendLines("lines");
                if (static_cast<std::size_t>(i) < mFields.size()) mFields[(i + 1) % mFields.size()]->focus();
            };
            // Java SignEditScreen: Esc closes the editor and keeps the typed lines (removed() sends
            // them); the field's own Esc put back the text it had when focused: last frame's again
            f.onCancel = [this, i] {
                if (i < static_cast<int>(mShadow.size())) mFields[i]->text = mShadow[i];
                sendLines("done");
            };
            // SignEditScreen: no character past the line width
            f.filter = [this, i](char32_t c) {
                std::string s = mFields[i]->text;
                return lineWidth(s) + charWidth(c) <= mMaxWidth;
            };
            mFields.push_back(&f);
            mShadow.push_back(current);
        }
        mDone = &add<TextButton>(Rect{-100, doneY(), 200, 20}, uiText("gui.done", "Done"), [this] { sendLines("done"); });
        if (!mFields.empty()) mFields[0]->focus();
        log().info("client screen: families.sign");
    }

    void tick() override {
        for (std::size_t i = 0; i < mFields.size() && i < mShadow.size(); ++i) mShadow[i] = mFields[i]->text;
        if (mDone) mDone->rect.y = doneY(); // the GUI size may change while open
    }

    void renderExtra(screen::Canvas& c, float, float) override {
        auto [sw, sh] = screen::guiSize();
        // AbstractSignEditScreen.render: the title at y 40 (sign pivot at 90, a hanging sign's at
        // 125), white with a shadow
        float titleY = std::max(-sh / 2 + 4, mHanging ? -85.f : -50.f);
        c.text(0, titleY, mTitle, 0xFFFFFFFF, screen::Align::Centre, true);
        drawSign(c);
        // AbstractSignEditScreen.renderSignText: centred lines (10 px apart, a hanging sign's 9),
        // the cursor blinking on the line being edited ("_" at its end, else a bar)
        float lh    = mHanging ? 9.f : 10.f;
        float scale = mHanging ? 1.f : 0.9765628f;
        bool  blink = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 300 % 2 == 0;
        for (std::size_t i = 0; i < mFields.size(); ++i) {
            auto const& s = mFields[i]->text;
            float       y = (static_cast<float>(i) * lh - 2 * lh) * scale;
            float       w = c.textWidth(s);
            c.text(-w / 2, y, s, mColor, screen::Align::Left, false);
            if (!mFields[i]->focused() || !blink) continue;
            auto  u     = decode(s);
            int   caret = std::clamp(mFields[i]->caret, 0, static_cast<int>(u.size()));
            float cx    = -w / 2 + c.textWidth(encode(u.substr(0, caret)));
            if (caret >= static_cast<int>(u.size())) c.text(cx, y, "_", mColor, screen::Align::Left, false);
            else c.fill(Rect{cx, y - 1, 1, 10}, mColor);
        }
    }

private:
    /// Done at Java's height / 4 + 144 (relative to the sign pivot), kept on the screen and below
    /// the sign.
    float doneY() const {
        auto [sw, sh] = screen::guiSize();
        float java    = sh / 4 + (mHanging ? 19.f : 54.f);
        float below   = mStanding ? 82.f : 27.f;
        float bottom  = sh / 2 - 22; // always on the screen, over the stick on a very small GUI
        return std::min(std::max(below, std::min(java, bottom)), bottom);
    }

    void drawSign(screen::Canvas& c) {
        if (mHanging) {
            // HangingSignEditScreen.renderSignBackground: translate(0, -13), scale 4.5, the 16 x 16
            // GUI texture at (-8, -8)
            Rect board{-36, -49, 72, 72};
            if (!blit(c, mTexture, board, 0, 0, 16, 16, 16, 16)) c.fill(Rect{-36, -31, 72, 54}, 0xFF8F6B3E);
            return;
        }
        // SignEditScreen.renderSignBackground: translate(0, 31), scale 62.5 (3.90625 px per model
        // pixel): the board (-12, -14, 24 x 12; its front face at 2, 2 of the 64 x 32 texture)
        // and, for a standing sign, the stick (-1, -2, 2 x 14; at 2, 16)
        constexpr float k = 62.500004f / 16;
        Rect board{-12 * k, 31 - 14 * k, 24 * k, 12 * k};
        if (!blit(c, mTexture, board, 2, 2, 24, 12, 64, 32)) c.fill(board, 0xFF8F6B3E);
        if (mStanding) {
            Rect stick{-1 * k, 31 - 2 * k, 2 * k, 14 * k};
            if (!blit(c, mTexture, stick, 2, 16, 2, 14, 64, 32)) c.fill(stick, 0xFF5E4527);
        }
    }

    static std::u32string decode(std::string const& s) {
        std::u32string out;
        for (std::size_t i = 0; i < s.size();) {
            unsigned char c   = static_cast<unsigned char>(s[i]);
            std::size_t   len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
            char32_t      cp  = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
            for (std::size_t k = 1; k < len && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
            out.push_back(cp);
            i += len;
        }
        return out;
    }
    static std::string encode(std::u32string const& u) {
        std::string out;
        for (char32_t cp : u) {
            if (cp < 0x80) out += static_cast<char>(cp);
            else if (cp < 0x800) out += {static_cast<char>(0xC0 | (cp >> 6)), static_cast<char>(0x80 | (cp & 0x3F))};
            else if (cp < 0x10000) out += {static_cast<char>(0xE0 | (cp >> 12)), static_cast<char>(0x80 | ((cp >> 6) & 0x3F)), static_cast<char>(0x80 | (cp & 0x3F))};
            else out += {static_cast<char>(0xF0 | (cp >> 18)), static_cast<char>(0x80 | ((cp >> 12) & 0x3F)), static_cast<char>(0x80 | ((cp >> 6) & 0x3F)), static_cast<char>(0x80 | (cp & 0x3F))};
        }
        return out;
    }

    void sendLines(char const* op) {
        Json lines = Json::array();
        for (auto* f : mFields) lines.push_back(f->text);
        mSend({{"op", op}, {"lines", lines}});
    }

    screen::SendFn                  mSend;
    Json                            mData;
    std::string                     mTitle;
    std::string                     mTexture;
    int                             mMaxWidth;
    bool                            mHanging;
    bool                            mStanding;
    unsigned                        mColor;
    TextButton*                     mDone = nullptr;
    std::vector<screen::TextField*> mFields;
    std::vector<std::string>        mShadow; ///< the lines as of the last frame (Esc keeps them)
};

class FamilyScreensModule {
public:
    void install() {
        screen::registerServerScreen("families.sign", [](Json const& data, screen::SendFn send) { return std::make_unique<SignEditScreen>(data, std::move(send)); });
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::client::families

GF_REGISTER_MODULE(gf::client::families::FamilyScreensModule);

#endif // GF_CLIENT
