// Block families (F11), client mod: the native screens of the families (gf::screens types opened
// by the server, src/feature/families):
// - "families.sign": Java's SignEditScreen / HangingSignEditScreen: the title, the sign's four
//   lines (typing stops at Java's line width: 90 px, hanging signs 60 px) and Done; the lines go to
//   the server as they change and are kept when the screen closes (SignEditScreen.removed);
// - "families.chest": Java's ChestMenu: 3 rows (a double chest: 6) over the player's inventory,
//   the title top left; clicks go to the server, which owns the items and the cursor stack.

#ifdef GF_CLIENT

#include <algorithm>
#include <memory>

#include "client/screen/base/Screen.h"
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

/// Java's default font advances (the server cuts lines the same way).
int charWidth(char32_t c) {
    switch (c) {
    case '!': case '\'': case ',': case '.': case ':': case ';': case '|': case 'i': return 2;
    case '`': case 'l': return 3;
    case ' ': case 'I': case '[': case ']': case 't': return 4;
    case '"': case '(': case ')': case '*': case '<': case '>': case 'f': case 'k': case '{': case '}': return 5;
    case '@': case '~': return 7;
    default: return c < 0x80 ? 6 : 9;
    }
}

int lineWidth(std::string const& s) {
    int w = 0;
    for (std::size_t i = 0; i < s.size();) {
        unsigned char c   = static_cast<unsigned char>(s[i]);
        std::size_t   len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        w += charWidth(c < 0x80 ? c : 0x100);
        i += len;
    }
    return w;
}

class SignEditScreen final : public screen::Screen {
public:
    SignEditScreen(Json const& data, screen::SendFn send)
    : Screen(data.value("title", std::string{"Edit Sign Message"}), 200, 130),
      mSend(std::move(send)),
      mData(data),
      mMaxWidth(data.value("max_width", 90)) {}

    void init() override {
        add<screen::Label>(Rect{0, 6, width, 10}, title, color::kTextDark, screen::Align::Centre);
        auto lines = mData.value("lines", Json::array());
        for (int i = 0; i < 4; ++i) {
            std::string current = i < static_cast<int>(lines.size()) && lines[i].is_string() ? lines[i].get<std::string>() : "";
            auto&       f       = add<screen::TextField>(Rect{30, 22 + 18.f * i, 140, 16}, current);
            f.maxLength         = 384;
            f.onChange          = [this](std::string const&) { sendLines("lines"); };
            f.onSubmit          = [this](std::string const&) { sendLines("lines"); };
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
        add<TextButton>(Rect{50, 100, 100, 20}, "Done", [this] { sendLines("done"); });
        if (!mFields.empty()) mFields[0]->focus();
        log().info("client screen: families.sign");
    }

    void tick() override {
        for (std::size_t i = 0; i < mFields.size() && i < mShadow.size(); ++i) mShadow[i] = mFields[i]->text;
    }

private:
    void sendLines(char const* op) {
        Json lines = Json::array();
        for (auto* f : mFields) lines.push_back(f->text);
        mSend({{"op", op}, {"lines", lines}});
    }

    screen::SendFn                  mSend;
    Json                            mData;
    int                             mMaxWidth;
    std::vector<screen::TextField*> mFields;
    std::vector<std::string>        mShadow; ///< the lines as of the last frame (Esc keeps them)
};

constexpr float kSlot = 18;

class ChestScreen final : public screen::Screen {
public:
    ChestScreen(Json const& data, screen::SendFn send)
    : Screen(data.value("title", std::string{"Chest"}), 176, 17 + kSlot * static_cast<float>(rowsOf(data)) + 5),
      mRows(rowsOf(data)),
      mSend(std::move(send)),
      mData(data) {
        showInventory = true;
    }

    void init() override {
        add<screen::Label>(Rect{8, 6, 160, 10}, title, color::kTextDark);
        for (int i = 0; i < mRows * 9; ++i) {
            float x = 8 + static_cast<float>(i % 9) * kSlot, y = 18 + static_cast<float>(i / 9) * kSlot;
            auto& s = add<screen::Slot>(Rect{x, y, kSlot, kSlot}, [this, i](Button b, bool shift) {
                mSend({{"op", "click"}, {"slot", i}, {"button", b == Button::Right ? "right" : "left"}, {"shift", shift}});
            });
            mSlots.push_back(&s);
        }
        onData(mData);
        log().info("client screen: families.chest {}", title);
    }

    void onData(Json const& data) override {
        mData      = data;
        auto items = data.value("items", Json::array());
        for (std::size_t i = 0; i < mSlots.size(); ++i) mSlots[i]->stack = i < items.size() ? screen::iconOf(items[i]) : nullptr;
        setCursorItem(data.value("carried", Json{}));
    }

    void clickInventory(int slot, ItemStack const&, Button b, bool shift) override {
        mSend({{"op", "inv_click"}, {"slot", slot}, {"button", b == Button::Right ? "right" : "left"}, {"shift", shift}});
    }

private:
    static int rowsOf(Json const& data) { return std::clamp(data.value("rows", 3), 1, 6); }

    int                        mRows;
    screen::SendFn             mSend;
    Json                       mData;
    std::vector<screen::Slot*> mSlots;
};

class FamilyScreensModule {
public:
    void install() {
        screen::registerServerScreen("families.sign", [](Json const& data, screen::SendFn send) { return std::make_unique<SignEditScreen>(data, std::move(send)); });
        screen::registerServerScreen("families.chest", [](Json const& data, screen::SendFn send) { return std::make_unique<ChestScreen>(data, std::move(send)); });
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::client::families

GF_REGISTER_MODULE(gf::client::families::FamilyScreensModule);

#endif // GF_CLIENT
