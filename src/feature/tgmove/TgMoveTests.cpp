// Selftests of U12 (tgmove.*, testPos dx 1200..1299, rule R12): the server half of the Travellers'
// Gear movement modifiers (docs/spec/tgmove.md section 5). The client half (double jump height,
// sidestep distance, glide sink rate, water walk, step up) is measured by the client e2e scenario.
#include <cmath>
#include <string>
#include <vector>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/entity/components/BlockMovementSlowdownImmunityComponent.h"
#include "mc/entity/components/ItemUseSlowdownModifierComponent.h"
#include "mc/entity/components/MaxAutoStepComponent.h"
#include "mc/entity/components/MoveInputComponent.h"
#include "mc/entity/components/WaterWalkSpeedEnchantComponent.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/AbilitiesIndex.h"
#include "mc/world/actor/player/LayeredAbilities.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/level/block/Block.h"
#include "mc/entity/components_json_legacy/BreathableComponent.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/AttributeInstanceRef.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "game/Blocks.h"
#include "feature/ext/ui/Server.h"
#include "feature/movement/Movement.h"
#include "feature/spawn/Spawn.h"
#include "feature/gear/Gear.h"
#include "feature/net/Net.h"
#include "feature/tgmove/TgMove.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
namespace tm = gf::feature::tgmove;
namespace tg = gf::core::tgmove;
using tm::Mod;

using Slot = SharedTypes::Legacy::ArmorSlot;

/// Puts Travellers' gear `itemId` with `mods` (beyond its built-in ones) in armour slot `slot`.
/// The gear items get their wearable component from U11; the selftests set the slot directly and
/// write F9's modifier data ("gf:gear"), so they do not depend on U11.
void equip(Player& p, int slot, std::string const& itemId, std::vector<std::string> const& mods) {
    auto stack = gf::game::items::make(itemId, 1);
    if (!stack) return;
    nlohmann::json data = nlohmann::json::object();
    for (auto const& m : mods) data[m] = 1;
    if (!data.empty()) gf::game::items::setData(*stack, "gear", data);
    p.setArmor(static_cast<Slot>(slot), *stack);
}

void unequipAll(Player& p) {
    for (int s = 0; s < 4; ++s) p.setArmor(static_cast<Slot>(s), ItemStack::EMPTY_ITEM());
}

Vec3 at(gf::core::GlobalPos const& g, float dy = 0) { return Vec3{g.x + 0.5f, static_cast<float>(g.y) + dy, g.z + 0.5f}; }

/// The player on the ground at `g`, its state fresh.
ll::coro::CoroTask<> place(Context& ctx, gf::core::GlobalPos g) {
    auto& p = ctx.player();
    p.teleport(at(g), DimensionType(g.dim));
    tm::forget(p);
    co_await ctx.ticks(10);
}

std::string num(double d) { return std::to_string(d); }

GF_REGISTER_SELFTEST("tgmove.modifiers", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(1200, 8);
    co_await place(ctx, ctx.testPos(1202, 2));
    auto& p = ctx.player();
    unequipAll(p);
    for (auto m : {Mod::DoubleJump, Mod::SideStep, Mod::GradualGlide, Mod::HighJump, Mod::StepUp, Mod::SwiftSwim})
        ctx.check(!tm::active(p, m), std::string(tm::modId(m)) + " is off without gear");
    equip(p, 2, "twilightforest:travellers_wings", {"twilightforest:double_jump", "twilightforest:side_step", "twilightforest:gradual_glide"});
    equip(p, 3, "twilightforest:travellers_boots", {"twilightforest:water_walk", "twilightforest:slimy_soles"});
    equip(p, 1, "twilightforest:travellers_vest", {});
    for (auto m : {Mod::DoubleJump, Mod::SideStep, Mod::GradualGlide, Mod::HighJump, Mod::StepUp, Mod::WaterWalk, Mod::SlimySoles, Mod::SwiftSwim})
        ctx.check(tm::active(p, m), std::string(tm::modId(m)) + " is active");
    ctx.check(!tm::active(p, Mod::Unrestrained), "unrestrained was not added");
    ctx.check(tm::modSlot(Mod::AquaticAgility) == 0 && tm::modSlot(Mod::SwiftSwim) == 1 && tm::modSlot(Mod::DoubleJump) == 2 && tm::modSlot(Mod::WaterWalk) == 3, "the equipment slot groups");
    // broken gear (max damage - 1) turns its modifiers off
    auto wings = p.getArmor(Slot::Legs);
    if (Item const* item = wings.mItem.get()) {
        wings.setDamageValue(static_cast<short>(item->getMaxDamage() - 1));
        p.setArmor(Slot::Legs, wings);
        ctx.check(tm::brokenGear(p.getArmor(Slot::Legs)), "the wings are broken");
        ctx.check(!tm::active(p, Mod::DoubleJump), "broken wings: no double jump");
    }
    unequipAll(p);
    co_return;
});

