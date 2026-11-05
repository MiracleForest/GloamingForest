// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/SpecialMagicLogBlock.java
//   src/main/java/twilightforest/block/TimeLogCoreBlock.java
//   src/main/java/twilightforest/block/TransLogCoreBlock.java
//   src/main/java/twilightforest/block/MineLogCoreBlock.java
//   src/main/java/twilightforest/block/SortLogCoreBlock.java
//   src/main/java/twilightforest/item/OreMagnetItem.java (doMagnet, findVein)
//   src/main/java/twilightforest/events/ToolEvents.java (refreshOreMagnetCache)
//   src/main/java/twilightforest/util/WorldUtil.java (randomOffset)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U3 trees: the magic log cores. Using a core toggles `active` (useWithoutItem, with any held item
// unless sneaking with one); an active core runs its effect every 20 ticks (SpecialMagicLogBlock's
// scheduled tick, here a module timer per core kept in the world DB as gf:trees:cores). The mining
// core runs OreMagnetItem.doMagnet as TF does (translated here: U8 owns the magnet item, rule R2).
#include "feature/trees/Trees.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <unordered_set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/entity/components_json_legacy/ContainerComponent.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/SetTitlePacket.h"
#include "mc/server/NetworkChunkPublisher.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemDescriptor.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/alchemy/PotionBrewing.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkBlockPos.h"
#include "mc/world/level/ChunkLocalHeight.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/server/ChunkPositionAndDimension.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/biome/registry/BiomeRegistry.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"
#include "mc/world/level/block/actor/component/IVanillaMainBlockActorComponent.h"
#include "mc/world/level/block/actor/component/IVanillaTickBlockActorComponent.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/chunk/LevelChunkDataField.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "core/jrand/JRand.h"
#include "feature/common/Interactions.h"
#include "feature/ext/trees/RandomTicks.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/families/BlockEvents.h"
#include "feature/fx/Fx.h"
#include "feature/inventory/Inventory.h"
#include "feature/item/DataFiles.h"
#include "feature/tags/Tags.h"
#include "feature/tags/VanillaTags.h"
#include "feature/trees/TreeUtil.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::trees {

namespace ia  = gf::interactions;
namespace kit = gf::features::kit;
using core::GlobalPos;

namespace {

constexpr char kDbKey[]     = "gf:trees:cores";
constexpr int  kTickRate    = 20; ///< SpecialMagicLogBlock#tickRate
constexpr int  kMaxRange    = 128; ///< TFConfig's defineInRange(.., 0, 128) of every core range
constexpr char kEnchanted[] = "twilightforest:enchanted_forest";

struct CoreInfo {
    Core        core;
    char const* id;
    char const* nameKey;
    char const* fallback;
};
constexpr CoreInfo kCores[] = {
    {Core::Time, "twilightforest:time_log_core", "block.twilightforest.time_log_core", "Timewood Clock"},
    {Core::Transformation, "twilightforest:transformation_log_core", "block.twilightforest.transformation_log_core", "Heart of Transformation"},
    {Core::Mining, "twilightforest:mining_log_core", "block.twilightforest.mining_log_core", "Minewood Core"},
    {Core::Sorting, "twilightforest:sorting_log_core", "block.twilightforest.sorting_log_core", "Sortingwood Engine"},
};

CoreInfo const& info(Core c) { return kCores[static_cast<int>(c)]; }

/// The level's random (Java ServerLevel.random) for the cores.
core::jrand::RandomSource& coreRandom() {
    static core::jrand::LegacyRandom r(static_cast<std::int64_t>(std::random_device{}()) << 16 ^ static_cast<std::int64_t>(std::random_device{}()));
    return r;
}

struct State {
    std::map<GlobalPos, std::int64_t>   due; ///< active cores -> the tick of their next cycle
    std::int64_t                        tick = 0;
    std::vector<ll::event::ListenerPtr> listeners;
    std::vector<ia::HandlerId>          handlers;
    bool                                loaded = false;
    std::string                         transformationTargetOverride; ///< selftests: a stand-in biome
    ChunkResendStats                    resends;
};
State& state() {
    static State s;
    return s;
}

void save() {
    if (!game::db::available()) return;
    nlohmann::json list = nlohmann::json::array();
    for (auto const& [p, d] : state().due) list.push_back({p.dim, p.x, p.y, p.z});
    game::db::put(kDbKey, list);
}

void load() {
    state().loaded = true;
    auto j         = game::db::get(kDbKey);
    if (!j || !j->is_array()) return;
    for (auto const& e : *j)
        if (e.is_array() && e.size() == 4) state().due.try_emplace(GlobalPos{e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()}, state().tick + kTickRate);
}

bool isActive(Block const& b) {
    auto states = game::statesOf(b);
    auto it     = states.find("twilightforest:active");
    return it != states.end() && (it->second == "true" || it->second == "1");
}

Block const& withActive(Block const& b, bool on) { return kit::withProperty(b, "active", on ? "true" : "false"); }

void actionBar(Player& player, std::string const& text) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::SetTitle);
    if (!packet) return;
    auto& title      = static_cast<SetTitlePacket&>(*packet);
    title.mType      = SetTitlePacketPayload::TitleType::Actionbar;
    title.mTitleText = text;
    packet->sendTo(player);
}

Vec3 center(BlockPos const& p) { return Vec3{p.x + 0.5f, p.y + 0.5f, p.z + 0.5f}; }

BlockPos randomOffset(core::jrand::RandomSource& r, BlockPos const& pos, int rx, int ry, int rz) {
    int dx = r.nextInt(rx * 2 + 1) - rx;
    int dy = r.nextInt(ry * 2 + 1) - ry;
    int dz = r.nextInt(rz * 2 + 1) - rz;
    return at(pos, dx, dy, dz);
}

// ---- time ---------------------------------------------------------------------------------------------

