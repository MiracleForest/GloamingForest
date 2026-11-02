// The client mod's chest-like container screen ("inventory.chest", gf::inventory's screen for
// custom containers): Java's ChestScreen on the generic_54 layout, one 176-wide panel of
// 114 + 18 * rows pixels with the title at (8, 6), `rows` rows of 9 container slots from y 18, the
// player inventory title at (8, height - 94) and the player's inventory below (ChestMenu: the
// main rows at y 103 + 18 * (rows - 4), the hotbar 58 pixels lower). Stack counts are drawn on the
// items.
//
// The mouse and keys work as Java's AbstractContainerScreen: a click picks up / places / swaps
// (right: half / one), Shift+click quick-moves, a double click collects the same items (pickup
// all; Shift: quick-moves every matching stack of that inventory), dragging a stack over slots
// splits it (left: evenly, right: one each, middle in creative: full stacks), a click outside the
// panel drops the stack on the cursor (right: one), the hotbar keys 1-9 and F swap the hovered slot
// with that hotbar slot / the off-hand, Q throws one of the hovered stack (Ctrl+Q all of it) and
// the middle button clones a stack in creative. The server owns the items and the stack on the
// cursor ("screen.action" {op: click, slot (menu index, -999 outside), button, type, slots}) and
// answers with fresh data.

#ifdef GF_CLIENT

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/input/MouseInputEvent.h"

#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/screen/base/Screen.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::inventory {

namespace {

using screen::Button;
using screen::Json;
using screen::Rect;

constexpr float kSlot  = 18;
constexpr float kWidth = 176;
constexpr int   kOutside = -999; ///< Java's slot id of a click outside the panel

// ---- input the framework does not route to screens (input thread -> client thread) ---------------

struct InputEvent {
    enum class Kind { Release, Key } kind = Kind::Release;
    int  button = 0; ///< Release: 0 left, 1 right, 2 middle
    int  key    = 0; ///< Key: Windows virtual key
    bool ctrl   = false;
    bool shift  = false;
};

struct Input {
    std::mutex              mutex;
    std::vector<InputEvent> events;
    std::atomic<bool>       active{false}; ///< a chest screen is open
    std::atomic<bool>       shift{false}, ctrl{false};
};

Input& input() {
    static Input i;
    return i;
}

std::uint64_t millis() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

/// One slot of the chest screen: a container slot (the server's stack, `icon`) or a slot of the
/// local player's inventory (`inventorySlot`, read from the game every frame), with its count and
/// the hover highlight drawn over the item as Java's AbstractContainerScreen does.
class ChestSlot final : public screen::Widget {
public:
    ChestSlot(Rect r, int menuIndex, int inventorySlot, std::function<void(int, Button, bool)> onPress)
    : Widget(r),
      menuIndex(menuIndex),
      inventorySlot(inventorySlot),
      onPress(std::move(onPress)) {}

    int                                    menuIndex     = 0;  ///< Java's slot index in the menu
    std::shared_ptr<ItemStack>             icon;               ///< container slots
    int                                    inventorySlot = -1; ///< player inventory slots
    std::function<void(int, Button, bool)> onPress;
    bool                                   dragged = false; ///< part of the drag being split

    ItemStack const* stack() const {
        if (inventorySlot < 0) return icon.get();
        auto* player = currentPlayer();
        return player ? &player->getInventory().getItem(inventorySlot) : nullptr;
    }
    bool hasItem() const {
        auto const* s = stack();
        return s && !s->isNull() && s->mCount > 0;
    }

    void render(screen::Canvas& c, float mx, float my) override {
        c.bevel(rect, screen::color::kSlot, screen::color::kSlotDark, screen::color::kSlotLight, true);
        if (hasItem()) c.item(*stack(), rect.x + 1, rect.y + 1, true);
        if (dragged) c.fill(rect.inset(1), 0x80FFFFFF);
        else if (hovered(mx, my)) c.fill(rect.inset(1), screen::color::kHover);
    }
    bool click(float mx, float my, Button b, bool shift) override {
        if (!hovered(mx, my)) return false;
        if (onPress) onPress(menuIndex, b, shift);
        return true;
    }
};

class ChestScreen final : public screen::Screen {
public:
    ChestScreen(Json const& data, screen::SendFn send)
    : Screen(std::string{}, kWidth, 114 + kSlot * static_cast<float>(rowsOf(data))),
      mRows(rowsOf(data)),
      mContainerSlots(std::clamp(data.value("slots", mRows * 9), 0, mRows * 9)),
      mSend(std::move(send)),
      mData(data) {}

