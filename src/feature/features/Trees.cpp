// The tree feature of the interpreter (F5): Minecraft 1.21.1's TreeFeature with the vanilla trunk
// placers (straight, fancy, giant), foliage placers (blob, bush, fancy, spruce, pine, mega_pine,
// acacia), feature sizes (two / three layers) and decorators (trunk_vine, leave_vine, cocoa) that
// The Twilight Forest's tree JSON uses; units add TF's own placers and decorators by id. Behaviour
// compatible implementations, drawing from the random source in Java's order.
#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"

#include "core/jrand/JavaOrder.h"
#include "core/jrand/Mth.h"
#include "feature/features/Internal.h"

#pragma clang fp contract(off)

namespace gf::features {

using detail::field;
using detail::fieldArr;
using detail::fieldObj;

using detail::normalizeId;
using detail::packPos;
using detail::TreeState;
using detail::warnOnce;
using Json = nlohmann::json;
namespace mth = core::mth;

namespace {

BlockPos at(BlockPos const& p, int dx, int dy, int dz) { return {p.x + dx, p.y + dy, p.z + dz}; }

/// The tree the tree feature is placing on this thread (TreeContext is not polymorphic).
thread_local TreeState* tCurrentTree = nullptr;

TreeState* stateOf(TreeContext& t) {
    return tCurrentTree && static_cast<TreeContext*>(tCurrentTree) == &t ? tCurrentTree : nullptr;
}

struct CurrentTree {
    TreeState* previous;
    explicit CurrentTree(TreeState& t) : previous(tCurrentTree) { tCurrentTree = &t; }
    ~CurrentTree() { tCurrentTree = previous; }
};

void addUnique(std::vector<BlockPos>& list, std::unordered_set<std::int64_t>* set, BlockPos const& p) {
    if (set) {
        if (set->insert(packPos(p)).second) list.push_back(p);
    } else if (std::find(list.begin(), list.end(), p) == list.end()) {
        list.push_back(p);
    }
}

bool isVine(Block const& b) { return b.getTypeName() == "minecraft:vine"; }

} // namespace

// ---- kit: tree helpers ----------------------------------------------------------------------------

namespace kit {

bool validTreePos(Region& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    return b.isAir() || hasTag(b, "minecraft:replaceable_by_trees");
}

bool isFree(Region& region, BlockPos const& pos) {
    if (validTreePos(region, pos)) return true;
    return hasTag(region.getBlock(pos), "minecraft:logs");
}

bool placeLog(TreeContext& tree, BlockPos const& pos, char axis) {
    if (!validTreePos(tree.region, pos)) return false;
    Block const* b = sampleState(field(tree.config, "trunk_provider"), tree.random, pos);
    if (!b) return false;
    if (axis) b = &withProperty(*b, "axis", std::string(1, axis));
    tree.region.setBlock(pos, *b, 19);
    auto* s = stateOf(tree);
    addUnique(tree.logs, s ? &s->logSet : nullptr, pos);
    return true;
}

void setDirtAt(TreeContext& tree, BlockPos const& pos) {
    auto const& b       = tree.region.getBlock(pos);
    bool        isDirt  = hasTag(b, "minecraft:dirt") && javaId(b) != "minecraft:grass_block" && javaId(b) != "minecraft:mycelium";
    if (tree.config.value("force_dirt", false) || !isDirt) {
        if (auto const* d = sampleState(field(tree.config, "dirt_provider"), tree.random, pos)) {
            tree.region.setBlock(pos, *d, 19);
            auto* s = stateOf(tree);
            addUnique(tree.logs, s ? &s->logSet : nullptr, pos); // Java sets the dirt through the trunk setter
        }
    }
}

bool tryPlaceLeaf(TreeContext& tree, BlockPos const& pos) {
    auto const& current = tree.region.getBlock(pos);
    if (detail::javaProperty(current, "persistent").value_or("false") == "true") return false;
    if (!validTreePos(tree.region, pos)) return false;
    Block const* b = sampleState(field(tree.config, "foliage_provider"), tree.random, pos);
    if (!b) return false;
    tree.region.setBlock(pos, *b, 19);
    auto* s = stateOf(tree);
    addUnique(tree.leaves, s ? &s->leafSet : nullptr, pos);
    return true;
}

void setDecoration(TreeContext& tree, BlockPos const& pos, Block const& block) {
    tree.region.setBlock(pos, block, 19);
    if (auto* s = stateOf(tree)) addUnique(s->decorations, &s->decorationSet, pos);
}

} // namespace kit

namespace detail {

namespace {

using kit::sampleInt;

// ---- trunk placers ----------------------------------------------------------------------------------

std::vector<FoliageAttachment> straightTrunk(TreeContext& t, int height, BlockPos const& pos, Json const&) {
    kit::setDirtAt(t, at(pos, 0, -1, 0));
    for (int i = 0; i < height; ++i) kit::placeLog(t, at(pos, 0, i, 0));
    return {{at(pos, 0, height, 0), 0, false}};
}

std::vector<FoliageAttachment> giantTrunk(TreeContext& t, int height, BlockPos const& pos, Json const&) {
    BlockPos below = at(pos, 0, -1, 0);
    kit::setDirtAt(t, below);
    kit::setDirtAt(t, at(below, 1, 0, 0));
    kit::setDirtAt(t, at(below, 0, 0, 1));
    kit::setDirtAt(t, at(below, 1, 0, 1));
    auto ifFree = [&](int dx, int dy, int dz) {
        BlockPos p = at(pos, dx, dy, dz);
        if (kit::isFree(t.region, p)) kit::placeLog(t, p);
    };
    for (int i = 0; i < height; ++i) {
        ifFree(0, i, 0);
        if (i < height - 1) {
            ifFree(1, i, 0);
            ifFree(1, i, 1);
            ifFree(0, i, 1);
        }
    }
    return {{at(pos, 0, height, 0), 0, true}};
}

struct FancyTrunk {
    TreeContext& t;

