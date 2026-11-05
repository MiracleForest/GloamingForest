// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/item/travellers_gear/TravellersGearLogic.java
//   src/main/java/twilightforest/events/TravellersGearEvents.java
//   src/main/java/twilightforest/asmhooks/EntityHooks.java
//   src/main/java/twilightforest/asmhooks/PlayerHooks.java
//   src/main/java/twilightforest/network/PerformDoubleJumpPacket.java
//   src/main/java/twilightforest/network/PerformSidestepPacket.java
//   src/main/java/twilightforest/network/GradualGlidePacket.java
//   src/main/java/twilightforest/network/TravellersWingsStatePacket.java
//   src/main/java/twilightforest/init/custom/TravellersModifiersManager.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U12's server half (see TgMove.h). One ServerLevelTickEvent listener runs TF's per-tick player
// and mob logic; the companion packets, the input edges (F12's movement contract), the fall damage
// (F10's combat contract) and the mining speed (the ext/tgmove extension) come in through the
// contracts.
#include "feature/tgmove/TgMove.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <random>
#include <unordered_set>
#include <climits>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/player/PlayerRespawnEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/deps/vanilla_components/OnGroundFlagComponent.h"
#include "mc/deps/vanilla_components/StateVectorComponent.h"
#include "mc/entity/components/BlockMovementSlowdownImmunityComponent.h"
#include "mc/entity/components/ItemUseSlowdownModifierComponent.h"
#include "mc/entity/components_json_legacy/LeashableComponent.h"
#include "mc/world/actor/BuiltInActorComponents.h"
#include "mc/entity/components/MaxAutoStepComponent.h"
#include "mc/entity/components/MoveInputComponent.h"
#include "mc/entity/components/WaterWalkSpeedEnchantComponent.h"
#include "mc/entity/components_json_legacy/BreathableComponent.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/AbilitiesIndex.h"
#include "mc/world/actor/player/LayeredAbilities.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/AttributeInstanceRef.h"
#include "mc/world/attribute/AttributeModifier.h"
#include "mc/world/attribute/AttributeModifierOperation.h"
#include "mc/world/attribute/AttributeOperands.h"
#include "mc/world/attribute/MutableAttributeWithContext.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockDescriptor.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/deps/shared_types/v1_26_20/block/MaterialType.h"

#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/ext/tgmove/Mining.h"
#include "feature/ext/ui/Server.h"
#include "feature/fx/Fx.h"
#include "feature/gear/Gear.h"
#include "feature/movement/Movement.h"
#include "feature/net/Net.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::tgmove {

namespace tg = core::tgmove;
using Json   = nlohmann::json;

GF_REGISTER_CONFIG("tgmove", Config);

namespace {

constexpr unsigned kJumpBoostId    = 8;  ///< MobEffectIds::JumpBoost
constexpr unsigned kSlowFallingId  = 27; ///< MobEffectIds::SlowFalling
constexpr int      kEffectTicks    = 20; ///< our hidden effects last this long, refreshed at half
constexpr double   kTrackRange     = 64; ///< companions that see a player's wings
constexpr char     kWings[]        = "twilightforest:travellers_wings";
constexpr char     kStraightName[] = "twilightforest:travellers_gear.boots_straight_ahead";
constexpr char const* kSlowdownBlocks[] = {"minecraft:web", "minecraft:sweet_berry_bush", "minecraft:powder_snow", "minecraft:honey_block", "minecraft:soul_sand"};

struct Store {
    std::unordered_map<std::int64_t, PlayerState> players; ///< players and mobs
    std::unordered_set<std::int64_t>              mobs;    ///< the mob entries of `players`
    std::optional<bool>                           forceValidator;
    std::mt19937                                  rng{std::random_device{}()};
};
Store& store() {
    static Store s;
    return s;
}

double rand01() { return std::uniform_real_distribution<double>(0.0, 1.0)(store().rng); }

Level* level() { return ll::service::getLevel().as_ptr(); }

int dimOf(Actor const& a) { return static_cast<int>(a.getDimensionId()); }

ItemStack const& armor(Actor const& a, int slot) { return a.getArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(slot)); }

bool wearsWings(Actor const& a) { return game::items::idOf(armor(a, 2)) == kWings; }

bool inLava(Actor& a) {
    auto id = game::blockIdAt(a.getDimensionBlockSource(), a.getFeetBlockPos());
    return id == "minecraft:lava" || id == "minecraft:flowing_lava";
}

bool eyesInWater(Actor const& a) { return a.isUnderLiquid(SharedTypes::v1_26_20::MaterialType::Water); }

bool flying(Player const& p) { return p.isFlying(); }

int effectAmplifier(Actor const& a, unsigned id) {
    auto const* e = a.getEffect(id);
    return e ? e->mAmplifier : -1;
}

/// Adds an effect (hidden: no particles, no icon, as MobEffectInstance(..., false, false, false)).
void addEffect(Actor& a, unsigned id, int amplifier, int ticks, bool visible = false, bool ambient = false) {
    MobEffectInstance inst(id, EffectDuration{ticks});
    inst.mAmplifier     = amplifier;
    inst.mAmbient       = ambient;
    inst.mEffectVisible = visible;
    a.addEffect(inst);
}

/// Our hidden effect of `id` / `amplifier` is on (not an effect of the player's own).
bool ownsEffect(Actor const& a, unsigned id, int amplifier) {
    auto const* e = a.getEffect(id);
    if (!e) return false;
    int d = e->mDuration->mValue;
    return e->mAmplifier == amplifier && d >= 0 && d <= kEffectTicks && !e->mEffectVisible;
}

