// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/events/ToolEvents.java
//   src/main/java/twilightforest/events/EntityEvents.java
//   src/main/java/twilightforest/enchantment/ApplyFrostedEffect.java
//   src/main/java/twilightforest/item/FierySwordItem.java
//   src/main/java/twilightforest/item/FieryPickItem.java
//   src/main/java/twilightforest/item/FieryArmorItem.java
//   src/main/java/twilightforest/item/KnightmetalSwordItem.java
//   src/main/java/twilightforest/item/KnightmetalPickItem.java
//   src/main/java/twilightforest/item/KnightmetalAxeItem.java
//   src/main/java/twilightforest/item/MinotaurAxeItem.java
//   src/main/java/twilightforest/item/MazebreakerPickItem.java
//   src/main/java/twilightforest/item/ArcticArmorItem.java
//   src/main/java/twilightforest/item/PhantomArmorItem.java
//   src/main/java/twilightforest/item/YetiArmorItem.java
//   src/main/java/twilightforest/loot/modifiers/FieryToolSmeltingModifier.java
//   src/main/java/twilightforest/item/recipe/NoTemplateSmithingRecipe.java
//   src/main/java/twilightforest/data/CraftingGenerator.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U7 (armory): TF's armour and tool tiers. The BP items (assets/pack/bp/items/armory) carry the
// numbers Bedrock reads (protection, durability, enchantability, repair items, attack damage, mining
// speeds and tiers, rarity, fire resistance, the arctic dye); this module adds the behaviour:
// - ToolEvents: the fiery tools set what they hurt on fire (1 s for any damage dealt holding one,
//   15 s on a hit: FierySwordItem / FieryPickItem#hurtEnemy), knightmetal's bonus against armoured
//   (sword, pickaxe) and unarmoured (axe) targets, the minotaur axes' +7 while sprinting (each with
//   the enchanted-hit sparkles, AnimatePacket 5), the mazebreaker's x16 speed on
//   #mazebreaker_accelerated_mining and the 16 extra wear of any other tool there;
// - EntityEvents#entityHurts: fiery armour sets the attacker on fire (rand(25) < 5f, 5f/2 s), yeti
//   armour frosts it (5y + 5 ticks, amplifier y; ApplyFrostedEffect#doChillAuraEffect's immunities);
// - armour toughness (ext/armory, Mob::getToughnessValue), powder snow walking for the arctic and
//   yeti boots and freeze immunity for #freeze_immune_wearables (the entity flags leather sets);
// - the global loot modifier twilightforest:fiery_pick_smelting (FieryToolSmeltingModifier);
// - the default enchantments of 24 crafted results (CraftingGenerator's result components) as
//   special crafting of their BP recipes;
// - no-template smithing (twilightforest:no_template_smithing, R4): Bedrock's smithing table needs a
//   template, so the emperor's cloth recipe is crafted in the crafting table (base + addition,
//   assets/pack/bp/recipes/armory) with NoTemplateSmithingRecipe's match and result;
// - tooltips (gray descriptions), rarities, the knightmetal armour's equip sound.
// Phantom armour is kept on death by F10's #twilightforest:kept_on_death keeper (U9's tag).
#include "feature/armory/Armory.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <mutex>
#include <random>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/entity/components/CanStandOnPowderSnowComponent.h"
#include "mc/entity/components/FreezingImmuneComponent.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/AnimatePacket.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ExperienceOrb.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"
#include "mc/world/level/dimension/Dimension.h"

#include "core/armory/Armory.h"
#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/effects/Effects.h"
#include "feature/enchant/Enchant.h"
#include "feature/ext/armory/ArmorHooks.h"
#include "feature/ext/item/Item.h"
#include "feature/ext/item/Recipes.h"
#include "feature/ext/provisions/Provisions.h"
#include "feature/fx/Fx.h"
#include "feature/item/Item.h"
#include "feature/loot/Loot.h"
#include "feature/recipes/Recipes.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::armory {

