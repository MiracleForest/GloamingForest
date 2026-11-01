// Adapted from LiteLDev/Levitate v0.1.0 (src/client/assembly/TransitionalBar.cpp), LGPL-3.0-or-later.
//
// The client mod's part of F9 (client build only):
// - the synced player data (traits.sync, effects.sync, stats.sync) behind ext/item/Client.h;
// - item bars (item::setBar): drawn over the inventory slot after the item batch, as Java's
//   isBarVisible / getBarWidth (round(13 * fraction)) / getBarColor, black underlay;
// - name colours of TF's rarities (ext/item/Item.h, TFRarityEnumExtension's TWILIGHT dark green):
//   the hover text's formatting prefix of the item's name;
// - the HUD icon row of the local player's custom effects (client.ui hud layer), an effect's
//   colour square and level, top right, as Java's Gui#renderEffects places them.
#ifdef GF_CLIENT

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/renderers/InventoryItemRenderer.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/input/RectangleArea.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "feature/effects/EffectsInternal.h"
#include "feature/ext/item/Client.h"
#include "feature/ext/item/Item.h"
#include "feature/item/ItemInternal.h"
#include "feature/net/Net.h"
#include "mod/Module.h"

namespace gf::client::item {

namespace {

struct Synced {
    std::mutex                               mutex;
    std::map<std::string, bool>              traits;
    std::vector<effects::ext::client::Active> effects;
    std::map<std::string, long long>         stats;
};

Synced& synced() {
    static Synced s;
    return s;
}

// ---- bars ----------------------------------------------------------------------------------------------

struct PendingBar {
    float    x, y, size, fraction;
    unsigned rgb;
};
std::vector<PendingBar>& pendingBars() {
    static std::vector<PendingBar> v;
    return v;
}

void drawBar(MinecraftUIRenderContext& ctx, PendingBar const& b) {
    // Java's GuiGraphics#renderItemDecorations: a 13 x 2 black box at (2, 13), the bar on its top row
    int   width = static_cast<int>(std::lround(13.f * b.fraction));
    float px    = b.size / 16.f;
    float r = static_cast<float>((b.rgb >> 16) & 0xFF) / 255.f, g = static_cast<float>((b.rgb >> 8) & 0xFF) / 255.f,
          bl = static_cast<float>(b.rgb & 0xFF) / 255.f;
    RectangleArea back{b.x + 2 * px, b.x + 15 * px, b.y + 13 * px, b.y + 15 * px};
    ctx.fillRectangle(back, mce::Color{0.f, 0.f, 0.f, 1.f}, 1.f);
    if (width > 0) {
        RectangleArea bar{b.x + 2 * px, b.x + static_cast<float>(2 + width) * px, b.y + 13 * px, b.y + 14 * px};
        ctx.fillRectangle(bar, mce::Color{r, g, bl, 1.f}, 1.f);
    }
}

} // namespace

} // namespace gf::client::item

// ---- the synced data (ext/item/Client.h) -------------------------------------------------------------

namespace gf::traits::ext::client {
bool get(std::string const& traitId) {
    auto&           s = gf::client::item::synced();
    std::lock_guard lock(s.mutex);
    auto            it = s.traits.find(traitId);
    return it != s.traits.end() && it->second;
}
} // namespace gf::traits::ext::client

namespace gf::effects::ext::client {
std::vector<Active> active() {
    auto&           s = gf::client::item::synced();
    std::lock_guard lock(s.mutex);
    return s.effects;
}
int amplifier(std::string const& effectId) {
    for (auto const& a : active())
        if (a.id == effectId) return a.amplifier;
    return -1;
}
} // namespace gf::effects::ext::client

namespace gf::stats::ext::client {
std::map<std::string, long long> values() {
    auto&           s = gf::client::item::synced();
    std::lock_guard lock(s.mutex);
    return s.stats;
}
} // namespace gf::stats::ext::client

// ---- hooks -----------------------------------------------------------------------------------------------

namespace ci = gf::client::item;

LL_TYPE_INSTANCE_HOOK(
    ItemBarRenderHook,
    ll::memory::HookPriority::Normal,
    InventoryItemRenderer,
    &InventoryItemRenderer::$render,
    void,
    MinecraftUIRenderContext& ctx,
    IClientInstance&          client,
    UIControl&                owner,
    int                       pass
) {
    origin(ctx, client, owner, pass);
    try {
        ItemStack const& stack = *mItemInstance;
        if (stack.isNull() || !stack.mItem) return;
        if (!gf::item::internal::hasBar(stack.mItem.get())) return;
        auto bar = gf::item::internal::bar(stack);
        if (!bar) return;
        glm::vec2 pos  = *owner.mCachedPosition;
        glm::vec2 size = *owner.mSize;
        // items are drawn in a shared mesh batch: the bar goes on top once the batch is done
        ci::pendingBars().push_back({pos.x, pos.y, std::min(size.x, size.y), bar->fraction, bar->rgb});
    } catch (...) {
    }
}

