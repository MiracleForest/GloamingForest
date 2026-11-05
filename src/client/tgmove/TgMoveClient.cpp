// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/event/TravellersClientEvents.java
//   src/main/java/twilightforest/item/travellers_gear/TravellersGearLogic.java
//   src/main/java/twilightforest/asmhooks/EntityHooks.java
//   src/main/java/twilightforest/asmhooks/PlayerHooks.java
//   src/main/java/twilightforest/events/TravellersGearEvents.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U12's client half (client build only): the local player's movement modifiers, run before the
// player's tick through gf::client::movement::onLocalTick (rule R8: no LocalPlayer hook here).
// - double jump (handleDoubleJump -> performDoubleJump, PerformDoubleJumpPacket = tgmove.double_jump)
// - sidestep (handleSidestep -> tryPerformSidestep, PerformSidestepPacket = tgmove.sidestep)
// - gradual glide (updateGradualGlideState, GradualGlidePacket = tgmove.gradual_glide;
//   travellersWingsGradualGlide; speedUpControlledWhileSneaking)
// - step up (+0.5 step height, off while crouching), straight ahead (strafe / 1.4 and the FOV
//   without it), agile ranger (no use slowdown with a bow), water walk (WaterWalkTransformer: water
//   source blocks carry the player half a block up), unrestrained (ResetStuckUnrestrained,
//   UnrestrainedBlockSpeedAndJumpFactor, WaterSprint) and swift swim (water movement efficiency).
// The server owns high jump, the glide's fall damage, the safe fall, slimy soles' bounce and the
// wing state (feature/tgmove). Each user-visible action logs one line ("tgmove: ...") that the
// e2e scenario measures.

#ifdef GF_CLIENT

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <string>

#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/vanilla_components/OnGroundFlagComponent.h"
#include "mc/entity/components/BlockMovementSlowdownImmunityComponent.h"
#include "mc/entity/components/ItemUseSlowdownModifierComponent.h"
#include "mc/entity/components/MoveInputComponent.h"
#include "mc/entity/components/WaterWalkSpeedEnchantComponent.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/player/AbilitiesIndex.h"
#include "mc/world/actor/player/LayeredAbilities.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceRef.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockDescriptor.h"

#include "client/Client.h"
#include "client/input/Input.h"
#include "client/world/Sky.h"
#include "core/tgmove/TgMove.h"
#include "feature/net/Net.h"
#include "feature/tags/Tags.h"
#include "feature/tgmove/TgMove.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::tgmove {

namespace tg = core::tgmove;
using feature::tgmove::active;
using feature::tgmove::Mod;
using Json = nlohmann::json;

