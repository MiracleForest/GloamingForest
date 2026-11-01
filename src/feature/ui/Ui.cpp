// Server half of "client.ui" (F12): the vanilla-client fallbacks of TF's screens.
//
// - form: a LeviLamina SimpleForm (ModalFormRequestPacket), every client shows it;
// - ddui: a Data-Driven UI custom form (LeviLamina ll::ui::CustomForm) built from a small JSON
//   document, for vanilla clients that support DDUI;
// - containerTitleSkin: the title convention ("gfskin:<id>;<title>") a resource pack JSON-UI would
//   read to draw a TF GUI texture around a vanilla container screen. Spike S5: the vanilla chest
//   screen's JSON-UI cannot test or cut `$container_title`, so no skin is listed
//   (kJsonUiSkins) and titles stay plain; vanilla players see the vanilla container.
//
// One log line per form shown / answered (e2e scenarios wait for them).

#include "feature/ui/Ui.h"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/ui/form/CustomForm.h"

#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include "feature/ext/ui/Server.h"
#include "mod/Log.h"

namespace gf::ui {

using Json = nlohmann::json;

namespace {

/// The player again by UUID (rule R16: no Player* across ticks).
Player* playerByUuid(mce::UUID const& id) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    return level->getPlayer(id);
}

std::string nameOf(Player const& p) { return p.getRealName(); }

std::string imageType(std::string const& icon) { return icon.starts_with("http://") || icon.starts_with("https://") ? "url" : "path"; }

} // namespace

void form(Player& player, std::string const& title, std::string const& body, std::vector<FormButton> buttons, std::function<void(Player&)> onClose) {
    ll::form::SimpleForm f(title, body);
    for (auto const& b : buttons) {
        if (b.icon.empty()) f.appendButton(b.text);
        else f.appendButton(b.text, b.icon, imageType(b.icon));
    }
    // one callback for the whole form: button callbacks are dispatched by index here
    auto shared = std::make_shared<std::vector<FormButton>>(std::move(buttons));
    log().info("ui form '{}' sent to {}", title, nameOf(player));
    f.sendTo(player, [shared, onClose = std::move(onClose), title](Player& p, int index, ll::form::FormCancelReason reason) {
        if (index < 0 || reason.has_value() || static_cast<std::size_t>(index) >= shared->size()) {
            log().info("ui form '{}' closed by {}", title, nameOf(p));
            if (onClose) onClose(p);
            return;
        }
        log().info("ui form '{}' answered by {}: button {}", title, nameOf(p), index);
        auto const& b = (*shared)[static_cast<std::size_t>(index)];
        if (b.onClick) b.onClick(p);
    });
}

// ---- DDUI ------------------------------------------------------------------------------------------

namespace {

/// One open DDUI document: the form and the values its controls write.
struct DduiSession {
    std::string                                             screenId;
    std::unique_ptr<ll::ui::CustomForm>                     form;
    std::map<std::string, ll::ui::ObservableBoolean>        toggles;
    std::map<std::string, ll::ui::ObservableNumber>         numbers;
    std::map<std::string, ll::ui::ObservableString>         texts;
    std::string                                             button; ///< id of the button pressed
    std::function<void(Player&, Json const&)>               onResponse;
    bool                                                    answered = false;
};

struct DduiState {
    std::mutex mutex; // server thread only; guards against re-entrant close callbacks
    std::unordered_map<std::string, std::shared_ptr<DduiSession>> sessions; ///< "<uuid>|<screenId>"
};
DduiState& ddui() {
    static DduiState s;
    return s;
}

std::string text(Json const& j, char const* key) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return {};
    return it->get<std::string>();
}

Json responseOf(DduiSession const& s, char const* reason) {
    Json values = Json::object();
    for (auto const& [k, v] : s.toggles) values[k] = v.getData();
    for (auto const& [k, v] : s.numbers) values[k] = v.getData();
    for (auto const& [k, v] : s.texts) values[k] = v.getData();
    Json r{{"screen", s.screenId}, {"values", values}, {"reason", reason}};
    if (!s.button.empty()) r["button"] = s.button;
    return r;
}

char const* reasonName(ll::ui::ScreenCloseReason r) {
    switch (r) {
    case ll::ui::ScreenCloseReason::ProgrammaticClose: return "button";
    case ll::ui::ScreenCloseReason::ProgrammaticCloseAll: return "closed";
    case ll::ui::ScreenCloseReason::ClientCanceled: return "canceled";
    case ll::ui::ScreenCloseReason::UserBusy: return "busy";
    case ll::ui::ScreenCloseReason::InvalidForm: return "invalid";
    }
    return "closed";
}

} // namespace

