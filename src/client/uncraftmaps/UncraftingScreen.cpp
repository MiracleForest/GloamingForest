// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/client/UncraftingScreen.java
//   src/main/java/gloamingforest/network/SyncUncraftingTableConfigPacket.java (client handler)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The uncrafting table's screen with the client mod ("uncraftmaps.uncrafting"): upstream's
// guigoblintinkering.png frame (176 x 166) drawn 1:1, the slots at Java's positions, the ghost
// ingredients of the uncrafting matrix in the assembly slots under their grey (or red: broken)
// overlay, the two costs, the six cycle buttons from the texture, the mouse wheel regions, the
// labels, and the tooltips (the ghost item's, the cycle buttons', the red "disabled" line of a banned
// input). The server owns the items: a click sends {op: click, slot, button, type} ("screen.action"),
// a cycle button sends UncraftingGuiPacket's type on "uncraftmaps.gui"; the server answers with fresh
// data. Not drawn: the recipe book button and component (Java's recipe book has no Bedrock
// counterpart).
#ifdef GF_CLIENT

#include <array>
#include <functional>
#include <memory>
#include <mutex>

#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/screen/base/Screen.h"
#include "client/ui/Ui.h"
#include "feature/ext/ui/Client.h"
#include "feature/net/Net.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::uncraftmaps {

namespace {

using screen::Align;
using screen::Button;
using screen::Json;
using screen::Rect;

constexpr char  kTexture[]  = "textures/gloamingforest/gui/guigoblintinkering";
constexpr char  kScreen[]   = "uncraftmaps.uncrafting";
constexpr char  kGui[]      = "uncraftmaps.gui";
constexpr char  kSync[]     = "uncraftmaps.sync_config";
constexpr float kWidth      = 176;
constexpr float kHeight     = 166;
constexpr unsigned kLabel   = 0xFF404040;  // 4210752
constexpr unsigned kHover   = 0x80FFFFFF;  // renderSlotHighlight
constexpr unsigned kGhost   = 0x9F8B8B8B;
constexpr unsigned kBroken  = 0x80FF8B8B;
constexpr unsigned kAfford  = 0xFF80FF20;
constexpr unsigned kTooDear = 0xFFA00000;
constexpr unsigned kDarkRed = 0xFFAA0000;  // ChatFormatting.DARK_RED

/// SyncUncraftingTableConfigPacket's values on this client.
Json& syncedConfig() {
    static Json j = Json::object();
    return j;
}

/// The menu slot at panel pixels (Java's positions; 16 x 16 boxes), -1 for none.
int slotAt(float mx, float my) {
    auto in = [&](float x, float y) { return mx >= x && mx < x + 16 && my >= y && my < y + 16; };
    if (in(13, 35)) return 0;
    if (in(147, 35)) return 1;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            if (in(62 + 18.f * c, 17 + 18.f * r)) return 11 + r * 3 + c;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 9; ++c)
            if (in(8 + 18.f * c, 84 + 18.f * r)) return 20 + r * 9 + c;
    for (int c = 0; c < 9; ++c)
        if (in(8 + 18.f * c, 142)) return 47 + c;
    return -1;
}

std::pair<float, float> slotPos(int slot) {
    if (slot == 0) return {13, 35};
    if (slot == 1) return {147, 35};
    if (slot >= 11 && slot < 20) return {62 + 18.f * ((slot - 11) % 3), 17 + 18.f * ((slot - 11) / 3)};
    if (slot >= 20 && slot < 47) return {8 + 18.f * ((slot - 20) % 9), 84 + 18.f * ((slot - 20) / 9)};
    return {8 + 18.f * (slot - 47), 142};
}

/// CycleButton / CycleButtonMini: a texture button sending UncraftingGuiPacket.
class CycleButton final : public screen::Widget {
public:
    CycleButton(Rect r, float u, float v, bool up, int type, std::string tip) : Widget(r), u(u), v(v), up(up), type(type) { tooltip = {std::move(tip)}; }
    float u, v;
    bool  up;
    int   type;
    void  render(screen::Canvas&, float, float) override {} // drawn by the screen over the frame
    void  draw(screen::Canvas& c, float mx, float my) const {
        if (!visible) return;
        float tu = u + (hovered(mx, my) ? rect.w : 0);
        float tv = v + (up ? 0 : rect.h);
        ui::blit(c, kTexture, rect, tu, tv, rect.w, rect.h);
    }
    bool click(float mx, float my, Button b, bool) override {
        if (!hovered(mx, my) || b != Button::Left) return false;
        net::client::send(kGui, {{"type", type}});
        return true;
    }
};

