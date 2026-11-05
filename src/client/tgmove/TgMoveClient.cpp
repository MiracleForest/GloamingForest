// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/event/TravellersClientEvents.java
//   src/main/java/twilightforest/item/travellers_gear/TravellersGearLogic.java
//   src/main/java/twilightforest/asmhooks/EntityHooks.java
//   src/main/java/twilightforest/asmhooks/PlayerHooks.java
//   src/main/java/twilightforest/events/TravellersGearEvents.java
//   src/main/java/twilightforest/client/model/armor/TravellersWingsModel.java
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
// Bedrock's movement is server authoritative: the server applies the same modifiers to its copy
// of the player and pushes the double jump and the sidestep itself (feature/tgmove); a jump of the
// client's own would be put back. The wings' animation (TravellersWingsModel) is computed here
// every frame from the synced wing state and handed to the wings' attachable as Molang variables
// (assets/pack/rp/animations/tgmove). Each user-visible action logs one line ("tgmove: ...") that
// the e2e scenario measures.

#ifdef GF_CLIENT

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>

#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/deps/vanilla_components/OnGroundFlagComponent.h"
#include "mc/entity/components/MoveInputComponent.h"
#include "mc/input/MoveInputState.h"
#include "mc/util/MolangScriptArg.h"
#include "mc/util/MolangVariableMap.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/player/AbilitiesIndex.h"
#include "mc/world/actor/player/LayeredAbilities.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/AttributeInstanceRef.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockDescriptor.h"

#include "client/Client.h"
#include "client/input/Input.h"
#include "client/render/Render.h"
#include "client/world/Sky.h"
#include "core/tgmove/TgMove.h"
#include "feature/net/Net.h"
#include "feature/tgmove/TgMove.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::tgmove {

namespace tg = core::tgmove;
using feature::tgmove::active;
using feature::tgmove::Mod;
using Json = nlohmann::json;