/// Keeps our hidden effect on while `on` and takes it off once `on` ends. An effect of the
/// entity's own that is weaker is replaced and kept in `saved` (Java's hidden effect), and comes
/// back with the time it had left when ours ends; a stronger one stays and ours waits.
void keepEffect(Actor& a, unsigned id, int amplifier, bool on, bool& applied, std::optional<SavedEffect>* saved) {
    long long now = gameTime();
    if (on) {
        auto const* e    = a.getEffect(id);
        bool        ours = ownsEffect(a, id, amplifier);
        if (e && !ours && e->mAmplifier > amplifier) { // a stronger one of the entity's own
            applied = true;
            return;
        }
        if (e && !ours && e->mAmplifier < amplifier) { // a weaker one: ours replaces it, it waits
            if (saved && !*saved) {
                int d   = e->mDuration->mValue;
                *saved = SavedEffect{e->mAmplifier, d < 0 ? LLONG_MAX : now + d, e->mEffectVisible, e->mAmbient};
            }
            a.removeEffect(static_cast<int>(id));
            addEffect(a, id, amplifier, kEffectTicks);
        } else if (!e || (ours && e->mDuration->mValue <= kEffectTicks / 2)) {
            addEffect(a, id, amplifier, kEffectTicks);
        } // else: the entity's own of the same strength, longer than ours, does the job
        applied = true;
    } else if (applied) {
        if (ownsEffect(a, id, amplifier)) a.removeEffect(static_cast<int>(id));
        applied = false;
    }
    if (!applied && saved && *saved) {
        auto s = **saved;
        saved->reset();
        if (!a.getEffect(id) && s.endTick > now) addEffect(a, id, s.amplifier, s.endTick == LLONG_MAX ? -1 : static_cast<int>(s.endTick - now), s.visible, s.ambient);
    }
}

/// Every companion within the tracking range of `e` (same dimension), `e` too when a companion.
template <class F>
void forTrackers(Actor& e, F&& fn) {
    auto* lv = level();
    if (!lv) return;
    int  dim = dimOf(e);
    Vec3 at  = e.getFeetPos();
    std::vector<std::int64_t> ids;
    lv->forEachPlayer([&](Player& q) {
        if (dimOf(q) == dim && q.getFeetPos().distanceTo(at) <= kTrackRange && net::isCompanion(q)) ids.push_back(q.getOrCreateUniqueID().rawID);
        return true;
    });
    for (auto id : ids)
        if (auto* q = lv->getPlayer(ActorUniqueID{id})) fn(*q);
}

void playAt(Actor& a, std::string const& event, float volume, float pitch) { fx::sound(a.getDimension(), event, a.getFeetPos(), volume, pitch); }

void sendSlimyBoost(Actor& a, double boost) {
    if (!a.isPlayer()) return;
    auto& p = static_cast<Player&>(a);
    if (net::isCompanion(p)) net::send(p, "tgmove.slimy", {{"boost", boost}});
}

void broadcastGlide(Player& p, bool gliding) {
    Json msg{{"player", p.getOrCreateUniqueID().rawID}, {"gliding", gliding}};
    forTrackers(p, [&](Player& q) { net::send(q, "tgmove.gradual_glide", msg); });
}

/// Java's exhaustion of a jump (Player#jumpFromGround).
float jumpExhaustionOf(Player const& p) { return p.isSprinting() ? 0.2f : 0.05f; }

/// Takes `amount` of exhaustion back (efficient eater halves what the game added), never below 0.
void refundExhaustion(Player& p, float amount) {
    float have = 0;
    if (auto ex = p.getAttribute(Player::EXHAUSTION()); ex.mPtr) have = ex.mPtr->mCurrentValue;
    float take = std::min(amount, have);
    if (take > 0) p.causeFoodExhaustion(-take);
}

/// The x1.4 movement speed of straight ahead (multiply-total, transient).
void setStraightAhead(Actor& a, bool want, bool& flag) {
    if (want == flag) return;
    auto attr = a.getMutableAttribute(SharedAttributes::MOVEMENT_SPEED());
    auto* inst = attr.mInstance->mPtr;
    if (!inst) return;
    auto uuid = mce::UUID::fromStringHash(kStraightName);
    inst->removeModifier(uuid, attr.mContext); // idempotent: never twice, whatever `flag` says
    if (want) {
        AttributeModifier mod(uuid, kStraightName, static_cast<float>(tg::kStraightAheadMultiplier - 1), AttributeModifierOperation::OperationMultiplyTotal, AttributeOperands::OperandCurrent, false);
        inst->addModifier(mod, attr.mContext);
    }
    flag = want;
}

/// The step height: base + 0.5 while `on`; the game's own changes become the new base.
void setStep(Actor& a, PlayerState& st, bool on) {
    auto step = a.getEntityContext().tryGetComponent<MaxAutoStepComponent>();
    if (!step) return;
    float cur = step->mValue;
    if (!st.stepBase || cur != st.stepSet) st.stepBase = cur;
    float want = *st.stepBase + (on ? static_cast<float>(tg::kStepUp) : 0.0f);
    if (cur != want) step->mValue = want;
    st.stepSet = want;
}

} // namespace

// ---- engine state ------------------------------------------------------------------------------

/// Unrestrained: our slowdown immunity entries in (or out, leaving the ones others added).
void applyImmunity(Actor& a, PlayerState& st, bool want) {
    auto& ctx = a.getEntityContext();
    if (want && !st.immunity) {
        st.immunityAdded = !ctx.hasComponent<BlockMovementSlowdownImmunityComponent>();
        auto& imm        = ctx.getOrAddComponent<BlockMovementSlowdownImmunityComponent>();
        for (char const* b : kSlowdownBlocks) imm.mBlocks->emplace_back(HashedString(b));
        st.immunity = true;
    } else if (!want && st.immunity) {
        if (auto imm = ctx.tryGetComponent<BlockMovementSlowdownImmunityComponent>()) {
            auto& v = imm->mBlocks.get();
            for (char const* b : kSlowdownBlocks) { // one of each of ours
                for (auto it = v.rbegin(); it != v.rend(); ++it)
                    if (it->getFullName() == b) {
                        v.erase(std::next(it).base());
                        break;
                    }
            }
            if (st.immunityAdded && v.empty()) ctx.removeComponent<BlockMovementSlowdownImmunityComponent>();
        }
        st.immunity      = false;
        st.immunityAdded = false;
    }
}