/// Vanilla blocks whose states tick randomly in Java 1.21.1 (BlockState#isRandomlyTicking), by
/// Bedrock id: crops and stems, saplings and propagules, sugar cane, cactus, bamboo, kelp, vines
/// (vine, cave / twisting / weeping vines), grass, mycelium, nylium, farmland, ice, frosted ice, snow,
/// lava, cocoa, nether wart, sweet berries, chorus flowers, turtle eggs, pointed dripstone, budding
/// amethyst, lit redstone ores, the leaves and every copper block that still oxidizes. Not: fire
/// (scheduled ticks), big dripleaf, sculk shriekers, amethyst buds, sniffer eggs.
bool vanillaRandomTicking(std::string const& id) {
    static std::unordered_set<std::string> const ids = [] {
        std::unordered_set<std::string> s{
            "minecraft:wheat", "minecraft:carrots", "minecraft:potatoes", "minecraft:beetroot", "minecraft:melon_stem", "minecraft:pumpkin_stem",
            "minecraft:torchflower_crop", "minecraft:pitcher_crop", "minecraft:sapling", "minecraft:oak_sapling", "minecraft:spruce_sapling",
            "minecraft:birch_sapling", "minecraft:jungle_sapling", "minecraft:acacia_sapling", "minecraft:dark_oak_sapling",
            "minecraft:cherry_sapling", "minecraft:pale_oak_sapling", "minecraft:mangrove_propagule", "minecraft:bamboo_sapling", "minecraft:reeds",
            "minecraft:cactus", "minecraft:bamboo", "minecraft:kelp", "minecraft:vine", "minecraft:grass_block", "minecraft:mycelium",
            "minecraft:farmland", "minecraft:ice", "minecraft:frosted_ice", "minecraft:snow_layer", "minecraft:lava", "minecraft:flowing_lava",
            "minecraft:cocoa", "minecraft:nether_wart", "minecraft:sweet_berry_bush", "minecraft:chorus_flower", "minecraft:turtle_egg",
            "minecraft:pointed_dripstone", "minecraft:budding_amethyst", "minecraft:cave_vines", "minecraft:cave_vines_body_with_berries",
            "minecraft:cave_vines_head_with_berries", "minecraft:twisting_vines", "minecraft:weeping_vines", "minecraft:crimson_nylium",
            "minecraft:warped_nylium", "minecraft:lit_redstone_ore", "minecraft:lit_deepslate_redstone_ore", "minecraft:oak_leaves",
            "minecraft:spruce_leaves", "minecraft:birch_leaves", "minecraft:jungle_leaves", "minecraft:acacia_leaves", "minecraft:dark_oak_leaves",
            "minecraft:mangrove_leaves", "minecraft:cherry_leaves", "minecraft:azalea_leaves", "minecraft:azalea_leaves_flowered",
            "minecraft:pale_oak_leaves",
        };
        // WeatheringCopper: every unwaxed copper block below the oxidized stage
        for (char const* stage : {"", "exposed_", "weathered_"}) {
            std::string st = stage;
            for (char const* kind : {"cut_copper", "cut_copper_stairs", "cut_copper_slab", "double_cut_copper_slab", "copper_door", "copper_trapdoor",
                                     "copper_bulb", "copper_grate", "chiseled_copper"})
                s.insert("minecraft:" + st + kind);
            s.insert(st.empty() ? std::string("minecraft:copper_block") : "minecraft:" + st + "copper");
        }
        return s;
    }();
    return ids.contains(id);
}

/// Vanilla block entities with a server ticker in Java 1.21.1 (getTicker != null on the server), by
/// Bedrock id.
bool vanillaTicker(std::string const& id) {
    static std::unordered_set<std::string> const ids{
        "minecraft:furnace", "minecraft:lit_furnace", "minecraft:blast_furnace", "minecraft:lit_blast_furnace", "minecraft:smoker",
        "minecraft:lit_smoker", "minecraft:brewing_stand", "minecraft:hopper", "minecraft:campfire", "minecraft:soul_campfire",
        "minecraft:mob_spawner", "minecraft:beacon", "minecraft:conduit", "minecraft:bell", "minecraft:end_gateway",
        "minecraft:sculk_catalyst", "minecraft:sculk_sensor", "minecraft:calibrated_sculk_sensor", "minecraft:sculk_shrieker",
        "minecraft:trial_spawner", "minecraft:vault", "minecraft:crafter", "minecraft:beehive", "minecraft:bee_nest", "minecraft:jukebox",
        "minecraft:daylight_detector", "minecraft:daylight_detector_inverted",
    };
    return ids.contains(id);
}

/// `state.getOcclusionShape(level, pos).max(Y)` (block-relative; nullopt: an empty shape). Blocks
/// built with noOcclusion() (leaves, glass, ...) have an empty occlusion shape in Java; the others'
/// is their shape, here the block's outline box.
std::optional<double> occlusionTop(BlockSource& region, BlockPos const& pos, Block const& state) {
    if (kit::isLeaves(state) || kit::javaId(state).find("glass") != std::string::npos) return std::nullopt;
    AABB        buffer{};
    AABB const& box = state.getBlockType().getOutline(state, region, pos, buffer);
    if (!(box.max.y > box.min.y) && !(box.max.x > box.min.x)) return std::nullopt; // empty
    // the outline is in world space (a block-local box, 0..1.5, is taken as it is)
    bool world = box.min.y >= static_cast<float>(pos.y) - 1.0f && box.max.y <= static_cast<float>(pos.y) + 2.5f;
    return world ? static_cast<double>(box.max.y) - pos.y : static_cast<double>(box.max.y);
}