    static float treeShape(int height, int y) {
        if (static_cast<float>(y) < static_cast<float>(height) * 0.3F) return -1.0F;
        float f  = static_cast<float>(height) / 2.0F;
        float f1 = f - static_cast<float>(y);
        float f2 = static_cast<float>(std::sqrt(static_cast<double>(f * f - f1 * f1)));
        if (f1 == 0.0F) f2 = f;
        else if (std::fabs(f1) >= f) return 0.0F;
        return f2 * 0.5F;
    }

    static char logAxis(BlockPos const& a, BlockPos const& b) {
        int i = std::abs(b.x - a.x), j = std::abs(b.z - a.z), k = std::max(i, j);
        if (k > 0) return i == k ? 'x' : 'z';
        return 'y';
    }

    bool makeLimb(BlockPos const& base, BlockPos const& end, bool modify) {
        if (!modify && base == end) return true;
        BlockPos d{end.x - base.x, end.y - base.y, end.z - base.z};
        int      i  = std::max(std::abs(d.x), std::max(std::abs(d.y), std::abs(d.z)));
        float    f  = static_cast<float>(d.x) / static_cast<float>(i);
        float    f1 = static_cast<float>(d.y) / static_cast<float>(i);
        float    f2 = static_cast<float>(d.z) / static_cast<float>(i);
        for (int j = 0; j <= i; ++j) {
            BlockPos p = at(base, mth::floorf(0.5F + static_cast<float>(j) * f), mth::floorf(0.5F + static_cast<float>(j) * f1), mth::floorf(0.5F + static_cast<float>(j) * f2));
            if (modify) kit::placeLog(t, p, logAxis(base, p));
            else if (!kit::isFree(t.region, p)) return false;
        }
        return true;
    }

    static bool trimBranches(int maxHeight, int height) { return static_cast<double>(height) >= static_cast<double>(maxHeight) * 0.2; }

