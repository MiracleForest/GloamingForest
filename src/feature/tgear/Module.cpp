// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/events/TravellersGearEvents.java
//   src/main/java/twilightforest/item/travellers_gear/TravellersGearLogic.java
//   src/main/java/twilightforest/item/travellers_gear/TravellersArmorItem.java
//   src/main/java/twilightforest/item/travellers_gear/TravellersGogglesItem.java
//   src/main/java/twilightforest/network/GogglesZoomPacket.java
//   src/main/java/twilightforest/network/SwapHotbarPacket.java
//   src/main/java/twilightforest/network/CycleMapSlotPacket.java
//   src/main/java/twilightforest/block/RedThreadBlock.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U11's server module: the per-tick modifiers of every wearer (haste, auto repair; stealth, the item
// display's map and efficient eater for players), arrow magnetism, perfect dodge (combat; the projectile
// flies on), the never-break rule and the last armour damage time (ext/tgear armour hook), the broken
// state (" (Broken)" name, no armour points), the all-night goggles (no phantoms, ender mask), the
// piglin-neutral goggles / wings, powder snow and freezing of the boots / vest, the companion channels
// tgear.goggles_zoom / tgear.swap_hotbar / tgear.cycle_map_slot / tgear.thread_vision, the vanilla
// fallbacks (sneak-use the goggles: item display form; sneak-use a belt / wings: hotbar swap), the
// add_modifier trigger of crafted gear and the red thread's MultifaceBlock rules.
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <random>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/event/world/SpawnMobEvent.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/entity/components/CanStandOnPowderSnowComponent.h"
#include "mc/entity/components/ExhaustionComponent.h"
#include "mc/entity/components/FreezingImmuneComponent.h"
#include "mc/entity/components_json_legacy/InsomniaComponent.h"
#include "mc/entity/components_json_legacy/ProjectileComponent.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/actor/projectile/AbstractArrow.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include "core/tgear/Logic.h"
#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/dimension/World.h"
#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/entity/Handles.h"
#include "feature/ext/tgear/CraftHooks.h"
#include "feature/ext/tgear/PlayerHooks.h"
#include "feature/families/BlockEvents.h"
#include "feature/fx/Fx.h"
#include "feature/net/Net.h"
#include "feature/progression/Progression.h"
#include "feature/tgear/Gear.h"
#include "feature/tgear/Internal.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Commands.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::tgear {

