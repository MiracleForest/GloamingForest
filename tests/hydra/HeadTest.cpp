// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// gtests of src/core/hydra (U24): the Hydra head state machine, poses and part geometry, and the
// Hydra's attributes against the Java oracle's tests/golden/mobs.json (F13).
#include <gtest/gtest.h>

#include <cmath>

#include "core/hydra/Head.h"
#include "../golden/GoldenRepo.h"

using namespace gf::core::hydra;

TEST(HydraHead, StartStates) {
    Head a(0, true), d(4, false);
    EXPECT_EQ(a.currentState, State::Idle);
    EXPECT_EQ(a.ticksNeeded, 60);
    EXPECT_EQ(a.ticksProgress, 60);
    EXPECT_TRUE(a.isActive());
    EXPECT_TRUE(a.isIdle());
    EXPECT_EQ(d.currentState, State::Dead);
    EXPECT_EQ(d.ticksNeeded, 20);
    EXPECT_TRUE(d.isDead());
    EXPECT_FALSE(d.isActive());
    EXPECT_TRUE(d.canRespawn());
}

TEST(HydraHead, Durations) {
    EXPECT_EQ(duration(State::Idle), 10);
    EXPECT_EQ(duration(State::BiteReady), 80);
    EXPECT_EQ(duration(State::Biting), 7);
    EXPECT_EQ(duration(State::Flaming), 100);
    EXPECT_EQ(duration(State::MortarShooting), 25);
    EXPECT_EQ(duration(State::Dying), 70);
    EXPECT_EQ(duration(State::AttackCooldown), 80);
    EXPECT_EQ(duration(State::RoarRawr), 50);
    EXPECT_EQ(automaticNext(State::BiteEnding), State::AttackCooldown);
    EXPECT_EQ(automaticNext(State::Dead), State::Dead);
    EXPECT_EQ(automaticNext(State::Born), State::RoarStart);
    EXPECT_EQ(stateFromName("MORTAR_SHOOTING"), State::MortarShooting);
    EXPECT_EQ(static_cast<int>(State::RoarRawr), 16);
}

TEST(HydraHead, BiteSequence) {
    Head h(0, true);
    h.setNextState(State::BiteBeginning);
    EXPECT_TRUE(h.isBiting());
    EXPECT_FALSE(h.isIdle());
    h.advanceHeadState(); // 61 >= 60: switch
    EXPECT_EQ(h.currentState, State::BiteBeginning);
    EXPECT_EQ(h.prevState, State::Idle);
    EXPECT_EQ(h.ticksProgress, 0);
    int ticks = 0;
    while (h.currentState != State::Idle && ticks < 1000) {
        h.advanceHeadState();
        ++ticks;
    }
    // 40 + 80 + 7 + 40 + 80 (cooldown)
    EXPECT_EQ(ticks, 247);
}

TEST(HydraHead, SecondarySkipsCooldown) {
    Head h(3, true);
    h.isSecondaryAttacking = true;
    h.setNextState(State::FlameBeginning);
    h.advanceHeadState();
    int ticks = 0;
    while (h.currentState != State::Idle && ticks < 1000) {
        h.advanceHeadState();
        ++ticks;
    }
    EXPECT_EQ(ticks, 40 + 100 + 30);
    EXPECT_FALSE(h.isSecondaryAttacking);
}

TEST(HydraHead, Respawn) {
    Head h(5, false);
    h.respawnCounter = kHeadRespawnTicks;
    int born = -1;
    for (int t = 1; t <= 200 && born < 0; ++t) {
        if (h.advanceRespawnCounter()) born = t;
        h.advanceHeadState();
    }
    EXPECT_EQ(born, 140);
    EXPECT_EQ(h.currentState, State::Born);
    EXPECT_EQ(h.respawnCounter, -1);
    EXPECT_EQ(h.damageTaken, 0);
}

TEST(HydraHead, PosesLerp) {
    Head h(1, true);
    EXPECT_FLOAT_EQ(h.currentNeckLength(), 9.0F);
    EXPECT_FLOAT_EQ(h.currentYRotation(), 60.0F);
    h.setNextState(State::FlameBeginning);
    h.advanceHeadState();
    for (int i = 0; i < 20; ++i) h.advanceHeadState();
    // halfway from IDLE (10, 60, 9, 0) to FLAME_BEGINNING (30, 45, 9, .75)
    EXPECT_FLOAT_EQ(h.currentXRotation(), 20.0F);
    EXPECT_FLOAT_EQ(h.currentYRotation(), 52.5F);
    EXPECT_FLOAT_EQ(h.currentMouthOpen(), 0.375F);
    // heads 3..6 have no bite poses: IDLE stands in
    EXPECT_FLOAT_EQ(pose(4, State::Biting).yRot, -90.0F);
    EXPECT_FLOAT_EQ(pose(2, State::Dead).yRot, -180.0F);
    EXPECT_FLOAT_EQ(pose(6, State::RoarRawr).neckLength, 11.0F);
}