/// Depth Strider III (water movement efficiency +1) while `on`; the level before comes back after.
void applyDepthStrider(Actor& a, PlayerState& st, bool on) {
    auto& ctx = a.getEntityContext();
    auto  ds  = ctx.tryGetComponent<WaterWalkSpeedEnchantComponent>();
    if (on) {
        if (!st.depthBase) st.depthBase = ds ? ds->mValue : -1;
        else if (ds && ds->mValue != st.depthSet) st.depthBase = ds->mValue; // the game changed it
        int   want = std::max(*st.depthBase, tg::kSwiftSwimDepthStrider);
        auto& c    = ctx.getOrAddComponent<WaterWalkSpeedEnchantComponent>();
        c.mValue   = want;
        st.depthSet = want;
    } else if (st.depthBase) {
        if (ds && ds->mValue == st.depthSet) {
            if (*st.depthBase < 0) ctx.removeComponent<WaterWalkSpeedEnchantComponent>();
            else ds->mValue = *st.depthBase;
        }
        st.depthBase.reset();
    }
}

namespace {

/// Puts back everything this unit changed on `a` (the modifiers' end, forget, uninstall).
void restore(Actor& a, PlayerState& st) {
    if (st.stepBase) {
        if (auto step = a.getEntityContext().tryGetComponent<MaxAutoStepComponent>(); step && step->mValue == st.stepSet) step->mValue = *st.stepBase;
        st.stepBase.reset();
    }
    applyImmunity(a, st, false);
    applyDepthStrider(a, st, false);
    setStraightAhead(a, false, st.straightAhead);
    keepEffect(a, kJumpBoostId, tg::kHighJumpAmplifier, false, st.highJump, &st.savedJumpBoost);
    keepEffect(a, kSlowFallingId, 0, false, st.slowFalling, nullptr);
    if (a.isPlayer()) {
        auto& p = static_cast<Player&>(a);
        (void)applyAgileRanger(p, st.agileBase, st.agileSet); // off: the slowdown before comes back
        st.agileBase.reset();
    }
}

} // namespace

// ---- modifiers ---------------------------------------------------------------------------------

char const* modId(Mod m) {
    switch (m) {
    case Mod::DoubleJump: return "twilightforest:double_jump";
    case Mod::SideStep: return "twilightforest:side_step";
    case Mod::GradualGlide: return "twilightforest:gradual_glide";
    case Mod::HighJump: return "twilightforest:high_jump";
    case Mod::AgileRanger: return "twilightforest:agile_ranger";
    case Mod::StepUp: return "twilightforest:step_up";
    case Mod::StraightAhead: return "twilightforest:straight_ahead";
    case Mod::SlimySoles: return "twilightforest:slimy_soles";
    case Mod::Unrestrained: return "twilightforest:unrestrained";
    case Mod::WaterWalk: return "twilightforest:water_walk";
    case Mod::SwiftSwim: return "twilightforest:swift_swim";
    case Mod::AquaticAgility: return "twilightforest:aquatic_agility";
    case Mod::EfficientEater: return "twilightforest:efficient_eater";
    }
    return "";
}

int modSlot(Mod m) {
    switch (m) {
    case Mod::AquaticAgility: return 0;
    case Mod::SwiftSwim:
    case Mod::EfficientEater: return 1;
    case Mod::DoubleJump:
    case Mod::SideStep:
    case Mod::GradualGlide:
    case Mod::HighJump:
    case Mod::AgileRanger: return 2;
    default: return 3;
    }
}

bool brokenGear(ItemStack const& stack) {
    if (game::items::isEmpty(stack) || !game::items::idOf(stack).starts_with("twilightforest:travellers_")) return false;
    Item const* item = stack.mItem.get();
    if (!item || !stack.isDamageableItem()) return false;
    return item->getMaxDamage() - 1 <= stack.getDamageValue();
}

bool active(Player const& player, Mod m) {
    if (player.isSpectator()) return false;
    if (gear::level(player, modId(m)) <= 0) return false;
    return !brokenGear(armor(player, modSlot(m)));
}

bool activeOn(Actor const& entity, Mod m) {
    if (entity.isPlayer()) return active(static_cast<Player const&>(entity), m);
    auto const& stack = armor(entity, modSlot(m));
    if (game::items::isEmpty(stack) || brokenGear(stack)) return false;
    auto mods = gear::modifiers(stack);
    auto it   = mods.find(modId(m));
    return it != mods.end() && it->second > 0;
}

bool agileRangerItem(ItemStack const& stack) {
    auto id = game::items::idOf(stack);
    if (id.empty()) return false;
    bool legal = id == "minecraft:bow" || id == "minecraft:crossbow" || id == "twilightforest:triple_bow" || id == "twilightforest:seeker_bow"
              || id == "twilightforest:ice_bow" || id == "twilightforest:ender_bow"
              || tags::has(tags::Registry::Item, "twilightforest:travellers_agile_ranger_whitelisted", id);
    return legal && !tags::has(tags::Registry::Item, "twilightforest:travellers_agile_ranger_blacklisted", id);
}

bool applyAgileRanger(Player& player, std::optional<float>& base, float& set) {
    // handleAgileRanger: using a projectile weapon, not riding -> the input x5, which cancels the
    // item use slowdown (Java's 0.2) up to full speed; Bedrock applies the slowdown from this component
    bool on = active(player, Mod::AgileRanger) && player.getItemUseDuration() > 0 && player.getVehicle() == nullptr
           && agileRangerItem(player.getCarriedItem());
    auto slow = player.getEntityContext().tryGetComponent<ItemUseSlowdownModifierComponent>();
    if (!slow) {
        base.reset();
        return false;
    }
    float cur = slow->mModifier;
    if (on) {
        if (!base || cur != set) base = cur; // the game set it for this use
        set             = std::min(1.0f, *base * static_cast<float>(tg::kAgileRangerModifier));
        slow->mModifier = set;
    } else if (base) {
        if (cur == set) slow->mModifier = *base;
        base.reset();
    }
    return on;
}

