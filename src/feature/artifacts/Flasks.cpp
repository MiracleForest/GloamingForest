// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/item/BrittleFlaskItem.java
//   src/main/java/twilightforest/item/GreaterFlaskItem.java
//   src/main/java/twilightforest/components/item/PotionFlaskComponent.java
//   src/main/java/twilightforest/components/entity/PotionFlaskTrackingAttachment.java
//   src/main/java/twilightforest/client/renderer/tooltip/PotionFlaskTooltipComponent.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The brittle and greater potion flasks: up to 3 doses of one potion (item data "flask_contents",
// TF's component), drunk over 32 ticks (BP use_modifiers, DRINK). Filling: Java right-clicks a
// potion onto the flask in an inventory; Bedrock has no such click, so a flask is filled by using
// it (or the potion) while the potion (or the flask) is in the other hand (docs/units/artifacts.md).
// A strong harming dose deals 6 << amplifier twilightforest:failed_challenge damage; each drink
// fires twilightforest:drink_from_flask (doses in a row of the same potion and the seconds since
// the first) and a brittle flask cracks, then breaks at its third dose. The dose bar is the
// potion's colour; filled flasks carry a stack nonce so they never stack (Java: stack size 1).
#include <cmath>
#include <random>
#include <unordered_map>

#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffect.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"

#include "feature/artifacts/Artifacts.h"
#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/effects/Effects.h"
#include "feature/item/Item.h"
#include "feature/progression/Progression.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::artifacts {

namespace ia    = gf::feature::common::interactions;
namespace items = game::items;
namespace ca    = core::artifacts;

namespace {

bool isFlask(std::string const& id) { return id == kBrittleFlask || id == kGreaterFlask; }

bool isPotion(std::string const& id) { return id == "minecraft:potion" || id == "minecraft:splash_potion" || id == "minecraft:lingering_potion"; }

std::string trackKey(Player const& p) { return "gf:artifacts:flask_doses:" + p.getUuid().asString(); }

std::unordered_map<std::string, ca::FlaskTracking>& trackings() {
    static std::unordered_map<std::string, ca::FlaskTracking> m;
    return m;
}

void saveTracking(Player& p, ca::FlaskTracking const& t) {
    game::db::put(trackKey(p), Json{{"last_used_potion", t.lastPotion}, {"doses_drank", t.dosesDrank}, {"last_game_time_started", t.startTime}});
}

/// BrittleFlaskItem#changeAndConsumeFlask: a stacked flask splits one off for the change.
ItemStack* splitOne(Player& player, ItemStack& stack, ItemStack& spare) {
    if (items::countOf(stack) <= 1) return &stack;
    spare = items::withCount(stack, 1);
    stack.remove(1);
    return &spare;
}

void giveOrDrop(Player& player, ItemStack s) {
    if (items::isEmpty(s)) return;
    if (!player.add(s) && !items::isEmpty(s)) player.getLevel().getSpawner().spawnItem(player.getDimensionBlockSource(), s, nullptr, player.getPosition(), 0);
}

std::string potionName(int aux, Player const* viewer) {
    (void)viewer;
    if (auto s = items::make("minecraft:potion", 1, aux)) return items::displayName(*s);
    return {};
}

void flaskTooltip(ItemStack const& stack, std::vector<std::string>& lines, Player const* viewer) {
    auto f = flaskOf(stack);
    if (f.empty()) {
        lines.push_back("§7" + (viewer ? trFor(*viewer, "item.twilightforest.flask.empty_description", "Can hold multiple potion doses")
                                       : tr("item.twilightforest.flask.empty_description", "Can hold multiple potion doses")));
        lines.push_back("§f" + (viewer ? trFor(*viewer, "item.twilightforest.flask.empty", "Empty") : tr("item.twilightforest.flask.empty", "Empty")));
        return;
    }
    lines.push_back("§7" + potionName(f.potion, viewer));
    // the dose bar (PotionFlaskTooltipComponent): filled doses, room, broken segments
    std::string bar = "§8[";
    for (int i = 0; i < ca::kFlaskDoses; ++i) {
        if (i < f.doses) bar += "§b■";
        else if (i >= ca::kFlaskDoses - f.breakage) bar += "§4✕";
        else bar += "§7□";
    }
    lines.push_back(bar + "§8]");
}

} // namespace

