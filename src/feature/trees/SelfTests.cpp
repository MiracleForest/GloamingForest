// U3 trees: selftests (trees.*, testPos dx 300..399, rule R12).
//
//   trees.features_a / trees.features_b   every owned tree / mushroom configured feature places
//   trees.golden_*                        golden volume hashes of seeded canopy, mega canopy, darkwood
//                                         and mangrove trees (rule R19 style review once)
//   trees.sapling_growers                 each tree sapling grows its tree from bone meal (the real click)
//   trees.mega_2x2                        four canopy saplings grow the mega canopy tree on bone meal
//   trees.mega_2x2_java                   seeded 2x2 canopy and twilight oak (forest / savannah mega oak) vs Java
//   trees.core_toggle_held_item           a core toggles with a block in hand, nothing is placed
//   trees.core_disabled_config            a core disabled by config stays off; the message names it
//   trees.time_core_ticks                 the tree of time ticks a crop; only randomly ticking states count
//   trees.transformation_core_biome       the heart of transformation turns a column into enchanted forest
//   trees.mining_core_magnet              the minewood core pulls an ore vein toward it
//   trees.sorting_core_moves_item         the sortingwood engine moves an item into the chest holding more
//   trees.snow_under_trees                snow under leaves
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <optional>
#include <map>
#include <memory>
#include <set>
#include <tuple>

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/world/Container.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/GameType.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkBlockPos.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/ChunkLocalHeight.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/biome/registry/BiomeRegistry.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/component/IVanillaMainBlockActorComponent.h"
#include "mc/world/level/block/actor/component/IVanillaTickBlockActorComponent.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/chunk/LevelChunkDataField.h"
#include "mc/world/level/chunk/SubChunk.h"

#include "core/jrand/JRand.h"
#include "feature/ext/trees/ChunkDirty.h"
#include "feature/ext/trees/HugeMushrooms.h"
#include "feature/ext/trees/PickBlock.h"
#include "feature/ext/trees/RandomTicks.h"
#include "feature/ext/trees/TimeCoreTickers.h"
#include "feature/enchant/Enchant.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/features/Features.h"
#include "feature/item/DataFiles.h"
#include "feature/loot/Loot.h"
#include "feature/spawn/Internal.h"
#include "feature/trees/Trees.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace gf::feature::trees {

namespace {

using gf::core::BlockBox;
using gf::core::GlobalPos;
using gf::selftest::Context;
namespace kit = gf::features::kit;

/// The showcase's undo log: the first original block of every position it (or a tree it places)
/// changed, so trees.zz_showcase_restore can put the world back (rule R12: nothing left behind).
struct ShowcaseLog {
    bool                                      active = false;
    std::vector<std::function<void()>>        undo; ///< what else to put back (biomes, overrides), run newest first
    std::map<std::tuple<int, int, int, int>, Block const*> originals;
    std::vector<std::tuple<int, int, int, int>>             order;
};
ShowcaseLog& showcaseLog() {
    static ShowcaseLog log;
    return log;
}
void recordOriginal(BlockSource& region, BlockPos const& pos) {
    auto& log = showcaseLog();
    if (!log.active) return;
    auto key = std::make_tuple(static_cast<int>(region.getDimensionId()), pos.x, pos.y, pos.z);
    if (log.originals.contains(key)) return;
    log.originals.emplace(key, &region.getBlock(pos));
    log.order.push_back(key);
}
/// game::setBlock with the original recorded.
void showcaseSet(BlockSource& region, BlockPos const& pos, Block const& block) {
    recordOriginal(region, pos);
    game::setBlock(region, pos, block);
}

/// A worldgen Region over the live world (Java's WorldGenRegion for a test placement).
class LiveRegion final : public gf::worldgen::Region {
public:
    LiveRegion(BlockSource& region, BlockPos const& at) : mRegion(region), mCx(at.x >> 4), mCz(at.z >> 4) {}
    int          centerChunkX() const override { return mCx; }
    int          centerChunkZ() const override { return mCz; }
    int          minY() const override { return mRegion.getMinHeight(); }
    int          maxY() const override { return mRegion.getMaxHeight(); }
    bool         isInside(int x, int z) const override { return mRegion.hasChunksAt(BlockPos{x, minY(), z}, 0, false); }
    Block const& getBlock(BlockPos const& pos) const override { return mRegion.getBlock(pos); }
    bool         setBlock(BlockPos const& pos, Block const& block, int) override {
        if (pos.y < minY() || pos.y >= maxY() || !mRegion.hasChunksAt(pos, 0, false)) return false;
        recordOriginal(mRegion, pos);
        return game::setBlock(mRegion, pos, block);
    }
    int height(gf::worldgen::Heightmap type, int x, int z) const override {
        bool leaves = type != gf::worldgen::Heightmap::MotionBlockingNoLeaves;
        for (int y = maxY() - 1; y >= minY(); --y) {
            Block const& b = mRegion.getBlock(BlockPos{x, y, z});
            if (b.isAir()) continue;
            if (type == gf::worldgen::Heightmap::MotionBlocking || type == gf::worldgen::Heightmap::MotionBlockingNoLeaves) {
                if (b.getTypeName() == "minecraft:snow_layer") continue;
            }
            if (!leaves && b.getTypeName().find("leaves") != std::string::npos) continue;
            return y + 1;
        }
        return minY();
    }
    std::string  biome(BlockPos const&) const override { return "minecraft:plains"; }
    std::int64_t seed() const override { return 0; }
    int          dimension() const override { return static_cast<int>(mRegion.getDimensionId()); }

private:
    BlockSource& mRegion;
    int          mCx, mCz;
};

BlockPos bp(GlobalPos const& p) { return game::blockPos(p); }

/// The vanilla huge mushrooms as Minecraft 1.21.1 configures them (huge_brown_mushroom /
/// huge_red_mushroom: the features data has no configured feature of these ids).
nlohmann::json vanillaMushroom(bool red) {
    auto face = [](bool up, bool down) {
        return nlohmann::json{{"down", down ? "true" : "false"}, {"east", "true"}, {"north", "true"}, {"south", "true"}, {"up", up ? "true" : "false"}, {"west", "true"}};
    };
    nlohmann::json cap  = nlohmann::json::object();
    cap["type"]         = "minecraft:simple_state_provider";
    cap["state"]        = {{"Name", red ? "minecraft:red_mushroom_block" : "minecraft:brown_mushroom_block"}, {"Properties", face(true, false)}};
    nlohmann::json stem = nlohmann::json::object();
    stem["type"]        = "minecraft:simple_state_provider";
    stem["state"]       = {{"Name", "minecraft:mushroom_stem"}, {"Properties", face(false, false)}};
    nlohmann::json config = nlohmann::json::object();
    config["cap_provider"]   = cap;
    config["stem_provider"]  = stem;
    config["foliage_radius"] = red ? 2 : 3;
    nlohmann::json out       = nlohmann::json::object();
    out["type"]              = red ? "minecraft:huge_red_mushroom" : "minecraft:huge_brown_mushroom";
    out["config"]            = config;
    return out;
}

bool placeSeeded(Context& ctx, std::string const& id, BlockPos const& pos, std::int64_t seed) {
    auto* region = ctx.region();
    if (!region) return false;
    LiveRegion                live(*region, pos);
    core::jrand::LegacyRandom random(seed);
    if (id == "minecraft:huge_brown_mushroom" || id == "minecraft:huge_red_mushroom") return gf::features::placeJson(vanillaMushroom(id == "minecraft:huge_red_mushroom"), live, pos, random);
    return gf::features::place(id, live, pos, random);
}

/// The tree of time checks 4 blocks under its base for #minecraft:features_cannot_replace (the flat
/// world's bedrock is there): a 3x4x3 dirt pedestal topped with grass lifts it; returns the new base.
BlockPos pedestal(BlockSource& region, BlockPos const& pos) {
    auto const* dirt  = game::blockWith("minecraft:dirt");
    auto const* grass = game::blockWith("minecraft:grass_block");
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = 0; dy < 4; ++dy)
            for (int dz = -1; dz <= 1; ++dz)
                if (auto const* b = dy == 3 ? grass : dirt) showcaseSet(region, BlockPos{pos.x + dx, pos.y + dy, pos.z + dz}, *b);
    return BlockPos{pos.x, pos.y + 4, pos.z};
}

int countBlocks(BlockSource& region, BlockBox const& box, std::string_view needle) {
    int n = 0;
    for (int x = box.minX; x <= box.maxX; ++x)
        for (int y = box.minY; y <= box.maxY; ++y)
            for (int z = box.minZ; z <= box.maxZ; ++z)
                if (region.getBlock(BlockPos{x, y, z}).getTypeName().find(needle) != std::string::npos) ++n;
    return n;
}

bool compareWithJava(Context& ctx, std::string const& name, BlockBox const& box, std::vector<std::string> const& cells = {});

/// Places every feature of `ids` on a 4 x 4 grid of 36-block cells in one flat area, then compares the
/// area with the Java oracle's `name` case.
ll::coro::CoroTask<> placeAll(Context& ctx, int dx, std::string name, std::vector<std::string> const& ids) {
    auto box = co_await ctx.flatArea(dx, 144, 72);
    for (std::size_t i = 0; i < ids.size(); ++i) {
        int      cx  = static_cast<int>(i % 4), cz = static_cast<int>(i / 4);
        BlockPos pos{box.minX + 18 + cx * 36, box.minY, box.minZ + 18 + cz * 36};
        co_await ctx.ensureLoaded(game::globalPos(ctx.dimension(), pos));
        if (ids[i].ends_with("time_tree") && ctx.region()) pos = pedestal(*ctx.region(), pos);
        bool ok = placeSeeded(ctx, ids[i], pos, 1234 + static_cast<std::int64_t>(i));
        ctx.check(ok, ids[i] + " placed");
        if (ok) ctx.info(ids[i] + " placed");
    }
    compareWithJava(ctx, name, box, ids);
}

std::string tree(char const* path) { return std::string("twilightforest:tree/") + path; }

} // namespace

GF_REGISTER_SELFTEST("trees.features_a", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await placeAll(ctx, 300, "features_a", {tree("twilight_oak_tree"), tree("large_twilight_oak_tree"), tree("swampy_oak_tree"), tree("canopy_tree"),
                                 tree("firefly_canopy_tree"), tree("dead_canopy_tree"), tree("mega_canopy_tree"), tree("forest_mega_oak_tree"),
                                 tree("savannah_mega_oak_tree"), tree("mangrove_tree"), tree("darkwood_tree"), tree("homegrown_darkwood_tree"),
                                 tree("darkwood_lantern_tree"), tree("dark_forest_oak_tree"), tree("dark_forest_birch_tree"), tree("dark_oak_bush")});
});

GF_REGISTER_SELFTEST("trees.features_b", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await placeAll(ctx, 310, "features_b", {tree("oak_bush"), tree("rainbow_oak"), tree("large_rainbow_oak"), tree("time_tree"), tree("transformation_tree"),
                                 tree("mining_tree"), tree("sorting_tree"), tree("snowy_spruce_tree"), tree("mega_spruce_tree"), tree("large_winter_tree"),
                                 tree("smaller_jungle_tree"), tree("vanilla_oak_tree"), tree("vanilla_birch_tree"), "twilightforest:mushroom/brown_canopy_mushroom",
                                 "twilightforest:mushroom/canopy_red_vanilla_mushroom", "twilightforest:mushroom/canopy_red_smooth_mushroom"});
});

GF_REGISTER_SELFTEST("trees.features_c", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await placeAll(ctx, 320, "features_c", {"twilightforest:mushroom/canopy_red_spheroid_mushroom", "twilightforest:mushroom/canopy_red_flat_mushroom", "minecraft:huge_brown_mushroom",
                                 "minecraft:huge_red_mushroom"});
});