namespace {

namespace items = game::items;
namespace ia    = interactions;
using Slot      = SharedTypes::Legacy::ArmorSlot;

constexpr unsigned kHasteEffect        = 3;  // minecraft:haste
constexpr unsigned kInvisibilityEffect = 14; // minecraft:invisibility
constexpr char     kVisionKey[]        = "gf:tgear:red_thread_vision"; ///< {player uuid: bool}
constexpr char     kBrokenNameKey[]    = "tgear.broken_name";          ///< the custom name we set

struct State {
    std::mutex                                             mutex;
    std::mt19937_64                                        rng{std::random_device{}()};
    std::unordered_map<std::int64_t, std::int64_t>         lastArmorDamage; ///< wearer uid -> game time
    std::unordered_map<std::int64_t, std::array<float, 5>> exhaustionBase;  ///< walk, sprint, swim, jump, sprint jump
    std::unordered_map<std::int64_t, bool>                 zoom;            ///< player uid -> zooming
    std::unordered_map<std::int64_t, int>                  standFlags;      ///< wearer uid -> flags we added (1 snow, 2 freeze)
    struct Arrow {
        std::int64_t firstTick = 0;
        Vec3         last{};
        bool         flying = false; ///< seen moving while tracked
    };
    std::unordered_map<std::int64_t, Arrow>  arrows;  ///< arrows of wearers, by unique id
    std::map<std::string, bool>              vision;  ///< player uuid -> red thread vision (default on)
    std::vector<std::pair<std::pair<std::int64_t, std::int64_t>, std::int64_t>> dodgedImpacts; ///< (victim, projectile), tick
    std::optional<float>                     dodgeOverride;
    int                                      dodges = 0;
};

State& state() {
    static State s;
    return s;
}

float nextFloat() {
    std::lock_guard lock(state().mutex);
    return std::uniform_real_distribution<float>(0.f, 1.f)(state().rng);
}

double nextDouble() {
    std::lock_guard lock(state().mutex);
    return std::uniform_real_distribution<double>(0.0, 1.0)(state().rng);
}

std::int64_t uidOf(Actor const& a) { return a.getOrCreateUniqueID().rawID; }

std::int64_t gameTime() {
    auto level = ll::service::getLevel();
    return level ? static_cast<std::int64_t>(level->getCurrentTick().tickID) : 0;
}

/// LivingEntity#getVoicePitch: (r1 - r2) * 0.2 + 1.
float voicePitch() { return (nextFloat() - nextFloat()) * 0.2f + 1.f; }

/// Entity#position / blockPosition: the feet (Bedrock's getPosition is the eyes of a player).
Vec3 feetOf(Actor const& a) { return a.getFeetPos(); }

/// MobEffectInstance(effect, 2, amplifier, false, false, false): hidden.
void hiddenEffect(Actor& wearer, unsigned id, int amplifier) {
    MobEffectInstance inst(id, EffectDuration{2});
    inst.mAmplifier                       = amplifier;
    inst.mAmbient                         = false;
    inst.mEffectVisible                   = false;
    inst.mDisplayOnScreenTextureAnimation = false;
    wearer.addEffect(inst);
}

/// Level#isDay for the auto repair boost: !fixed time and skyDarken < 4 (Java's formula over the
/// dimension's time of day; the weather terms are left out).
bool isDay(Dimension& dim, int time) {
    float  t      = dim.getTimeOfDay(time, 1.f);
    double d2     = 0.5 + 2.0 * std::clamp(std::cos(t * 6.283185307179586), -0.25, 0.25);
    int    darken = static_cast<int>((1.0 - d2) * 11.0);
    return darken < 4;
}

bool wearsGear(Actor const& a) {
    for (int i = 0; i < 4; ++i) {
        auto const& s = a.getArmor(static_cast<Slot>(i));
        if (!items::isEmpty(s) && isGear(items::idOf(s))) return true;
    }
    return false;
}

// ---- per-tick modifiers ------------------------------------------------------------------------------

/// PlayerHooks (efficient eater): TF divides the exhaustion of checkMovementStatistics and of jumps by the
/// component value (2). Bedrock's movement exhaustion is the FoodExhaustionSystem's, from the player's
/// ExhaustionComponent coefficients: while the modifier is active its walk, sprint, swim, jump and sprint
/// jump coefficients are the player's own divided by the value (mining, attacking, damage and healing
/// keep theirs, as in TF).
void applyEfficientEater(Player& player) {
    auto c = player.getEntityContext().tryGetComponent<ExhaustionComponent>();
    if (!c) return;
    float divisor = isActive(player, ids::kEfficientEater) ? static_cast<float>(numberOf(ids::kEfficientEater, 2.0)) : 1.f;
    if (divisor <= 0) divisor = 1.f;
    auto            uid = uidOf(player);
    std::lock_guard lock(state().mutex);
    auto            it = state().exhaustionBase.find(uid);
    if (it == state().exhaustionBase.end()) {
        if (divisor == 1.f) return; // nothing scaled yet: the component holds the player's own values
        it = state().exhaustionBase.emplace(uid, std::array<float, 5>{c->mWalk, c->mSprint, c->mSwim, c->mJump, c->mSprintJump}).first;
    }
    auto const& b  = it->second;
    c->mWalk       = b[0] / divisor;
    c->mSprint     = b[1] / divisor;
    c->mSwim       = b[2] / divisor;
    c->mJump       = b[3] / divisor;
    c->mSprintJump = b[4] / divisor;
    if (divisor == 1.f) state().exhaustionBase.erase(it);
}

/// TravellersArmorItem#canWalkOnPowderedSnow (the boots) and the minecraft:freeze_immune_wearables tag
/// (the vest and the boots): Bedrock reads both from entity flags (CanStandOnPowderSnowComponent,
/// FreezingImmuneComponent; leather armour sets their FromEquipment twins), so the wearer gets the
/// flags while it wears the pieces and loses the ones this module added when it takes them off.
void applySnowFlags(Actor& wearer) {
    auto const& feet  = wearer.getArmor(Slot::Feet);
    auto const& chest = wearer.getArmor(Slot::Torso);
    bool        snow   = !items::isEmpty(feet) && items::idOf(feet) == ids::kBoots;
    bool        freeze = snow || (!items::isEmpty(chest) && items::idOf(chest) == ids::kVest);
    auto&       ec     = wearer.getEntityContext();
    auto        uid    = uidOf(wearer);
    int         had    = 0;
    {
        std::lock_guard lock(state().mutex);
        if (auto it = state().standFlags.find(uid); it != state().standFlags.end()) had = it->second;
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
    if (now) state().standFlags[uid] = now;
    else state().standFlags.erase(uid);
}

/// TravellersArmorItem#getName: a broken piece is named "<name> (Broken)" (gray). Bedrock items have
/// no per-stack name function: the piece gets a custom name in the holder's language while it is
/// broken (marked in its data, so a player's own anvil name is never touched), removed once repaired.
bool refreshBrokenName(ItemStack& stack, Player const* holder) {
    if (items::isEmpty(stack) || !isGear(items::idOf(stack)) || !stack.isDamageableItem()) return false;
    auto mark    = items::getData(stack, kBrokenNameKey);
    auto current = stack.getCustomName();
    bool ours    = mark.is_string() && !current.empty() && current == mark.get<std::string>();
    bool foreign = !current.empty() && !ours;
    if (isBroken(stack) && !foreign) {
        auto id   = items::idOf(stack);
        auto key  = "item.twilightforest." + id.substr(id.find(':') + 1);
        auto name = holder ? trFor(*holder, key, key) : tr(key, key);
        auto mark2 = holder ? trFor(*holder, "travellers_gear.broken", " (Broken)") : tr("travellers_gear.broken", " (Broken)");
        // Component.translatable(descriptionId).append(" (Broken)" gray); Bedrock draws custom names
        // in italics: the reset keeps TF's upright name
        auto text = "§r" + name + "§7" + mark2;
        if (ours && current == text) return false;
        stack.setCustomName(Bedrock::Safety::RedactableString(text));
        items::setData(stack, kBrokenNameKey, text);
        return true;
    }
    if (!isBroken(stack) && ours) {
        stack.resetHoverName();
        items::setData(stack, kBrokenNameKey, nullptr);
        return true;
    }
    if (!ours && mark.is_string()) {
        items::setData(stack, kBrokenNameKey, nullptr);
        return true;
    }
    return false;
}

void refreshBrokenNames(Player& player) {
    bool changed = false;
    for (int i = 0; i < 4; ++i) {
        ItemStack s = player.getArmor(static_cast<Slot>(i));
        if (refreshBrokenName(s, &player)) {
            player.setArmor(static_cast<Slot>(i), s);
            changed = true;
        }
    }
    auto& inv = player.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        auto const& cur = inv.getItem(i);
        if (items::isEmpty(cur) || !isGear(items::idOf(cur))) continue;
        ItemStack s = cur;
        if (refreshBrokenName(s, &player)) {
            inv.setItem(i, s);
            changed = true;
        }
    }
    {
        ItemStack s = player.getOffhandSlot();
        if (refreshBrokenName(s, &player)) {
            player.setOffhandSlot(s);
            changed = true;
        }
    }
    if (changed) player.sendInventory(false);
}

/// The ticks of every LivingEntity wearing gear (EntityTickEvent: haste, auto repair) plus the
/// snow / freeze flags.
void tickWearer(Actor& wearer, std::int64_t now) {
    if (!wearer.isAlive()) return;
    // TravellersGearLogic#travellersVestHaste
    if (isActive(wearer, ids::kHaste)) hiddenEffect(wearer, kHasteEffect, static_cast<int>(numberOf(ids::kHaste, 1)));
    autoRepair(wearer, now);
    applySnowFlags(wearer);
}

/// ComputeFovModifierEvent (FOV x zoom) on the zooming player's own camera: Bedrock's world projection
/// comes from the vanilla camera (its custom FOV), which the camera command sets for one player
/// ("camera <player> fov_set <fov>" / "fov_clear", run as the server). The round-3 e2e probes: the
/// command narrows the view (49 % of the pixels change) while a hand-built CameraInstructionPacket with
/// the same FOV instruction is accepted without effect. `fov` is the client's FOV times the zoom; the
/// camera takes [30, 110] only ("Custom FOV value must be within [30.0, 110.0]"), so TF's 21 degrees
/// becomes 30.
void sendZoomFov(Player& player, bool zoom, float fov) {
    if (player.isSimulatedPlayer()) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    auto name = player.getRealName();
    std::erase(name, '"');
    auto cmd = zoom ? fmt::format("camera \"{}\" fov_set {:.1f}", name, std::clamp(fov, 30.f, 110.f)) : fmt::format("camera \"{}\" fov_clear", name);
    ServerCommandOrigin origin("gloamingforest-tgear", level->asServer(), CommandPermissionLevel::Owner, player.getDimensionId());
    ++commands::detail::quietCommandErrors;
    auto out = ll::command::CommandRegistrar::getServerInstance().executeCommand(cmd, origin);
    --commands::detail::quietCommandErrors;
    if (out.mSuccessCount <= 0) log().warn("tgear: '{}' failed for {}", cmd, player.getRealName());
}
void applyZoom(Player& player, bool zoom, float fov) {
    sendZoomFov(player, zoom, fov > 0 ? fov : 70.f * static_cast<float>(numberOf(ids::kZoom, 0.3)));
    log().info("tgear: {} zoom {} (fov {})", player.getRealName(), zoom ? "in" : "out", zoom ? fov : 0.f);
    // TravellersClientEvents#updateZoomState: the zooming client plays the zoom sound to itself at once
    // (LocalPlayer#playSound: nobody else hears it), so the server plays none.
    // GogglesZoomPacket: the attachment is synced to the players tracking this one (the goggles' "down"
    // texture: the worn-model state on the wearer, refreshed at once, and the companions in range)
    refreshWornModel(player);
    auto level = ll::service::getLevel();
    if (!level) return;
    auto uid = uidOf(player);
    auto dim = player.getDimensionId();
    auto at  = player.getPosition();
    level->forEachPlayer([&](Player& other) {
        if (&other == &player || other.isClientSide() || other.getDimensionId() != dim) return true;
        float range = static_cast<float>(std::max(4u, static_cast<unsigned>(other.mChunkRadius)) * 16u);
        if (other.getPosition().distanceToSqr(at) <= range * range) net::send(other, "tgear.goggles_zoom", {{"player", uid}, {"zoom", zoom}});
        return true;
    });
}

/// A zooming player whose zoom ability is gone (goggles off or broken) stops zooming.
void clearZoomIfInactive(Player& player) {
    {
        std::lock_guard lock(state().mutex);
        auto            it = state().zoom.find(uidOf(player));
        if (it == state().zoom.end() || !it->second) return;
    }
    if (isActive(player, ids::kZoom)) return;
    {
        std::lock_guard lock(state().mutex);
        state().zoom[uidOf(player)] = false;
    }
    applyZoom(player, false, 0.f);
}
void tickPlayer(Player& player, std::int64_t now) {
    if (!player.isAlive()) return;
    // travellersStealth (PlayerTickEvent): crouching -> Invisibility (2 ticks, hidden)
    if (isActive(player, ids::kStealth) && player.isSneaking()) hiddenEffect(player, kInvisibilityEffect, 0);
    tickDisplayMap(player);
    applyEfficientEater(player);
    clearZoomIfInactive(player);
    // the worn models' state (gloves, slim arms, belt, zoom) for every viewer
    refreshWornModel(player);
    // the broken names, once a second per player (staggered)
    if ((now + uidOf(player)) % 20 == 0) refreshBrokenNames(player);
}

bool isArrowType(std::string const& type) { return type == "minecraft:arrow" || type == "minecraft:thrown_trident"; }

/// TravellersGearEvents#magnetizeArrows (ProjectileImpactEvent on a block): an AbstractArrow (arrows and
/// tridents) of a wearer that hits a block before it is 200 ticks old is discarded; a player owner gets
/// the pickup item when the pickup is ALLOWED (survival), and CREATIVE_ONLY arrows are discarded for a
/// creative owner. Bedrock gives units no impact event: an arrow of a wearer is tracked from the tick it
/// is first seen moving, and the tick it stops moving is its impact. Arrows already stuck when they are first seen (shot before, loaded from a
/// chunk) never count.
void magnetize(Level& level, std::vector<Actor*> const& arrows, std::unordered_set<std::int64_t> const& wearers, std::int64_t now) {
    auto&                                   s = state();
    std::unordered_map<std::int64_t, State::Arrow> next;
    std::vector<Actor*>                     hits;
    for (auto* a : arrows) {
        auto ownerId = a->getOwnerId().rawID;
        if (!wearers.contains(ownerId)) continue;
        auto         id  = uidOf(*a);
        auto const&  pos = a->getPosition();
        State::Arrow rec;
        bool         known = false;
        {
            std::lock_guard lock(s.mutex);
            if (auto it = s.arrows.find(id); it != s.arrows.end()) {
                rec   = it->second;
                known = true;
            }
        }
        if (!known) {
            // first sighting: whether it flies shows next tick
            rec.firstTick = now;
            rec.last      = pos;
            next[id]      = rec;
            continue;
        }
        bool moved = std::abs(pos.x - rec.last.x) + std::abs(pos.y - rec.last.y) + std::abs(pos.z - rec.last.z) > 1e-3f;
        rec.last   = pos;
        if (moved) {
            rec.flying = true;
            next[id]   = rec;
            continue;
        }
        // stopped: an impact only when it was seen flying (projectile.tickCount < 200)
        if (rec.flying && now - rec.firstTick < 200) hits.push_back(a);
        rec.flying = false;
        next[id]   = rec;
    }
    {
        std::lock_guard lock(s.mutex);
        s.arrows.swap(next);
    }
    for (auto* a : hits) {
        Actor* owner = level.fetchEntity(a->getOwnerId(), false);
        if (!owner || !isActive(*owner, ids::kArrowMagnetism)) continue;
        if (!owner->isPlayer()) {
            a->remove();
            continue;
        }
        auto& player  = static_cast<Player&>(*owner);
        auto& arrow   = static_cast<AbstractArrow&>(*a);
        bool  allowed = !arrow.mIsCreative; // Bedrock's creative / infinity arrows are CREATIVE_ONLY
        if (allowed && !player.isCreative()) {
            ItemStack pickup = arrow._getPickupItem();
            if (!items::isEmpty(pickup) && !player.add(pickup)) player.drop(pickup, false);
            player.sendInventory(false);
        }
        if (allowed || player.isCreative()) {
            log().info("tgear: {}'s {} magnetized back", player.getRealName(), a->getTypeName());
            a->remove();
        }
    }
}

void onLevelTick() {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto now = gameTime();
    level->forEachPlayer([&](Player& p) {
        if (!p.isClientSide()) tickPlayer(p, now);
        return true;
    });
    // one pass over the actors: the wearers (players and mobs) and the arrows
    std::unordered_set<std::int64_t> magnets;
    std::vector<Actor*>              arrows;
    for (auto* a : level->getRuntimeActorList()) {
        if (!a || a->isClientSide()) continue;
        auto const& type = a->getTypeName();
        if (isArrowType(type)) {
            if (a->isAlive()) arrows.push_back(a);
            continue;
        }
        if (!a->hasCategory(ActorCategory::Mob) || !wearsGear(*a)) {
            // a wearer that took everything off loses the flags this module gave it
            bool had;
            {
                std::lock_guard lock(state().mutex);
                had = state().standFlags.contains(uidOf(*a));
            }
            if (had) applySnowFlags(*a);
            continue;
        }
        tickWearer(*a, now);
        if (isActive(*a, ids::kArrowMagnetism)) magnets.insert(uidOf(*a));
    }
    if (magnets.empty()) {
        std::lock_guard lock(state().mutex);
        state().arrows.clear();
        return;
    }
    magnetize(*level, arrows, magnets, now);
}

// ---- armour damage ------------------------------------------------------------------------------------

/// TravellersGearEvents#stopDamagingTravellersGear (ArmorHurtEvent): a piece whose new damage would
/// reach its max damage stops at max damage - 1 without a sound; a piece that reaches max damage - 1
/// exactly plays item.break to its player. Bedrock's hurtArmorSlots has no "new damage" to change, so
/// the gear slots are taken out of the game's call and damaged here with the game's amount
/// (max(1, damage / 4), Inventory#hurtArmor); the game's own break effects never run for them.
void onArmorHurtPre(Mob& wearer, int damage, std::bitset<5>& slots) {
    if (wearer.isClientSide()) return;
    if (wearer.isPlayer() && static_cast<Player&>(static_cast<Actor&>(wearer)).isCreative()) return;
    int amount = std::max(1, damage / 4);
    for (int i = 0; i < 4; ++i) {
        if (!slots.test(i)) continue;
        ItemStack stack = wearer.getArmor(static_cast<Slot>(i));
        if (items::isEmpty(stack) || !isGear(items::idOf(stack)) || !stack.isDamageableItem()) continue;
        slots.reset(i);
        int max = maxDamageOf(stack), cur = stack.getDamageValue();
        int add = amount;
        if (cur + add >= max) add = max - cur - 1;
        else if (cur + add >= max - 1 && wearer.isPlayer()) fx::soundTo(static_cast<Player&>(static_cast<Actor&>(wearer)), "minecraft:entity.item.break", 1.f, voicePitch());
        if (add > 0) {
            stack.setDamageValue(static_cast<short>(cur + add));
            wearer.setArmor(static_cast<Slot>(i), stack);
            setLastArmorDamageTime(wearer, gameTime()); // setLastDamageArmorTime: a new damage > 0
            if (isBroken(stack)) log().info("tgear: {}'s {} is broken", wearer.getTypeName(), items::idOf(stack));
        }
    }
}

/// TravellersGearEvents#setLastDamageArmorTime for the pieces the game damaged itself.
void onArmorHurt(Mob& wearer, std::array<ItemStack, 4> const& before) {
    if (wearer.isClientSide()) return;
    for (int i = 0; i < 4; ++i) {
        auto const& was = before[i];
        auto const& now = wearer.getArmor(static_cast<Slot>(i));
        bool        broke = !items::isEmpty(was) && (items::isEmpty(now) || items::idOf(now) != items::idOf(was));
        if (broke || (!items::isEmpty(now) && now.getDamageValue() > was.getDamageValue())) {
            setLastArmorDamageTime(wearer, gameTime());
            return;
        }
    }
}

// ---- incoming damage -----------------------------------------------------------------------------------

/// Damage types that ignore armour (Java's #minecraft:bypasses_armor).
bool bypassesArmor(std::string const& type) {
    static std::unordered_set<std::string> const kTypes{
        "minecraft:fall",          "minecraft:drown",          "minecraft:starve",        "minecraft:out_of_world",
        "minecraft:generic_kill",  "minecraft:magic",          "minecraft:indirect_magic", "minecraft:wither",
        "minecraft:freeze",        "minecraft:in_wall",        "minecraft:cramming",      "minecraft:dry_out",
        "minecraft:sonic_boom",    "minecraft:fly_into_wall",  "minecraft:outside_border", "minecraft:on_fire",
        "minecraft:stalagmite",    "minecraft:falling_stalactite", "minecraft:void",       "minecraft:suffocation",
    };
    return kTypes.contains(type);
}

/// Perfect dodge (TravellersGearEvents#performPerfectDodge): the impact is cancelled, so the projectile
/// flies on through the wearer. A TF projectile (simulated by gf::projectile) gets the impact itself
/// cancelled (F8's ext::cancelImpact, NeoForge's ProjectileImpactEvent#setCanceled): no hit, no
/// removal, it flies on with its velocity. A vanilla projectile (Bedrock's own hit): the damage is
/// cancelled and, a tick later, the projectile gets back the motion it hit with, one step further
/// along, so it passes the wearer or hits it again.
combat::Verdict onIncoming(combat::DamageInfo& d) {
    // the projectile of the hit (combat's direct entity; a child-actor source may name it as the
    // attacker instead)
    Actor* projectile = nullptr;
    bool   simulated  = false;
    for (Actor* a : {d.direct, d.attacker}) {
        if (projectile || !a || a == &d.victim) continue;
        if (gf::projectile::ext::isProjectile(*a)) {
            projectile = a;
            simulated  = true;
        }
        else if (a->getEntityContext().tryGetComponent<ProjectileComponent>()) projectile = a;
    }
    if (projectile) {
        auto key = std::make_pair(uidOf(d.victim), uidOf(*projectile));
        {
            // TF cancels the whole impact: the explosion a dodged fireball / skull would cause on that
            // impact (the same tick, the same victim) is cancelled with it
            std::lock_guard lock(state().mutex);
            auto&           dodged = state().dodgedImpacts;
            auto            now    = gameTime();
            std::erase_if(dodged, [now](auto const& e) { return e.second < now; });
            for (auto const& e : dodged)
                if (e.first == key) return combat::Verdict::Cancel;
        }
        // only the projectile's own impact (ProjectileImpactEvent with an EntityHitResult on the wearer),
        // never the explosion, fire or magic damage a projectile causes
        // (a TF projectile's hit deals its own TF damage type: the types TF's projectiles deal in
        // onHitEntity; not the ice bomb's area freeze, the falling ice or the mortar's blast)
        static std::unordered_set<std::string> const kTfImpacts{
            "twilightforest:lich_bolt",      "twilightforest:lich_bomb",     "twilightforest:moonworm",
            "twilightforest:leaf_brain",     "twilightforest:thrown_block",  "twilightforest:thrown_axe",
            "twilightforest:thrown_pickaxe", "twilightforest:lost_words",    "twilightforest:schooled",
            "twilightforest:twilight_scepter", "twilightforest:snowball_fight", "twilightforest:spiked",
        };
        bool impact = core::tgear::isProjectileImpactType(d.type) || (simulated && kTfImpacts.contains(d.type));
        if (impact && rollPerfectDodge(d.victim, projectile)) {
            {
                std::lock_guard lock(state().mutex);
                state().dodgedImpacts.push_back({key, gameTime()});
            }
            if (simulated) {
                gf::projectile::ext::cancelImpact(*projectile);
                return combat::Verdict::Cancel;
            }
            auto  id  = projectile->getOrCreateUniqueID();
            Vec3  v   = projectile->getPosDelta();
            Vec3  at  = projectile->getPosition();
            ia::nextTick([id, v, at] {
                auto level = ll::service::getLevel();
                if (!level) return;
                auto* p = level->fetchEntity(id, false);
                if (!p || !p->isAlive()) return;
                if (auto pc = p->getEntityContext().tryGetComponent<ProjectileComponent>()) {
                    pc->mOnGroundTime = 0;
                    pc->mHitActor     = false;
                }
                p->_setPos(Vec3{at.x + v.x, at.y + v.y, at.z + v.z});
                const_cast<Vec3&>(p->getPosDelta()) = v;
                p->lerpMotion(v);
            });
            return combat::Verdict::Cancel;
        }
    }
    // TravellersGearEvents#activateAndDeactivateTravellersModifiers: a broken piece has no attribute
    // modifiers (no armour points). Bedrock's protection is the item's: the damage is raised so the
    // game's armour reduction (4 % per point) reduces it as if the broken pieces gave none.
    if (!bypassesArmor(d.type)) {
        int broken = 0;
        for (int i = 0; i < 4; ++i) {
            auto const& s = d.victim.getArmor(static_cast<Slot>(i));
            if (!items::isEmpty(s) && isGear(items::idOf(s)) && isBroken(s)) broken += armorPointsOf(items::idOf(s));
        }
        if (broken > 0 && d.victim.hasCategory(ActorCategory::Mob)) {
            int total = std::min(static_cast<Mob&>(d.victim).getArmorValue(), 20); // Bedrock caps the reduction at 20 points
            int left  = std::max(total - broken, 0);
            d.amount *= (25.f - static_cast<float>(left)) / (25.f - static_cast<float>(total));
        }
    }
    return combat::Verdict::Pass;
}

// ---- target filters ---------------------------------------------------------------------------------------

/// The attacker was hurt by `candidate` (it is angry at it).
bool hurtBy(Mob& attacker, Actor& candidate) {
    auto id = uidOf(candidate);
    return static_cast<ActorUniqueID const&>(attacker.mLastHurtByMobId).rawID == id || static_cast<ActorUniqueID const&>(attacker.mLastHurtByPlayerId).rawID == id;
}

int armorCover(Actor const& a) {
    int n = 0;
    for (int i = 0; i < 4; ++i) n += items::isEmpty(a.getArmor(static_cast<Slot>(i))) ? 0 : 1;
    return n;
}

bool targetFilter(Mob& attacker, Actor& candidate) {
    if (!candidate.isPlayer()) return true;
    auto const& type = attacker.getTypeName();
    // TravellersGogglesItem#isEnderMask: active all-night goggles hide the wearer's gaze
    if (type == "minecraft:enderman" && isActive(candidate, ids::kAllNight) && !hurtBy(attacker, candidate)) return false;
    // TravellersArmorItem#makesPiglinsNeutral: the goggles, or wings
    if (type == "minecraft:piglin" && !hurtBy(attacker, candidate)) {
        auto const& head = candidate.getArmor(Slot::Head);
        auto const& legs = candidate.getArmor(Slot::Legs);
        if ((!items::isEmpty(head) && items::idOf(head) == ids::kGoggles) || (!items::isEmpty(legs) && items::idOf(legs) == ids::kWings)) return false;
    }
    // stealth: the invisible crouching wearer is seen only within LivingEntity#getVisibilityPercent of the
    // follow range (0.8 crouching x 0.7 x the armour cover, at least 0.1; TargetingConditions: at least 2
    // blocks)
    if (candidate.isSneaking() && isActive(candidate, ids::kStealth) && !hurtBy(attacker, candidate)) {
        float cover = std::max(0.1f, static_cast<float>(armorCover(candidate)) / 4.f);
        auto  attr  = attacker.getAttribute(SharedAttributes::FOLLOW_RANGE());
        float follow = attr.mPtr ? attr.mPtr->mCurrentValue : 16.f; // the attacker's follow range (Mob#getAttributeValue(FOLLOW_RANGE))
        float range = std::max(follow * 0.8f * 0.7f * cover, 2.f);
        auto  a = feetOf(attacker), c = feetOf(candidate);
        float dx = a.x - c.x, dy = a.y - c.y, dz = a.z - c.z;
        if (dx * dx + dy * dy + dz * dz > range * range) return false;
    }
    return true;
}

// ---- red thread (RedThreadBlock: a MultifaceBlock) -------------------------------------------------------

constexpr char const* kFaceStates[6]     = {"twilightforest:down", "twilightforest:up", "twilightforest:north", "twilightforest:south", "twilightforest:west", "twilightforest:east"};
constexpr int         kFaceOffsets[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};

int oppositeFace(int f) { return f ^ 1; }

nlohmann::json facesOf(Block const& block) {
    nlohmann::json j = nlohmann::json::object();
    for (int f = 0; f < 6; ++f) j[kFaceStates[f]] = game::intState(block, kFaceStates[f]).value_or(0) != 0;
    return j;
}

bool anyFace(nlohmann::json const& j) {
    for (auto const* f : kFaceStates)
        if (j.value(f, false)) return true;
    return false;
}

/// Context#getNearestLookingDirections: the six faces by how much the player looks along them.
std::vector<int> lookingFaces(Player const& player) {
    auto                               v = player.getViewVector(1.f);
    std::vector<std::pair<float, int>> order;
    float                              comps[6] = {-v.y, v.y, -v.z, v.z, -v.x, v.x};
    for (int f = 0; f < 6; ++f) order.push_back({-comps[f], f});
    std::stable_sort(order.begin(), order.end());
    std::vector<int> out;
    for (auto const& [_, f] : order) out.push_back(f);
    return out;
}

/// MultifaceBlock#getStateForPlacement at `pos` (the existing thread's faces, or none): the first of the
/// player's looking directions that the thread does not have yet and can attach to.
std::optional<nlohmann::json> placementFaces(BlockSource& region, BlockPos const& pos, nlohmann::json faces, Player const& player) {
    for (int f : lookingFaces(player)) {
        if (faces.value(kFaceStates[f], false) || !canAttachRedThread(region, pos, f)) continue;
        faces[kFaceStates[f]] = true;
        return faces;
    }
    return std::nullopt;
}

/// MultifaceBlock#updateShape: the faces whose support is gone are removed; no face left: air (no drop).
void updateRedThread(BlockSource& region, BlockPos const& pos) {
    auto const& block = region.getBlock(pos);
    if (game::blockId(block) != ids::kRedThread) return;
    auto faces   = facesOf(block);
    bool changed = false;
    for (int f = 0; f < 6; ++f) {
        if (!faces.value(kFaceStates[f], false) || canAttachRedThread(region, pos, f)) continue;
        faces[kFaceStates[f]] = false;
        changed               = true;
    }
    if (!changed) return;
    if (!anyFace(faces)) {
        game::clearBlock(region, pos);
        return;
    }
    if (auto const* b = game::blockWithStates(ids::kRedThread, faces)) game::setBlock(region, pos, *b);
}

// ---- red thread vision (TFDataAttachments.TRAVELLERS_GOGGLES_RED_THREAD_VISION) -------------------------

std::string uuidOf(Player const& p) { return p.getUuid().asString(); }

void loadVision() {
    std::lock_guard lock(state().mutex);
    state().vision.clear();
    if (auto j = game::db::get(kVisionKey); j && j->is_object())
        for (auto const& [k, v] : j->items())
            if (v.is_boolean()) state().vision[k] = v.get<bool>();
}

void saveVision() {
    nlohmann::json j = nlohmann::json::object();
    {
        std::lock_guard lock(state().mutex);
        for (auto const& [k, v] : state().vision) j[k] = v;
    }
    game::db::put(kVisionKey, j);
}

void sendVision(Player& p) { net::send(p, "tgear.thread_vision", {{"on", redThreadVision(p)}}); }

} // namespace