GF_REGISTER_SELFTEST("tgmove.double_jump", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(1210, 8);
    auto g = ctx.testPos(1212, 2);
    co_await place(ctx, g);
    auto& p = ctx.player();
    equip(p, 2, "twilightforest:travellers_wings", {"twilightforest:double_jump"});
    co_await ctx.ticks(3);
    auto& p1 = ctx.player();
    ctx.require(tm::stateOf(p1).hasDoubleJump, "has_double_jump is set on the ground");
    auto const* jb = p1.getEffect(8u);
    ctx.check(jb && jb->mAmplifier == tg::kHighJumpAmplifier, "the wings give Jump Boost II (high_jump)");
    ctx.check(!tm::performDoubleJump(p1, false), "no double jump on the ground");
    // in the air
    p1.teleport(at(g, 6), DimensionType(g.dim));
    co_await ctx.ticks(2);
    auto& p2 = ctx.player();
    tm::stateOf(p2).airborne = true;
    tm::stateOf(p2).slimy.doubleJumpBoostVelocity = 0.4;
    p2.setFallDistance(3);
    ctx.require(tm::performDoubleJump(p2, true), "the double jump in the air");
    double vy = p2.getPosDelta().y;
    ctx.check(std::abs(vy - std::sqrt(0.62 * 0.62 + 0.4 * 0.4)) < 0.01, "jump 0.62 with Jump Boost II plus the slimy boost: vy " + num(vy));
    ctx.check(p2.getFallDistance() == 0, "the fall distance is reset");
    auto& st = tm::stateOf(p2);
    ctx.check(!st.hasDoubleJump && st.safeFall, "used up; safe fall +2");
    ctx.check(st.slimy.doubleJumpBoostVelocity == 0, "the slimy boost is spent");
    ctx.check(st.wings.state == tg::WingState::DoubleJump, "the wings flap (DOUBLE_JUMP)");
    ctx.check(!tm::performDoubleJump(p2, true), "only once per airtime");
    // safe fall: 2 less fall damage until landing
    float a = 3;
    ctx.check(tm::onFall(p2, a) && std::abs(a - 1) < 1e-4, "a 3 damage landing deals 1");
    float b = 2;
    ctx.check(!tm::onFall(p2, b), "a 2 damage landing is cancelled");
    // landing restores it
    p2.teleport(at(g), DimensionType(g.dim));
    co_await ctx.ticks(10);
    auto& p3 = ctx.player();
    ctx.check(tm::stateOf(p3).hasDoubleJump && !tm::stateOf(p3).safeFall, "landing restores the double jump and drops the safe fall");
    unequipAll(p3);
    co_await ctx.ticks(2);
    auto& p4 = ctx.player();
    ctx.check(!tm::stateOf(p4).hasDoubleJump, "no double jump without the modifier");
    ctx.check(p4.getEffect(8u) == nullptr, "Jump Boost removed with the wings");
});

GF_REGISTER_SELFTEST("tgmove.validator", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(1220, 8);
    co_await place(ctx, ctx.testPos(1222, 2));
    auto& p = ctx.player();
    equip(p, 2, "twilightforest:travellers_wings", {"twilightforest:double_jump", "twilightforest:side_step"});
    tm::forceValidator(true);
    std::vector<tg::Abuse> got;
    for (int i = 0; i < 6; ++i) got.push_back(tm::handleAbuse(p, false));
    ctx.check(got[0] == tg::Abuse::Accept && got[1] == tg::Abuse::Accept, "the first two refused jumps pass");
    ctx.check(got[2] == tg::Abuse::SnapBack && got[4] == tg::Abuse::SnapBack, "count > 1 snaps back");
    ctx.check(got[5] == tg::Abuse::Disconnect, "count >= 5 disconnects");
    ctx.check(tm::stateOf(p).snapBackPending, "a snap back waits for the next move");
    Vec3 from = p.getFeetPos();
    ctx.check(!gf::movement::validateMove(p, from, from + Vec3{0, 1, 0}), "the next move is rejected (snapped back)");
    ctx.check(gf::movement::validateMove(p, from, from + Vec3{0, 1, 0}), "only once");
    // a refused companion packet runs the validator (PerformDoubleJumpPacket#handle)
    tm::forget(p);
    gf::net::setTestCompanion(p, true);
    gf::net::injectFromClient(p, "tgmove.double_jump", nlohmann::json::object());
    ctx.check(tm::stateOf(p).doubleJumpValidator.lastCheck == p.mTickCount && tm::stateOf(p).doubleJumpValidator.count <= 1,
              "a refused double jump packet is checked");
    gf::net::injectFromClient(p, "tgmove.sidestep", {{"left", true}});
    gf::net::injectFromClient(p, "tgmove.sidestep", {{"left", true}});
    ctx.check(tm::stateOf(p).sidestepValidator.count >= 1 || tm::stateOf(p).wings.state == tg::WingState::Sidestep, "the sidestep packet: performed or checked");
    gf::net::setTestCompanion(p, false);
    tm::forceValidator(std::nullopt);
    tm::forget(p);
    unequipAll(p);
    co_return;
});

