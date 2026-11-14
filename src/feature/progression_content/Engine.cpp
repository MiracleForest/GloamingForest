// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/data/GloamingAdvancementGenerator.java
//   src/main/java/gloamingforest/init/GloamingAdvancements.java
//   src/main/java/gloamingforest/advancements/HurtBossTrigger.java
//   src/main/java/gloamingforest/advancements/KillBugTrigger.java
//   src/main/java/gloamingforest/advancements/DrinkFromFlaskTrigger.java
//   src/main/java/gloamingforest/advancements/UncraftItemTrigger.java
//   src/main/java/gloamingforest/advancements/AddModifierTrigger.java
//   src/main/java/gloamingforest/advancements/SimpleAdvancementTrigger.java
//   src/main/java/gloamingforest/init/custom/Restrictions.java
//   src/main/java/gloamingforest/util/Restriction.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U14's criteria engine. upstream's 45 advancements (data/gloamingforest/advancement/*.json, the JSON
// GloamingAdvancementGenerator wrote) are read here; every criterion is attached to its trigger:
//
// - minecraft:inventory_changed  progression::events::onInventoryChanged (the whole inventory,
//                                armor and offhand against every item predicate)
// - minecraft:player_killed_entity  events::onKilled (the victim, its vehicle, the structures and
//                                dimension at its feet; the killing blow's direct and source
//                                entities as F10's lethal pipeline saw them)
// - minecraft:consume_item       the trigger F10 fires after events::onConsumed ({"item": id})
// - minecraft:location, tick     every second (events::onTick) and after every grant (the
//                                "previous progression" criteria follow at once)
// - minecraft:item_used_on_block / recipe_crafted and upstream's 13 triggers: progression::onTrigger
//   (payloads in docs/units/progression.md); recipe_crafted counts F1's numbered Bedrock layouts
//   of a split Java recipe (core::progression::sameRecipe against recipeIndex())
//
// A criterion that passes is granted with progression::grant(player, advancement, criterion);
// F10's store completes the advancement when its requirements are met.

#include "feature/progression_content/Content.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/death/Death.h"
#include "feature/dimension/World.h"
#include "feature/landmark/Landmark.h"
#include "feature/progression/Progression.h"
#include "feature/recipes/Recipes.h"
#include "feature/stats/Stats.h"
#include "feature/tags/Tags.h"
#include "feature/worldgen/Worldgen.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