namespace {

namespace ca    = gf::core::armory;
namespace ia    = gf::feature::common::interactions;
namespace items = gf::game::items;
using Slot      = SharedTypes::Legacy::ArmorSlot;
using Json      = nlohmann::json;

std::string idOf(ItemStack const& s) { return items::isEmpty(s) ? std::string() : items::idOf(s); }

Mob* asMob(Actor& actor) { return actor.hasCategory(ActorCategory::Mob) ? static_cast<Mob*>(&actor) : nullptr; }

int wornPieces(Actor const& a) {
    int n = 0;
    for (int i = 0; i < 4; ++i) n += items::isEmpty(a.getArmor(static_cast<Slot>(i))) ? 0 : 1;
    return n;
}

std::string gray(Player const* viewer, std::string_view key, std::string_view fallback) {
    return "§7" + std::string(translate(key, fallback, viewer ? localeOf(*viewer) : std::string()));
}

ca::Rarity rarityOfItem(std::string_view id) {
    if (auto const* p = ca::armorPiece(id))
        if (auto const* m = ca::armorMaterial(p->material)) return m->rarity;
    if (auto const* t = ca::tool(id)) return t->rarity;
    for (auto const& m : ca::materialItems())
        if (m.id == id) return m.rarity;
    return ca::Rarity::Common;
}

gf::item::ext::Rarity toExt(ca::Rarity r) {
    switch (r) {
    case ca::Rarity::Uncommon: return gf::item::ext::Rarity::Uncommon;
    case ca::Rarity::Rare: return gf::item::ext::Rarity::Rare;
    case ca::Rarity::Epic: return gf::item::ext::Rarity::Epic;
    default: return gf::item::ext::Rarity::Common;
    }
}

// ---- ToolEvents --------------------------------------------------------------------------------------

/// ClientboundAnimatePacket(target, 5) through broadcastAndSend: the players near the target.
void sparkles(Actor& target) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::Animate);
    if (!packet) return;
    auto& pkt      = static_cast<AnimatePacket&>(*packet);
    pkt.mRuntimeId = target.getRuntimeID();
    pkt.mAction    = AnimatePacketPayload::Action::MagicCriticalHit;
    pkt.mData      = 0.f;
    auto dim       = target.getDimensionId();
    auto pos       = target.getPosition();
    level->forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
        if (p.getDimensionId() == dim && p.distanceToSqr(pos) < 64.f * 64.f) pkt.sendTo(p);
        return true;
    }));
}

ca::KnightmetalWeapon knightmetalWeapon(std::string const& id) {
    if (id == ids::kKnightmetalSword || id == ids::kKnightmetalPickaxe) return ca::KnightmetalWeapon::SwordOrPickaxe;
    if (id == ids::kKnightmetalAxe) return ca::KnightmetalWeapon::Axe;
    return ca::KnightmetalWeapon::None;
}

bool isFieryTool(std::string const& id) { return id == ids::kFierySword || id == ids::kFieryPickaxe; }
bool isMinotaurAxe(std::string const& id) { return id == ids::kGoldMinotaurAxe || id == ids::kDiamondMinotaurAxe; }

combat::Verdict onIncoming(combat::DamageInfo& d) {
    // fieryToolSetFire: the causing entity holds a fiery sword / pickaxe in its main hand
    if (d.attacker && asMob(*d.attacker) && isFieryTool(idOf(d.attacker->getCarriedItem())) && !d.victim.isFireImmune()
        && !d.victim.isOnFire()) // igniteForTicks never shortens a longer fire
        d.victim.setOnFire(ca::kFieryToolSeconds, true);
    // doKnightmetalToolLogic, addExtraAxeChargingDamage: the direct entity's main hand
    if (d.direct && asMob(*d.direct)) {
        auto  weapon   = idOf(d.direct->getCarriedItem());
        float original = d.amount;
        if (auto w = knightmetalWeapon(weapon); w != ca::KnightmetalWeapon::None) {
            auto* victim = asMob(d.victim);
            int   armor  = victim ? victim->getArmorValue() : 0;
            auto  b      = ca::knightmetal(w, armor, ca::armorCover(wornPieces(d.victim)), d.amount, original);
            if (b.sparkles) {
                d.amount = b.damage;
                sparkles(d.victim);
            }
        }
        if (isMinotaurAxe(weapon)) {
            auto b = ca::minotaurAxe(d.direct->isSprinting(), d.amount);
            if (b.sparkles) {
                d.amount = b.damage;
                sparkles(d.victim);
            }
        }
    }
    return combat::Verdict::Pass;
}