    ~ChestScreen() override { input().active = false; }

    void init() override {
        {
            std::lock_guard lock(input().mutex);
            input().events.clear();
        }
        input().active = true;
        // the title is drawn by the screen, not the framework's centred title (Java: titleLabelX 8)
        add<screen::Label>(Rect{8, 6, 160, 10}, mData.value("title", std::string{"Chest"}), screen::color::kTextDark);
        auto press = [this](int index, Button b, bool shift) { onPress(index, b, shift); };
        for (int i = 0; i < mContainerSlots; ++i) {
            float x = 7 + static_cast<float>(i % 9) * kSlot, y = 17 + static_cast<float>(i / 9) * kSlot;
            mSlots.push_back(&add<ChestSlot>(Rect{x, y, kSlot, kSlot}, i, -1, press));
        }
        // ChestMenu's player inventory: (rows - 4) * 18 below the 4-row layout; menu indices after
        // the container's: the main rows (inventory 9..35), then the hotbar (0..8)
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
            s->tooltip     = s->hasItem() ? std::vector<std::string>{st->getName()} : std::vector<std::string>{};
        }
    }

    void renderExtra(screen::Canvas&, float mx, float my) override {
        mMouseX = mx;
        mMouseY = my;
        // the releases and keys of this frame (after the framework's clicks of this frame)
        std::vector<InputEvent> events;
        {
            std::lock_guard lock(input().mutex);
            events.swap(input().events);
        }
        for (auto const& e : events) {
            if (e.kind == InputEvent::Kind::Release) onRelease(e.button, e.shift);
            else onKey(e.key, e.ctrl);
        }
        // AbstractContainerScreen#mouseDragged: while splitting, the slots the mouse passes over
        if (mQuickCrafting) {
            if (auto* slot = slotAt(mx, my); slot && canDragTo(*slot)) {
                mQuickCraftSlots.push_back(slot->menuIndex);
                slot->dragged = mQuickCraftSlots.size() > 1;
                if (mQuickCraftSlots.size() == 2)
                    if (auto* first = slotOf(mQuickCraftSlots.front())) first->dragged = true;
            }
        }
    }

    bool clickBackground(float mx, float my, Button b, bool shift) override {
        // hasClickedOutside: outside the panel (Java's slot -999); inside but on no slot: nothing
        bool outside = mx < 0 || my < 0 || mx >= width || my >= height;
        onPress(outside ? kOutside : -1, b, shift);
        return true; // the cursor stack is the server's: never cleared here
    }

    void onData(Json const& data) override {
        mData      = data;
        auto items = data.value("items", Json::array());
        for (std::size_t i = 0; i < mSlots.size(); ++i) {
            mSlots[i]->icon    = i < items.size() ? screen::iconOf(items[i]) : nullptr;
            mSlots[i]->tooltip = mSlots[i]->icon && !mSlots[i]->icon->isNull() ? std::vector<std::string>{mSlots[i]->icon->getName()} : std::vector<std::string>{};
        }
        setCursorItem(data.value("carried", Json{}));
        mPredicted = nullptr; // the server's cursor stack is known again
    }

private:
    static int rowsOf(Json const& data) { return std::clamp(data.value("rows", 3), 1, 6); }

    void addInventorySlot(int slot, float x, float y) {
        int  index = mContainerSlots + (slot >= 9 ? slot - 9 : 27 + slot);
        auto press = [this](int i, Button b, bool shift) { onPress(i, b, shift); };
        mInventory.push_back(&add<ChestSlot>(Rect{x, y, kSlot, kSlot}, index, slot, press));
    }

    ChestSlot* slotOf(int index) {
        for (auto* s : mSlots)
            if (s->menuIndex == index) return s;
        for (auto* s : mInventory)
            if (s->menuIndex == index) return s;
        return nullptr;
    }
    ChestSlot* slotAt(float mx, float my) {
        for (auto* s : mSlots)
            if (s->hovered(mx, my)) return s;
        for (auto* s : mInventory)
            if (s->hovered(mx, my)) return s;
        return nullptr;
    }

