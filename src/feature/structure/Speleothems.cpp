// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/StructureSpeleothemConfig.java
//   src/main/java/gloamingforest/data/custom/stalactites/entry/SpeleothemVarietyConfig.java
//   src/main/java/gloamingforest/data/custom/stalactites/entry/Stalactite.java
//   src/main/java/gloamingforest/data/custom/stalactites/entry/StalactiteReloadListener.java
//   src/main/java/gloamingforest/world/components/feature/BlockSpikeFeature.java
//   src/main/java/gloamingforest/util/iterators/RectangleLatticeIterator.java
//   src/main/java/gloamingforest/util/iterators/ZippedIterator.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "feature/structure/Internal.h"

#include <charconv>
#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <shared_mutex>

#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockProperty.h"

namespace gf::structure::speleothems {

namespace {

/// Stalactite: Either<List<Pair<Block, Integer>>, Block> ores, size variation, max length, weight.
struct Stalactite {
    std::string                              ore;  ///< a single block (Either.right)
    std::vector<std::pair<std::string, int>> ores; ///< a weighted list (Either.left)
    float                                    sizeVariation = 0.25F;
    int                                      maxLength     = 11;
    int                                      weight        = 1;
};
/// BlockSpikeFeature.STONE_STALACTITE.
Stalactite const& stoneStalactite() {
    static Stalactite const s{"minecraft:stone", {}, 0.25F, 11, 1};
    return s;
}

struct Variety {
    std::string             type;
    std::vector<Stalactite> base, ore, stalagmites;
    float                   oreChance = 0, stalactiteChance = 0, stalagmiteChance = 0;
    /// The compiled weighted pickers (StructureSpeleothemConfig#compileStalactites / Stalagmites).
    std::vector<std::pair<Stalactite, int>> stalactiteList, stalagmiteList;
};

struct Config {
    std::string type;
    Lattice     lattice;
};

std::shared_mutex              gMutex;
std::map<std::string, Variety> gVarieties; ///< by type ("small_hollow_hill")
std::map<std::string, Config>  gConfigs;   ///< by id ("gloamingforest:small_hollow_hill")

Stalactite parseStalactite(Json const& j) {
    Stalactite s;
    if (j["ores"].is_string()) s.ore = j["ores"].get<std::string>();
    else
        for (auto const& p : j["ores"]) s.ores.emplace_back(p.value("block", std::string("minecraft:stone")), p.value("weight", 1));
    s.sizeVariation = j.value("size_variation", 0.25F);
    s.maxLength     = j.value("max_length", 11);
    s.weight        = j.value("weight", 1);
    return s;
}

/// Java's Float.toString for the small values upstream uses (shortest round-trip, ".0" on integers).
std::string javaFloat(float v) {
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), v);
    std::string s(buf, res.ptr);
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos) s += ".0";
    return s;
}

std::vector<std::pair<Stalactite, int>> simple(std::vector<Stalactite> const& list) {
    std::vector<std::pair<Stalactite, int>> out;
    for (auto const& s : list) out.emplace_back(s, s.weight);
    return out;
}

std::vector<std::pair<Stalactite, int>> compileStalactites(Variety const& v) {
    float w            = std::clamp(v.oreChance, 0.0F, 1.0F);
    int   stoneWeight  = 0, oreWeight = 0;
    for (auto const& s : v.base) stoneWeight += s.weight;
    for (auto const& s : v.ore) oreWeight += s.weight;
    auto totalWeight = static_cast<float>(stoneWeight + oreWeight);
    if (totalWeight <= 0) return {};
    if (v.base.empty() || stoneWeight <= 0) return simple(v.ore);
    if (v.ore.empty() || oreWeight <= 0) return simple(v.base);
    int    digits             = static_cast<int>((javaFloat(w) + javaFloat(totalWeight)).size()) - 4;
    double quantization       = std::ceil(std::pow(10.0, std::clamp(digits, 2, 6)));
    double stoneCounterweight = quantization * static_cast<double>(1.0F - w) / stoneWeight;
    double oreCounterweight   = quantization * static_cast<double>(w) / oreWeight;
    if (stoneCounterweight <= 0) return simple(v.ore);
    if (oreCounterweight <= 0) return simple(v.base);
    std::vector<std::pair<Stalactite, int>> out;
    for (auto const& s : v.base) out.emplace_back(s, pure::mthCeil(s.weight * stoneCounterweight));
    for (auto const& s : v.ore) out.emplace_back(s, pure::mthCeil(s.weight * oreCounterweight));
    return out;
}