// ---- Internal.h ----------------------------------------------------------------------------------------

std::int64_t lastArmorDamageTime(Actor const& wearer) {
    std::lock_guard lock(state().mutex);
    auto            it = state().lastArmorDamage.find(uidOf(wearer));
    return it == state().lastArmorDamage.end() ? std::numeric_limits<std::int64_t>::min() / 2 : it->second;
}

void setLastArmorDamageTime(Actor const& wearer, std::int64_t time) {
    std::lock_guard lock(state().mutex);
    state().lastArmorDamage[uidOf(wearer)] = time;
}

void seedRandom(std::uint64_t seed) {
    std::lock_guard lock(state().mutex);
    state().rng.seed(seed);
}

void setDodgeProbabilityForTest(std::optional<float> p) {
    std::lock_guard lock(state().mutex);
    state().dodgeOverride = p;
}

int autoRepair(Actor& wearer, std::int64_t now) {
    // TravellersGearLogic#travellersGearAutoRepair: nothing within 10 s of armour damage
    if (now - lastArmorDamageTime(wearer) <= core::tgear::kAutoRepairQuietTicks) return 0;
    int repaired = 0;
    for (int i = 0; i < 4; ++i) {
        ItemStack stack = wearer.getArmor(static_cast<Slot>(i));
        if (items::isEmpty(stack) || !hasModifier(stack, ids::kAutoRepair) || !isActiveOn(wearer, stack, ids::kAutoRepair)) continue;
        if (!stack.isDamageableItem() || stack.getDamageValue() <= 0) {
            (void)nextFloat(); // the roll happens either way in TF
            continue;
        }
        auto&    dim  = wearer.getDimension();
        BlockPos pos  = game::blockPosOf(feetOf(wearer)); // entity.blockPosition()
        bool     sky  = wearer.getDimensionBlockSource().canSeeSky(pos);
        double   base = numberOf(ids::kAutoRepair, 0.001);
        double   p    = core::tgear::autoRepairChance(base, sky, gf::world::isTwilight(dim), isDay(dim, wearer.getLevel().getTime()));
        if (p > nextFloat()) {
            stack.setDamageValue(static_cast<short>(std::max(stack.getDamageValue() - 1, 0)));
            wearer.setArmor(static_cast<Slot>(i), stack);
            ++repaired;
        }
    }
    return repaired;
}