namespace {

/// The Java oracle's trees (data/gf/java_goldens/trees.json, from TF 4.8 on NeoForge 1.21.1 placed at
/// the same absolute positions as these selftests: tests/trees/oracle): a case's box, block by block.
struct JavaCase {
    BlockBox                       box{};
    std::vector<Block const*>      palette;
    std::vector<std::string>       paletteText;
    std::vector<std::pair<int, int>> rle; ///< [count, palette index], x outermost, then y, then z
};
/// The features kit maps Java states without the leaves' distance (0: not computed yet, F11): the
/// Java distance put back, for the comparison.
Block const* withJavaDistance(Block const* b, nlohmann::json const& props) {
    if (!b || !props.contains("distance")) return b;
    return &leafWithDistance(*b, std::stoi(props["distance"].get<std::string>()));
}

std::optional<JavaCase> javaCase(std::string const& name) {
    // read for each comparison (a few per run): the data root may be a selftest's override or change
    // between worlds, so nothing is cached
    nlohmann::json const all = [] {
        auto            file = gf::feature::item::data::root() / "gf" / "java_goldens" / "trees.json";
        std::error_code ec;
        if (!std::filesystem::exists(file, ec)) return nlohmann::json();
        return gf::feature::item::data::read(file);
    }();
    if (!all.is_object() || !all.contains("cases")) return std::nullopt;
    for (auto const& c : all["cases"]) {
        if (c.value("name", std::string()) != name) continue;
        JavaCase out;
        auto const& b = c["box"];
        out.box       = BlockBox{b[0].get<int>(), b[1].get<int>(), b[2].get<int>(), b[3].get<int>(), b[4].get<int>(), b[5].get<int>()};
        for (auto const& p : c["palette"]) {
            auto props = p.value("Properties", nlohmann::json::object());
            std::string id = p.value("Name", std::string());
            Block const* mapped = nullptr;
            if (id == "minecraft:brown_mushroom_block" || id == "minecraft:red_mushroom_block" || id == "minecraft:mushroom_stem") {
                // the features kit maps every huge mushroom block to its all-faces permutation:
                // Java's faces -> huge_mushroom_bits as the mushroom features place them
                auto face = [&](char const* k) { return props.value(k, std::string("false")) == "true"; };
                gf::trees_ext::Faces faces{face("up"), face("down"), face("north"), face("east"), face("south"), face("west")};
                gf::trees_ext::MushroomState state{id, faces, true};
                mapped = gf::trees_ext::mushroomBlock(state, faces);
            }
            if (!mapped) mapped = withJavaDistance(kit::javaBlock(id, props), props);
            out.palette.push_back(mapped);
            out.paletteText.push_back(p.value("Name", std::string()) + (props.empty() ? "" : props.dump()));
        }
        for (auto const& r : c["rle"]) out.rle.emplace_back(r[0].get<int>(), r[1].get<int>());
        return out;
    }
    return std::nullopt;
}

/// The block a Bedrock world shows for a Java state and the one there match: the same permutation, or
/// the same type with every state of the mapped block equal (Bedrock's own update bit of its leaves
/// aside).
bool sameBlock(Block const& here, Block const* java) {
    if (!java) return false;
    if (&here == java) return true;
    if (here.getTypeName() != java->getTypeName()) return false;
    auto a = game::statesOf(here), b = game::statesOf(*java);
    a.erase("update_bit");
    b.erase("update_bit");
    // a distance not computed yet (0: F5 places leaves so, F11 computes it) stands for Java's 7
    if (auto ia = a.find("twilightforest:distance"), ib = b.find("twilightforest:distance"); ia != a.end() && ib != b.end() && ia->second == "0" && ib->second == "7") a.erase(ia), b.erase(ib);
    return a == b;
}

/// The differences from TF 4.8 known and explained (docs/units/trees.md, "Java oracle comparison"),
/// per feature (a placeAll cell's feature id, or the case name of a one-tree case): at most this
/// many blocks may differ; every other feature must match block for block.
struct Allowance {
    char const* feature;
    int         max;
    char const* why;
};
constexpr Allowance kAllowances[] = {
    // the oracle places in a live level, where HugeMushroomBlock#updateShape clears the faces between
    // cap blocks of the same kind; worldgen (WorldGenRegion) keeps them, as Bedrock's features do
    // (measured: the spheroid cap's 19 inner cap faces, the smooth cap's 8; the flat and vanilla caps
    // have no cap block next to another of a face it shows)
    {"twilightforest:mushroom/canopy_red_spheroid_mushroom", 19, "Java oracle artifact: HugeMushroomBlock#updateShape in a live level"},
    {"twilightforest:mushroom/canopy_red_smooth_mushroom", 8, "Java oracle artifact: HugeMushroomBlock#updateShape in a live level"},
};

/// Compares the box of `name` with the Java oracle's (when the oracle placed it at this very box:
/// the positions depend on the world's spawn); returns false when there is nothing to compare.
/// `cells`: the features of a placeAll grid (4 x n cells of 36 blocks), to tell the differences
/// apart per feature; empty: the whole box is the one feature `name`.
bool compareWithJava(Context& ctx, std::string const& name, BlockBox const& box, std::vector<std::string> const& cells) {
    auto jc = javaCase(name);
    if (!jc) {
        ctx.check(false, name + ": no Java oracle case (data/gf/java_goldens/trees.json)");
        return false;
    }
    if (jc->box.minX != box.minX || jc->box.minY != box.minY || jc->box.minZ != box.minZ || jc->box.maxX != box.maxX || jc->box.maxY != box.maxY || jc->box.maxZ != box.maxZ) {
        // the server e2e world's spawn fixes the boxes the oracle dumped; another world (a client
        // run, a different spawn) has nothing to compare with. The server scenario requires every
        // case to be compared.
        ctx.info("JAVA PARITY " + name + ": not compared (the Java oracle placed it at another box: this world's spawn differs)");
        return false;
    }
    auto* region = ctx.region();
    if (!region) return false;
    auto featureAt = [&](int x, int z) -> std::string {
        if (cells.empty()) return name;
        std::size_t i = static_cast<std::size_t>((z - box.minZ) / 36 * 4 + (x - box.minX) / 36);
        return i < cells.size() ? cells[i] : std::string("(no feature)");
    };
    int                        x = box.minX, y = box.minY, z = box.minZ, mismatches = 0, nonAir = 0;
    std::map<std::string, int> perFeature;
    std::vector<std::string>   first;
    std::map<std::string, int> shown;
    for (auto const& [count, index] : jc->rle) {
        for (int i = 0; i < count; ++i) {
            Block const& here = region->getBlock(BlockPos{x, y, z});
            Block const* java = jc->palette[static_cast<std::size_t>(index)];
            if (!here.isAir()) ++nonAir;
            if (!sameBlock(here, java)) {
                ++mismatches;
                auto feature = featureAt(x, z);
                ++perFeature[feature];
                if (shown[feature]++ < 20)
                    first.push_back(feature + " at " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z) + ": Java " + jc->paletteText[static_cast<std::size_t>(index)] +
                                    " (" + (java ? java->getTypeName() + nlohmann::json(game::statesOf(*java)).dump() : std::string("unmapped")) + "), here " + here.getTypeName() +
                                    nlohmann::json(game::statesOf(here)).dump());
            }
            if (++z > box.maxZ) {
                z = box.minZ;
                if (++y > box.maxY) {
                    y = box.minY;
                    ++x;
                }
            }
        }
    }
    for (auto const& f : first) ctx.info(name + " differs: " + f);
    ctx.info("JAVA PARITY " + name + ": " + (mismatches == 0 ? std::string("matches TF 4.8 on Java block for block") : "differs from TF 4.8 in " + std::to_string(mismatches) + " blocks") + " (" +
             std::to_string(box.volume()) + " compared, " + std::to_string(nonAir) + " blocks here)");
    for (auto const& [feature, n] : perFeature) {
        Allowance const* allowed = nullptr;
        for (auto const& a : kAllowances)
            if (feature == a.feature) allowed = &a;
        if (allowed && n <= allowed->max)
            ctx.info("PARITY GAP (known): " + feature + " differs from TF 4.8 in " + std::to_string(n) + " blocks (at most " + std::to_string(allowed->max) + "): " + allowed->why);
        else ctx.check(false, name + ": " + feature + " differs from TF 4.8 in " + std::to_string(n) + " blocks" + (allowed ? " (more than the known " + std::to_string(allowed->max) + ")" : ""));
    }
    return true;
}

/// Golden volume hashes of seeded trees (filled from the first reviewed run; empty: record only).
ll::coro::CoroTask<> golden(Context& ctx, int dx, std::string const& id, std::string const& want) {
    auto     box = co_await ctx.flatArea(dx, 48, 48);
    BlockPos pos{box.minX + 24, box.minY, box.minZ + 24};
    co_await ctx.ensureLoaded(game::globalPos(ctx.dimension(), pos));
    auto writes = leafDistanceWrites();
    ctx.info(id + " at " + std::to_string(pos.x) + " " + std::to_string(pos.y) + " " + std::to_string(pos.z) + " (box " + std::to_string(box.minX) + " " +
             std::to_string(box.minY) + " " + std::to_string(box.minZ) + " .. " + std::to_string(box.maxX) + " " + std::to_string(box.maxY) + " " + std::to_string(box.maxZ) + ")");
    ctx.require(placeSeeded(ctx, id, pos, 20240611), id + " placed");
    ctx.info(id + ": updateLeaves wrote " + std::to_string(leafDistanceWrites() - writes) + " leaf distances");
    auto hash = ctx.snapshotVolume(box);
    ctx.info(id + " volume hash " + hash);
    if (!want.empty()) ctx.check(hash == want, id + " matches its golden hash " + want + " (got " + hash + ")");
    compareWithJava(ctx, "golden_" + id.substr(id.rfind('/') + 1), box);
}

} // namespace

GF_REGISTER_SELFTEST("trees.golden_canopy_tree", [](Context& ctx) -> ll::coro::CoroTask<> { co_await golden(ctx, 330, tree("canopy_tree"), "e59320ec7205bcde"); });
GF_REGISTER_SELFTEST("trees.golden_mega_canopy_tree", [](Context& ctx) -> ll::coro::CoroTask<> { co_await golden(ctx, 335, tree("mega_canopy_tree"), "0e085f04c2c2d4de"); });
GF_REGISTER_SELFTEST("trees.golden_darkwood_tree", [](Context& ctx) -> ll::coro::CoroTask<> { co_await golden(ctx, 340, tree("darkwood_tree"), "4b02f8878de8e514"); });
GF_REGISTER_SELFTEST("trees.golden_mangrove_tree", [](Context& ctx) -> ll::coro::CoroTask<> { co_await golden(ctx, 345, tree("mangrove_tree"), "1028ba086a8b4c9d"); });

namespace {

SimulatedPlayer& sim(Context& ctx) { return static_cast<SimulatedPlayer&>(ctx.player()); }

/// The test player right-clicks the top face of `p` with `itemId` (16 of it; "" = empty hand), as
/// the network handler runs a click (GameMode::useItemOn, a first event).
ll::coro::CoroTask<> click(Context& ctx, GlobalPos p, std::string itemId, bool sneak = false) {
    auto& sp = sim(ctx);
    sp.teleport(Vec3{p.x + 0.5f, p.y + 1.0f, p.z - 1.5f}, sp.getDimensionId());
    sp.simulateLookAt(bp(p), sim::LookDuration::Instant);
    if (itemId.empty()) sp.simulateSetItem(ItemStack::EMPTY_ITEM(), true, 0);
    else if (auto item = game::items::make(itemId, 16)) sp.simulateSetItem(*item, true, 0);
    sp.setSneaking(sneak);
    co_await ctx.ticks(8);
    auto&    s2   = sim(ctx);
    BlockPos b    = bp(p);
    auto&    held = const_cast<ItemStack&>(s2.getSelectedItem());
    s2.mGameMode->useItemOn(held, b, 1, Vec3{b.x + 0.5f, b.y + 1.0f, b.z + 0.5f}, HandSlot::Mainhand, nullptr, true);
    s2.setSneaking(false);
    co_await ctx.ticks(2);
}

bool activeAt(Context& ctx, GlobalPos const& p) {
    auto* region = ctx.region();
    if (!region) return false;
    auto states = game::statesOf(region->getBlock(bp(p)));
    auto it     = states.find("twilightforest:active");
    return it != states.end() && (it->second == "true" || it->second == "1");
}

struct RangeGuard {
    TreesConfig saved = gf::config::get<TreesConfig>();
    ~RangeGuard() { gf::config::mut<TreesConfig>() = saved; }
};

/// Puts the ground under the grass of an area back to the flat world's layers (those of the area's
/// corner column, which no tree reaches): a sapling grown through the game's setters leaves its
/// underground roots there, outside what flatArea restores. Roots left by an earlier tree would stop
/// the next tree's roots (#twilightforest:tree_roots_skip) and shift its random draws.
void resetGround(BlockSource& region, int minX, int minZ, int maxX, int maxZ, int grassY) {
    int const                 bottom = region.getMinHeight();
    std::vector<Block const*> column;
    for (int y = bottom; y < grassY; ++y) column.push_back(&region.getBlock(BlockPos{minX, y, minZ}));
    // the four corners agree on an untouched flat world; otherwise a corner was changed: leave it
    for (auto [cx, cz] : {std::pair{maxX, minZ}, std::pair{minX, maxZ}, std::pair{maxX, maxZ}})
        for (int y = bottom; y < grassY; ++y)
            if (&region.getBlock(BlockPos{cx, y, cz}) != column[static_cast<std::size_t>(y - bottom)]) {
                gf::log().warn("trees: the ground's corner columns differ at {} {}: not reset", cx, cz);
                return;
            }
    for (int x = minX; x <= maxX; ++x)
        for (int z = minZ; z <= maxZ; ++z)
            for (int y = bottom; y < grassY; ++y) {
                BlockPos     p{x, y, z};
                Block const* want = column[static_cast<std::size_t>(y - bottom)];
                if (&region.getBlock(p) != want) game::setBlock(region, p, *want);
            }
}

} // namespace

GF_REGISTER_SELFTEST("trees.sapling_growers", [](Context& ctx) -> ll::coro::CoroTask<> {
    char const* saplings[] = {"twilight_oak_sapling", "canopy_sapling", "mangrove_sapling", "darkwood_sapling", "time_sapling",
                              "transformation_sapling", "mining_sapling", "sorting_sapling", "rainbow_oak_sapling"};
    auto box = co_await ctx.flatArea(350, 144, 60);
    for (int i = 0; i < 9; ++i) {
        GlobalPos p = game::globalPos(ctx.dimension(), BlockPos{box.minX + 24 + (i % 3) * 48, box.minY, box.minZ + 24 + (i / 3) * 48});
        std::string id = std::string("twilightforest:") + saplings[i];
        if (id == "twilightforest:time_sapling" && ctx.region()) p = game::globalPos(ctx.dimension(), pedestal(*ctx.region(), bp(p)));
        ctx.require(ctx.setBlock(p, id), id + " planted");
        bool grown = false;
        for (int tries = 0; tries < 40 && !grown; ++tries) {
            co_await click(ctx, p, "minecraft:bone_meal");
            grown = ctx.blockAt(p) != id;
        }
        ctx.check(grown, id + " grew its tree on bone meal (now " + ctx.blockAt(p) + ")");
        if (grown) ctx.info(id + " grew into " + ctx.blockAt(p));
    }
    // the roots under the grass (flatArea puts back only its box)
    if (auto* region = ctx.region()) resetGround(*region, box.minX, box.minZ, box.maxX, box.maxZ, box.minY - 1);
});