namespace gf::feature::progression_content {

namespace cp = core::progression;
namespace pr = gf::progression;
namespace ia = gf::interactions;

namespace {

struct Store {
    std::recursive_mutex                            mutex;
    bool                                            loaded = false;
    std::vector<Advancement>                        list;
    std::unordered_map<std::string, std::size_t>    index;
    std::map<std::string, cp::Restriction>          restrictions;
    /// trigger -> (advancement index, criterion index)
    std::map<std::string, std::vector<std::pair<std::size_t, std::size_t>>> byTrigger;
    std::set<std::string>                           statIds; ///< stats the criteria read
};

Store& store() {
    static Store s;
    return s;
}

std::filesystem::path dataDir() {
    try {
        return Mod::getInstance().getSelf().getModDir() / "data";
    } catch (...) {
        return {};
    }
}

std::string keyOf(Json const& component) {
    if (component.is_string()) return component.get<std::string>();
    if (component.is_object()) return component.value("translate", component.value("text", std::string{}));
    return {};
}

void collectStats(Json const& j, std::set<std::string>& out) {
    if (j.is_object()) {
        if (j.contains("stat") && j["stat"].is_string()) out.insert(j["stat"].get<std::string>());
        for (auto const& [_, v] : j.items()) collectStats(v, out);
    } else if (j.is_array()) {
        for (auto const& v : j) collectStats(v, out);
    }
}

std::optional<Advancement> parseAdvancement(std::string const& id, Json const& j) {
    Advancement a;
    a.id     = id;
    a.parent = j.value("parent", std::string{});
    if (auto d = j.find("display"); d != j.end() && d->is_object()) {
        a.hasDisplay = true;
        a.title       = d->value("title", Json(""));
        a.description = d->value("description", Json(""));
        a.titleKey    = keyOf(a.title);
        a.descKey     = keyOf(a.description);
        auto icon    = d->value("icon", Json::object());
        a.icon       = icon.is_object() ? icon.value("id", icon.value("item", std::string{})) : std::string{};
        a.iconComponents = icon.is_object() ? icon.value("components", Json::object()) : Json::object();
        auto frame       = d->value("frame", std::string{"task"});
        a.frame          = frame == "goal" ? Frame::Goal : frame == "challenge" ? Frame::Challenge : Frame::Task;
        a.hidden         = d->value("hidden", false);
        a.showToast      = d->value("show_toast", true);
        a.announceToChat = d->value("announce_to_chat", true);
        a.background     = d->value("background", std::string{});
    }
    auto const criteria = j.value("criteria", Json::object()); // items() must not outlive its object
    for (auto const& [name, c] : criteria.items())
        a.criteria.push_back({name, c.value("trigger", std::string{}), c.value("conditions", Json::object())});
    for (auto const& group : j.value("requirements", Json::array())) {
        std::vector<std::string> g;
        for (auto const& n : group)
            if (n.is_string()) g.push_back(n.get<std::string>());
        if (!g.empty()) a.requirements.push_back(std::move(g));
    }
    if (a.requirements.empty())
        for (auto const& c : a.criteria) a.requirements.push_back({c.name});
    if (auto r = j.find("rewards"); r != j.end() && r->is_object()) a.experience = r->value("experience", 0);
    if (a.criteria.empty()) return std::nullopt;
    return a;
}

void loadLocked(Store& s) {
    s.loaded = true;
    s.list.clear();
    s.index.clear();
    s.byTrigger.clear();
    s.restrictions.clear();
    s.statIds.clear();
    auto data = dataDir();
    std::error_code ec;
    if (data.empty() || !std::filesystem::is_directory(data, ec)) return;
    // upstream's own advancements: the top level of data/gloamingforest/advancement (the recipe unlocks
    // under recipes/ are F1's, R21)
    auto dir = data / "gloamingforest" / "advancement";
    std::vector<std::filesystem::path> files;
    for (auto const& f : std::filesystem::directory_iterator(dir, ec))
        if (f.is_regular_file() && f.path().extension() == ".json") files.push_back(f.path());
    std::sort(files.begin(), files.end());
    for (auto const& f : files) {
        try {
            std::ifstream in(f);
            auto          j  = Json::parse(in, nullptr, true, true);
            auto          id = "gloamingforest:" + f.stem().string();
            if (auto a = parseAdvancement(id, j)) {
                collectStats(j, s.statIds);
                s.index[id] = s.list.size();
                s.list.push_back(std::move(*a));
            }
        } catch (std::exception const& e) {
            log().warn("progression: bad advancement {}: {}", f.string(), e.what());
        }
    }
    for (std::size_t i = 0; i < s.list.size(); ++i)
        for (std::size_t c = 0; c < s.list[i].criteria.size(); ++c) s.byTrigger[s.list[i].criteria[c].trigger].push_back({i, c});
    // ServerAdvancementManager#apply: TreeNodePosition over every tree (children in file order)
    {
        std::vector<cp::TreeNode> layout;
        for (auto const& a : s.list) layout.push_back({a.id, a.parent, a.hasDisplay});
        cp::layoutTree(layout);
        for (std::size_t i = 0; i < s.list.size(); ++i) {
            s.list[i].x      = layout[i].x;
            s.list[i].y      = layout[i].y;
            s.list[i].placed = layout[i].placed;
        }
    }
    // the restrictions registry: data/<ns>/gloaming/restrictions/<path>.json, keyed by biome
    for (auto const& nsDir : std::filesystem::directory_iterator(data, ec)) {
        auto root = nsDir.path() / "gloaming" / "restrictions";
        if (!std::filesystem::is_directory(root, ec)) continue;
        for (auto const& f : std::filesystem::recursive_directory_iterator(root, ec)) {
            if (f.path().extension() != ".json") continue;
            try {
                std::ifstream in(f.path());
                auto          j     = Json::parse(in, nullptr, true, true);
                auto          biome = nsDir.path().filename().string() + ":"
                         + std::filesystem::relative(f.path(), root, ec).replace_extension().generic_string();
                if (auto r = cp::Restriction::fromJson(biome, j)) s.restrictions[r->biome] = std::move(*r);
                else log().warn("progression: bad restriction {}", f.path().string());
            } catch (std::exception const& e) {
                log().warn("progression: bad restriction {}: {}", f.path().string(), e.what());
            }
        }
    }
    log().info("progression: {} advancements, {} criteria triggers, {} biome restrictions", s.list.size(), s.byTrigger.size(), s.restrictions.size());
}

Store& loaded() {
    auto& s = store();
    std::lock_guard lock(s.mutex);
    if (!s.loaded) loadLocked(s);
    return s;
}

std::string dimensionName(Actor const& actor) {
    auto& dim = const_cast<Actor&>(actor).getDimension();
    if (world::isGloaming(dim)) return world::kDimensionId;
    switch (static_cast<int>(actor.getDimensionId())) {
    case 0: return "minecraft:overworld";
    case 1: return "minecraft:the_nether";
    case 2: return "minecraft:the_end";
    default: return "gf:dimension_" + std::to_string(static_cast<int>(actor.getDimensionId()));
    }
}

BlockPos feetOf(Actor const& actor) { return blockPositionOf(actor); }


bool doneCriterion(Player const& p, std::string const& adv, std::string const& crit) {
    return pr::has(p, adv) || pr::done(p, adv, crit);
}

/// Grants every criterion of `triggers` whose test passes for `player` (the player's view is
/// built at most once).
void awardWhere(Player& player, std::initializer_list<std::string> triggers, std::function<bool(Advancement const&, Criterion const&)> const& test) {
    std::vector<std::pair<std::string, std::string>> grants;
    {
        auto&                                       s = loaded();
        std::lock_guard                             lock(s.mutex);
        std::vector<std::pair<std::size_t, std::size_t>> list;
        for (auto const& t : triggers)
            if (auto it = s.byTrigger.find(t); it != s.byTrigger.end()) list.insert(list.end(), it->second.begin(), it->second.end());
        std::optional<cp::EntityView> self;
        for (auto const& [ai, ci] : list) {
            auto const& adv  = s.list[ai];
            auto const& crit = adv.criteria[ci];
            if (doneCriterion(player, adv.id, crit.name)) continue;
            // every trigger's "player" predicate
            if (auto pc = crit.conditions.find("player"); pc != crit.conditions.end() && !pc->is_null()) {
                if (!self) self = playerView(player);
                if (!cp::matchConditions(*pc, cp::Context{&*self}, env())) continue;
            }
            bool pass = false;
            try {
                pass = test(adv, crit);
            } catch (std::exception const& e) {
                log().warn("progression: criterion {}/{} failed: {}", adv.id, crit.name, e.what());
            }
            if (pass) grants.push_back({adv.id, crit.name});
        }
    }
    for (auto const& [adv, crit] : grants) {
        log().info("progression: {} met {} / {}", player.getRealName(), adv, crit);
        pr::grant(player, adv, crit);
    }
}

void awardWhere(Player& player, std::string const& trigger, std::function<bool(Advancement const&, Criterion const&)> const& test) {
    awardWhere(player, {trigger}, test);
}

std::vector<cp::ItemView> inventoryOf(Player& player) {
    std::vector<cp::ItemView> out;
    auto                      add = [&](ItemStack const& s) {
        if (!game::items::isEmpty(s)) out.push_back(itemView(s));
    };
    auto& inv = player.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) add(inv.getItem(i));
    using SharedTypes::Legacy::ArmorSlot;
    for (auto slot : {ArmorSlot::Head, ArmorSlot::Torso, ArmorSlot::Legs, ArmorSlot::Feet}) add(player.getArmor(slot));
    add(player.getOffhandSlot());
    return out;
}

std::optional<BlockPos> posIn(Json const& data) {
    auto it = data.find("pos");
    if (it == data.end()) return std::nullopt;
    if (it->is_array() && it->size() >= 3) return BlockPos{(*it)[0].get<int>(), (*it)[1].get<int>(), (*it)[2].get<int>()};
    if (it->is_object()) return BlockPos{it->value("x", 0), it->value("y", 0), it->value("z", 0)};
    return std::nullopt;
}

/// What the lethal pipeline saw of a dying entity (PlayerKilledEntityTrigger's view).
struct KillSnapshot {
    std::shared_ptr<cp::EntityView> vehicle; ///< what the victim rode at the killing blow
    std::optional<cp::EntityView>   direct;  ///< the direct entity when it was not the attacker
    std::uint64_t                   tick = 0;
};
struct Kills {
    std::mutex                                       mutex;
    std::unordered_map<std::int64_t, KillSnapshot>   byVictim;
};
Kills& kills() {
    static Kills k;
    return k;
}
std::uint64_t currentTick() {
    auto level = ll::service::getLevel();
    return level ? static_cast<std::uint64_t>(level->getCurrentServerTick().tickID) : 0;
}

bool regenerating(Block const& b) {
    for (auto const& [k, v] : game::statesOf(b))
        if ((k == "regenerate" || k.ends_with(":regenerate")) && (v == "true" || v == "1")) return true;
    return false;
}

class EngineModule {
public:
    void install() {
        static bool registered = false;
        if (registered) return;
        registered = true;
        // the data files are the mod's own and never change while it runs: loaded once (on the first
        // world or the first use), so the references advancements() / restrictions() hand out stay
        // valid on every thread (the client mod reads them on the client thread)
        game::db::onWorldReady([] { (void)loaded(); });
        (void)loaded(); // BDS: the world was ready before the mod was enabled

        pr::events::onInventoryChanged([](Player& p) { evaluateInventory(p); });

        // PlayerKilledEntityTrigger reads the victim as it was when the killing blow landed (its
        // vehicle before die() dismounts it) and the damage source's direct entity: both are
        // captured in F10's lethal pipeline (last, so only deaths nobody prevents) and read back
        // when F10 fires onKilled after die()
        death::onLethal(-1000000, [](Actor& victim, combat::DamageInfo const& d) {
            // only kills a player can be credited with; the direct entity is the one of the two
            // source entities that is not the player (whichever way round the damage source names
            // them: F10's combat::entitiesOf reads a child-actor source's ids as (owner, child))
            if (victim.isClientSide()) return false;
            Actor* player = d.attacker && d.attacker->isPlayer() ? d.attacker : d.direct && d.direct->isPlayer() ? d.direct : nullptr;
            if (!player) return false;
            Actor* other = d.attacker && d.attacker != player ? d.attacker : d.direct && d.direct != player ? d.direct : nullptr;
            KillSnapshot snap;
            if (auto* veh = victim.getVehicle()) snap.vehicle = std::make_shared<cp::EntityView>(entityView(*veh, false));
            if (other) snap.direct = entityView(*other, false);
            snap.tick = currentTick();
            auto& k   = kills();
            std::lock_guard lock(k.mutex);
            if (k.byVictim.size() > 256) std::erase_if(k.byVictim, [now = snap.tick](auto const& e) { return now - e.second.tick > 40; });
            k.byVictim[victim.getOrCreateUniqueID().rawID] = std::move(snap);
            return false;
        });

        pr::events::onKilled([](Player& killer, Actor& victim, std::string const&) {
            std::optional<KillSnapshot> snap;
            {
                auto&           k = kills();
                std::lock_guard lock(k.mutex);
                if (auto it = k.byVictim.find(victim.getOrCreateUniqueID().rawID); it != k.byVictim.end()) {
                    snap = std::move(it->second);
                    k.byVictim.erase(it);
                }
            }
            auto view = entityView(victim);
            if (snap && snap->vehicle && !view.vehicle) view.vehicle = snap->vehicle; // dismounted by die()
            // the killing blow (Java's DamageSource of LivingEntity#die): with a snapshot the player
            // dealt it, its direct entity the projectile when there was one, else the killer (a
            // melee blow: getDirectEntity is the attacking player). Without one F10 credited the
            // player (lastHurtByPlayer) for a death by a source without entities, e.g. fire or a
            // fall after the player's hit: Java's source then has neither a direct nor a causing
            // entity, and isDirect() is true (null == null)
            std::optional<cp::EntityView> direct, source;
            if (snap) {
                source       = entityView(killer, false);
                source->type = "minecraft:player";
                direct       = snap->direct ? *snap->direct : *source;
            }
            bool isDirect = !(snap && snap->direct);
            awardWhere(killer, "minecraft:player_killed_entity", [&](Advancement const&, Criterion const& c) {
                if (auto e = c.conditions.find("entity"); e != c.conditions.end() && !cp::matchConditions(*e, cp::Context{&view}, env())) return false;
                if (auto kb = c.conditions.find("killing_blow"); kb != c.conditions.end() && kb->is_object()) {
                    for (auto const& [key, pred] : kb->items()) {
                        if (key == "direct_entity") {
                            if (!direct || !cp::matchEntity(pred, *direct, env())) return false;
                        } else if (key == "source_entity") {
                            if (!source || !cp::matchEntity(pred, *source, env())) return false;
                        } else if (key == "is_direct") {
                            if (!pred.is_boolean() || pred.get<bool>() != isDirect) return false;
                        } else {
                            return false; // damage type tags: not used by upstream's advancements
                        }
                    }
                }
                return true;
            });
        });
        // F10 fires minecraft:consume_item {"item": id} after Player#completeUsingItem
        pr::onTrigger("minecraft:consume_item", [](Player& p, Json const& data) {
            cp::ItemView item;
            item.id = cp::idIn(data, {"item"});
            awardWhere(p, "minecraft:consume_item", [&](Advancement const&, Criterion const& c) {
                auto pred = c.conditions.find("item");
                return pred == c.conditions.end() || cp::matchItem(*pred, item, env());
            });
        });

        pr::events::onTick([](Player& p) { evaluatePlayerCriteria(p); });
        pr::onTrigger("minecraft:location", [](Player& p, Json const&) { evaluatePlayerCriteria(p); });

        pr::onTrigger("minecraft:item_used_on_block", [](Player& p, Json const& data) {
            cp::BlockView block;
            block.id = cp::idIn(data, {"block"});
            if (auto s = data.find("states"); s != data.end() && s->is_object()) block.states = *s;
            if (auto pos = posIn(data); pos && (block.id.empty() || !data.contains("states"))) {
                // the block at the position after the use (ItemUsedOnLocationTrigger reads it then)
                auto& region = p.getDimensionBlockSource();
                if (region.hasChunksAt(*pos, 0, false)) {
                    auto const& b = region.getBlock(*pos);
                    block.id      = game::blockId(b);
                    block.states  = Json::object();
                    for (auto const& [k, v] : game::statesOf(b)) block.states[k] = v;
                }
            }
            cp::ItemView tool;
            tool.id = cp::idIn(data, {"item", "tool", "stack"});
            awardWhere(p, "minecraft:item_used_on_block", [&](Advancement const&, Criterion const& c) {
                auto loc = c.conditions.find("location");
                return loc == c.conditions.end() || cp::matchConditions(*loc, cp::Context{nullptr, &block, &tool}, env());
            });
        });

        // redstone on experiment 115 (upstream's Experiment115Block#useItemOn sets regenerate): vanilla
        // fires item_used_on_block for a block's own use; the click is watched here (below F6's
        // protection) and the trigger fires when the block turned regenerating
        ia::onUseOn(
            {.block = "gloamingforest:experiment_115", .item = "minecraft:redstone"},
            [](ia::UseOnContext const& c) {
                if (!c.firstEvent) return ia::Result::Pass;
                bool before = regenerating(c.block);
                auto id     = c.player.getOrCreateUniqueID();
                auto pos    = c.pos;
                ia::nextTick([id, pos, before] {
                    auto  level  = ll::service::getLevel();
                    auto* p      = level ? level->getPlayer(id) : nullptr;
                    auto* region = game::regionOf(pos.dim);
                    if (!p || !region || before) return;
                    auto const& b = region->getBlock(game::blockPos(pos));
                    if (game::blockId(b) != "gloamingforest:experiment_115" || !regenerating(b)) return;
                    pr::trigger(*p, "minecraft:item_used_on_block", {{"pos", {pos.x, pos.y, pos.z}}, {"item", "minecraft:redstone"}});
                });
                return ia::Result::Pass;
            },
            ia::priority::Highest - 1
        );

        // magic beans on uberous soil (MagicBeansItem#useOn, U29's item): ServerPlayerGameMode fires
        // item_used_on_block when the use consumed the action, i.e. when the soil had air above it
        // and a beanstalk grower took its place; watched here so beanstalk's use_beans does not
        // depend on the item's owner firing it (a second trigger is harmless)
        ia::onUseOn(
            {.block = "gloamingforest:uberous_soil", .item = "gloamingforest:magic_beans"},
            [](ia::UseOnContext const& c) {
                if (!c.firstEvent) return ia::Result::Pass;
                auto  id     = c.player.getOrCreateUniqueID();
                auto  pos    = c.pos;
                auto& here   = c.player.getDimensionBlockSource();
                auto  bpNow  = game::blockPos(pos);
                if (!here.getBlock(BlockPos{bpNow.x, bpNow.y + 1, bpNow.z}).isAir()) return ia::Result::Pass; // the beans need air above
                ia::nextTick([id, pos] {
                    auto  level  = ll::service::getLevel();
                    auto* p      = level ? level->getPlayer(id) : nullptr;
                    auto* region = game::regionOf(pos.dim);
                    if (!p || !region) return;
                    auto bp = game::blockPos(pos);
                    if (game::blockId(region->getBlock(bp)) != "gloamingforest:uberous_soil") return;
                    if (game::blockId(region->getBlock(BlockPos{bp.x, bp.y + 1, bp.z})) != "gloamingforest:beanstalk_grower") return;
                    pr::trigger(*p, "minecraft:item_used_on_block", {{"pos", {pos.x, pos.y, pos.z}}, {"item", "gloamingforest:magic_beans"}});
                });
                return ia::Result::Pass;
            },
            ia::priority::Highest - 1
        );

        pr::onTrigger("minecraft:recipe_crafted",[](Player& p, Json const& data) {
            auto crafted = cp::idIn(data, {"recipe_id", "recipe"});
            awardWhere(p, "minecraft:recipe_crafted", [&](Advancement const&, Criterion const& c) {
                return cp::sameRecipe(c.conditions.value("recipe_id", std::string{}), crafted, recipeIndex());
            });
        });

        // upstream's triggers (GloamingAdvancements): fired by their owners through progression::trigger
        for (char const* t : {"gloamingforest:make_gf_portal", "gloamingforest:hurt_boss", "gloamingforest:kill_bug", "gloamingforest:broke_glass_sword",
                              "gloamingforest:drink_from_flask", "gloamingforest:consume_hydra_chop_on_low_hunger", "gloamingforest:add_modifier",
                              "gloamingforest:uncraft_item", "gloamingforest:complete_quest_ram", "gloamingforest:placed_on_trophy_pedestal",
                              "gloamingforest:kill_all_phantoms", "gloamingforest:activate_ghast_trap", "gloamingforest:structure_cleared"}) {
            std::string trigger = t;
            pr::onTrigger(trigger, [trigger](Player& p, Json const& data) {
                awardWhere(p, trigger, [&](Advancement const&, Criterion const& c) { return cp::matchGloamingTrigger(trigger, c.conditions, data, env()); });
            });
        }

        // the "previous progression" tick criteria follow every grant at once
        pr::onGranted([](Player& p, std::string const& adv) {
            if (adv.starts_with("gloamingforest:")) evaluatePlayerCriteria(p);
        });
    }
    void uninstall() {}
};

} // namespace