int timeEffect(BlockSource& region, BlockPos const& pos) {
    auto&     random   = coreRandom();
    int const range    = coreRange(Core::Time);
    auto&     level    = region.getLevel();
    auto&     dim      = region.getDimension();
    int       worked   = 0;
    int const numTicks = 8 * 3 * kTickRate;
    for (int i = 0; i < numTicks; ++i) {
        BlockPos dPos = randomOffset(random, pos, range, range, range);
        if (dPos.y < region.getMinHeight() || dPos.y >= region.getMaxHeight() || !region.hasChunksAt(dPos, 0, false)) continue;
        Block const& state = region.getBlock(dPos);
        if (state.isAir() || gf::tags::has(gf::tags::Registry::Block, "twilightforest:time_core_excluded", state.getTypeName())) continue;
        bool ok = false;
        if (gf::feature::families::detail::randomTicked(state.getBlockType())) {
            gf::feature::families::detail::randomTickAt(region, dPos);
            ok = true;
        } else if (vanillaRandomTicking(state.getTypeName())) {
            state.randomTick(region, dPos, level.getRandom());
            ok = true;
        }
        if (vanillaTicker(state.getTypeName())) {
            if (auto* actor = region.getBlockEntity(dPos))
                if (auto* ticker = actor->_getTickComponent()) {
                    ticker->tick(region);
                    ok = true;
                }
        }
        if (!ok) continue;
        ++worked;
        // the particle at the top of the state's occlusion shape (an empty shape: -infinity, unseen)
        if (auto top = occlusionTop(region, dPos, state))
            fx::particle(dim, "twilightforest:log_core_particle", Vec3{dPos.x + 0.5f, static_cast<float>(dPos.y + *top), dPos.z + 0.5f}, Vec3{0.953f, 0.698f, 0.0f});
    }
    return worked;
}

// ---- transformation ----------------------------------------------------------------------------------

/// The enchanted forest (U1's behaviour pack biome "twilightforest:enchanted_forest"; a selftest may
/// put a stand-in in its place); null when the world has none.
Biome const* transformationTarget(BlockSource& region) {
    auto&       registry = region.getLevel().getBiomeRegistry();
    auto const& id       = state().transformationTargetOverride;
    if (!id.empty()) {
        std::string name = id.starts_with("minecraft:") ? id.substr(10) : id;
        if (auto* b = registry.lookupByName(name)) return b;
        return registry.lookupByName(id);
    }
    return registry.lookupByName(kEnchanted);
}

/// LevelChunk#setUnsaved(true) for a biome-only change: the chunk's biome-state counter is touched
/// as the engine touches its counters on a change (DirtyTicksCounter: the change tick is the
/// counter's current tick, a clean counter starts counting), so the level storage saves the chunk's
/// data (Data3D with the biomes) on its next pass. No block is edited.
void markChunkDirty(LevelChunk& chunk) {
    auto& counter = chunk.mFullChunkDirtyTicksCounters[static_cast<int>(LevelChunkDataField::BiomeState)].get();
    if (counter.totalTime < 0) counter.totalTime = 0;
    counter.lastChange = counter.totalTime;
}

/// ChunkHolder#broadcastChanges for the biome resend (ChunkMap#resendBiomesForChunks): the chunk goes
/// again, as a LevelChunkPacket, to every player whose view holds it (NetworkChunkPublisher, the
/// game's own chunk sender; the client then asks for the sub-chunks again).
int resendChunk(BlockSource& region, ChunkPos const& cp) {
    int  sent = 0;
    auto type = region.getDimensionId();
    region.getDimension().forEachPlayer([&](Player& p) {
        if (p.isSimulatedPlayer()) return true;
        NetworkChunkPublisher* publisher = p.mChunkPublisherView.get();
        if (!publisher) return true;
        BlockPos at     = game::blockPosOf(p.getPosition());
        int      radius = static_cast<int>(p.mChunkRadius);
        if (std::abs((at.x >> 4) - cp.x) > radius || std::abs((at.z >> 4) - cp.z) > radius) return true;
        ChunkPositionAndDimension which;
        which.mPos  = cp;
        which.mType = type;
        if (publisher->_sendQueuedChunk(which, nullptr)) ++sent;
        else ++state().resends.refused;
        return true;
    });
    state().resends.sent += sent;
    return sent;
}

int transformationEffect(BlockSource& region, BlockPos const& pos) {
    auto&     random = coreRandom();
    int const range  = coreRange(Core::Transformation);
    auto*     target = transformationTarget(region);
    if (!target) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            gf::log().warn("trees: no biome {} in this world (U1's behaviour pack biome): the transformation core does nothing", kEnchanted);
        }
        return 0;
    }
    for (int i = 0; i < 16; ++i) {
        BlockPos  dPos = randomOffset(random, pos, range, 0, range);
        long long dx = dPos.x - pos.x, dy = dPos.y - pos.y, dz = dPos.z - pos.z;
        if (static_cast<double>(dx * dx + dy * dy + dz * dz) > 256.0) continue;
        if (!region.hasChunksAt(dPos, 0, false)) continue;
        if (&region.getBiome(dPos) == target) continue;
        auto* chunk = region.getChunkAt(dPos);
        if (!chunk) continue;
        // every 4x4x4 cell of the quart column in all sections (Bedrock keeps a biome per block)
        int qx = (dPos.x & 15) & ~3, qz = (dPos.z & 15) & ~3;
        for (int lx = qx; lx < qx + 4; ++lx)
            for (int lz = qz; lz < qz + 4; ++lz)
                chunk->_setBiome(*target, ChunkBlockPos{static_cast<uchar>(lx), ChunkLocalHeight{0}, static_cast<uchar>(lz)}, true);
        markChunkDirty(*chunk);
        int sent = resendChunk(region, ChunkPos{dPos.x >> 4, dPos.z >> 4});
        Vec3  xyz = center(dPos);
        auto& dim = region.getDimension();
        for (int j = 0; j < 9; ++j) {
            float angle = random.nextFloat() * 360.0F; // TF passes degrees to Math.cos (radians)
            Vec3  offset{static_cast<float>(std::cos(static_cast<double>(angle)) * 2.0), 0.0f, static_cast<float>(std::sin(static_cast<double>(angle)) * 2.0)};
            fx::particle(dim, "twilightforest:transformation_particle", Vec3{xyz.x + offset.x, xyz.y, xyz.z + offset.z}, Vec3{-offset.x, -0.0f, -offset.z});
        }
        gf::log().info("trees: the transformation core at {} {} {} turned the column at {} {} into {} (chunk resent to {} players)", pos.x, pos.y, pos.z, dPos.x, dPos.z,
                       biomeName(*target), sent);
        return 1;
    }
    return 0;
}

