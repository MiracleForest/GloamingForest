// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/config/TFClientConfig.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U15 (atmosphere), client mod: the editor of TFClientConfig's auroraBiomes list ("Aurora Shader
// Biomes", config.gloamingforest.aurora_biomes; NeoForge's list screen behind its "Edit Biomes"
// button, .button; the explanation of .tooltip). One text box per biome id, a remove button per
// row, an add button and Done: the list goes to the client's own config/atmosphere.json (a client
// joined to a remote server edits its own copy) and applies at once. Opened as the screen type
// "atmosphere.aurora_biomes" (feature/atmosphere/AuroraBiomesScreen.cpp).

#ifdef GF_CLIENT

#include <algorithm>

#include "client/Client.h"
#include "client/atmosphere/AtmosphereClient.h"
#include "client/screen/base/Screen.h"
#include "client/ui/Ui.h"
#include "core/atmosphere/Atmosphere.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::atmosphere {

namespace {

namespace sc = gf::client::screen;

/// A bevelled button with a text label.
class TextButton : public sc::Widget {
public:
    TextButton(sc::Rect r, std::string text, std::function<void()> onClick) : Widget(r), text(std::move(text)), onClick(std::move(onClick)) {}
    std::string           text;
    std::function<void()> onClick;
    void render(sc::Canvas& c, float mx, float my) override {
        c.bevel(rect, enabled ? sc::color::kButton : sc::color::kSlotDark, sc::color::kPanelLight, sc::color::kPanelDark, false);
        if (enabled && hovered(mx, my)) c.fill(rect, sc::color::kHover);
        c.text(rect.x + rect.w / 2, rect.y + (rect.h - 8) / 2, text, enabled ? sc::color::kText : sc::color::kTextGray, sc::Align::Centre);
    }
    bool click(float mx, float my, sc::Button b, bool) override {
        if (!enabled || b != sc::Button::Left || !hovered(mx, my)) return false;
        if (onClick) onClick();
        return true;
    }
};

/// The panel: forwards the wheel to the list.
class WheelArea : public sc::Widget {
public:
    explicit WheelArea(std::function<void(int)> fn) : Widget(sc::Rect{0, 0, 10000, 10000}), mFn(std::move(fn)) {}
    void render(sc::Canvas&, float, float) override {}
    bool scroll(float, float, int delta, bool) override {
        mFn(delta);
        return true;
    }

private:
    std::function<void(int)> mFn;
};

/// Splits `text` into lines of at most `width` GUI pixels.
std::vector<std::string> wrap(sc::Canvas& c, std::string const& text, float width) {
    std::vector<std::string> lines;
    std::string              line, word;
    auto flushWord = [&] {
        if (word.empty()) return;
        std::string next = line.empty() ? word : line + " " + word;
        if (!line.empty() && c.textWidth(next) > width) {
            lines.push_back(line);
            line = word;
        } else line = next;
        word.clear();
    };
    for (char ch : text) {
        if (ch == ' ') flushWord();
        else word += ch;
    }
    flushWord();
    if (!line.empty()) lines.push_back(line);
    return lines;
}

class AuroraBiomesScreen : public sc::Screen {
public:
    static constexpr float kWidth = 260, kHeight = 196, kRow = 20, kListTop = 52;
    static constexpr int   kRows  = 6;

    AuroraBiomesScreen() : Screen(ctr("config.gloamingforest.aurora_biomes", "Aurora Shader Biomes"), kWidth, kHeight), mBiomes(auroraBiomesConfigured()) {}

    void init() override { rebuild(); }
    /// Widgets are rebuilt between frames, never from inside one of their own callbacks.
    void tick() override {
        if (mDirty) {
            mDirty = false;
            rebuild();
        }
    }