bool rollPerfectDodge(Actor& victim, Actor* projectile) {
    auto const& chest = victim.getArmor(Slot::Torso);
    if (items::isEmpty(chest) || !isActiveOn(victim, chest, ids::kPerfectDodge)) return false;
    float probability = static_cast<float>(numberOf(ids::kPerfectDodge, 0.3));
    {
        std::lock_guard lock(state().mutex);
        if (state().dodgeOverride) probability = *state().dodgeOverride;
    }
    if (probability <= nextFloat()) return false;
    // projectile.position().add(projectile.getDeltaMovement())
    Vec3 hit = projectile ? projectile->getPosition() + projectile->getPosDelta() : feetOf(victim);
    fx::sound(victim.getDimension(), "item.twilightforest.travellers_gear.perfect_dodge", hit, 1.5f, voicePitch());
    for (int n = 0; n < 20; ++n) {
        Vec3 v{static_cast<float>(nextDouble() - 0.5), static_cast<float>(nextDouble() - 0.5), static_cast<float>(nextDouble() - 0.5)};
        fx::particle(victim.getDimension(), "twilightforest:perfect_dodge", hit, v, 1);
    }
    {
        std::lock_guard lock(state().mutex);
        ++state().dodges;
    }
    log().info("tgear: {} dodged a projectile", victim.isPlayer() ? static_cast<Player&>(victim).getRealName() : victim.getTypeName());
    return true;
}

