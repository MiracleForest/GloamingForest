// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/passive/Bird.java
//   src/main/java/twilightforest/entity/passive/FlyingBird.java
//   src/main/java/twilightforest/entity/passive/Bighorn.java
//   src/main/java/twilightforest/entity/passive/DwarfRabbit.java
//   src/main/java/twilightforest/entity/passive/TinyBirdVariant.java
//   src/main/java/twilightforest/entity/passive/DwarfRabbitVariant.java
//   src/main/java/twilightforest/entity/monster/Kobold.java
//   src/main/java/twilightforest/entity/monster/MosquitoSwarm.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/fauna/Fauna.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace gf::core::fauna {

namespace {

constexpr std::array<std::string_view, kDyeCount> kDyeNames = {
    "white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
    "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black",
};

enum Dye : int { White, Orange, Magenta, LightBlue, Yellow, Lime, Pink, Gray, LightGray, Cyan, Purple, Blue, Brown, Green, Red, Black };

/// Vanilla 1.21.1's shapeless recipes of exactly two dyes (data/minecraft/recipe/*_dye*.json).
struct Mix {
    int a, b, out;
};
constexpr Mix kMixes[] = {
    {Blue, Green, Cyan},        {Black, White, Gray},     {Blue, White, LightBlue}, {Gray, White, LightGray}, {Green, White, Lime},
    {Purple, Pink, Magenta},    {Red, Yellow, Orange},    {Red, White, Pink},       {Blue, Red, Purple},
};

int signum(double d) { return d > 0.0 ? 1 : (d < 0.0 ? -1 : 0); }

} // namespace

std::string_view dyeName(int id) { return id >= 0 && id < kDyeCount ? kDyeNames[static_cast<std::size_t>(id)] : std::string_view{}; }

std::optional<int> dyeByName(std::string_view name) {
    for (int i = 0; i < kDyeCount; ++i)
        if (kDyeNames[static_cast<std::size_t>(i)] == name) return i;
    return std::nullopt;
}

std::optional<int> dyeOfItem(std::string_view itemId) {
    if (itemId.starts_with("minecraft:")) itemId.remove_prefix(10);
    if (!itemId.ends_with("_dye")) return std::nullopt;
    itemId.remove_suffix(4);
    return dyeByName(itemId);
}

std::string woolOf(int dye) { return "minecraft:" + std::string(dyeName(dye)) + "_wool"; }

std::optional<int> mixDyes(int a, int b) {
    for (auto const& m : kMixes)
        if ((m.a == a && m.b == b) || (m.a == b && m.b == a)) return m.out;
    return std::nullopt;
}

int offspringColor(int a, int b, RandomSource& random) {
    if (auto mixed = mixDyes(a, b)) return *mixed;
    return random.nextBoolean() ? a : b;
}

int randomFleeceColor(RandomSource& random) { return random.nextBoolean() ? kBrown : random.nextInt(16); }

std::string bighornLootTable(bool sheared, int color) {
    if (sheared) return "twilightforest:entities/bighorn_sheep";
    auto name = dyeName(color);
    return "twilightforest:entities/bighorn_sheep/" + std::string(name.empty() ? "white" : name);
}

int shearCount(RandomSource& random) { return 1 + random.nextInt(3); }

int pickVariant(std::vector<Variant> const& variants, std::string_view biome, int fallback, RandomSource& random) {
    std::vector<int> valid;
    for (int i = 0; i < static_cast<int>(variants.size()); ++i) {
        auto const& v = variants[static_cast<std::size_t>(i)];
        if (v.anyBiome || std::find(v.biomes.begin(), v.biomes.end(), biome) != v.biomes.end()) valid.push_back(i);
    }
    return valid.empty() ? fallback : valid[static_cast<std::size_t>(random.nextInt(static_cast<int>(valid.size())))];
}