PlayerState& stateOf(Actor const& entity) {
    auto id = entity.getOrCreateUniqueID().rawID;
    if (!entity.isPlayer()) store().mobs.insert(id);
    return store().players[id];
}

void forget(Actor& entity) {
    auto id = entity.getOrCreateUniqueID().rawID;
    if (auto it = store().players.find(id); it != store().players.end()) {
        restore(entity, it->second);
        store().players.erase(it);
    }
    store().mobs.erase(id);
}

float voicePitch() { return static_cast<float>((rand01() - rand01()) * 0.2 + 1.0); }

long long gameTime() {
    auto* lv = level();
    return lv ? static_cast<long long>(lv->getCurrentTick().tickID) : 0;
}

bool validatorEnabled() {
    if (auto f = store().forceValidator) return *f;
#ifdef GF_CLIENT
    return false; // the integrated server of a local world (TF: isDedicatedServer)
#else
    return true;
#endif
}

void forceValidator(std::optional<bool> on) { store().forceValidator = on; }

// ---- water walk --------------------------------------------------------------------------------

namespace {
bool isWater(Block const& b) {
    auto id = game::blockId(b);
    return id == "minecraft:water" || id == "minecraft:flowing_water";
}
} // namespace

std::optional<double> waterSurface(Actor& actor, Vec3 const& feet, double vy) {
    auto& region = actor.getDimensionBlockSource();
    int   top    = static_cast<int>(std::floor(feet.y - tg::kWaterWalkSurface + 1e-4));
    int   bottom = static_cast<int>(std::floor(feet.y + std::min(vy, 0.0) - tg::kWaterWalkSurface));
    for (int by = top; by >= bottom; --by) {
        double surface = by + tg::kWaterWalkSurface;
        if (surface > feet.y + 1e-4 || surface < feet.y + vy - 1e-4) continue;
        for (double dx : {-0.299, 0.299})
            for (double dz : {-0.299, 0.299}) {
                BlockPos at{static_cast<int>(std::floor(feet.x + dx)), by, static_cast<int>(std::floor(feet.z + dz))};
                auto const& b = region.getBlock(at);
                if (!isWater(b) || game::intState(b, "liquid_depth").value_or(0) != 0) continue;
                if (isWater(region.getBlock(BlockPos{at.x, at.y + 1, at.z}))) continue;
                return surface;
            }
    }
    return std::nullopt;
}

// ---- wings ---------------------------------------------------------------------------------------

void syncWings(Actor& entity) {
    auto& w = stateOf(entity).wings;
    Json  msg{
        {"entity", entity.getOrCreateUniqueID().rawID},
        {"state", std::string(tg::wingStateName(w.state))},
        {"left", w.sidestepLeft},
        {"double_jump_timer", w.doubleJumpTimer},
        {"sidestep_timer", w.sidestepTimer},
    };
    forTrackers(entity, [&](Player& q) { net::send(q, "tgmove.wings_state", msg); });
}

// ---- double jump -------------------------------------------------------------------------------