/// WeightedRandomList#getRandom over a compiled list (empty: BlockSpikeFeature::defaultRandom).
Stalactite const& pick(std::vector<std::pair<Stalactite, int>> const& list, RandomSource& random) {
    int total = 0;
    for (auto const& [s, w] : list) total += w;
    if (list.empty() || total <= 0) return stoneStalactite();
    int i = random.nextInt(total);
    for (auto const& [s, w] : list) {
        i -= w;
        if (i < 0) return s;
    }
    return stoneStalactite();
}

bool isBlockNotOk(Block const& b) {
    auto const& id = blocks::id(b);
    return blocks::isLiquid(b) || id == "minecraft:bedrock" || id.starts_with("gloamingforest:giant_") || blocks::inTag(b, "gloamingforest:clouds") ||
           id == "gloamingforest:hardened_dark_leaves";
}

bool faceSturdyUp(Block const& b) { return !b.isAir() && !blocks::isLiquid(b) && (b.hasProperty(BlockProperty::CubeShaped) || !blocks::isPartial(b)); }

bool makeSpike(Region& region, BlockPos const& start, Stalactite const& config, int length, int dY, RandomSource& random, bool hang) {
    int diameter = static_cast<int>(static_cast<float>(length) / 4.5F);
    if (!hang) {
        BlockPos     below{start.x, start.y - 2, start.z};
        Block const& belowState = region.getBlock(below);
        if (!blocks::inTag(belowState, "gloamingforest:supports_stalagmites") &&
            (!blocks::worldGenReplaceable(belowState) || !faceSturdyUp(belowState) || isBlockNotOk(belowState)))
            return false;
    }
    for (int dx = -diameter; dx <= diameter; dx++) {
        for (int dz = -diameter; dz <= diameter; dz++) {
            int absx = std::abs(dx), absz = std::abs(dz);
            int dist = static_cast<int>(static_cast<float>(std::max(absx, absz)) + static_cast<float>(std::min(absx, absz)) * 0.5F);
            int spikeLength = dist <= 0 ? length : random.nextInt(static_cast<int>(static_cast<float>(length) / (static_cast<float>(dist) + 0.25F)));
            for (int i = -1; i < spikeLength; i++) {
                BlockPos p{start.x + dx, start.y + i * dY, start.z + dz};
                if (!blocks::worldGenReplaceable(region.getBlock(p))) continue;
                if (!(dY > 0 || p.y < region.height(worldgen::Heightmap::MotionBlockingNoLeaves, p.x, p.z) - 2)) continue;
                if (!config.ore.empty()) {
                    region.setBlock(p, blocks::java(config.ore), 3);
                } else {
                    int total = 0;
                    for (auto const& [id, w] : config.ores) total += w;
                    std::string chosen = "minecraft:stone";
                    if (total > 0) {
                        int r = random.nextInt(total);
                        for (auto const& [id, w] : config.ores) {
                            r -= w;
                            if (r < 0) {
                                chosen = id;
                                break;
                            }
                        }
                    }
                    region.setBlock(p, blocks::java(chosen), 3);
                }
            }
        }
    }
    return true;
}

