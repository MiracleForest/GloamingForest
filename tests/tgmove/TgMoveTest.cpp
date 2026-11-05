// Tests of src/core/tgmove (U12): the numbers and state machines of TF's Travellers' Gear movement.
#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "core/tgmove/TgMove.h"

using namespace gf::core::tgmove;

TEST(TgMove, JumpPower) {
    EXPECT_DOUBLE_EQ(jumpPower(-1), 0.42);
    EXPECT_NEAR(jumpPower(kHighJumpAmplifier), 0.62, 1e-12); // Jump Boost II
    EXPECT_NEAR(jumpPower(0, 0.5), 0.31, 1e-12);             // honey block factor
}

TEST(TgMove, JumpFromGround) {
    auto v = jumpFromGround({0.1, -0.5, 0.0}, 0.42, false, 0);
    EXPECT_DOUBLE_EQ(v.y, 0.42);
    EXPECT_DOUBLE_EQ(v.x, 0.1);
    v = jumpFromGround({0, 0.6, 0}, 0.42, false, 0); // a faster rise is kept
    EXPECT_DOUBLE_EQ(v.y, 0.6);
    v = jumpFromGround({0, 0, 0}, 0.42, true, 0); // yaw 0 faces +z
    EXPECT_NEAR(v.z, 0.2, 1e-6);
    EXPECT_NEAR(v.x, 0.0, 1e-6);
    v = jumpFromGround({0, 0, 0}, 0.42, true, 90); // yaw 90 faces -x
    EXPECT_NEAR(v.x, -0.2, 1e-6);
}

TEST(TgMove, DoubleJumpBoostAndFall) {
    EXPECT_DOUBLE_EQ(boostedJump(0.42, 0), 0.42);
    EXPECT_NEAR(boostedJump(0.3, 0.4), 0.5, 1e-12);
    EXPECT_DOUBLE_EQ(fallDamage(3.0, kSafeFallDistance), 0);
    EXPECT_DOUBLE_EQ(fallDamage(3.1, kSafeFallDistance), 1);
    // safe fall +2 after a double jump: a 5-block fall does not hurt
    EXPECT_LE(fallDamage(5.0, kSafeFallDistance + kDoubleJumpSafeFall), 0);
    EXPECT_GT(fallDamage(5.0, kSafeFallDistance), 0);
}

TEST(TgMove, DoubleJumpConditions) {
    DoubleJumpState s{.hasDoubleJump = true};
    EXPECT_TRUE(canDoubleJump(s));
    for (bool DoubleJumpState::* f : {&DoubleJumpState::fallFlying, &DoubleJumpState::onClimbable, &DoubleJumpState::onGround,
                                      &DoubleJumpState::swimming, &DoubleJumpState::flying, &DoubleJumpState::inLiquid,
                                      &DoubleJumpState::passenger}) {
        auto t = s;
        t.*f   = true;
        EXPECT_FALSE(canDoubleJump(t));
    }
    s.hasDoubleJump = false;
    EXPECT_FALSE(canDoubleJump(s));

    EXPECT_EQ(refreshDoubleJump(false, true, false, false), false);
    EXPECT_EQ(refreshDoubleJump(true, true, false, false), true);
    EXPECT_EQ(refreshDoubleJump(true, false, true, false), true);
    EXPECT_EQ(refreshDoubleJump(true, false, false, true), true);
    EXPECT_EQ(refreshDoubleJump(true, false, false, false), std::nullopt);

    EXPECT_TRUE(avoidCreativeFly(true, 106, 100));
    EXPECT_FALSE(avoidCreativeFly(true, 107, 100));
    EXPECT_FALSE(avoidCreativeFly(false, 101, 100));
    EXPECT_NEAR(doubleJumpPitch(1.0f), 0.98f, 1e-6);
    EXPECT_NEAR(doubleJumpPitch(1.2f), 1.2f, 1e-6);
}