int dodgeCount() {
    std::lock_guard lock(state().mutex);
    return state().dodges;
}

bool canAttachRedThread(BlockSource& region, BlockPos const& pos, int face) {
    // MultifaceBlock#canAttachTo: the neighbour's face towards the thread is full (isFaceFull of its
    // support / collision shape: Bedrock's canProvideFullSupport of that face)
    BlockPos n{pos.x + kFaceOffsets[face][0], pos.y + kFaceOffsets[face][1], pos.z + kFaceOffsets[face][2]};
    if (!region.hasChunksAt(n, 0, false)) return false;
    auto const& b = region.getBlock(n);
    return b.canProvideFullSupport(static_cast<uchar>(oppositeFace(face)));
}

bool usingZoom(Player const& player) {
    std::lock_guard lock(state().mutex);
    auto            it = state().zoom.find(uidOf(player));
    return it != state().zoom.end() && it->second;
}

void setZoom(Player& player, bool zoom, float fov) {
    // GogglesZoomPacket#handle on the server: ignored entirely without an active zoom ability (the
    // state of a wearer who loses the ability is cleared by the tick: clearZoomIfInactive)
    if (!isActive(player, ids::kZoom)) return;
    {
        std::lock_guard lock(state().mutex);
        auto&           cur = state().zoom[uidOf(player)];
        if (cur == zoom) return; // a repeated state changes nothing
        cur = zoom;
    }
    applyZoom(player, zoom, fov);
}
bool redThreadVision(Player const& player) {
    std::lock_guard lock(state().mutex);
    auto            it = state().vision.find(uuidOf(player));
    return it == state().vision.end() || it->second; // TFDataAttachments default: true
}