namespace {

/// Golden volume hashes of each sapling's tree grown with a seeded random (SaplingBlock#advanceTree
/// twice: stage 1, then the tree), recorded from a run whose boxes matched TF 4.8 on Java block for block
/// (compareWithJava, the oracle's sapling_* cases).
struct SaplingGolden {
    char const* sapling;
    char const* hash;
};
constexpr SaplingGolden kSaplingGoldens[] = {
    {"twilight_oak_sapling", "a1b2c5a299f0e75e"}, {"canopy_sapling", "d9124ba1a42db576"},         {"mangrove_sapling", "d9d95df8efd64c0b"},       {"darkwood_sapling", "8d2cb57d8fbc026a"},
    {"time_sapling", "488bf3bed2c51e32"},         {"transformation_sapling", "90f238ac17b5fe5c"}, {"mining_sapling", "cb9cad4efe78e3c1"},         {"sorting_sapling", "761d50d030059e92"},
    {"rainbow_oak_sapling", "99fd8e7c28d9425d"},
};

} // namespace

GF_REGISTER_SELFTEST("trees.sapling_hashes", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(352, 144, 64);
    for (int i = 0; i < 9; ++i) {
        auto const& g  = kSaplingGoldens[i];
        BlockBox    cell{box.minX + (i % 3) * 48, box.minY, box.minZ + (i / 3) * 48, box.minX + (i % 3) * 48 + 47, box.maxY, box.minZ + (i / 3) * 48 + 47};
        GlobalPos   p  = game::globalPos(ctx.dimension(), BlockPos{cell.minX + 24, box.minY, cell.minZ + 24});
        std::string id = std::string("twilightforest:") + g.sapling;
        co_await ctx.ensureLoaded(p);
        auto* region = ctx.region();
        ctx.require(region != nullptr, "a region");
        // the flat world's ground under the cell (the Java oracle resets it too): no roots of an
        // earlier tree (trees.sapling_growers grows the same saplings around the same spots)
        resetGround(*region, cell.minX, cell.minZ, cell.maxX, cell.maxZ, box.minY - 1);
        if (id == "twilightforest:time_sapling") p = game::globalPos(ctx.dimension(), pedestal(*region, bp(p)));
        ctx.require(ctx.setBlock(p, id), id + " planted");
        ctx.info(id + " at " + std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z) + " (cell " + std::to_string(cell.minX) + " " +
                 std::to_string(cell.minY) + " " + std::to_string(cell.minZ) + " .. " + std::to_string(cell.maxX) + " " + std::to_string(cell.maxY) + " " + std::to_string(cell.maxZ) + ")");
        core::jrand::LegacyRandom random(77000 + i);
        for (int step = 0; step < 2; ++step) gf::feature::families::detail::advanceSaplingWith(*region, bp(p), random);
        bool grown = ctx.blockAt(p) != id;
        ctx.check(grown, id + " grew with a seeded random (now " + ctx.blockAt(p) + ")");
        auto hash = ctx.snapshotVolume(cell);
        ctx.info(id + " tree volume hash " + hash);
        if (*g.hash) ctx.check(hash == g.hash, id + " matches its golden hash " + std::string(g.hash) + " (got " + hash + ")");
        compareWithJava(ctx, std::string("sapling_") + g.sapling, cell);
    }
});

GF_REGISTER_SELFTEST("trees.hollow_oak_sapling", [](Context& ctx) -> ll::coro::CoroTask<> {
    // StructureTreeGrower: HollowTreeStructure#checkSaplingClearance first (3 around, 3 below to 128
    // above, everything replaceable by trees), the sapling stays when it fails; then the hollow tree
    // landmark (U18) starts at the sapling
    auto      box = co_await ctx.flatArea(366, 16, 140);
    GlobalPos p   = game::globalPos(ctx.dimension(), BlockPos{box.minX + 8, box.minY, box.minZ + 8});
    co_await ctx.ensureLoaded(p);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    // the flat world's bedrock is within 3 blocks below the surface (not replaceable): a dirt pedestal
    p = game::globalPos(ctx.dimension(), pedestal(*region, bp(p)));
    ctx.require(ctx.setBlock(p, "twilightforest:hollow_oak_sapling", {{"twilightforest:stage", 1}}), "a robust twilight oak sapling");
    // an obsidian block 40 above, inside the clearance column: no tree, the sapling stays
    GlobalPos stone = p.offset(2, 40, -1);
    ctx.require(ctx.setBlock(stone, "minecraft:obsidian"), "an obstacle above");
    ctx.check(!hollowTreeClearance(*region, bp(p)), "the obstacle fails the sapling clearance");
    core::jrand::LegacyRandom random(4711);
    gf::feature::families::detail::advanceSaplingWith(*region, bp(p), random);
    ctx.check(ctx.blockAt(p) == "twilightforest:hollow_oak_sapling", "without clearance the sapling stays (" + ctx.blockAt(p) + ")");
    ctx.cleanupBlock(stone);
    if (auto const* air = game::blockWith("minecraft:air")) game::setBlock(*region, bp(stone), *air);
    ctx.check(hollowTreeClearance(*region, bp(p)), "a clear column passes the sapling clearance");
    gf::feature::families::detail::advanceSaplingWith(*region, bp(p), random);
    if (ctx.blockAt(p) == "twilightforest:hollow_oak_sapling")
        ctx.info("NOT RUN: the hollow tree landmark (U18's twilightforest:hollow_tree generator) did not start; the sapling stayed as TF's grower "
                 "leaves it without a valid start");
    else ctx.info("the robust twilight oak sapling grew the hollow tree (now " + ctx.blockAt(p) + ")");
});

namespace {

/// The 2x2 sapling cases (TreeGrower#growTree's mega branch), each in a 48-block cell of one flat area:
/// four saplings planted at the cell's centre (p, +x, +z, +xz) and SaplingBlock#advanceTree twice at p
/// with LegacyRandom(seed). Twilight oak: getConfiguredMegaFeature draws nextFloat() < 0.1 first
/// (78001: 0.245, the forest mega oak; 83748: 0.016, the savannah mega oak); canopy has no secondary
/// mega tree (no draw).
struct MegaCase {
    char const* name;
    char const* sapling;
    std::int64_t seed;
    char const* expect; ///< a log the tree must have at p
};
constexpr MegaCase kMegaCases[] = {
    {"mega_canopy_sapling", "canopy_sapling", 78000, "twilightforest:canopy_log"},
    {"mega_twilight_oak_sapling_forest", "twilight_oak_sapling", 78001, "twilightforest:twilight_oak_log"},
    {"mega_twilight_oak_sapling_savannah", "twilight_oak_sapling", 83748, "twilightforest:twilight_oak_log"},
};

} // namespace

GF_REGISTER_SELFTEST("trees.mega_2x2", [](Context& ctx) -> ll::coro::CoroTask<> {
    // bone meal on a 2x2 of canopy saplings grows the mega canopy tree
    {
        auto      box = co_await ctx.flatArea(360, 64, 64);
        GlobalPos p   = game::globalPos(ctx.dimension(), BlockPos{box.minX + 30, box.minY, box.minZ + 30});
        for (auto [dx, dz] : {std::pair{0, 0}, std::pair{1, 0}, std::pair{0, 1}, std::pair{1, 1}})
            ctx.require(ctx.setBlock(p.offset(dx, 0, dz), "twilightforest:canopy_sapling"), "sapling planted");
        bool grown = false;
        for (int tries = 0; tries < 40 && !grown; ++tries) {
            co_await click(ctx, p, "minecraft:bone_meal");
            grown = ctx.blockAt(p) != "twilightforest:canopy_sapling";
        }
        ctx.require(grown, "the 2x2 canopy saplings grew on bone meal");
        int trunk = 0;
        for (auto [dx, dz] : {std::pair{0, 0}, std::pair{1, 0}, std::pair{0, 1}, std::pair{1, 1}})
            for (int y = 0; y < 20; ++y)
                if (ctx.blockAt(p.offset(dx, y, dz)) == "twilightforest:canopy_log") ++trunk;
        ctx.check(trunk >= 70, "a 2x2 mega canopy trunk (" + std::to_string(trunk) + " logs in the lowest 20 layers)");
        if (auto* region = ctx.region()) resetGround(*region, box.minX, box.minZ, box.maxX, box.maxZ, box.minY - 1);
    }
});

// The 2x2 saplings seeded, block for block against TF 4.8 on Java (the oracle's mega_* sapling cases).
GF_REGISTER_SELFTEST("trees.mega_2x2_java", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(361, 144, 80);
    for (int i = 0; i < 3; ++i) {
        auto const& mc   = kMegaCases[i];
        BlockBox    cell{box.minX + i * 48, box.minY, box.minZ, box.minX + i * 48 + 47, box.maxY, box.minZ + 47};
        GlobalPos   p    = game::globalPos(ctx.dimension(), BlockPos{cell.minX + 24, box.minY, cell.minZ + 24});
        std::string id   = std::string("twilightforest:") + mc.sapling;
        co_await ctx.ensureLoaded(p);
        auto* region = ctx.region();
        ctx.require(region != nullptr, "a region");
        // the flat world's ground under the whole area first (the oracle resets it once per case)
        if (i == 0) resetGround(*region, box.minX, box.minZ, box.maxX, box.maxZ, box.minY - 1);
        for (auto [dx, dz] : {std::pair{0, 0}, std::pair{1, 0}, std::pair{0, 1}, std::pair{1, 1}})
            ctx.require(ctx.setBlock(p.offset(dx, 0, dz), id), id + " planted");
        ctx.info(std::string(mc.name) + " at " + std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z) + " (cell " + std::to_string(cell.minX) + " " +
                 std::to_string(cell.minY) + " " + std::to_string(cell.minZ) + " .. " + std::to_string(cell.maxX) + " " + std::to_string(cell.maxY) + " " + std::to_string(cell.maxZ) + ")");
        core::jrand::LegacyRandom random(mc.seed);
        for (int step = 0; step < 2; ++step) gf::feature::families::detail::advanceSaplingWith(*region, bp(p), random);
        ctx.check(ctx.blockAt(p) == mc.expect, std::string(mc.name) + " grew a 2x2 tree (" + ctx.blockAt(p) + " at the sapling)");
        ctx.info(std::string(mc.name) + " tree volume hash " + ctx.snapshotVolume(cell));
        compareWithJava(ctx, mc.name, cell);
    }
    if (auto* region = ctx.region()) resetGround(*region, box.minX, box.minZ, box.maxX, box.maxZ, box.minY - 1);
});
GF_REGISTER_SELFTEST("trees.leaf_distance_after_growth", [](Context& ctx) -> ll::coro::CoroTask<> {
    // TFTreeFeature#place runs TreeFeature.updateLeaves with the leaves as sources and writes with
    // UPDATE_KNOWN_SHAPE: every leaf with a leaf beside it gets distance 1 and keeps it (no shape
    // update recomputes it), so a grown TF tree's outer leaves never decay in TF. F11's leaves
    // recompute the true distance after a placement (onPlaced / onNeighborChanged); this test grows
    // a mega canopy tree, counts the decay-bound leaves (distance 7) right away and 40 ticks later
    // and reports the difference (routed to F11: skip the recompute for feature-placed blocks).
    auto      box = co_await ctx.flatArea(362, 64, 64);
    GlobalPos p   = game::globalPos(ctx.dimension(), BlockPos{box.minX + 30, box.minY, box.minZ + 30});
    for (auto [dx, dz] : {std::pair{0, 0}, std::pair{1, 0}, std::pair{0, 1}, std::pair{1, 1}}) ctx.require(ctx.setBlock(p.offset(dx, 0, dz), "twilightforest:canopy_sapling"), "sapling planted");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    core::jrand::LegacyRandom random(5150);
    for (int step = 0; step < 2 && ctx.blockAt(p) == "twilightforest:canopy_sapling"; ++step) gf::feature::families::detail::advanceSaplingWith(*region, bp(p), random);
    ctx.require(ctx.blockAt(p) != "twilightforest:canopy_sapling", "the 2x2 canopy saplings grew (" + ctx.blockAt(p) + ")");
    auto count = [&](int& leaves, int& decaying, int& one) {
        leaves = decaying = one = 0;
        auto* r = ctx.region();
        if (!r) return;
        for (int x = box.minX; x <= box.maxX; ++x)
            for (int z = box.minZ; z <= box.maxZ; ++z)
                for (int y = box.minY; y <= box.maxY; ++y) {
                    auto const& b = r->getBlock(BlockPos{x, y, z});
                    auto        d = game::intState(b, "twilightforest:distance");
                    if (!d || b.getTypeName() != "twilightforest:canopy_leaves") continue;
                    ++leaves;
                    if (*d == 7 || *d == 0) ++decaying;
                    if (*d == 1) ++one;
                }
    };
    int leaves = 0, decaying = 0, one = 0;
    count(leaves, decaying, one);
    ctx.info("right after growth: " + std::to_string(leaves) + " canopy leaves, " + std::to_string(one) + " at distance 1, " + std::to_string(decaying) + " decay-bound");
    ctx.check(leaves > 0 && one * 2 > leaves, "updateLeaves gave most leaves distance 1 as TF does (" + std::to_string(one) + " of " + std::to_string(leaves) + ")");
    co_await ctx.ticks(40);
    int leaves2 = 0, decaying2 = 0, one2 = 0;
    count(leaves2, decaying2, one2);
    ctx.info("40 ticks later: " + std::to_string(leaves2) + " canopy leaves, " + std::to_string(one2) + " at distance 1, " + std::to_string(decaying2) + " decay-bound");
    if (decaying2 != decaying || one2 != one)
        ctx.info("PARITY GAP (F11): F11 recomputed the distances updateLeaves wrote (TF keeps them: UPDATE_KNOWN_SHAPE): decay-bound leaves " + std::to_string(decaying) +
                 " in TF, " + std::to_string(decaying2) + " here; at distance 1 " + std::to_string(one) + " in TF, " + std::to_string(one2) + " here");
});
GF_REGISTER_SELFTEST("trees.core_toggle_held_item", [](Context& ctx) -> ll::coro::CoroTask<> {
    GlobalPos core = ctx.testPos(370, 2);
    ctx.require(ctx.setBlock(core, "twilightforest:time_log_core", {{"twilightforest:axis", "y"}}), "a timewood clock");
    ctx.cleanupBlock(core.offset(0, 1, 0));
    co_await click(ctx, core, "minecraft:dirt");
    ctx.check(activeAt(ctx, core), "using the core with dirt in hand activates it");
    ctx.check(ctx.blockAt(core.offset(0, 1, 0)) != "minecraft:dirt", "no dirt was placed on the core");
    ctx.check(game::items::countOf(sim(ctx).getSelectedItem()) == 16, "the held dirt was not used");
    co_await click(ctx, core, "");
    ctx.check(!activeAt(ctx, core), "using it again with an empty hand deactivates it");
    co_await click(ctx, core, "minecraft:dirt", true);
    ctx.check(!activeAt(ctx, core), "sneaking with an item skips the core");
    ctx.check(ctx.blockAt(core.offset(0, 1, 0)) == "minecraft:dirt", "sneaking with dirt places the dirt");
});