cp::RecipeIndex const& recipeIndex() {
    static cp::RecipeIndex const index{
        // a Java data recipe (F9 indexes the game's recipes too, marked "native")
        [](std::string const& id) {
            auto const* r = gf::recipes::byId(id);
            return r != nullptr && !(r->data.is_object() && r->data.value("native", false));
        },
        // a recipe identifier the game loaded (under any recipe tag)
        [](std::string const& id) {
            auto level = ll::service::getLevel();
            if (!level) return false;
            auto const& byTag = level->getRecipes().mRecipes.get();
            auto        bare  = id.starts_with("minecraft:") ? id.substr(10) : id;
            for (auto const& [tag, recipes] : byTag)
                if (recipes.contains(id) || recipes.contains(bare)) return true;
            return false;
        },
    };
    return index;
}

char const* frameName(Frame f) { return f == Frame::Goal ? "goal" : f == Frame::Challenge ? "challenge" : "task"; }

std::vector<Advancement> const& advancements() { return loaded().list; }

Advancement const* find(std::string const& id) {
    auto&           s = loaded();
    std::lock_guard lock(s.mutex);
    auto            it = s.index.find(id);
    return it == s.index.end() ? nullptr : &s.list[it->second];
}

std::map<std::string, cp::Restriction> const& restrictions() { return loaded().restrictions; }

