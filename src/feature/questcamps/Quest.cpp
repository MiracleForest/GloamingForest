// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/passive/quest/QuestReloadListener.java
//   src/main/java/twilightforest/entity/passive/quest/ram/QuestingRamContext.java
//   src/main/java/twilightforest/entity/passive/quest/ram/QuestingRamCurrentContext.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The quest of the Questing Ram (registry twilight/quests, entry twilightforest:questing_ram): which
// item each of the 16 dye colours asks for, and the reward table. A quest that does not name all 16
// colours is refused (QuestingRamContext#validate) and the fallback (the 16 wools) stays.

#include <memory>
#include <mutex>

#include "core/questcamps/Questcamps.h"
#include "feature/ext/questcamps/W0Internals.h"
#include "feature/questcamps/Questcamps.h"
#include "feature/tags/Tags.h"
#include "mod/Log.h"

namespace gf::feature::questcamps {

namespace cq   = gf::core::questcamps;
namespace data = gf::feature::item::data;

namespace {

Quest fallback() {
    Quest q;
    for (int i = 0; i < 16; ++i) q.items[static_cast<std::size_t>(i)] = Json{{"item", "minecraft:" + std::string(cq::kDyeNames[static_cast<std::size_t>(i)]) + "_wool"}};
    q.reward = "twilightforest:entities/questing_ram_rewards";
    return q;
}

std::mutex& questMutex() {
    static std::mutex m;
    return m;
}
/// The current quest, swapped whole on reload (readers keep their snapshot).
std::shared_ptr<Quest const>& current() {
    static std::shared_ptr<Quest const> q = std::make_shared<Quest const>(fallback());
    return q;
}
std::shared_ptr<Quest const> snapshot() {
    std::lock_guard lock(questMutex());
    return current();
}

} // namespace

Quest quest() { return *snapshot(); }

void reloadQuest() {
    Quest q = fallback();
    for (auto const& f : data::list("twilight/quests")) {
        if (f.id() != "twilightforest:questing_ram") continue;
        auto doc = data::read(f.path);
        if (!doc.is_object() || !doc.contains("items") || !doc["items"].is_object()) {
            gf::log().warn("questcamps: {} has no items; the fallback quest stays", f.id());
            break;
        }
        Quest loaded;
        int   flags = 0;
        bool bad = false;
        for (auto const& [colour, ingredient] : doc["items"].items()) {
            int id = cq::dyeId(colour);
            // DyeColor.CODEC / Ingredient.CODEC_NONEMPTY: an unknown colour or an empty ingredient fails the whole quest
            if (id < 0 || ingredient.is_null() || (ingredient.is_array() && ingredient.empty())) {
                bad = true;
                break;
            }
            loaded.items[static_cast<std::size_t>(id)] = ingredient;
            flags |= 1 << id;
        }
        if (bad || !doc.contains("reward") || !doc["reward"].is_string()) {
            gf::log().warn("questcamps: {} does not decode (colours, ingredients, reward); the fallback quest stays", f.id());
            break;
        }
        if (cq::countColors(flags) != 16) {
            gf::log().warn("questcamps: Questing Ram quest must contain all 16 dye colors ({}); the fallback quest stays", f.id());
            break;
        }
        loaded.reward   = doc["reward"].get<std::string>();
        loaded.fromData = true;
        q               = std::move(loaded);
        break;
    }
    auto next = std::make_shared<Quest const>(std::move(q));
    std::lock_guard lock(questMutex());
    current() = std::move(next);
}

bool ingredientMatches(Json const& ingredient, std::string const& itemId) {
    if (itemId.empty()) return false;
    if (ingredient.is_array()) {
        for (auto const& e : ingredient)
            if (ingredientMatches(e, itemId)) return true;
        return false;
    }
    if (!ingredient.is_object()) return false;
    if (ingredient.contains("item")) return data::withNamespace(ingredient["item"].get<std::string>()) == itemId;
    if (ingredient.contains("tag")) {
        auto tag = ingredient["tag"].get<std::string>();
        if (!tag.empty() && tag[0] == '#') tag.erase(0, 1);
        return gf::tags::has(gf::tags::Registry::Item, data::withNamespace(tag), itemId);
    }
    return false;
}

std::vector<int> questColorsOf(std::string const& itemId) {
    std::vector<int> out;
    auto             q = snapshot();
    for (int i = 0; i < 16; ++i)
        if (ingredientMatches(q->items[static_cast<std::size_t>(i)], itemId)) out.push_back(i);
    return out;
}

Json questJson() {
    auto q     = quest();
    Json items = Json::object();
    for (int i = 0; i < 16; ++i)
        if (!q.items[static_cast<std::size_t>(i)].is_null()) items[std::string(cq::kDyeNames[static_cast<std::size_t>(i)])] = q.items[static_cast<std::size_t>(i)];
    return Json{{"items", items}, {"reward", q.reward}};
}

} // namespace gf::feature::questcamps