// ---- mining (OreMagnetItem.doMagnet with sourceIsMineCore) ----------------------------------------------

/// The block of a tag member (Bedrock ids; a Java id through the features kit).
Block const* memberBlock(std::string const& id) {
    if (auto const* b = game::blockWith(id)) return b;
    return kit::javaBlock(id);
}

/// Every `c:` block tag the data and the vanilla bridge know.
std::set<std::string> commonBlockTags() {
    std::set<std::string> tags;
    for (auto const& t : gf::tags::vanilla::knownTags(gf::tags::Registry::Block))
        if (t.starts_with("c:")) tags.insert(t);
    for (auto const& f : gf::feature::item::data::list("tags")) {
        if (f.ns != "c" || !f.rel.starts_with("block/")) continue;
        tags.insert("c:" + f.rel.substr(6));
    }
    return tags;
}

/// OreMagnetItem.TREE_ORE_TO_BLOCK_REPLACEMENTS as ToolEvents#refreshOreMagnetCache builds it: for
/// every c:ores_in_ground/<g> tag with a matching c:ore_bearing_ground/<g> tag, each ore of the first
/// maps to each ground of the second (the last ground wins), unless the ore is in
/// #twilightforest:mining_tree_excluded; then ancient debris -> netherrack unless excluded or mapped.
/// Rebuilt when the data reloads.
struct OreCache {
    std::mutex                         mutex;
    bool                               built = false;
    std::map<std::string, std::string> map;
};
OreCache& oreCache() {
    static OreCache c;
    return c;
}

std::map<std::string, std::string> buildTreeOres() {
    std::map<std::string, std::string> m;
    constexpr char kExcluded[] = "twilightforest:mining_tree_excluded";
    auto const     tags        = commonBlockTags();
    for (auto const& tag : tags) {
        auto slash = tag.find("ores_in_ground/");
        if (slash == std::string::npos) continue;
        std::string ground = tag.substr(slash + 15);
        if (!tags.contains("c:ore_bearing_ground/" + ground)) continue;
        for (auto const& groundId : gf::tags::members(gf::tags::Registry::Block, "c:ore_bearing_ground/" + ground)) {
            Block const* gb = memberBlock(groundId);
            if (!gb) continue;
            for (auto const& oreId : gf::tags::members(gf::tags::Registry::Block, tag)) {
                Block const* ob = memberBlock(oreId);
                if (!ob) continue;
                if (gf::tags::has(gf::tags::Registry::Block, kExcluded, ob->getTypeName())) continue;
                m[ob->getTypeName()] = gb->getTypeName();
            }
        }
    }
    if (!gf::tags::has(gf::tags::Registry::Block, kExcluded, "minecraft:ancient_debris") && !m.contains("minecraft:ancient_debris"))
        m["minecraft:ancient_debris"] = "minecraft:netherrack";
    return m;
}

std::map<std::string, std::string> treeOres() {
    auto&           c = oreCache();
    std::lock_guard lock(c.mutex);
    if (!c.built) {
        c.map   = buildTreeOres();
        c.built = true;
        gf::log().info("trees: the minewood core knows {} ores", c.map.size());
    }
    return c.map;
}

bool safeReplace(Block const& b) { return gf::tags::has(gf::tags::Registry::Block, "twilightforest:ore_magnet/ore_safe_replace_block", b.getTypeName()); }

void findVein(BlockSource& region, BlockPos const& here, Block const& ore, std::vector<BlockPos>& vein) {
    if (std::find(vein.begin(), vein.end(), here) != vein.end()) return;
    if (vein.size() >= 24) return; // 24 blocks at a time
    if (&region.getBlock(here) != &ore) return;
    vein.push_back(here);
    for (Dir d : kAll) findVein(region, rel(here, d), ore, vein);
}

int doMagnet(BlockSource& region, BlockPos const& usePos, BlockPos const& destPos) {
    Block const*            ore         = nullptr;
    std::string             replacement = "minecraft:stone";
    std::optional<BlockPos> foundPos, basePos;
    auto const              ores = treeOres();
    Bresenham               line(usePos, destPos);
    BlockPos                coord;
    while (line.next(coord)) {
        if (coord.y < region.getMinHeight() || coord.y >= region.getMaxHeight()) continue;
        Block const& search = region.getBlock(coord);
        // the dirt / stone found first is where the ore goes: pushing ores away is a bad idea
        if (!basePos) {
            if (safeReplace(search)) basePos = coord;
        } else if (!foundPos && !search.isAir()) {
            auto it = ores.find(search.getTypeName());
            if (it != ores.end() && !region.getBlockEntity(coord)) {
                ore         = &search;
                replacement = it->second;
                foundPos    = coord;
            }
        }
    }
    if (!basePos || !foundPos || !ore) return 0;
    Block const* repl = game::blockWith(replacement);
    if (!repl) return 0;
    std::vector<BlockPos> vein;
    findVein(region, *foundPos, *ore, vein);
    int   offX = basePos->x - foundPos->x, offY = basePos->y - foundPos->y, offZ = basePos->z - foundPos->z;
    int   moved = 0;
    auto& dim   = region.getDimension();
    for (auto const& c : vein) {
        BlockPos     replacePos = at(c, offX, offY, offZ);
        Block const& rs         = region.getBlock(replacePos);
        if (!(safeReplace(rs) || kit::isReplaceable(rs) || rs.isAir())) continue;
        game::setBlock(region, c, *repl);
        Vec3 xyz = center(replacePos);
        for (int i = 0; i < 16; ++i) {
            auto& r  = coreRandom();
            float ox = static_cast<float>((r.nextDouble() - 0.5) * 1.25), oy = static_cast<float>((r.nextDouble() - 0.5) * 1.25), oz = static_cast<float>((r.nextDouble() - 0.5) * 1.25);
            fx::particle(dim, "twilightforest:log_core_particle", Vec3{xyz.x + ox, xyz.y + oy, xyz.z + oz}, Vec3{0.8f, 0.9f, 0.2f});
        }
        game::setBlock(region, replacePos, *ore); // set close to ore material
        ++moved;
    }
    return moved;
}