    void renderExtra(sc::Canvas& c, float, float) override {
        auto lines = wrap(c, ctr("config.gloamingforest.aurora_biomes.tooltip", "Defines which biomes the aurora shader effect will appear in. Leave the list empty to disable the effect."), kWidth - 16);
        float y = 18;
        for (auto const& l : lines) {
            c.text(8, y, l, sc::color::kTextDark, sc::Align::Left, false);
            y += 10;
        }
        if (mBiomes.empty()) c.text(kWidth / 2, kListTop + 6, ctr("gf.atmosphere.aurora_biomes.empty", "(empty: no aurora)"), sc::color::kTextGray, sc::Align::Centre, false);
        for (std::size_t i = 0; i < mBiomes.size(); ++i) {
            auto rl = core::atmosphere::parseResourceLocation(mBiomes[i]);
            int  row = static_cast<int>(i) - mScroll;
            if (row < 0 || row >= kRows || rl) continue;
            c.text(kWidth - 46, kListTop + row * kRow + 4, "!", sc::color::kButtonOff, sc::Align::Centre, false); // not a valid id: skipped
        }
        if (!mStatus.empty()) c.text(8, kHeight - 32, mStatus, sc::color::kTextDark, sc::Align::Left, false);
    }

private:
    void collect() {
        for (auto& [index, field] : mFields)
            if (index < mBiomes.size()) mBiomes[index] = field->text;
    }

    void rebuild() {
        widgets.clear();
        mFields.clear();
        add<WheelArea>([this](int delta) {
            collect();
            int maxScroll = std::max(0, static_cast<int>(mBiomes.size()) - kRows);
            mScroll       = std::clamp(mScroll - delta, 0, maxScroll);
            mDirty = true;
        });
        for (int row = 0; row < kRows; ++row) {
            std::size_t i = static_cast<std::size_t>(mScroll + row);
            if (i >= mBiomes.size()) break;
            float y     = kListTop + row * kRow;
            auto& field = add<sc::TextField>(sc::Rect{8, y, kWidth - 60, 16}, mBiomes[i]);
            field.maxLength = 128;
            field.placeholder = "gloamingforest:glacier";
            field.onChange    = [this, i](std::string const& t) {
                if (i < mBiomes.size()) mBiomes[i] = t;
            };
            mFields.emplace_back(i, &field);
            add<TextButton>(sc::Rect{kWidth - 36, y, 28, 16}, "-", [this, i] {
                collect();
                if (i < mBiomes.size()) mBiomes.erase(mBiomes.begin() + static_cast<std::ptrdiff_t>(i));
                mScroll = std::clamp(mScroll, 0, std::max(0, static_cast<int>(mBiomes.size()) - kRows));
                mDirty = true;
            });
        }
        float const bottom = kHeight - 22;
        add<TextButton>(sc::Rect{8, bottom, 76, 16}, "+", [this] {
            collect();
            mBiomes.emplace_back();
            mScroll = std::max(0, static_cast<int>(mBiomes.size()) - kRows);
            mDirty = true;
        });
        add<TextButton>(sc::Rect{92, bottom, 76, 16}, ctr("gf.atmosphere.aurora_biomes.reset", "Reset"), [this] {
            mBiomes = {"gloamingforest:glacier"};
            mScroll = 0;
            mDirty = true;
        });
        add<TextButton>(sc::Rect{kWidth - 84, bottom, 76, 16}, ctr("gui.done", "Done"), [this] {
            collect();
            std::vector<std::string> out;
            for (auto const& b : mBiomes)
                if (!b.empty()) out.push_back(b);
            bool saved = setAuroraBiomes(out);
            mStatus    = saved ? "" : ctr("gf.atmosphere.aurora_biomes.not_saved", "Could not save config/atmosphere.json");
            if (saved) sc::close();
        });
    }

    std::vector<std::string>                          mBiomes;
    std::vector<std::pair<std::size_t, sc::TextField*>> mFields;
    int                                               mScroll = 0;
    bool                                              mDirty  = false;
    std::string                                       mStatus;
};

class AuroraBiomesScreenClientModule {
public:
    void install() {
        sc::registerScreen("atmosphere.aurora_biomes", [](sc::Json const&, sc::SendFn) -> std::unique_ptr<sc::Screen> {
            log().info("atmosphere: aurora biome editor opened");
            return std::make_unique<AuroraBiomesScreen>();
        });
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::client::atmosphere

GF_REGISTER_MODULE(gf::client::atmosphere::AuroraBiomesScreenClientModule);

#endif // GF_CLIENT
