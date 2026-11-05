// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/events/EntityEvents.java (alertPlayerCastleIsWIP)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// What earning one of TF's advancements shows, as Java's PlayerAdvancements#award does for an
// advancement with a display: the rewards (experience), the advancement toast (show_toast: the
// client mod draws Java's toast, "progression.toast"; vanilla clients get the game's toast, text
// only), the chat announcement to every player (announce_to_chat; "<player> has made the
// advancement [<title>]", reached the goal, completed the challenge; the title green, purple for
// challenges), and TF's progression_end message (EntityEvents#alertPlayerCastleIsWIP). Bedrock's
// chat has no click events, so the Discord link's URL is written out after its blue, underlined
// text.
//
// The advancement screen ("progression.advancements"): the client mod's native tree (TF's tab
// with the mazestone background, redrawn frames), opened with the advancements key (L) or
// /twilightforest advancements; vanilla clients get a form list with a detail form per advancement.

#include <functional>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/service/Bedrock.h"

#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/ToastRequestPacket.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include "feature/common/Screens.h"
#include "feature/net/Net.h"
#include "feature/progression/Progression.h"
#include "feature/progression_content/Config.h"
#include "feature/progression_content/Content.h"
#include "feature/ui/Ui.h"
#include "mod/Commands.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::progression_content {

namespace pr = gf::progression;
namespace cp = core::progression;

inline constexpr char kScreen[]      = "progression.advancements";
inline constexpr char kToastChannel[] = "progression.toast";
inline constexpr char kOpenChannel[]  = "progression.open";
inline constexpr char kDiscordUrl[]   = "https://discord.experiment115.com/";

namespace {

/// advancements.toast.<frame> (Java's AdvancementType#getToastName)
std::string toastTitle(Player const& p, Frame f) {
    switch (f) {
    case Frame::Goal: return trFor(p, "advancements.toast.goal", "Goal Reached!");
    case Frame::Challenge: return trFor(p, "advancements.toast.challenge", "Challenge Complete!");
    default: return trFor(p, "advancements.toast.task", "Advancement Made!");
    }
}

/// ChatFormatting of the frame (Java's AdvancementType#getChatColor): GREEN, DARK_PURPLE.
char const* chatColor(Frame f) { return f == Frame::Challenge ? "§5" : "§a"; }

void announce(Player& earner, Advancement const& adv) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto name = earner.getRealName();
    level->forEachPlayer([&](Player& p) {
        if (p.isClientSide()) return true;
        auto title = trFor(p, adv.titleKey, adv.titleKey);
        std::string line;
        auto bracket = std::string(chatColor(adv.frame)) + "[" + title + "]§r";
        switch (adv.frame) {
        case Frame::Goal: line = trFor(p, "chat.type.advancement.goal", "%s has reached the goal %s", name, bracket); break;
        case Frame::Challenge: line = trFor(p, "chat.type.advancement.challenge", "%s has completed the challenge %s", name, bracket); break;
        default: line = trFor(p, "chat.type.advancement.task", "%s has made the advancement %s", name, bracket); break;
        }
        p.sendMessage(line);
        return true;
    });
}

void showToast(Player& player, Advancement const& adv) {
    if (net::isCompanion(player)) {
        net::send(player, kToastChannel,
                  {{"advancement", adv.id}, {"frame", frameName(adv.frame)}, {"title", adv.titleKey}, {"icon", adv.icon}, {"components", adv.iconComponents}});
        return;
    }
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::ToastRequest);
    if (!packet) return;
    auto& pkt    = static_cast<ToastRequestPacket&>(*packet);
    pkt.mTitle   = toastTitle(player, adv.frame);
    pkt.mContent = trFor(player, adv.titleKey, adv.titleKey);
    pkt.sendTo(player);
}