bool performDoubleJump(Player& player, bool vanilla) {
    auto& st = stateOf(player);
    tg::DoubleJumpState s;
    s.hasDoubleJump = st.hasDoubleJump;
    s.fallFlying    = player.isGliding();
    s.onClimbable   = player.onClimbableBlock();
    // a vanilla client's jump edge comes before the server applied its input: the edges' ground state
    s.onGround      = vanilla ? !st.airborne : player.isOnGround();
    s.swimming      = player.isSwimming();
    s.flying        = flying(player);
    s.inLiquid      = player.isInWater() || inLava(player);
    s.passenger     = player.getVehicle() != nullptr;
    if (!tg::canDoubleJump(s)) return false;

    Vec3         d = player.getPosDelta();
    tg::Velocity v{d.x, d.y, d.z};
    v   = tg::jumpFromGround(v, tg::jumpPower(effectAmplifier(player, kJumpBoostId)), player.isSprinting(), player.getRotation().y);
    tg::slimyJump(st.slimy); // LivingJumpEvent (cancelSlimySolesJump)
    if (st.slimy.doubleJumpBoostVelocity != 0) {
        v.y                              = tg::boostedJump(v.y, st.slimy.doubleJumpBoostVelocity);
        st.slimy.doubleJumpBoostVelocity = 0;
        sendSlimyBoost(player, 0);
    }
    Vec3 nv{static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
    // the server pushes the jump for every client: Bedrock's server-authoritative movement would
    // put a jump of the client's own back
    movement::applyMotion(player, nv);
    player.setFallDistance(0);
    float e = jumpExhaustionOf(player);
    if (active(player, Mod::EfficientEater)) e /= 2.0f; // efficient_eater: 2.0
    player.causeFoodExhaustion(e);
    playAt(player, "item.twilightforest.travellers_gear.double_jump", 1.5f, tg::doubleJumpPitch(voicePitch()));
    st.hasDoubleJump             = false;
    st.doubleJumpValidator.count = 0;
    st.safeFall                  = true;

    if (wearsWings(player)) {
        tg::startDoubleJump(st.wings);
        float body = player.getYBodyRotation() * static_cast<float>(3.14159265358979 / 180.0);
        Vec3  pos  = player.getFeetPos() + Vec3{std::sin(body) / 3.0f, 1.2f, -std::cos(body) / 3.0f};
        for (int i = 0; i < 10; ++i) {
            Vec3 vel{static_cast<float>((rand01() - 0.5) * 0.25), static_cast<float>((rand01() + 1) * -0.5), static_cast<float>((rand01() - 0.5) * 0.25)};
            fx::particle(player.getDimension(), "twilightforest:double_jump", pos, vel + nv);
        }
        syncWings(player);
    }
    log().info("tgmove: {} double jumped (vy {:.3f}{})", player.getRealName(), v.y, vanilla ? ", vanilla fallback" : "");
    return true;
}

// ---- sidestep ----------------------------------------------------------------------------------

bool tryPerformSidestep(Player& player, bool left) {
    auto&     st  = stateOf(player);
    long long now = gameTime();
    if (!active(player, Mod::SideStep) || !tg::sidestepReady(now, st.wings.lastSidestepTime, tg::kSidestepCooldown) || player.isGliding()
        || !player.isOnGround() || player.isSneaking())
        return false;
    auto push = tg::sidestepPush(player.getRotation().y, left);
    movement::applyMotion(player, player.getPosDelta() + Vec3{static_cast<float>(push.x), 0.0f, static_cast<float>(push.z)});
    playAt(player, "item.twilightforest.travellers_gear.side_step", 1.0f, voicePitch());
    tg::startSidestep(st.wings, left);
    syncWings(player);
    st.wings.lastSidestepTime        = now;
    st.wings.shouldPlayCooldownSound = true;
    log().info("tgmove: {} sidestepped {}", player.getRealName(), left ? "left" : "right");
    return true;
}

// ---- validation --------------------------------------------------------------------------------

tg::Abuse handleAbuse(Player& player, bool sidestep) {
    if (!validatorEnabled()) return tg::Abuse::Accept;
    auto& st      = stateOf(player);
    auto& v       = sidestep ? st.sidestepValidator : st.doubleJumpValidator;
    auto  verdict = tg::validate(v, player.mTickCount, player.isGliding());
    char const* what = sidestep ? "sidestep" : "double jump";
    if (verdict == tg::Abuse::Disconnect) {
        log().warn("tgmove: {} illegal {}: disconnected", player.getRealName(), what);
        if (!player.isSimulated()) player.disconnect(trFor(player, "multiplayer.disconnect.flying", "Flying is not enabled on this server"));
    } else if (verdict == tg::Abuse::SnapBack) {
        log().warn("tgmove: {} illegal {}", player.getRealName(), what);
        st.snapBackPending = true; // the validator below rejects the next move: F12 puts the player back
    }
    return verdict;
}

// ---- fall damage -------------------------------------------------------------------------------

bool onFall(Actor& entity, float& amount) {
    auto it = store().players.find(entity.getOrCreateUniqueID().rawID);
    bool slimy = activeOn(entity, Mod::SlimySoles);
    if (it == store().players.end() && !slimy) return true;
    auto& st = stateOf(entity);
    // Java's calculateFallDamage counts the safe fall distance, the double jump's +2 included
    float effective = amount;
    if (st.safeFall) effective -= static_cast<float>(tg::kDoubleJumpSafeFall);
    // slimy soles (reduceSlimySolesFallDamage): a landing that would hurt becomes a bounce
    if (tg::slimyCancelsFall(entity.isSneaking(), slimy, effective, st.slimy.forceBounce)) {
        double vy = std::min(st.fallVy, static_cast<double>(entity.getPosDelta().y));
        tg::slimyLand(st.slimy, vy, tg::kSlimySolesCoefficient);
        st.landedByDamage = true;
        sendSlimyBoost(entity, st.slimy.doubleJumpBoostVelocity);
        log().info("tgmove: {} landed on slimy soles (bounce {:.3f})", entity.isPlayer() ? static_cast<Player&>(entity).getRealName() : entity.getTypeName(), st.slimy.bounceVelocity);
        return false;
    }
    amount = effective;
    return amount > 0;
}

// ---- the tick ------------------------------------------------------------------------------------

namespace {

/// Efficient eater's half of a ground jump's exhaustion, once the game added it.
void flushRefund(Player& p) {
    auto it = store().players.find(p.getOrCreateUniqueID().rawID);
    if (it == store().players.end() || it->second.pendingRefund <= 0 || gameTime() < it->second.pendingRefundAt) return;
    refundExhaustion(p, it->second.pendingRefund);
    it->second.pendingRefund = 0;
}

/// The slimy soles bounce of any living entity (travellersBootsSlimySolesBounce) and the landing
/// of a forced bounce that did not hurt.
void tickSlimy(Actor& e, PlayerState& st, bool onGround, Vec3 delta) {
    bool slimy = activeOn(e, Mod::SlimySoles);
    if (onGround && !st.lastOnGround && !st.landedByDamage && slimy && st.slimy.forceBounce && !e.isSneaking()) {
        tg::slimyLand(st.slimy, st.fallVy, tg::kSlimySolesCoefficient);
        sendSlimyBoost(e, st.slimy.doubleJumpBoostVelocity);
    }
    st.landedByDamage = false;
    if (auto vy = tg::slimyBounce(st.slimy, delta.y)) {
        Vec3 pos = e.getFeetPos();
        fx::sound(e.getDimension(), "minecraft:entity.slime.jump", pos, 0.5f, 1.0f);
        int n = tg::slimyParticleCount(st.slimy.bounceVelocity);
        for (int i = 0; i < n; ++i) {
            std::normal_distribution<double> g(0.0, 1.0);
            Vec3 vel{static_cast<float>(g(store().rng) * 0.15), static_cast<float>(g(store().rng) * 0.15), static_cast<float>(g(store().rng) * 0.15)};
            fx::particle(e.getDimension(), "minecraft:block", pos, vel, 1, {{"block", "minecraft:slime"}});
        }
        Vec3 nv{delta.x, static_cast<float>(*vy), delta.z};
        if (e.isPlayer()) movement::applyMotion(static_cast<Player&>(e), nv);
        else e.mBuiltInComponents->mStateVectorComponent->mPosDelta = nv;
        log().info("tgmove: {} bounced on slimy soles (vy {:.3f})", e.isPlayer() ? static_cast<Player&>(e).getRealName() : e.getTypeName(), *vy);
    }
    if (!onGround) st.fallVy = delta.y;
}

/// The wing state of any wearer of the wings (determineWingState).
void tickWings(Actor& e, PlayerState& st, bool onGround, bool liquid, Vec3 delta, bool isFlying, bool sprinting) {
    st.walkSpeed = tg::walkAnimation(st.walkSpeed, std::sqrt(delta.x * delta.x + delta.z * delta.z));
    if (!wearsWings(e)) return;
    tg::WingInputs in;
    in.passenger    = e.getVehicle() != nullptr;
    in.swimming     = e.isSwimming();
    in.onGround     = onGround;
    in.inLiquid     = liquid;
    in.flying       = isFlying;
    in.fallDistance = e.getFallDistance();
    in.vy           = delta.y;
    in.sprinting    = sprinting;
    in.walkSpeed    = st.walkSpeed;
    if (tg::determineWingState(st.wings, in)) syncWings(e);
}

} // namespace

void tickPlayer(Player& player) {
    if (!player.isAlive()) return;
    auto&     st       = stateOf(player);
    long long now      = gameTime();
    bool      onGround = player.isOnGround();
    bool      inWater  = player.isInWater();
    bool      liquid   = inWater || inLava(player);
    Vec3      delta    = player.getPosDelta();
    Vec3      pos      = player.getFeetPos();
    auto&     ctx      = player.getEntityContext();

    // has_double_jump (tickMovementModifiers)
    if (auto r = tg::refreshDoubleJump(active(player, Mod::DoubleJump), onGround, liquid, player.onClimbableBlock()); r && *r != st.hasDoubleJump) {
        st.hasDoubleJump             = *r;
        st.doubleJumpValidator.count = 0;
        st.safeFall                  = false;
    }

    // gradual glide: off without the modifier; the fall distance a glide leaves
    bool glideActive = active(player, Mod::GradualGlide);
    if (!glideActive && st.gliding) {
        st.gliding = false;
        broadcastGlide(player, false);
    }
    // the server's copy of a gliding companion glides too (Bedrock's server-authoritative movement
    // simulates it from the client's input and corrects a client that falls slower than it)
    if (auto vy = tg::glide(delta.y, tg::kGradualGlideMultiplier, glideActive, st.gliding, player.isGliding())) {
        delta.y = static_cast<float>(*vy);
        player.mBuiltInComponents->mStateVectorComponent->mPosDelta = delta;
        player.setFallDistance(tg::glideFallDistance(*vy));
    }
    // the server's copy of a companion walks on water as its client does
    bool waterWalking = false;
    if (active(player, Mod::WaterWalk) && !player.isSneaking() && net::isCompanion(player)) {
        if (auto surface = waterSurface(player, pos, delta.y)) {
            delta.y = static_cast<float>(*surface - pos.y);
            player.mBuiltInComponents->mStateVectorComponent->mPosDelta = delta;
            if (!ctx.hasComponent<OnGroundFlagComponent>()) ctx.getRegistry().emplace<OnGroundFlagComponent>(ctx.mEntity);
            waterWalking = true;
        }
    }
    // swift swim (water movement efficiency +1 = Depth Strider III), and ground speed on the water
    applyDepthStrider(player, st, active(player, Mod::SwiftSwim) || waterWalking);

    // vanilla clients: sneak in the air = Slow Falling (the client config's default)
    bool vanillaGlide = glideActive && !net::isCompanion(player)
                     && tg::wantsGlide(true, player.isSneaking(), delta.y, onGround && !st.airborne) && !player.isGliding();
    bool wasSlowFalling = st.slowFalling;
    keepEffect(player, kSlowFallingId, 0, vanillaGlide, st.slowFalling, nullptr);
    if (st.slowFalling && !wasSlowFalling) log().info("tgmove: {} glides (Slow Falling)", player.getRealName());

    // sidestep cooldown sound (travellersWingsSidestepCooldownSound)
    if (active(player, Mod::SideStep) && tg::sidestepReady(now, st.wings.lastSidestepTime, tg::kSidestepCooldown) && st.wings.shouldPlayCooldownSound) {
        fx::soundTo(player, "item.twilightforest.travellers_gear.side_step_ready", 1.0f, voicePitch());
        st.wings.shouldPlayCooldownSound = false;
    }

    // high jump: Jump Boost II, hidden
    keepEffect(player, kJumpBoostId, tg::kHighJumpAmplifier, active(player, Mod::HighJump), st.highJump, &st.savedJumpBoost);

    // step up: +0.5 step height, off while crouching (disableHighStepWhileSneaking)
    setStep(player, st, active(player, Mod::StepUp) && !player.isSneaking());

    // unrestrained: no block slowdown (cobweb, berry bush, powder snow, honey, soul sand)
    applyImmunity(player, st, active(player, Mod::Unrestrained));

    // agile ranger: the item use slowdown of a bow x5 (the client mod does the same on its side)
    (void)applyAgileRanger(player, st.agileBase, st.agileSet);

    // straight ahead: movement speed x1.4 while the forward input is held (handleStraightAhead)
    {
        bool forward = false;
        if (auto in = ctx.tryGetComponent<MoveInputComponent>()) forward = in->mMove->y > 0;
        setStraightAhead(player, active(player, Mod::StraightAhead) && forward, st.straightAhead);
    }

    // slimy soles: the bounce
    tickSlimy(player, st, onGround, delta);

    // water walk splash (processWaterWalking -> waterWalkingSplashEffect)
    if (active(player, Mod::WaterWalk) && !player.isSneaking()) {
        BlockPos below{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y - 0.25)), static_cast<int>(std::floor(pos.z))};
        auto&    region = player.getDimensionBlockSource();
        auto     id     = game::blockIdAt(region, below);
        bool     water  = id == "minecraft:water" || id == "minecraft:flowing_water";
        double   sub    = water ? (below.y + tg::waterHeight(game::intState(region.getBlock(below), "liquid_depth").value_or(0))) - pos.y : 0;
        double   hs     = std::sqrt(delta.x * delta.x + delta.z * delta.z);
        if (water && sub > 0 && tg::waterStandable(sub, false) && tg::waterSplash(now, st.lastSplash, hs)) {
            st.lastSplash = now;
            Vec3 at{static_cast<float>(pos.x + (rand01() * 2 - 1) * 0.3), static_cast<float>(pos.y + tg::kWaterWalkMaxSubmerged), static_cast<float>(pos.z + (rand01() * 2 - 1) * 0.3)};
            fx::particle(player.getDimension(), "minecraft:splash", at, Vec3{-delta.x, 0.5f, -delta.z});
        }
    }

    // aquatic agility: oxygen bonus 3 (LivingEntity#decreaseAirSupply)
    if (auto br = ctx.tryGetComponent<BreathableComponent>()) {
        int air = br->mAirSupply;
        if (active(player, Mod::AquaticAgility) && st.lastAir >= 0 && air == st.lastAir - 1) {
            int r = std::uniform_int_distribution<int>(0, tg::kOxygenBonus)(store().rng);
            if (tg::keepsAir(tg::kOxygenBonus, r)) {
                br->mAirSupply = static_cast<short>(st.lastAir);
                air            = st.lastAir;
            }
        }
        st.lastAir = air;
    }

    // efficient eater: movement exhaustion / 2 (ReduceMovementFoodExhaustion over Player#checkMovementStatistics)
    if (st.hasLast && active(player, Mod::EfficientEater) && !player.isSimulated()) {
        double dx = pos.x - st.lastX, dy = pos.y - st.lastY, dz = pos.z - st.lastZ;
        double horiz = std::sqrt(dx * dx + dz * dz);
        float  e     = 0;
        if (player.isSwimming() || eyesInWater(player)) e = static_cast<float>(0.01 * std::sqrt(dx * dx + dy * dy + dz * dz));
        else if (inWater) e = static_cast<float>(0.01 * horiz); // wading: WALK_ON_WATER, horizontal
        else if (onGround && player.isSprinting()) e = static_cast<float>(0.1 * horiz);
        if (e < 1) refundExhaustion(player, e / 2.0f);
    }
    st.lastX = pos.x, st.lastY = pos.y, st.lastZ = pos.z, st.hasLast = true;

    // wing state (determineWingState), for the wings' model
    tickWings(player, st, onGround, liquid, delta, flying(player), player.isSprinting());
    st.lastOnGround = onGround;
}