GF_REGISTER_SELFTEST("tgmove.sidestep", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(1230, 8);
    co_await place(ctx, ctx.testPos(1232, 2));
    auto& p = ctx.player();
    equip(p, 2, "twilightforest:travellers_wings", {"twilightforest:side_step"});
    co_await ctx.ticks(2);
    auto& p1 = ctx.player();
    const_cast<Vec2&>(p1.getRotation()) = Vec2{0, 0};
    Vec3 before = p1.getPosDelta();
    ctx.require(tm::tryPerformSidestep(p1, true), "a sidestep on the ground");
    Vec3 push = p1.getPosDelta() - before;
    double h  = std::sqrt(push.x * push.x + push.z * push.z);
    ctx.check(std::abs(h - 1.6) < 0.01, "pushed 1.6 sideways: " + num(h));
    auto& st = tm::stateOf(p1);
    ctx.check(st.wings.state == tg::WingState::Sidestep && st.wings.sidestepLeft, "the wings sidestep left");
    ctx.check(!tm::tryPerformSidestep(p1, false), "cooldown: not again at once");
    st.wings.lastSidestepTime = tm::gameTime() - tg::kSidestepCooldown;
    ctx.check(!tm::tryPerformSidestep(p1, false), "40 ticks are not enough (more than 40)");
    st.wings.lastSidestepTime = tm::gameTime() - tg::kSidestepCooldown - 1;
    co_await ctx.ticks(1);
    auto& p2 = ctx.player();
    ctx.check(!tm::stateOf(p2).wings.shouldPlayCooldownSound, "the side_step_ready sound played once the cooldown passed");
    co_await ctx.ticks(10);
    auto& p3 = ctx.player();
    ctx.check(tm::tryPerformSidestep(p3, false), "after the cooldown");
    p3.setSneaking(true);
    tm::stateOf(p3).wings.lastSidestepTime = -100000;
    ctx.check(!tm::tryPerformSidestep(p3, false), "not while crouching");
    p3.setSneaking(false);
    unequipAll(p3);
});

GF_REGISTER_SELFTEST("tgmove.glide", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(1240, 8);
    auto g = ctx.testPos(1242, 2);
    co_await place(ctx, g);
    auto& p = ctx.player();
    equip(p, 2, "twilightforest:travellers_wings", {"twilightforest:gradual_glide"});
    p.teleport(at(g, 12), DimensionType(g.dim));
    co_await ctx.ticks(2);
    auto& p1 = ctx.player();
    // a companion glides: the server keeps the fall distance at v^2 / 2g
    tm::stateOf(p1).gliding = true;
    p1.lerpMotion(Vec3{0, -0.5f, 0});
    tm::tickPlayer(p1);
    ctx.check(std::abs(p1.getPosDelta().y + 0.5f * 5 / 6) < 1e-3f, "the server copy glides: vy x 5/6 (" + num(p1.getPosDelta().y) + ")");
    ctx.check(std::abs(p1.getFallDistance() - tg::glideFallDistance(-0.5 * 5 / 6)) < 0.01f, "fall distance v^2 / 2g: " + num(p1.getFallDistance()));
    // without the modifier the glide stops
    unequipAll(p1);
    tm::tickPlayer(p1);
    ctx.check(!tm::stateOf(p1).gliding, "is_gradually_gliding is cleared without the modifier");
    // vanilla fallback: sneaking in the air = Slow Falling
    equip(p1, 2, "twilightforest:travellers_wings", {"twilightforest:gradual_glide"});
    p1.teleport(at(g, 24), DimensionType(g.dim));
    co_await ctx.ticks(4);
    auto& p1b = ctx.player();
    p1b.setSneaking(true);
    tm::stateOf(p1b).airborne = true; // the input edges of a vanilla client (LeftGround)
    p1b.lerpMotion(Vec3{0, -0.3f, 0});
    tm::tickPlayer(p1b);
    ctx.check(p1b.getEffect(27u) != nullptr,
              "Slow Falling while sneaking in the air (sneaking " + std::to_string(p1b.isSneaking()) + ", on ground " + std::to_string(p1b.isOnGround())
                  + ", vy " + num(p1b.getPosDelta().y) + ", active " + std::to_string(tm::active(p1b, Mod::GradualGlide)) + ", companion "
                  + std::to_string(gf::net::isCompanion(p1b)) + ")");
    p1b.setSneaking(false);
    tm::tickPlayer(p1b);
    ctx.check(p1b.getEffect(27u) == nullptr, "no Slow Falling once the sneak key is released");
    unequipAll(p1b);
    p1b.teleport(at(g), DimensionType(g.dim));
    co_return;
});