int miningEffect(BlockSource& region, BlockPos const& pos) {
    int      range = coreRange(Core::Mining);
    BlockPos dPos  = randomOffset(coreRandom(), pos, range, range, range);
    int      moved = doMagnet(region, pos, dPos);
    if (moved > 0) {
        fx::sound(region.getDimension(), "item.twilightforest.magnet.grab", center(pos), 0.1f, 1.0f);
        gf::log().info("trees: the mining core at {} {} {} moved {} ore blocks", pos.x, pos.y, pos.z, moved);
    }
    return moved;
}

// ---- sorting ----------------------------------------------------------------------------------------

/// One IItemHandler: the container slots it exposes (a SidedInvWrapper's face, or every slot) with
/// the container's rules (isItemValid / getSlotLimit / the extraction rule of the face).
struct Slots {
    std::function<ItemStack(int)>              get; ///< by container slot
    std::function<void(int, ItemStack const&)> set;
    std::vector<int>                           slots;
    std::function<bool(int, ItemStack const&)> canInsert;  ///< isItemValid (+ canPlaceItemThroughFace)
    std::function<bool(int, ItemStack const&)> canExtract; ///< canTakeItemThroughFace
    int                                        limit = 64; ///< getSlotLimit
    /// The inventory contract's own insert / extract (TF containers): used instead of set.
    std::function<ItemStack(ItemStack const&, bool)> insert;
    std::function<ItemStack(int, bool)>              extractOne;
};

struct Source {
    std::vector<Slots> faces; ///< a block's handlers per side (DOWN, UP, NORTH, SOUTH, WEST, EAST), merged when equal
    Vec3               at;    ///< where the particles start / end
};

bool any(std::string const& id, std::initializer_list<char const*> ids) {
    return std::any_of(ids.begin(), ids.end(), [&](char const* s) { return id == s; });
}

/// The faces of a vanilla container block as NeoForge's capability wraps it: a WorldlyContainer
/// gets a SidedInvWrapper per face (getSlotsForFace, canPlaceItemThroughFace,
/// canTakeItemThroughFace), any other Container one InvWrapper (the six are equal).
std::vector<Slots> vanillaFaces(std::string const& id, std::function<Container*()> find) {
    auto get = [find](int s) {
        auto* c = find();
        return c ? c->getItem(s) : game::items::empty();
    };
    auto set = [find](int s, ItemStack const& st) {
        if (auto* c = find()) c->setItem(s, st);
    };
    Container* c     = find();
    int        size  = c ? c->getContainerSize() : 0;
    int        limit = c ? c->getMaxStackSize() : 64;
    auto       all   = [&] {
        std::vector<int> v(static_cast<std::size_t>(size));
        for (int i = 0; i < size; ++i) v[static_cast<std::size_t>(i)] = i;
        return v;
    };
    auto yes = [](int, ItemStack const&) { return true; };
    auto idOf = [](ItemStack const& s) { return game::items::idOf(s); };
    if (any(id, {"minecraft:furnace", "minecraft:lit_furnace", "minecraft:blast_furnace", "minecraft:lit_blast_furnace", "minecraft:smoker", "minecraft:lit_smoker"})) {
        // AbstractFurnaceBlockEntity: 0 input, 1 fuel, 2 result; faces DOWN [2, 1], UP [0], sides [1]
        auto canPlace = [get, idOf](int slot, ItemStack const& s) {
            if (slot == 2) return false;
            if (slot != 1) return true;
            bool fuel = FurnaceBlockActor::getBurnDuration(s, 200.0f) > 0.0f;
            return fuel || (idOf(s) == "minecraft:bucket" && idOf(get(1)) != "minecraft:bucket");
        };
        auto takeDown = [idOf](int slot, ItemStack const& s) { return slot != 1 || idOf(s) == "minecraft:water_bucket" || idOf(s) == "minecraft:bucket"; };
        std::vector<Slots> faces;
        faces.push_back({get, set, {2, 1}, canPlace, takeDown, limit});
        faces.push_back({get, set, {0}, canPlace, yes, limit});
        for (int i = 0; i < 4; ++i) faces.push_back({get, set, {1}, canPlace, yes, limit});
        return faces;
    }
    if (id == "minecraft:brewing_stand") {
        // BrewingStandBlockEntity, Java slots 0-2 bottles, 3 ingredient, 4 fuel (Bedrock: 0
        // ingredient, 1-3 bottles, 4 fuel); faces UP [3], DOWN [0, 1, 2, 3], sides [0, 1, 2, 4]
        auto canPlace = [get, idOf](int slot, ItemStack const& s) {
            auto item = idOf(s);
            if (slot == 0) return PotionBrewing::isIngredient(ItemDescriptor(item, 0));
            if (slot == 4) return item == "minecraft:blaze_powder";
            return any(item, {"minecraft:potion", "minecraft:splash_potion", "minecraft:lingering_potion", "minecraft:glass_bottle"}) && game::items::isEmpty(get(slot));
        };
        auto canTake = [idOf](int slot, ItemStack const& s) { return slot != 0 || idOf(s) == "minecraft:glass_bottle"; };
        std::vector<Slots> faces;
        faces.push_back({get, set, {1, 2, 3, 0}, canPlace, canTake, limit});
        faces.push_back({get, set, {0}, canPlace, canTake, limit});
        for (int i = 0; i < 4; ++i) faces.push_back({get, set, {1, 2, 3, 4}, canPlace, canTake, limit});
        return faces;
    }
    if (id.ends_with("shulker_box")) {
        // ShulkerBoxBlockEntity: every slot on every face (one handler); no shulker box inside
        auto canPlace = [idOf](int, ItemStack const& s) { return !idOf(s).ends_with("shulker_box"); };
        return {Slots{get, set, all(), canPlace, yes, limit}};
    }
    if (id == "minecraft:chiseled_bookshelf") {
        auto canPlace = [get](int slot, ItemStack const& s) {
            return game::items::isEmpty(get(slot)) && gf::tags::has(gf::tags::Registry::Item, "minecraft:bookshelf_books", game::items::idOf(s));
        };
        return {Slots{get, set, all(), canPlace, yes, 1}};
    }
    return {Slots{get, set, all(), yes, yes, limit}};
}