void tickMob(Mob& mob) {
    if (!mob.isAlive()) return;
    auto& st       = stateOf(mob);
    bool  onGround = mob.isOnGround();
    Vec3  delta    = mob.getPosDelta();
    // travellersWingsGradualGlide: a mob glides whenever it falls (isGraduallyGliding = !(Player))
    if (auto vy = tg::glide(delta.y, tg::kGradualGlideMultiplier, activeOn(mob, Mod::GradualGlide), true, mob.isGliding())) {
        delta.y = static_cast<float>(*vy);
        mob.mBuiltInComponents->mStateVectorComponent->mPosDelta = delta;
        mob.setFallDistance(tg::glideFallDistance(*vy));
    }
    applyImmunity(mob, st, activeOn(mob, Mod::Unrestrained));
    tickSlimy(mob, st, onGround, delta);
    keepEffect(mob, kJumpBoostId, tg::kHighJumpAmplifier, activeOn(mob, Mod::HighJump), st.highJump, &st.savedJumpBoost);
    setStraightAhead(mob, activeOn(mob, Mod::StraightAhead), st.straightAhead); // travellersBootsStraightAhead
    tickWings(mob, st, onGround, mob.isInWater() || inLava(mob), delta, false, false);
    st.lastOnGround = onGround;
}