    std::vector<FoliageAttachment> place(int freeTreeHeight, BlockPos const& pos) {
        int j = freeTreeHeight + 2;
        int k = mth::floor(static_cast<double>(j) * 0.618);
        kit::setDirtAt(t, at(pos, 0, -1, 0));
        int l  = std::min(1, mth::floor(1.382 + std::pow(1.0 * static_cast<double>(j) / 13.0, 2.0)));
        int i1 = pos.y + k;
        int j1 = j - 5;
        struct Coords {
            BlockPos pos;
            int      branchBase;
        };
        std::vector<Coords> list{{at(pos, 0, j1, 0), i1}};
        for (; j1 >= 0; --j1) {
            float f = treeShape(j, j1);
            if (f < 0.0F) continue;
            for (int k1 = 0; k1 < l; ++k1) {
                double   d2  = 1.0 * static_cast<double>(f) * (static_cast<double>(t.random.nextFloat()) + 0.328);
                double   d3  = static_cast<double>(t.random.nextFloat() * 2.0F) * std::numbers::pi;
                double   d4  = d2 * std::sin(d3) + 0.5;
                double   d5  = d2 * std::cos(d3) + 0.5;
                BlockPos b   = at(pos, mth::floor(d4), j1 - 1, mth::floor(d5));
                BlockPos top = at(b, 0, 5, 0);
                if (makeLimb(b, top, false)) {
                    int    l1 = pos.x - b.x, i2 = pos.z - b.z;
                    double d6 = static_cast<double>(b.y) - std::sqrt(static_cast<double>(l1 * l1 + i2 * i2)) * 0.381;
                    int    j2 = d6 > static_cast<double>(i1) ? i1 : static_cast<int>(d6);
                    BlockPos b2{pos.x, j2, pos.z};
                    if (makeLimb(b2, b, false)) list.push_back({b, b2.y});
                }
            }
        }
        makeLimb(pos, at(pos, 0, k, 0), true);
        for (auto const& c : list) {
            BlockPos base{pos.x, c.branchBase, pos.z};
            if (!(base == c.pos) && trimBranches(j, c.branchBase - pos.y)) makeLimb(base, c.pos, true);
        }
        std::vector<FoliageAttachment> out;
        for (auto const& c : list)
            if (trimBranches(j, c.branchBase - pos.y)) out.push_back({c.pos, 0, false});
        return out;
    }
};

// ---- foliage placers ---------------------------------------------------------------------------------

using SkipFn = std::function<bool(RandomSource&, int x, int y, int z, int range, bool large)>;

void placeLeavesRow(TreeContext& t, BlockPos const& pos, int range, int localY, bool large, SkipFn const& skip) {
    int i = large ? 1 : 0;
    for (int j = -range; j <= range + i; ++j) {
        for (int k = -range; k <= range + i; ++k) {
            int ax, az;
            if (large) {
                ax = std::min(std::abs(j), std::abs(j - 1));
                az = std::min(std::abs(k), std::abs(k - 1));
            } else {
                ax = std::abs(j);
                az = std::abs(k);
            }
            if (!skip(t.random, ax, localY, az, range, large)) kit::tryPlaceLeaf(t, at(pos, j, localY, k));
        }
    }
}

bool blobSkip(RandomSource& r, int x, int y, int z, int range, bool) { return x == range && z == range && (r.nextInt(2) == 0 || y == 0); }

void blobFoliage(TreeContext& t, FoliageAttachment const& a, int foliageHeight, int radius, Json const& c) {
    int offset = sampleInt(c.value("offset", Json(0)), t.random);
    for (int i = offset; i >= offset - foliageHeight; --i) {
        int j = std::max(radius + a.radiusOffset - 1 - i / 2, 0);
        placeLeavesRow(t, a.pos, j, i, a.doubleTrunk, blobSkip);
    }
}

void bushFoliage(TreeContext& t, FoliageAttachment const& a, int foliageHeight, int radius, Json const& c) {
    int offset = sampleInt(c.value("offset", Json(0)), t.random);
    for (int i = offset; i >= offset - foliageHeight; --i) {
        int j = radius + a.radiusOffset - 1 - i;
        placeLeavesRow(t, a.pos, j, i, a.doubleTrunk, [](RandomSource& r, int x, int, int z, int range, bool) {
            return x == range && z == range && r.nextInt(2) == 0;
        });
    }
}

void fancyFoliage(TreeContext& t, FoliageAttachment const& a, int foliageHeight, int radius, Json const& c) {
    int offset = sampleInt(c.value("offset", Json(0)), t.random);
    for (int i = offset; i >= offset - foliageHeight; --i) {
        int j = radius + (i != offset && i != offset - foliageHeight ? 1 : 0);
        placeLeavesRow(t, a.pos, j, i, a.doubleTrunk, [](RandomSource&, int x, int, int z, int range, bool) {
            float fx = static_cast<float>(x) + 0.5F, fz = static_cast<float>(z) + 0.5F;
            return fx * fx + fz * fz > static_cast<float>(range * range);
        });
    }
}

bool cornerSkip(RandomSource&, int x, int, int z, int range, bool) { return x == range && z == range && range > 0; }

void spruceFoliage(TreeContext& t, FoliageAttachment const& a, int foliageHeight, int radius, Json const& c) {
    int offset = sampleInt(c.value("offset", Json(0)), t.random);
    int i      = t.random.nextInt(2);
    int j = 1, k = 0;
    for (int l = offset; l >= -foliageHeight; --l) {
        placeLeavesRow(t, a.pos, i, l, a.doubleTrunk, cornerSkip);
        if (i >= j) {
            i = k;
            k = 1;
            j = std::min(j + 1, radius + a.radiusOffset);
        } else {
            ++i;
        }
    }
}

void pineFoliage(TreeContext& t, FoliageAttachment const& a, int foliageHeight, int radius, Json const& c) {
    int offset = sampleInt(c.value("offset", Json(0)), t.random);
    int i      = 0;
    for (int j = offset; j >= offset - foliageHeight; --j) {
        placeLeavesRow(t, a.pos, i, j, a.doubleTrunk, cornerSkip);
        if (i >= 1 && j == offset - foliageHeight + 1) --i;
        else if (i < radius + a.radiusOffset) ++i;
    }
}

void megaPineFoliage(TreeContext& t, FoliageAttachment const& a, int foliageHeight, int radius, Json const& c) {
    int offset = sampleInt(c.value("offset", Json(0)), t.random);
    int i      = 0;
    for (int j = a.pos.y - foliageHeight + offset; j <= a.pos.y + offset; ++j) {
        int k  = a.pos.y - j;
        int l  = radius + a.radiusOffset + mth::floorf(static_cast<float>(k) / static_cast<float>(foliageHeight) * 3.5F);
        int i1 = (k > 0 && l == i && (j & 1) == 0) ? l + 1 : l;
        placeLeavesRow(t, BlockPos{a.pos.x, j, a.pos.z}, i1, 0, a.doubleTrunk, [](RandomSource&, int x, int, int z, int range, bool) {
            if (x + z >= 7) return true;
            return x * x + z * z > range * range;
        });
        i = l;
    }
}

void acaciaFoliage(TreeContext& t, FoliageAttachment const& a, int foliageHeight, int radius, Json const& c) {
    int      offset = sampleInt(c.value("offset", Json(0)), t.random);
    bool     flag   = a.doubleTrunk;
    BlockPos p      = at(a.pos, 0, offset, 0);
    auto     skip   = [](RandomSource&, int x, int y, int z, int range, bool) {
        return y == 0 ? (x > 1 || z > 1) && x != 0 && z != 0 : x == range && z == range && range > 0;
    };
    placeLeavesRow(t, p, radius + a.radiusOffset, -1 - foliageHeight, flag, skip);
    placeLeavesRow(t, p, radius - 1, -foliageHeight, flag, skip);
    placeLeavesRow(t, p, radius + a.radiusOffset - 1, 0, flag, skip);
}

/// FoliagePlacer#foliageHeight and #foliageRadius of the built-in placers.
int foliageHeight(std::string const& type, Json const& c, RandomSource& random, int height, Json const& tree) {
    (void)tree;
    if (type == "minecraft:spruce_foliage_placer") return std::max(4, height - sampleInt(c.value("trunk_height", Json(0)), random));
    if (type == "minecraft:pine_foliage_placer") return sampleInt(c.value("height", Json(0)), random);
    if (type == "minecraft:mega_pine_foliage_placer") return sampleInt(c.value("crown_height", Json(0)), random);
    if (type == "minecraft:acacia_foliage_placer") return 0;
    if (c.contains("height")) return sampleInt(c["height"], random); // blob, bush, fancy and most mod placers
    return 0;
}

int foliageRadius(std::string const& type, Json const& c, RandomSource& random, int trunkHeight) {
    int r = sampleInt(c.value("radius", Json(0)), random);
    if (type == "minecraft:pine_foliage_placer") r += random.nextInt(std::max(trunkHeight + 1, 1));
    return r;
}

/// FeatureSize#getSizeAtHeight.
int sizeAtHeight(Json const& s, int height, int midpoint) {
    auto type = normalizeId(s.value("type", std::string("minecraft:two_layers_feature_size")));
    if (type == "minecraft:three_layers_feature_size") {
        int limit = s.value("limit", 1), upperLimit = s.value("upper_limit", 1);
        if (midpoint < limit) return s.value("lower_size", 0);
        return midpoint >= height - upperLimit ? s.value("upper_size", 1) : s.value("middle_size", 1);
    }
    return midpoint < s.value("limit", 1) ? s.value("lower_size", 0) : s.value("upper_size", 1);
}

// ---- decorators ------------------------------------------------------------------------------------------

Block const* vineFacing(char const* side) { return kit::javaBlock("minecraft:vine", Json{{side, "true"}}); }

void trunkVine(TreeContext& t, Json const&) {
    struct Side {
        int         dx, dz;
        char const* prop;
    } sides[] = {{-1, 0, "east"}, {1, 0, "west"}, {0, -1, "south"}, {0, 1, "north"}};
    for (auto const& p : t.logs) {
        for (auto const& s : sides) {
            if (t.random.nextInt(3) > 0) {
                BlockPos b = at(p, s.dx, 0, s.dz);
                if (t.region.getBlock(b).isAir())
                    if (auto const* v = vineFacing(s.prop)) kit::setDecoration(t, b, *v);
            }
        }
    }
}

void leaveVine(TreeContext& t, Json const& c) {
    float probability = c.value("probability", 0.0F);
    struct Side {
        int         dx, dz;
        char const* prop;
    } sides[] = {{-1, 0, "east"}, {1, 0, "west"}, {0, -1, "south"}, {0, 1, "north"}};
    auto leaves = t.leaves; // decorations do not add leaves; iterate a copy anyway
    for (auto const& p : leaves) {
        for (auto const& s : sides) {
            if (t.random.nextFloat() < probability) {
                BlockPos b = at(p, s.dx, 0, s.dz);
                if (!t.region.getBlock(b).isAir()) continue;
                auto const* v = vineFacing(s.prop);
                if (!v) continue;
                kit::setDecoration(t, b, *v);
                int      i = 4;
                for (BlockPos d = at(b, 0, -1, 0); t.region.getBlock(d).isAir() && i > 0; --i) {
                    kit::setDecoration(t, d, *v);
                    d = at(d, 0, -1, 0);
                }
            }
        }
    }
}

void cocoa(TreeContext& t, Json const& c) {
    if (t.random.nextFloat() >= c.value("probability", 0.0F)) return;
    if (t.logs.empty()) return;
    int y0 = t.logs[0].y;
    // Direction.Plane.HORIZONTAL: north, east, south, west
    struct Dir {
        char const* name;
        int         dx, dz;
    } dirs[] = {{"north", 0, -1}, {"east", 1, 0}, {"south", 0, 1}, {"west", -1, 0}};
    for (auto const& p : t.logs) {
        if (p.y - y0 > 2) continue;
        for (auto const& d : dirs) {
            if (t.random.nextFloat() <= 0.25F) {
                BlockPos b = at(p, -d.dx, 0, -d.dz);
                if (t.region.getBlock(b).isAir()) {
                    int age = t.random.nextInt(3);
                    if (auto const* s = kit::javaBlock("minecraft:cocoa", Json{{"age", std::to_string(age)}, {"facing", d.name}}))
                        kit::setDecoration(t, b, *s);
                }
            }
        }
    }
}

/// The Java order of a position set handed to decorators: HashSet iteration, then by y (stable).
void javaOrder(std::vector<BlockPos>& v) {
    std::vector<int> hashes;
    hashes.reserve(v.size());
    for (auto const& p : v) hashes.push_back(core::jrand::blockPosHash(p.x, p.y, p.z));
    std::vector<BlockPos> out;
    out.reserve(v.size());
    for (auto i : core::jrand::hashSetOrder(hashes)) out.push_back(v[i]);
    std::stable_sort(out.begin(), out.end(), [](BlockPos const& a, BlockPos const& b) { return a.y < b.y; });
    v = std::move(out);
}

// ---- TreeFeature ---------------------------------------------------------------------------------------

int maxFreeTreeHeight(Region& region, int trunkHeight, BlockPos const& top, Json const& config, bool ignoreVines) {
    auto const& size = fieldObj(config, "minimum_size");
    for (int i = 0; i <= trunkHeight + 1; ++i) {
        int j = sizeAtHeight(size, trunkHeight, i);
        for (int k = -j; k <= j; ++k)
            for (int l = -j; l <= j; ++l) {
                BlockPos m = at(top, k, i, l);
                if (!kit::isFree(region, m) || (!ignoreVines && isVine(region.getBlock(m)))) return i - 2;
            }
    }
    return trunkHeight;
}

bool treeFeature(FeatureContext const& c) {
    Json const& config = c.config;
    auto const& tp     = fieldObj(config, "trunk_placer");
    auto const& fp     = fieldObj(config, "foliage_placer");
    auto        tpType = normalizeId(tp.value("type", std::string()));
    auto        fpType = normalizeId(fp.value("type", std::string()));

    TrunkPlacerFn trunk;
    FoliagePlacerFn foliage;
    RootPlacerFn rootPlacer;
    std::optional<Json> rootConfig;
    if (config.contains("root_placer")) rootConfig = config["root_placer"];
    withRegistries([&](Registries const& r) {
        if (auto it = r.trunkPlacers.find(tpType); it != r.trunkPlacers.end()) trunk = it->second;
        if (auto it = r.foliagePlacers.find(fpType); it != r.foliagePlacers.end()) foliage = it->second;
        if (rootConfig) {
            auto rt = normalizeId(rootConfig->value("type", std::string()));
            if (auto it = r.rootPlacers.find(rt); it != r.rootPlacers.end()) rootPlacer = it->second;
        }
        return 0;
    });
    if (!trunk) {
        warnOnce("trunk_placer " + tpType, "unknown trunk placer " + tpType);
        return false;
    }
    if (!foliage) {
        warnOnce("foliage_placer " + fpType, "unknown foliage placer " + fpType);
        return false;
    }
    if (rootConfig && !rootPlacer) {
        auto rt = normalizeId(rootConfig->value("type", std::string()));
        warnOnce("root_placer " + rt, "unknown root placer " + rt + " (the tree grows without roots)");
    }

    // TrunkPlacer#getTreeHeight, FoliagePlacer#foliageHeight / #foliageRadius
    int i = tp.value("base_height", 0) + c.random.nextInt(tp.value("height_rand_a", 0) + 1) + c.random.nextInt(tp.value("height_rand_b", 0) + 1);
    int j = foliageHeight(fpType, fp, c.random, i, config);
    int k = i - j;
    int l = foliageRadius(fpType, fp, c.random, k);

    TreeState   t{TreeContext{c.region, c.random, config, {}, {}, {}}};
    CurrentTree current(t);
    BlockPos  trunkOrigin = c.origin;
    if (rootConfig && rootPlacer && rootConfig->contains("trunk_offset_y")) {
        trunkOrigin = at(c.origin, 0, sampleInt((*rootConfig)["trunk_offset_y"], c.random), 0); // RootPlacer#getTrunkOrigin
    }
    int i1 = std::min(c.origin.y, trunkOrigin.y);
    int j1 = std::max(c.origin.y, trunkOrigin.y) + i + 1;
    if (i1 < c.region.minY() + 1 || j1 > c.region.maxY()) return false;
    auto const& size     = fieldObj(config, "minimum_size");
    bool        hasClip  = size.contains("min_clipped_height");
    int         clip     = hasClip ? size["min_clipped_height"].get<int>() : 0;
    int         k1       = maxFreeTreeHeight(c.region, i, trunkOrigin, config, config.value("ignore_vines", false));
    if (!(k1 >= i || (hasClip && k1 >= clip))) return false;
    if (rootConfig && rootPlacer && !rootPlacer(t, c.origin, trunkOrigin, *rootConfig)) return false;
    auto attachments = trunk(t, k1, trunkOrigin, tp);
    for (auto const& a : attachments) foliage(t, a, j, l, fp);
    if (t.logs.empty() && t.leaves.empty()) return false;

    auto const& decorators = fieldArr(config, "decorators");
    if (!decorators.empty()) {
        javaOrder(t.logs);
        javaOrder(t.leaves);
        javaOrder(t.roots);
        for (auto const& d : decorators) {
            auto            type = normalizeId(d.value("type", std::string()));
            TreeDecoratorFn fn   = withRegistries([&](Registries const& r) -> TreeDecoratorFn {
                auto it = r.decorators.find(type);
                return it == r.decorators.end() ? TreeDecoratorFn{} : it->second;
            });
            if (fn) fn(t, d);
            else warnOnce("tree_decorator " + type, "unknown tree decorator " + type);
        }
    }
    return true;
}

} // namespace

void registerVanillaTrees() {
    registerType("minecraft:tree", treeFeature);
    registerTrunkPlacer("minecraft:straight_trunk_placer", straightTrunk);
    registerTrunkPlacer("minecraft:giant_trunk_placer", giantTrunk);
    registerTrunkPlacer("minecraft:fancy_trunk_placer", [](TreeContext& t, int height, BlockPos const& start, Json const&) {
        return FancyTrunk{t}.place(height, start);
    });
    registerFoliagePlacer("minecraft:blob_foliage_placer", blobFoliage);
    registerFoliagePlacer("minecraft:bush_foliage_placer", bushFoliage);
    registerFoliagePlacer("minecraft:fancy_foliage_placer", fancyFoliage);
    registerFoliagePlacer("minecraft:spruce_foliage_placer", spruceFoliage);
    registerFoliagePlacer("minecraft:pine_foliage_placer", pineFoliage);
    registerFoliagePlacer("minecraft:mega_pine_foliage_placer", megaPineFoliage);
    registerFoliagePlacer("minecraft:acacia_foliage_placer", acaciaFoliage);
    registerTreeDecorator("minecraft:trunk_vine", trunkVine);
    registerTreeDecorator("minecraft:leave_vine", leaveVine);
    registerTreeDecorator("minecraft:cocoa", cocoa);
}

} // namespace detail

} // namespace gf::features