// ---- EntityEvents#entityHurts ------------------------------------------------------------------------

struct State {
    std::mutex                               mutex;
    std::optional<int>                       forcedRoll;
    std::mt19937                             rng{std::random_device{}()};
    std::unordered_map<std::int64_t, int>    snowFlags; ///< uid -> bits this module added (1 stand, 2 freeze)
};

State& state() {
    static State s;
    return s;
}

int fireRoll() {
    std::lock_guard lock(state().mutex);
    if (state().forcedRoll) return *state().forcedRoll;
    return std::uniform_int_distribution<int>(0, 24)(state().rng);
}

bool wearsFreezeImmune(Actor const& a) {
    for (int i = 0; i < 4; ++i) {
        auto const& s = a.getArmor(static_cast<Slot>(i));
        if (!items::isEmpty(s) && gf::tags::has(gf::tags::Registry::Item, "minecraft:freeze_immune_wearables", items::idOf(s))) return true;
    }
    return false;
}

/// ApplyFrostedEffect#doChillAuraEffect: U10's frosted helper (its immunities: freeze immune
/// entity types, #freeze_immune_wearables worn, creative players).
void chill(Mob& victim, int duration, int amplifier, bool shouldHit) { (void)gf::provisions::ext::doChillAuraEffect(victim, duration, amplifier, shouldHit); }

void onDealt(combat::DamageInfo const& d) {
    // Java: getOriginalDamage() > 0; the handlers only hear about damage that applied (the health
    // Bedrock keeps as an integer may not have dropped for a small hit)
    if (!d.attacker) return;
    int fiery = countPieces(d.victim, "fiery");
    int yeti  = countPieces(d.victim, "yeti");
    if (fiery > 0) {
        auto r = ca::fireReact(fiery, fireRoll());
        if (r.ignite && !d.attacker->isFireImmune()) d.attacker->setOnFire(r.seconds, true);
    }
    if (auto* target = asMob(*d.attacker)) {
        auto c = ca::chillAura(yeti);
        chill(*target, c.duration, c.amplifier, c.apply);
    }
}

// ---- powder snow, freeze immunity -----------------------------------------------------------------

/// Item#canWalkOnPowderedSnow (the arctic and yeti boots) and #minecraft:freeze_immune_wearables:
/// Bedrock reads both from entity flags (leather armour sets their equipment twins), so the wearer
/// gets them while it wears the pieces and loses the ones this module added when it takes them off.
void applySnowFlags(Actor& wearer) {
    auto feet   = idOf(wearer.getArmor(Slot::Feet));
    bool snow   = feet == ids::kArcticBoots || feet == ids::kYetiBoots;
    bool freeze = wearsFreezeImmune(wearer);
    auto& ec  = wearer.getEntityContext();
    auto  uid = wearer.getOrCreateUniqueID().rawID;
    int   had = 0;
    {
        std::lock_guard lock(state().mutex);
        if (auto it = state().snowFlags.find(uid); it != state().snowFlags.end()) had = it->second;
    }
    int now = had;
    if (snow && !ec.hasComponent<CanStandOnPowderSnowComponent>()) {
        ec.getRegistry().emplace_or_replace<CanStandOnPowderSnowComponent>(ec.mEntity);
        now |= 1;
    } else if (!snow && (had & 1)) {
        ec.removeComponent<CanStandOnPowderSnowComponent>();
        now &= ~1;
    }
    if (freeze && !ec.hasComponent<FreezingImmuneComponent>()) {
        ec.getRegistry().emplace_or_replace<FreezingImmuneComponent>(ec.mEntity);
        now |= 2;
    } else if (!freeze && (had & 2)) {
        ec.removeComponent<FreezingImmuneComponent>();
        now &= ~2;
    }
    if (now == had) return;
    std::lock_guard lock(state().mutex);
    if (now) state().snowFlags[uid] = now;
    else state().snowFlags.erase(uid);
}

