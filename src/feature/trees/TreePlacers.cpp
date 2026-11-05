// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/feature/trees/treeplacers/BranchesConfig.java
//   src/main/java/twilightforest/world/components/feature/trees/treeplacers/BranchingTrunkPlacer.java
//   src/main/java/twilightforest/world/components/feature/trees/treeplacers/DangleFromTreeDecorator.java
//   src/main/java/twilightforest/world/components/feature/trees/treeplacers/LeafSpheroidFoliagePlacer.java
//   src/main/java/twilightforest/world/components/feature/trees/treeplacers/TreeCorePlacer.java
//   src/main/java/twilightforest/world/components/feature/trees/treeplacers/TreeRootsDecorator.java
//   src/main/java/twilightforest/world/components/feature/trees/treeplacers/TrunkRiser.java
//   src/main/java/twilightforest/world/components/feature/trees/treeplacers/TrunkSideDecorator.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U3 trees: TF's trunk placers, foliage placer and tree decorators (TFFeatureModifiers), registered
// by id with the features contract (F5) so the minecraft:tree feature of TF's tree JSON uses them.
#include "feature/trees/Trees.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_set>

#include "mc/world/level/block/Block.h"

#include "core/jrand/JavaOrder.h"
#include "core/jrand/Mth.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/trees/TreeUtil.h"
#include "mod/Log.h"

#pragma clang fp contract(off)

namespace gf::feature::trees {

namespace kit = gf::features::kit;
using gf::features::FoliageAttachment;
using gf::features::TreeContext;

namespace {

Json const& member(Json const& j, char const* key) {
    static Json const null;
    if (!j.is_object()) return null;
    auto it = j.find(key);
    return it == j.end() ? null : *it;
}
template <class T>
T num(Json const& j, char const* key, T fallback) {
    auto const& v = member(j, key);
    return v.is_number() ? v.get<T>() : fallback;
}

/// A hash index next to a tree's position list (TreeFeature's sets), so adding stays O(1): rebuilt
/// when the list is another one or something else (the features kit) appended to it.
struct PosIndex {
    std::vector<BlockPos> const*     list = nullptr;
    std::unordered_set<std::int64_t> keys;
};
thread_local PosIndex tLogIndex, tLeafIndex;

std::int64_t posKey(BlockPos const& p) { return core::mth::blockPosAsLong(p.x, p.y, p.z); }

void addOnce(PosIndex& index, std::vector<BlockPos>& list, BlockPos const& p) {
    if (index.list != &list || index.keys.size() != list.size() || (!list.empty() && !index.keys.contains(posKey(list.back())))) {
        index.list = &list;
        index.keys.clear();
        for (auto const& q : list) index.keys.insert(posKey(q));
    }
    if (index.keys.insert(posKey(p)).second) list.push_back(p);
}

/// TFTreeFeature#place's decoration set (set3) while runDecorator runs for it.
thread_local std::vector<BlockPos>* tDecorations = nullptr;

/// TreeDecorator.Context#setBlock: the decoration setter (the features kit's, which records it for
/// the minecraft:tree feature; TF's own tree features collect it in tDecorations).
void decorate(TreeContext& tree, BlockPos const& pos, Block const& block) {
    kit::setDecoration(tree, pos, block);
    if (tDecorations) tDecorations->push_back(pos);
}

} // namespace

// ---- tree setters (TreeFeature's trunk / foliage / decoration consumers) ---------------------------

void setLog(TreeContext& tree, BlockPos const& pos, Block const& block) {
    tree.region.setBlock(pos, block, 19);
    addOnce(tLogIndex, tree.logs, pos);
}

void setLeaf(TreeContext& tree, BlockPos const& pos, Block const& block) {
    tree.region.setBlock(pos, block, 19);
    addOnce(tLeafIndex, tree.leaves, pos);
}

namespace {

// ---- BranchingTrunkPlacer ------------------------------------------------------------------------

struct Branching {
    TreeContext& tree;
    Json const&  config; ///< the trunk placer's JSON

    Json const& branches() const { return member(config, "branch_config"); }

    /// placeWood: the branch provider where TreeFeature.validTreePos allows it (the trunk setter).
    bool placeWood(BlockPos const& pos) {
        if (!kit::validTreePos(tree.region, pos)) return false;
        if (auto const* b = provide(member(branches(), "branch_provider"), tree.random, pos)) setLog(tree, pos, *b);
        return true;
    }

    void drawBranch(BlockPos const& from, BlockPos const& to) {
        Bresenham line(from, to);
        BlockPos  p;
        while (line.next(p)) placeWood(p);
    }

