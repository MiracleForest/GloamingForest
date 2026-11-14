// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/client/event/TravellersClientEvents.java
//   src/main/java/gloamingforest/item/travellers_gear/TravellersGearLogic.java
//   src/main/java/gloamingforest/asmhooks/EntityHooks.java
//   src/main/java/gloamingforest/asmhooks/PlayerHooks.java
//   src/main/java/gloamingforest/events/TravellersGearEvents.java
//   src/main/java/gloamingforest/client/model/armor/TravellersWingsModel.java
// Copyright (c) the upstream authors (see NOTICE).
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
// every frame from the synced wing state and set as public Molang variables on the wearer, which
// animation.gloamingforest.travellers_wings (assets/pack/rp/animations/tgmove) turns into the wing
// bases' pose: the client mod adds that animation to the wings' attachable (U11 / F2) as the game
// reads it (feature/ext/tgmove/PackPatch), with no wings in first person (upstream draws no armour
// there). Vanilla clients read the attachable unchanged: their wings keep the model's rest pose and
// never read variables they do not have. Each user-visible action logs one line ("tgmove: ...")
// that the e2e scenario measures.

#ifdef GF_CLIENT

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/deps/vanilla_components/OnGroundFlagComponent.h"
#include "mc/entity/components/MaxAutoStepComponent.h"
#include "mc/entity/components/MoveInputComponent.h"
#include "mc/input/MoveInputState.h"
#include "mc/util/MolangScriptArg.h"
#include "mc/util/MolangVariable.h"
#include "mc/util/MolangVariableMap.h"
#include "mc/util/MolangVariableSetting_AccessSpecifier.h"
#include "mc/util/MolangVariableSettings.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/player/Ability.h"
#include "mc/world/actor/player/AbilitiesIndex.h"
#include "mc/world/actor/player/LayeredAbilities.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/AttributeInstanceRef.h"
#include "mc/world/attribute/AttributeModifier.h"
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
#include "feature/ext/tgmove/PackPatch.h"
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
constexpr char   kStraightName[] = "gloamingforest:travellers_gear.boots_straight_ahead";
constexpr int    kStaleFrames    = 200; ///< a remote entity unseen for this many frames is dropped
constexpr char   kWingsAnimationKey[] = "tgmove_wings"; ///< the wing animation's short name in the attachable

/// The FOV factor the engine last asked for and what it got (render thread; read by the tick's log).
std::atomic<float> gFovIn{0}, gFovOut{0};

/// A stretch of ticks the e2e scenario measures the walking speed over.
struct SpeedRun {
    std::string what;
    int         ticks    = 0;
    double      distance = 0;
};

/// The local player's upstream attachments (per world session; reset when the world or player changes).
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
    bool             sneakScaledLater = false; ///< Bedrock applies its sneak factor after MoveInputComponent::mMove
    bool             loggedSneakInput = false;
    bool             sneakChecked = false;
    bool             loggedAgileInput = false;     ///< this glide's first sneaking input was looked at
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
    std::optional<Vec3>   slideStart; ///< where the forward key was let go on the ground
    std::string           slideOn;
    int                   slideTicks = 0;
    float                 lastForward = 0;
    bool                  loggedStraight = false;
    /// tgmove.honey_jump: the server made the honey jump up; this side adds the same velocity
    struct Honey {
        double add   = 0;
        double lost  = 0;
        int    ticks = 0;
    };
    std::optional<Honey> honey;
    int                  honeyJumpTick = -1; ///< the tick (tickCount) of the last jump off a honey block
};
Local& local() {
    static Local l;
    return l;
}

/// Other entities' wing states (TravellersWingsStatePacket) and
/// their animation (TravellersWingsAnimAttachment).
struct Remote {
    std::map<std::int64_t, tg::Wings>     wings;
    std::map<std::int64_t, tg::WingsAnim> anim;
    std::map<std::int64_t, std::pair<int, float>> walk; ///< last tick, walkAnimation speed
    std::map<std::int64_t, long long>             seen; ///< the frame each entity was last animated
    long long                                     frame = 0;
    std::mt19937                                  rng{std::random_device{}()};
    std::mutex                                    wearersMutex;
    std::set<std::int64_t>                        wearers; ///< entities wearing the wings (scanWearers)
    tg::WingState                         loggedState = tg::WingState::Idle;
    bool                                  loggedAny   = false;
};
Remote& remote() {
    static Remote r;
    return r;
}