    /// The stack on the cursor as the client knows it (the server's, or the one a pickup sent a
    /// moment ago is about to put there).
    Json carried() const { return !mPredicted.is_null() ? mPredicted : cursorItem; }
    bool carriedEmpty() const { return carried().is_null(); }
    int  carriedCount() const {
        if (!mPredicted.is_null()) return mPredicted.value("count", 1);
        return cursor && !cursor->isNull() ? cursor->mCount : 0;
    }

    /// AbstractContainerMenu#canItemQuickReplace(slot, carried, true) as far as the client sees it.
    bool canQuickReplace(ChestSlot const& slot) const {
        if (!slot.hasItem()) return true;
        if (!cursor || cursor->isNull()) return false;
        auto const& s = *slot.stack();
        return game::items::sameItemSameData(s, *cursor) && s.mCount <= cursor->getMaxStackSize();
    }
    bool canDragTo(ChestSlot const& slot) const {
        if (carriedEmpty() || std::find(mQuickCraftSlots.begin(), mQuickCraftSlots.end(), slot.menuIndex) != mQuickCraftSlots.end()) return false;
        return (carriedCount() > static_cast<int>(mQuickCraftSlots.size()) || mQuickCraftType == 2) && canQuickReplace(slot);
    }

    void send(int slot, int button, char const* type) { mSend({{"op", "click"}, {"slot", slot}, {"button", button}, {"type", type}}); }

    /// AbstractContainerScreen#mouseClicked.
    void onPress(int slot, Button b, bool shift) {
        int  button = b == Button::Left ? 0 : b == Button::Right ? 1 : 2;
        bool pick   = b == Button::Middle; // the pick block key
        auto now    = millis();
        // a press during a drag: its release never arrived (released outside the window), the
        // drag is over
        if (mQuickCrafting) endQuickCraft();
        mDoubleClick     = slot >= 0 && mLastClickSlot == slot && now - mLastClickTime < 250 && mLastClickButton == button;
        mSkipNextRelease = false;
        if (slot != -1 && !mQuickCrafting) {
            if (carriedEmpty()) {
                if (pick) {
                    send(slot, button, "clone");
                } else {
                    bool quickMove = slot != kOutside && shift;
                    if (quickMove) mLastQuickMoved = stackJsonAt(slot);
                    send(slot, button, quickMove ? "quick_move" : slot == kOutside ? "throw" : "pickup");
                    // what the pickup will put on the cursor (until the server's answer)
                    if (!quickMove && slot != kOutside)
                        if (auto* s = slotOf(slot); s && s->hasItem()) mPredicted = game::items::toJsonWithCount(*s->stack(), s->stack()->mCount);
                }
                mSkipNextRelease = true;
            } else {
                mQuickCrafting       = true;
                mQuickCraftingButton = button;
                mQuickCraftSlots.clear();
                mQuickCraftType = button == 0 ? 0 : button == 1 ? 1 : pick ? 2 : 0;
            }
        }
        mLastClickSlot   = slot;
        mLastClickTime   = now;
        mLastClickButton = button;
    }

    /// AbstractContainerScreen#mouseReleased.
    void onRelease(int button, bool shift) {
        auto* hovered = slotAt(mMouseX, mMouseY);
        bool  outside = mMouseX < 0 || mMouseY < 0 || mMouseX >= width || mMouseY >= height;
        int   slot    = hovered ? hovered->menuIndex : outside ? kOutside : -1;
        if (mDoubleClick && hovered && button == 0) {
            if (shift) {
                // every matching stack of the same inventory (the container or the player's) quick-moves
                if (!mLastQuickMoved.is_null()) {
                    bool container = hovered->menuIndex < mContainerSlots;
                    auto proto     = screen::iconOf(mLastQuickMoved);
                    for (auto* list : {&mSlots, &mInventory})
                        for (auto* s : *list) {
                            if ((s->menuIndex < mContainerSlots) != container || !s->hasItem() || !proto) continue;
                            if (game::items::sameItemSameData(*s->stack(), *proto)) send(s->menuIndex, button, "quick_move");
                        }
                }
            } else {
                send(slot, 0, "pickup_all");
            }
            mDoubleClick   = false;
            mLastClickTime = 0;
        } else {
            if (mQuickCrafting && mQuickCraftingButton != button) {
                endQuickCraft();
                mSkipNextRelease = true;
                return;
            }
            if (mSkipNextRelease) {
                mSkipNextRelease = false;
                return;
            }
            if (mQuickCrafting && !mQuickCraftSlots.empty()) {
                mSend({{"op", "click"}, {"slot", kOutside}, {"button", mQuickCraftType}, {"type", "quick_craft"}, {"slots", mQuickCraftSlots}});
            } else if (!carriedEmpty() && slot != -1) {
                if (button == 2) {
                    send(slot, button, "clone");
                } else {
                    bool quickMove = slot != kOutside && shift;
                    if (quickMove) mLastQuickMoved = stackJsonAt(slot);
                    send(slot, button, quickMove ? "quick_move" : "pickup");
                }
            }
        }
        if (carriedEmpty()) mLastClickTime = 0;
        endQuickCraft();
    }