cp::Restriction const* restrictionFor(Player const& player, std::string const& biome) {
    auto const& all = restrictions();
    auto        it  = all.find(biome);
    if (it == all.end()) return nullptr;
    if (it->second.liftedFor([&](std::string const& a) { return pr::has(player, a); })) return nullptr;
    return &it->second;
}

cp::Env const& env() {
    static cp::Env e = [] {
        cp::Env v;
        v.itemTag   = [](std::string const& tag, std::string const& id) { return tags::has(tags::Registry::Item, tag, id); };
        v.entityTag = [](std::string const& tag, std::string const& id) { return tags::has(tags::Registry::EntityType, tag, id); };
        v.blockTag  = [](std::string const& tag, std::string const& id) { return tags::has(tags::Registry::Block, tag, id); };
        v.biomeTag  = [](std::string const& tag, std::string const& id) { return tags::has(tags::Registry::Biome, tag, id); };
        return v;
    }();
    return e;
}

cp::ItemView itemView(ItemStack const& stack) {
    cp::ItemView v;
    v.id    = game::items::idOf(stack);
    v.count = game::items::countOf(stack);
    // minecraft:dyed_color: Bedrock's dye tag (customColor, ARGB) or an item data document
    if (auto const* tag = game::items::userData(stack); tag && tag->contains("customColor")) {
        auto const& c = tag->at("customColor");
        if (c.is_number_integer()) v.dyedColor = static_cast<int>(c) & 0xFFFFFF;
    }
    if (!v.dyedColor)
        for (auto const* key : {"minecraft:dyed_color", "dyed_color"}) {
            auto d = game::items::getData(stack, key);
            if (d.is_number_integer()) v.dyedColor = d.get<int>() & 0xFFFFFF;
            else if (d.is_object() && d.contains("rgb")) v.dyedColor = d["rgb"].get<int>() & 0xFFFFFF;
            if (v.dyedColor) break;
        }
    return v;
}