namespace {

constexpr double kPi = 3.14159265358979323846;

/// The local player's TF attachments (per world session; reset when the player changes).
struct Local {
    std::int64_t     player = 0;
    bool             hasDoubleJump = false;
    int              lastJumpKeyPressTime = -100;
    tg::SidestepTap  tap;
    long long        lastSidestepTime = -1000000;
    bool             gliding = false;
    double           doubleJumpBoost = 0; ///< slimy soles, from the server (tgmove.slimy)
    std::optional<float> stepBase;
    float            stepSet = 0;
    bool             immunity = false;
    bool             straightForward = false; ///< straight ahead is speeding the player up
    double           speed = 0.1;             ///< the movement speed attribute (FOV)
    bool             waterWalking = false;
    // measurements for the e2e scenario
    std::optional<double> jumpBaseY;  ///< double jump: the y it started at
    double                jumpPeakY = 0;
    std::optional<Vec3>   stepStart;  ///< sidestep: where it started
    int                   stepTicks = 0;
    int                   glideTicks = 0;
    double                glideSink = 0;
    double                lastY = 0;
    bool                  lastOnGround = true;
};
Local& local() {
    static Local l;
    return l;
}

/// Other players' wing states and glides (TravellersWingsStatePacket / GradualGlidePacket).
struct Remote {
    std::map<std::int64_t, tg::Wings> wings;
    std::map<std::int64_t, bool>      gliding;
};
Remote& remote() {
    static Remote r;
    return r;
}

bool connected() { return net::client::connected(); }

double yawOf(LocalPlayer const& p) { return p.getRotation().y; }

/// The bows of Java's ProjectileWeaponItem (vanilla and TF's own) and the agile ranger tags.
bool agileRangerItem(ItemStack const& stack) {
    auto id = game::items::idOf(stack);
    if (id.empty()) return false;
    bool legal = id == "minecraft:bow" || id == "minecraft:crossbow" || id == "twilightforest:triple_bow" || id == "twilightforest:seeker_bow"
              || id == "twilightforest:ice_bow" || id == "twilightforest:ender_bow"
              || tags::has(tags::Registry::Item, "twilightforest:travellers_agile_ranger_whitelisted", id);
    return legal && !tags::has(tags::Registry::Item, "twilightforest:travellers_agile_ranger_blacklisted", id);
}

bool isWater(Block const& b) {
    auto id = game::blockId(b);
    return id == "minecraft:water" || id == "minecraft:flowing_water";
}

/// WaterWalkTransformer: the y of the water surface (the stable half-block shape of a water source
/// whose block above is not water) that the player's box lands on this tick, if any.
std::optional<double> waterSurface(LocalPlayer& p, Vec3 const& feet, double vy) {
    auto& region = p.getDimensionBlockSource();
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

void tick(movement::MoveState& s) {
    auto* p = currentPlayer();
    if (!p) return;
    auto& l  = local();
    auto  id = p->getOrCreateUniqueID().rawID;
    if (id != l.player) {
        l        = Local{};
        l.player = id;
    }
    int  tickCount = p->mTickCount;
    auto& ctx      = p->getEntityContext();
    Vec3 feet      = p->getFeetPos();
    bool inLava    = false;
    {
        auto bid = game::blockIdAt(p->getDimensionBlockSource(), p->getFeetBlockPos());
        inLava   = bid == "minecraft:lava" || bid == "minecraft:flowing_lava";
    }
    bool liquid = s.inWater || inLava;
    auto const& cfg = config::get<feature::tgmove::Config>();

    // ---- water walk (before anything reads the ground state) ----
    bool waterWalk = active(*p, Mod::WaterWalk) && !s.sneakHeld;
    l.waterWalking = false;
    if (waterWalk) {
        if (auto surface = waterSurface(*p, feet, s.velocity.y)) {
            s.velocity.y = static_cast<float>(*surface - feet.y);
            ctx.getOrAddComponent<OnGroundFlagComponent>();
            s.onGround     = true;
            l.waterWalking = true;
            auto& ds       = ctx.getOrAddComponent<WaterWalkSpeedEnchantComponent>();
            if (ds.mValue < tg::kSwiftSwimDepthStrider) ds.mValue = tg::kSwiftSwimDepthStrider; // ground speed on the water
            if (!l.lastOnGround) log().info("tgmove: walking on water at y {:.2f}", *surface);
            // WaterSprintTransformer: an unrestrained wearer keeps sprinting on the water
            if (active(*p, Mod::Unrestrained) && p->isSprinting()) s.sprinting = true;
        }
    }

    // ---- has_double_jump ----
    if (auto r = tg::refreshDoubleJump(active(*p, Mod::DoubleJump), s.onGround, liquid, p->onClimbableBlock()); r) l.hasDoubleJump = *r;

    // ---- double jump (handleDoubleJump) ----
    if (s.jumpEdge) {
        int  last  = l.lastJumpKeyPressTime;
        l.lastJumpKeyPressTime = tickCount;
        bool mayFly = p->getAbilities().getBool(AbilitiesIndex::MayFly);
        if (!tg::avoidCreativeFly(mayFly, tickCount, last) && active(*p, Mod::DoubleJump)) {
            tg::DoubleJumpState d;
            d.hasDoubleJump = l.hasDoubleJump;
            d.fallFlying    = s.gliding;
            d.onClimbable   = p->onClimbableBlock();
            d.onGround      = s.onGround;
            d.swimming      = p->isSwimming();
            d.flying        = p->isFlying();
            d.inLiquid      = liquid;
            d.passenger     = p->getVehicle() != nullptr;
            if (tg::canDoubleJump(d)) {
                auto const* jb  = p->getEffect(8u);
                auto        v   = tg::jumpFromGround({s.velocity.x, s.velocity.y, s.velocity.z}, tg::jumpPower(jb ? jb->mAmplifier : -1), s.sprinting, yawOf(*p));
                v.y             = tg::boostedJump(v.y, l.doubleJumpBoost);
                l.doubleJumpBoost = 0;
                s.velocity      = Vec3{static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
                s.fallDistance  = 0;
                l.hasDoubleJump = false;
                l.jumpBaseY     = feet.y;
                l.jumpPeakY     = feet.y;
                log().info("tgmove: double jump at y {:.2f} (vy {:.3f})", feet.y, v.y);
                if (connected()) net::client::send("tgmove.double_jump", Json::object());
            }
        }
    }

    // ---- sidestep (handleSidestep) ----
    if (auto left = tg::detectSidestep(l.tap, s.strafe, tickCount, s.onGround)) {
        if (active(*p, Mod::SideStep) && tg::sidestepReady(tickCount, l.lastSidestepTime, tg::kSidestepCooldown) && !s.gliding && s.onGround && !s.sneakHeld) {
            auto push = tg::sidestepPush(yawOf(*p), *left);
            s.velocity = s.velocity + Vec3{static_cast<float>(push.x), 0.0f, static_cast<float>(push.z)};
            l.lastSidestepTime = tickCount;
            l.stepStart        = feet;
            l.stepTicks        = 0;
            log().info("tgmove: sidestep {} (strafe {:.2f})", *left ? "left" : "right", s.strafe);
            if (connected()) net::client::send("tgmove.sidestep", {{"left", *left}});
        }
    }

    // ---- gradual glide (updateGradualGlideState, travellersWingsGradualGlide) ----
    bool glideActive = active(*p, Mod::GradualGlide);
    bool glide       = glideActive && tg::wantsGlide(cfg.travellersWingsGradualGlide, s.sneakHeld, s.velocity.y, s.onGround);
    if (glide != l.gliding) {
        l.gliding = glide;
        if (connected()) net::client::send("tgmove.gradual_glide", {{"gliding", glide}});
        if (glide) {
            l.glideTicks = 0;
            l.glideSink  = 0;
        } else if (l.glideTicks > 0) {
            log().info("tgmove: glide ended after {} ticks, sink {:.3f} blocks per tick", l.glideTicks, l.glideSink / l.glideTicks);
        }
    }
    if (auto vy = tg::glide(s.velocity.y, tg::kGradualGlideMultiplier, glideActive, l.gliding, s.gliding)) {
        s.velocity.y   = static_cast<float>(*vy);
        s.fallDistance = tg::glideFallDistance(*vy);
        ++l.glideTicks;
        l.glideSink += -*vy;
    }

    // ---- input: straight ahead, gliding sneak, agile ranger ----
    if (auto in = ctx.tryGetComponent<MoveInputComponent>()) {
        Vec2& move = in->mMove.get();
        double mult = tg::straightAheadMultiplier(active(*p, Mod::StraightAhead), tg::kStraightAheadMultiplier, move.y);
        l.straightForward = mult != 1.0;
        move.x = static_cast<float>(move.x / mult);
        if (l.gliding && s.sneakHeld) tg::unsneakImpulse(move.y, move.x);
    }
    if (active(*p, Mod::AgileRanger) && p->getItemUseDuration() > 0 && p->getVehicle() == nullptr && agileRangerItem(p->getCarriedItem())) {
        if (auto slow = ctx.tryGetComponent<ItemUseSlowdownModifierComponent>())
            slow->mModifier = std::min(1.0f, slow->mModifier * static_cast<float>(tg::kAgileRangerModifier));
    }
    {
        auto attr = p->getAttribute(SharedAttributes::MOVEMENT_SPEED());
        if (attr.mPtr) l.speed = attr.mPtr->mCurrentValue;
    }

    // ---- step up ----
    {
        float cur = s.stepHeight;
        if (!l.stepBase || cur != l.stepSet) l.stepBase = cur;
        bool on   = active(*p, Mod::StepUp) && !s.sneakHeld;
        s.stepHeight = *l.stepBase + (on ? static_cast<float>(tg::kStepUp) : 0.0f);
        l.stepSet    = s.stepHeight;
    }

    // ---- unrestrained: no block slowdown ----
    bool unrestrained = active(*p, Mod::Unrestrained);
    if (unrestrained && !l.immunity) {
        auto& imm = ctx.getOrAddComponent<BlockMovementSlowdownImmunityComponent>();
        for (char const* b : {"minecraft:web", "minecraft:sweet_berry_bush", "minecraft:powder_snow", "minecraft:honey_block", "minecraft:soul_sand"})
            imm.mBlocks->emplace_back(HashedString(b));
        l.immunity = true;
    } else if (!unrestrained && l.immunity) {
        ctx.removeComponent<BlockMovementSlowdownImmunityComponent>();
        l.immunity = false;
    }

    // ---- swift swim ----
    if (active(*p, Mod::SwiftSwim)) {
        auto& ds = ctx.getOrAddComponent<WaterWalkSpeedEnchantComponent>();
        if (ds.mValue < tg::kSwiftSwimDepthStrider) ds.mValue = tg::kSwiftSwimDepthStrider;
    }

    // ---- measurements (one line per action) ----
    if (l.jumpBaseY) {
        l.jumpPeakY = std::max(l.jumpPeakY, static_cast<double>(feet.y));
        if (s.onGround && tickCount > l.lastJumpKeyPressTime + 2) {
            log().info("tgmove: double jump peak {:.2f} blocks above its start", l.jumpPeakY - *l.jumpBaseY);
            l.jumpBaseY.reset();
        }
    }
    if (l.stepStart) {
        ++l.stepTicks;
        double hs = std::sqrt(s.velocity.x * s.velocity.x + s.velocity.z * s.velocity.z);
        if (l.stepTicks > 2 && hs < 0.01 || l.stepTicks > 60) {
            double dx = feet.x - l.stepStart->x, dz = feet.z - l.stepStart->z;
            log().info("tgmove: sidestep moved {:.2f} blocks", std::sqrt(dx * dx + dz * dz));
            l.stepStart.reset();
        }
    }
    if (s.onGround && l.lastOnGround && feet.y - l.lastY > 0.6 && feet.y - l.lastY < 1.2) log().info("tgmove: stepped up {:.2f} blocks", feet.y - l.lastY);
    l.lastY        = feet.y;
    l.lastOnGround = s.onGround;
}

class TgMoveClientModule {
public:
    void install() {
        movement::onLocalTick(0, [](movement::MoveState& s) {
            try {
                tick(s);
            } catch (std::exception const& e) {
                log().error("tgmove: local tick failed: {}", e.what());
            }
        });
        // PlayerHooks#straightAheadNullify / Restore: the FOV ignores straight ahead's speed
        camera::addFovModifier([](float fov) {
            auto& l = local();
            if (!l.straightForward || l.speed <= 0) return fov;
            double walk = 0.1;
            float  with = tg::speedFovFactor(l.speed, walk), without = tg::speedFovFactor(l.speed / tg::kStraightAheadMultiplier, walk);
            return with > 0 ? fov * without / with : fov;
        });
        net::client::on("tgmove.slimy", [](Json const& msg) { local().doubleJumpBoost = msg.value("boost", 0.0); });
        net::client::on("tgmove.wings_state", [](Json const& msg) {
            auto  id = msg.value("entity", std::int64_t{0});
            auto& w  = remote().wings[id];
            if (auto s = tg::wingStateFromName(msg.value("state", std::string("idle")))) w.state = *s;
            w.sidestepLeft    = msg.value("left", false);
            w.doubleJumpTimer = msg.value("double_jump_timer", 0);
            w.sidestepTimer   = msg.value("sidestep_timer", 0);
            static bool logged = false;
            if (!logged) {
                logged = true;
                log().info("tgmove: wing state {} received", tg::wingStateName(w.state));
            }
        });
        net::client::on("tgmove.gradual_glide", [](Json const& msg) { remote().gliding[msg.value("player", std::int64_t{0})] = msg.value("gliding", false); });
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::client::tgmove

GF_REGISTER_MODULE(gf::client::tgmove::TgMoveClientModule);

#endif // GF_CLIENT
