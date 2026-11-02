// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// Waterlogging for the structure kit. Java keeps "waterlogged" in the block state; Bedrock keeps
// the water in the block's second (liquid) layer. A Java state with waterlogged = true is placed
// as the block plus water in that layer, any other state empties it. During generation the
// region has no liquid layer of its own: the water is put there when the chunk is loaded
// (blockentities::waterlog), and a placement remembers which positions it made wet.
#include "feature/structure/Region.h"

#include <algorithm>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockProperty.h"

#include "game/Blocks.h"

namespace gf::structure::liquids {

bool ignoresWaterlogging(std::vector<std::string> const& processors) {
    return std::find(processors.begin(), processors.end(), std::string(kIgnoreWaterlogging)) != processors.end();
}

namespace {

bool hasFlag(Block const& block, BlockProperty flag) {
    auto p = static_cast<std::uint64_t>(static_cast<BlockProperty>(block.getBlockType().mProperties));
    return (p & static_cast<std::uint64_t>(flag)) != 0;
}

bool endsWithAny(std::string const& id, std::initializer_list<char const*> suffixes) {
    for (auto const* s : suffixes)
        if (id.ends_with(s)) return true;
    return false;
}

} // namespace

namespace {

std::shared_mutex                              gCacheMutex;
std::unordered_map<Block const*, bool>         gWaterloggable; ///< per block permutation

bool computeWaterloggable(Block const& block);

} // namespace

void clearCaches() {
    std::unique_lock lock(gCacheMutex);
    gWaterloggable.clear();
}

bool waterloggable(Block const& block) {
    {
        std::shared_lock lock(gCacheMutex);
        if (auto it = gWaterloggable.find(&block); it != gWaterloggable.end()) return it->second;
    }
    bool result = computeWaterloggable(block);
    std::unique_lock lock(gCacheMutex);
    gWaterloggable[&block] = result;
    return result;
}

namespace {

bool computeWaterloggable(Block const& block) {
    // the Java 1.21.1 SimpleWaterloggedBlock / LiquidBlockContainer families (vanilla and TF)
    auto const& id = block.getTypeName();
    if (block.isAir() || blocks::isLiquid(block)) return false;
    if (hasFlag(block, BlockProperty::Stair) || hasFlag(block, BlockProperty::HalfSlab) || hasFlag(block, BlockProperty::Trapdoor) ||
        hasFlag(block, BlockProperty::Leaves))
        return true;
    if (id.ends_with("_double_slab")) return false; // Java's double slab state is never waterlogged
    if (id.ends_with("_fence_gate") || id == "minecraft:fence_gate") return false;
    if (endsWithAny(id, {"_stairs", "_slab", "_fence", "_wall", "_trapdoor", "_leaves", "_sign", "_hanging_sign", "_pane", "_bars", "_banister",
                         "_drying_rack", "_chest", "_chain", "_candle", "_coral", "_coral_fan", "_coral_wall_fan", "_rail", "_bud", "_grate",
                         "_propagule", "_dripleaf", "_lightning_rod"}))
        return true;
    static constexpr char const* kIds[] = {
        "minecraft:fence",          "minecraft:chest",          "minecraft:trapped_chest",  "minecraft:ender_chest",    "minecraft:ladder",
        "minecraft:chain",          "minecraft:iron_chain",     "minecraft:iron_bars",      "minecraft:glass_pane",     "minecraft:candle",
        "minecraft:scaffolding",    "minecraft:conduit",        "minecraft:sea_pickle",     "minecraft:lightning_rod",  "minecraft:pointed_dripstone",
        "minecraft:amethyst_cluster", "minecraft:hanging_roots", "minecraft:glow_lichen",   "minecraft:sculk_vein",     "minecraft:mangrove_roots",
        "minecraft:decorated_pot",  "minecraft:heavy_core",     "minecraft:light_block",    "minecraft:campfire",       "minecraft:soul_campfire",
        "minecraft:rail",           "minecraft:golden_rail",    "minecraft:detector_rail",  "minecraft:activator_rail", "minecraft:standing_sign",
        "minecraft:wall_sign",      "minecraft:trapdoor",       "minecraft:lantern",        "minecraft:soul_lantern",   "minecraft:big_dripleaf_stem",
    };
    return std::find(std::begin(kIds), std::end(kIds), id) != std::end(kIds);
}

} // namespace

bool alwaysWater(Block const& block) {
    auto const& id = block.getTypeName();
    return id == "minecraft:seagrass" || id == "minecraft:tall_seagrass" || id == "minecraft:kelp" || id == "minecraft:kelp_plant" ||
           id == "minecraft:bubble_column";
}

bool isWaterSource(Block const& block) {
    if (!blocks::isWater(block)) return false;
    auto depth = game::intState(block, "liquid_depth");
    return !depth || *depth == 0;
}

bool isLavaSource(Block const& block) {
    auto const& id = block.getTypeName();
    if (id != "minecraft:lava" && id != "minecraft:flowing_lava") return false;
    auto depth = game::intState(block, "liquid_depth");
    return !depth || *depth == 0;
}

bool waterSourceAt(Region& region, BlockPos const& pos) {
    if (isWaterSource(region.getBlock(pos))) return true;
    if (auto* source = blockentities::sourceOf(region)) {
        (void)source;
        return isWaterSource(static_cast<BlockSourceRegion&>(region).getLiquidLayer(pos));
    }
    return false;
}

void setWaterlogged(Region& region, BlockPos const& pos, bool waterlogged) {
    if (blockentities::sourceOf(region)) {
        auto& live = static_cast<BlockSourceRegion&>(region);
        if (waterlogged) {
            if (!isWaterSource(live.getLiquidLayer(pos))) live.setLiquidLayer(pos, blocks::java("minecraft:water"), 3);
        } else if (!blocks::isAir(live.getLiquidLayer(pos))) {
            live.setLiquidLayer(pos, blocks::air(), 3);
        }
        return;
    }
    // a generation region: the block layer replaced its water already; water goes in later
    if (waterlogged) blockentities::waterlog(region, pos, region.getBlock(pos).getTypeName());
}

bool setBlock(Region& region, BlockPos const& pos, Block const& block, int flags, bool waterlogged) {
    if (blockentities::sourceOf(region)) return static_cast<BlockSourceRegion&>(region).setBlockWithLiquid(pos, block, flags, waterlogged);
    bool ok = region.setBlock(pos, block, flags);
    if (ok && waterlogged && !blocks::isLiquid(block)) blockentities::waterlog(region, pos, block.getTypeName());
    return ok;
}

} // namespace gf::structure::liquids