    void buildBranch(BlockPos const& pos, std::vector<FoliageAttachment>& leaves, int height, double length, double angle, double tilt, bool perpendicular) {
        BlockPos src  = above(pos, height);
        BlockPos dest = translate(src, length, angle, tilt);
        if (perpendicular) {
            drawBranch(src, BlockPos{dest.x, src.y, dest.z});
            int max = std::max(src.y, dest.y);
            for (int i = std::min(src.y, dest.y); i < max + 1; ++i) placeWood(BlockPos{dest.x, i, dest.z});
        } else {
            drawBranch(src, dest);
        }
        placeWood(rel(dest, Dir::East));
        placeWood(rel(dest, Dir::West));
        placeWood(rel(dest, Dir::South));
        placeWood(rel(dest, Dir::North));
        leaves.push_back({dest, 0, false});
    }

    std::vector<FoliageAttachment> place(int height, BlockPos const& start) {
        std::vector<FoliageAttachment> out;
        if (config.value("prevent_exposed_root", false)) {
            for (Dir d : kHorizontal) {
                if (kit::isReplaceable(tree.region.getBlock(rel(below(start), d)))) {
                    if (auto const* b = provide(member(tree.config, "trunk_provider"), tree.random, below(start))) setLog(tree, below(start), *b);
                    break;
                }
            }
        }
        // keep building upwards until blocked, then the tree is that tall
        for (int y = 0; y <= height; ++y) {
            if (!kit::placeLog(tree, above(start, y))) {
                height = y;
                break;
            }
        }
        out.push_back({above(start, height), 0, false});
        auto const& bc          = branches();
        int         count       = num(bc, "count_minimum", 0);
        int         randomAdd   = num(bc, "random_add_count", 0);
        double      length      = num(bc, "length", 1.0);
        double      spacingYaw  = num(bc, "spacing_yaw", 0.3);
        double      pitch       = num(bc, "downwards_pitch", 0.2);
        int         offsetDown  = num(config, "branch_start_offset_down", 0);
        bool        perpendicular = config.value("perpendicular_branches", false);
        int         numBranches = count + tree.random.nextInt(randomAdd + 1);
        float       offset      = tree.random.nextFloat();
        for (int b = 0; b < numBranches; ++b)
            buildBranch(start, out, height - offsetDown + b, length, spacingYaw * b + static_cast<double>(offset), pitch, perpendicular);
        return out;
    }
};

std::vector<FoliageAttachment> branchingTrunk(TreeContext& tree, int height, BlockPos const& start, Json const& config) {
    return Branching{tree, config}.place(height, start);
}

// ---- TrunkRiser ---------------------------------------------------------------------------------------

std::vector<FoliageAttachment> trunkRiser(TreeContext& tree, int height, BlockPos const& start, Json const& config) {
    BlockPos    raised = above(start, num(config, "offset_up", 0));
    auto const& inner  = member(config, "trunk_placer");
    std::string type   = inner.value("type", std::string());
    if (type.find(':') == std::string::npos) type = "minecraft:" + type;
    if (type == "twilightforest:branching_trunk_placer") return branchingTrunk(tree, height, raised, inner);
    if (type == "minecraft:straight_trunk_placer") {
        kit::setDirtAt(tree, below(raised));
        for (int i = 0; i < height; ++i) kit::placeLog(tree, above(raised, i));
        return {{above(raised, height), 0, false}};
    }
    static bool warned = false;
    if (!warned) {
        warned = true;
        gf::log().warn("trees: trunk_mover_upper wraps an unsupported trunk placer {}", type);
    }
    return {};
}

// ---- LeafSpheroidFoliagePlacer --------------------------------------------------------------------------

void spheroidFoliage(TreeContext& tree, FoliageAttachment const& at, int, int, Json const& config) {
    int      offset   = kit::sampleInt(config.value("offset", Json(0)), tree.random); // FoliagePlacer#createFoliage: offset(random)
    BlockPos center   = above(at.pos, offset);
    float    hRadius  = num(config, "horizontal_radius", 0.0f);
    float    vRadius  = num(config, "vertical_radius", 0.0f);
    float    bias     = num(config, "vertical_filler_bias", 0.0f);
    int      randomH  = num(config, "random_add_horizontal", 0);
    int      randomV  = num(config, "random_add_vertical", 0);
    int      shag     = num(config, "shag_factor", 0);
    auto const& provider = member(tree.config, "foliage_provider");
    Placer   leaf     = [&tree](BlockPos const& p, Block const& b) { setLeaf(tree, p, b); };

    int   addH = tree.random.nextInt(randomH + 1);
    float xz   = static_cast<float>(at.radiusOffset) + hRadius + static_cast<float>(addH);
    int   addV = tree.random.nextInt(randomV + 1);
    float y    = static_cast<float>(at.radiusOffset) + vRadius + static_cast<float>(addV);
    placeSpheroidBiased(tree.region, leaf, tree.random, center, xz, y, bias, provider);

    constexpr float kTwoPi = static_cast<float>(std::numbers::pi * 2.0);
    for (int i = 0; i < shag; ++i) {
        float randomYaw   = tree.random.nextFloat() * kTwoPi;
        float randomPitch = tree.random.nextFloat() * 2.0F - 1.0F;
        float yUnit       = static_cast<float>(std::sqrt(static_cast<double>(1.0F - randomPitch * randomPitch)));
        float xCircle     = yUnit * core::jrand::mthCos(randomYaw) * (hRadius - 1.0F); // radius minus 1: overlapping the foliage
        float zCircle     = yUnit * core::jrand::mthSin(randomYaw) * (hRadius - 1.0F);
        int   dx          = static_cast<int>(xCircle + static_cast<float>(static_cast<int>(xCircle) >> 31));
        int   dy          = static_cast<int>(randomPitch * (vRadius + 0.25F) + bias);
        int   dz          = static_cast<int>(zCircle + static_cast<float>(static_cast<int>(zCircle) >> 31));
        BlockPos p        = trees::at(center, dx, dy, dz);
        // placeLeafCluster
        for (BlockPos q : {p, rel(p, Dir::East), rel(p, Dir::South), trees::at(p, 1, 0, 1)})
            placeProvided(tree.region, leaf, treeFeatureValidPos, q, provider, tree.random);
    }
}

// ---- TreeCorePlacer -------------------------------------------------------------------------------------

void corePlacer(TreeContext& tree, Json const& config) {
    if (tree.logs.empty()) return;
    BlockPos pos = above(tree.logs.front(), num(config, "core_position", 0));
    if (auto const* b = provide(member(config, "deco_provider"), tree.random, pos)) decorate(tree, pos, *b);
}

// ---- TrunkSideDecorator ---------------------------------------------------------------------------------

void trunkSide(TreeContext& tree, Json const& config) {
    int size = static_cast<int>(tree.logs.size());
    if (size == 0) {
        gf::log().error("[TrunkSideDecorator] Trunk Blocks were empty! Why?");
        return;
    }
    int   count       = num(config, "placement_count", 0);
    float probability = num(config, "probability_of_placement", 0.0f);
    auto const& deco  = member(config, "deco_provider");
    for (int attempt = 0; attempt < count; ++attempt) {
        if (tree.random.nextFloat() >= probability) continue;
        BlockPos logPos    = tree.logs[static_cast<std::size_t>(tree.random.nextInt(size))];
        Dir      direction = randomHorizontal(tree.random);
        BlockPos newPos    = rel(logPos, direction);
        if (!tree.region.getBlock(newPos).isAir()) continue;
        // Java draws the state twice in both branches: getState(..).hasProperty(FACING), then
        // getState(..) again for the block it sets (with FACING when the first had it)
        Block const* probe = provide(deco, tree.random, newPos);
        Block const* state = provide(deco, tree.random, newPos);
        if (!probe || !state) continue;
        bool facing = &kit::withProperty(*probe, "facing", "up") != probe || &kit::withProperty(*probe, "facing", "down") != probe;
        decorate(tree, newPos, facing ? kit::withProperty(*state, "facing", dirName(direction)) : *state);
    }
}

// ---- TreeRootsDecorator --------------------------------------------------------------------------------

void treeRoots(TreeContext& tree, Json const& config) {
    if (tree.logs.empty()) return;
    int         strands     = num(config, "base_strand_count", 0);
    int         extra       = num(config, "additional_random_strands", 0);
    int         length      = num(config, "root_length", 0);
    int         yOffset     = num(config, "y_offset", 0);
    int         penetrate   = num(config, "rootPenetrability", 1);
    auto const& exposed     = member(config, "exposed_roots_provider");
    auto const& ground      = member(config, "ground_roots_provider");
    int         numBranches = strands + tree.random.nextInt(extra + 1);
    float       offset      = tree.random.nextFloat();
    BlockPos    startPos    = above(tree.logs.front(), yOffset);
    RootPlacer  placer{[&tree](BlockPos const& p, Block const& b) { decorate(tree, p, b); }, penetrate};
    for (int i = 0; i < numBranches; ++i) {
        BlockPos  dest = translate(below(startPos, i + 2), length, 0.3 * i + static_cast<double>(offset), 0.8);
        Bresenham line(below(startPos), dest);
        if (exposed.is_object()) traceExposedRoot(tree.region, placer, tree.random, exposed, ground, line);
        else traceRoot(tree.region, placer, tree.random, ground, line);
    }
}

// ---- DangleFromTreeDecorator ---------------------------------------------------------------------------

/// WeightedStateProvider (its codec: {"entries": [{data, weight}]}, or a typed provider).
Block const* weighted(Json const& provider, RandomSource& random, BlockPos const& pos) {
    if (provider.is_object() && provider.contains("entries") && !provider.contains("type")) {
        if (auto const* data = kit::weightedPick(provider["entries"], random)) return kit::javaState(*data);
        return nullptr;
    }
    return provide(provider, random, pos);
}

void dangle(TreeContext& tree, Json const& config) {
    if (tree.leaves.empty() || tree.logs.empty()) return;
    int count       = num(config, "attempts_minimum", 0);
    int randomAdd   = num(config, "random_add_attempts", 0);
    int minLength   = num(config, "minimum_required_length", 1);
    int baseLength  = num(config, "base_length", 1);
    int randomLen   = num(config, "random_add_length", 0);
    int totalTries  = count + tree.random.nextInt(randomAdd + 1);
    int leafTotal   = static_cast<int>(tree.leaves.size());
    totalTries      = std::min(totalTries, leafTotal);
    auto isAir      = [&](BlockPos const& p) { return tree.region.getBlock(p).isAir(); };
    for (int attempt = 0; attempt < totalTries; ++attempt) {
        bool     cleared = false;
        BlockPos pos     = tree.leaves[static_cast<std::size_t>(tree.random.nextInt(leafTotal))];
        // no rope where the trunk is (TF compares x with the first log's y)
        if (pos.x == tree.logs[0].y && pos.z == tree.logs[0].z) return;
        int cordLength = baseLength + tree.random.nextInt(randomLen + 1);
        for (int r = 1; r <= cordLength; ++r) {
            bool air = isAir(below(pos, r));
            if (!cleared && air) cleared = true;
            if (cleared && !air) {
                cordLength = r - 1;
                break;
            }
        }
        if (cordLength > minLength) { // no pathetic unroped baggage
            Block const* rope = weighted(member(config, "rope_provider"), tree.random, pos);
            for (int r = 1; r < cordLength; ++r) {
                pos = below(pos);
                if (rope && isAir(pos)) decorate(tree, pos, *rope);
            }
            pos = below(pos);
            if (isAir(pos))
                if (auto const* bag = weighted(member(config, "baggage_provider"), tree.random, pos)) decorate(tree, pos, *bag);
        }
    }
}

} // namespace

void registerTreePlacers() {
    gf::features::registerTrunkPlacer("twilightforest:branching_trunk_placer", branchingTrunk);
    gf::features::registerTrunkPlacer("twilightforest:trunk_mover_upper", trunkRiser);
    gf::features::registerFoliagePlacer("twilightforest:spheroid_foliage_placer", spheroidFoliage);
    gf::features::registerTreeDecorator("twilightforest:core_placer", corePlacer);
    gf::features::registerTreeDecorator("twilightforest:trunkside_decorator", trunkSide);
    gf::features::registerTreeDecorator("twilightforest:tree_roots", treeRoots);
    gf::features::registerTreeDecorator("twilightforest:dangle_from_tree_decorator", dangle);
}

void runDecorator(TreeContext& tree, Json const& decorator, std::vector<BlockPos>* decorations) {
    struct Sink {
        std::vector<BlockPos>* saved = tDecorations;
        ~Sink() { tDecorations = saved; }
    } sink;
    tDecorations     = decorations;
    std::string type = decorator.value("type", std::string());
    if (type == "twilightforest:core_placer") corePlacer(tree, decorator);
    else if (type == "twilightforest:trunkside_decorator") trunkSide(tree, decorator);
    else if (type == "twilightforest:tree_roots") treeRoots(tree, decorator);
    else if (type == "twilightforest:dangle_from_tree_decorator") dangle(tree, decorator);
    else {
        static bool warned = false;
        if (!warned) {
            warned = true;
            gf::log().warn("trees: TF tree features run only TF's decorators ({} skipped)", type);
        }
    }
}

} // namespace gf::feature::trees