void refreshSnowFlagsNextTick(Player& player) {
    auto uid = player.getOrCreateUniqueID();
    ia::nextTick([uid] {
        auto level = ll::service::getLevel();
        if (!level) return;
        if (auto* a = level->fetchEntity(uid, false)) applySnowFlags(*a);
    });
}

// ---- FieryToolSmeltingModifier -----------------------------------------------------------------------

bool ingredientMatches(recipes::Ingredient const& in, std::string const& id) {
    if (std::find(in.items.begin(), in.items.end(), id) != in.items.end()) return true;
    if (!in.tag.empty()) {
        std::string tag = in.tag.starts_with('#') ? in.tag.substr(1) : in.tag;
        return gf::tags::has(gf::tags::Registry::Item, tag, id);
    }
    return false;
}

/// RecipeManager#getRecipeFor(SMELTING, stack): the data's smelting recipes (TF's), then the game's
/// furnace recipes (vanilla's); the result (count x result count) and the recipe's XP.
std::optional<std::pair<ItemStack, float>> smelt(ItemStack const& stack) {
    auto id = items::idOf(stack);
    int  n  = items::countOf(stack);
    for (auto const* r : recipes::ofType("minecraft:smelting")) {
        if (!r || r->ingredients.empty() || r->resultItem.empty() || !ingredientMatches(r->ingredients.front(), id)) continue;
        auto out = items::make(r->resultItem, n * std::max(1, r->resultCount));
        if (!out) continue;
        float xp = 0.f;
        if (auto it = r->data.find("experience"); it != r->data.end() && it->is_number()) xp = it->get<float>();
        return std::pair{*out, xp};
    }
    auto level = ll::service::getLevel();
    if (!level) return std::nullopt;
    auto result = level->getRecipes().getFurnaceRecipeResult(stack, HashedString("furnace"));
    if (result.isNull()) return std::nullopt;
    auto out = items::make(result.getTypeName(), n * std::max<int>(1, result.mCount), result.getAuxValue());
    if (!out) return std::nullopt;
    // Bedrock keeps a smelting's XP on the items (Item#getFurnaceXPmultiplier)
    float xp = std::max(FurnaceBlockActor::_getXPRewardMultiplier(stack), FurnaceBlockActor::_getXPRewardMultiplier(*out));
    return std::pair{*out, xp};
}

void smeltingModifier(std::vector<ItemStack>& loot, loot::LootContext const& ctx, std::string const&) {
    // condition: match_tool twilightforest:fiery_pickaxe
    if (!ctx.tool || idOf(*ctx.tool) != ids::kFieryPickaxe) return;
    float xp = smeltDrops(loot);
    if (xp > 0.f && ctx.thisEntity && ctx.region) {
        int amount = ca::smeltingXp(xp);
        if (amount > 0)
            ExperienceOrb::spawnOrbs(*ctx.region, ctx.thisEntity->getPosition(), amount, ExperienceOrb::DropType::FromBlock, ctx.player);
    }
}

// ---- registration -----------------------------------------------------------------------------------

