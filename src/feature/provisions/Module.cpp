// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/init/TFItems.java
//   src/main/java/gloamingforest/item/HydraChopItem.java
//   src/main/java/gloamingforest/item/Experiment115Item.java
//   src/main/java/gloamingforest/item/CustomDamageSwordItem.java
//   src/main/java/gloamingforest/init/TFJukeboxSongs.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U10 (provisions), server module:
// - foods: the BP items carry nutrition, saturation, always-edible and the bowl (minecraft:food);
//   the effects of upstream's FoodProperties (hydra chop: Regeneration 100 t; meef stroganoff: Fire
//   Resistance 3600 t and Strength 2400 t) are given in finishUsing, where HydraChopItem fires
//   consume_hydra_chop_on_low_hunger at food level 0 and Experiment115Item awards
//   e115_slices_eaten; rarities through F9's ext::setRarity;
// - stale bread (CustomDamageSwordItem): its melee hits deal damage type
//   gloamingforest:stale_sandwich (the player attack is replaced by gf::combat::hurt of that type);
// - music discs: BP minecraft:record (length, comparator 15) and the JukeboxSong sound event
//   through F9's ext::setRecordSound;
// - the frosted effect (Frosted.cpp), the four enchantments (Enchantments.cpp) and Experiment 115
//   (Experiment115.cpp).
#include <string>
#include <string_view>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "core/provisions/Provisions.h"
#include "feature/combat/Combat.h"
#include "feature/effects/Effects.h"
#include "feature/ext/item/Item.h"
#include "feature/ext/item/Records.h"
#include "feature/item/Item.h"
#include "feature/progression/Progression.h"
#include "feature/provisions/Internal.h"
#include "feature/stats/Stats.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::provisions {

void installFrosted();
void installEnchantments();
void installE115();

namespace cp = core::provisions;

namespace {

AttributeInstance* hungerOf(Player const& p) {
    auto ref = p.getAttribute(Player::HUNGER());
    return const_cast<AttributeInstance*>(static_cast<AttributeInstance const*>(ref.mPtr));
}

/// FoodProperties' effects (probability 1.0 in every upstream food) and the items' own finishUsingItem.
void registerFoods() {
    for (auto const& f : cp::foods()) {
        std::string id(f.id);
        cp::Food    food = f;
        ::gf::item::registerItem(id, {.finishUsing = [id, food](Player& player, ItemStack&) {
            // HydraChopItem: the trigger before super (the food level as it was before eating)
            if (id == ids::kHydraChop && foodLevel(player) <= 0) {
                ::gf::progression::trigger(player, ids::kHydraTrigger);
                log().info("provisions: {} ate a hydra chop on an empty stomach", player.getRealName());
            }
            if (id == ids::kE115) ::gf::stats::award(player, ids::kSlicesEaten);
            for (auto const& e : food.effects)
                if (!e.effect.empty()) ::gf::effects::apply(player, std::string(e.effect), e.ticks, e.amplifier);
        }});
    }
    ::gf::item::ext::setRarity(ids::kHydraChop, ::gf::item::ext::Rarity::Uncommon);
    ::gf::item::ext::setRarity(ids::kStroganoff, ::gf::item::ext::Rarity::Uncommon);
}

/// upstream's disc credits (jukebox_song.gloamingforest.<key>), the English fallbacks of upstream's en_us.
std::string songCredit(std::string_view key) {
    if (key == "radiance" || key == "steps" || key == "superstitious") return "Rotch Gwylt - " + std::string(key == "radiance" ? "Radiance" : key == "steps" ? "Steps" : "Superstitious");
    std::string name(key == "warfarer" ? "wayfarer" : key);
    name[0] = static_cast<char>(name[0] - 'a' + 'A');
    return "MrCompost - " + name;
}

void registerDiscs() {
    for (auto const& s : cp::songs()) {
        auto disc = "gloamingforest:music_disc_" + std::string(s.track);
        ::gf::item::ext::setRecordSound(disc, "music_disc.gloamingforest." + std::string(s.track));
        ::gf::item::ext::setRarity(disc, ::gf::item::ext::Rarity::Rare);
        // JukeboxPlayable's tooltip: the song's description in gray
        auto key      = "jukebox_song.gloamingforest." + std::string(s.key);
        auto fallback = songCredit(s.key);
        ::gf::item::addTooltip(disc, [key, fallback](ItemStack const&, std::vector<std::string>& lines, Player const* viewer) {
            lines.push_back("§7" + (viewer ? ::gf::trFor(*viewer, key, fallback) : ::gf::tr(key, fallback)));
        });
    }
}

/// CustomDamageSwordItem#hurtEnemy... upstream's sword deals its melee damage as `damageType` (Java's
/// getDamageSource override): the player's attack becomes a hit of that type.
void registerStaleBread() {
    ::gf::combat::onIncoming(::gf::combat::priority::Lowest, [](::gf::combat::DamageInfo& d) {
        if (d.type != "minecraft:player_attack" || !d.attacker || d.attacker != d.direct || !d.attacker->isPlayer()) return ::gf::combat::Verdict::Pass;
        auto& attacker = static_cast<Player&>(*d.attacker);
        if (game::items::idOf(attacker.getSelectedItem()) != ids::kStaleBread) return ::gf::combat::Verdict::Pass;
        if (::gf::combat::hurt(d.victim, d.amount, ids::kStaleSandwich, &attacker, &attacker)) ++counters().staleSandwich;
        return ::gf::combat::Verdict::Cancel;
    });
}

void levelTick() {
    auto level = ll::service::getLevel();
    if (!level) return;
    level->forEachPlayer([](Player& p) {
        if (!p.isClientSide()) renewalTick(p);
        return true;
    });
    thawFrozen();
    syncFrosted();
}

class ProvisionsModule {
public:
    void install() {
        static bool once = false;
        if (!once) {
            once = true;
            registerFoods();
            registerDiscs();
            registerStaleBread();
            installFrosted();
            installEnchantments();
            installE115();
            log().info("provisions: {} foods, {} music discs, frosted, 4 enchantments and experiment 115 registered", cp::foods().size(), cp::songs().size());
        }
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { levelTick(); });
    }
    void uninstall() { ll::event::EventBus::getInstance().removeListener(mTick); }

private:
    ll::event::ListenerPtr mTick;
};

} // namespace

float foodLevel(Player const& player) {
    auto* h = hungerOf(player);
    return h ? h->mCurrentValue : 20.0f;
}

void setFoodLevel(Player& player, float value) {
    if (auto* h = hungerOf(player)) h->mCurrentValue = value;
}

Counters& counters() {
    static Counters c;
    return c;
}

} // namespace gf::feature::provisions

GF_REGISTER_MODULE(gf::feature::provisions::ProvisionsModule);
