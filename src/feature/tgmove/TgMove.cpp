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
// logic; the companion packets, the input edges of vanilla clients (F12's movement contract) and
// the fall damage (F10's combat contract) come in through the contracts.
#include "feature/tgmove/TgMove.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <random>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/entity/components/BlockMovementSlowdownImmunityComponent.h"
#include "mc/entity/components/MaxAutoStepComponent.h"
#include "mc/entity/components/MoveInputComponent.h"
#include "mc/entity/components/WaterWalkSpeedEnchantComponent.h"
#include "mc/entity/components_json_legacy/BreathableComponent.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/AbilitiesIndex.h"
#include "mc/world/actor/player/LayeredAbilities.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
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

#include "feature/combat/Combat.h"
#include "feature/ext/ui/Server.h"
#include "feature/fx/Fx.h"
#include "feature/gear/Gear.h"
#include "feature/movement/Movement.h"
#include "feature/net/Net.h"
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

struct Store {
    std::unordered_map<std::int64_t, PlayerState> players;
    std::optional<bool>                           forceValidator;
    std::mt19937                                  rng{std::random_device{}()};
};
Store& store() {
    static Store s;
    return s;
}

double rand01() { return std::uniform_real_distribution<double>(0.0, 1.0)(store().rng); }

Level* level() { return ll::service::getLevel().as_ptr(); }

int dimOf(Player const& p) { return static_cast<int>(p.getDimensionId()); }

ItemStack const& armor(Player const& p, int slot) { return p.getArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(slot)); }

bool wearsWings(Player const& p) { return game::items::idOf(armor(p, 2)) == kWings; }

bool inLava(Player& p) {
    auto id = game::blockIdAt(p.getDimensionBlockSource(), p.getFeetBlockPos());
    return id == "minecraft:lava" || id == "minecraft:flowing_lava";
}

bool flying(Player const& p) { return p.isFlying(); }

int effectAmplifier(Actor const& a, unsigned id) {
    auto const* e = a.getEffect(id);
    return e ? e->mAmplifier : -1;
}

/// Adds a hidden effect (no particles, no icon: MobEffectInstance(..., false, false, false)).
void addHiddenEffect(Actor& a, unsigned id, int amplifier, int ticks) {
    MobEffectInstance inst(id, EffectDuration{ticks});
    inst.mAmplifier     = amplifier;
    inst.mAmbient       = false;
    inst.mEffectVisible = false;
    a.addEffect(inst);
}

/// Our hidden effect of `id` / `amplifier` is on (and not a longer potion of the player's own).
bool ownsEffect(Actor const& a, unsigned id, int amplifier) {
    auto const* e = a.getEffect(id);
    return e && e->mAmplifier == amplifier && e->mDuration->mValue <= kEffectTicks && !e->mEffectVisible;
}

/// Keeps our hidden effect on while `on`, removes it once `on` ends.
void keepEffect(Actor& a, unsigned id, int amplifier, bool on, bool& applied) {
    if (on) {
        auto const* e = a.getEffect(id);
        if (e && e->mAmplifier > amplifier) return; // a stronger one of the player's own
        if (!e || e->mDuration->mValue <= kEffectTicks / 2) addHiddenEffect(a, id, amplifier, kEffectTicks);
        applied = true;
    } else if (applied) {
        if (ownsEffect(a, id, amplifier)) a.removeEffect(static_cast<int>(id));
        applied = false;
    }
}

/// Every companion within the tracking range of `p` (same dimension), `p` too when a companion.
template <class F>
void forTrackers(Player& p, F&& fn) {
    auto* lv = level();
    if (!lv) return;
    int  dim = dimOf(p);
    Vec3 at  = p.getFeetPos();
    std::vector<std::int64_t> ids;
    lv->forEachPlayer([&](Player& q) {
        if (dimOf(q) == dim && q.getFeetPos().distanceTo(at) <= kTrackRange && net::isCompanion(q)) ids.push_back(q.getOrCreateUniqueID().rawID);
        return true;
    });
    for (auto id : ids)
        if (auto* q = lv->getPlayer(ActorUniqueID{id})) fn(*q);
}