TEST(TgMove, SidestepDoubleTapWindow) {
    // press left (ticks 10..11), release, press again at tick 14: 14 - 11 = 3 < 4 triggers
    SidestepTap tap;
    EXPECT_EQ(detectSidestep(tap, 1, 10, true), std::nullopt); // the first press: last impulse 0, but last walking 0 is long ago
    EXPECT_EQ(detectSidestep(tap, 1, 11, true), std::nullopt);
    EXPECT_EQ(detectSidestep(tap, 0, 12, true), std::nullopt);
    EXPECT_EQ(detectSidestep(tap, 0, 13, true), std::nullopt);
    EXPECT_EQ(detectSidestep(tap, 1, 14, true), true);

    // a difference of 4 does not
    SidestepTap tap2;
    (void)detectSidestep(tap2, -1, 10, true);
    (void)detectSidestep(tap2, 0, 11, true);
    (void)detectSidestep(tap2, 0, 12, true);
    (void)detectSidestep(tap2, 0, 13, true);
    EXPECT_EQ(detectSidestep(tap2, -1, 14, true), std::nullopt);

    // right then right again quickly: false = right
    SidestepTap tap3;
    (void)detectSidestep(tap3, -1, 20, true);
    (void)detectSidestep(tap3, 0, 21, true);
    EXPECT_EQ(detectSidestep(tap3, -1, 22, true), false);

    // the other direction does not
    SidestepTap tap4;
    (void)detectSidestep(tap4, 1, 30, true);
    (void)detectSidestep(tap4, 0, 31, true);
    EXPECT_EQ(detectSidestep(tap4, -1, 32, true), std::nullopt);

    // in the air nothing is recorded
    SidestepTap tap5;
    (void)detectSidestep(tap5, 1, 40, false);
    EXPECT_EQ(tap5.lastWalkingTime, 0);
}

TEST(TgMove, SidestepCooldownAndPush) {
    EXPECT_FALSE(sidestepReady(140, 100, kSidestepCooldown));
    EXPECT_TRUE(sidestepReady(141, 100, kSidestepCooldown));
    auto l = sidestepPush(0, true); // facing +z, left is +x
    EXPECT_NEAR(l.x, 1.6, 1e-9);
    EXPECT_NEAR(l.z, 0.0, 1e-9);
    auto r = sidestepPush(0, false);
    EXPECT_NEAR(r.x, -1.6, 1e-9);
    auto r90 = sidestepPush(90, false); // facing -x, right is -z
    EXPECT_NEAR(r90.z, -1.6, 1e-9);
}

TEST(TgMove, GradualGlide) {
    EXPECT_TRUE(wantsGlide(true, true, -0.1, false));
    EXPECT_FALSE(wantsGlide(true, false, -0.1, false));
    EXPECT_TRUE(wantsGlide(false, false, -0.1, false));
    EXPECT_FALSE(wantsGlide(true, true, 0.1, false));
    EXPECT_FALSE(wantsGlide(true, true, -0.1, true));
    auto vy = glide(-0.6, kGradualGlideMultiplier, true, true, false);
    ASSERT_TRUE(vy);
    EXPECT_NEAR(*vy, -0.5, 1e-6);
    EXPECT_FALSE(glide(-0.6, kGradualGlideMultiplier, true, true, true));
    EXPECT_FALSE(glide(0.1, kGradualGlideMultiplier, true, true, false));
    EXPECT_FALSE(glide(-0.6, kGradualGlideMultiplier, false, true, false));
    EXPECT_NEAR(glideFallDistance(-0.4), 1.0f, 1e-6);
    // the terminal glide speed: ((v - 0.08) x 0.98) x 5/6
    double v = 0;
    for (int i = 0; i < 400; ++i) v = *glide((v - kGravity) * 0.98, kGradualGlideMultiplier, true, true, false);
    EXPECT_NEAR(v, -0.08 * 0.98 * kGradualGlideMultiplier / (1 - 0.98 * kGradualGlideMultiplier), 1e-6);
    EXPECT_LT(glideFallDistance(v), 1.0f); // a long glide never hurts
    float f = 0.3f, l = 0;
    unsneakImpulse(f, l);
    EXPECT_NEAR(f, 1.0f, 1e-6);
    f = 0.1f;
    unsneakImpulse(f, l);
    EXPECT_NEAR(f, 0.5f, 1e-6);
}