GF_REGISTER_SELFTEST("tgmove.step_up", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(1250, 8);
    co_await place(ctx, ctx.testPos(1252, 2));
    auto& p    = ctx.player();
    auto  step = p.getEntityContext().tryGetComponent<MaxAutoStepComponent>();
    ctx.require(step.has_value(), "the player has a max auto step");
    float base = step->mValue;
    equip(p, 3, "twilightforest:travellers_boots", {});
    tm::tickPlayer(p);
    ctx.check(std::abs(p.getEntityContext().tryGetComponent<MaxAutoStepComponent>()->mValue - (base + 0.5f)) < 1e-4, "boots: step height +0.5");
    p.setSneaking(true);
    tm::tickPlayer(p);
    ctx.check(std::abs(p.getEntityContext().tryGetComponent<MaxAutoStepComponent>()->mValue - base) < 1e-4, "crouching: the normal step height");
    p.setSneaking(false);
    unequipAll(p);
    tm::tickPlayer(p);
    ctx.check(std::abs(p.getEntityContext().tryGetComponent<MaxAutoStepComponent>()->mValue - base) < 1e-4, "without the boots: the normal step height");
    co_return;
});

GF_REGISTER_SELFTEST("tgmove.slimy_soles", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(1260, 8);
    co_await place(ctx, ctx.testPos(1262, 2));
    auto& p = ctx.player();
    equip(p, 3, "twilightforest:travellers_boots", {"twilightforest:slimy_soles"});
    auto& st  = tm::stateOf(p);
    st.fallVy = -1.0;
    float dmg = 4;
    ctx.check(!tm::onFall(p, dmg), "a damaging landing is cancelled");
    ctx.check(std::abs(st.slimy.bounceVelocity - std::sqrt(0.5)) < 1e-6, "bounce = 1.0 x sqrt(0.5)");
    ctx.check(st.slimy.doubleJumpBoostVelocity == st.slimy.bounceVelocity, "stored as the next double jump boost");
    p.lerpMotion(Vec3{0, 0, 0});
    tm::tickPlayer(p);
    double vy = p.getPosDelta().y;
    ctx.check(std::abs(vy - std::sqrt(0.5)) < 0.01, "the bounce: vy " + num(vy));
    ctx.check(st.slimy.forceBounce && st.slimy.hasBounced, "the bounce continues (|vy| > 0.25), once per landing");
    // sneaking lands normally
    tm::forget(p);
    p.setSneaking(true);
    float dmg2 = 4;
    ctx.check(tm::onFall(p, dmg2) && dmg2 == 4, "sneaking: the fall hurts");
    p.setSneaking(false);
    unequipAll(p);
    co_return;
});

GF_REGISTER_SELFTEST("tgmove.unrestrained", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(1270, 8);
    co_await place(ctx, ctx.testPos(1272, 2));
    auto& p = ctx.player();
    equip(p, 3, "twilightforest:travellers_boots", {"twilightforest:unrestrained"});
    tm::tickPlayer(p);
    auto imm = p.getEntityContext().tryGetComponent<BlockMovementSlowdownImmunityComponent>();
    ctx.check(imm.has_value() && imm->mBlocks->size() == 5, "immune to the slowdown of cobweb, berry bush, powder snow, honey and soul sand");
    unequipAll(p);
    tm::tickPlayer(p);
    ctx.check(!p.getEntityContext().hasComponent<BlockMovementSlowdownImmunityComponent>(), "removed with the boots");
    co_return;
});

GF_REGISTER_SELFTEST("tgmove.straight_ahead", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(1280, 8);
    co_await place(ctx, ctx.testPos(1282, 2));
    auto& p     = ctx.player();
    auto  speed = [&] {
        auto a = p.getAttribute(SharedAttributes::MOVEMENT_SPEED());
        return a.mPtr ? a.mPtr->mCurrentValue : 0.0f;
    };
    float base = speed();
    equip(p, 3, "twilightforest:travellers_boots", {"twilightforest:straight_ahead"});
    auto input = [&](float forward) {
        if (auto in = p.getEntityContext().tryGetComponent<MoveInputComponent>()) in->mMove->y = forward;
    };
    ctx.require(p.getEntityContext().hasComponent<MoveInputComponent>(), "the player has a move input");
    input(1.0f); // the forward key
    tm::tickPlayer(p);
    ctx.check(std::abs(speed() - base * 1.4f) < 1e-3f, "x1.4 with the forward key: " + num(speed()) + " of " + num(base));
    input(0.0f);
    p.lerpMotion(Vec3{0, 0, 0.3f}); // still moving forward, the key released (TF: forwardImpulse only)
    tm::tickPlayer(p);
    ctx.check(std::abs(speed() - base) < 1e-4f, "forward momentum without the key: normal speed");
    input(-1.0f);
    tm::tickPlayer(p);
    ctx.check(std::abs(speed() - base) < 1e-4f, "backwards: normal speed");
    unequipAll(p);
    tm::tickPlayer(p);
    ctx.check(std::abs(speed() - base) < 1e-4f, "without the boots: normal speed");
    co_return;
});