GF_REGISTER_SELFTEST("trees.core_disabled_config", [](Context& ctx) -> ll::coro::CoroTask<> {
    RangeGuard guard;
    gf::config::mut<TreesConfig>().timeCoreRange = 0;
    GlobalPos core = ctx.testPos(372, 2);
    ctx.require(ctx.setBlock(core, "twilightforest:time_log_core", {{"twilightforest:axis", "y"}, {"twilightforest:active", true}}), "an active timewood clock");
    co_await click(ctx, core, "");
    ctx.check(!activeAt(ctx, core), "a disabled core turns off when used");
    auto text = coreDisabledMessage(ctx.player(), Core::Time);
    ctx.check(text.find("Timewood Clock") != std::string::npos && text.find("disabled via config") != std::string::npos, "the message names the core: " + text);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    ctx.check(performCoreEffect(Core::Time, *region, bp(core)) == 0, "a disabled core does nothing");
});

GF_REGISTER_SELFTEST("trees.time_core_ticks", [](Context& ctx) -> ll::coro::CoroTask<> {
    RangeGuard guard;
    gf::config::mut<TreesConfig>().timeCoreRange = 1;
    GlobalPos core = ctx.testPos(374, 2);
    GlobalPos soil = core.offset(1, 0, 0), crop = core.offset(1, 1, 0);
    ctx.require(ctx.setBlock(core, "twilightforest:time_log_core", {{"twilightforest:axis", "y"}}), "a timewood clock");
    ctx.require(ctx.setBlock(soil, "minecraft:farmland", {{"moisturized_amount", 7}}), "farmland");
    ctx.require(ctx.setBlock(crop, "minecraft:wheat", {{"growth", 0}}), "wheat");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    int grown = 0, ticked = 0;
    // one cycle at a time until a cycle grew the wheat; the farmland is wetted before each cycle (its
    // own random ticks may dry it out)
    for (int cycle = 0; cycle < 40 && grown == 0; ++cycle) {
        region = ctx.region();
        ctx.require(region != nullptr, "a region");
        if (auto const* wet = game::blockWithStates("minecraft:farmland", {{"moisturized_amount", 7}}); wet && &region->getBlock(bp(soil)) != wet)
            (void)ctx.setBlock(soil, "minecraft:farmland", {{"moisturized_amount", 7}});
        ticked += performCoreEffect(Core::Time, *region, bp(core));
        grown = game::intState(region->getBlock(bp(crop)), "growth").value_or(0);
        co_await ctx.ticks(1);
    }
    ctx.check(ticked > 0, "the core ticked blocks (" + std::to_string(ticked) + ")");
    ctx.check(grown > 0, "the wheat grew to " + std::to_string(grown));
    ctx.cleanupBlock(crop);
    // BlockState#isRandomlyTicking: the state decides, not the block type. A probe cell the core's
    // cube never reached (range 1), cleared first, so each probe state is a change
    region = ctx.region();
    ctx.require(region != nullptr, "a region");
    GlobalPos probe = core.offset(-1, 3, 0);
    ctx.cleanupBlock(probe);
    auto works = [&](char const* id, nlohmann::json const& states) {
        (void)ctx.setBlock(probe, "minecraft:air", nlohmann::json::object());
        ctx.require(ctx.setBlock(probe, id, states), std::string(id) + " " + states.dump());
        return timeCoreWorks(*region, bp(probe));
    };
    ctx.check(works("minecraft:wheat", {{"growth", 3}}), "growing wheat ticks");
    ctx.check(!works("minecraft:wheat", {{"growth", 7}}), "fully grown wheat does not tick (CropBlock: below the maximum age only)");
    ctx.check(!works("twilightforest:twilight_oak_leaves", {{"twilightforest:persistent", true}, {"twilightforest:distance", 7}}), "persistent leaves do not tick");
    ctx.check(!works("twilightforest:time_leaves", {{"twilightforest:persistent", false}, {"twilightforest:distance", 1}}),
              "leaves near a log (distance 1) do not tick (LeavesBlock: distance 7 only)");
    ctx.check(works("twilightforest:time_leaves", {{"twilightforest:persistent", false}, {"twilightforest:distance", 7}}), "decaying leaves tick");
    ctx.check(!works("minecraft:oak_leaves", {{"persistent_bit", true}}), "persistent vanilla leaves do not tick");
    ctx.check(!works("minecraft:cocoa", {{"age", 2}, {"direction", 0}}), "ripe cocoa does not tick");
    ctx.check(!works("minecraft:nether_wart", {{"age", 3}}), "ripe nether wart does not tick");
    ctx.check(works("minecraft:nether_wart", {{"age", 1}}), "growing nether wart ticks");
    ctx.check(!works("minecraft:cave_vines_body_with_berries", nlohmann::json::object()), "a cave vines body never ticks");
    (void)ctx.setBlock(probe, "minecraft:air", nlohmann::json::object());
});

GF_REGISTER_SELFTEST("trees.time_core_block_entity_tickers", [](Context& ctx) -> ll::coro::CoroTask<> {
    // TimeLogCoreBlock: state.getTicker(...) of ANY block entity, TF's own included: a unit registers
    // its block's ticker (gf::trees_ext::registerTimeCoreTicker); here a plain vanilla block stands in.
    // Not gf_test:fixture_block: families.blockevents leaves a random tick handler on it for the rest
    // of the run (blockevents has no removal), and a randomly ticking block counts without asking its
    // ticker, so in the full run the ticker was never asked and "no ticker" still ticked.
    static constexpr char kStandIn[] = "minecraft:smooth_basalt";
    RangeGuard guard;
    gf::config::mut<TreesConfig>().timeCoreRange = 1;
    GlobalPos core = ctx.testPos(378, 2), fixture = core.offset(1, 0, 0), box = core.offset(-1, 0, 0);
    ctx.require(ctx.setBlock(core, "twilightforest:time_log_core", {{"twilightforest:axis", "y"}}), "a timewood clock");
    ctx.require(ctx.setBlock(fixture, kStandIn), "the stand-in block");
    ctx.require(gf::trees_ext::timeCoreTicker(kStandIn) == nullptr, std::string("no ticker registered for ") + kStandIn + " yet");
    {
        auto* r = ctx.region();
        ctx.require(r != nullptr && !timeCoreWorks(*r, bp(fixture)), std::string(kStandIn) + " does not tick on its own");
    }
    struct Unregister {
        ~Unregister() { gf::trees_ext::unregisterTimeCoreTicker(kStandIn); }
    } unregister;
    int ticks = 0, asked = 0;
    bool hasTicker = true;
    gf::trees_ext::registerTimeCoreTicker(kStandIn, [&](BlockSource&, BlockPos const&, bool simulate) {
        if (!hasTicker) return false;
        if (simulate) ++asked;
        else ++ticks;
        return true;
    });
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    ctx.check(timeCoreWorks(*region, bp(fixture)), "a block with a registered ticker counts as ticking");
    int worked = performCoreEffect(Core::Time, *region, bp(core));
    ctx.check(ticks > 0 && worked >= ticks, "one cycle ran the registered ticker " + std::to_string(ticks) + " times (" + std::to_string(worked) + " hits worked)");
    ctx.check(asked == 1, "the cycle itself never only asked");
    hasTicker = false; // Java: getTicker returned null in this state
    int before = ticks;
    ctx.check(!timeCoreWorks(*region, bp(fixture)), "no ticker in this state: not ticking");
    (void)performCoreEffect(Core::Time, *region, bp(core));
    ctx.check(ticks == before, "a block without a ticker in its state is skipped");
    // JukeboxBlock#getTicker: null without a record
    ctx.require(ctx.setBlock(box, "minecraft:jukebox"), "a jukebox");
    co_await ctx.ticks(1);
    region = ctx.region();
    ctx.require(region != nullptr, "a region");
    ctx.check(!timeCoreWorks(*region, bp(box)), "an empty jukebox does not tick");
    auto* actor = region->getBlockEntity(bp(box));
    auto* main  = actor ? actor->_getMainComponent() : nullptr;
    auto* c     = main ? main->getContainer() : nullptr;
    ctx.require(c != nullptr, "the jukebox's container");
    if (auto disc = game::items::make("minecraft:music_disc_13", 1)) c->setItem(0, *disc);
    ctx.check(timeCoreWorks(*region, bp(box)), "a jukebox with a record ticks");
    c->setItem(0, ItemStack::EMPTY_ITEM());
    // signs tick (SignBlockEntity::tick)
    ctx.require(ctx.setBlock(box, "minecraft:air"), "air");
    ctx.require(ctx.setBlock(box, "minecraft:standing_sign", {{"ground_sign_direction", 0}}), "a sign");
    co_await ctx.ticks(1);
    region = ctx.region();
    ctx.require(region != nullptr, "a region");
    auto* sign = region->getBlockEntity(bp(box));
    ctx.info(std::string("the sign's block entity ") + (sign && sign->_getTickComponent() ? "ticks" : "has no tick component on Bedrock"));
    ctx.check(!sign || !sign->_getTickComponent() || timeCoreWorks(*region, bp(box)), "a sign with a ticking block entity ticks");
});

GF_REGISTER_SELFTEST("trees.transformation_core_biome", [](Context& ctx) -> ll::coro::CoroTask<> {
    RangeGuard guard;
    gf::config::mut<TreesConfig>().transformationCoreRange = 3;
    GlobalPos core   = ctx.testPos(376, 2);
    auto*     region = ctx.region();
    ctx.require(region != nullptr, "a region");
    ctx.require(ctx.setBlock(core, "twilightforest:transformation_log_core", {{"twilightforest:axis", "y"}}), "a heart of transformation");
    // The enchanted forest is U1's behaviour pack biome. Without it (U1 not merged) the core does
    // nothing in game; the test then says so and checks the mechanism with a stand-in biome.
    std::string want = "twilightforest:enchanted_forest";
    struct TargetGuard {
        ~TargetGuard() { setTransformationTargetForTest(""); }
    } targetGuard;
    if (!hasTransformationTarget(*region)) {
        want = "minecraft:mushroom_island";
        setTransformationTargetForTest(want);
        ctx.info("NOT RUN with the enchanted forest: no twilightforest:enchanted_forest biome is registered (U1's behaviour pack biome), so the "
                 "core does nothing in this build; the biome rewrite, the chunk save mark and the resend are checked with the stand-in " + want);
    }
    // the biomes of the columns within reach, put back afterwards
    BlockPos                                    c = bp(core);
    std::map<std::pair<int, int>, Biome const*> saved;
    // (range 3: the column's 4x4 quart cell reaches 6 blocks from the core)
    for (int dx = -7; dx <= 7; ++dx)
        for (int dz = -7; dz <= 7; ++dz) saved[{c.x + dx, c.z + dz}] = &region->getBiome(BlockPos{c.x + dx, c.y, c.z + dz});
    auto* chunk  = region->getChunkAt(c);
    ctx.require(chunk != nullptr, "the core's chunk");
    auto  before = chunkResendStats();
    int   changed = 0;
    for (int i = 0; i < 20 && changed == 0; ++i) changed = performCoreEffect(Core::Transformation, *region, c);
    int        transformed = 0;
    std::set<LevelChunk*> chunks;
    for (auto const& [xz, b] : saved) {
        BlockPos p{xz.first, c.y, xz.second};
        if (biomeName(region->getBiome(p)) == want && biomeName(*b) != want) {
            ++transformed;
            if (auto* lc = region->getChunkAt(p)) chunks.insert(lc);
        }
    }
    // the chunk will save (LevelChunk#setUnsaved): its biome data counter was touched this tick (a
    // clean DirtyTicksCounter counts from INT_MIN; a touched one has lastChange == totalTime >= 0)
    bool dirty = !chunks.empty() && std::all_of(chunks.begin(), chunks.end(), [](LevelChunk* lc) { return gf::feature::ext::trees::biomesUnsaved(*lc); });

    auto after = chunkResendStats();
    ctx.info("chunk resends: " + std::to_string(after.sent - before.sent) + " sent, " + std::to_string(after.refused - before.refused) +
             " refused (simulated players have no client)");
    for (auto const& [xz, b] : saved)
        if (auto* lc = region->getChunkAt(BlockPos{xz.first, c.y, xz.second}))
            lc->_setBiome(*b, ChunkBlockPos{static_cast<uchar>(xz.first & 15), ChunkLocalHeight{0}, static_cast<uchar>(xz.second & 15)}, true);
    ctx.check(changed == 1, "the core transformed a column");
    ctx.check(transformed >= 16, "a 4x4 column became " + want + " (" + std::to_string(transformed) + " columns)");
    ctx.check(dirty, "the transformed chunk is marked for saving");
    co_return;
});