std::string biomeAt(Actor& actor) {
    auto bp = feetOf(actor);
    if (world::isGloaming(actor.getDimension())) {
        auto id = worldgen::biomeAt(bp);
        if (!id.empty()) return id;
    }
    auto& region = actor.getDimensionBlockSource();
    if (!region.hasChunksAt(bp, 0, false)) return {};
    std::string name = region.getBiome(bp).mHash->getString();
    return name.find(':') == std::string::npos ? "minecraft:" + name : name;
}

std::vector<std::string> structuresAt(Actor& actor) {
    std::vector<std::string> out;
    auto                     bp    = feetOf(actor);
    auto                     start = landmark::startAt(static_cast<int>(actor.getDimensionId()), bp);
    if (!start) return out;
    bool inPiece = start->pieceBoxes.empty() ? start->box.contains(bp.x, bp.y, bp.z) : false;
    for (auto const& b : start->pieceBoxes)
        if (b.contains(bp.x, bp.y, bp.z)) inPiece = true;
    if (inPiece) out.push_back(start->key);
    return out;
}

cp::EntityView entityView(Actor& actor, bool withVehicle) {
    cp::EntityView v;
    v.type      = actor.getTypeName();
    v.isBaby    = actor.isBaby();
    v.dimension = dimensionName(actor);
    v.biome     = biomeAt(actor);
    v.structures = structuresAt(actor);
    if (withVehicle)
        if (auto* veh = actor.getVehicle()) v.vehicle = std::make_shared<cp::EntityView>(entityView(*veh, false));
    auto const& held = actor.getCarriedItem();
    if (!game::items::isEmpty(held)) v.mainhand = itemView(held);
    return v;
}