TEST(TgMove, SlimySoles) {
    EXPECT_TRUE(slimyCancelsFall(false, true, 1, false));
    EXPECT_TRUE(slimyCancelsFall(false, true, 0, true));
    EXPECT_FALSE(slimyCancelsFall(true, true, 3, true));
    EXPECT_FALSE(slimyCancelsFall(false, false, 3, false));
    EXPECT_FALSE(slimyCancelsFall(false, true, 0, false));

    Slimy s;
    slimyLand(s, -1.0, kSlimySolesCoefficient);
    EXPECT_NEAR(s.bounceVelocity, std::sqrt(0.5), 1e-12);
    EXPECT_DOUBLE_EQ(s.doubleJumpBoostVelocity, s.bounceVelocity);
    auto vy = slimyBounce(s, 0.0);
    ASSERT_TRUE(vy);
    EXPECT_NEAR(*vy, std::sqrt(0.5), 1e-12);
    EXPECT_TRUE(s.forceBounce);
    EXPECT_FALSE(slimyBounce(s, 0.0)); // once per landing
    slimyLand(s, -0.2, kSlimySolesCoefficient);
    auto vy2 = slimyBounce(s, 0.0);
    ASSERT_TRUE(vy2);
    EXPECT_FALSE(s.forceBounce); // 0.14 < 0.25: the bouncing ends
    slimyJump(s);
    EXPECT_EQ(s.bounceVelocity, 0);
    EXPECT_EQ(slimyParticleCount(0), 0);
    EXPECT_EQ(slimyParticleCount(0.3), 20);
    EXPECT_EQ(slimyParticleCount(5.0), 100);
}

TEST(TgMove, WaterWalk) {
    EXPECT_TRUE(waterStandable(0.39, false));
    EXPECT_FALSE(waterStandable(0.4, false));
    EXPECT_FALSE(waterStandable(0.1, true));
    EXPECT_NEAR(waterHeight(0), 8.0 / 9.0, 1e-12);
    EXPECT_NEAR(waterHeight(4), 4.0 / 9.0, 1e-12);
    // standing on the stable shape (half a block) of a source: submerged 0.389 < 0.4
    EXPECT_TRUE(waterStandable(waterHeight(0) - kWaterWalkSurface, false));
    EXPECT_TRUE(waterSplash(31, 0, 0.1));
    EXPECT_FALSE(waterSplash(30, 0, 0.1));
    EXPECT_FALSE(waterSplash(31, 30, 0.1));
    EXPECT_FALSE(waterSplash(31, 0, 0.005));
}

TEST(TgMove, Validator) {
    Validator v;
    // packets 45+ ticks apart never add up
    EXPECT_EQ(validate(v, 100, false), Abuse::Accept);
    EXPECT_EQ(v.count, 0);
    EXPECT_EQ(validate(v, 200, false), Abuse::Accept);
    EXPECT_EQ(v.count, 0);
    // quick ones do: counts 0 and 1 are accepted, 2..4 snap back, 5 disconnects
    EXPECT_EQ(validate(v, 201, false), Abuse::Accept); // 0 -> 1
    EXPECT_EQ(validate(v, 202, false), Abuse::Accept); // 1 -> 2
    EXPECT_EQ(validate(v, 203, false), Abuse::SnapBack);
}

TEST(TgMove, ValidatorThresholds) {
    Validator v{.count = 0, .lastCheck = 0};
    std::vector<Abuse> seen;
    for (int t = 1; t <= 7; ++t) seen.push_back(validate(v, t, false));
    // count before: 0 1 2 3 4 5 (disconnect)
    EXPECT_EQ(seen[0], Abuse::Accept);
    EXPECT_EQ(seen[1], Abuse::Accept);
    EXPECT_EQ(seen[2], Abuse::SnapBack);
    EXPECT_EQ(seen[3], Abuse::SnapBack);
    EXPECT_EQ(seen[4], Abuse::SnapBack);
    EXPECT_EQ(seen[5], Abuse::Disconnect);
    // elytra flight keeps counting across a 45 tick gap
    Validator e{.count = 3, .lastCheck = 0};
    EXPECT_EQ(validate(e, 100, true), Abuse::SnapBack);
    EXPECT_EQ(e.count, 4);
}