ca::Flask flaskOf(ItemStack const& stack) {
    ca::Flask f;
    f.breakable = items::idOf(stack) != kGreaterFlask;
    auto j      = items::getData(stack, kDataFlask);
    if (!j.is_object()) return f;
    f.potion    = j.value("potion", -1);
    f.doses     = j.value("doses", 0);
    f.breakage  = j.value("breakage", 0);
    f.breakable = j.value("breakable", f.breakable);
    return f;
}

void setFlask(ItemStack& stack, ca::Flask const& f) {
    static std::mt19937_64 rng{std::random_device{}()};
    if (f.empty() && f.doses == 0 && f.breakage == 0) {
        items::setData(stack, kDataFlask, nullptr); // the default component: stacks again
        return;
    }
    Json j{{"potion", f.potion}, {"doses", f.doses}, {"breakage", f.breakage}, {"breakable", f.breakable}};
    // stacksTo 1 while filled (and a cracked flask is its own stack)
    j["nonce"] = static_cast<std::int64_t>(rng() & 0x7FFFFFFFFFFFLL);
    items::setData(stack, kDataFlask, j);
}

bool fillFlask(Player& player, ItemStack& flask, ItemStack& potion, bool flaskInHand) {
    (void)flaskInHand;
    if (!isFlask(items::idOf(flask)) || !isPotion(items::idOf(potion))) return false;
    int  aux = potion.getAuxValue();
    auto f   = flaskOf(flask);
    if (!f.canFill(aux)) return false;
    if (!creative(player)) {
        potion.remove(1);
        if (items::countOf(potion) <= 0) potion = items::empty();
        if (auto bottle = items::make("minecraft:glass_bottle")) giveOrDrop(player, *bottle);
    }
    ItemStack spare;
    ItemStack* one = splitOne(player, flask, spare);
    setFlask(*one, f.addDose(aux));
    if (one == &spare) giveOrDrop(player, spare);
    auto& r = levelRandom();
    playSound(player, "item.twilightforest.flask.fill", static_cast<float>(f.doses + 1) * 0.25F, r.nextFloat() * 0.1F + 0.9F);
    log().info("artifacts: {} filled a flask ({} dose(s) of potion {})", player.getRealName(), f.doses + 1, aux);
    return true;
}

ca::FlaskTracking& flaskTracking(Player& player) {
    auto key = trackKey(player);
    auto it  = trackings().find(key);
    if (it != trackings().end()) return it->second;
    ca::FlaskTracking t;
    if (auto doc = game::db::get(key); doc && doc->is_object()) {
        t.lastPotion = doc->value("last_used_potion", std::string{});
        t.dosesDrank = doc->value("doses_drank", 0);
        t.startTime  = doc->value("last_game_time_started", std::int64_t{0});
    }
    return trackings().emplace(key, t).first->second;
}