GF_REGISTER_SELFTEST("tgmove.wings_state", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(1290, 8);
    co_await place(ctx, ctx.testPos(1292, 2));
    auto& p = ctx.player();
    gf::net::setTestCompanion(p, true);
    (void)gf::net::takeTestMessages(p);
    equip(p, 2, "twilightforest:travellers_wings", {"twilightforest:side_step"});
    co_await ctx.ticks(2);
    auto& p1 = ctx.player();
    ctx.require(tm::tryPerformSidestep(p1, false), "a sidestep");
    int locked = 0;
    for (int i = 0; i < 20; ++i) {
        tm::tickPlayer(p1);
        if (tm::stateOf(p1).wings.state == tg::WingState::Sidestep) ++locked;
    }
    ctx.check(locked == tg::kSidestepDuration - 1, "SIDESTEP holds 11 ticks: " + std::to_string(locked + 1));
    bool sent = false;
    for (auto const& e : gf::net::takeTestMessages(p1))
        if (e.channel == "tgmove.wings_state") sent = true;
    ctx.check(sent, "the state goes to the companion (tgmove.wings_state)");
    gf::net::setTestCompanion(p1, false);
    unequipAll(p1);
});

GF_REGISTER_SELFTEST("tgmove.aquatic_agility", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(1298, 2);
    co_await place(ctx, ctx.testPos(1298, 2));
    auto& p  = ctx.player();
    auto  br = p.getEntityContext().tryGetComponent<BreathableComponent>();
    ctx.require(br.has_value(), "the player breathes");
    short saved = br->mAirSupply;
    equip(p, 0, "twilightforest:travellers_goggles", {"twilightforest:aquatic_agility"});
    int kept = 0;
    for (int i = 0; i < 400; ++i) {
        tm::stateOf(p).lastAir = 200;
        br->mAirSupply         = 199;
        tm::tickPlayer(p);
        if (br->mAirSupply == 200) ++kept;
    }
    ctx.check(kept > 240 && kept < 360, "oxygen bonus 3 keeps 3 of 4 breaths: " + std::to_string(kept) + " / 400");
    br->mAirSupply = saved;
    unequipAll(p);
    co_return;
});

GF_REGISTER_SELFTEST("tgmove.effects", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(1200, 8);
    co_await place(ctx, ctx.testPos(1203, 1));
    auto& p = ctx.player();
    unequipAll(p);
    // a weaker Jump Boost of the player's own (a potion): the wings' Jump Boost II replaces it and
    // it comes back with the time it had left (Java's hidden effect)
    MobEffectInstance potion(8u, EffectDuration{600});
    potion.mAmplifier = 0;
    p.addEffect(potion);
    equip(p, 2, "twilightforest:travellers_wings", {});
    tm::tickPlayer(p);
    auto const* e = p.getEffect(8u);
    ctx.check(e && e->mAmplifier == tg::kHighJumpAmplifier, "Jump Boost II over a weaker potion: amplifier " + std::to_string(e ? e->mAmplifier : -1));
    unequipAll(p);
    tm::tickPlayer(p);
    e = p.getEffect(8u);
    ctx.check(e && e->mAmplifier == 0 && e->mDuration->mValue > 500, "the potion is back: amplifier " + std::to_string(e ? e->mAmplifier : -1) + ", "
                                                                         + std::to_string(e ? e->mDuration->mValue : 0) + " ticks");
    p.removeEffect(8);
    // a stronger one stays
    MobEffectInstance strong(8u, EffectDuration{600});
    strong.mAmplifier = 3;
    p.addEffect(strong);
    equip(p, 2, "twilightforest:travellers_wings", {});
    tm::tickPlayer(p);
    e = p.getEffect(8u);
    ctx.check(e && e->mAmplifier == 3, "a stronger potion stays");
    unequipAll(p);
    tm::tickPlayer(p);
    e = p.getEffect(8u);
    ctx.check(e && e->mAmplifier == 3, "and is not taken off with the wings");
    p.removeEffect(8);
    tm::forget(p);
    co_return;
});