namespace {

/// Mobs wearing Travellers' gear (legs or feet), and the leash override marker of every mob.
void tickMobs(Level& lv) {
    auto&                            s = store();
    std::unordered_set<std::int64_t> seen;
    for (Actor* a : lv.getRuntimeActorList()) {
        if (!a || a->isPlayer() || !a->hasCategory(ActorCategory::Mob) || !a->isAlive()) continue;
        // leashed_pathfinder_override: dropped from a mob that can no longer be leashed
        // (EntityEvents#handleLeashPathingOverrides)
        if (a->hasTags() && a->hasTag(kLeashOverrideTag) && !a->getEntityContext().hasComponent<LeashableComponent>()) a->removeTag(kLeashOverrideTag);
        auto id    = a->getOrCreateUniqueID().rawID;
        bool wears = game::items::idOf(armor(*a, 2)).starts_with("twilightforest:travellers_")
                  || game::items::idOf(armor(*a, 3)).starts_with("twilightforest:travellers_");
        if (!wears) {
            if (s.mobs.contains(id)) forget(*a); // the gear came off: put everything back
            continue;
        }
        seen.insert(id);
        try {
            tickMob(static_cast<Mob&>(*a));
        } catch (std::exception const& e) {
            log().error("tgmove: tick of {} failed: {}", a->getTypeName(), e.what());
        }
    }
    // mobs that left the world
    for (auto it = s.mobs.begin(); it != s.mobs.end();) {
        if (seen.contains(*it)) {
            ++it;
            continue;
        }
        s.players.erase(*it);
        it = s.mobs.erase(it);
    }
}

} // namespace