bool toggleRedThreadVision(Player& player) {
    // TravellersClientEvents#toggleBooleanDataAttachment: only while the modifier is active
    if (!isActive(player, ids::kRedThreadVision)) return false;
    bool on = !redThreadVision(player);
    {
        std::lock_guard lock(state().mutex);
        state().vision[uuidOf(player)] = on;
    }
    saveVision();
    sendVision(player);
    log().info("tgear: {}'s red thread vision {}", player.getRealName(), on ? "on" : "off");
    return true;
}

namespace {

class TgearModule {
public:
    void install() {
        gf::ext::tgear::installHooks();
        gf::ext::tgear::installCraftHooks();
        static bool once = false;
        if (!once) {
            once = true;
            gf::ext::tgear::onArmorHurtPre(&onArmorHurtPre);
            gf::ext::tgear::onArmorHurt(&onArmorHurt);
            combat::onIncoming(combat::priority::High, &onIncoming);
            entity::addTargetFilter(&targetFilter);
            progression::events::onInventoryChanged([](Player& p) { fireCraftedTriggers(p); });
            game::db::onWorldReady([] {
                registerRecipes();
                loadVision();
            });
            game::db::onWorldClosed([] {
                std::lock_guard lock(state().mutex);
                state().vision.clear();
            });
            net::on("tgear.swap_hotbar", [](Player& p, Json const&) { trySwapHotbar(p); });
            net::on("tgear.cycle_map_slot", [](Player& p, Json const&) { cycleMapSlot(p); });
            net::on("tgear.goggles_zoom", [](Player& p, Json const& msg) { setZoom(p, msg.value("zoom", false), msg.value("fov", 0.f)); });
            net::on("tgear.display_click", [](Player& p, Json const& msg) { displaySlotClick(p, msg.value("collection", std::string()), msg.value("index", -1)); });
            net::on("tgear.thread_vision_toggle", [](Player& p, Json const&) { toggleRedThreadVision(p); });
            // the client mod's firstPersonGloveOverlay (renderGlovesInFirstPerson reads the client config)
            net::on("tgear.glove_overlay", [](Player& p, Json const& msg) { setFirstPersonGloveOverlay(p, msg.value("on", true)); });
            net::onCompanionJoined([](Player& p) { sendVision(p); });
            blockevents::onNeighborChanged(ids::kRedThread, [](BlockSource& region, BlockPos const& pos, BlockPos const&) { updateRedThread(region, pos); });
            registerInteractions();
            registerMachines();
            // BDS enables mods after the world is ready (onWorldReady has fired already): register for
            // this world now; the client's worlds come later through onWorldReady
            if (game::db::available()) {
                registerRecipes();
                loadVision();
            }
        }
        mTick  = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { onLevelTick(); });
        mLeave = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerDisconnectEvent>([](ll::event::PlayerDisconnectEvent& ev) {
            std::lock_guard lock(state().mutex);
            state().zoom.erase(uidOf(ev.self()));
            state().lastArmorDamage.erase(uidOf(ev.self()));
            state().exhaustionBase.erase(uidOf(ev.self()));
            state().standFlags.erase(uidOf(ev.self()));
            forgetWornModel(ev.self());
        });
        // TravellersGearEvents#cancelPhantomSpawns (PlayerSpawnPhantomsEvent DENY): Bedrock spawns the
        // phantoms of an insomniac player around it; a natural phantom spawn whose nearest player wears
        // active all-night goggles is cancelled (the time since rest runs on, as in TF)
        mPhantom = ll::event::EventBus::getInstance().emplaceListener<ll::event::SpawningMobEvent>([](ll::event::SpawningMobEvent& ev) {
            if (ev.identifier().mFullName.get() != "minecraft:phantom" && ev.identifier().mIdentifier.get() != "phantom") return;
            if (!ev.naturalSpawn() || ev.fromSpawner()) return;
            if (deniesPhantomAt(ev.blockSource().getDimensionId(), ev.pos())) ev.cancel();
        });
    }
    void uninstall() {
        gf::ext::tgear::uninstallHooks();
        gf::ext::tgear::uninstallCraftHooks();
        ll::event::EventBus::getInstance().removeListener(mTick);
        ll::event::EventBus::getInstance().removeListener(mLeave);
        ll::event::EventBus::getInstance().removeListener(mPhantom);
    }

private:
    static void registerInteractions() {
        // vanilla fallbacks (docs/units/tgear.md): sneak-use the goggles -> item display screen
        ia::onUse({.item = ids::kGoggles}, [](ia::UseContext const& c) {
            if (!c.sneaking || !displayOf(c.item)) return ia::Result::Pass;
            openDisplayForm(c.player);
            return ia::Result::Consume;
        });
        // sneak-use a belt / wings -> swap the hotbar with the worn legs piece's storage
        for (auto const* id : {ids::kBelt, ids::kWings}) {
            ia::onUse({.item = id}, [](ia::UseContext const& c) {
                if (!c.sneaking || !hasSwapHotbar(c.player.getArmor(Slot::Legs))) return ia::Result::Pass;
                trySwapHotbar(c.player);
                return ia::Result::Consume;
            });
        }
        // red thread placed by the game at pos: MultifaceBlock#getStateForPlacement picks the face from
        // the player's looking directions (not the clicked face); no face it can attach to: the
        // placement fails (the thread goes back to the player)
        ia::onPlace({.block = ids::kRedThread}, [](ia::PlaceContext const& c) {
            auto* region = game::regionOf(c.pos.dim);
            if (!region) return;
            auto pos   = game::blockPos(c.pos);
            auto empty = nlohmann::json::object();
            for (auto const* f : kFaceStates) empty[f] = false;
            auto faces = placementFaces(*region, pos, empty, c.player);
            if (!faces) {
                game::clearBlock(*region, pos);
                if (!c.player.isCreative()) {
                    auto back = items::make(ids::kRedThread, 1);
                    if (back && !c.player.add(*back)) c.player.drop(*back, false);
                    c.player.sendInventory(false);
                }
                return;
            }
            if (auto const* b = game::blockWithStates(ids::kRedThread, *faces)) game::setBlock(*region, pos, *b);
        });
        // RedThreadBlock#canBeReplaced: red thread used on a thread goes into that block (another face
        // from the looking directions), never next to it
        ia::onUseOn({.block = ids::kRedThread, .item = ids::kRedThread}, [](ia::UseOnContext const& c) {
            if (!c.firstEvent) return ia::Result::Consume;
            auto* region = game::regionOf(c.pos.dim);
            if (!region) return ia::Result::Pass;
            auto pos   = game::blockPos(c.pos);
            auto faces = placementFaces(*region, pos, facesOf(c.block), c.player);
            if (!faces) return ia::Result::Consume;
            if (auto const* b = game::blockWithStates(ids::kRedThread, *faces)) {
                game::setBlock(*region, pos, *b);
                // BlockItem#place: SoundType.STONE (RedThreadBlock's Properties.of()) at (volume + 1) / 2, pitch * 0.8;
                // Bedrock's stone place sound is "dig.stone" (sounds.json block_sounds.stone.place)
                fx::sound(c.player.getDimension(), "dig.stone", Vec3{pos.x + .5f, pos.y + .5f, pos.z + .5f}, 1.f, 0.8f);
                if (!c.player.isCreative()) {
                    auto& hand = ia::mutableItem(c.item);
                    auto  rest = items::withCount(hand, items::countOf(hand) - 1);
                    if (c.offhand) c.player.setOffhandSlot(rest);
                    else c.player.setSelectedItem(rest);
                    c.player.sendInventory(false);
                }
            }
            return ia::Result::Consume;
        });
    }