GF_REGISTER_SELFTEST("tgmove.restore", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(1210, 8);
    co_await place(ctx, ctx.testPos(1213, 1));
    auto& p     = ctx.player();
    auto& ectx  = p.getEntityContext();
    auto  depth = [&] {
        auto c = ectx.tryGetComponent<WaterWalkSpeedEnchantComponent>();
        return c ? c->mValue : -1;
    };
    unequipAll(p);
    tm::tickPlayer(p);
    int depthBefore = depth();
    // swift swim: Depth Strider III with the vest, the level before once it is off
    equip(p, 1, "twilightforest:travellers_vest", {});
    tm::tickPlayer(p);
    ctx.check(depth() == tg::kSwiftSwimDepthStrider, "the vest: Depth Strider III (" + std::to_string(depth()) + ")");
    unequipAll(p);
    tm::tickPlayer(p);
    ctx.check(depth() == depthBefore, "without the vest: Depth Strider " + std::to_string(depth()) + " as before (" + std::to_string(depthBefore) + ")");
    // step up: forgetting the state (tests, respawn) never adds the +0.5 twice
    auto step = [&] { return ectx.tryGetComponent<MaxAutoStepComponent>()->mValue; };
    float base = step();
    equip(p, 3, "twilightforest:travellers_boots", {"twilightforest:unrestrained"});
    for (int i = 0; i < 3; ++i) {
        tm::tickPlayer(p);
        tm::forget(p);
    }
    tm::tickPlayer(p);
    ctx.check(std::abs(step() - (base + 0.5f)) < 1e-4f, "step height +0.5 once after forgetting the state 3 times: " + num(step()) + " of " + num(base));
    // unrestrained leaves slowdown immunities of others in place
    auto imm = ectx.tryGetComponent<BlockMovementSlowdownImmunityComponent>();
    ctx.require(imm.has_value(), "the immunity component");
    imm->mBlocks->emplace_back(HashedString("minecraft:mud"));
    unequipAll(p);
    tm::tickPlayer(p);
    auto after = ectx.tryGetComponent<BlockMovementSlowdownImmunityComponent>();
    ctx.check(after.has_value() && after->mBlocks->size() == 1 && after->mBlocks->front().getFullName() == "minecraft:mud", "only the other entry is left");
    if (after) ectx.removeComponent<BlockMovementSlowdownImmunityComponent>();
    ctx.check(std::abs(step() - base) < 1e-4f, "the step height is back");
    tm::forget(p);
    co_return;
});

GF_REGISTER_SELFTEST("tgmove.safe_fall_slimy", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(1220, 8);
    co_await place(ctx, ctx.testPos(1223, 1));
    auto& p = ctx.player();
    equip(p, 3, "twilightforest:travellers_boots", {"twilightforest:slimy_soles"});
    auto& st    = tm::stateOf(p);
    st.safeFall = true; // after a double jump
    st.fallVy   = -0.8;
    float a     = 2; // TF: no damage within the +2 safe fall, so no bounce either
    ctx.check(!tm::onFall(p, a) && st.slimy.bounceVelocity == 0, "a 2 damage landing after a double jump: harmless, no bounce");
    float b = 4;
    ctx.check(!tm::onFall(p, b) && st.slimy.bounceVelocity > 0, "a 4 damage landing after a double jump: the bounce");
    tm::forget(p);
    unequipAll(p);
    co_return;
});

GF_REGISTER_SELFTEST("tgmove.creative_fly", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(1230, 8);
    auto g = ctx.testPos(1233, 1);
    co_await place(ctx, g);
    auto& p = ctx.player();
    equip(p, 2, "twilightforest:travellers_wings", {"twilightforest:double_jump"});
    co_await ctx.ticks(2);
    auto& p1 = ctx.player();
    p1.teleport(at(g, 6), DimensionType(g.dim));
    co_await ctx.ticks(1);
    auto& p2 = ctx.player();
    using gf::movement::Edge;
    // a may-fly player pressing jump twice within 6 ticks starts flying: no double jump
    p2.getAbilities().setAbility(AbilitiesIndex::MayFly, true);
    tm::stateOf(p2).hasDoubleJump = true;
    tm::stateOf(p2).airborne      = false;
    tm::onInputEdge(p2, static_cast<int>(Edge::JumpPressed)); // the jump from the ground
    tm::onInputEdge(p2, static_cast<int>(Edge::LeftGround));
    tm::onInputEdge(p2, static_cast<int>(Edge::JumpReleased));
    tm::onInputEdge(p2, static_cast<int>(Edge::JumpPressed)); // pressed again at once, in the air
    ctx.check(tm::stateOf(p2).hasDoubleJump, "may fly, two presses within 6 ticks: no double jump (avoidCreativeFly)");
    // without may-fly the same press is the double jump
    p2.getAbilities().setAbility(AbilitiesIndex::MayFly, false);
    tm::stateOf(p2).lastJumpPressTick = -100;
    tm::onInputEdge(p2, static_cast<int>(Edge::JumpPressed));
    ctx.check(!tm::stateOf(p2).hasDoubleJump, "without may-fly: the double jump");
    tm::forget(p2);
    unequipAll(p2);
    p2.teleport(at(g), DimensionType(g.dim));
    co_return;
});

