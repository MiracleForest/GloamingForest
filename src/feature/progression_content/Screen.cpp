// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// The advancement tree's data (U14): the nodes of TF's tab with their layout, display and the
// player's progress, as the client mod's native screen and the vanilla form show them. The
// visibility follows Java's AdvancementVisibilityEvaluator (a reimplementation of its documented
// rule: shown when done or with a done descendant, else decided by the nearest of the node, its
// parent and its grandparent that is done (shown) or hidden / without display (not shown)).

#include <functional>

#include "mc/world/actor/player/Player.h"

#include "feature/progression/Progression.h"
#include "feature/progression_content/Content.h"

namespace gf::feature::progression_content {

namespace pr = gf::progression;
namespace cp = core::progression;

namespace {

enum class Rule { Show, Hide, NoChange };

/// "twilightforest:textures/block/x.png" -> the resource pack path F1 imports it to
/// ("textures/twilightforest/block/x").
std::string texturePath(std::string const& javaPath) {
    auto p   = javaPath;
    auto ns  = std::string{"minecraft"};
    if (auto c = p.find(':'); c != std::string::npos) {
        ns = p.substr(0, c);
        p  = p.substr(c + 1);
    }
    if (p.ends_with(".png")) p.resize(p.size() - 4);
    if (p.starts_with("textures/")) p = p.substr(9);
    return ns == "minecraft" ? "textures/" + p : "textures/" + ns + "/" + p;
}

} // namespace

std::set<std::string> visibleFor(Player const& player) {
    auto const&                                     all = advancements();
    std::map<std::string, std::vector<std::size_t>> children;
    std::vector<std::size_t>                        roots;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i].parent.empty() || !find(all[i].parent)) roots.push_back(i);
        else children[all[i].parent].push_back(i);
    }
    std::set<std::string> out;
    std::vector<Rule>     stack;
    std::function<bool(std::size_t)> visit = [&](std::size_t i) -> bool {
        auto const& a    = all[i];
        bool        done = pr::has(player, a.id);
        Rule        rule = !a.hasDisplay ? Rule::Hide : done ? Rule::Show : a.hidden ? Rule::Hide : Rule::NoChange;
        bool        hasVisibleChild = done;
        stack.push_back(rule);
        for (auto c : children[a.id]) hasVisibleChild |= visit(c);
        bool visible = hasVisibleChild;
        if (!visible) {
            for (int d = 0; d <= 2 && d < static_cast<int>(stack.size()); ++d) {
                auto r = stack[stack.size() - 1 - static_cast<std::size_t>(d)];
                if (r == Rule::Show) {
                    visible = true;
                    break;
                }
                if (r == Rule::Hide) break;
            }
        }
        stack.pop_back();
        if (visible) out.insert(a.id);
        return hasVisibleChild;
    };
    for (auto r : roots) visit(r);
    return out;
}

Json screenData(Player& player) {
    auto const&                       all = advancements();
    std::vector<cp::TreeNode>         layout;
    for (auto const& a : all) layout.push_back({a.id, a.parent});
    cp::layoutTree(layout);
    auto visible = visibleFor(player);
    Json nodes   = Json::array();
    Json tab     = Json::object();
    for (std::size_t i = 0; i < all.size(); ++i) {
        auto const& a = all[i];
        if (a.parent.empty()) {
            tab = {{"id", a.id}, {"title", a.titleKey}, {"icon", a.icon}, {"background", texturePath(a.background)}};
        }
        if (!visible.contains(a.id)) continue;
        auto prog = progressOf(player, a);
        nodes.push_back({
            {"id", a.id},
            {"parent", a.parent},
            {"title", a.titleKey},
            {"desc", a.descKey},
            {"icon", a.icon},
            {"frame", frameName(a.frame)},
            {"done", prog.done},
            {"met", prog.met},
            {"groups", prog.groups},
            {"x", layout[i].x},
            {"y", layout[i].y},
        });
    }
    return {{"tab", tab}, {"nodes", nodes}};
}

} // namespace gf::feature::progression_content