bool startSpikeRaw(Region& region, BlockPos const& start, Stalactite const& config, int length, int lengthMinimum, int lengthMaximum, int clearance,
                   bool hang, RandomSource& random) {
    if (lengthMaximum < std::max(lengthMinimum, 1)) return false;
    BlockPos moving        = start;
    int      clearedLength = 0;
    int      dY            = hang ? -1 : 1;
    for (int i = 0; i < length; i++) {
        clearedLength = i;
        if (blocks::worldGenReplaceable(region.getBlock(moving))) break;
        moving.y += dY;
    }
    moving.y += dY;
    int remainingScanLength = length - clearedLength + clearance;
    int finalLength         = clearedLength - clearance;
    for (int i = 0; i < remainingScanLength; i++) {
        finalLength = clearedLength + i;
        if (!blocks::worldGenReplaceable(region.getBlock(moving))) break;
        moving.y += dY;
    }
    finalLength = std::min(length, finalLength);
    if (finalLength < lengthMinimum || finalLength > lengthMaximum) return false;
    return makeSpike(region, start, config, finalLength, dY, random, hang);
}

bool startSpike(Region& region, BlockPos const& start, Stalactite const& config, RandomSource& random, bool hanging, int forcedMaxHeight) {
    int maxInclusive = config.maxLength;
    int minInclusive = static_cast<int>(static_cast<float>(maxInclusive) * config.sizeVariation);
    int length       = pure::randomBetweenInclusive(random, minInclusive, maxInclusive);
    return startSpikeRaw(region, start, config, length, minInclusive, std::min(maxInclusive, forcedMaxHeight), 4, hanging, random);
}

Variety const* varietyFor(std::string const& configId) {
    auto c = gConfigs.find(configId);
    if (c == gConfigs.end()) return nullptr;
    auto v = gVarieties.find(c->second.type);
    return v == gVarieties.end() ? nullptr : &v->second;
}

// RectangleLatticeIterator
struct RectLattice {
    int   yLevel, startX, startZ, countX, countZ;
    float xSpacing, zSpacing, xOffset, zOffset;
    int   lx = 0, lz = 0;

    static int nearestIndex(float spacing, int i) {
        float fi = static_cast<float>(i);
        return pure::mthFloorF((fi - pure::positiveModulo(fi, spacing)) / spacing);
    }
    RectLattice(BlockBox const& b, int y, float xs, float zs, float xo, float zo) : yLevel(y), xSpacing(xs), zSpacing(zs), xOffset(xo), zOffset(zo) {
        startX = nearestIndex(xSpacing, static_cast<int>(static_cast<float>(b.minX) - xOffset));
        startZ = nearestIndex(zSpacing, static_cast<int>(static_cast<float>(b.minZ) - zOffset));
        countX = nearestIndex(xSpacing, static_cast<int>(static_cast<float>(b.maxX + 1) - xOffset)) - startX;
        countZ = nearestIndex(zSpacing, static_cast<int>(static_cast<float>(b.maxZ + 1) - zOffset)) - startZ;
    }
    [[nodiscard]] bool hasNext() const { return lx < countX; }
    BlockPos           next() {
        BlockPos p{static_cast<int>(xOffset + static_cast<float>(startX + lx) * xSpacing), yLevel,
                   static_cast<int>(zOffset + static_cast<float>(startZ + lz) * zSpacing)};
        if (lz + 1 < countZ) lz++;
        else {
            lz = 0;
            lx++;
        }
        return p;
    }
};

} // namespace