cp::EntityView playerView(Player& player) {
    auto v = entityView(player, true);
    v.type = "minecraft:player";
    auto& s = loaded();
    std::vector<std::string> ids;
    std::set<std::string>    stats;
    {
        std::lock_guard lock(s.mutex);
        for (auto const& a : s.list) ids.push_back(a.id);
        stats = s.statIds;
    }
    for (auto const& id : ids)
        if (pr::has(player, id)) v.advancements.insert(id);
    for (auto const& st : stats) v.stats[st] = stats::value(player, st);
    return v;
}

void evaluatePlayerCriteria(Player& player) {
    // LocationTrigger / PlayerTrigger(TICK): only the player predicate
    awardWhere(player, {std::string("minecraft:tick"), std::string("minecraft:location")}, [](Advancement const&, Criterion const&) { return true; });
}

void evaluateInventory(Player& player) {
    std::optional<std::vector<cp::ItemView>> inv;
    awardWhere(player, "minecraft:inventory_changed", [&](Advancement const&, Criterion const& c) {
        if (!inv) inv = inventoryOf(player);
        return cp::matchInventory(c.conditions, *inv, env());
    });
}

std::map<std::string, int> criteriaByTrigger() {
    auto&                      s = loaded();
    std::lock_guard            lock(s.mutex);
    std::map<std::string, int> out;
    for (auto const& [t, list] : s.byTrigger) out[t] = static_cast<int>(list.size());
    return out;
}