TEST(HydraHead, Geometry) {
    // body 3 blocks behind the hydra, tail 10.5 (yBodyRot 0 faces +z: behind is -z)
    V3 body = bodyPosition({0, 64, 0}, 0.0F);
    EXPECT_NEAR(body.x, 0.0, 1e-4);
    EXPECT_NEAR(body.y, 64.1, 1e-9);
    EXPECT_NEAR(body.z, -3.0, 1e-4);
    V3 tail = tailPosition({0, 64, 0}, 90.0F);
    EXPECT_NEAR(tail.x, 10.5, 1e-3);
    EXPECT_NEAR(tail.z, 0.0, 1e-3);
    // head 0 idle at tick 0 (no swing): neck 7 turned up by 60 degrees, 3 above the hydra
    Head h(0, true);
    V3   p   = headPosition(h, {0, 0, 0}, 0.0F, 0);
    double a = 60.0 * 3.1415927 / 180.0;
    EXPECT_NEAR(p.y, 3.0 + 7.0 * std::sin(a), 1e-3);
    EXPECT_NEAR(p.z, 7.0 * std::cos(a), 1e-3);
    EXPECT_NEAR(p.x, 0.0, 1e-3);
    // neck anchors
    V3 n1 = neckAnchor(1, {0, 0, 0}, 0.0F);
    EXPECT_NEAR(n1.y, 3.0, 1e-9);
    EXPECT_NEAR(std::hypot(n1.x, n1.z), std::hypot(1.0, 3.0), 1e-3);
    // look vectors
    V3 v = viewVector(0.0F, 0.0F);
    EXPECT_NEAR(v.z, 1.0, 1e-4);
    V3 d = viewVector(90.0F, 0.0F);
    EXPECT_NEAR(d.y, -1.0, 1e-4);
}

TEST(HydraHead, Necks) {
    auto necks = neckPositions({0, 3, 0}, 0.0F, {0, 10, 8}, 0.0F, 20.0F);
    // pitched down: the head end is pulled back 1.8 along the yaw
    EXPECT_NEAR(necks[0].pos.z, 8.0 - 1.8, 1e-3);
    EXPECT_NEAR(necks[0].pos.y, 9.5, 1e-6);
    EXPECT_NEAR(necks[4].pos.z, 0.0, 1e-6);
    EXPECT_NEAR(necks[4].pos.y, 3.0, 1e-6);
    EXPECT_FLOAT_EQ(necks[0].xRot, 20.0F);
    EXPECT_FLOAT_EQ(necks[4].xRot, 0.0F);
    EXPECT_FLOAT_EQ(necks[2].xRot, 10.0F);
}

TEST(HydraHead, Facing) {
    EXPECT_FLOAT_EQ(wrapDegrees(190.0F), -170.0F);
    EXPECT_FLOAT_EQ(wrapDegrees(-180.0F), -180.0F);
    EXPECT_FLOAT_EQ(updateRotation(0.0F, 30.0F, 5.0F), 5.0F);
    EXPECT_FLOAT_EQ(updateRotation(0.0F, -30.0F, 5.0F), -5.0F);
    // a target straight ahead (+z) at the head's height + 1: yaw 0
    Facing f = faceVec({0, 0, 0}, 10.0F, 0.0F, {0, 1, 10}, 5.0F, 500.0F);
    EXPECT_FLOAT_EQ(f.yRot, 5.0F);
    EXPECT_NEAR(f.xRot, 0.0F, 1e-4);
    // body follows the yaw only past 50 degrees, at most 75 behind
    EXPECT_FLOAT_EQ(tickBodyTurn(0.0F, 40.0F), 0.0F);
    EXPECT_FLOAT_EQ(tickBodyTurn(0.0F, 60.0F), 12.0F);
    EXPECT_FLOAT_EQ(tickBodyTurn(0.0F, 100.0F), 25.0F + 15.0F);
}

TEST(HydraHead, Attacks) {
    std::array<Head, kMaxHeads> heads;
    for (int i = 0; i < kMaxHeads; ++i) heads[i] = Head(i, i < 3);
    EXPECT_EQ(countActiveHeads(heads), 3);
    EXPECT_EQ(activeMask(heads), 0b0000111);
    EXPECT_FALSE(tooManyAttacking(heads, 0, false));
    heads[1].currentState = State::Flaming;
    // one other attack >= 1 + 3 * 0.3
    EXPECT_FALSE(tooManyAttacking(heads, 0, false));
    heads[2].currentState = State::BiteReady;
    EXPECT_TRUE(tooManyAttacking(heads, 0, false));
    EXPECT_TRUE(otherHeadsBiting(heads, 0));
    EXPECT_FALSE(otherHeadsBiting(heads, 2));
    std::array<Head, kMaxHeads> loaded;
    for (int i = 0; i < kMaxHeads; ++i) loaded[i] = Head(i, i < 3);
    activateHeadsOnLoad(loaded, 0b1010001);
    for (auto& h : loaded) h.advanceHeadState();
    EXPECT_EQ(loaded[4].currentState, State::Idle);
    EXPECT_EQ(loaded[6].currentState, State::Idle);
    EXPECT_EQ(loaded[3].currentState, State::Dead);
    EXPECT_EQ(kPartCount, 44);
    EXPECT_EQ(neckPartIndex(6, 4), 43);
}

TEST(HydraHead, OracleAttributes) {
    auto dir = gf::tests::golden::goldenDir();
    if (dir.empty()) GTEST_SKIP() << "tests/golden not found";
    auto mobs = gf::tests::golden::readJson(dir / "mobs.json");
    ASSERT_FALSE(mobs.is_discarded());
    nlohmann::json const* hydra = nullptr;
    // the file nests the mobs under one object level
    for (auto const& [k, v] : mobs.items()) {
        if (k == "twilightforest:hydra") hydra = &v;
        else if (v.is_object() && v.contains("twilightforest:hydra")) hydra = &v["twilightforest:hydra"];
    }
    ASSERT_NE(hydra, nullptr);
    EXPECT_DOUBLE_EQ((*hydra)["maxHealth"].get<double>(), kMaxHealth);
    EXPECT_DOUBLE_EQ((*hydra)["attributes"]["minecraft:generic.movement_speed"]["base"].get<double>(), kMovementSpeed);
    EXPECT_EQ((*hydra)["experienceMin"].get<int>(), kXpReward);
    EXPECT_TRUE((*hydra)["fireImmune"].get<bool>());
}