    ll::event::ListenerPtr mTick;
    ll::event::ListenerPtr mLeave;
    ll::event::ListenerPtr mPhantom;
};

} // namespace

bool deniesPhantomAt(int dim, Vec3 const& pos) {
    // PlayerSpawnPhantomsEvent is per insomniac player: Bedrock spawns the phantoms of an insomniac
    // player around it. The spawn is denied only when every player who could own it (an insomniac
    // player of the dimension within 64 blocks; without one, the nearest player) wears active
    // all-night goggles, so a nearby insomniac player without them still gets its phantoms.
    auto level = ll::service::getLevel();
    if (!level) return false;
    std::vector<Player*> owners;
    Player*              nearest = nullptr;
    float                best    = std::numeric_limits<float>::max();
    level->forEachPlayer([&](Player& p) {
        if (p.isClientSide() || static_cast<int>(p.getDimensionId()) != dim) return true;
        auto  f  = p.getFeetPos();
        float d2 = (f.x - pos.x) * (f.x - pos.x) + (f.y - pos.y) * (f.y - pos.y) + (f.z - pos.z) * (f.z - pos.z);
        if (d2 < best) {
            best    = d2;
            nearest = &p;
        }
        float h2 = (f.x - pos.x) * (f.x - pos.x) + (f.z - pos.z) * (f.z - pos.z);
        if (h2 > 64.f * 64.f) return true;
        if (auto ins = p.getEntityContext().tryGetComponent<InsomniaComponent>()) {
            int since = ins->mTimeSinceRest, until = ins->mTicksUntilInsomnia;
            if (until > 0 && since >= until) owners.push_back(&p);
        }
        return true;
    });
    // no insomniac player near: any insomniac player of the dimension may own it, else the nearest player
    if (owners.empty())
        level->forEachPlayer([&](Player& p) {
            if (p.isClientSide() || static_cast<int>(p.getDimensionId()) != dim) return true;
            if (auto ins = p.getEntityContext().tryGetComponent<InsomniaComponent>()) {
                int since = ins->mTimeSinceRest, until = ins->mTicksUntilInsomnia;
                if (until > 0 && since >= until) owners.push_back(&p);
            }
            return true;
        });
    if (owners.empty() && nearest) owners.push_back(nearest);
    if (owners.empty()) return false;
    return std::all_of(owners.begin(), owners.end(), [](Player* p) { return isActive(*p, ids::kAllNight); });
}
} // namespace gf::feature::tgear

GF_REGISTER_MODULE(gf::feature::tgear::TgearModule);