void resetWorld() {
    local() = Local{};
    auto& r = remote();
    std::lock_guard lock(r.wearersMutex);
    r.wings.clear();
    r.anim.clear();
    r.walk.clear();
    r.seen.clear();
    r.wearers.clear();
    r.frame     = 0;
    r.loggedAny = false;
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
    if (s.forward <= 0) return ""; // the runs are walks with W held
    auto  in     = game::blockIdAt(region, at);
    if (in == "minecraft:web" || game::blockIdAt(region, BlockPos{at.x, at.y + 1, at.z}) == "minecraft:web") return "cobweb";
    if (p.getItemUseDuration() > 0 && feature::tgmove::agileRangerItem(p.getCarriedItem())) return "bow";
    if (local().waterWalking) return "water_walk";
    if (local().gliding) return "glide";
    if (s.inWater) return "water";
    auto below = blockBelow(p, feet);
    if (in == "minecraft:soul_sand" || below == "minecraft:soul_sand") return "soul_sand";
    if (s.onGround && below == "minecraft:packed_ice") return "ice";
    if (s.onGround && below == "minecraft:slime") return "slime";
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
    unsigned const mask = feature::tgmove::modMask(*p); // one gear lookup per slot and tick
    auto const     act  = [mask](Mod m) { return feature::tgmove::has(mask, m); };
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
    bool waterWalk = act(Mod::WaterWalk) && !s.sneakHeld;
    l.waterWalking = false;
    if (waterWalk) {
        if (auto surface = feature::tgmove::waterSurface(*p, feet, s.velocity.y)) {
            s.velocity.y = static_cast<float>(*surface - feet.y);
            if (!ctx.hasComponent<OnGroundFlagComponent>()) ctx.getRegistry().emplace<OnGroundFlagComponent>(ctx.mEntity);
            s.onGround     = true;
            l.waterWalking = true;
            if (!l.lastOnGround) log().info("tgmove: walking on water at y {:.2f}", *surface);
            // WaterSprintTransformer: an unrestrained wearer is not "in water" while standing on it,
            // so it keeps sprinting there (upstream changes nothing for water it does not stand on)
            if (act(Mod::Unrestrained) && p->isSprinting()) s.sprinting = true;
        }
    }
    // swift swim (Depth Strider III), and ground speed on the water
    feature::tgmove::applyDepthStrider(*p, l.eng, act(Mod::SwiftSwim) || l.waterWalking);

    // ---- has_double_jump ----
    if (auto r = tg::refreshDoubleJump(act(Mod::DoubleJump), s.onGround, liquid, p->onClimbableBlock()); r) l.hasDoubleJump = *r;

    // ---- double jump (handleDoubleJump) ----
    if (s.jumpEdge) {
        int  last  = l.lastJumpKeyPressTime;
        l.lastJumpKeyPressTime = tickCount;
        bool mayFly = p->getAbilities().getBool(AbilitiesIndex::MayFly);
        if (!tg::avoidCreativeFly(mayFly, tickCount, last) && act(Mod::DoubleJump)) {
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
        // a jump off honey: the engine's jump step is this tick's (tgmove.honey_jump counts from it)
        if (s.onGround) l.honeyJumpTick = blockBelow(*p, feet) == "minecraft:honey_block" ? tickCount : -1;
    }

    // ---- sidestep (handleSidestep) ----
    if (auto left = tg::detectSidestep(l.tap, s.strafe, tickCount, s.onGround)) {
        if (act(Mod::SideStep) && tg::sidestepReady(now, l.lastSidestepTime, tg::kSidestepCooldown) && !s.gliding && s.onGround && !s.sneakHeld) {
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
    bool glideActive = act(Mod::GradualGlide);
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
        l.sneakChecked = false;
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
    // the engine reads its input after this hook, so upstream's input changes go in as the velocity they
    // make (feature::tgmove::inputDelta), as on the server copy
    if (auto in = ctx.tryGetComponent<MoveInputComponent>()) {
        Vec2 const move = in->mMove;
        Vec2       tf   = move;
        double     mult = tg::straightAheadMultiplier(act(Mod::StraightAhead), tg::kStraightAheadMultiplier, move.y);
        l.straightForward = mult != 1.0;
        if (l.straightForward) { // handleStraightAhead: strafe / 1.4 of the raw impulses
            auto s2 = tg::straightAheadInput(move.x, move.y, mult);
            tf      = Vec2{s2.strafe, s2.forward};
        }
        if (l.straightForward && move.x != 0 && !l.loggedStraight) {
            l.loggedStraight = true;
            log().info("tgmove: straight ahead strafe {:.3f} -> {:.3f}", move.x, tf.x);
        }
        Vec2 engine = move;
        // handleAgileRanger: drawing a projectile weapon, the input x 5 (cancelling Java's 0.2 use
        // slowdown); where Bedrock's input carries its slowdown the velocity makes up the rest
        if (act(Mod::AgileRanger) && p->getItemUseDuration() > 0 && p->getVehicle() == nullptr && feature::tgmove::agileRangerItem(p->getCarriedItem())) {
            // the slowdown of this use as the input shows it (each item has its own)
            Vec2 const raw  = in->mRawInputState->mAnalogMoveVector;
            double     slow = feature::tgmove::itemUseSlowdownOf(move, raw);
            tf              = feature::tgmove::agileRangerInput(tf, slow);
            if (!l.loggedAgileInput && (move.x != 0 || move.y != 0)) {
                l.loggedAgileInput = true;
                log().info("tgmove: agile ranger, input length while drawing {:.3f} (raw {:.3f}, use slowdown {:.3f})", std::sqrt(move.x * move.x + move.y * move.y),
                           std::sqrt(raw.x * raw.x + raw.y * raw.y), slow);
            }
        }
        if (l.gliding && sneak) {
            // speedUpControlledWhileSneaking: Java's impulses carry the 0.3 sneak factor before upstream's
            // / 0.2; when Bedrock's input is the unscaled one (longer than 0.35 while sneaking), its
            // factor comes later (decided on the glide's first sneaking tick)
            float len = std::sqrt(move.x * move.x + move.y * move.y);
            if (!l.sneakChecked && len > 0.01f) {
                l.sneakChecked     = true;
                l.sneakScaledLater = len > 0.35f;
            }
            if (!l.loggedSneakInput && len > 0.01f) {
                l.loggedSneakInput = true;
                log().info("tgmove: gliding with sneak, input length {:.3f} (the engine's sneak factor {})", len,
                           l.sneakScaledLater ? "comes after the input" : "is in the input");
            }
            if (l.sneakScaledLater) engine = Vec2{move.x * 0.3f, move.y * 0.3f};
            tg::unsneakImpulse(tf.y, tf.x, false);
            if (l.sneakScaledLater) { // the raw input: upstream's is its unit length
                tf = Vec2{static_cast<float>(move.x / mult), move.y};
                tg::unsneakImpulse(tf.y, tf.x, true, 1.0f);
            }
        }
        if (tf.x != engine.x || tf.y != engine.y) s.velocity = s.velocity + feature::tgmove::inputDelta(*p, engine, tf, s.onGround, s.sprinting, act(Mod::Unrestrained));
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
        bool on   = act(Mod::StepUp) && !s.sneakHeld;
        s.stepHeight = *l.stepBase + (on ? static_cast<float>(tg::kStepUp) : 0.0f);
        l.stepSet    = s.stepHeight;
    }

    // ---- unrestrained: no block slowdown; friction 0.6, no slime step-on slowdown or walking bounce ----
    feature::tgmove::applyImmunity(*p, l.eng, act(Mod::Unrestrained));
    // the honey jump's make-up: the server's copy makes it when the server runs the mod (it tells this
    // side, tgmove.honey_jump), else this side; never both (they would stack through the corrections)
    l.eng.unrHoneyByServer = connected();
    if (act(Mod::Unrestrained) || l.eng.unrRescale != 1.0) {
        Vec2 move{s.strafe, s.forward};
        if (auto in = ctx.tryGetComponent<MoveInputComponent>()) move = in->mMove;
        s.velocity = feature::tgmove::applyUnrestrained(*p, l.eng, act(Mod::Unrestrained), s.velocity, s.onGround, sneak, &move);
    }
    if (l.honey && !act(Mod::Unrestrained)) l.honey.reset(); // the boots came off: nothing to make up
    if (l.honey) {
        // the steps this side jumped weakly after its jump step (the jump key's tick) until the word came
        int late = l.honeyJumpTick >= 0 ? tickCount - l.honeyJumpTick - 1 : -1;
        if (!s.onGround && (late >= 0 || l.honey->ticks >= 1)) {
            // the server's make-up, plus those steps; the height made up comes off the next step
            // (applyUnrestrained), as on the server
            auto up           = tg::honeyCatchUp(l.honey->lost, std::clamp(late, 0, 20));
            s.velocity.y      = static_cast<float>(s.velocity.y + up.add);
            l.eng.unrJumpLost = up.deficit;
            l.honeyJumpTick   = -1;
            log().info("tgmove: honey jump made up by the server (vy +{:.3f}, {} steps late)", up.add, late);
            l.honey.reset();
        } else if (++l.honey->ticks > 4) {
            l.honey.reset(); // too late: the server's corrections put the jump right
        }
    }

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
            if (l.run.ticks >= 10 && !l.run.what.empty()) {
                log().info("tgmove: speed {} {:.4f} blocks per tick over {} ticks", l.run.what, l.run.distance / l.run.ticks, l.run.ticks);
                if (l.run.what == "forward") log().info("tgmove: fov forward in {:.4f} out {:.4f}", gFovIn.load(), gFovOut.load());
            }
            l.run = SpeedRun{what};
        } else if (!what.empty() && l.hasLastFeet) {
            double dx = feet.x - l.lastFeet.x, dz = feet.z - l.lastFeet.z;
            ++l.run.ticks;
            l.run.distance += std::sqrt(dx * dx + dz * dz);
        }
    }
    // the slide after the forward key is let go (unrestrained: as on any block)
    if (l.lastForward > 0 && s.forward <= 0 && s.onGround && !l.slideStart) {
        l.slideStart = feet;
        l.slideOn    = blockBelow(*p, feet);
        l.slideTicks = 0;
    }
    if (l.slideStart) {
        double hs = std::sqrt(s.velocity.x * s.velocity.x + s.velocity.z * s.velocity.z);
        if (++l.slideTicks > 100 || (l.slideTicks > 1 && hs < 0.003) || s.forward > 0) {
            double dx = feet.x - l.slideStart->x, dz = feet.z - l.slideStart->z;
            log().info("tgmove: slid {:.2f} blocks on {} in {} ticks", std::sqrt(dx * dx + dz * dz), l.slideOn, l.slideTicks);
            l.slideStart.reset();
        }
    }
    l.lastForward  = s.forward;
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

/// Sets a public Molang variable on the wearer: the wings' attachable reads it as
/// c.owning_entity->v.<name> (an attachable's own v. map is not the wearer's; only public variables
/// of another entity can be read through ->).
void setPublic(MolangVariableMap& vars, HashedString const& name, float value) {
    MolangScriptArg arg(value);
    vars.setMolangVariable(name, arg);
    if (MolangVariable* v = vars._getOrAddMolangVariable(MolangVariable::getVariableIndex(name.getHash()))) {
        MolangVariableSettings& settings = v->mSettings;
        settings.mAccessSpecifier         = MolangVariableSetting_AccessSpecifier::Public;
        std::unique_ptr<MolangScriptArg>& pub = v->mPublicValue;
        if (pub) *pub = arg;
        else pub = std::make_unique<MolangScriptArg>(arg);
    }
    vars.mHasPublicVariables = true;
}

bool wearsWings(Actor const& a) {
    return game::items::idOf(a.getArmor(SharedTypes::Legacy::ArmorSlot::Legs)) == "gloamingforest:travellers_wings";
}

/// One frame of the wings of `a`: the pose in degrees on its Molang variables, read by the
/// attachable's animation (animation.gloamingforest.travellers_wings).
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
    auto [animIt, fresh] = r.anim.try_emplace(id);
    auto& anim           = animIt->second;
    if (fresh) tg::seedPhase(anim, std::uniform_real_distribution<double>(0.0, 1.0)(r.rng)); // accumulatedPhase: random
    r.seen[id] = r.frame;
    double age        = a.mTickCount + partialTicks;
    if (anim.oldAge <= 0 || age < anim.oldAge || age - anim.oldAge > 40) anim.oldAge = age; // first frame / jump in time
    auto pose = tg::animateWings(anim, w, age, walk.second, a.isSneaking());
    auto& vars = a.mMolangVariables.get();
    auto const& n = wingVars();
    setPublic(vars, n.on, 1.0f);
    setPublic(vars, n.rx, static_cast<float>(pose.rightX * kToDeg));
    setPublic(vars, n.ry, static_cast<float>(pose.rightY * kToDeg));
    setPublic(vars, n.rz, static_cast<float>(pose.rightZ * kToDeg));
    setPublic(vars, n.ly, static_cast<float>(pose.leftY * kToDeg));
    setPublic(vars, n.drop, pose.drop);
    // the e2e trace: the local player's pose whenever its state changes
    if (auto* me = currentPlayer(); me && me->getOrCreateUniqueID().rawID == id && (!r.loggedAny || r.loggedState != w.state)) {
        r.loggedAny   = true;
        r.loggedState = w.state;
        log().info("tgmove: wings pose {} (right {:.1f} {:.1f} {:.1f}, left y {:.1f})", tg::wingStateName(w.state), pose.rightX * kToDeg, pose.rightY * kToDeg,
                   pose.rightZ * kToDeg, pose.leftY * kToDeg);
    }
}

/// Once per client tick: the entities in the level that wear the wings. A new wearer gets its pose
/// variables at once, before any frame draws its attachable (the animation reads them through an
/// arrow, which must never find them missing); the frames then animate the wearers found here.
void scanWearers() {
    auto* me = currentPlayer();
    if (!me) return;
    auto&                  r = remote();
    std::set<std::int64_t> found;
    for (Actor* a : me->getLevel().getRuntimeActorList()) {
        if (!a || !a->isAlive() || !wearsWings(*a)) continue;
        auto id = a->getOrCreateUniqueID().rawID;
        found.insert(id);
        if (!r.anim.contains(id)) animate(*a, 0.0f);
    }
    std::lock_guard lock(r.wearersMutex);
    r.wearers = std::move(found);
}

void renderWings(float partialTicks) {
    auto* me = currentPlayer();
    if (!me) return;
    auto& lv = me->getLevel();
    auto& r  = remote();
    ++r.frame;
    // every entity wearing the wings (the tick's scan), players and mobs, whether or not a state came
    // for it yet (IDLE until one does)
    std::vector<std::int64_t> wearers;
    {
        std::lock_guard lock(r.wearersMutex);
        wearers.assign(r.wearers.begin(), r.wearers.end());
    }
    for (auto id : wearers)
        if (auto* a = lv.fetchEntity(ActorUniqueID{id}, false); a && a->isAlive() && wearsWings(*a)) animate(*a, partialTicks);
    // entities gone or out of sight for a while: their state goes (it comes back with the next packet)
    if (r.frame % 100 == 0) {
        std::set<std::int64_t> ids;
        for (auto const& [id, _] : r.wings) ids.insert(id);
        for (auto const& [id, _] : r.anim) ids.insert(id);
        for (auto const& [id, _] : r.walk) ids.insert(id);
        for (auto id : ids) {
            auto it = r.seen.find(id);
            if (it != r.seen.end() && r.frame - it->second < kStaleFrames) continue;
            if (it == r.seen.end()) { // a state packet without a frame yet: keep it until it is that old
                r.seen[id] = r.frame;
                continue;
            }
            // the animation goes; the synced state (sent only when it changes) stays while the entity
            // is in the world
            r.anim.erase(id);
            r.walk.erase(id);
            if (!lv.fetchEntity(ActorUniqueID{id}, false)) {
                r.wings.erase(id);
                r.seen.erase(id);
            }
        }
    }
}

/// The wings' attachable as the client mod reads it: U12's wing animation (TravellersWingsModel,
/// fed by animate()) and no wings in first person (upstream draws no armour there). Returns whether it
/// changed `doc` (only the wings' attachable, once).
bool patchWingsAttachable(Json& doc) {
    auto att = doc.find("minecraft:attachable");
    if (att == doc.end() || !att->is_object()) return false;
    auto desc = att->find("description");
    if (desc == att->end() || !desc->is_object() || desc->value("identifier", std::string()) != "gloamingforest:travellers_wings") return false;
    Json& anims = (*desc)["animations"];
    if (!anims.is_object()) anims = Json::object();
    if (anims.contains(kWingsAnimationKey)) return false;
    anims[kWingsAnimationKey] = "animation.gloamingforest.travellers_wings";
    Json& scripts = (*desc)["scripts"];
    if (!scripts.is_object()) scripts = Json::object();
    Json& animate = scripts["animate"];
    if (!animate.is_array()) animate = Json::array();
    animate.push_back(kWingsAnimationKey);
    Json& rcs = (*desc)["render_controllers"];
    Json  out = Json::array();
    if (rcs.is_array())
        for (auto const& rc : rcs) out.push_back(rc.is_string() ? Json{{rc.get<std::string>(), "!c.is_first_person"}} : rc);
    rcs = std::move(out);
    return true;
}

/// Whether the client mod is enabled (the contracts have no unregistration: its handlers check it).
std::atomic<bool> gEnabled{false};

class TgMoveClientModule {
public:
    // the resource packs load before the mod is enabled: the attachable's patch is registered first
    void load() { registerPatch(); }
    void install() {
        registerPatch();
        gEnabled = true;
        if (mInstalled) return; // the contracts keep the handlers below; they check gEnabled
        mInstalled = true;
        movement::onLocalTick(0, [](movement::MoveState& s) {
            if (!gEnabled) return;
            try {
                tick(s);
                scanWearers();
            } catch (std::exception const& e) {
                log().error("tgmove: local tick failed: {}", e.what());
            }
        });
        // PlayerHooks#straightAheadNullify / Restore: the FOV ignores straight ahead's speed
        camera::addFovModifier([](float fov) {
            gFovIn  = fov;
            gFovOut = fov;
            if (!gEnabled) return fov;
            auto* p  = currentPlayer();
            if (!p) return fov;
            // only while the synced movement speed carries straight ahead's modifier (PlayerHooks#
            // straightAheadNullify reads the attribute's modifier), with its amount
            auto attr = p->getAttribute(SharedAttributes::MOVEMENT_SPEED());
            if (!attr.mPtr) return fov;
            double mult = 1.0;
            for (auto const& m : attr.mPtr->getModifiers())
                if (m.mName.get() == kStraightName) mult *= 1.0 + m.mAmount;
            if (mult == 1.0) return fov;
            double walk = 0.1;
            if (auto const& ab = p->getAbilities().getAbility(AbilitiesIndex::WalkSpeed); ab.mType == Ability::Type::Float) {
                Ability::Value const& v = ab.mValue;
                float                 w = v.mFloatVal;
                if (w > 0) walk = w;
            }
            float k  = tg::straightAheadFovCompensation(attr.mPtr->mCurrentValue, walk, mult);
            gFovOut = fov * k;
            static bool logged = false;
            if (!logged) {
                logged = true;
                log().info("tgmove: straight ahead fov x{:.3f} (speed {:.4f}, modifier x{:.2f}, walk speed {:.3f})", k, attr.mPtr->mCurrentValue, mult, walk);
            }
            return fov * k;
        });
        render::onWorld([](render::RenderContext&, float partialTicks) {
            if (!gEnabled) return;
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
        // GradualGlidePacket: the glide itself is in the server's motion of the other player; nothing to keep
        net::client::on("tgmove.gradual_glide", [](Json const&) {});
        // the server made a honey jump up (unrestrained): this side adds the same on its next step
        net::client::on("tgmove.honey_jump", [](Json const& msg) {
            if (!gEnabled) return;
            local().honey = Local::Honey{msg.value("add", 0.0), msg.value("lost", 0.0), 0};
        });
    }
    /// Puts back what this side changed on the local player (Depth Strider, slowdown immunity, item
    /// use slowdown, step height, the honey make-up) and stops the handlers (they check gEnabled).
    void uninstall() {
        gEnabled = false;
        auto& l  = local();
        if (auto* p = currentPlayer()) {
            feature::tgmove::applyImmunity(*p, l.eng, false);
            feature::tgmove::applyDepthStrider(*p, l.eng, false);
            (void)feature::tgmove::applyAgileRanger(*p, l.eng.agileBase, l.eng.agileSet, true);
            if (l.stepBase) {
                if (auto step = p->getEntityContext().tryGetComponent<MaxAutoStepComponent>(); step && step->mValue == l.stepSet) step->mValue = *l.stepBase;
            }
        }
        resetWorld();
    }

private:
    static void registerPatch() {
        static bool done = false;
        if (done) return;
        done = true;
        gf::ext::packpatch::addJsonPatch("travellers_wings.json", [](Json& doc) { return patchWingsAttachable(doc); });
    }
    bool mInstalled = false;
};

} // namespace

} // namespace gf::client::tgmove

GF_REGISTER_MODULE(gf::client::tgmove::TgMoveClientModule);

#endif // GF_CLIENT