TEST(TgMove, WingStates) {
    Wings w;
    WingInputs ground{.onGround = true};
    EXPECT_FALSE(determineWingState(w, ground));
    EXPECT_EQ(w.state, WingState::Idle);
    WingInputs walking{.onGround = true, .walkSpeed = 0.5f};
    EXPECT_TRUE(determineWingState(w, walking));
    EXPECT_EQ(w.state, WingState::Walk);
    WingInputs sprint{.onGround = true, .sprinting = true};
    (void)determineWingState(w, sprint);
    EXPECT_EQ(w.state, WingState::Sprint);
    WingInputs air{.onGround = false, .fallDistance = 1.0f, .vy = -0.2};
    (void)determineWingState(w, air);
    EXPECT_EQ(w.state, WingState::FallSlow);
    WingInputs fall{.onGround = false, .fallDistance = 3.0f, .vy = -0.5};
    (void)determineWingState(w, fall);
    EXPECT_EQ(w.state, WingState::FallFast);
    WingInputs ride{.passenger = true};
    (void)determineWingState(w, ride);
    EXPECT_EQ(w.state, WingState::Ride);
    WingInputs swim{.swimming = true};
    (void)determineWingState(w, swim);
    EXPECT_EQ(w.state, WingState::Swim);

    // double jump locks 15 ticks, sidestep 11
    startDoubleJump(w);
    int locked = 0;
    while (w.state == WingState::DoubleJump) {
        (void)determineWingState(w, ground);
        if (w.state == WingState::DoubleJump) ++locked;
        ASSERT_LT(locked, 100);
    }
    EXPECT_EQ(locked, kDoubleJumpDuration - 1); // the tick that starts it plus 14 more
    startSidestep(w, true);
    locked = 0;
    while (w.state == WingState::Sidestep) {
        (void)determineWingState(w, ground);
        if (w.state == WingState::Sidestep) ++locked;
        ASSERT_LT(locked, 100);
    }
    EXPECT_EQ(locked, kSidestepDuration - 1);
    EXPECT_TRUE(w.sidestepLeft);

    EXPECT_EQ(wingStateName(WingState::FallFast), "fall_fast");
    EXPECT_EQ(wingStateFromName("double_jump"), WingState::DoubleJump);
    EXPECT_EQ(wingStateFromName("nope"), std::nullopt);
}

TEST(TgMove, WalkAnimation) {
    float s = 0;
    for (int i = 0; i < 30; ++i) s = walkAnimation(s, 0.216);
    EXPECT_NEAR(s, 0.864f, 1e-3);
    EXPECT_NEAR(walkAnimation(0, 1.0), 0.4f, 1e-6);
}

TEST(TgMove, WingsAnimation) {
    EXPECT_NEAR(interpolateToTarget(0, 1, 4, 4), 1 - std::exp(-1.0), 1e-12);
    WingsAnim a;
    Wings     w;
    w.state = WingState::DoubleJump;
    WingPose p{};
    for (int t = 1; t <= 200; ++t) p = animateWings(a, w, t, 0, false);
    EXPECT_NEAR(p.rightX, -0.4f, 1e-4);
    EXPECT_NEAR(p.rightY, -0.8f, 1e-4);
    EXPECT_NEAR(p.rightZ, -0.1f, 1e-4);
    EXPECT_NEAR(p.leftY, 0.8f, 1e-4); // mirrored
    EXPECT_NEAR(p.leftZ, 0.1f, 1e-4);
    // idle: around (30 deg, -0.6, -0.3) within the big swing (1/15)
    w.state = WingState::Idle;
    for (int t = 201; t <= 400; ++t) p = animateWings(a, w, t, 0, true);
    EXPECT_NEAR(p.rightX, 3.14159265f / 6, 1.0f / 15 + 1e-3);
    EXPECT_NEAR(p.rightY, -0.6f, 1.0f / 15 + 1e-3);
    EXPECT_EQ(p.drop, 2.0f);
    EXPECT_LT(a.phase, 2 * 3.1415927);
}

TEST(TgMove, InputModifiers) {
    EXPECT_DOUBLE_EQ(straightAheadMultiplier(true, 1.4, 1.0f), 1.4);
    EXPECT_DOUBLE_EQ(straightAheadMultiplier(true, 1.4, 0.0f), 1.0);
    EXPECT_DOUBLE_EQ(straightAheadMultiplier(false, 1.4, 1.0f), 1.0);
    EXPECT_NEAR(speedFovFactor(0.1, 0.1), 1.0f, 1e-6);
    EXPECT_NEAR(speedFovFactor(0.14, 0.1), 1.2f, 1e-6);
    EXPECT_FALSE(keepsAir(kOxygenBonus, 0));
    EXPECT_TRUE(keepsAir(kOxygenBonus, 2));
}