GF_REGISTER_SELFTEST("trees.core_range_clamp", [](Context& ctx) -> ll::coro::CoroTask<> {
    RangeGuard guard;
    gf::config::mut<TreesConfig>().sortingCoreRange = 1000;
    gf::config::mut<TreesConfig>().timeCoreRange    = -5;
    ctx.check(coreRange(Core::Sorting) == 128, "a range above 128 is clamped to 128 (TFConfig defineInRange)");
    ctx.check(coreRange(Core::Time) == 0, "a negative range is clamped to 0 (disabled)");
    co_return;
});

GF_REGISTER_SELFTEST("trees.mining_core_magnet", [](Context& ctx) -> ll::coro::CoroTask<> {
    RangeGuard guard;
    gf::config::mut<TreesConfig>().miningCoreRange = 2;
    auto     box  = co_await ctx.flatArea(380, 9, 12);
    BlockPos c{box.minX + 4, box.minY + 4, box.minZ + 4};
    GlobalPos core = game::globalPos(ctx.dimension(), c);
    // stone around the core, iron ore on the shell two blocks out
    for (int dx = -2; dx <= 2; ++dx)
        for (int dy = -2; dy <= 2; ++dy)
            for (int dz = -2; dz <= 2; ++dz) {
                int d = std::max({std::abs(dx), std::abs(dy), std::abs(dz)});
                if (d == 0) continue;
                ctx.setBlock(core.offset(dx, dy, dz), d == 1 ? "minecraft:stone" : "minecraft:iron_ore");
            }
    ctx.require(ctx.setBlock(core, "twilightforest:mining_log_core", {{"twilightforest:axis", "y"}}), "a minewood core");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    int moved = 0;
    for (int i = 0; i < 60 && moved == 0; ++i) moved = performCoreEffect(Core::Mining, *region, c);
    ctx.check(moved > 0, "the core pulled ore (" + std::to_string(moved) + " blocks)");
    int innerOre = 0;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -1; dz <= 1; ++dz)
                if (ctx.blockAt(core.offset(dx, dy, dz)) == "minecraft:iron_ore") ++innerOre;
    ctx.check(innerOre > 0, "ore moved next to the core (" + std::to_string(innerOre) + ")");
});

GF_REGISTER_SELFTEST("trees.mining_core_excludes_coal", [](Context& ctx) -> ll::coro::CoroTask<> {
    // TREE_ORE_TO_BLOCK_REPLACEMENTS: every ores_in_ground / ore_bearing_ground pair, minus
    // #twilightforest:mining_tree_excluded (#minecraft:coal_ores), plus ancient debris
    auto ores = miningCoreOres();
    auto has  = [&](char const* ore, char const* ground) {
        auto it = ores.find(ore);
        return it != ores.end() && it->second == ground;
    };
    ctx.check(has("minecraft:iron_ore", "minecraft:stone"), "iron ore sits in stone");
    ctx.check(has("minecraft:deepslate_iron_ore", "minecraft:deepslate"), "deepslate iron ore sits in deepslate");
    ctx.check(has("minecraft:quartz_ore", "minecraft:netherrack"), "nether quartz ore sits in netherrack");
    ctx.check(has("twilightforest:liveroot_block", "twilightforest:root"), "liveroot sits in roots (c:ores_in_ground/root)");
    ctx.check(has("minecraft:ancient_debris", "minecraft:netherrack"), "ancient debris sits in netherrack");
    ctx.check(!ores.contains("minecraft:coal_ore") && !ores.contains("minecraft:deepslate_coal_ore"), "coal ores are excluded");
    RangeGuard guard;
    gf::config::mut<TreesConfig>().miningCoreRange = 2;
    auto      box  = co_await ctx.flatArea(383, 9, 12);
    BlockPos  c{box.minX + 4, box.minY + 4, box.minZ + 4};
    GlobalPos core = game::globalPos(ctx.dimension(), c);
    // stone around the core, a coal vein on the shell two blocks out: it never moves
    for (int dx = -2; dx <= 2; ++dx)
        for (int dy = -2; dy <= 2; ++dy)
            for (int dz = -2; dz <= 2; ++dz) {
                int d = std::max({std::abs(dx), std::abs(dy), std::abs(dz)});
                if (d == 0) continue;
                ctx.setBlock(core.offset(dx, dy, dz), d == 1 ? "minecraft:stone" : "minecraft:coal_ore");
            }
    ctx.require(ctx.setBlock(core, "twilightforest:mining_log_core", {{"twilightforest:axis", "y"}}), "a minewood core");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    int moved = 0;
    for (int i = 0; i < 60; ++i) moved += performCoreEffect(Core::Mining, *region, c);
    ctx.check(moved == 0, "the core moved no coal (" + std::to_string(moved) + " blocks)");
    int coal = 0;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -1; dz <= 1; ++dz)
                if (ctx.blockAt(core.offset(dx, dy, dz)) == "minecraft:coal_ore") ++coal;
    ctx.check(coal == 0, "no coal next to the core (" + std::to_string(coal) + ")");
});

namespace {
Container* chestAt(BlockSource& region, BlockPos const& p) {
    auto* actor = region.getBlockEntity(p);
    auto* main  = actor ? actor->_getMainComponent() : nullptr;
    return main ? main->getContainer() : nullptr;
}
int sticks(Container* c) {
    int n = 0;
    if (!c) return -1;
    for (int i = 0; i < c->getContainerSize(); ++i)
        if (game::items::idOf(c->getItem(i)) == "minecraft:stick") n += game::items::countOf(c->getItem(i));
    return n;
}
} // namespace

GF_REGISTER_SELFTEST("trees.sorting_core_moves_item", [](Context& ctx) -> ll::coro::CoroTask<> {
    RangeGuard guard;
    gf::config::mut<TreesConfig>().sortingCoreRange = 4;
    GlobalPos core = ctx.testPos(386, 2);
    GlobalPos in = core.offset(1, 0, 0), out = core.offset(4, 0, 0);
    ctx.require(ctx.setBlock(core, "twilightforest:sorting_log_core", {{"twilightforest:axis", "y"}}), "a sortingwood engine");
    ctx.require(ctx.setBlock(in, "minecraft:chest"), "an input chest");
    ctx.require(ctx.setBlock(out, "minecraft:chest"), "an output chest");
    co_await ctx.ticks(2);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    auto* ci = chestAt(*region, bp(in));
    auto* co = chestAt(*region, bp(out));
    ctx.require(ci && co, "both chests have containers");
    if (auto s = game::items::make("minecraft:stick", 3)) ci->setItem(0, *s);
    if (auto s = game::items::make("minecraft:stick", 1)) co->setItem(5, *s);
    int sorted = performCoreEffect(Core::Sorting, *region, bp(core));
    ctx.check(sorted == 1, "one item sorted per input and cycle (" + std::to_string(sorted) + ")");
    ctx.check(sticks(chestAt(*region, bp(in))) == 2 && sticks(chestAt(*region, bp(out))) == 2, "a stick moved into the chest holding sticks");
    if (auto* c = chestAt(*region, bp(in))) c->removeAllItems();
    if (auto* c = chestAt(*region, bp(out))) c->removeAllItems();
});

GF_REGISTER_SELFTEST("trees.snow_under_trees", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto      box  = co_await ctx.flatArea(390, 16, 12);
    GlobalPos leaf = game::globalPos(ctx.dimension(), BlockPos{box.minX + 5, box.minY + 5, box.minZ + 5});
    ctx.require(ctx.setBlock(leaf, "minecraft:spruce_leaves", {{"persistent_bit", true}}), "leaves above the ground");
    BlockPos origin{box.minX, box.minY, box.minZ};
    ctx.require(placeSeeded(ctx, "twilightforest:snow_under_trees", origin, 1), "snow_under_trees placed");
    ctx.check(ctx.blockAt(game::globalPos(ctx.dimension(), BlockPos{box.minX + 5, box.minY, box.minZ + 5})) == "minecraft:snow_layer", "snow under the leaves");
    ctx.check(ctx.blockAt(game::globalPos(ctx.dimension(), BlockPos{box.minX + 7, box.minY, box.minZ + 5})) == "minecraft:air", "no snow in the open");
});

GF_REGISTER_SELFTEST("trees.mushroom_cap_faces", [](Context& ctx) -> ll::coro::CoroTask<> {
    // the canopy mushrooms' caps carry the cap skin on top (Bedrock huge_mushroom_bits 1..9, 14)
    auto     box = co_await ctx.flatArea(325, 40, 40);
    BlockPos pos{box.minX + 20, box.minY, box.minZ + 20};
    for (char const* id : {"twilightforest:mushroom/brown_canopy_mushroom", "twilightforest:mushroom/canopy_red_flat_mushroom"}) {
        ctx.require(placeSeeded(ctx, id, pos, 99), std::string(id) + " placed");
        auto*                region = ctx.region();
        std::map<int, int>   bits;
        int                  pores = 0, caps = 0;
        std::string          capId;
        for (int x = box.minX; x <= box.maxX; ++x)
            for (int z = box.minZ; z <= box.maxZ; ++z)
                for (int y = box.maxY; y >= box.minY; --y) {
                    auto const& b = region->getBlock(BlockPos{x, y, z});
                    if (b.isAir()) continue;
                    auto const& n = b.getTypeName();
                    if (n == "minecraft:brown_mushroom_block" || n == "minecraft:red_mushroom_block") {
                        int v = game::intState(b, "huge_mushroom_bits").value_or(-1);
                        ++bits[v];
                        ++caps;
                        capId = n;
                        if (v == 0) ++pores;
                    }
                    break; // the topmost block of the column
                }
        std::string hist;
        for (auto [k, v] : bits) hist += " " + std::to_string(k) + ":" + std::to_string(v);
        ctx.info(std::string(id) + " top cap blocks (" + capId + ") huge_mushroom_bits" + hist);
        ctx.check(caps > 0 && pores == 0, std::string(id) + ": no cap shows pores on top");
        if (auto const* air = game::blockWith("minecraft:air"))
            for (int x = box.minX; x <= box.maxX; ++x)
                for (int z = box.minZ; z <= box.maxZ; ++z)
                    for (int y = box.minY; y <= box.maxY; ++y) game::setBlock(*region, BlockPos{x, y, z}, *air);
    }
});

GF_REGISTER_SELFTEST("trees.leaf_decay", [](Context& ctx) -> ll::coro::CoroTask<> {
    // LeavesBlock: leaves without a log within 6 (distance 7) and not persistent decay on a random
    // tick; a log next to them (distance 1) or persistence keeps them
    GlobalPos lone = ctx.testPos(392, 0), kept = ctx.testPos(392, 2), log = ctx.testPos(393, 2), placed = ctx.testPos(395, 4);
    co_await ctx.ensureLoaded(lone);
    ctx.require(ctx.setBlock(lone, "twilightforest:twilight_oak_leaves", {{"twilightforest:distance", 7}, {"twilightforest:persistent", false}}), "lone leaves");
    ctx.require(ctx.setBlock(log, "twilightforest:twilight_oak_log", {{"twilightforest:axis", "y"}}), "a log");
    ctx.require(ctx.setBlock(kept, "twilightforest:canopy_leaves", {{"twilightforest:distance", 7}, {"twilightforest:persistent", false}}), "leaves by a log");
    ctx.require(ctx.setBlock(placed, "twilightforest:dark_leaves", {{"twilightforest:distance", 7}, {"twilightforest:persistent", true}}), "persistent leaves");
    co_await ctx.ticks(4); // LeavesBlock#updateShape schedules the distance update
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    ctx.check(game::intState(region->getBlock(bp(kept)), "twilightforest:distance").value_or(0) == 1, "the leaves by the log have distance 1");
    for (auto const& p : {lone, kept, placed}) gf::feature::families::detail::randomTickAt(*region, bp(p));
    co_await ctx.ticks(1);
    ctx.check(ctx.blockAt(lone) == "minecraft:air", "lone leaves decayed (" + ctx.blockAt(lone) + ")");
    ctx.check(ctx.blockAt(kept) == "twilightforest:canopy_leaves", "leaves by a log stay");
    ctx.check(ctx.blockAt(placed) == "twilightforest:dark_leaves", "persistent dark leaves stay");
    // the log goes: the leaves' distance becomes 7 and they decay
    if (auto const* air = game::blockWith("minecraft:air")) game::setBlock(*region, bp(log), *air);
    co_await ctx.ticks(4);
    region = ctx.region();
    ctx.require(region != nullptr, "a region");
    ctx.check(game::intState(region->getBlock(bp(kept)), "twilightforest:distance").value_or(0) == 7, "without the log the distance is 7");
    gf::feature::families::detail::randomTickAt(*region, bp(kept));
    co_await ctx.ticks(1);
    ctx.check(ctx.blockAt(kept) == "minecraft:air", "the leaves decayed once the log was gone (" + ctx.blockAt(kept) + ")");
});