std::optional<Source> blockSource(BlockSource& region, BlockPos const& p) {
    Vec3 top{p.x + 0.5f, p.y + 1.9f, p.z + 0.5f}; // Vec3.upFromBottomCenterOf(pos, 1.9)
    if (auto h = gf::inventory::find(region, p); h && h->get && h->set) {
        GlobalPos gp  = game::globalPos(region, p);
        auto      inv = std::make_shared<gf::inventory::Handler>(*h);
        Slots     s;
        s.get = [inv, gp](int slot) { return inv->get(gp, slot); };
        s.set = [inv, gp](int slot, ItemStack const& st) { inv->set(gp, slot, st); };
        for (int i = 0; i < h->slots; ++i) s.slots.push_back(i);
        s.canInsert  = [inv, gp](int, ItemStack const& st) { return !inv->insert || game::items::isEmpty(inv->insert(gp, game::items::withCount(st, 1), true)); };
        s.canExtract = [inv, gp](int slot, ItemStack const&) { return !inv->extract || !game::items::isEmpty(inv->extract(gp, slot, 1, true)); };
        if (h->insert) s.insert = [inv, gp](ItemStack const& st, bool simulate) { return inv->insert(gp, st, simulate); };
        if (h->extract) s.extractOne = [inv, gp](int slot, bool simulate) { return inv->extract(gp, slot, 1, simulate); };
        return Source{{std::move(s)}, top};
    }
    auto* actor = region.getBlockEntity(p);
    if (!actor) return std::nullopt;
    auto* main = actor->_getMainComponent();
    if (!main || !main->getContainer()) return std::nullopt;
    int  dim  = static_cast<int>(region.getDimensionId());
    auto find = [dim, p]() -> Container* {
        auto* r = game::regionOf(dim);
        if (!r) return nullptr;
        auto* a = r->getBlockEntity(p);
        auto* m = a ? a->_getMainComponent() : nullptr;
        return m ? m->getContainer() : nullptr;
    };
    return Source{vanillaFaces(region.getBlock(p).getTypeName(), find), top};
}

std::optional<Source> entitySource(Actor& e) {
    auto comp = e.getEntityContext().tryGetComponent<ContainerComponent>();
    if (!comp) return std::nullopt;
    Container* c   = &comp->mContainer.get();
    auto       pos = e.getPosition();
    float      h   = e.getAABB().max.y - e.getAABB().min.y;
    Slots      s;
    s.get = [c](int slot) { return c->getItem(slot); };
    s.set = [c](int slot, ItemStack const& st) { c->setItem(slot, st); };
    for (int i = 0; i < c->getContainerSize(); ++i) s.slots.push_back(i);
    s.canInsert  = [](int, ItemStack const&) { return true; };
    s.canExtract = [](int, ItemStack const&) { return true; };
    s.limit      = c->getMaxStackSize();
    return Source{{std::move(s)}, Vec3{pos.x, e.getAABB().min.y + h + 0.9f, pos.z}};
}

bool sortable(Actor& e) { return e.isAlive() && gf::tags::has(gf::tags::Registry::EntityType, "twilightforest:sortable_entities", e.getTypeName()); }

/// IItemHandler#extractItem(slot, 1, simulate).
ItemStack extractOne(Slots& h, int slot, bool simulate) {
    if (h.extractOne) return h.extractOne(slot, simulate);
    ItemStack s = h.get(slot);
    if (game::items::isEmpty(s) || !h.canExtract(slot, s)) return game::items::empty();
    if (!simulate) {
        int left = game::items::countOf(s) - 1;
        h.set(slot, left > 0 ? game::items::withCount(s, left) : game::items::empty());
    }
    return game::items::withCount(s, 1);
}

/// IItemHandler#insertItem(slot, one, false) after the simulation said it fits.
void insertOne(Slots& h, int slot, ItemStack const& one) {
    if (h.insert) {
        (void)h.insert(one, false);
        return;
    }
    ItemStack dest = h.get(slot);
    h.set(slot, game::items::isEmpty(dest) ? one : game::items::withCount(dest, game::items::countOf(dest) + 1));
}