std::string textFor(Player const& player, Json const& component) {
    auto locale = localeOf(player);
    return cp::renderComponent(component, [&](std::string const& key, std::string const& fallback, std::vector<std::string> const& args) {
        return core::formatMessage(translate(key, fallback, locale), args);
    });
}

namespace {

/// Java potion ids -> Bedrock potion aux values (the flask icon's potion).
int potionAux(std::string const& javaId) {
    static std::unordered_map<std::string, int> const m{
        {"minecraft:water", 0},           {"minecraft:mundane", 1},          {"minecraft:thick", 3},
        {"minecraft:awkward", 4},         {"minecraft:night_vision", 5},     {"minecraft:long_night_vision", 6},
        {"minecraft:invisibility", 7},    {"minecraft:long_invisibility", 8}, {"minecraft:leaping", 9},
        {"minecraft:long_leaping", 10},   {"minecraft:strong_leaping", 11},  {"minecraft:fire_resistance", 12},
        {"minecraft:long_fire_resistance", 13}, {"minecraft:swiftness", 14}, {"minecraft:long_swiftness", 15},
        {"minecraft:strong_swiftness", 16}, {"minecraft:slowness", 17},      {"minecraft:long_slowness", 18},
        {"minecraft:water_breathing", 19}, {"minecraft:long_water_breathing", 20}, {"minecraft:healing", 21},
        {"minecraft:strong_healing", 22}, {"minecraft:harming", 23},          {"minecraft:strong_harming", 24},
        {"minecraft:poison", 25},         {"minecraft:long_poison", 26},      {"minecraft:strong_poison", 27},
        {"minecraft:regeneration", 28},   {"minecraft:long_regeneration", 29}, {"minecraft:strong_regeneration", 30},
        {"minecraft:strength", 31},       {"minecraft:long_strength", 32},    {"minecraft:strong_strength", 33},
        {"minecraft:weakness", 34},       {"minecraft:long_weakness", 35},    {"minecraft:turtle_master", 37},
        {"minecraft:long_turtle_master", 38}, {"minecraft:strong_turtle_master", 39}, {"minecraft:slow_falling", 40},
        {"minecraft:long_slow_falling", 41}, {"minecraft:strong_slowness", 42}, {"minecraft:wind_charged", 43},
        {"minecraft:weaving", 44},        {"minecraft:oozing", 45},           {"minecraft:infested", 46},
    };
    auto it = m.find(javaId);
    return it == m.end() ? -1 : it->second;
}

/// An icon component as the item data the port keeps: upstream's potion flask component becomes U9's
/// flask data ("flask_contents" {potion: Bedrock potion aux, doses, breakage, breakable}), so the
/// icon draws the filled flask; other components keep their id.
std::pair<std::string, Json> iconData(std::string const& component, Json const& value) {
    if (component == "gloamingforest:flask_contents" && value.is_object()) {
        auto        pot  = value.value("potion", Json::object());
        std::string java = pot.is_object() ? pot.value("potion", std::string{}) : pot.is_string() ? pot.get<std::string>() : std::string{};
        return {"flask_contents",
                Json{{"potion", potionAux(java)}, {"doses", value.value("doses", 0)}, {"breakage", value.value("breakage", 0)}, {"breakable", value.value("breakable", true)}}};
    }
    return {component, value};
}

} // namespace

