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
#include "mc/entity/components_json_legacy/LeashablePreset.h"
#include "mc/entity/utilities/leashable_utility/LeashableUtility.h"
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
#include "mc/world/level/block/BlockType.h"
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
constexpr int      kMobScanPeriod  = 20; ///< ticks between looks for mobs that put Travellers' gear on
constexpr float    kLeashFreeDistance = 6.0f; ///< PathfinderMob#tickLeash: no stay-close pathing within 6 blocks with the override
constexpr char     kWings[]        = "twilightforest:travellers_wings";
constexpr char     kStraightName[] = "twilightforest:travellers_gear.boots_straight_ahead";
// the slowdowns BlockMovementSlowdownImmunityComponent covers (EntityInsideSystemImpl: cobweb, powder
// snow; the berry bush's stuck speed); soul sand and honey are applyUnrestrained's
constexpr char const* kSlowdownBlocks[] = {"minecraft:web", "minecraft:sweet_berry_bush", "minecraft:powder_snow"};

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

double blockFriction(Block const& block) {
    float f = block.getBlockType().mFriction;
    return f > 0 && f < 1.5f ? static_cast<double>(f) : tg::kUnrestrainedFriction;
}

Vec2 agileRangerInput(Vec2 const& move) {
    // Java: the use slowdown 0.2, then x 5 = the input as it was; Bedrock's slowdown is in the input
    // (measured: 0.122 of a key's 1), so TF's input is the input without it, at most length 1
    Vec2  tf{static_cast<float>(move.x / kBedrockItemUseSlowdown), static_cast<float>(move.y / kBedrockItemUseSlowdown)};
    float l2 = tf.x * tf.x + tf.y * tf.y;
    if (l2 > 1.0f) {
        float l = std::sqrt(l2);
        tf.x /= l, tf.y /= l;
    }
    return tf;
}

Vec3 inputDelta(Actor& a, Vec2 const& engineMove, Vec2 const& tfMove, bool onGround, bool sprinting) {
    double scale = sprinting ? 0.026 : 0.02; // LivingEntity#getFlyingSpeed of players
    if (onGround) {
        Vec3     feet = a.getFeetPos();
        BlockPos below{static_cast<int>(std::floor(feet.x)), static_cast<int>(std::floor(feet.y - 0.5000001)), static_cast<int>(std::floor(feet.z))};
        float    speed = 0.1f;
        if (auto attr = a.getAttribute(SharedAttributes::MOVEMENT_SPEED()); attr.mPtr) speed = attr.mPtr->mCurrentValue;
        scale = speed * tg::groundAccelFactor(blockFriction(a.getDimensionBlockSource().getBlock(below)));
    }
    double yaw = a.getRotation().y;
    auto   e   = tg::inputAcceleration(engineMove.x, engineMove.y, yaw, scale);
    auto   t   = tg::inputAcceleration(tfMove.x, tfMove.y, yaw, scale);
    return Vec3{static_cast<float>(t.x - e.x), 0.0f, static_cast<float>(t.z - e.z)};
}

EngineSlowdown engineSlowdown(std::string const& blockId, double vy, bool steppingCarefully) {
    // measured (tgmove.unrestrained_blocks): Bedrock's slime and honey blocks multiply a ground
    // step's velocity by Java's SlimeBlock#stepOn factor (0.4 + |vy| x 0.2) on top of their friction
    // 0.8; soul sand leaves a slide alone and slows the walk (the input acceleration)
    if (blockId == "minecraft:slime" || blockId == "minecraft:honey_block") return {tg::slimeStepFactor(vy, steppingCarefully), 1.0};
    if (blockId == "minecraft:soul_sand") return {1.0, kSoulSandAccel};
    return {};
}