int sortingEffect(BlockSource& region, BlockPos const& pos) {
    int const                                range = coreRange(Core::Sorting);
    std::vector<Source>                      inputs;
    std::vector<std::pair<Slots, Vec3>>      outputs;
    for (int dx = -range; dx <= range; ++dx)
        for (int dy = -range; dy <= range; ++dy)
            for (int dz = -range; dz <= range; ++dz) {
                if (dx == 0 && dy == 0 && dz == 0) continue;
                BlockPos p = at(pos, dx, dy, dz);
                if (p.y < region.getMinHeight() || p.y >= region.getMaxHeight() || !region.hasChunksAt(p, 0, false)) continue;
                auto h = blockSource(region, p);
                if (!h) continue;
                if (std::abs(dx) <= 2 && std::abs(dy) <= 2 && std::abs(dz) <= 2) inputs.push_back(std::move(*h));
                else
                    for (auto& f : h->faces) outputs.emplace_back(std::move(f), h->at);
            }
    std::vector<Actor*> used;
    AABB                inner{Vec3{pos.x - 2.0f, pos.y - 2.0f, pos.z - 2.0f}, Vec3{pos.x + 3.0f, pos.y + 3.0f, pos.z + 3.0f}};
    for (auto* e : region.getEntities(inner, 0.0f)) {
        if (!e || !sortable(*e)) continue;
        if (auto h = entitySource(*e)) {
            inputs.push_back(std::move(*h));
            used.push_back(e);
        }
    }
    if (inputs.empty()) return 0;
    AABB outer{Vec3{pos.x - 16.0f, pos.y - 16.0f, pos.z - 16.0f}, Vec3{pos.x + 17.0f, pos.y + 17.0f, pos.z + 17.0f}};
    for (auto* e : region.getEntities(outer, 0.0f)) {
        if (!e || !sortable(*e) || std::find(used.begin(), used.end(), e) != used.end()) continue;
        if (auto h = entitySource(*e))
            for (auto& f : h->faces) outputs.emplace_back(std::move(f), h->at);
    }
    if (outputs.empty()) return 0;
    int   sorted = 0;
    auto& random = coreRandom();
    for (auto& input : inputs) {
        bool transferred = false;
        for (auto& face : input.faces) {
            for (std::size_t si = 0; si < face.slots.size() && !transferred; ++si) {
                int       i          = face.slots[si];
                ItemStack inputStack = extractOne(face, i, true);
                if (game::items::isEmpty(inputStack)) continue;
                // the outputs holding this item, by how many they hold (a map keyed by the count:
                // of two outputs with the same count the later one stays)
                std::map<int, std::pair<Slots, Vec3>*, std::greater<int>> byCount;
                for (auto& out : outputs) {
                    int count = 0;
                    for (int j : out.first.slots) {
                        ItemStack s = out.first.get(j);
                        if (!game::items::isEmpty(s) && game::items::sameItem(s, inputStack)) count += game::items::countOf(s);
                    }
                    if (count > 0) byCount[count] = &out;
                }
                for (auto& [count, outp] : byCount) {
                    auto& out   = outp->first;
                    int   first = -1;
                    for (int j : out.slots) {
                        if (!out.canInsert(j, inputStack)) continue;
                        ItemStack s = out.get(j);
                        if (first == -1 && game::items::isEmpty(s)) {
                            first = j; // the first empty slot, in case no stack has room
                        } else if (!game::items::isEmpty(s) && game::items::sameItemSameData(inputStack, s) && game::items::countOf(s) < s.getMaxStackSize()
                                   && game::items::countOf(s) < out.limit) {
                            first = j;
                            break;
                        }
                    }
                    if (first == -1) continue;
                    ItemStack one = extractOne(face, i, false);
                    if (game::items::isEmpty(one)) continue;
                    insertOne(out, first, one);
                    transferred = true;
                    ++sorted;
                    Vec3   xyz = outp->second;
                    Vec3   diff{input.at.x - xyz.x, input.at.y - xyz.y, input.at.z - xyz.z};
                    double len = std::sqrt(static_cast<double>(diff.x) * diff.x + static_cast<double>(diff.y) * diff.y + static_cast<double>(diff.z) * diff.z);
                    double x   = diff.x - 0.25 + random.nextDouble() * 0.5;
                    double y   = diff.y - 1.75 + random.nextDouble() * 0.5;
                    double z   = diff.z - 0.25 + random.nextDouble() * 0.5;
                    double s   = len > 0 ? 1.0 / len : 0.0;
                    fx::particle(region.getDimension(), "twilightforest:sorting_particle", xyz, Vec3{static_cast<float>(x * s), static_cast<float>(y * s), static_cast<float>(z * s)});
                    break;
                }
            }
            if (transferred) break; // one item per source and cycle
        }
    }
    if (sorted > 0) gf::log().info("trees: the sorting core at {} {} {} sorted {} items", pos.x, pos.y, pos.z, sorted);
    return sorted;
}

// ---- the cycle -----------------------------------------------------------------------------------

void playCoreSound(Core core, BlockSource& region, BlockPos const& pos) {
    auto& dim = region.getDimension();
    if (core == Core::Time) fx::sound(dim, "block.twilightforest.core.time", center(pos), 0.35f, 0.5f);
    else if (core == Core::Transformation) fx::sound(dim, "block.twilightforest.core.transformation", center(pos), 0.1f, coreRandom().nextFloat() * 2.0f);
}

void activate(GlobalPos const& gp) {
    state().due[gp] = state().tick + kTickRate;
    save();
}

void deactivate(GlobalPos const& gp) {
    if (state().due.erase(gp)) save();
}