void onEarned(Player& player, std::string const& id) {
    auto const* adv = find(id);
    if (!adv) return;
    // AdvancementRewards#grant
    if (adv->experience > 0) player.addExperience(adv->experience);
    if (adv->hasDisplay) {
        if (adv->showToast) showToast(player, *adv);
        if (adv->announceToChat) announce(player, *adv);
    }
    log().info("progression: {} earned {} ({})", player.getRealName(), id, frameName(adv->frame));
    if (id == "twilightforest:progression_end") {
        auto link = "§9§n" + trFor(player, "gui.twilightforest.progression_end.discord", "Discord Server") + "§r (" + kDiscordUrl + ")";
        player.sendMessage(trFor(
            player,
            "gui.twilightforest.progression_end.message",
            "This is the end of progression for now. The Final Castle that awaits on the plateau is still unfinished and a work in progress. If you would like to keep up with the mod's development you can join our %s.",
            link
        ));
    }
    screens::refresh(player, kScreen, Json::object());
}

// ---- the vanilla form ---------------------------------------------------------------------------

void detailForm(Player& player, std::string const& id);

void listForm(Player& player) {
    auto data = screenData(player);
    std::vector<ui::FormButton> buttons;
    for (auto const& n : data["nodes"]) {
        auto        id    = n.value("id", std::string{});
        bool        done  = n.value("done", false);
        auto        title = trFor(player, n.value("title", std::string{}), n.value("title", std::string{}));
        std::string text  = done ? std::string(chatColor(n.value("frame", std::string{}) == "challenge" ? Frame::Challenge : Frame::Task)) + title
                                 : "§8" + title;
        if (!done && n.value("groups", 0) > 1) text += "§8 (" + std::to_string(n.value("met", 0)) + "/" + std::to_string(n.value("groups", 0)) + ")";
        buttons.push_back({text, "", [id](Player& p) { detailForm(p, id); }});
    }
    auto tabTitle = trFor(player, data["tab"].value("title", std::string{}), "Twilight Forest");
    ui::form(player, trFor(player, "gui.advancements", "Advancements"), tabTitle, std::move(buttons));
    log().info("progression: advancement form shown to {} ({} entries)", player.getRealName(), data["nodes"].size());
}

void detailForm(Player& player, std::string const& id) {
    auto const* adv = find(id);
    if (!adv) return;
    auto        prog = progressOf(player, *adv);
    std::string body = trFor(player, adv->descKey, adv->descKey) + "\n\n";
    body += prog.done ? toastTitle(player, adv->frame) : trFor(player, "gf.progression.progress", "Progress: {}/{}", prog.met, prog.groups);
    ui::form(player, trFor(player, adv->titleKey, adv->titleKey), body,
             {{trFor(player, "gui.back", "Back"), "", [](Player& p) { listForm(p); }}});
}

bool openFor(Player& player) { return screens::open(player, kScreen, Json::object()); }

class PresentationModule {
public:
    void install() {
        static bool registered = false;
        if (registered) return;
        registered = true;
        pr::onGranted([](Player& p, std::string const& id) {
            if (id.starts_with("twilightforest:")) onEarned(p, id);
        });
        screens::registerType({
            .id     = kScreen,
            .data   = [](Player& p, Json const&) -> std::optional<Json> { return screenData(p); },
            .action = [](Player&, Json const&, Json const&) {},
            .form   = [](Player& p, Json const&) { listForm(p); },
        });
        // the client mod's advancements key asks for the screen
        net::on(kOpenChannel, [](Player& p, Json const&) { openFor(p); });
    }
    void uninstall() {}
};

} // namespace

bool openAdvancements(Player& player) { return openFor(player); }

} // namespace gf::feature::progression_content

GF_REGISTER_MODULE(gf::feature::progression_content::PresentationModule);
GF_REGISTER_CONFIG("progression", gf::feature::progression_content::ProgressionConfig);

// /twilightforest advancements: the advancement screen (GloamingForest addition: Bedrock has no
// advancements key for vanilla clients)
GF_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("advancements").execute([](CommandOrigin const& o, CommandOutput& out) {
        auto* p = gf::commands::playerOf(o);
        if (!p) {
            out.error("Run this as a player.");
            return;
        }
        if (gf::feature::progression_content::openAdvancements(*p)) out.success(gf::trFor(*p, "gui.advancements", "Advancements"));
        else out.error("The advancements are not loaded.");
    });
});