void playAt(Player& p, std::string const& event, float volume, float pitch) {
    fx::sound(p.getDimension(), event, p.getFeetPos(), volume, pitch);
}

void sendSlimyBoost(Player& p, double boost) {
    if (net::isCompanion(p)) net::send(p, "tgmove.slimy", {{"boost", boost}});
}

void broadcastGlide(Player& p, bool gliding) {
    Json msg{{"player", p.getOrCreateUniqueID().rawID}, {"gliding", gliding}};
    forTrackers(p, [&](Player& q) { net::send(q, "tgmove.gradual_glide", msg); });
}

/// Java's exhaustion of a jump (Player#jumpFromGround), through efficient eater's divisor.
void jumpExhaustion(Player& p) {
    float e = p.isSprinting() ? 0.2f : 0.05f;
    if (active(p, Mod::EfficientEater)) e /= 2.0f; // efficient_eater: 2.0
    p.causeFoodExhaustion(e);
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

PlayerState& stateOf(Player const& player) { return store().players[player.getOrCreateUniqueID().rawID]; }

void forget(Player const& player) { store().players.erase(player.getOrCreateUniqueID().rawID); }

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

// ---- wings ---------------------------------------------------------------------------------------

void syncWings(Player& player) {
    auto& w = stateOf(player).wings;
    Json  msg{
        {"entity", player.getOrCreateUniqueID().rawID},
        {"state", std::string(tg::wingStateName(w.state))},
        {"left", w.sidestepLeft},
        {"double_jump_timer", w.doubleJumpTimer},
        {"sidestep_timer", w.sidestepTimer},
    };
    forTrackers(player, [&](Player& q) { net::send(q, "tgmove.wings_state", msg); });
}

// ---- double jump -------------------------------------------------------------------------------

bool performDoubleJump(Player& player, bool sendMotion) {
    auto& st = stateOf(player);
    tg::DoubleJumpState s;
    s.hasDoubleJump = st.hasDoubleJump;
    s.fallFlying    = player.isGliding();
    s.onClimbable   = player.onClimbableBlock();
    // a vanilla client's jump edge comes before the server applied its input: the edges' ground state
    s.onGround      = sendMotion ? !st.airborne : player.isOnGround();
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
    if (sendMotion) movement::applyMotion(player, nv);
    player.setFallDistance(0);
    jumpExhaustion(player);
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
    log().info("tgmove: {} double jumped (vy {:.3f}{})", player.getRealName(), v.y, sendMotion ? ", vanilla fallback" : "");
    return true;
}

// ---- sidestep ----------------------------------------------------------------------------------

bool tryPerformSidestep(Player& player, bool left, bool sendMotion) {
    auto&     st  = stateOf(player);
    long long now = gameTime();
    if (!active(player, Mod::SideStep) || !tg::sidestepReady(now, st.wings.lastSidestepTime, tg::kSidestepCooldown) || player.isGliding()
        || !player.isOnGround() || player.isSneaking())
        return false;
    auto push = tg::sidestepPush(player.getRotation().y, left);
    if (sendMotion) movement::applyMotion(player, player.getPosDelta() + Vec3{static_cast<float>(push.x), 0.0f, static_cast<float>(push.z)});
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

bool onFall(Player& player, float& amount) {
    auto& st = stateOf(player);
    // slimy soles (reduceSlimySolesFallDamage): the landing becomes a bounce
    bool slimy = active(player, Mod::SlimySoles);
    if (tg::slimyCancelsFall(player.isSneaking(), slimy, amount, st.slimy.forceBounce)) {
        double vy = std::min(st.fallVy, static_cast<double>(player.getPosDelta().y));
        tg::slimyLand(st.slimy, vy, tg::kSlimySolesCoefficient);
        st.landedByDamage = true;
        sendSlimyBoost(player, st.slimy.doubleJumpBoostVelocity);
        log().info("tgmove: {} landed on slimy soles (bounce {:.3f})", player.getRealName(), st.slimy.bounceVelocity);
        return false;
    }
    // the double jump's safe fall distance +2
    if (st.safeFall) {
        amount -= static_cast<float>(tg::kDoubleJumpSafeFall);
        if (amount <= 0) return false;
    }
    return true;
}

// ---- the tick ------------------------------------------------------------------------------------

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
    if (glideActive && st.gliding && delta.y < 0 && !player.isGliding()) player.setFallDistance(tg::glideFallDistance(delta.y));
    // vanilla clients: sneak in the air = Slow Falling (the client config's default)
    bool vanillaGlide = glideActive && !net::isCompanion(player)
                     && tg::wantsGlide(true, player.isSneaking(), delta.y, onGround) && !player.isGliding();
    bool wasSlowFalling = st.slowFalling;
    keepEffect(player, kSlowFallingId, 0, vanillaGlide, st.slowFalling);
    if (st.slowFalling && !wasSlowFalling) log().info("tgmove: {} glides (Slow Falling)", player.getRealName());

    // sidestep cooldown sound (travellersWingsSidestepCooldownSound)
    if (active(player, Mod::SideStep) && tg::sidestepReady(now, st.wings.lastSidestepTime, tg::kSidestepCooldown) && st.wings.shouldPlayCooldownSound) {
        fx::soundTo(player, "item.twilightforest.travellers_gear.side_step_ready", 1.0f, voicePitch());
        st.wings.shouldPlayCooldownSound = false;
    }

    // high jump: Jump Boost II, hidden
    keepEffect(player, kJumpBoostId, tg::kHighJumpAmplifier, active(player, Mod::HighJump), st.highJump);

    // step up: +0.5 step height, off while crouching (disableHighStepWhileSneaking)
    if (auto step = ctx.tryGetComponent<MaxAutoStepComponent>()) {
        float cur = step->mValue;
        if (!st.stepBase || cur != st.stepSet) st.stepBase = cur; // the game's own value changed it
        bool  on  = active(player, Mod::StepUp) && !player.isSneaking();
        float want = *st.stepBase + (on ? static_cast<float>(tg::kStepUp) : 0.0f);
        if (cur != want) step->mValue = want;
        st.stepSet = want;
    }

    // unrestrained: no block slowdown (cobweb, berry bush, powder snow, honey, soul sand)
    bool unrestrained = active(player, Mod::Unrestrained);
    if (unrestrained && !st.immunity) {
        auto& imm = ctx.getOrAddComponent<BlockMovementSlowdownImmunityComponent>();
        for (char const* b : {"minecraft:web", "minecraft:sweet_berry_bush", "minecraft:powder_snow", "minecraft:honey_block", "minecraft:soul_sand"})
            imm.mBlocks->emplace_back(HashedString(b));
        st.immunity = true;
    } else if (!unrestrained && st.immunity) {
        ctx.removeComponent<BlockMovementSlowdownImmunityComponent>();
        st.immunity = false;
    }

    // swift swim: water movement efficiency +1 (Depth Strider III)
    if (active(player, Mod::SwiftSwim)) {
        auto& ds = ctx.getOrAddComponent<WaterWalkSpeedEnchantComponent>();
        if (ds.mValue < tg::kSwiftSwimDepthStrider) ds.mValue = tg::kSwiftSwimDepthStrider;
    }

    // straight ahead: movement speed x1.4 while moving forward
    {
        bool forward = false;
        if (auto in = ctx.tryGetComponent<MoveInputComponent>()) forward = in->mMove->y > 0;
        if (!forward) {
            float yaw = player.getRotation().y * static_cast<float>(3.14159265358979 / 180.0);
            forward   = (-std::sin(yaw) * delta.x + std::cos(yaw) * delta.z) > 0.01f;
        }
        bool want = active(player, Mod::StraightAhead) && forward;
        if (want != st.straightAhead) {
            auto attr = player.getMutableAttribute(SharedAttributes::MOVEMENT_SPEED());
            if (auto* inst = attr.mInstance->mPtr) {
                auto uuid = mce::UUID::fromStringHash(kStraightName);
                if (want) {
                    AttributeModifier mod(uuid, kStraightName, static_cast<float>(tg::kStraightAheadMultiplier - 1), AttributeModifierOperation::OperationMultiplyTotal, AttributeOperands::OperandCurrent, false);
                    inst->addModifier(mod, attr.mContext);
                } else {
                    inst->removeModifier(uuid, attr.mContext);
                }
                st.straightAhead = want;
            }
        }
    }

    // slimy soles: a landing without damage while the forced bounce lasts; the bounce itself
    bool slimy = active(player, Mod::SlimySoles);
    if (onGround && !st.lastOnGround && !st.landedByDamage && slimy && st.slimy.forceBounce && !player.isSneaking()) {
        tg::slimyLand(st.slimy, st.fallVy, tg::kSlimySolesCoefficient);
        sendSlimyBoost(player, st.slimy.doubleJumpBoostVelocity);
    }
    st.landedByDamage = false;
    if (auto vy = tg::slimyBounce(st.slimy, delta.y)) {
        fx::sound(player.getDimension(), "minecraft:entity.slime.jump", pos, 0.5f, 1.0f);
        int n = tg::slimyParticleCount(st.slimy.bounceVelocity);
        for (int i = 0; i < n; ++i) {
            std::normal_distribution<double> g(0.0, 1.0);
            Vec3 vel{static_cast<float>(g(store().rng) * 0.15), static_cast<float>(g(store().rng) * 0.15), static_cast<float>(g(store().rng) * 0.15)};
            fx::particle(player.getDimension(), "minecraft:block", pos, vel, 1, {{"block", "minecraft:slime"}});
        }
        movement::applyMotion(player, Vec3{delta.x, static_cast<float>(*vy), delta.z});
        log().info("tgmove: {} bounced on slimy soles (vy {:.3f})", player.getRealName(), *vy);
    }
    if (!onGround) st.fallVy = delta.y;

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

    // efficient eater: movement exhaustion / 2 (ReduceMovementFoodExhaustion)
    if (st.hasLast && active(player, Mod::EfficientEater) && !player.isSimulated()) {
        double dx = pos.x - st.lastX, dy = pos.y - st.lastY, dz = pos.z - st.lastZ;
        double horiz = std::sqrt(dx * dx + dz * dz);
        float  e     = 0;
        if (player.isSwimming() || inWater) e = static_cast<float>(0.01 * std::sqrt(dx * dx + dy * dy + dz * dz));
        else if (onGround && player.isSprinting()) e = static_cast<float>(0.1 * horiz);
        if (e > 0 && e < 1) player.causeFoodExhaustion(-e / 2.0f); // remove the half the divisor saves
    }
    st.lastX = pos.x, st.lastY = pos.y, st.lastZ = pos.z, st.hasLast = true;

    // wing state (determineWingState), for the wings' model
    st.walkSpeed = tg::walkAnimation(st.walkSpeed, std::sqrt(delta.x * delta.x + delta.z * delta.z));
    if (wearsWings(player)) {
        tg::WingInputs in;
        in.passenger    = player.getVehicle() != nullptr;
        in.swimming     = player.isSwimming();
        in.onGround     = onGround;
        in.inLiquid     = liquid;
        in.flying       = flying(player);
        in.fallDistance = player.getFallDistance();
        in.vy           = delta.y;
        in.sprinting    = player.isSprinting();
        in.walkSpeed    = st.walkSpeed;
        if (tg::determineWingState(st.wings, in)) syncWings(player);
    }
    st.lastOnGround = onGround;
}

namespace {

void onEdge(Player& player, movement::Edge edge) {
    auto& st = stateOf(player);
    switch (edge) {
    case movement::Edge::LeftGround: st.airborne = true; break;
    case movement::Edge::Landed: st.airborne = false; break;
    case movement::Edge::JumpPressed:
        if (!st.airborne) {
            tg::slimyJump(st.slimy); // a normal jump (cancelSlimySolesJump)
            break;
        }
        // vanilla clients: a jump in the air is the double jump (the client mod sends its own)
        if (!net::isCompanion(player) && active(player, Mod::DoubleJump)) (void)performDoubleJump(player, true);
        break;
    default: break;
    }
}

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
                    tickPlayer(*p);
                } catch (std::exception const& e) {
                    log().error("tgmove: tick of {} failed: {}", p->getRealName(), e.what());
                }
            }
        });
        mDisconnect = bus.emplaceListener<ll::event::PlayerDisconnectEvent>([](ll::event::PlayerDisconnectEvent& ev) { forget(ev.self()); });

        net::on("tgmove.double_jump", [](Player& p, Json const&) {
            if (!performDoubleJump(p, false)) (void)handleAbuse(p, false);
        });
        net::on("tgmove.sidestep", [](Player& p, Json const& msg) {
            bool left = msg.value("left", false);
            if (!tryPerformSidestep(p, left, false)) (void)handleAbuse(p, true);
        });
        net::on("tgmove.gradual_glide", [](Player& p, Json const& msg) {
            auto& st   = stateOf(p);
            bool  want = msg.value("gliding", false);
            if (want == st.gliding) return;
            st.gliding = want;
            broadcastGlide(p, want);
        });
        movement::onInputEdge(&onEdge);
        auto snapBack = [](Player& p, Vec3 const&, Vec3 const&) {
            auto it = store().players.find(p.getOrCreateUniqueID().rawID);
            if (it == store().players.end() || !it->second.snapBackPending) return true;
            it->second.snapBackPending = false;
            return false;
        };
        movement::validate("tgmove.travellers_movement", snapBack);
        combat::onIncoming(combat::priority::Normal, [](combat::DamageInfo& d) {
            if (d.type != "minecraft:fall" || !d.victim.isPlayer()) return combat::Verdict::Pass;
            auto& p = static_cast<Player&>(d.victim);
            return onFall(p, d.amount) ? combat::Verdict::Pass : combat::Verdict::Cancel;
        });
        // last_damage_armor_time (TravellersGearEvents#setLastDamageArmorTime): armour takes damage
        // from what it protects against; U11's auto repair reads it by name.
        combat::onDealt(combat::priority::Normal, [](combat::DamageInfo const& d) {
            if (!d.victim.isPlayer() || d.amount <= 0) return;
            auto& p = static_cast<Player&>(d.victim);
            for (int s = 0; s < 4; ++s)
                if (!game::items::isEmpty(armor(p, s)) && armor(p, s).isDamageableItem()) {
                    stateOf(p).lastDamageArmorTime = gameTime();
                    break;
                }
        });
        log().info("tgmove: travellers movement modifiers installed");
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        bus.removeListener(mTick);
        bus.removeListener(mDisconnect);
        net::off("tgmove.double_jump");
        net::off("tgmove.sidestep");
        net::off("tgmove.gradual_glide");
        movement::removeValidator("tgmove.travellers_movement");
        store().players.clear();
    }

private:
    ll::event::ListenerPtr mTick;
    ll::event::ListenerPtr mDisconnect;
};

} // namespace

} // namespace gf::feature::tgmove

GF_REGISTER_MODULE(gf::feature::tgmove::TgMoveModule);