/// SpecialMagicLogBlock#useWithoutItem.
ia::Result use(ia::UseOnContext const& c) {
    // secondary use (sneaking with an item in either hand) bypasses the block
    bool holding = !game::items::isEmpty(c.player.getSelectedItem()) || !game::items::isEmpty(c.player.getOffhandSlot());
    if (c.sneaking && holding) return ia::Result::Pass;
    if (c.offhand || !c.firstEvent) return ia::Result::Consume; // the main hand's use took it; the repeats do nothing
    Core core;
    if (!coreOf(c.blockId, core)) return ia::Result::Pass;
    auto* region = game::regionOf(c.pos.dim);
    if (!region) return ia::Result::Consume;
    BlockPos     bp  = game::blockPos(c.pos);
    Block const& now = region->getBlock(bp);
    auto const&  ci  = info(core);
    if (coreRange(core) <= 0) {
        game::setBlock(*region, bp, withActive(now, false));
        deactivate(c.pos);
        actionBar(c.player, "§c" + coreDisabledMessage(c.player, core));
        gf::log().info("trees: {} is disabled via config ({} used it)", ci.id, c.player.getRealName());
        return ia::Result::Consume;
    }
    bool on = !isActive(now);
    game::setBlock(*region, bp, withActive(now, on));
    if (on) activate(c.pos);
    else deactivate(c.pos);
    gf::log().info("trees: {} at {} {} {} {}", ci.id, bp.x, bp.y, bp.z, on ? "activated" : "deactivated");
    return ia::Result::Consume;
}

} // namespace

std::string biomeName(Biome const& b) {
    auto const& name = b.mHash->getString();
    return name.find(':') == std::string::npos ? "minecraft:" + name : name;
}

bool coreOf(std::string const& blockId, Core& out) {
    for (auto const& c : kCores)
        if (blockId == c.id) {
            out = c.core;
            return true;
        }
    return false;
}

int coreRange(Core core) {
    auto const& cfg = config();
    int         r   = 0;
    switch (core) {
    case Core::Time:
        r = cfg.timeCoreRange;
        break;
    case Core::Transformation:
        r = cfg.transformationCoreRange;
        break;
    case Core::Mining:
        r = cfg.miningCoreRange;
        break;
    case Core::Sorting:
        r = cfg.sortingCoreRange;
        break;
    }
    return std::clamp(r, 0, kMaxRange); // TFConfig: defineInRange(.., 16, 0, 128)
}

int performCoreEffect(Core core, BlockSource& region, BlockPos const& pos) {
    if (coreRange(core) <= 0) return 0;
    switch (core) {
    case Core::Time:
        return timeEffect(region, pos);
    case Core::Transformation:
        return transformationEffect(region, pos);
    case Core::Mining:
        return miningEffect(region, pos);
    case Core::Sorting:
        return sortingEffect(region, pos);
    }
    return 0;
}

std::size_t activeCoreCount() { return state().due.size(); }

void setTransformationTargetForTest(std::string biomeId) { state().transformationTargetOverride = std::move(biomeId); }

bool hasTransformationTarget(BlockSource& region) {
    auto saved = std::exchange(state().transformationTargetOverride, std::string{});
    bool has   = transformationTarget(region) != nullptr;
    state().transformationTargetOverride = std::move(saved);
    return has;
}

ChunkResendStats chunkResendStats() { return state().resends; }

std::map<std::string, std::string> miningCoreOres() { return treeOres(); }

std::string coreDisabledMessage(Player const& player, Core core) {
    auto const& ci = info(core);
    return trFor(player, "misc.twilightforest.core_disabled", "%s is disabled via config", trFor(player, ci.nameKey, ci.fallback));
}

void tickCores() {
    auto& s = state();
    ++s.tick;
    if (!s.loaded) {
        if (!game::db::available()) return;
        load();
    }
    if (s.due.empty()) return;
    std::vector<GlobalPos> dueNow;
    for (auto const& [gp, t] : s.due)
        if (t <= s.tick) dueNow.push_back(gp);
    bool changed = false;
    for (auto const& gp : dueNow) {
        auto*    region = game::regionOf(gp.dim);
        BlockPos bp     = game::blockPos(gp);
        if (!region || !region->hasChunksAt(bp, 0, false)) {
            s.due[gp] = s.tick + kTickRate; // a scheduled tick waits with its chunk
            continue;
        }
        Block const& b = region->getBlock(bp);
        Core         core;
        if (!coreOf(b.getTypeName(), core) || !isActive(b)) {
            s.due.erase(gp);
            changed = true;
            continue;
        }
        if (coreRange(core) <= 0) {
            // SpecialMagicLogBlock#tick: a disabled core stops (it stays lit until used)
            s.due.erase(gp);
            changed = true;
            continue;
        }
        playCoreSound(core, *region, bp);
        performCoreEffect(core, *region, bp);
        if (auto it = s.due.find(gp); it != s.due.end()) it->second = s.tick + kTickRate;
    }
    if (changed) save();
}

void installCores() {
    auto& s = state();
    for (auto const& c : kCores) {
        ia::Match m;
        m.block = c.id;
        s.handlers.push_back(ia::onUseOn(m, use));
    }
    static bool registered = false;
    if (!registered) {
        registered = true;
        for (auto const& c : kCores) {
            // SpecialMagicLogBlock#onPlace: an active core placed (commands, structures) ticks
            gf::blockevents::onPlaced(c.id, [](BlockSource& region, BlockPos const& pos, Block const& block, Actor*) {
                if (isActive(block)) activate(game::globalPos(region, pos));
            });
            gf::blockevents::onRemoved(c.id, [](BlockSource& region, BlockPos const& pos, Block const&) { deactivate(game::globalPos(region, pos)); });
        }
        // TagsUpdatedEvent: the ore map follows the data
        gf::feature::item::data::onReload([] {
            std::lock_guard lock(oreCache().mutex);
            oreCache().built = false;
        });
    }
    game::db::onWorldReady([] { state().loaded = false; });
    game::db::onWorldClosed([] {
        state().due.clear();
        state().loaded = false;
    });
    auto& bus = ll::event::EventBus::getInstance();
    s.listeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](ll::event::ServerLevelTickEvent&) {
        tickCores();
        tickLeaves();
    }));
}

void uninstallCores() {
    auto& s   = state();
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : s.listeners) bus.removeListener(l);
    s.listeners.clear();
    for (auto id : s.handlers) ia::remove(id);
    s.handlers.clear();
}

} // namespace gf::feature::trees