void drinkFlask(Player& player, ItemStack& stack) {
    auto f = flaskOf(stack);
    if (f.empty()) return;
    auto const* potion = ca::potionByAux(f.potion);
    if (potion) {
        for (auto const& e : potion->effects) {
            // an amplified Instant Damage (Instant Health for the undead) is the failed challenge
            if (e.id == "instant_damage" && e.amp > 0) {
                gf::combat::hurt(player, static_cast<float>(6 << e.amp), "twilightforest:failed_challenge");
            } else if (e.instant) {
                gf::effects::apply(player, "minecraft:" + e.id, 1, e.amp);
            } else if (auto const* me = MobEffect::getByName(e.id)) {
                MobEffectInstance inst(me->mId, EffectDuration{e.ticks});
                inst.mAmplifier = e.amp;
                player.addEffect(inst);
            }
        }
        if (!player.isCreative() && !player.isSpectator()) {
            auto& t        = flaskTracking(player);
            auto [n, secs] = t.track(potion->javaId, gameTime());
            saveTracking(player, t);
            if (player.isAlive()) gf::progression::trigger(player, "twilightforest:drink_from_flask", Json{{"doses", n}, {"seconds", secs}, {"potion", potion->javaId}});
        }
    }
    if (creative(player)) return;
    ItemStack  spare;
    ItemStack* one  = splitOne(player, stack, spare);
    auto       next = f.removeDose();
    bool       gone = false;
    if (next.breakable) {
        if (next.breakage >= ca::kFlaskDoses) {
            gone = true;
            playSound(player, "item.twilightforest.flask.break", 1.5F, 0.7F);
        } else {
            playSound(player, "item.twilightforest.flask.crack", 1.5F, 2.0F);
        }
    }
    if (gone) {
        one->remove(1);
        if (items::countOf(*one) <= 0) *one = items::empty();
    } else {
        setFlask(*one, next);
    }
    if (one == &spare) giveOrDrop(player, spare);
    log().info("artifacts: {} drank from a flask ({} dose(s) left{})", player.getRealName(), next.doses, gone ? ", it broke" : "");
}

void registerFlasks() {
    for (char const* id : {kBrittleFlask, kGreaterFlask}) {
        gf::item::registerItem(id, {
            .use = [](Player& player, ItemStack& stack) {
                // Bedrock's filling: the potion in the other hand
                bool        inMain = &stack == &mainHand(player) || items::idOf(mainHand(player)) == items::idOf(stack);
                ItemStack   other  = inMain ? offHand(player) : mainHand(player);
                if (isPotion(items::idOf(other))) {
                    ItemStack flask = stack;
                    if (fillFlask(player, flask, other, true)) {
                        stack = flask;
                        if (inMain) player.setOffhandSlot(other);
                        else player.setSelectedItem(other);
                        player.sendInventory(false);
                        return gf::item::Result::Consume;
                    }
                }
                auto f = flaskOf(stack);
                if (f.empty() || f.doses <= 0) return gf::item::Result::Fail;
                return gf::item::Result::Pass; // ItemUtils#startUsingInstantly: the 32 drink ticks
            },
            .finishUsing = [](Player& player, ItemStack& stack) { drinkFlask(player, stack); },
        });
        gf::item::addTooltip(id, flaskTooltip);
        gf::item::setBar(id, [](ItemStack const& stack) -> std::optional<gf::item::Bar> {
            auto f = flaskOf(stack);
            if (f.empty()) return std::nullopt;
            return gf::item::Bar{static_cast<float>(f.barWidth()) / 13.0F, ca::potionColor(f.potion)};
        });
    }
    // using a potion with a flask in the other hand fills the flask instead of drinking
    for (char const* potion : {"minecraft:potion", "minecraft:splash_potion", "minecraft:lingering_potion"}) {
        ia::onUse({.item = potion}, [](ia::UseContext const& c) {
            bool       inMain = items::idOf(mainHand(c.player)) == c.itemId;
            ItemStack  flask  = inMain ? offHand(c.player) : mainHand(c.player);
            if (!isFlask(items::idOf(flask))) return ia::Result::Pass;
            ItemStack pot = c.item;
            if (!fillFlask(c.player, flask, pot, false)) return ia::Result::Pass;
            if (inMain) {
                c.player.setSelectedItem(pot);
                c.player.setOffhandSlot(flask);
            } else {
                c.player.setOffhandSlot(pot);
                c.player.setSelectedItem(flask);
            }
            c.player.sendInventory(false);
            return ia::Result::Consume;
        }, ia::priority::High);
    }
    // EntityEvents#resetFlaskLogic, handed over by U14 (docs/units/progression.md, "Flask doses")
    gf::progression::onTrigger("twilightforest:flask_doses_reset", [](Player& player, Json const&) {
        auto& t = flaskTracking(player);
        t.reset();
        saveTracking(player, t);
    });
    game::db::onWorldClosed([] { trackings().clear(); });
}

} // namespace gf::feature::artifacts
