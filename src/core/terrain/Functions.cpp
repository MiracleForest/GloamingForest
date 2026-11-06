// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/chunkgenerators/FocusedDensityFunction.java
//   src/main/java/twilightforest/world/components/chunkgenerators/HollowHillFunction.java
//   src/main/java/twilightforest/world/components/chunkgenerators/AbsoluteDifferenceFunction.java
//   src/main/java/twilightforest/world/components/chunkgenerators/TanhHillFunction.java
//   src/main/java/twilightforest/world/components/chunkgenerators/TerrainColumn.java
//   src/main/java/twilightforest/world/components/layer/BiomeDensitySource.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/terrain/Functions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

// Java never contracts a * b + c into an FMA; keep every float / double step as written.
#pragma clang fp contract(off)

namespace gf::core::terrain {

namespace jmth {

int f2i(float v) {
    if (std::isnan(v)) return 0;
    if (v >= 2147483648.0F) return std::numeric_limits<int>::max();
    if (v <= -2147483648.0F) return std::numeric_limits<int>::min();
    return static_cast<int>(v);
}

int d2i(double v) {
    if (std::isnan(v)) return 0;
    if (v >= 2147483647.0) return std::numeric_limits<int>::max();
    if (v <= -2147483648.0) return std::numeric_limits<int>::min();
    return static_cast<int>(v);
}

namespace {
std::array<float, 65536> const& sinTable() {
    static std::array<float, 65536> const table = [] {
        std::array<float, 65536> t{};
        for (int i = 0; i < 65536; ++i) t[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(static_cast<double>(i) * std::numbers::pi * 2.0 / 65536.0));
        return t;
    }();
    return table;
}
} // namespace

float sin(float v) {
    float scaled = v * 10430.378F;
    return sinTable()[static_cast<std::size_t>(f2i(scaled) & 65535)];
}
float cos(float v) {
    float scaled = v * 10430.378F + 16384.0F;
    return sinTable()[static_cast<std::size_t>(f2i(scaled) & 65535)];
}

} // namespace jmth

// ---- Focused -------------------------------------------------------------------------------------------

Focused Focused::fromPos(int x, int y, int z, float radius, float nearValue, float farValue) {
    return Focused{static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F, static_cast<float>(z) + 0.5F, radius, nearValue, farValue};
}

double Focused::compute(int blockX, int blockY, int blockZ) const {
    float dX   = centerX - static_cast<float>(blockX);
    float dY   = bottomY - static_cast<float>(blockY);
    float dZ   = centerZ - static_cast<float>(blockZ);
    float dist = jmth::sqrt(dX * dX + dY * dY + dZ * dZ);
    // Mth.clampedMap(float...) with the int literal 0 widened to float (the float overload is chosen)
    return static_cast<double>(jmth::clampedMap(dist, 0.0F, radius, nearValue, farValue));
}

// ---- HollowHill ----------------------------------------------------------------------------------------

HollowHill HollowHill::fromPos(int x, int y, int z, float radius, float heightScale) {
    return HollowHill{static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F, static_cast<float>(z) + 0.5F, radius, heightScale};
}

double HollowHill::compute(int blockX, int blockY, int blockZ) const {
    float dX = static_cast<float>(blockX) - centerX;
    float dY = static_cast<float>(blockY) - bottomY;
    float dZ = static_cast<float>(blockZ) - centerZ;
    return compute(dX, dY, dZ);
}

double HollowHill::compute(float dX, float dY, float dZ) const {
    float dist           = jmth::sqrt(dX * dX + dZ * dZ);
    float height         = jmth::cos(dist / radius * jmth::kPi) * radius * 0.3333333334F;
    float normalizedDist = jmth::clamp(dist / std::fabs(radius), 0.0F, 1.0F);
    if (normalizedDist >= 1.0F) return 0.0;
    return static_cast<double>(jmth::clamp(height * heightScale - dY, -1.0F, 1.0F));
}

// ---- AbsoluteDifference ------------------------------------------------------------------------------

double AbsoluteDifference::compute(int blockX, int blockZ) const {
    double ax = std::fabs(static_cast<double>(blockX) - centerX);
    double az = std::fabs(static_cast<double>(blockZ) - centerZ);
    // Math.min / Math.max of doubles (no NaN or signed zero can arise from these inputs)
    double v = isMax ? (ax >= az ? ax : az) : (ax <= az ? ax : az);
    return v <= max ? v : max;
}

// ---- TanhHill ------------------------------------------------------------------------------------------

TanhHill TanhHill::withAngle(float centerX, float bottomY, float centerZ, float radius, float heightScale, float angleBiasDirection, bool isXOriented,
                             bool isOnRightSide) {
    return TanhHill{centerX, bottomY, centerZ, radius, heightScale, jmth::cos(angleBiasDirection), jmth::sin(angleBiasDirection), isXOriented, isOnRightSide};
}

double TanhHill::compute(int blockX, int blockY, int blockZ) const {
    constexpr float kPerpendicularBias = 1.3F;
    float           dX                 = static_cast<float>(blockX) - centerX;
    float           dY                 = static_cast<float>(blockY) - bottomY;
    float           dZ                 = static_cast<float>(blockZ) - centerZ;
    if (isXOriented) dZ *= kPerpendicularBias;
    else dX *= kPerpendicularBias;
    return compute(dX, dY, dZ);
}

double TanhHill::compute(float dX, float dY, float dZ) const {
    if (dY > 10.0F || dY < -10.0F) return -1.0;
    float perpendicularDist = isXOriented ? dZ : dX;
    if (!isOnRightSide) perpendicularDist = -perpendicularDist;
    if (perpendicularDist < 0.0F) return -1.0;
    // `double distSquared = dX * dX + dZ * dZ` is a float sum widened; `9 * radius * radius` is float
    double distSquared = static_cast<double>(dX * dX + dZ * dZ);
    if (distSquared > static_cast<double>(9.0F * radius * radius)) return -1.0;
    double dist    = std::sqrt(distSquared);
    double cosPhi  = static_cast<double>(dX) / dist;
    double sinPhi  = static_cast<double>(dZ) / dist;
    double cos2Phi = cosPhi * cosPhi - sinPhi * sinPhi;
    double sin2Phi = 2.0 * cosPhi * sinPhi;
    dist /= (1.0 + (sin2Phi * static_cast<double>(cosAngleBiasDirection) - cos2Phi * static_cast<double>(sinAngleBiasDirection)) / 3.0);
    double r       = static_cast<double>(radius);
    double tanhExp = std::exp(2.0 * (2.0 - 2.0 * dist / r));
    double height  = ((tanhExp - 1.0) / (tanhExp + 1.0)) - dist * (1.0 - std::exp(-dist * r * 0.01));
    if (static_cast<double>(heightScale) * (height + 1.0) > static_cast<double>(dY)) return 1.0;
    return -1.0;
}

// ---- TerrainColumn / TerrainGrid ----------------------------------------------------------------------

std::string const& TerrainColumn::biomeAt(int quartY) const {
    // Stream#reduce over the ascending entries: keep `a` while |a - y| <= |b - y|
    if (layers.empty()) return keyBiome;
    std::size_t best = 0;
    for (std::size_t i = 1; i < layers.size(); ++i) {
        double aDelta = layers[best].first - static_cast<double>(quartY);
        double bDelta = layers[i].first - static_cast<double>(quartY);
        if (!(std::fabs(aDelta) <= std::fabs(bDelta))) best = i;
    }
    return layers[best].second;
}

TerrainGrid TerrainGrid::fromJson(nlohmann::json const& json) {
    TerrainGrid grid;
    if (!json.is_object()) return grid;
    auto it = json.find("biome_landscape");
    if (it == json.end() || !it->is_array()) return grid;
    for (auto const& c : *it) {
        if (!c.is_object() || !c.contains("key_biome") || !c["key_biome"].is_string()) continue;
        auto num = [&](char const* key) -> std::optional<double> {
            auto f = c.find(key);
            if (f == c.end()) return std::nullopt;
            if (f->is_number()) return f->get<double>();
            // a constant density function object
            if (f->is_object() && f->value("type", std::string()) == "minecraft:constant" && f->contains("argument") && (*f)["argument"].is_number())
                return (*f)["argument"].get<double>();
            return std::nullopt;
        };
        auto d = num("depth"), s = num("scale"), w = num("weight");
        if (!d || !s || !w) continue;
        TerrainColumn col;
        col.keyBiome = c["key_biome"].get<std::string>();
        col.depth    = *d;
        col.scale    = *s;
        col.weight   = *w;
        if (auto l = c.find("biome_layers"); l != c.end() && l->is_object()) {
            for (auto const& [k, v] : l->items()) {
                if (!v.is_string()) continue;
                try {
                    col.layers.emplace_back(std::stod(k), v.get<std::string>());
                } catch (...) {
                }
            }
            std::sort(col.layers.begin(), col.layers.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        }
        grid.mColumns[col.keyBiome] = std::move(col);
    }
    return grid;
}

TerrainColumn const* TerrainGrid::column(std::string const& keyBiome) const {
    auto it = mColumns.find(keyBiome);
    return it == mColumns.end() ? nullptr : &it->second;
}

// ---- sampleTerrain -----------------------------------------------------------------------------------

DensityData sampleTerrain(int blockX, int blockZ, KeyBiomeFn const& columnAt) {
    constexpr double kBlendRadius    = 8.75;
    constexpr int    kBlendRadiusInt = 9; // Mth.floor(8.75 + 1.0)
    constexpr int    kOffset         = 2; // QuartPos.SIZE / 2
    // Java float literals: 2f is exactly 2, -0.4f widens to -0.4000000059604645
    constexpr double kTwo      = static_cast<double>(2.0F);
    constexpr double kMinus04  = static_cast<double>(-0.4F);

    double totalMappedDepth = 0.0, totalContribution = 0.0, totalScale = 0.0, totalScaleContribution = 0.0;

    int bxo         = blockX - kOffset;
    int bzo         = blockZ - kOffset;
    int xQuartStart = (bxo - kBlendRadiusInt) >> 2;
    int zQuartStart = (bzo - kBlendRadiusInt) >> 2;
    int xQuartEnd   = (bxo + kBlendRadiusInt) >> 2;
    int zQuartEnd   = (bzo + kBlendRadiusInt) >> 2;
    int xCount      = xQuartEnd - xQuartStart + 1;
    int zCount      = zQuartEnd - zQuartStart + 1;

    double xQuartDelta = static_cast<double>(bxo - (xQuartStart << 2)) * (1.0 / 4.0);
    double zQuartDelta = static_cast<double>(bzo - (zQuartStart << 2)) * (1.0 / 4.0);

    for (int cx = 0; cx < xCount; ++cx) {
        for (int cz = 0; cz < zCount; ++cz) {
            double dX     = xQuartDelta - cx;
            double dZ     = zQuartDelta - cz;
            double distSq = dX * dX + dZ * dZ;
            if (!(distSq < kBlendRadius * kBlendRadius)) continue;
            TerrainColumn const* column = columnAt(cx + xQuartStart, cz + zQuartStart);
            if (!column) continue;
            double falloff      = kBlendRadius * kBlendRadius * column->weight;
            double scaleFalloff = kBlendRadius * kBlendRadius * column->weight;
            double depth        = column->depth;
            double scale        = column->scale;

            falloff *= std::exp((distSq * kTwo + depth) * kMinus04);
            totalMappedDepth += depth * falloff;
            totalContribution += falloff;

            scaleFalloff *= std::exp((distSq * kTwo + scale) * kMinus04);
            totalScale += scale * scaleFalloff;
            totalScaleContribution += scaleFalloff;
        }
    }
    return DensityData{totalMappedDepth / totalContribution, totalScale / totalScaleContribution};
}

} // namespace gf::core::terrain