void onInputEdge(Player& player, int edgeValue) {
    auto edge = static_cast<movement::Edge>(edgeValue);
    auto& st  = stateOf(player);
    switch (edge) {
    case movement::Edge::LeftGround: st.airborne = true; break;
    case movement::Edge::Landed: st.airborne = false; break;
    case movement::Edge::JumpPressed: {
        int last             = st.lastJumpPressTick;
        st.lastJumpPressTick = player.mTickCount;
        if (!st.airborne) {
            tg::slimyJump(st.slimy); // a normal jump (cancelSlimySolesJump)
            // efficient eater halves the jump's exhaustion too (ReduceMovementFoodExhaustion on
            // Player#jumpFromGround); the game adds it with the jump, so take half back next tick
            if (active(player, Mod::EfficientEater) && player.isOnGround() && !player.isInWater()) {
                st.pendingRefund  += jumpExhaustionOf(player) / 2.0f;
                st.pendingRefundAt = gameTime() + 2;
            }
            break;
        }
        // vanilla clients: a jump in the air is the double jump (the client mod sends its own);
        // a may-fly player pressing jump twice within 6 ticks starts flying (avoidCreativeFly)
        bool mayFly = player.getAbilities().getBool(AbilitiesIndex::MayFly);
        if (tg::avoidCreativeFly(mayFly, player.mTickCount, last)) break;
        if (!net::isCompanion(player) && active(player, Mod::DoubleJump)) (void)performDoubleJump(player, true);
        break;
    }
    default: break;
    }
}

namespace {

class TgMoveModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mTick     = bus.emplaceListener<ll::event::ServerLevelTickEvent>([](ll::event::ServerLevelTickEvent&) {
            auto* lv = level();
            if (!lv) return;
            std::vector<std::int64_t> ids;
            lv->forEachPlayer([&](Player& p) {
                ids.push_back(p.getOrCreateUniqueID().rawID);
                return true;
            });
            for (auto id : ids) {
                auto* p = lv->getPlayer(ActorUniqueID{id});
                if (!p) continue;
                try {
                    flushRefund(*p);
                    tickPlayer(*p);
                } catch (std::exception const& e) {
                    log().error("tgmove: tick of {} failed: {}", p->getRealName(), e.what());
                }
            }
            tickMobs(*lv);
        });
        mDisconnect = bus.emplaceListener<ll::event::PlayerDisconnectEvent>([](ll::event::PlayerDisconnectEvent& ev) { forget(ev.self()); });
        // a respawned player starts afresh
        mRespawn = bus.emplaceListener<ll::event::PlayerRespawnEvent>([](ll::event::PlayerRespawnEvent& ev) { forget(ev.self()); });

        net::on("tgmove.double_jump", [](Player& p, Json const&) {
            if (!performDoubleJump(p, false)) (void)handleAbuse(p, false);
        });
        net::on("tgmove.sidestep", [](Player& p, Json const& msg) {
            bool left = msg.value("left", false);
            if (!tryPerformSidestep(p, left)) (void)handleAbuse(p, true);
        });
        net::on("tgmove.gradual_glide", [](Player& p, Json const& msg) {
            auto& st   = stateOf(p);
            bool  want = msg.value("gliding", false);
            if (want == st.gliding) return;
            st.gliding = want;
            broadcastGlide(p, want);
        });
        movement::onInputEdge([](Player& p, movement::Edge e) { onInputEdge(p, static_cast<int>(e)); });
        auto snapBack = [](Player& p, Vec3 const&, Vec3 const&) {
            auto it = store().players.find(p.getOrCreateUniqueID().rawID);
            if (it == store().players.end() || !it->second.snapBackPending) return true;
            it->second.snapBackPending = false;
            return false;
        };
        movement::validate("tgmove.travellers_movement", snapBack);
        combat::onIncoming(combat::priority::Normal, [](combat::DamageInfo& d) {
            if (d.type != "minecraft:fall") return combat::Verdict::Pass;
            return onFall(d.victim, d.amount) ? combat::Verdict::Pass : combat::Verdict::Cancel;
        });
        // last_damage_armor_time (TravellersGearEvents#setLastDamageArmorTime): armour takes damage
        // from what it protects against; U11's auto repair reads it by name (F8's entity data).
        combat::onDealt(combat::priority::Normal, [](combat::DamageInfo const& d) {
            if (d.amount <= 0 || tags::has(tags::Registry::DamageType, "minecraft:bypasses_armor", d.type)) return;
            for (int s = 0; s < 4; ++s)
                if (!game::items::isEmpty(armor(d.victim, s)) && armor(d.victim, s).isDamageableItem()) {
                    entity::data(d.victim, kLastDamageArmorTimeKey) = gameTime();
                    break;
                }
        });
        // aquatic agility: player.submerged_mining_speed +4 multiply-total (x5 with the eyes under water)
        ext::mining::addModifier([](Player& p, Block const&) {
            return active(p, Mod::AquaticAgility) && eyesInWater(p) ? static_cast<float>(1.0 + tg::kSubmergedMiningBonus) : 1.0f;
        });
        log().info("tgmove: travellers movement modifiers installed");
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        bus.removeListener(mTick);
        bus.removeListener(mDisconnect);
        bus.removeListener(mRespawn);
        net::off("tgmove.double_jump");
        net::off("tgmove.sidestep");
        net::off("tgmove.gradual_glide");
        movement::removeValidator("tgmove.travellers_movement");
        // put back what the modifiers changed on everyone still in the world
        if (auto* lv = level()) {
            std::vector<std::int64_t> ids;
            for (auto const& [id, st] : store().players) ids.push_back(id);
            for (auto id : ids)
                if (auto* a = lv->fetchEntity(ActorUniqueID{id}, false)) forget(*a);
        }
        store().players.clear();
        store().mobs.clear();
    }

private:
    ll::event::ListenerPtr mTick;
    ll::event::ListenerPtr mDisconnect;
    ll::event::ListenerPtr mRespawn;
};

} // namespace

} // namespace gf::feature::tgmove

GF_REGISTER_MODULE(gf::feature::tgmove::TgMoveModule);
