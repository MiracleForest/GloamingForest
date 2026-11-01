// The client mod's chest-like container screen ("inventory.chest", gf::inventory's screen for
// custom containers): Java's ChestScreen on the generic_54 layout, one 176-wide panel of
// 114 + 18 * rows pixels with the title at (8, 6), `rows` rows of 9 container slots from y 18, the
// player inventory title at (8, height - 94) and the player's inventory below (ChestMenu: the
// main rows at y 103 + 18 * (rows - 4), the hotbar 58 pixels lower). Stack counts are drawn on the
// items. Clicks go to the server ("screen.action" {op: click | inv_click, slot, button, shift}),
// which owns the items and the stack on the cursor and answers with fresh data.

#ifdef GF_CLIENT

#include <algorithm>
#include <memory>

#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/screen/base/Screen.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::inventory {

namespace {

using screen::Button;
using screen::Json;
using screen::Rect;

constexpr float kSlot  = 18;
constexpr float kWidth = 176;

/// One slot of the chest screen: a container slot (the server's stack, `icon`) or a slot of the
/// local player's inventory (`inventorySlot`, read from the game every frame), with its count and
/// the hover highlight drawn over the item as Java's AbstractContainerScreen does.
class ChestSlot final : public screen::Widget {
public:
    ChestSlot(Rect r, int inventorySlot, std::function<void(Button, bool)> onClick)
    : Widget(r),
      inventorySlot(inventorySlot),
      onClick(std::move(onClick)) {}

    std::shared_ptr<ItemStack>        icon;              ///< container slots
    int                               inventorySlot = -1; ///< player inventory slots
    std::function<void(Button, bool)> onClick;

    ItemStack const* stack() const {
        if (inventorySlot < 0) return icon.get();
        auto* player = currentPlayer();
        return player ? &player->getInventory().getItem(inventorySlot) : nullptr;
    }

    void render(screen::Canvas& c, float mx, float my) override {
        c.bevel(rect, screen::color::kSlot, screen::color::kSlotDark, screen::color::kSlotLight, true);
        if (auto const* s = stack(); s && !s->isNull() && s->mCount > 0) c.item(*s, rect.x + 1, rect.y + 1, true);
        if (hovered(mx, my)) c.fill(rect.inset(1), screen::color::kHover);
    }
    bool click(float mx, float my, Button b, bool shift) override {
        if (!hovered(mx, my)) return false;
        if (onClick) onClick(b, shift);
        return true;
    }
};

class ChestScreen final : public screen::Screen {
public:
    ChestScreen(Json const& data, screen::SendFn send)
    : Screen(std::string{}, kWidth, 114 + kSlot * static_cast<float>(rowsOf(data))),
      mRows(rowsOf(data)),
      mSend(std::move(send)),
      mData(data) {}

    void init() override {
        // the title is drawn by the screen, not the framework's centred title (Java: titleLabelX 8)
        add<screen::Label>(Rect{8, 6, 160, 10}, mData.value("title", std::string{"Chest"}), screen::color::kTextDark);
        for (int i = 0; i < mRows * 9; ++i) {
            float x = 7 + static_cast<float>(i % 9) * kSlot, y = 17 + static_cast<float>(i / 9) * kSlot;
            auto& s = add<ChestSlot>(Rect{x, y, kSlot, kSlot}, -1, [this, i](Button b, bool shift) {
                mSend({{"op", "click"}, {"slot", i}, {"button", b == Button::Right ? "right" : "left"}, {"shift", shift}});
            });
            mSlots.push_back(&s);
        }
        // ChestMenu's player inventory: (rows - 4) * 18 below the 4-row layout
        float shift = static_cast<float>(mRows - 4) * kSlot;
        add<screen::Label>(Rect{8, height - 94, 160, 10}, ctr("container.inventory", "Inventory"), screen::color::kTextDark);
        for (int r = 0; r < 3; ++r)
            for (int col = 0; col < 9; ++col) addInventorySlot(9 + r * 9 + col, 7 + static_cast<float>(col) * kSlot, 102 + static_cast<float>(r) * kSlot + shift);
        for (int col = 0; col < 9; ++col) addInventorySlot(col, 7 + static_cast<float>(col) * kSlot, 160 + shift);
        onData(mData);
    }

    void tick() override {
        // tooltips: the hovered stack's name
        for (auto* s : mInventory) {
            auto const* st = s->stack();
            s->tooltip     = st && !st->isNull() && st->mCount > 0 ? std::vector<std::string>{st->getName()} : std::vector<std::string>{};
        }
    }

    void onData(Json const& data) override {
        mData      = data;
        auto items = data.value("items", Json::array());
        for (std::size_t i = 0; i < mSlots.size(); ++i) {
            mSlots[i]->icon    = i < items.size() ? screen::iconOf(items[i]) : nullptr;
            mSlots[i]->tooltip = mSlots[i]->icon && !mSlots[i]->icon->isNull() ? std::vector<std::string>{mSlots[i]->icon->getName()} : std::vector<std::string>{};
        }
        setCursorItem(data.value("carried", Json{}));
    }

private:
    static int rowsOf(Json const& data) { return std::clamp(data.value("rows", 3), 1, 6); }

    void addInventorySlot(int slot, float x, float y) {
        auto& s = add<ChestSlot>(Rect{x, y, kSlot, kSlot}, slot, [this, slot](Button b, bool shift) {
            mSend({{"op", "inv_click"}, {"slot", slot}, {"button", b == Button::Right ? "right" : "left"}, {"shift", shift}});
        });
        mInventory.push_back(&s);
    }

    int                     mRows;
    screen::SendFn          mSend;
    Json                    mData;
    std::vector<ChestSlot*> mSlots;
    std::vector<ChestSlot*> mInventory;
};

class ChestScreenClientModule {
public:
    void install() {
        screen::registerServerScreen("inventory.chest", [](Json const& data, screen::SendFn send) {
            log().info("client screen: inventory.chest {}", data.value("title", std::string{}));
            return std::make_unique<ChestScreen>(data, std::move(send));
        });
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::client::inventory

GF_REGISTER_MODULE(gf::client::inventory::ChestScreenClientModule);

#endif // GF_CLIENT