namespace {
struct LootCount {
    std::map<std::string, int> items;
    int                        rolls = 0;
};
LootCount rollMany(Context& ctx, std::string const& table, char const* blockId, char const* toolId, int n) {
    LootCount out;
    auto*     region = ctx.region();
    auto      tool   = toolId ? game::items::make(toolId, 1) : std::optional<ItemStack>{};
    auto*     block  = game::blockWith(blockId);
    for (int i = 0; i < n; ++i) {
        gf::loot::LootContext lc;
        lc.region = region;
        lc.origin = Vec3{0.5f, 64.5f, 0.5f};
        lc.tool   = tool ? &*tool : nullptr;
        lc.block  = block;
        lc.seed   = 0; // a random seed per roll (consecutive LCG seeds draw correlated first floats)
        for (auto const& s : gf::loot::roll(table, lc)) out.items[game::items::idOf(s)] += game::items::countOf(s);
        ++out.rolls;
    }
    return out;
}
} // namespace

GF_REGISTER_SELFTEST("trees.leaf_loot", [](Context& ctx) -> ll::coro::CoroTask<> {
    // shears -> the leaves; otherwise the sapling (fortune table, 1/20 for twilight oak, 1/40 for
    // dark leaves) and 1-2 sticks with 1/50; the magic leaves drop no sapling; hardened dark leaves
    // drop dark leaves under shears
    auto sheared = rollMany(ctx, "twilightforest:blocks/twilight_oak_leaves", "twilightforest:twilight_oak_leaves", "minecraft:shears", 20);
    ctx.check(sheared.items["twilightforest:twilight_oak_leaves"] == 20 && sheared.items.size() == 1, "shears drop the leaves themselves");
    auto plain = rollMany(ctx, "twilightforest:blocks/twilight_oak_leaves", "twilightforest:twilight_oak_leaves", nullptr, 4000);
    int  saplings = plain.items["twilightforest:twilight_oak_sapling"], sticks = plain.items["minecraft:stick"];
    ctx.info("twilight oak leaves x4000: " + std::to_string(saplings) + " saplings, " + std::to_string(sticks) + " sticks");
    ctx.check(saplings >= 140 && saplings <= 260, "about 1/20 drop a sapling (" + std::to_string(saplings) + " of 4000)");
    ctx.check(sticks >= 70 && sticks <= 190, "about 1/50 drop 1-2 sticks (" + std::to_string(sticks) + " sticks)");
    ctx.check(!plain.items.contains("twilightforest:twilight_oak_leaves"), "no leaves without shears");
    auto dark = rollMany(ctx, "twilightforest:blocks/dark_leaves", "twilightforest:dark_leaves", nullptr, 4000);
    int  darkSaplings = dark.items["twilightforest:darkwood_sapling"];
    ctx.check(darkSaplings >= 60 && darkSaplings <= 150, "about 1/40 of dark leaves drop a darkwood sapling (" + std::to_string(darkSaplings) + ")");
    auto hardened = rollMany(ctx, "twilightforest:blocks/hardened_dark_leaves", "twilightforest:hardened_dark_leaves", "minecraft:shears", 10);
    ctx.check(hardened.items["twilightforest:dark_leaves"] == 10, "hardened dark leaves drop dark leaves under shears");
    auto time = rollMany(ctx, "twilightforest:blocks/time_leaves", "twilightforest:time_leaves", nullptr, 2000);
    ctx.check(!time.items.contains("twilightforest:time_sapling"), "leaves of time drop no sapling");
    ctx.check(time.items["minecraft:stick"] > 0, "leaves of time drop sticks");
    co_return;
});

GF_REGISTER_SELFTEST("trees.leaf_tool_speeds", [](Context& ctx) -> ll::coro::CoroTask<> {
    // Java's per-tick progress speed / hardness / 30 (no tool is required): shears 15 and swords 1.5
    // on #leaves (#sword_efficient), hoes their tier's speed on #mineable/hoe, anything else 1;
    // hardened dark leaves are in none of these tags. Bedrock gets the times from the leaves'
    // item_specific_speeds (tools/import/overrides/trees.json).
    struct Leaf {
        char const* id;
        double      hardness;
        bool        tagged;
    };
    Leaf const leaves[] = {
        {"twilightforest:twilight_oak_leaves", 0.2, true}, {"twilightforest:canopy_leaves", 0.2, true},      {"twilightforest:mangrove_leaves", 0.2, true},
        {"twilightforest:rainbow_oak_leaves", 0.2, true},  {"twilightforest:time_leaves", 0.2, true},        {"twilightforest:transformation_leaves", 0.2, true},
        {"twilightforest:mining_leaves", 0.2, true},       {"twilightforest:sorting_leaves", 0.2, true},     {"twilightforest:dark_leaves", 2.0, true},
        {"twilightforest:hardened_dark_leaves", 2.0, false},
    };
    struct Tool {
        char const* id;
        double      speed; ///< on the tagged leaves
    };
    Tool const tools[] = {
        {nullptr, 1.0},                     {"minecraft:shears", 15.0},       {"minecraft:wooden_sword", 1.5}, {"minecraft:stone_sword", 1.5},
        {"minecraft:iron_sword", 1.5},      {"minecraft:golden_sword", 1.5},  {"minecraft:diamond_sword", 1.5}, {"minecraft:netherite_sword", 1.5},
        {"minecraft:wooden_hoe", 2.0},      {"minecraft:stone_hoe", 4.0},     {"minecraft:iron_hoe", 6.0},      {"minecraft:golden_hoe", 12.0},
        {"minecraft:diamond_hoe", 8.0},     {"minecraft:netherite_hoe", 9.0}, {"minecraft:iron_pickaxe", 1.0},  {"minecraft:iron_axe", 1.0},
    };
    // on the ground (Player#getDestroySpeed divides by 5 in the air)
    GlobalPos floor = ctx.testPos(397, 2);
    co_await ctx.ensureLoaded(floor);
    ctx.require(ctx.setBlock(floor, "minecraft:stone"), "a floor");
    ctx.cleanupBlock(floor.offset(0, 1, 0));
    ctx.cleanupBlock(floor.offset(0, 2, 0));
    sim(ctx).teleport(Vec3{floor.x + 0.5f, floor.y + 1.0f, floor.z + 0.5f}, sim(ctx).getDimensionId());
    co_await ctx.ticks(10);
    ctx.info(std::string("the player stands on the floor: ") + (sim(ctx).isOnGround() ? "yes" : "no"));
    std::vector<std::string> wrong;
    for (auto const& t : tools) {
        if (t.id) {
            if (auto item = game::items::make(t.id, 1)) sim(ctx).simulateSetItem(*item, true, 0);
        } else sim(ctx).simulateSetItem(ItemStack::EMPTY_ITEM(), true, 0);
        co_await ctx.ticks(2);
        for (auto const& l : leaves) {
            auto* block = game::blockWith(l.id);
            ctx.require(block != nullptr, std::string(l.id) + " exists");
            double want = (l.tagged ? t.speed : 1.0) / l.hardness / 30.0;
            double got  = sim(ctx).getDestroyProgress(*block);
            bool   same = (got >= 1.0 && want >= 1.0) || std::abs(got - want) <= want * 0.05;
            gf::log().info("trees speed {} {} {:.6f} {:.6f}", l.id, t.id ? t.id : "hand", got, want);
            if (!same) wrong.push_back(std::string(l.id) + " with " + (t.id ? t.id : "a hand") + ": " + std::to_string(got) + " per tick (Java " + std::to_string(want) + ")");
        }
    }
    ctx.check(wrong.empty(), "every leaf mines at Java's speed; off: " + [&] {
        std::string s;
        for (auto const& w : wrong) s += (s.empty() ? "" : "; ") + w;
        return s;
    }());
});

GF_REGISTER_SELFTEST("trees.dark_canopy_free_height", [](Context& ctx) -> ll::coro::CoroTask<> {
    // DarkCanopyTreeFeature#getMaxFreeTreeHeight uses validTreePos (a log is not free): a log in the
    // trunk's feature-size box stops the dark forest oak, where TreeFeature (logs free) grows it
    auto     box = co_await ctx.flatArea(396, 16, 24);
    BlockPos pos{box.minX + 8, box.minY, box.minZ + 8};
    GlobalPos log = game::globalPos(ctx.dimension(), BlockPos{pos.x + 1, pos.y + 2, pos.z + 1});
    ctx.require(ctx.setBlock(log, "minecraft:oak_log"), "a log in the way");
    ctx.check(!placeSeeded(ctx, tree("dark_forest_oak_tree"), pos, 5), "the dark forest oak does not grow into the log");
    ctx.check(ctx.blockAt(game::globalPos(ctx.dimension(), pos)) == "minecraft:air", "no trunk was placed");
    ctx.cleanupBlock(log);
    if (auto* region = ctx.region())
        if (auto const* air = game::blockWith("minecraft:air")) game::setBlock(*region, bp(log), *air);
    ctx.check(placeSeeded(ctx, tree("dark_forest_oak_tree"), pos, 5), "without the log it grows");
});

GF_REGISTER_SELFTEST("trees.hardened_dark_leaves", [](Context& ctx) -> ll::coro::CoroTask<> {
    GlobalPos leaves = ctx.testPos(394, 0), dirt = ctx.testPos(394, 3);
    co_await ctx.ensureLoaded(leaves);
    ctx.require(ctx.setBlock(leaves, "twilightforest:hardened_dark_leaves"), "hardened dark leaves");
    ctx.require(ctx.setBlock(dirt, "minecraft:dirt"), "dirt");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    // TFBlocks.noSpawning: no spawn list above them
    for (char const* category : {"monster", "creature", "ambient"}) {
        auto list = gf::spawn::detail::spawnListAt(*region, bp(leaves.offset(0, 1, 0)), category, "minecraft:forest");
        ctx.check(list.empty(), std::string("no ") + category + " spawns on hardened dark leaves");
    }
    auto onDirt = gf::spawn::detail::spawnListAt(*region, bp(dirt.offset(0, 1, 0)), "monster", "minecraft:forest");
    ctx.info("monster spawn entries on dirt: " + std::to_string(onDirt.size()));
    // TF's LeavesBlock leaves keep the default isValidSpawn (isFaceSturdy(UP)) on LeavesBlock's empty
    // support shape: nothing spawns on them either; dark leaves (DarkLeavesBlock: a full support shape)
    // take the biome's list
    for (char const* id : {"twilight_oak_leaves", "canopy_leaves", "mangrove_leaves", "rainbow_oak_leaves", "time_leaves", "transformation_leaves",
                           "mining_leaves", "sorting_leaves", "dark_leaves"}) {
        std::string full = std::string("twilightforest:") + id;
        ctx.require(ctx.setBlock(leaves, full, {{"twilightforest:persistent", true}}), full);
        auto list = gf::spawn::detail::spawnListAt(*region, bp(leaves.offset(0, 1, 0)), "monster", "minecraft:forest");
        if (full == "twilightforest:dark_leaves") ctx.check(list.size() == onDirt.size(), "dark leaves take the biome's monster spawns");
        else ctx.check(list.empty(), "no monster spawns on " + full);
    }
    // getLightBlock: DarkLeavesBlock 15 (the dark forest's canopy shuts the sky out), LeavesBlock 1,
    // HardenedDarkLeavesBlock a full opaque block (15)
    for (auto [id, want] : {std::pair{"twilightforest:dark_leaves", 15}, std::pair{"twilightforest:hardened_dark_leaves", 15},
                            std::pair{"twilightforest:twilight_oak_leaves", 1}, std::pair{"twilightforest:canopy_leaves", 1}}) {
        auto const* b = game::blockWith(id);
        ctx.require(b != nullptr, id);
        int light = static_cast<int>(b->getBlockType().getLight(*b).mValue);
        int direct = static_cast<int>(b->mDirectData->mLight->mValue);
        ctx.check(direct == want, std::string(id) + " dampens light by " + std::to_string(direct) + " (TF getLightBlock " + std::to_string(want) + "; type " +
                                      std::to_string(light) + ")");
    }
    ctx.require(ctx.setBlock(leaves, "twilightforest:hardened_dark_leaves"), "hardened dark leaves again");
    // HardenedDarkLeavesBlock#getCloneItemStack: pick block gives dark leaves
    ctx.check(gf::trees_ext::pickItem("twilightforest:hardened_dark_leaves") == "twilightforest:dark_leaves", "pick block gives dark leaves");
    auto& sp = sim(ctx);
    auto& inv = sp.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) inv.setItem(i, ItemStack::EMPTY_ITEM());
    if (auto item = game::items::make("twilightforest:dark_leaves", 5)) inv.setItem(20, *item);
    sp.setSelectedSlot(2);
    ctx.check(gf::trees_ext::pickBlock(sp, bp(leaves)), "the pick is handled");
    auto const& held = sim(ctx).getSelectedItem();
    ctx.check(game::items::idOf(held) == "twilightforest:dark_leaves" && game::items::countOf(held) == 5, "the dark leaves moved into the hand (" + game::items::idOf(held) + ")");
    // Inventory#setPickedItem in creative with a full hotbar: getSuitableHotbarSlot skips the enchanted
    // stacks (from the selected slot on), and the stack there moves to the first free slot
    {
        auto& p  = sim(ctx);
        auto& in = p.getInventory();
        for (int i = 0; i < in.getContainerSize(); ++i) in.setItem(i, ItemStack::EMPTY_ITEM());
        for (int i = 0; i < 9; ++i)
            if (auto st = game::items::make(i == 4 ? "minecraft:dirt" : "minecraft:stick", 1)) {
                if (i != 4) gf::enchant::set(*st, "minecraft:unbreaking", 1);
                in.setItem(i, *st);
            }
        auto mode = p.getPlayerGameType();
        p.setPlayerGameType(GameType::Creative);
        p.setSelectedSlot(2);
        ctx.check(gf::trees_ext::pickBlock(p, bp(leaves)), "the creative pick is handled");
        auto& q = sim(ctx);
        ctx.check(q.getSelectedItemSlot() == 4, "the first hotbar slot without enchantments from the selected one is used (" + std::to_string(q.getSelectedItemSlot()) + ")");
        ctx.check(game::items::idOf(q.getInventory().getItem(4)) == "twilightforest:dark_leaves", "the picked dark leaves are there");
        ctx.check(game::items::idOf(q.getInventory().getItem(9)) == "minecraft:dirt", "the displaced dirt moved to the first free slot (" + game::items::idOf(q.getInventory().getItem(9)) + ")");
        q.setPlayerGameType(mode);
    }
    for (int i = 0; i < sim(ctx).getInventory().getContainerSize(); ++i) sim(ctx).getInventory().setItem(i, ItemStack::EMPTY_ITEM());
});