/// The mouse wheel regions of UncraftingScreen#mouseScrolled (a widget over the whole frame that
/// takes no clicks).
class WheelArea final : public screen::Widget {
public:
    explicit WheelArea(std::function<bool()> ingredients) : Widget(Rect{0, 0, kWidth, kHeight}), ingredientsOn(std::move(ingredients)) {}
    std::function<bool()> ingredientsOn;
    void render(screen::Canvas&, float, float) override {}
    bool scroll(float x, float y, int delta, bool) override {
        bool used = false;
        auto send = [&](int type) {
            net::client::send(kGui, {{"type", type}});
            used = true;
        };
        if (ingredientsOn() && x > 27 && x < 33 && y > 56 && y < 69) send(delta > 0 ? 2 : 3);
        if (x > 40 && x < 54 && y > 22 && y < 64) send(delta > 0 ? 0 : 1);
        if (x > 121 && x < 135 && y > 22 && y < 64) send(delta > 0 ? 4 : 5);
        return used;
    }
};

class UncraftingScreen final : public screen::Screen {
public:
    UncraftingScreen(Json const& data, screen::SendFn send) : Screen(std::string{}, kWidth, kHeight), mSend(std::move(send)), mData(data) {}

    void init() override {
        add<WheelArea>([this] { return !mData.value("disableIngredientSwitching", false); });
        auto tip = [](char const* key, char const* fallback) { return ctr(key, fallback); };
        mButtons.push_back(&add<CycleButton>(Rect{40, 22, 14, 9}, 176.f, 0.f, true, 0, tip("container.gloamingforest.uncrafting_table.cycle_next_uncraft", "Next Uncrafting Recipe")));
        mButtons.push_back(&add<CycleButton>(Rect{40, 55, 14, 9}, 176.f, 0.f, false, 1, tip("container.gloamingforest.uncrafting_table.cycle_back_uncraft", "Previous Uncrafting Recipe")));
        mButtons.push_back(&add<CycleButton>(Rect{27, 56, 8, 6}, 176.f, 41.f, true, 2, tip("container.gloamingforest.uncrafting_table.cycle_next_ingredient", "Next Uncrafting Ingredients")));
        mButtons.push_back(&add<CycleButton>(Rect{27, 63, 8, 6}, 176.f, 41.f, false, 3, tip("container.gloamingforest.uncrafting_table.cycle_back_ingredient", "Previous Uncrafting Ingredients")));
        mButtons.push_back(&add<CycleButton>(Rect{121, 22, 14, 9}, 176.f, 0.f, true, 4, tip("container.gloamingforest.uncrafting_table.cycle_next_recipe", "Next Crafting Recipe")));
        mButtons.push_back(&add<CycleButton>(Rect{121, 55, 14, 9}, 176.f, 0.f, false, 5, tip("container.gloamingforest.uncrafting_table.cycle_back_recipe", "Previous Crafting Recipe")));
        onData(mData);
        log().info("client screen: uncraftmaps.uncrafting opened");
    }

    void onData(Json const& data) override {
        mData = data;
        auto icon = [](Json const& j) { return j.is_object() ? screen::iconOf(j) : nullptr; };
        mInput  = icon(data.value("input", Json()));
        mResult = icon(data.value("result", Json()));
        auto unc = data.value("uncrafting", Json::array());
        auto asm_ = data.value("assembly", Json::array());
        auto mk  = data.value("marked", Json::array());
        for (std::size_t i = 0; i < 9; ++i) {
            mGhost[i]    = i < unc.size() ? icon(unc[i]) : nullptr;
            mAssembly[i] = i < asm_.size() ? icon(asm_[i]) : nullptr;
            mMarked[i]   = i < mk.size() && mk[i].is_boolean() && mk[i].get<bool>();
        }
        setCursorItem(data.value("carried", Json()));
        bool ingredients = !data.value("disableIngredientSwitching", false);
        if (mButtons.size() == 6) mButtons[2]->visible = mButtons[3]->visible = ingredients;
    }

