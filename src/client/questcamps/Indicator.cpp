// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/overlay/QuestingRamIndicatorOverlay.java
//   src/main/java/twilightforest/network/SyncQuestsPacket.java (the client half)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The Questing Ram's crosshair indicator (client mod, config client.questRamWoolIndicator): looking
// at a quest ram while holding a quest item draws questing_ram_x (that colour is still missing) or
// questing_ram_check, 7 x 7 at ((guiHeight - 1) / 2 - 3, (guiHeight - 1) / 2 - 11) as TF does (it
// uses the GUI height for both coordinates). The quest comes over questcamps.sync_quests; the
// ram's colours over F8's entity data sync. Bedrock note: the sprite is drawn with normal alpha
// blending (F12's sprite blit has no ONE_MINUS_DST_COLOR mode).

#ifdef GF_CLIENT

#include <mutex>
#include <optional>

#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"
#include "mc/world/phys/HitResult.h"
#include "mc/world/phys/HitResultType.h"

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "core/questcamps/Questcamps.h"
#include "feature/ext/dimension/HudSprite.h"
#include "feature/ext/entity/EntityClientExt.h"
#include "feature/net/Net.h"
#include "feature/questcamps/Questcamps.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::questcamps {

namespace fq = gf::feature::questcamps;
namespace cq = gf::core::questcamps;

namespace {

constexpr char kCheck[] = "textures/twilightforest/gui/sprites/questing_ram_check";
constexpr char kX[]     = "textures/twilightforest/gui/sprites/questing_ram_x";

std::mutex& questMutex() {
    static std::mutex m;
    return m;
}
/// QuestingRamCurrentContext on the client (null until the server synced it).
std::optional<nlohmann::json>& syncedQuest() {
    static std::optional<nlohmann::json> q;
    return q;
}

/// The first quest colour `itemId` matches (-1: not a quest item).
int questColour(std::string const& itemId) {
    std::optional<nlohmann::json> synced;
    {
        std::lock_guard lock(questMutex());
        synced = syncedQuest();
    }
    if (synced && (*synced).contains("items")) {
        for (int i = 0; i < 16; ++i) {
            auto name = std::string(cq::kDyeNames[static_cast<std::size_t>(i)]);
            if ((*synced)["items"].contains(name) && fq::ingredientMatches((*synced)["items"][name], itemId)) return i;
        }
        return -1;
    }
    auto colours = fq::questColorsOf(itemId);
    return colours.empty() ? -1 : colours.front();
}

void draw(screen::Canvas& canvas, float) {
    if (!gf::config::get<fq::QuestcampsConfig>().questRamWoolIndicator) return;
    auto* player = currentPlayer();
    if (!player) return;
    auto const& stack = player->getSelectedItem();
    if (game::items::isEmpty(stack)) return;
    auto&  hit = player->getLevel().getHitResult();
    Actor* ram = hit.mType == HitResultType::Entity ? hit.getEntity() : nullptr;
    if (!ram || ram->getTypeName() != fq::kRam) return;
    int colour = questColour(game::items::idOf(stack));
    if (colour < 0) return;
    auto data  = gf::client::entity::data(ram->getOrCreateUniqueID().rawID, fq::kRamData);
    int  flags = data.is_object() ? data.value("ColorFlags", 0) : 0;
    auto [w, h] = screen::guiSize();
    (void)w;
    int   gh = static_cast<int>(h);
    float x  = static_cast<float>((gh - 1) / 2 - 3) - canvas.originX();
    float y  = static_cast<float>((gh - 1) / 2 - 11) - canvas.originY();
    hud::ext::blitSprite(canvas, cq::colorPresent(flags, colour) ? kCheck : kX, screen::Rect{x, y, 7, 7}, 1.0f, 0);
}

} // namespace

class IndicatorModule {
public:
    void install() {
        static bool const once = [] {
            hud::addLayer("questcamps.ram_indicator", 50, draw);
            feature::net::client::on(fq::kSyncChannel, [](nlohmann::json const& payload) {
                std::lock_guard lock(questMutex());
                syncedQuest() = payload;
            });
            return true;
        }();
        (void)once;
    }
    void uninstall() {}
};

} // namespace gf::client::questcamps

GF_REGISTER_MODULE(gf::client::questcamps::IndicatorModule);

#endif // GF_CLIENT