void registerItems() {
    using B = gf::item::Behaviour;
    // FierySwordItem / FieryPickItem#hurtEnemy: 15 s of fire for a target that is not fire immune
    // (their client-side flame particles never show on a server: Java's hurtEnemy runs server side)
    auto fieryHit = [](ItemStack&, Actor& target, Actor&) {
        if (!target.isFireImmune()) target.setOnFire(ca::kFieryHitSeconds, true);
        return true;
    };
    gf::item::registerItem(ids::kFierySword, B{.hurtEnemy = fieryHit});
    gf::item::registerItem(ids::kFieryPickaxe, B{.hurtEnemy = fieryHit});
    // MazebreakerPickItem: x16 on the accelerated blocks, never repaired
    gf::item::registerItem(ids::kMazebreaker, B{
        .destroySpeed = [](ItemStack const&, Block const& block) -> std::optional<float> {
            if (!gf::tags::has(gf::tags::Registry::Block, ids::kMazebreakerTag, game::blockId(block))) return std::nullopt;
            return ca::toolTier("diamond")->speed * static_cast<float>(ca::kMazebreakerSpeedFactor);
        },
        .repairWith = [](ItemStack const&, ItemStack const&) { return false; },
    });
    // the half point of the shovels' attack damage (Bedrock's damage is an integer)
    for (auto const& t : ca::tools()) {
        float frac = ca::fractionalDamage(t);
        if (frac <= 0.f) continue;
        gf::item::registerItem(std::string(t.id), B{.attackBonus = [frac](ItemStack const&, Actor&, Player&) { return frac; }});
    }
    // armour: the snow / freeze flags while worn, the knightmetal equip sound
    for (auto const& p : ca::armorPieces()) {
        auto const* m = ca::armorMaterial(p.material);
        bool snowy = p.material == "fiery" || p.material == "arctic" || p.material == "yeti";
        B    b;
        if (snowy) {
            b.armorTick = [](ItemStack&, Player& player, int) { applySnowFlags(player); };
            b.onUnequip = [](Player& player, ItemStack const&, int) { refreshSnowFlagsNextTick(player); };
        }
        if (m && !m->equipSound.empty()) {
            std::string sound(m->equipSound);
            b.onEquip = [sound](Player& player, ItemStack const&, int) { gf::fx::sound(player.getDimension(), sound, player.getPosition(), 1.0f, 1.0f); };
        }
        if (b.armorTick || b.onEquip) gf::item::registerItem(std::string(p.id), std::move(b));
    }
    // rarities (the client mod's name colours; the BP's minecraft:rarity for vanilla clients)
    auto rarity = [](std::string_view id) {
        auto r = rarityOfItem(id);
        if (r != ca::Rarity::Common) gf::item::ext::setRarity(std::string(id), toExt(r));
    };
    for (auto const& p : ca::armorPieces()) rarity(p.id);
    for (auto const& t : ca::tools()) rarity(t.id);
    for (auto const& m : ca::materialItems()) rarity(m.id);
}

void registerTooltips() {
    auto line = [](std::string key, std::string fallback) {
        return [key, fallback](ItemStack const&, std::vector<std::string>& lines, Player const* viewer) { lines.push_back(gray(viewer, key, fallback)); };
    };
    gf::item::addTooltip(ids::kFierySword, line("item.twilightforest.fiery_sword.desc", "Burns targets"));
    gf::item::addTooltip(ids::kFieryPickaxe, line("item.twilightforest.fiery_pickaxe.desc", "Auto-smelting"));
    gf::item::addTooltip(ids::kKnightmetalSword, line("item.twilightforest.knightmetal_sword.desc", "Extra damage to armored targets"));
    gf::item::addTooltip(ids::kKnightmetalPickaxe, line("item.twilightforest.knightmetal_pickaxe.desc", "Extra damage to armored targets"));
    gf::item::addTooltip(ids::kKnightmetalAxe, line("item.twilightforest.knightmetal_axe.desc", "Extra damage to unarmored targets"));
    for (auto const* id : {ids::kGoldMinotaurAxe, ids::kDiamondMinotaurAxe})
        gf::item::addTooltip(id, line("item.twilightforest.minotaur_axe.desc", "Extra damage while charging"));
    for (auto const& p : ca::armorPieces()) {
        std::string id(p.id);
        if (p.material == "arctic") gf::item::addTooltip(id, line("item.twilightforest.arctic_armor.desc", "Dyeable"));
        else if (p.material == "fiery") gf::item::addTooltip(id, line("item.twilightforest.fiery_armor.desc", "Burns attackers"));
        else if (p.material == "yeti") gf::item::addTooltip(id, line("item.twilightforest.yeti_armor.desc", "Chills attackers"));
        else if (p.material == "phantom") gf::item::addTooltip(id, line("item.twilightforest.phantom_armor.desc", "Is never lost on death"));
    }
}