    void endQuickCraft() {
        mQuickCrafting = false;
        mQuickCraftSlots.clear();
        for (auto* s : mSlots) s->dragged = false;
        for (auto* s : mInventory) s->dragged = false;
    }

    /// AbstractContainerScreen#keyPressed: the hotbar keys and F swap, Q throws (Ctrl: the stack).
    void onKey(int key, bool ctrl) {
        auto* hovered = slotAt(mMouseX, mMouseY);
        if (!hovered) return;
        if (carriedEmpty()) { // checkHotbarKeyPressed
            if (key == 'F') return send(hovered->menuIndex, 40, "swap");
            if (key >= '1' && key <= '9') return send(hovered->menuIndex, key - '1', "swap");
        }
        if (hovered->hasItem() && key == 'Q') send(hovered->menuIndex, ctrl ? 1 : 0, "throw");
    }

    Json stackJsonAt(int index) {
        auto* s = slotOf(index);
        return s && s->hasItem() ? game::items::toJsonWithCount(*s->stack(), s->stack()->mCount) : Json(nullptr);
    }

    int                     mRows;
    int                     mContainerSlots;
    screen::SendFn          mSend;
    Json                    mData;
    std::vector<ChestSlot*> mSlots;
    std::vector<ChestSlot*> mInventory;
    float                   mMouseX = -1, mMouseY = -1;
    Json                    mPredicted; ///< the cursor stack a pickup is about to bring
    // AbstractContainerScreen's click state
    int              mLastClickSlot   = -1;
    std::uint64_t    mLastClickTime   = 0;
    int              mLastClickButton = -1;
    bool             mDoubleClick     = false;
    bool             mSkipNextRelease = false;
    bool             mQuickCrafting   = false;
    int              mQuickCraftingButton = 0;
    int              mQuickCraftType  = 0;
    std::vector<int> mQuickCraftSlots;
    Json             mLastQuickMoved;
};

class ChestScreenClientModule {
public:
    /// Input listeners at mod load (as the screen framework's): the releases and keys of an open
    /// chest screen, ahead of the framework's listeners (which cancel them for the game).
    void load() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::MouseInputEvent>(
            [](ll::event::MouseInputEvent& ev) {
                if (!input().active) return;
                char id = ev.actionButtonId();
                if ((id == 1 || id == 2 || id == 3) && ev.buttonData() == 0) {
                    InputEvent e;
                    e.kind   = InputEvent::Kind::Release;
                    e.button = id - 1;
                    e.shift  = input().shift;
                    std::lock_guard lock(input().mutex);
                    input().events.push_back(e);
                }
            },
            ll::event::EventPriority::High
        ));
        mListeners.push_back(bus.emplaceListener<ll::event::KeyInputEvent>(
            [](ll::event::KeyInputEvent& ev) {
                int key = ev.keyCode();
                if (key == 16) input().shift = ev.isDown();
                if (key == 17) input().ctrl = ev.isDown();
                if (!input().active || !ev.isDown()) return;
                if (key != 'F' && key != 'Q' && !(key >= '1' && key <= '9')) return;
                InputEvent e;
                e.kind = InputEvent::Kind::Key;
                e.key  = key;
                e.ctrl = input().ctrl;
                std::lock_guard lock(input().mutex);
                input().events.push_back(e);
            },
            ll::event::EventPriority::High
        ));
    }
    void install() {
        screen::registerServerScreen("inventory.chest", [](Json const& data, screen::SendFn send) {
            log().info("client screen: inventory.chest {}", data.value("title", std::string{}));
            return std::make_unique<ChestScreen>(data, std::move(send));
        });
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace

} // namespace gf::client::inventory

GF_REGISTER_MODULE(gf::client::inventory::ChestScreenClientModule);

#endif // GF_CLIENT