void load(std::map<std::string, Json> const& stalactiteDocs, std::map<std::string, Json> const& settings) {
    // entries/* are stalactites; the other documents are SpeleothemVarietyConfigs by type
    std::map<std::string, Stalactite> entries;
    for (auto const& [id, doc] : stalactiteDocs)
        if (doc.contains("ores")) entries[id] = parseStalactite(doc);
    std::map<std::string, Variety> varieties;
    for (auto const& [id, doc] : stalactiteDocs) {
        if (doc.contains("ores")) continue;
        Variety v;
        v.type             = doc.value("type", std::string{});
        v.oreChance        = doc.value("ore_chance", 0.0F);
        v.stalactiteChance = doc.value("stalactite_chance", 0.0F);
        v.stalagmiteChance = doc.value("stalagmite_chance", 0.0F);
        auto fill          = [&](char const* key, std::vector<Stalactite>& into) {
            for (auto const& ref : doc.value(key, Json::array())) {
                auto it = entries.find(ref.get<std::string>());
                if (it != entries.end()) into.push_back(it->second);
                else warnOnce("stalactite:" + ref.get<std::string>(), "structure: missing stalactite entry " + ref.get<std::string>());
            }
        };
        fill("base_stalactites", v.base);
        fill("ore_stalactites", v.ore);
        fill("stalagmites", v.stalagmites);
        if (varieties.contains(v.type) && !doc.value("replace", false)) continue;
        v.stalactiteList = compileStalactites(v);
        v.stalagmiteList = simple(v.stalagmites);
        varieties[v.type] = std::move(v);
    }
    std::map<std::string, Config> configs;
    for (auto const& [id, doc] : settings) {
        Config c;
        c.type           = doc.value("type", std::string{});
        auto const& lat  = doc.value("lattice", Json::object());
        c.lattice.spacing  = lat.value("spacing", 3.5F);
        float s            = c.lattice.spacing;
        c.lattice.xOffset  = lat.value("x_offset", pure::mthCos(pure::kMthPi / 6.0F) * s);
        c.lattice.zOffset  = lat.value("z_offset", pure::mthSin(pure::kMthPi / 6.0F) * s);
        c.lattice.xSpacing = lat.value("x_spacing", c.lattice.xOffset * 2.0F);
        c.lattice.zSpacing = lat.value("z_spacing", s);
        configs[id]        = c;
    }
    std::unique_lock lock(gMutex);
    gVarieties = std::move(varieties);
    gConfigs   = std::move(configs);
}

std::vector<std::string> ids() {
    std::shared_lock         lock(gMutex);
    std::vector<std::string> out;
    for (auto const& [id, _] : gConfigs) out.push_back(id);
    return out;
}

Lattice lattice(std::string const& configId) {
    std::shared_lock lock(gMutex);
    auto             it = gConfigs.find(configId);
    return it == gConfigs.end() ? Lattice{} : it->second.lattice;
}

std::vector<BlockPos> latticePositions(Lattice const& l, BlockBox const& bounds, int y) {
    // TriangularLatticeConfig#boundedGrid: ZippedIterator of two rectangle lattices
    RectLattice           first(bounds, y, l.xSpacing, l.zSpacing, 0, 0);
    RectLattice           second(bounds, y, l.xSpacing, l.zSpacing, l.xOffset, l.zOffset);
    std::vector<BlockPos> out;
    bool                  firstNext = first.hasNext();
    while (first.hasNext() || second.hasNext()) {
        RectLattice& current = firstNext ? first : second;
        RectLattice& other   = firstNext ? second : first;
        if (other.hasNext()) firstNext = !firstNext;
        out.push_back(current.next());
    }
    return out;
}

bool shouldDoAStalactite(std::string const& configId, RandomSource& random) {
    std::shared_lock lock(gMutex);
    auto const*      v = varietyFor(configId);
    return random.nextFloat() < (v ? v->stalactiteChance : 0.0F);
}

bool shouldDoAStalagmite(std::string const& configId, RandomSource& random) {
    std::shared_lock lock(gMutex);
    auto const*      v = varietyFor(configId);
    return random.nextFloat() < (v ? v->stalagmiteChance : 0.0F);
}

bool spike(Region& region, BlockPos const& pos, std::string const& configId, RandomSource& random, bool hanging, int forcedMaxHeight) {
    Stalactite chosen;
    {
        std::shared_lock lock(gMutex);
        auto const*      v = varietyFor(configId);
        if (!v) {
            warnOnce("speleothem:" + configId, "structure: unknown speleothem config " + configId);
            chosen = stoneStalactite();
        } else {
            chosen = pick(hanging ? v->stalactiteList : v->stalagmiteList, random);
        }
    }
    return startSpike(region, pos, chosen, random, hanging, forcedMaxHeight);
}

} // namespace gf::structure::speleothems

namespace gf::structure::speleothems {

bool place(Region& region, BlockPos const& pos, std::string const& configId, RandomSource& random, bool hanging) {
    return spike(region, pos, configId, random, hanging, std::numeric_limits<int>::max());
}

} // namespace gf::structure::speleothems