Json iconItem(Advancement const& adv) {
    if (adv.icon.empty()) return nullptr;
    auto stack = game::items::make(adv.icon, 1);
    if (!stack) return Json{{"id", adv.icon}, {"count", 1}, {"aux", 0}};
    if (adv.iconComponents.is_object())
        for (auto const& [component, value] : adv.iconComponents.items()) {
            auto [key, data] = iconData(component, value);
            game::items::setData(*stack, key, data);
        }
    return game::items::toJson(*stack);
}

Progress progressOf(Player const& player, Advancement const& adv) {
    Progress p;
    p.done   = pr::has(player, adv.id);
    p.groups = static_cast<int>(adv.requirements.size());
    for (auto const& g : adv.requirements) {
        bool any = p.done || std::any_of(g.begin(), g.end(), [&](std::string const& c) { return pr::done(player, adv.id, c); });
        if (any) ++p.met;
    }
    return p;
}

BlockPos blockPositionOf(Actor const& actor) {
    // Java's blockPosition(): the feet (Bedrock keeps a player's position at the eyes); the epsilon
    // keeps a feet y of 63.9999 (float rounding of a player standing on y 64) on block 64
    auto p = actor.getFeetPos();
    return {static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y + 0.001f)), static_cast<int>(std::floor(p.z))};
}

} // namespace gf::feature::progression_content

GF_REGISTER_MODULE(gf::feature::progression_content::EngineModule);