Vec3 applyUnrestrained(Actor& a, PlayerState& st, bool on, Vec3 v, bool onGround, bool steppingCarefully, Vec2 const* move) {
    auto&       region = a.getDimensionBlockSource();
    Vec3        feet   = a.getFeetPos();
    // Entity#getBlockPosBelowThatAffectsMyMovement
    BlockPos    below{static_cast<int>(std::floor(feet.x)), static_cast<int>(std::floor(feet.y - 0.5000001)), static_cast<int>(std::floor(feet.z))};
    Block const& block = region.getBlock(below);
    std::string id     = game::blockId(block);
    // the height a jump off honey lost last step was made up in it: its velocity comes off again
    if (st.unrJumpLost != 0) {
        v.y            = static_cast<float>(v.y - st.unrJumpLost * 0.98);
        st.unrJumpLost = 0;
    }
    // resetFactorWithUnrestrained (getBlockJumpFactor): the engine's jump off honey was weaker
    if (on && st.unrHoney && !onGround && v.y > 0 && st.unrVyPre <= 0) {
        double power       = tg::jumpPower(effectAmplifier(a, kJumpBoostId));
        double enginePower = v.y / 0.98 + tg::kGravity;
        // only the engine's honey jump (measured: 0.6 of the power on the server copy, 0.74 as the client
        // reads its velocity), not a bounce or a push
        double ratio       = power > 0 ? enginePower / power : 0;
        auto   j           = ratio > kHoneyJumpFactor - 0.1 && ratio < 0.85 ? tg::unrestrainedJump(v.y, power) : std::nullopt;
        if (j) {
            v.y            = static_cast<float>(j->vy);
            st.unrJumpLost = j->lost;
        }
    }
    // stopBouncing: the engine bounced a walk on slime in the last step
    if (on && st.unrSlime && !steppingCarefully)
        if (auto vy = tg::slimeBounceStop(st.unrVyPre, v.y)) v.y = static_cast<float>(*vy);
    // without the input (mobs, simulated players): the input acceleration of the last ground step
    float speed = 0.1f;
    if (auto attr = a.getAttribute(SharedAttributes::MOVEMENT_SPEED()); attr.mPtr) speed = attr.mPtr->mCurrentValue;
    if (!move) {
        tg::Velocity raw{};
        bool         have = false;
        if (st.unrLastDrag > 0) {
            double k = tg::groundAccelFactor(st.unrLastFriction) * st.unrLastAccelSlow;
            if (k > 0) {
                raw  = {(v.x / st.unrLastDrag - st.unrStepX) / k, 0, (v.z / st.unrLastDrag - st.unrStepZ) / k};
                have = true;
            }
        }
        // a push (knockback, a motion packet) looks like one step's acceleration: only what the last
        // two steps agree on, no more than the movement speed, is the input's
        double len = std::sqrt(raw.x * raw.x + raw.z * raw.z);
        double dx = raw.x - st.unrAccelRaw.x, dz = raw.z - st.unrAccelRaw.z;
        bool   steady = have && st.unrAccelRawValid && std::sqrt(dx * dx + dz * dz) <= 0.25 * len + 0.003 && len <= 1.5 * speed;
        st.unrAccel         = steady ? raw : tg::Velocity{};
        st.unrAccelRaw      = raw;
        st.unrAccelRawValid = have;
    }
    tg::Velocity acc = move ? tg::inputAcceleration(move->x, move->y, a.getRotation().y, speed) : st.unrAccel;
    // standing on a liquid (water walking): no block friction of its own to undo
    bool   liquidBelow = id == "minecraft:water" || id == "minecraft:flowing_water" || id == "minecraft:lava" || id == "minecraft:flowing_lava";
    double f           = liquidBelow ? tg::kUnrestrainedFriction : blockFriction(block);
    auto   slow        = liquidBelow ? EngineSlowdown{} : engineSlowdown(id, v.y, steppingCarefully);
    auto   step = tg::unrestrainedStep({v.x, v.y, v.z}, st.unrRescale, acc, f, slow.drag, onGround, on, slow.accel);
    st.unrRescale = step.nextRescale;
    st.unrSlime   = on && onGround && id == "minecraft:slime";
    st.unrHoney   = on && onGround && id == "minecraft:honey_block";
    st.unrVyPre   = v.y;
    Vec3 out{static_cast<float>(step.velocity.x), static_cast<float>(step.velocity.y), static_cast<float>(step.velocity.z)};
    st.unrStepX         = out.x;
    st.unrStepZ         = out.z;
    st.unrLastDrag      = on && onGround ? f * 0.91 * slow.drag : 0.0;
    st.unrLastFriction  = f;
    st.unrLastAccelSlow = on && onGround ? slow.accel : 1.0;
    return out;
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
        (void)applyAgileRanger(p, st.agileBase, st.agileSet, true); // off: the slowdown before comes back
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

namespace {
constexpr Mod kAllMods[] = {Mod::DoubleJump, Mod::SideStep, Mod::GradualGlide, Mod::HighJump, Mod::AgileRanger, Mod::StepUp, Mod::StraightAhead,
                            Mod::SlimySoles, Mod::Unrestrained, Mod::WaterWalk, Mod::SwiftSwim, Mod::AquaticAgility, Mod::EfficientEater};
} // namespace

unsigned modMask(Actor const& entity) {
    // TravellersModifier#isActive: the stack in the modifier's equipment slot group, not a spectator
    if (entity.isPlayer() && static_cast<Player const&>(entity).isSpectator()) return 0;
    unsigned mask = 0;
    for (int slot = 0; slot < 4; ++slot) {
        auto const& stack = armor(entity, slot);
        if (game::items::isEmpty(stack) || brokenGear(stack)) continue;
        auto mods = gear::modifiers(stack);
        if (mods.empty()) continue;
        for (Mod m : kAllMods) {
            if (modSlot(m) != slot) continue;
            auto it = mods.find(modId(m));
            if (it != mods.end() && it->second > 0) mask |= 1u << static_cast<unsigned>(m);
        }
    }
    return mask;
}

bool active(Player const& player, Mod m) { return has(modMask(player), m); }

bool activeOn(Actor const& entity, Mod m) { return has(modMask(entity), m); }

bool agileRangerItem(ItemStack const& stack) {
    auto id = game::items::idOf(stack);
    if (id.empty()) return false;
    bool legal = id == "minecraft:bow" || id == "minecraft:crossbow" || id == "twilightforest:triple_bow" || id == "twilightforest:seeker_bow"
              || id == "twilightforest:ice_bow" || id == "twilightforest:ender_bow"
              || tags::has(tags::Registry::Item, "twilightforest:travellers_agile_ranger_whitelisted", id);
    return legal && !tags::has(tags::Registry::Item, "twilightforest:travellers_agile_ranger_blacklisted", id);
}

bool applyAgileRanger(Player& player, std::optional<float>& base, float& set, bool forceOff) {
    // handleAgileRanger: using a projectile weapon, not riding -> the input x5, which cancels the
    // item use slowdown (Java's 0.2) up to full speed; Bedrock applies the slowdown from this component
    bool on = !forceOff && active(player, Mod::AgileRanger) && player.getItemUseDuration() > 0 && player.getVehicle() == nullptr
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

void onDoubleJumpPacket(Player& player) {
    auto& st = stateOf(player);
    if (performDoubleJump(player, false)) {
        st.pendingJumpTries = 0;
        return;
    }
    // Java's move packets come before PerformDoubleJumpPacket; Bedrock's auth input of the same tick
    // can come after it: try again once the server copy has moved
    st.pendingJumpTries = kPacketRetryTicks;
}

void onSidestepPacket(Player& player, bool left) {
    auto& st = stateOf(player);
    if (tryPerformSidestep(player, left)) {
        st.pendingSidestepTries = 0;
        return;
    }
    st.pendingSidestepTries = kPacketRetryTicks;
    st.pendingSidestepLeft  = left;
}

void processPending(Player& player) {
    auto it = store().players.find(player.getOrCreateUniqueID().rawID);
    if (it == store().players.end()) return;
    auto& st = it->second;
    if (st.pendingJumpTries > 0) {
        if (performDoubleJump(player, false)) st.pendingJumpTries = 0;
        else if (--st.pendingJumpTries == 0) (void)handleAbuse(player, false);
    }
    if (st.pendingSidestepTries > 0) {
        if (tryPerformSidestep(player, st.pendingSidestepLeft)) st.pendingSidestepTries = 0;
        else if (--st.pendingSidestepTries == 0) (void)handleAbuse(player, true);
    }
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

float exhaustionOf(Player const& p) {
    auto ex = p.getAttribute(Player::EXHAUSTION());
    return ex.mPtr ? ex.mPtr->mCurrentValue : 0.0f;
}

/// Efficient eater's half of a ground jump's exhaustion, once the game added it.
void flushRefund(Player& p) {
    auto it = store().players.find(p.getOrCreateUniqueID().rawID);
    if (it == store().players.end() || it->second.pendingRefund <= 0 || gameTime() < it->second.pendingRefundAt) return;
    float before = exhaustionOf(p);
    refundExhaustion(p, it->second.pendingRefund);
    // the movement measure of tickPlayer does not see this refund as exhaustion the game took off
    if (it->second.lastExhaustion >= 0) it->second.lastExhaustion -= before - exhaustionOf(p);
    it->second.pendingRefund = 0;
}

/// The slimy soles bounce of any living entity (travellersBootsSlimySolesBounce) and the landing
/// of a forced bounce that did not hurt.
void tickSlimy(Actor& e, PlayerState& st, bool onGround, Vec3 delta, bool slimy) {
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

void setPosDelta(Actor& a, Vec3 const& v) { a.mBuiltInComponents->mStateVectorComponent->mPosDelta = v; }

} // namespace

void tickPlayer(Player& player) {
    if (!player.isAlive()) return;
    auto&          st       = stateOf(player);
    long long      now      = gameTime();
    bool           onGround = player.isOnGround();
    bool           inWater  = player.isInWater();
    bool           liquid   = inWater || inLava(player);
    Vec3           delta    = player.getPosDelta();
    Vec3           pos      = player.getFeetPos();
    auto&          ctx      = player.getEntityContext();
    unsigned const mask     = modMask(player); // one gear lookup per slot and tick
    bool const     companion = net::isCompanion(player);
    // the server copy simulates what the companion's client simulates (selftests: simulated players)
    bool const simulatesClient = companion || player.isSimulated();
    auto       in              = ctx.tryGetComponent<MoveInputComponent>();

    // has_double_jump (tickMovementModifiers)
    if (auto r = tg::refreshDoubleJump(has(mask, Mod::DoubleJump), onGround, liquid, player.onClimbableBlock()); r && *r != st.hasDoubleJump) {
        st.hasDoubleJump             = *r;
        st.doubleJumpValidator.count = 0;
        st.safeFall                  = false;
    }

    // gradual glide: off without the modifier; the fall distance a glide leaves
    bool glideActive = has(mask, Mod::GradualGlide);
    if (!glideActive && st.gliding) {
        st.gliding = false;
        broadcastGlide(player, false);
    }
    // the server's copy of a gliding companion glides too (Bedrock's server-authoritative movement
    // simulates it from the client's input and corrects a client that falls slower than it)
    if (auto vy = tg::glide(delta.y, tg::kGradualGlideMultiplier, glideActive, st.gliding, player.isGliding())) {
        delta.y = static_cast<float>(*vy);
        setPosDelta(player, delta);
        player.setFallDistance(tg::glideFallDistance(*vy));
    }
    // the server's copy of a companion walks on water as its client does
    bool waterWalking = false;
    if (has(mask, Mod::WaterWalk) && !player.isSneaking() && companion) {
        if (auto surface = waterSurface(player, pos, delta.y)) {
            delta.y = static_cast<float>(*surface - pos.y);
            setPosDelta(player, delta);
            if (!ctx.hasComponent<OnGroundFlagComponent>()) ctx.getRegistry().emplace<OnGroundFlagComponent>(ctx.mEntity);
            waterWalking = true;
            onGround     = true;
        }
    }
    // swift swim (water movement efficiency +1 = Depth Strider III), and ground speed on the water
    applyDepthStrider(player, st, has(mask, Mod::SwiftSwim) || waterWalking);

    // vanilla clients: sneak in the air = Slow Falling (the client config's default)
    bool vanillaGlide = glideActive && !companion && tg::wantsGlide(true, player.isSneaking(), delta.y, onGround && !st.airborne) && !player.isGliding();
    bool wasSlowFalling = st.slowFalling;
    keepEffect(player, kSlowFallingId, 0, vanillaGlide, st.slowFalling, nullptr);
    if (st.slowFalling && !wasSlowFalling) log().info("tgmove: {} glides (Slow Falling)", player.getRealName());

    // sidestep cooldown sound (travellersWingsSidestepCooldownSound)
    if (has(mask, Mod::SideStep) && tg::sidestepReady(now, st.wings.lastSidestepTime, tg::kSidestepCooldown) && st.wings.shouldPlayCooldownSound) {
        fx::soundTo(player, "item.twilightforest.travellers_gear.side_step_ready", 1.0f, voicePitch());
        st.wings.shouldPlayCooldownSound = false;
    }

    // high jump: Jump Boost II, hidden
    keepEffect(player, kJumpBoostId, tg::kHighJumpAmplifier, has(mask, Mod::HighJump), st.highJump, &st.savedJumpBoost);

    // step up: +0.5 step height, off while crouching (disableHighStepWhileSneaking)
    setStep(player, st, has(mask, Mod::StepUp) && !player.isSneaking());

    // unrestrained: no block slowdown (cobweb, berry bush, powder snow, honey, soul sand) ...
    bool unrestrained = has(mask, Mod::Unrestrained);
    applyImmunity(player, st, unrestrained);
    // ... and friction 0.6, no slime step-on slowdown or walking bounce on the ground (the server
    // copy of a companion as its client; a vanilla client keeps the engine's friction, which its own
    // simulation has too)
    if (simulatesClient && (unrestrained || st.unrRescale != 1.0)) {
        Vec2 move{};
        if (in) move = in->mMove;
        // simulated players walk without an input of their own, and a server input that reads 0 may
        // not carry the auth input: then the estimate of mobs (the acceleration the steps agree on)
        bool useInput = !player.isSimulated() && (move.x * move.x + move.y * move.y) > 1e-8f;
        Vec3 nv       = applyUnrestrained(player, st, unrestrained, delta, onGround, player.isSneaking(), useInput ? &move : nullptr);
        if (nv != delta) {
            delta = nv;
            setPosDelta(player, delta);
        }
    }

    // the input changes of the companion's client (straight ahead's strafe / 1.4, the glide's
    // unsneaked input) as the velocity they make, as the client adds them
    if (companion && in) {
        Vec2 const move = in->mMove;
        Vec2       tf   = move;
        if (st.straightAhead) {
            auto s2 = tg::straightAheadInput(move.x, move.y, tg::kStraightAheadMultiplier);
            tf      = Vec2{s2.strafe, s2.forward};
        }
        if (has(mask, Mod::AgileRanger) && player.getItemUseDuration() > 0 && player.getVehicle() == nullptr && agileRangerItem(player.getCarriedItem()))
        {
            tf = agileRangerInput(tf); // handleAgileRanger
            if (!st.loggedAgile && (move.x != 0 || move.y != 0)) {
                st.loggedAgile = true;
                log().info("tgmove: {} agile ranger, server input length while drawing {:.3f}", player.getRealName(), std::sqrt(move.x * move.x + move.y * move.y));
            }
        }
        if (st.gliding && player.isSneaking() && !onGround) tg::unsneakImpulse(tf.y, tf.x, false);
        if (tf.x != move.x || tf.y != move.y) {
            delta = delta + inputDelta(player, move, tf, onGround, player.isSprinting());
            setPosDelta(player, delta);
        }
    }

    // agile ranger: the item use slowdown of a bow x5 (the client mod does the same on its side)
    (void)applyAgileRanger(player, st.agileBase, st.agileSet);

    // straight ahead: movement speed x1.4 while the forward input is held (handleStraightAhead)
    setStraightAhead(player, has(mask, Mod::StraightAhead) && in && in->mMove->y > 0, st.straightAhead);

    // slimy soles: the bounce
    tickSlimy(player, st, onGround, delta, has(mask, Mod::SlimySoles));

    // water walk splash (processWaterWalking -> waterWalkingSplashEffect): only while the server
    // copy stands on the water
    if (waterWalking) {
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
        if (has(mask, Mod::AquaticAgility) && st.lastAir >= 0 && air == st.lastAir - 1) {
            int r = std::uniform_int_distribution<int>(0, tg::kOxygenBonus)(store().rng);
            if (tg::keepsAir(tg::kOxygenBonus, r)) {
                br->mAirSupply = static_cast<short>(st.lastAir);
                air            = st.lastAir;
            }
        }
        st.lastAir = air;
    }

    // efficient eater: movement exhaustion / 2 (ReduceMovementFoodExhaustion over Player#checkMovementStatistics):
    // half of Java's movement exhaustion of the move, at most half of what the game added this tick
    // (other sources keep theirs); a teleport or a push of more than a block is no movement
    if (has(mask, Mod::EfficientEater) && !player.isSimulated()) {
        float exh = exhaustionOf(player);
        if (st.hasLast && st.lastExhaustion >= 0) {
            double dx = pos.x - st.lastX, dy = pos.y - st.lastY, dz = pos.z - st.lastZ;
            double horiz = std::sqrt(dx * dx + dz * dz);
            float  e     = 0;
            if (std::sqrt(dx * dx + dy * dy + dz * dz) <= 1.0) {
                if (player.isSwimming() || eyesInWater(player)) e = static_cast<float>(0.01 * std::sqrt(dx * dx + dy * dy + dz * dz));
                else if (inWater) e = static_cast<float>(0.01 * horiz); // wading: WALK_ON_WATER, horizontal
                else if (onGround && player.isSprinting()) e = static_cast<float>(0.1 * horiz);
            }
            if (float refund = tg::efficientEaterRefund(e, exh - st.lastExhaustion); refund > 0) {
                refundExhaustion(player, refund);
                exh = exhaustionOf(player);
            }
        }
        st.lastExhaustion = exh;
    } else {
        st.lastExhaustion = -1;
    }
    st.lastX = pos.x, st.lastY = pos.y, st.lastZ = pos.z, st.hasLast = true;

    // wing state (determineWingState), for the wings' model
    tickWings(player, st, onGround, liquid, delta, flying(player), player.isSprinting());
    st.lastOnGround = onGround;
}

void tickMob(Mob& mob) {
    if (!mob.isAlive()) return;
    auto&          st       = stateOf(mob);
    bool           onGround = mob.isOnGround();
    Vec3           delta    = mob.getPosDelta();
    unsigned const mask     = modMask(mob);
    // travellersWingsGradualGlide: a mob glides whenever it falls (isGraduallyGliding = !(Player))
    if (auto vy = tg::glide(delta.y, tg::kGradualGlideMultiplier, has(mask, Mod::GradualGlide), true, mob.isGliding())) {
        delta.y = static_cast<float>(*vy);
        setPosDelta(mob, delta);
        mob.setFallDistance(tg::glideFallDistance(*vy));
    }
    bool unrestrained = has(mask, Mod::Unrestrained);
    applyImmunity(mob, st, unrestrained);
    if (unrestrained || st.unrRescale != 1.0) {
        Vec3 nv = applyUnrestrained(mob, st, unrestrained, delta, onGround, mob.isSneaking(), nullptr);
        if (nv != delta) {
            delta = nv;
            setPosDelta(mob, delta);
        }
    }
    tickSlimy(mob, st, onGround, delta, has(mask, Mod::SlimySoles));
    keepEffect(mob, kJumpBoostId, tg::kHighJumpAmplifier, has(mask, Mod::HighJump), st.highJump, &st.savedJumpBoost);
    setStraightAhead(mob, has(mask, Mod::StraightAhead), st.straightAhead); // travellersBootsStraightAhead
    tickWings(mob, st, onGround, mob.isInWater() || inLava(mob), delta, false, false);
    st.lastOnGround = onGround;
}
namespace {

/// leashed_pathfinder_override (EntityHooks#overrideStayCloseToHolder): Java's PathfinderMob#tickLeash
/// leaves a marked mob free within 6 blocks of the holder and pulls it beyond. Bedrock's leash pulls
/// a mob beyond the preset's soft distance (measured by tgmove.leash: unmarked pigs 3.5 blocks away
/// end up next to the holder): a marked mob's soft (and hard) distance becomes at least 6.
void applyLeashOverride(Actor& a) {
    auto leash = a.getEntityContext().tryGetComponent<LeashableComponent>();
    if (!leash) return;
    auto& presets = leash->mPresets.get();
    if (presets.empty()) presets.push_back(LeashableUtility::getPreset(a, *leash, nullptr)); // the default, made the mob's own
    for (auto& preset : presets) {
        if (preset.mSoftDistance < kLeashFreeDistance) preset.mSoftDistance = kLeashFreeDistance;
        if (preset.mHardDistance < preset.mSoftDistance) preset.mHardDistance = preset.mSoftDistance;
        if (preset.mMaxDistance < preset.mHardDistance) preset.mMaxDistance = preset.mHardDistance;
    }
}

/// A mob wears Travellers' gear where TF's mob modifiers come from (legs or feet).
bool wearsTravellers(Actor const& a) {
    for (int slot : {2, 3}) {
        auto const& stack = armor(a, slot);
        if (!game::items::isEmpty(stack) && game::items::idOf(stack).starts_with("twilightforest:travellers_")) return true;
    }
    return false;
}

/// Mobs wearing Travellers' gear (legs or feet), and the leash override marker of every mob. The
/// world's mobs are looked through every kMobScanPeriod ticks; the wearers found are ticked every
/// tick until the gear comes off or they leave.
void tickMobs(Level& lv) {
    auto& s = store();
    if (gameTime() % kMobScanPeriod == 0) {
        for (Actor* a : lv.getRuntimeActorList()) {
            if (!a || a->isPlayer() || !a->hasCategory(ActorCategory::Mob) || !a->isAlive()) continue;
            // leashed_pathfinder_override: dropped from a mob that can no longer be leashed
            // (EntityEvents#handleLeashPathingOverrides)
            if (a->hasTags() && a->hasTag(kLeashOverrideTag)) {
                if (!a->getEntityContext().hasComponent<LeashableComponent>()) a->removeTag(kLeashOverrideTag);
                else applyLeashOverride(*a);
            }
            if (!s.mobs.contains(a->getOrCreateUniqueID().rawID) && wearsTravellers(*a)) (void)stateOf(*a);
        }
    }
    std::vector<std::int64_t> ids(s.mobs.begin(), s.mobs.end());
    for (auto id : ids) {
        Actor* a = lv.fetchEntity(ActorUniqueID{id}, false);
        if (!a || !a->isAlive() || a->isPlayer()) { // left the world
            s.players.erase(id);
            s.mobs.erase(id);
            continue;
        }
        if (!wearsTravellers(*a)) { // the gear came off: put everything back
            forget(*a);
            continue;
        }
        try {
            tickMob(static_cast<Mob&>(*a));
        } catch (std::exception const& e) {
            log().error("tgmove: tick of {} failed: {}", a->getTypeName(), e.what());
        }
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
                    processPending(*p);
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

        net::on("tgmove.double_jump", [](Player& p, Json const&) { onDoubleJumpPacket(p); });
        net::on("tgmove.sidestep", [](Player& p, Json const& msg) { onSidestepPacket(p, msg.value("left", false)); });
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
            // #minecraft:bypasses_armor: vanilla's members (1.21.1) and the data's (TF's)
            static std::unordered_set<std::string> const vanilla{
                "minecraft:on_fire", "minecraft:in_wall", "minecraft:cramming", "minecraft:drown", "minecraft:fly_into_wall", "minecraft:generic",
                "minecraft:wither", "minecraft:dragon_breath", "minecraft:starve", "minecraft:fall", "minecraft:freeze", "minecraft:stalagmite",
                "minecraft:magic", "minecraft:indirect_magic", "minecraft:out_of_world", "minecraft:generic_kill", "minecraft:sonic_boom",
                "minecraft:outside_border"};
            if (d.amount <= 0 || vanilla.contains(d.type) || tags::has(tags::Registry::DamageType, "minecraft:bypasses_armor", d.type)) return;
            // only the wearers of Travellers' gear need it (U11's auto repair): no data on every armoured mob
            bool damageable = false, travellers = false;
            for (int s = 0; s < 4; ++s) {
                auto const& stack = armor(d.victim, s);
                if (game::items::isEmpty(stack)) continue;
                damageable = damageable || stack.isDamageableItem();
                travellers = travellers || game::items::idOf(stack).starts_with("twilightforest:travellers_");
            }
            if (damageable && travellers) entity::data(d.victim, kLastDamageArmorTimeKey) = gameTime();
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