GF_REGISTER_SELFTEST("trees.mangrove_sapling_water", [](Context& ctx) -> ll::coro::CoroTask<> {
    // MangroveSaplingBlock#getStateForPlacement: waterlogged only in full water (FluidTags.WATER with
    // amount 8: a source), not in flowing water (amount < 8) nor in air. Placed by the player on dirt.
    auto box = co_await ctx.flatArea(387, 9, 8);
    int  y   = box.minY;
    auto pos = [&](int dx, int dy, int dz) { return game::globalPos(ctx.dimension(), BlockPos{box.minX + dx, y + dy, box.minZ + dz}); };
    // three cells along x, each sapling cell on dirt; a stone basin around the source cell and around
    // the two-cell channel (a source feeding the flowing water the second sapling goes into)
    for (int dx = 0; dx <= 8; ++dx)
        for (int dz = 0; dz <= 4; ++dz) ctx.require(ctx.setBlock(pos(dx, 0, dz), "minecraft:stone"), "the floor");
    for (int dx = 0; dx <= 8; ++dx)
        for (int dz = 0; dz <= 4; ++dz)
            if (dz == 0 || dz == 4 || dx == 0 || dx == 2 || dx == 6 || dx == 8 || (dx > 2 && dx < 6 && dz != 2)) ctx.require(ctx.setBlock(pos(dx, 1, dz), "minecraft:stone"), "the basin");
    GlobalPos source = pos(1, 1, 2), flowing = pos(3, 1, 2), feed = pos(4, 1, 2), dry = pos(7, 1, 2);
    ctx.require(ctx.setBlock(pos(1, 0, 2), "minecraft:dirt") && ctx.setBlock(pos(3, 0, 2), "minecraft:dirt") && ctx.setBlock(pos(7, 0, 2), "minecraft:dirt"), "dirt under the saplings");
    ctx.require(ctx.setBlock(pos(5, 1, 2), "minecraft:stone"), "the channel's end");
    ctx.require(ctx.setBlock(source, "minecraft:water"), "a water source");
    // Bedrock's minecraft:water is the static liquid: placed by setBlock with no neighbour changing
    // afterwards it never ticks and never spreads. The channel's source is the dynamic one (a source:
    // liquid_depth 0), whose placement schedules its liquid tick, so it flows into the channel.
    ctx.require(ctx.setBlock(feed, "minecraft:flowing_water", {{"liquid_depth", 0}}), "the channel's source");
    auto* region = ctx.region();
    auto  depth  = [&](GlobalPos const& p) { return game::intState(region->getBlock(bp(p)), "liquid_depth").value_or(-1); };
    auto  flows  = [&] { return ctx.blockAt(flowing) == "minecraft:flowing_water" || (ctx.blockAt(flowing) == "minecraft:water" && depth(flowing) > 0); };
    for (int i = 0; i < 100; ++i) {
        co_await ctx.ticks(1);
        region = ctx.region();
        ctx.require(region != nullptr, "a region");
        if (i >= 20 && flows()) break;
    }
    ctx.require(ctx.blockAt(flowing) == "minecraft:flowing_water" || (ctx.blockAt(flowing) == "minecraft:water" && depth(flowing) > 0),
                "flowing water in the channel (" + ctx.blockAt(flowing) + " depth " + std::to_string(depth(flowing)) + ")");
    // The placed state is read right after each placement (click: 2 ticks), before the next liquid
    // tick (water: 5 ticks). Later, Bedrock's flowing water spreads into the liquid layer of every
    // block with minecraft:liquid_detection can_contain_liquid (Java's SimpleWaterloggedBlock only
    // takes a source: canPlaceLiquid), which is the waterloggable blocks' behaviour, not the
    // sapling's placement this test is about.
    auto wetNow = [&](GlobalPos const& p) -> std::string {
        auto* r = ctx.region();
        if (!r) return "no region";
        auto const& extra = r->getExtraBlock(bp(p));
        auto const& id    = extra.getTypeName();
        if (id != "minecraft:water" && id != "minecraft:flowing_water") return "";
        return id + " depth " + std::to_string(game::intState(extra, "liquid_depth").value_or(-1));
    };
    std::map<int, std::string> placedWet; // by x
    for (GlobalPos const& p : {source, flowing, dry}) {
        co_await click(ctx, p.offset(0, -1, 0), "twilightforest:mangrove_sapling");
        placedWet[p.x] = wetNow(p);
    }
    for (GlobalPos const& p : {source, flowing, dry}) ctx.check(ctx.blockAt(p) == "twilightforest:mangrove_sapling", "a mangrove sapling was placed (" + ctx.blockAt(p) + ")");
    ctx.check(!placedWet[source.x].empty(), "the sapling placed into a water source is waterlogged");
    ctx.check(placedWet[flowing.x].empty(), "the sapling placed into flowing water is not (" + placedWet[flowing.x] + ")");
    ctx.check(placedWet[dry.x].empty(), "the sapling placed in air is not (" + placedWet[dry.x] + ")");
    ctx.check(!wetNow(source).empty(), "the waterlogged sapling keeps its water");
    for (GlobalPos const& p : {source, flowing, dry, feed}) ctx.cleanupBlock(p);
});


namespace {

/// Bumped whenever the showcase log is put back (the probe's own timed restore then has nothing to do).
std::uint64_t gRestoreGeneration = 0;

/// Sends the chunks holding `positions` again to the real clients that view them. A Bedrock client
/// lights its chunks itself; after the showcase's bulk edits (whole columns cleared, trees placed and
/// taken down) it kept stale skylight, large dark rectangles on the grass, until it got the chunk again.
void resendChunksOf(std::vector<std::tuple<int, int, int, int>> const& positions) {
    std::set<std::tuple<int, int, int>> chunks;
    for (auto const& [dim, x, y, z] : positions) chunks.emplace(dim, x >> 4, z >> 4);
    int sent = 0;
    for (auto const& [dim, cx, cz] : chunks)
        if (auto* r = game::regionOf(dim); r && r->hasChunksAt(BlockPos{cx * 16, 0, cz * 16}, 0, false)) sent += resendChunkToViewers(*r, ChunkPos{cx, cz});
    if (sent > 0) gf::log().info("trees: {} edited chunks resent ({} sends) so the clients light them again", chunks.size(), sent);
}

/// Puts back every block of the showcase log, newest first (positions in unloaded chunks are skipped
/// when `loadedOnly`); returns how many.
int restoreShowcase(bool loadedOnly) {
    auto& log  = showcaseLog();
    log.active = false;
    int restored = 0;
    auto undo = std::move(log.undo);
    log.undo.clear();
    for (auto it = undo.rbegin(); it != undo.rend(); ++it) (*it)();
    std::vector<std::tuple<int, int, int, int>> kept, done; // kept: not loaded, tried again later
    for (auto it = log.order.rbegin(); it != log.order.rend(); ++it) {
        auto [dim, x, y, z] = *it;
        auto*    region     = game::regionOf(dim);
        BlockPos p{x, y, z};
        if (!region || (loadedOnly && !region->hasChunksAt(p, 0, false))) {
            kept.push_back(*it);
            continue;
        }
        game::setBlock(*region, p, *log.originals[*it]);
        done.push_back(*it);
        ++restored;
    }
    resendChunksOf(done);
    std::map<std::tuple<int, int, int, int>, Block const*> keptOriginals;
    for (auto const& k : kept) keptOriginals.emplace(k, log.originals[k]);
    log.originals = std::move(keptOriginals);
    log.order.assign(kept.rbegin(), kept.rend());
    ++gRestoreGeneration;
    return restored;
}

/// The running level (nullptr: none), to tell the worlds apart.
Level* currentLevel() {
    auto level = ll::service::getLevel();
    return level ? &*level : nullptr;
}

/// How long the probe stands before it puts itself back (the client scenario photographs it well
/// within this; trees.zz_showcase_restore takes it down earlier).
constexpr int kProbeTicks = 2400;

} // namespace

/// The tag the client e2e scenario gives its real player (`/tag @s add gf_trees_probe`) to arm
/// trees.vanilla_probe: a selftest glob or the server 'full' run never builds the probe, even with a
/// real player online.
constexpr char kProbeTag[] = "gf_trees_probe";

/// Whether a real client (not a simulated player) with the probe tag is in the world.
static bool probeArmed() {
    auto level = ll::service::getLevel();
    if (!level) return false;
    bool found = false;
    level->forEachPlayer([&](Player& p) {
        if (!p.isSimulatedPlayer() && p.hasTag(kProbeTag)) found = true;
        return !found;
    });
    return found;
}

// Client e2e (remote modes: run on the host): a probe the client photographs, the leaves of
// transformation shedding runes over a timewood clock ticking wheat. It stands for a real client
// only: in a world with no real player (the server 'full' run, a selftest glob on a bare server) it
// changes nothing. It stands away from every unit's test rows (250 blocks east and 64 north of this
// unit's row). The clock is placed inactive, so the world never holds an active core outside U3's
// rows: the probe's own coroutine runs its cycle every 20 ticks (SpecialMagicLogBlock's rate) and puts
// every block back after kProbeTicks, or at once with trees.zz_showcase_restore.
GF_REGISTER_SELFTEST("trees.vanilla_probe", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!probeArmed()) {
        ctx.info(std::string("skipped: the probe stands only for a real client to photograph (client e2e: /tag @s add ") + kProbeTag +
                 " first); no real player with the tag is in this world");
        co_return;
    }
    showcaseLog().active = true;
    GlobalPos base = ctx.testPos(398, 2).offset(250, 0, -64);
    co_await ctx.ensureLoaded(base);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    auto set = [&](GlobalPos const& p, char const* id, nlohmann::json const& states) {
        if (auto const* b = game::blockWithStates(id, states)) showcaseSet(*region, bp(p), *b);
    };
    // a grass floor under the probe (whatever the flat world has there)
    for (int dx = -2; dx <= 2; ++dx)
        for (int dz = -2; dz <= 2; ++dz) set(base.offset(dx, -1, dz), "minecraft:grass_block", nlohmann::json::object());
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz) set(base.offset(dx, 4, dz), "twilightforest:transformation_leaves", {{"twilightforest:persistent", true}});
    set(base.offset(-1, 0, 0), "minecraft:farmland", {{"moisturized_amount", 7}});
    set(base.offset(-1, 1, 0), "minecraft:wheat", {{"growth", 0}});
    set(base, "twilightforest:time_log_core", {{"twilightforest:axis", "y"}, {"twilightforest:active", false}});
    showcaseLog().active = false;
    resendChunksOf(showcaseLog().order);
    ll::coro::keepThis([generation = gRestoreGeneration, level = currentLevel(), core = base]() -> ll::coro::CoroTask<> {
        // the clock's cycles, while the probe stands (the range is the config's, at most 128)
        for (int t = 0; t < kProbeTicks; t += 20) {
            co_await ll::chrono::ticks(20);
            if (currentLevel() != level || generation != gRestoreGeneration) co_return; // another world, or restored
            auto* r = game::regionOf(core.dim);
            if (!r || !r->hasChunksAt(bp(core), 0, false) || r->getBlock(bp(core)).getTypeName() != "twilightforest:time_log_core") continue;
            (void)performCoreEffect(Core::Time, *r, bp(core));
        }
        // the blocks in unloaded chunks stay in the log and are put back on a later try, once a
        // player or a ticking area has their chunks loaded again
        for (std::uint64_t gen = generation; gen == gRestoreGeneration && level && currentLevel() == level;) {
            int n = restoreShowcase(true);
            gen   = gRestoreGeneration;
            gf::log().info("trees: the probe put itself back ({} blocks, {} in unloaded chunks left for later)", n, showcaseLog().order.size());
            if (showcaseLog().order.empty()) co_return;
            co_await ll::chrono::ticks(600);
        }
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
    ctx.info("trees probe at " + std::to_string(base.x) + " " + std::to_string(base.y) + " " + std::to_string(base.z));
});
// Client e2e: puts back every block trees.showcase / trees.vanilla_probe changed (newest first), after
// the screenshots.
GF_REGISTER_SELFTEST("trees.zz_showcase_restore", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto order = showcaseLog().order;
    for (auto const& [dim, x, y, z] : order) co_await ctx.ensureLoaded(GlobalPos{dim, x, y, z});
    int restored = restoreShowcase(false);
    ctx.info("trees showcase restored " + std::to_string(restored) + " blocks");
    if (auto level = ll::service::getLevel()) level->forEachPlayer([](Player& p) {
            if (!p.isSimulatedPlayer()) p.removeTag(kProbeTag);
            return true;
        });
});
} // namespace gf::feature::trees