GF_REGISTER_SELFTEST("tgmove.mobs", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(1240, 8);
    auto g = ctx.testPos(1243, 1);
    co_await place(ctx, g);
    auto& p      = ctx.player();
    auto& region = p.getDimensionBlockSource();
    // a pig (leashable) wearing the wings, and a bat (not leashable) with the leash override tag
    Actor* a = gf::spawn::spawnMob(region, "minecraft:pig", at(g, 10), gf::spawn::SpawnReason::Command);
    Actor* b = gf::spawn::spawnMob(region, "minecraft:bat", at(g, 3), gf::spawn::SpawnReason::Command);
    ctx.require(a != nullptr && b != nullptr, "a pig and a bat");
    auto pigId = a->getOrCreateUniqueID();
    auto batId = b->getOrCreateUniqueID();
    a->addTag(tm::kLeashOverrideTag);
    b->addTag(tm::kLeashOverrideTag);
    {
        auto wings = *gf::game::items::make("twilightforest:travellers_wings", 1);
        gf::game::items::setData(wings, "gear", {{"twilightforest:gradual_glide", 1}});
        a->setArmor(Slot::Legs, wings);
    }
    ctx.check(tm::activeOn(*a, Mod::GradualGlide) && tm::activeOn(*a, Mod::HighJump), "a mob's wings: gradual glide and high jump");
    a->lerpMotion(Vec3{0, -0.6f, 0});
    tm::tickMob(static_cast<Mob&>(*a));
    ctx.check(std::abs(a->getPosDelta().y + 0.6f * 5 / 6) < 1e-3f, "a mob glides whenever it falls: vy " + num(a->getPosDelta().y));
    ctx.check(a->getEffect(8u) && a->getEffect(8u)->mAmplifier == tg::kHighJumpAmplifier, "a mob jumps high");
    co_await ctx.ticks(2);
    auto* lv  = ll::service::getLevel().as_ptr();
    auto* pig = lv->fetchEntity(pigId, false);
    auto* bat = lv->fetchEntity(batId, false);
    ctx.require(pig != nullptr && bat != nullptr, "the pig and the bat are still there");
    ctx.check(pig->hasTag(tm::kLeashOverrideTag), "leashed_pathfinder_override stays on a leashable mob");
    ctx.check(!bat->hasTag(tm::kLeashOverrideTag), "leashed_pathfinder_override is dropped from a mob that cannot be leashed");
    pig->setArmor(Slot::Legs, ItemStack::EMPTY_ITEM());
    co_await ctx.ticks(1);
    if (auto* p2 = ll::service::getLevel()->fetchEntity(pigId, false)) {
        ctx.check(p2->getEffect(8u) == nullptr, "the gear came off: no Jump Boost");
        p2->despawn();
    }
    if (auto* b2 = ll::service::getLevel()->fetchEntity(batId, false)) b2->despawn();
});

GF_REGISTER_SELFTEST("tgmove.aquatic_mining", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(1250, 8);
    auto g = ctx.testPos(1253, 1);
    co_await place(ctx, g);
    // water up to the player's eyes
    for (int dy = 0; dy < 3; ++dy) ctx.setBlock(gf::core::GlobalPos{g.dim, g.x, g.y + dy, g.z}, "minecraft:water");
    auto& p = ctx.player();
    p.teleport(at(g), DimensionType(g.dim));
    co_await ctx.ticks(5);
    auto& p1    = ctx.player();
    auto  stone = gf::game::blockWith("minecraft:stone");
    ctx.require(stone != nullptr, "stone");
    unequipAll(p1);
    float without = p1.getDestroyProgress(*stone);
    equip(p1, 0, "twilightforest:travellers_goggles", {"twilightforest:aquatic_agility"});
    float with = p1.getDestroyProgress(*stone);
    ctx.check(without > 0 && std::abs(with / without - 5.0f) < 1e-3f, "submerged mining speed x5: " + num(with) + " / " + num(without));
    unequipAll(p1);
    for (int dy = 0; dy < 3; ++dy) ctx.setBlock(gf::core::GlobalPos{g.dim, g.x, g.y + dy, g.z}, "minecraft:air");
});