    void renderExtra(screen::Canvas& c, float mx, float my) override {
        // renderBg: the frame, the ghost ingredients, the costs
        ui::blit(c, kTexture, {0, 0, kWidth, kHeight}, 0, 0, kWidth, kHeight);
        for (std::size_t i = 0; i < 9; ++i) {
            if (!mGhost[i]) continue;
            auto [x, y] = slotPos(11 + static_cast<int>(i));
            c.item(*mGhost[i], x, y, false);
            c.fill({x, y, 16, 16}, mMarked[i] ? kBroken : kGhost);
        }
        int  levels   = mData.value("levels", 0);
        bool creative = mData.value("creative", false);
        auto cost = [&](int value, float right) {
            if (value <= 0) return;
            auto s = std::to_string(value);
            c.text(right - c.textWidth(s), 38, s, levels < value && !creative ? kTooDear : kAfford, Align::Left, true);
        };
        cost(mData.value("uncraftingCost", 0), 48);
        cost(mData.value("recraftingCost", 0), 130);
        // the buttons
        for (auto* b : mButtons) b->draw(c, mx, my);
        // the slots' items and the hover highlight
        auto drawSlot = [&](int slot, std::shared_ptr<ItemStack> const& s) {
            auto [x, y] = slotPos(slot);
            if (s && !s->isNull()) c.item(*s, x, y, true);
        };
        drawSlot(0, mInput);
        drawSlot(1, mResult);
        for (int i = 0; i < 9; ++i) drawSlot(11 + i, mAssembly[static_cast<std::size_t>(i)]);
        if (auto* player = currentPlayer()) {
            auto& inv = player->getInventory();
            for (int k = 0; k < 36; ++k) {
                auto const& s    = inv.getItem(k);
                int         slot = k < 9 ? 47 + k : 20 + (k - 9);
                auto [x, y]      = slotPos(slot);
                if (!s.isNull() && s.mCount > 0) c.item(s, x, y, true);
            }
        }
        int hovered = slotAt(mx, my);
        if (hovered >= 0) {
            auto [x, y] = slotPos(hovered);
            c.fill({x, y, 16, 16}, kHover);
        }
        // renderLabels
        c.text(6, 6, mData.value("title", ctr("container.gloamingforest.uncrafting_table", "Uncrafting Table")), kLabel, Align::Left, false);
        if (mData.value("disableUncrafting", false))
            c.text(6, 72, ctr("container.gloamingforest.uncrafting_table.uncrafting_disabled", "Uncrafting is disabled via config"), kDarkRed, Align::Left, false);
        else c.text(7, 72, ctr("container.inventory", "Inventory"), kLabel, Align::Left, false);
        // renderTooltip (the cursor stack hides them)
        if (cursor && !cursor->isNull()) return;
        std::vector<std::string> tip;
        if (hovered == 0 && mInput && mData.value("inputBanned", false)) {
            tip = {"§c" + ctr("container.gloamingforest.uncrafting_table.disabled_item", "Uncrafting this item is disabled.")};
        } else if (hovered >= 11 && hovered < 20 && !mAssembly[static_cast<std::size_t>(hovered - 11)] && mGhost[static_cast<std::size_t>(hovered - 11)]) {
            tip = ui::hoverLines(*mGhost[static_cast<std::size_t>(hovered - 11)]);
        } else if (hovered >= 0) {
            ItemStack const* s = nullptr;
            if (hovered == 0 && mInput) s = mInput.get();
            else if (hovered == 1 && mResult) s = mResult.get();
            else if (hovered >= 11 && hovered < 20 && mAssembly[static_cast<std::size_t>(hovered - 11)]) s = mAssembly[static_cast<std::size_t>(hovered - 11)].get();
            else if (hovered >= 20) {
                if (auto* player = currentPlayer()) {
                    int k = hovered >= 47 ? hovered - 47 : hovered - 20 + 9;
                    s     = &player->getInventory().getItem(k);
                }
            }
            if (s && !s->isNull() && s->mCount > 0) tip = ui::hoverLines(*s);
        }
        if (!tip.empty()) c.tooltip(mx, my, tip);
    }

    bool clickBackground(float mx, float my, Button b, bool shift) override {
        int  slot    = slotAt(mx, my);
        bool outside = mx < 0 || my < 0 || mx >= width || my >= height;
        if (slot < 0 && !outside) return true; // the frame: nothing
        int         button = b == Button::Right ? 1 : 0;
        char const* type   = b == Button::Middle ? "clone" : (shift && slot >= 0 ? "quick_move" : "pickup");
        mSend({{"op", "click"}, {"slot", slot < 0 ? -999 : slot}, {"button", b == Button::Middle ? 2 : button}, {"type", type}});
        return true; // the cursor stack is the server's
    }

private:
    screen::SendFn             mSend;
    Json                       mData;
    std::shared_ptr<ItemStack> mInput, mResult;
    std::array<std::shared_ptr<ItemStack>, 9> mGhost{}, mAssembly{};
    std::array<bool, 9>        mMarked{};
    std::vector<CycleButton*>  mButtons;
};

class UncraftingClientModule {
public:
    void install() {
        screen::registerScreen(kScreen, [](Json const& data, screen::SendFn send) { return std::make_unique<UncraftingScreen>(data, std::move(send)); });
        net::client::on(kSync, [](Json const& msg) {
            syncedConfig() = msg;
            log().info("uncraftmaps: uncrafting config synced (uncrafting disabled: {}, table disabled: {})", msg.value("disabledUncrafting", false), msg.value("disabledTable", false));
        });
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::client::uncraftmaps

GF_REGISTER_MODULE(gf::client::uncraftmaps::UncraftingClientModule);

#endif // GF_CLIENT