bool ddui(Player& player, std::string const& screenId, Json const& document, std::function<void(Player&, Json const&)> onResponse) {
    if (player.isSimulated() || !document.is_object()) return false; // no client to show it
    auto uuid    = player.getUuid();
    auto key     = uuid.asString() + "|" + screenId;
    auto session = std::make_shared<DduiSession>();
    session->screenId   = screenId;
    session->onResponse = std::move(onResponse);
    try {
        session->form = std::make_unique<ll::ui::CustomForm>(player, ll::ui::TextValue{text(document, "title")});
        auto& f       = *session->form;
        std::weak_ptr<DduiSession> weak = session;
        for (auto const& el : document.value("elements", Json::array())) {
            auto type = text(el, "type");
            auto id   = text(el, "id");
            auto label = ll::ui::TextValue{text(el, "text")};
            if (type == "header") f.header(label);
            else if (type == "label") f.label(label);
            else if (type == "divider") f.divider();
            else if (type == "spacer") f.spacer();
            else if (type == "button") {
                f.button(label, [weak, id] {
                    auto s = weak.lock();
                    if (!s || s->answered) return;
                    s->button = id;
                    if (s->form) (void)s->form->close(); // the show callback reports the answer
                });
            } else if (type == "toggle") {
                auto [it, _] = session->toggles.emplace(id, ll::ui::ObservableBoolean(el.value("value", false), {.clientWritable = true}));
                f.toggle(label, it->second);
            } else if (type == "slider") {
                auto [it, _] = session->numbers.emplace(id, ll::ui::ObservableNumber(el.value("value", 0.0), {.clientWritable = true}));
                ll::ui::SliderOptions opt;
                if (el.contains("step")) opt.step = ll::ui::NumberValue{el.value("step", 1.0)};
                f.slider(label, it->second, ll::ui::NumberValue{el.value("min", 0.0)}, ll::ui::NumberValue{el.value("max", 1.0)}, std::move(opt));
            } else if (type == "dropdown") {
                auto [it, _] = session->numbers.emplace(id, ll::ui::ObservableNumber(el.value("value", 0.0), {.clientWritable = true}));
                std::vector<ll::ui::DropdownItemData> items;
                double                                n = 0;
                for (auto const& o : el.value("options", Json::array())) {
                    items.push_back({ll::ui::TextValue{o.is_string() ? o.get<std::string>() : o.dump()}, n++, std::nullopt});
                }
                f.dropdown(label, it->second, std::move(items));
            } else if (type == "text_field") {
                auto [it, _] = session->texts.emplace(id, ll::ui::ObservableString(el.value("value", std::string{}), {.clientWritable = true}));
                f.textField(label, it->second);
            } else if (type == "close_button") {
                f.closeButton();
            }
        }
        auto shown = f.show([weak, uuid, key](ll::ui::CustomForm::Result result) {
            auto s = weak.lock();
            if (!s || s->answered) return;
            s->answered = true;
            char const* reason = result ? reasonName(*result) : "error";
            if (!s->button.empty()) reason = "button";
            auto response = responseOf(*s, reason);
            {
                auto& st = ddui();
                std::lock_guard lock(st.mutex);
                if (auto it = st.sessions.find(key); it != st.sessions.end() && it->second == s) st.sessions.erase(it);
            }
            if (auto* p = playerByUuid(uuid)) {
                log().info("ui ddui '{}' answered by {}: {}", s->screenId, nameOf(*p), reason);
                if (s->onResponse) s->onResponse(*p, response);
            }
        });
        if (!shown) {
            log().info("ui ddui '{}' not available for {}", screenId, nameOf(player));
            return false;
        }
    } catch (std::exception const& e) {
        log().warn("ui ddui '{}' failed for {}: {}", screenId, nameOf(player), e.what());
        return false;
    }
    {
        auto&           st = ddui();
        std::lock_guard lock(st.mutex);
        st.sessions[key] = session; // replaces (and so closes) an older document of the same screen
    }
    log().info("ui ddui '{}' sent to {}", screenId, nameOf(player));
    return true;
}

// ---- container title skins -------------------------------------------------------------------------

namespace {
constexpr std::string_view kSkinPrefix = "gfskin:";
constexpr char             kSkinEnd    = ';';
} // namespace

// A JSON-UI would match each skin by its whole marker "gfskin:<id>;"; only listed skins get one,
// any other id keeps the plain title (no marker ever shows up in a vanilla client).
std::string containerTitleSkin(std::string const& titleKey, std::string const& skinId) {
    if (std::find(kJsonUiSkins.begin(), kJsonUiSkins.end(), skinId) == kJsonUiSkins.end()) {
        static std::mutex                       m;
        static std::unordered_map<std::string, bool> warned;
        std::lock_guard                         lock(m);
        if (!skinId.empty() && !warned[skinId]) {
            warned[skinId] = true;
            log().info("ui: no JSON-UI container skin '{}' for vanilla clients: plain title", skinId);
        }
        return titleKey;
    }
    return std::string(kSkinPrefix) + skinId + kSkinEnd + titleKey;
}

TitleSkin parseTitleSkin(std::string const& title) {
    if (!title.starts_with(kSkinPrefix)) return {"", title};
    auto end = title.find(kSkinEnd, kSkinPrefix.size());
    if (end == std::string::npos) return {"", title};
    return {title.substr(kSkinPrefix.size(), end - kSkinPrefix.size()), title.substr(end + 1)};
}

} // namespace gf::ui