TEST(TgMove, UnsneakImpulseEngineScalesLater) {
    // Bedrock applies its sneak factor after the impulses: 1 / 0.3 of the unit input, so the
    // engine's 0.3 brings it back to TF's full input
    float f = 1.0f, l = 0.0f;
    unsneakImpulse(f, l, true, 0.3f);
    EXPECT_NEAR(f * 0.3f, 1.0f, 1e-6);
    f = 0.6f, l = 0.8f; // diagonal: the length 1 kept
    unsneakImpulse(f, l, true, 0.3f);
    EXPECT_NEAR(std::sqrt(f * f + l * l) * 0.3f, 1.0f, 1e-5);
}

TEST(TgMove, UnrestrainedFriction) {
    EXPECT_NEAR(groundAccelFactor(0.6), 1.0, 1e-6);   // 0.21600002 / 0.216
    EXPECT_NEAR(groundAccelFactor(0.98), 0.2295, 1e-4); // ice
    // the input acceleration: forward at yaw 0 faces +z, a left strafe faces +x
    auto a = inputAcceleration(0, 1, 0, 0.1);
    EXPECT_NEAR(a.z, 0.1, 1e-9);
    EXPECT_NEAR(a.x, 0.0, 1e-9);
    a = inputAcceleration(1, 0, 0, 0.1);
    EXPECT_NEAR(a.x, 0.1, 1e-9);
    a = inputAcceleration(1, 1, 90, 0.1); // normalized to length 1
    EXPECT_NEAR(std::sqrt(a.x * a.x + a.z * a.z), 0.1, 1e-9);
    EXPECT_DOUBLE_EQ(inputAcceleration(0, 0, 0, 0.1).x, 0.0);

    // TF's 0.6 ground step against a simulated engine with ice friction: the engine moves by
    // v + a(f) and keeps (v + a(f)) x f x 0.91; with unrestrainedStep in front the positions and
    // velocities follow Java's 0.6 sequence
    double const f = 0.98, speed = 0.1;
    Velocity     acc = inputAcceleration(0, 1, 0, speed);
    Velocity     tf{0, 0, 0.3}, engine{0, 0, 0.3};
    double       rescale = 1.0, tfPos = 0, enginePos = 0;
    for (int i = 0; i < 20; ++i) {
        if (i == 10) acc = {}; // the key released: TF stops within a block
        // TF
        double move = tf.z + acc.z * groundAccelFactor(0.6);
        tfPos += move;
        tf.z = move * 0.6 * 0.91;
        // the engine behind unrestrainedStep
        auto step = unrestrainedStep(engine, rescale, acc, f, 1.0, true, true);
        rescale   = step.nextRescale;
        double m  = step.velocity.z + acc.z * groundAccelFactor(f);
        enginePos += m;
        engine.z = m * f * 0.91;
        EXPECT_NEAR(enginePos, tfPos, 1e-9) << "tick " << i;
        EXPECT_NEAR(engine.z * rescale, tf.z, 1e-9) << "tick " << i;
    }
    // off the ground: only the previous step's drag is restored
    auto air = unrestrainedStep({0.2, 0.1, 0.2}, 0.5, {0.05, 0, 0}, f, 1.0, false, true);
    EXPECT_NEAR(air.velocity.x, 0.1, 1e-9);
    EXPECT_NEAR(air.velocity.y, 0.1, 1e-9);
    EXPECT_DOUBLE_EQ(air.nextRescale, 1.0);
    // a slowdown of the engine's own (slime's step-on) is undone with the friction
    auto slime = unrestrainedStep({0, 0, 0}, 1.0, {}, 0.8, 0.4, true, true);
    EXPECT_NEAR(slime.nextRescale, 0.6 / (0.8 * 0.4), 1e-9);
    // normal blocks: nothing to do
    auto stone = unrestrainedStep({0.1, 0, 0}, 1.0, acc, 0.6, 1.0, true, true);
    EXPECT_DOUBLE_EQ(stone.nextRescale, 1.0);
    EXPECT_NEAR(stone.velocity.x, 0.1, 1e-6);
}