LL_TYPE_INSTANCE_HOOK(
    ItemBarBatchHook,
    ll::memory::HookPriority::Normal,
    MinecraftUIRenderContext,
    &MinecraftUIRenderContext::$endSharedMeshBatch,
    void,
    ::ComponentRenderBatch& renderBatch
) {
    origin(renderBatch);
    if (ci::pendingBars().empty()) return;
    auto bars = std::move(ci::pendingBars());
    ci::pendingBars().clear();
    try {
        for (auto const& b : bars) ci::drawBar(*this, b);
    } catch (...) {
    }
}

// The name colour of an item with a rarity (TF's TWILIGHT: dark green); custom names keep theirs.
LL_TYPE_INSTANCE_HOOK(
    ItemRarityPrefixHook,
    ll::memory::HookPriority::Normal,
    ItemStackBase,
    &ItemStackBase::_getHoverFormattingPrefix,
    std::string
) {
    auto prefix = origin();
    try {
        if (isNull() || !mItem) return prefix;
        auto rarity = gf::item::ext::rarityOf(mItem->mFullName->getString());
        if (rarity == gf::item::ext::Rarity::Common || !getCustomName().empty()) return prefix;
        return gf::item::ext::rarityColor(rarity);
    } catch (...) {
        return prefix;
    }
}

namespace gf::client::item {
namespace {

void onSync(std::string const& channel, nlohmann::json const& msg) {
    auto&           s = synced();
    std::lock_guard lock(s.mutex);
    if (channel == "traits.sync") {
        s.traits.clear();
        if (auto it = msg.find("traits"); it != msg.end() && it->is_object())
            for (auto& [k, v] : it->items())
                if (v.is_boolean()) s.traits[k] = v.get<bool>();
    } else if (channel == "effects.sync") {
        s.effects.clear();
        if (auto it = msg.find("effects"); it != msg.end() && it->is_array())
            for (auto const& e : *it)
                if (e.is_object()) s.effects.push_back({e.value("id", std::string()), e.value("amplifier", 0), e.value("ticks", 0)});
    } else if (channel == "stats.sync") {
        s.stats.clear();
        if (auto it = msg.find("stats"); it != msg.end() && it->is_object())
            for (auto& [k, v] : it->items())
                if (v.is_number_integer()) s.stats[k] = v.get<long long>();
    }
}

class ItemClientModule {
public:
    void load() {
        for (auto const* ch : {"traits.sync", "effects.sync", "stats.sync"}) {
            std::string channel = ch;
            net::client::on(channel, [channel](nlohmann::json const& msg) { onSync(channel, msg); });
        }
        net::client::onConnected([] {
            auto&           s = synced();
            std::lock_guard lock(s.mutex);
            s.traits.clear();
            s.effects.clear();
            s.stats.clear();
        });
        // the custom effects' HUD row (Java's Gui#renderEffects: 24 px boxes, 25 px apart; the layer's
        // canvas starts at the HUD's top left, so the row starts there)
        hud::addLayer("item.effects", 100, [](screen::Canvas& canvas, float) {
            auto  list = effects::ext::client::active();
            float x    = -24;
            for (auto const& a : list) {
                auto spec = effects::internal::specOf(a.id);
                if (!spec) continue;
                unsigned rgb = spec->color & 0xFFFFFF;
                x += 25;
                screen::Rect box{x, 1, 24, 24};
                canvas.bevel(box, 0xFF000000u | 0x3A3A3Au, 0xFF8B8B8Bu, 0xFF373737u, false);
                canvas.fill(screen::Rect{x + 4, 5, 16, 16}, 0xFF000000u | rgb);
                if (a.amplifier > 0) canvas.text(x + 12, 14, std::to_string(a.amplifier + 1), 0xFFFFFFFFu, screen::Align::Centre, true);
            }
        });
    }
    void install() {
        ItemBarRenderHook::hook();
        ItemBarBatchHook::hook();
        ItemRarityPrefixHook::hook();
    }
    void uninstall() {
        ItemRarityPrefixHook::unhook();
        ItemBarBatchHook::unhook();
        ItemBarRenderHook::unhook();
    }
};

} // namespace
} // namespace gf::client::item

GF_REGISTER_MODULE(gf::client::item::ItemClientModule);

#endif // GF_CLIENT