GF_REGISTER_SELFTEST("tgmove.armor_time", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(1260, 8);
    co_await place(ctx, ctx.testPos(1263, 1));
    auto& p = ctx.player();
    equip(p, 1, "twilightforest:travellers_vest", {});
    auto& slot = gf::entity::data(p, tm::kLastDamageArmorTimeKey);
    slot       = nullptr;
    gf::combat::hurt(p, 1.0f, "minecraft:fall");
    ctx.check(gf::entity::data(p, tm::kLastDamageArmorTimeKey).is_null(), "fall damage bypasses armour: no armour time");
    co_await ctx.ticks(20); // past the hurt cooldown of the fall
    auto& p1  = ctx.player();
    bool  hit = gf::combat::hurt(p1, 1.0f, "minecraft:cactus");
    auto const& t = gf::entity::data(p1, tm::kLastDamageArmorTimeKey);
    ctx.check(t.is_number_integer() && std::abs(t.get<long long>() - tm::gameTime()) <= 1, "last_damage_armor_time = the game time: " + t.dump() + (hit ? "" : " (the hit did not land)"));
    unequipAll(p1);
    p1.heal(20);
});

// The client e2e scenario's helpers. They act only on real players that carry the entity tag
// kE2eTag, which only the scenario adds (/tag @s add gf_e2e.tgmove): a selftest run on a live
// server (`/twilightforest selftest`, the full run) changes nobody. Armour they replace goes to
// the player's inventory (or is dropped), never destroyed.
constexpr char kE2eTag[] = "gf_e2e.tgmove";

template <class F>
int forE2ePlayers(F&& fn) {
    auto level = ll::service::getLevel();
    if (!level) return 0;
    std::vector<std::int64_t> ids;
    level->forEachPlayer([&](Player& p) {
        if (!p.isSimulated() && p.hasTag(kE2eTag)) ids.push_back(p.getOrCreateUniqueID().rawID);
        return true;
    });
    int n = 0;
    for (auto id : ids)
        if (auto* p = level->getPlayer(ActorUniqueID{id})) {
            fn(*p);
            ++n;
        }
    return n;
}

/// Puts gear in a real player's armour slot; what was there goes to the inventory.
void equipKeeping(Player& p, int slot, std::string const& itemId, std::vector<std::string> const& mods) {
    auto old = p.getArmor(static_cast<Slot>(slot));
    if (!gf::game::items::isEmpty(old) && gf::game::items::idOf(old) != itemId && !p.add(old)) p.drop(old, false);
    equip(p, slot, itemId, mods);
}

// Everything the scenario measures: the wings (double jump, sidestep, glide, agile ranger, built-in
// high jump), the vest (built-in swift swim), the boots (water walk, unrestrained, built-in step up).
GF_REGISTER_SELFTEST("tgmove.e2e.equip", [](Context& ctx) -> ll::coro::CoroTask<> {
    int n = forE2ePlayers([](Player& p) {
        equipKeeping(p, 2, "twilightforest:travellers_wings", {"twilightforest:double_jump", "twilightforest:side_step", "twilightforest:gradual_glide", "twilightforest:agile_ranger"});
        equipKeeping(p, 1, "twilightforest:travellers_vest", {});
        equipKeeping(p, 3, "twilightforest:travellers_boots", {"twilightforest:water_walk", "twilightforest:unrestrained"});
        tm::forget(p);
    });
    ctx.info("equipped " + std::to_string(n) + " players");
    co_return;
});

// The comparison runs: the same gear without agile ranger, unrestrained and swift swim (plain vest
// slot), and with straight ahead.
GF_REGISTER_SELFTEST("tgmove.e2e.plain", [](Context& ctx) -> ll::coro::CoroTask<> {
    int n = forE2ePlayers([](Player& p) {
        equipKeeping(p, 2, "twilightforest:travellers_wings", {"twilightforest:double_jump", "twilightforest:side_step", "twilightforest:gradual_glide"});
        auto vest = p.getArmor(Slot::Torso);
        if (gf::game::items::idOf(vest) == "twilightforest:travellers_vest") p.setArmor(Slot::Torso, ItemStack::EMPTY_ITEM());
        equipKeeping(p, 3, "twilightforest:travellers_boots", {"twilightforest:water_walk", "twilightforest:straight_ahead"});
        tm::forget(p);
    });
    ctx.info("equipped " + std::to_string(n) + " players");
    co_return;
});

// The client e2e scenario's glide: 2 s after it starts (the scenario holds SHIFT meanwhile, typing
// a command would release it), the tagged players are lifted 35 blocks.
GF_REGISTER_SELFTEST("tgmove.e2e.lift", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.ticks(40);
    int n = forE2ePlayers([](Player& p) { p.teleport(p.getFeetPos() + Vec3{0, 35, 0}, p.getDimensionId()); });
    ctx.info("lifted " + std::to_string(n) + " players");
});

} // namespace
