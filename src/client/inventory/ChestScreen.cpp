// The client mod's chest-like container screen ("inventory.chest", gf::inventory's screen for
// containers without a native game container): Java's ChestMenu layout, `rows` rows of 9 slots
// above the player's inventory, the title in the top-left corner. Clicks go to the server
// ("screen.action" {op: click | inv_click, slot, button, shift}), which owns the items and the
// stack on the cursor and answers with fresh data.

#ifdef GF_CLIENT

#include <algorithm>
#include <memory>

#include "client/screen/base/Screen.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::inventory {

namespace {

using screen::Button;
using screen::Json;
using screen::Rect;

constexpr float kSlot = 18;

class ChestScreen final : public screen::Screen {
public:
    ChestScreen(Json const& data, screen::SendFn send)
    : Screen(data.value("title", std::string{"Chest"}), 176, 17 + 18 * static_cast<float>(rowsOf(data)) + 5),
      mRows(rowsOf(data)),
      mSend(std::move(send)),
      mData(data) {
        showInventory = true;
    }

    void init() override {
        add<screen::Label>(Rect{8, 6, 160, 10}, title, screen::color::kTextDark);
        for (int i = 0; i < mRows * 9; ++i) {
            float x = 8 + static_cast<float>(i % 9) * kSlot, y = 18 + static_cast<float>(i / 9) * kSlot;
            auto& s = add<screen::Slot>(Rect{x, y, kSlot, kSlot}, [this, i](Button b, bool shift) {
                mSend({{"op", "click"}, {"slot", i}, {"button", b == Button::Right ? "right" : "left"}, {"shift", shift}});
            });
            mSlots.push_back(&s);
        }
        onData(mData);
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

    int                         mRows;
    screen::SendFn              mSend;
    Json                        mData;
    std::vector<screen::Slot*>  mSlots;
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