void registerRecipes() {
    // CraftingGenerator: the crafted result carries minecraft:enchantments
    for (auto const& d : ca::defaultEnchantments()) {
        std::string item(d.item);
        auto        enchantments = d.enchantments;
        recipes::registerSpecialCrafting(
            std::string(d.recipe),
            [](recipes::CraftingGrid const&) { return true; },
            [item, enchantments](recipes::CraftingGrid const&) {
                auto out = items::make(item, 1);
                if (!out) return ItemStack::EMPTY_ITEM();
                for (auto const& [id, lvl] : enchantments) gf::enchant::set(*out, std::string(id), lvl);
                return *out;
            }
        );
    }
    // No crafting-table copy of twilightforest:emperors_cloth_smithing: Bedrock's smithing table needs a
    // template, and TF's own crafting recipe twilightforest:emperors_cloth_recipe (U9) already puts the
    // cloth on any applicable piece with the same result (a copy of the base with
    // twilightforest:emperors_cloth); a second crafting recipe of the same layout was a duplicate
    // (content-log warning). noTemplateSmithing keeps NoTemplateSmithingRecipe's match and result.
}

class ArmoryModule {
public:
    void install() {
        gf::ext::armory::setToughness([](ItemStack const& worn) {
            auto const* p = ca::armorPiece(items::idOf(worn));
            return p ? ca::toughnessOf(*p) : 0.f;
        });
        gf::ext::armory::installHooks();
        static bool once = false;
        if (once) return;
        once = true;
        registerItems();
        registerTooltips();
        registerRecipes();
        combat::onIncoming(combat::priority::Normal, &onIncoming);
        combat::onDealt(combat::priority::Normal, &onDealt);
        loot::addGlobalModifier(ids::kSmeltingModifier, &smeltingModifier);
        // the flags this module added belong to the world's entities
        game::db::onWorldClosed([] {
            std::lock_guard lock(state().mutex);
            state().snowFlags.clear();
        });
        // ToolEvents#damageNonMazebreakerToolsMore is U23's (labyrinth): the owner of the accelerated tag registers it
        gf::log().info("armory: {} armour pieces, {} tools, {} crafted enchantments installed", ca::armorPieces().size(), ca::tools().size(),
                   ca::defaultEnchantments().size());
    }
    void uninstall() { gf::ext::armory::uninstallHooks(); }
};

} // namespace

int countPieces(Actor const& wearer, std::string_view material) {
    int n = 0;
    for (int i = 0; i < 4; ++i) {
        auto const& s = wearer.getArmor(static_cast<Slot>(i));
        if (items::isEmpty(s)) continue;
        auto const* p = ca::armorPiece(items::idOf(s));
        if (p && p->material == material) ++n;
    }
    return n;
}

float smeltDrops(std::vector<ItemStack>& drops) {
    float xp = 0.f;
    for (auto& s : drops) {
        if (items::isEmpty(s)) continue;
        if (auto r = smelt(s)) {
            s = r->first;
            xp += r->second;
        }
    }
    return xp;
}

void setFireReactRollForTest(std::optional<int> roll) {
    std::lock_guard lock(state().mutex);
    state().forcedRoll = roll;
}

std::optional<ItemStack> noTemplateSmithing(ItemStack const& base, ItemStack const& addition) {
    if (items::isEmpty(base) || items::isEmpty(addition)) return std::nullopt;
    auto baseId = items::idOf(base), addId = items::idOf(addition);
    for (auto const* r : recipes::ofType(ids::kNoTemplateSmithing)) {
        if (!r || r->ingredients.size() < 2) continue;
        if (!ingredientMatches(r->ingredients[0], baseId) || !ingredientMatches(r->ingredients[1], addId)) continue;
        auto data = r->data.value("additional_data", Json::object());
        if (!data.is_object()) continue;
        // refused when the base already has any of the components
        bool has = false;
        for (auto const& [key, value] : data.items()) has = has || !gf::item::getData(base, key).is_null();
        if (has) continue;
        ItemStack out = items::withCount(base, 1);
        for (auto const& [key, value] : data.items()) gf::item::setData(out, key, value);
        return out;
    }
    return std::nullopt;
}

} // namespace gf::feature::armory

GF_REGISTER_MODULE(gf::feature::armory::ArmoryModule);