namespace {

constexpr double kPi    = 3.14159265358979323846;
constexpr double kToDeg = 180.0 / kPi;

/// A stretch of ticks the e2e scenario measures the walking speed over.
struct SpeedRun {
    std::string what;
    int         ticks    = 0;
    double      distance = 0;
};

/// The local player's TF attachments (per world session; reset when the world or player changes).
struct Local {
    std::int64_t     player = 0;
    long long        lastLevelTick = -1;
    bool             hasDoubleJump = false;
    int              lastJumpKeyPressTime = -100;
    tg::SidestepTap  tap;
    long long        lastSidestepTime = -1000000; ///< level game time, as the server's
    bool             gliding = false;
    double           doubleJumpBoost = 0; ///< slimy soles, from the server (tgmove.slimy)
    std::optional<float> stepBase;
    float            stepSet = 0;
    feature::tgmove::PlayerState eng; ///< the engine state this side changed (immunity, Depth Strider, use slowdown)
    bool             straightForward = false; ///< straight ahead is speeding the player up
    double           speed = 0.1;             ///< the movement speed attribute (FOV)
    bool             waterWalking = false;
    // measurements for the e2e scenario
    std::optional<double> jumpBaseY;  ///< double jump: the y it started at
    double                jumpPeakY = 0;
    std::optional<double> groundJumpY; ///< a jump from the ground: the y it started at
    std::string           groundJumpFrom;
    double                groundJumpPeak = 0;
    std::optional<Vec3>   stepStart;  ///< sidestep: where it started
    int                   stepTicks = 0;
    int                   glideTicks = 0;
    double                glideSink = 0;
    double                lastY = 0;
    Vec3                  lastFeet{};
    bool                  hasLastFeet  = false;
    bool                  lastOnGround = true;
    SpeedRun              run;
    bool                  loggedStraight = false;
};
Local& local() {
    static Local l;
    return l;
}

/// Other entities' wing states and glides (TravellersWingsStatePacket / GradualGlidePacket) and
/// their animation (TravellersWingsAnimAttachment).
struct Remote {
    std::map<std::int64_t, tg::Wings>     wings;
    std::map<std::int64_t, bool>          gliding;
    std::map<std::int64_t, tg::WingsAnim> anim;
    std::map<std::int64_t, std::pair<int, float>> walk; ///< last tick, walkAnimation speed
    tg::WingState                         loggedState = tg::WingState::Idle;
    bool                                  loggedAny   = false;
};
Remote& remote() {
    static Remote r;
    return r;
}

void resetWorld() {
    local()  = Local{};
    remote() = Remote{};
}

bool connected() { return net::client::connected(); }

double yawOf(LocalPlayer const& p) { return p.getRotation().y; }

long long levelTick(LocalPlayer& p) { return static_cast<long long>(p.getLevel().getCurrentTick().tickID); }

std::string blockBelow(LocalPlayer& p, Vec3 const& feet) {
    auto& region = p.getDimensionBlockSource();
    BlockPos at{static_cast<int>(std::floor(feet.x)), static_cast<int>(std::floor(feet.y - 0.2)), static_cast<int>(std::floor(feet.z))};
    return game::blockIdAt(region, at);
}

/// What the scenario's speed runs are about: an agile ranger bow draw, soul sand, cobweb, water.
std::string speedContext(LocalPlayer& p, movement::MoveState const& s, Vec3 const& feet) {
    auto& region = p.getDimensionBlockSource();
    auto  at     = p.getFeetBlockPos();
    auto  in     = game::blockIdAt(region, at);
    if (in == "minecraft:web" || game::blockIdAt(region, BlockPos{at.x, at.y + 1, at.z}) == "minecraft:web") return "cobweb";
    if (p.getItemUseDuration() > 0 && feature::tgmove::agileRangerItem(p.getCarriedItem())) return "bow";
    if (local().waterWalking) return "water_walk";
    if (s.inWater) return "water";
    auto below = blockBelow(p, feet);
    if (in == "minecraft:soul_sand" || below == "minecraft:soul_sand") return "soul_sand";
    if (s.onGround && s.forward > 0) return "forward";
    return "";
}

void tick(movement::MoveState& s) {
    auto* p = currentPlayer();
    if (!p) return;
    auto& l    = local();
    auto  id   = p->getOrCreateUniqueID().rawID;
    long long now = levelTick(*p);
    if (id != l.player || now < l.lastLevelTick) { // another player or another world
        resetWorld();
        l.player = id;
    }
    l.lastLevelTick = now;
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
        if (auto surface = feature::tgmove::waterSurface(*p, feet, s.velocity.y)) {
            s.velocity.y = static_cast<float>(*surface - feet.y);
            if (!ctx.hasComponent<OnGroundFlagComponent>()) ctx.getRegistry().emplace<OnGroundFlagComponent>(ctx.mEntity);
            s.onGround     = true;
            l.waterWalking = true;
            if (!l.lastOnGround) log().info("tgmove: walking on water at y {:.2f}", *surface);
            // WaterSprintTransformer: an unrestrained wearer is not "in water" while standing on it,
            // so it keeps sprinting there (TF changes nothing for water it does not stand on)
            if (active(*p, Mod::Unrestrained) && p->isSprinting()) s.sprinting = true;
        }
    }
    // swift swim (Depth Strider III), and ground speed on the water
    feature::tgmove::applyDepthStrider(*p, l.eng, active(*p, Mod::SwiftSwim) || l.waterWalking);

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
                // the server pushes the jump (SetActorMotionPacket): Bedrock's server-authoritative
                // movement would put a jump the server does not know back (e2e: the player is
                // corrected 2 ticks later); without a server running the mod the client jumps
                if (!connected()) {
                    s.velocity     = Vec3{static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
                    s.fallDistance = 0;
                }
                l.hasDoubleJump = false;
                l.jumpBaseY     = feet.y;
                l.jumpPeakY     = feet.y;
                l.groundJumpY.reset();
                log().info("tgmove: double jump at y {:.2f} (vy {:.3f})", feet.y, v.y);
                if (connected()) net::client::send("tgmove.double_jump", Json::object());
            }
        }
        if (s.onGround && !l.jumpBaseY) { // a jump from the ground: its height (the honey test)
            l.groundJumpY    = feet.y;
            l.groundJumpPeak = feet.y;
            l.groundJumpFrom = blockBelow(*p, feet);
        }
    }

    // ---- sidestep (handleSidestep) ----
    if (auto left = tg::detectSidestep(l.tap, s.strafe, tickCount, s.onGround)) {
        if (active(*p, Mod::SideStep) && tg::sidestepReady(now, l.lastSidestepTime, tg::kSidestepCooldown) && !s.gliding && s.onGround && !s.sneakHeld) {
            auto push = tg::sidestepPush(yawOf(*p), *left);
            if (!connected()) s.velocity = s.velocity + Vec3{static_cast<float>(push.x), 0.0f, static_cast<float>(push.z)}; // else the server pushes it
            l.lastSidestepTime = now;
            l.stepStart        = feet;
            l.stepTicks        = 0;
            log().info("tgmove: sidestep {} (strafe {:.2f})", *left ? "left" : "right", s.strafe);
            if (connected()) net::client::send("tgmove.sidestep", {{"left", *left}});
        }
    }

    // ---- gradual glide (updateGradualGlideState, travellersWingsGradualGlide) ----
    bool glideActive = active(*p, Mod::GradualGlide);
    bool sneak       = s.sneakHeld || p->isSneaking();
    if (auto in = ctx.tryGetComponent<MoveInputComponent>()) { // the raw key, also in the air
        auto held = [](MoveInputState const& st) {
            return st.mFlagValues->test(static_cast<std::size_t>(MoveInputState::Flag::SneakDown))
                || st.mFlagValues->test(static_cast<std::size_t>(MoveInputState::Flag::SneakInputCurrentlyDown));
        };
        sneak = sneak || held(in->mInputState.get()) || held(in->mRawInputState.get());
    }
    bool glide       = glideActive && tg::wantsGlide(cfg.travellersWingsGradualGlide, sneak, s.velocity.y, s.onGround);
    if (glide != l.gliding) {
        l.gliding = glide;
        if (connected()) net::client::send("tgmove.gradual_glide", {{"gliding", glide}});
        if (glide) {
            log().info("tgmove: glide started at y {:.2f}", feet.y);
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

    // ---- input: straight ahead, gliding sneak ----
    if (auto in = ctx.tryGetComponent<MoveInputComponent>()) {
        Vec2& move = in->mMove.get();
        double mult = tg::straightAheadMultiplier(active(*p, Mod::StraightAhead), tg::kStraightAheadMultiplier, move.y);
        l.straightForward = mult != 1.0;
        float before      = move.x;
        move.x            = static_cast<float>(move.x / mult);
        if (l.straightForward && before != 0 && !l.loggedStraight) {
            l.loggedStraight = true;
            log().info("tgmove: straight ahead strafe {:.3f} -> {:.3f}", before, move.x);
        }
        if (l.gliding && sneak) tg::unsneakImpulse(move.y, move.x);
    }
    // ---- agile ranger: the bow's use slowdown x5 (the server's copy does the same) ----
    (void)feature::tgmove::applyAgileRanger(*p, l.eng.agileBase, l.eng.agileSet);
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
    feature::tgmove::applyImmunity(*p, l.eng, active(*p, Mod::Unrestrained));

    // ---- measurements (one line per action) ----
    if (l.jumpBaseY) {
        l.jumpPeakY = std::max(l.jumpPeakY, static_cast<double>(feet.y));
        if (s.onGround && tickCount > l.lastJumpKeyPressTime + 2) {
            log().info("tgmove: double jump peak {:.2f} blocks above its start", l.jumpPeakY - *l.jumpBaseY);
            l.jumpBaseY.reset();
        }
    }
    if (l.groundJumpY) {
        l.groundJumpPeak = std::max(l.groundJumpPeak, static_cast<double>(feet.y));
        if (s.onGround && tickCount > l.lastJumpKeyPressTime + 2) {
            log().info("tgmove: jump peak {:.2f} blocks from {}", l.groundJumpPeak - *l.groundJumpY, l.groundJumpFrom);
            l.groundJumpY.reset();
        }
    }
    if (l.stepStart) {
        ++l.stepTicks;
        double hs = std::sqrt(s.velocity.x * s.velocity.x + s.velocity.z * s.velocity.z);
        if ((l.stepTicks > 2 && hs < 0.01) || l.stepTicks > 60) {
            double dx = feet.x - l.stepStart->x, dz = feet.z - l.stepStart->z;
            log().info("tgmove: sidestep moved {:.2f} blocks", std::sqrt(dx * dx + dz * dz));
            l.stepStart.reset();
        }
    }
    if (s.onGround && l.lastOnGround && feet.y - l.lastY > 0.6 && feet.y - l.lastY < 1.2) log().info("tgmove: stepped up {:.2f} blocks", feet.y - l.lastY);
    // walking speed runs (agile ranger, soul sand, cobweb, swimming, straight ahead)
    {
        std::string what = speedContext(*p, s, feet);
        if (what != l.run.what) {
            if (l.run.ticks >= 10 && !l.run.what.empty())
                log().info("tgmove: speed {} {:.4f} blocks per tick over {} ticks", l.run.what, l.run.distance / l.run.ticks, l.run.ticks);
            l.run = SpeedRun{what};
        } else if (!what.empty() && l.hasLastFeet) {
            double dx = feet.x - l.lastFeet.x, dz = feet.z - l.lastFeet.z;
            ++l.run.ticks;
            l.run.distance += std::sqrt(dx * dx + dz * dz);
        }
    }
    l.lastFeet     = feet;
    l.hasLastFeet  = true;
    l.lastY        = feet.y;
    l.lastOnGround = s.onGround;
}

// ---- wings animation (TravellersWingsModel#setupModelAnimations) ----------------------------------

struct WingVars {
    HashedString on{"variable.tgmove_wing_on"};
    HashedString rx{"variable.tgmove_wing_rx"};
    HashedString ry{"variable.tgmove_wing_ry"};
    HashedString rz{"variable.tgmove_wing_rz"};
    HashedString ly{"variable.tgmove_wing_ly"};
    HashedString drop{"variable.tgmove_wing_drop"};
};
WingVars const& wingVars() {
    static WingVars v;
    return v;
}

bool wearsWings(Actor const& a) {
    return game::items::idOf(a.getArmor(SharedTypes::Legacy::ArmorSlot::Legs)) == "twilightforest:travellers_wings";
}

/// One frame of the wings of `a`: the pose in degrees on its Molang variables, read by the
/// attachable's animation (animation.twilightforest.travellers_wings).
void animate(Actor& a, float partialTicks) {
    auto& r  = remote();
    auto  id = a.getOrCreateUniqueID().rawID;
    // walkAnimation.speed(), once per tick
    auto& walk = r.walk[id];
    if (walk.first != a.mTickCount) {
        walk.first  = a.mTickCount;
        Vec3 d      = a.getPosDelta();
        walk.second = tg::walkAnimation(walk.second, std::sqrt(d.x * d.x + d.z * d.z));
    }
    auto it = r.wings.find(id);
    tg::Wings const w = it != r.wings.end() ? it->second : tg::Wings{};
    auto& anim        = r.anim[id];
    double age        = a.mTickCount + partialTicks;
    if (anim.oldAge <= 0 || age < anim.oldAge || age - anim.oldAge > 40) anim.oldAge = age; // first frame / jump in time
    auto pose = tg::animateWings(anim, w, age, walk.second, a.isSneaking());
    auto& vars = a.mMolangVariables.get();
    auto const& n = wingVars();
    vars.setMolangVariable(n.on, MolangScriptArg(1.0f));
    vars.setMolangVariable(n.rx, MolangScriptArg(static_cast<float>(pose.rightX * kToDeg)));
    vars.setMolangVariable(n.ry, MolangScriptArg(static_cast<float>(pose.rightY * kToDeg)));
    vars.setMolangVariable(n.rz, MolangScriptArg(static_cast<float>(pose.rightZ * kToDeg)));
    vars.setMolangVariable(n.ly, MolangScriptArg(static_cast<float>(pose.leftY * kToDeg)));
    vars.setMolangVariable(n.drop, MolangScriptArg(pose.drop));
    // the e2e trace: the local player's pose whenever its state changes
    if (auto* me = currentPlayer(); me && me->getOrCreateUniqueID().rawID == id && (!r.loggedAny || r.loggedState != w.state)) {
        r.loggedAny   = true;
        r.loggedState = w.state;
        log().info("tgmove: wings pose {} (right {:.1f} {:.1f} {:.1f}, left y {:.1f})", tg::wingStateName(w.state), pose.rightX * kToDeg, pose.rightY * kToDeg,
                   pose.rightZ * kToDeg, pose.leftY * kToDeg);
    }
}

void renderWings(float partialTicks) {
    auto* me = currentPlayer();
    if (!me) return;
    auto& lv = me->getLevel();
    std::set<std::int64_t> done;
    lv.forEachPlayer([&](Player& q) {
        if (wearsWings(q)) {
            animate(q, partialTicks);
            done.insert(q.getOrCreateUniqueID().rawID);
        }
        return true;
    });
    // mobs wearing the wings (their state comes from the server too)
    for (auto const& [id, w] : remote().wings) {
        if (done.contains(id)) continue;
        if (auto* a = lv.fetchEntity(ActorUniqueID{id}, false); a && !a->isPlayer() && wearsWings(*a)) animate(*a, partialTicks);
    }
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
            static bool logged = false;
            if (!logged && with > 0) {
                logged = true;
                log().info("tgmove: straight ahead fov x{:.3f} (speed {:.4f})", without / with, l.speed);
            }
            return with > 0 ? fov * without / with : fov;
        });
        render::onWorld([](render::RenderContext&, float partialTicks) {
            try {
                renderWings(partialTicks);
            } catch (std::exception const& e) {
                log().error("tgmove: wings animation failed: {}", e.what());
            }
        });
        game::db::onWorldClosed([] { resetWorld(); });
        net::client::on("tgmove.slimy", [](Json const& msg) { local().doubleJumpBoost = msg.value("boost", 0.0); });
        net::client::on("tgmove.wings_state", [](Json const& msg) {
            auto  id = msg.value("entity", std::int64_t{0});
            auto& w  = remote().wings[id];
            if (auto s = tg::wingStateFromName(msg.value("state", std::string("idle")))) w.state = *s;
            w.sidestepLeft    = msg.value("left", false);
            w.doubleJumpTimer = msg.value("double_jump_timer", 0);
            w.sidestepTimer   = msg.value("sidestep_timer", 0);
        });
        net::client::on("tgmove.gradual_glide", [](Json const& msg) { remote().gliding[msg.value("player", std::int64_t{0})] = msg.value("gliding", false); });
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::client::tgmove

GF_REGISTER_MODULE(gf::client::tgmove::TgMoveClientModule);

#endif // GF_CLIENT