TEST(TgMove, UnrestrainedSoulSandAndHoneyJump) {
    // an engine that slows the input acceleration (soul sand): the extra makes it TF's full one
    double const k = 0.5;
    Velocity     acc{0, 0, 0.1};
    auto         s = unrestrainedStep({}, 1.0, acc, 0.6, 1.0, true, true, k);
    EXPECT_NEAR(s.velocity.z + acc.z * groundAccelFactor(0.6) * k, acc.z * groundAccelFactor(0.6), 1e-9);
    // the honey block's jump (Bedrock 0.42 x 0.6): made up to 0.42
    double vyEngine = (0.42 * 0.6 - kGravity) * 0.98;
    auto   j        = unrestrainedJump(vyEngine, 0.42);
    ASSERT_TRUE(j.has_value());
    EXPECT_NEAR(j->lost, 0.42 * 0.4, 1e-9);
    // the step moves TF's jump step plus the lost height; the velocity after it, less lost x 0.98, is TF's
    double tfVy = (0.42 - kGravity) * 0.98;
    EXPECT_NEAR(j->vy, tfVy + j->lost, 1e-9);
    EXPECT_NEAR((j->vy - kGravity) * 0.98 - j->lost * 0.98, (tfVy - kGravity) * 0.98, 1e-9);
    EXPECT_FALSE(unrestrainedJump((0.42 - kGravity) * 0.98, 0.42).has_value()); // a full jump
}

TEST(TgMove, SlimeBlockUnrestrained) {
    EXPECT_NEAR(slimeStepFactor(0.0, false), 0.4, 1e-12);
    EXPECT_NEAR(slimeStepFactor(0.05, false), 0.41, 1e-12);
    EXPECT_DOUBLE_EQ(slimeStepFactor(0.0, true), 1.0);
    EXPECT_DOUBLE_EQ(slimeStepFactor(0.2, false), 1.0);
    // walking (-0.0784 per tick) bounces no more; a real fall still does
    auto s = slimeBounceStop(-0.0784, 0.0784);
    ASSERT_TRUE(s.has_value());
    EXPECT_DOUBLE_EQ(*s, 0.0);
    EXPECT_FALSE(slimeBounceStop(-0.5, 0.5).has_value());
    EXPECT_FALSE(slimeBounceStop(-0.0784, 0.0).has_value());
    EXPECT_FALSE(slimeBounceStop(-0.0784, 0.42).has_value()); // a jump off the slime stays
}

TEST(TgMove, StraightAheadFov) {
    EXPECT_FLOAT_EQ(straightAheadFovCompensation(0.14, 0.1, 1.0), 1.0f);
    // walking x1.4: the FOV of 0.14 back to the FOV of 0.1
    EXPECT_NEAR(straightAheadFovCompensation(0.14, 0.1, 1.4) * speedFovFactor(0.14, 0.1), speedFovFactor(0.1, 0.1), 1e-6);
    // sprinting (0.13) x1.4
    EXPECT_NEAR(straightAheadFovCompensation(0.182, 0.1, 1.4) * speedFovFactor(0.182, 0.1), speedFovFactor(0.13, 0.1), 1e-6);
}

TEST(TgMove, EfficientEaterRefund) {
    EXPECT_FLOAT_EQ(efficientEaterRefund(0.1f, 0.1f), 0.05f);
    EXPECT_FLOAT_EQ(efficientEaterRefund(0.1f, 0.02f), 0.01f); // the game added less: half of that
    EXPECT_FLOAT_EQ(efficientEaterRefund(0.1f, 0.5f), 0.05f);  // mining added more: only the movement
    EXPECT_NEAR(efficientEaterRefund(0.1f, 0.1f - 4.0f), 0.05f, 1e-6); // past 4: the wrap
    EXPECT_FLOAT_EQ(efficientEaterRefund(0.0f, 0.3f), 0.0f);
    EXPECT_FLOAT_EQ(efficientEaterRefund(0.1f, 0.0f), 0.0f);
}

TEST(TgMove, WingsPhaseSeed) {
    WingsAnim a;
    seedPhase(a, 0.0);
    EXPECT_DOUBLE_EQ(a.phase, 0.0);
    seedPhase(a, 0.5);
    EXPECT_NEAR(a.phase, 3.14159265358979, 1e-9);
    seedPhase(a, 0.999);
    EXPECT_LT(a.phase, 2 * 3.14159265358979);
}