#ifdef GF_CLIENT
namespace gf::feature::trees {

namespace {
std::vector<std::string> const& showcaseTrees() {
    static std::vector<std::string> const ids{
        tree("twilight_oak_tree"), tree("large_twilight_oak_tree"), tree("swampy_oak_tree"), tree("canopy_tree"), tree("firefly_canopy_tree"),
        tree("dead_canopy_tree"), tree("mega_canopy_tree"), tree("forest_mega_oak_tree"), tree("savannah_mega_oak_tree"), tree("mangrove_tree"),
        tree("darkwood_tree"), tree("homegrown_darkwood_tree"), tree("darkwood_lantern_tree"), tree("dark_forest_oak_tree"), tree("dark_forest_birch_tree"),
        tree("dark_oak_bush"), tree("oak_bush"), tree("rainbow_oak"), tree("large_rainbow_oak"), tree("time_tree"), tree("transformation_tree"),
        tree("mining_tree"), tree("sorting_tree"), tree("snowy_spruce_tree"), tree("mega_spruce_tree"), tree("large_winter_tree"),
        tree("smaller_jungle_tree"), "twilightforest:mushroom/brown_canopy_mushroom", "twilightforest:mushroom/canopy_red_vanilla_mushroom",
        "twilightforest:mushroom/canopy_red_smooth_mushroom", "twilightforest:mushroom/canopy_red_spheroid_mushroom",
        "twilightforest:mushroom/canopy_red_flat_mushroom",
    };
    return ids;
}
} // namespace

// Client e2e only (never in a server 'full' run): every tree, mushroom and block of the unit, left
// standing for the screenshots of tools/e2e/client/scenarios/trees.ps1 (40 blocks apart along x,
// 250 blocks east of the test rows; the blocks in a row 40 blocks north of the trees).
GF_REGISTER_SELFTEST("trees.showcase", [](Context& ctx) -> ll::coro::CoroTask<> {
    showcaseLog().active = true; // every change is undone by trees.zz_showcase_restore
    GlobalPos base   = ctx.testPos(399).offset(250, 0, 0);
    auto*     region = ctx.region();
    ctx.require(region != nullptr, "a region");
    auto const& ids = showcaseTrees();
    for (std::size_t i = 0; i < ids.size(); ++i) {
        GlobalPos p = base.offset(static_cast<int>(i) * 40, 0, 0);
        co_await ctx.ensureLoaded(p);
        region = ctx.region();
        ctx.require(region != nullptr, "a region");
        // a column of grass on dirt and air above, whatever the flat world has
        for (int dx = -20; dx < 20; ++dx)
            for (int dz = -20; dz < 20; ++dz) {
                BlockPos b{p.x + dx, p.y - 1, p.z + dz};
                if (auto const* g = game::blockWith("minecraft:grass_block"); g && &region->getBlock(b) != g) showcaseSet(*region, b, *g);
                for (int dy = 0; dy < 50; ++dy) {
                    BlockPos up{b.x, p.y + dy, b.z};
                    if (auto const* a = game::blockWith("minecraft:air"); a && !region->getBlock(up).isAir()) showcaseSet(*region, up, *a);
                }
            }
        BlockPos at = bp(p);
        if (ids[i].ends_with("time_tree")) at = pedestal(*region, at);
        bool ok = placeSeeded(ctx, ids[i], at, 4242 + static_cast<std::int64_t>(i));
        ctx.check(ok, ids[i] + " placed");
        ctx.info("trees showcase " + std::to_string(i) + " " + ids[i] + " at " + std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z));
    }
    // the blocks: leaves, saplings, pots, cores (two of them active), the mangrove root
    char const* blocks[] = {"twilight_oak_leaves", "canopy_leaves", "mangrove_leaves", "dark_leaves", "hardened_dark_leaves", "rainbow_oak_leaves",
                            "time_leaves", "transformation_leaves", "mining_leaves", "sorting_leaves", "twilight_oak_sapling", "canopy_sapling",
                            "mangrove_sapling", "darkwood_sapling", "hollow_oak_sapling", "time_sapling", "transformation_sapling", "mining_sapling",
                            "sorting_sapling", "rainbow_oak_sapling", "potted_twilight_oak_sapling", "potted_canopy_sapling", "potted_mangrove_sapling",
                            "potted_darkwood_sapling", "potted_hollow_oak_sapling", "potted_time_sapling", "potted_transformation_sapling",
                            "potted_mining_sapling", "potted_sorting_sapling", "potted_rainbow_oak_sapling", "time_log_core", "transformation_log_core",
                            "mining_log_core", "sorting_log_core", "mangrove_root"};
    GlobalPos row = base.offset(0, 0, -40);
    co_await ctx.ensureLoaded(row);
    region = ctx.region();
    ctx.require(region != nullptr, "a region");
    for (int i = 0; i < 35; ++i) {
        GlobalPos p  = row.offset(i * 2, 0, 0);
        std::string id = std::string("twilightforest:") + blocks[i];
        if (auto const* g = game::blockWith("minecraft:grass_block")) showcaseSet(*region, bp(p.offset(0, -1, 0)), *g);
        nlohmann::json states = nlohmann::json::object();
        if (id.ends_with("_log_core")) states = {{"twilightforest:axis", "y"}, {"twilightforest:active", i % 2 == 0}};
        if (id.ends_with("_leaves") && id != "twilightforest:hardened_dark_leaves") states = {{"twilightforest:persistent", true}};
        auto const* b = game::blockWithStates(id, states);
        ctx.check(b != nullptr, id + " exists");
        if (b) showcaseSet(*region, bp(p), *b);
    }
    // the game's own cap blocks with the cap skin on every face (huge_mushroom_bits 14), beside the
    // row: the brown canopy mushroom's caps must look like the first of them
    for (auto [i, id] : {std::pair{0, "minecraft:brown_mushroom_block"}, std::pair{1, "minecraft:red_mushroom_block"}}) {
        GlobalPos p = row.offset(72 + i * 2, 0, 0);
        if (auto const* g = game::blockWith("minecraft:grass_block")) showcaseSet(*region, bp(p.offset(0, -1, 0)), *g);
        if (auto const* b = game::blockWithStates(id, {{"huge_mushroom_bits", 14}})) showcaseSet(*region, bp(p), *b);
    }
    ctx.info("trees showcase blocks at " + std::to_string(row.x) + " " + std::to_string(row.y) + " " + std::to_string(row.z));
    showcaseLog().active = false;
    resendChunksOf(showcaseLog().order);
    ctx.info("trees showcase changed " + std::to_string(showcaseLog().order.size()) + " blocks (trees.zz_showcase_restore puts them back)");
});
// Client e2e only (local mode): the sorting and the transformation particles in flight and the
// transformation core's chunk resend reaching the real client. A sortingwood engine between two
// chests and a heart of transformation (range 4, the stand-in biome minecraft:mesa when the world has
// no enchanted forest: its grass is brown, so the resent chunk shows the change) run their effect
// every 10 / 20 ticks for kParticleTicks; the chests are refilled before each cycle and the biomes put
// back before each transformation (one fresh 4x4 cell per cycle). trees.zz_showcase_restore stops it
// and puts the blocks, biomes and overrides back.
GF_REGISTER_SELFTEST("trees.particles_probe", [](Context& ctx) -> ll::coro::CoroTask<> {
    constexpr int kParticleTicks = 1200;
    constexpr int kKeepTick      = 400;
    showcaseLog().active = true;
    GlobalPos base = ctx.testPos(399, 2).offset(250, 0, -96);
    co_await ctx.ensureLoaded(base);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    auto set = [&](GlobalPos const& p, char const* id, nlohmann::json const& states) {
        if (auto const* b = game::blockWithStates(id, states)) showcaseSet(*region, bp(p), *b);
    };
    for (int dx = -8; dx <= 8; ++dx)
        for (int dz = -8; dz <= 8; ++dz) {
            set(base.offset(dx, -1, dz), "minecraft:grass_block", nlohmann::json::object());
            for (int dy = 0; dy < 6; ++dy)
                if (!region->getBlock(bp(base.offset(dx, dy, dz))).isAir()) set(base.offset(dx, dy, dz), "minecraft:air", nlohmann::json::object());
        }
    GlobalPos sorter = base.offset(-4, 0, 0), in = sorter.offset(2, 0, 0), out = sorter.offset(-3, 0, 0), trans = base.offset(4, 0, 0);
    set(sorter, "twilightforest:sorting_log_core", {{"twilightforest:axis", "y"}});
    set(in, "minecraft:chest", nlohmann::json::object());
    set(out, "minecraft:chest", nlohmann::json::object());
    set(trans, "twilightforest:transformation_log_core", {{"twilightforest:axis", "y"}});
    showcaseLog().active = false;
    resendChunksOf(showcaseLog().order);
    co_await ctx.ticks(2);
    region = ctx.region();
    ctx.require(region != nullptr, "a region");
    // the stand-in biome and the biomes the transformation may touch (range 4: the 4x4 cells within 7)
    bool standIn = !hasTransformationTarget(*region);
    if (standIn) setTransformationTargetForTest("minecraft:mesa");
    auto saved = std::make_shared<std::map<std::pair<int, int>, Biome const*>>();
    BlockPos t = bp(trans);
    for (int dx = -8; dx <= 8; ++dx)
        for (int dz = -8; dz <= 8; ++dz) (*saved)[{t.x + dx, t.z + dz}] = &region->getBiome(BlockPos{t.x + dx, t.y, t.z + dz});
    int dim = trans.dim;
    auto putBiomesBack = [saved, dim, y = t.y] {
        auto* r = game::regionOf(dim);
        if (!r) return;
        std::set<std::pair<int, int>> chunks;
        for (auto const& [xz, b] : *saved)
            if (auto* lc = r->getChunkAt(BlockPos{xz.first, y, xz.second})) {
                lc->_setBiome(*b, ChunkBlockPos{static_cast<uchar>(xz.first & 15), ChunkLocalHeight{0}, static_cast<uchar>(xz.second & 15)}, true);
                chunks.emplace(xz.first >> 4, xz.second >> 4);
            }
        // the clients see the biomes back too
        for (auto const& [cx, cz] : chunks) resendChunkToViewers(*r, ChunkPos{cx, cz});
    };
    showcaseLog().undo.push_back([putBiomesBack, standIn] {
        putBiomesBack();
        if (standIn) setTransformationTargetForTest("");
    });
    auto before = chunkResendStats();
    ll::coro::keepThis([generation = gRestoreGeneration, dim, s = bp(sorter), i = bp(in), o = bp(out), t, putBiomesBack, before, saved]() -> ll::coro::CoroTask<> {
        bool kept = false; // from kKeepTick on, the last transformed cells stay for the screenshot from above
        for (int tick = 0; tick < kParticleTicks && generation == gRestoreGeneration; tick += 10) {
            auto* r = game::regionOf(dim);
            if (!r || !r->hasChunksAt(s, 0, false) || !r->hasChunksAt(t, 0, false)) {
                co_await ll::chrono::ticks(10);
                continue;
            }
            auto* ci = chestAt(*r, i);
            auto* co = chestAt(*r, o);
            if (ci && co) {
                ci->removeAllItems();
                co->removeAllItems();
                if (auto st = game::items::make("minecraft:stick", 3)) ci->setItem(0, *st);
                if (auto st = game::items::make("minecraft:stick", 1)) co->setItem(5, *st);
                RangeGuard guard;
                gf::config::mut<TreesConfig>().sortingCoreRange = 4;
                performCoreEffect(Core::Sorting, *r, s);
            }
            if (tick % 20 == 0 && !kept) {
                putBiomesBack();
                RangeGuard guard;
                gf::config::mut<TreesConfig>().transformationCoreRange = 4;
                for (int k = 0; k < 8 && performCoreEffect(Core::Transformation, *r, t) == 0; ++k) {}
                if (tick >= kKeepTick) {
                    // the cells this cycle turned stay (the undo puts them back): name one for the
                    // client scenario, which photographs it from above
                    for (auto const& [xz, b] : *saved)
                        if (&r->getBiome(BlockPos{xz.first, t.y, xz.second}) != b) {
                            kept = true;
                            gf::log().info("trees: particles probe: the transformed cell at {} {} {} stays", (xz.first & ~3) + 2, t.y, (xz.second & ~3) + 2);
                            break;
                        }
                }
            }
            if (tick == 200) {
                auto now = chunkResendStats();
                gf::log().info("trees: particles probe: {} chunk resends sent to real clients, {} refused", now.sent - before.sent, now.refused - before.refused);
            }
            co_await ll::chrono::ticks(10);
        }
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
    ctx.info("trees particles probe at " + std::to_string(base.x) + " " + std::to_string(base.y) + " " + std::to_string(base.z) + (standIn ? " (stand-in biome minecraft:mesa)" : ""));
});

} // namespace gf::feature::trees
#endif