int pickCommonVariant(std::vector<Variant> const& variants, int fallback, RandomSource& random) {
    std::vector<int> valid;
    for (int i = 0; i < static_cast<int>(variants.size()); ++i)
        if (variants[static_cast<std::size_t>(i)].anyBiome) valid.push_back(i);
    return valid.empty() ? fallback : valid[static_cast<std::size_t>(random.nextInt(static_cast<int>(valid.size())))];
}

int rabbitOffspringVariant(std::vector<Variant> const& variants, int self, int mate, int fallback, RandomSource& random) {
    // the common variant is drawn first (level random in TF; one source here), then replaced 19 times in 20
    int variant = pickCommonVariant(variants, fallback, random);
    if (random.nextInt(20) != 0) variant = random.nextBoolean() ? self : mate;
    return variant;
}

std::optional<int> variantIndex(std::vector<Variant> const& variants, std::string_view id) {
    for (int i = 0; i < static_cast<int>(variants.size()); ++i)
        if (variants[static_cast<std::size_t>(i)].id == id) return i;
    return std::nullopt;
}

void BirdFlap::step(bool onGround) {
    flapIntensity = static_cast<float>(flapIntensity + (onGround ? -1 : 4) * 0.3);
    flapIntensity = std::clamp(flapIntensity, 0.0F, 1.0F);
    if (!onGround && flapSpeed < 1.0F) flapSpeed = 1.0F;
    flapSpeed   = static_cast<float>(flapSpeed * 0.9);
    flapLength += flapSpeed * 2.0F;
}

float BirdFlap::bob() const { return (std::sin(flapLength) + 1.0F) * flapIntensity; }

std::array<int, 3> nextFlightTarget(double x, double y, double z, int flightTime, RandomSource& random) {
    int    yTarget = flightTime < 100 ? 2 : 4;
    // the draw order of the Java expression: x (two), y (one), z (two)
    double tx      = x + static_cast<double>(random.nextInt(7)) - static_cast<double>(random.nextInt(7));
    double ty      = y + static_cast<double>(random.nextInt(6)) - yTarget;
    double tz      = z + static_cast<double>(random.nextInt(7)) - static_cast<double>(random.nextInt(7));
    return {static_cast<int>(std::floor(tx)), static_cast<int>(std::floor(ty)), static_cast<int>(std::floor(tz))};
}

std::array<double, 3> steerTowards(std::array<double, 3> motion, std::array<double, 3> pos, std::array<int, 3> target) {
    double d2 = target[0] + 0.5 - pos[0];
    double d0 = target[1] + 0.1 - pos[1];
    double d1 = target[2] + 0.5 - pos[2];
    return {
        motion[0] + (signum(d2) * 0.5 - motion[0]) * static_cast<double>(0.1F),
        motion[1] + (signum(d0) * static_cast<double>(0.7F) - motion[1]) * static_cast<double>(0.1F),
        motion[2] + (signum(d1) * 0.5 - motion[2]) * static_cast<double>(0.1F),
    };
}

float yawOf(double mx, double mz) { return static_cast<float>(std::atan2(mz, mx) * (180.0F / std::numbers::pi_v<float>)) - 90.0F; }

float wrapDegrees(float degrees) {
    float f = std::fmod(degrees, 360.0F);
    if (f >= 180.0F) f -= 360.0F;
    if (f < -180.0F) f += 360.0F;
    return f;
}

int koboldEatDelay(int difficulty) {
    switch (difficulty) {
    case 1: return 400;
    case 2: return 200;
    case 3: return 100;
    default: return 200;
    }
}

int mosquitoHungerSeconds(int difficulty) {
    switch (difficulty) {
    case 1: return 7;
    case 3: return 30;
    default: return 15;
    }
}

float parrotPitch(RandomSource& random) { return (random.nextFloat() - random.nextFloat()) * 0.2F + 1.0F; }

float voicePitch(RandomSource& random, bool baby) { return (random.nextFloat() - random.nextFloat()) * 0.2F + (baby ? 1.5F : 1.0F); }

} // namespace gf::core::fauna
