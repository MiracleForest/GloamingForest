// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/SpecialMagicLogBlock.java
//   src/main/java/twilightforest/block/TimeLogCoreBlock.java
//   src/main/java/twilightforest/block/TransLogCoreBlock.java
//   src/main/java/twilightforest/block/MineLogCoreBlock.java
//   src/main/java/twilightforest/block/SortLogCoreBlock.java
//   src/main/java/twilightforest/item/OreMagnetItem.java (doMagnet, findVein)
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
#include <random>
#include <set>
#include <unordered_set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/LevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/entity/components_json_legacy/ContainerComponent.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/SetTitlePacket.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkBlockPos.h"
#include "mc/world/level/ChunkLocalHeight.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/biome/registry/BiomeRegistry.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/component/IVanillaMainBlockActorComponent.h"
#include "mc/world/level/block/actor/component/IVanillaTickBlockActorComponent.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "core/jrand/JRand.h"
#include "feature/common/Interactions.h"
#include "feature/ext/trees/RandomTicks.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/families/BlockEvents.h"
#include "feature/fx/Fx.h"
#include "feature/inventory/Inventory.h"
#include "feature/tags/Tags.h"
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

constexpr char kDbKey[]       = "gf:trees:cores";
constexpr int  kTickRate      = 20; ///< SpecialMagicLogBlock#tickRate
constexpr char kEnchanted[]   = "twilightforest:enchanted_forest";

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
    std::map<GlobalPos, std::int64_t> due; ///< active cores -> the tick of their next cycle
    std::int64_t                      tick = 0;
    std::vector<ll::event::ListenerPtr> listeners;
    std::vector<ia::HandlerId>          handlers;
    bool                                loaded = false;
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
    state().due.clear();
    state().loaded = true;
    auto j         = game::db::get(kDbKey);
    if (!j || !j->is_array()) return;
    for (auto const& e : *j)
        if (e.is_array() && e.size() == 4) state().due[GlobalPos{e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()}] = state().tick + kTickRate;
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

/// Vanilla blocks whose state ticks randomly in Java (BlockState#isRandomlyTicking), by Bedrock id.
bool vanillaRandomTicking(std::string const& id) {
    static std::unordered_set<std::string> const ids{
        "minecraft:wheat", "minecraft:carrots", "minecraft:potatoes", "minecraft:beetroot", "minecraft:melon_stem", "minecraft:pumpkin_stem",
        "minecraft:torchflower_crop", "minecraft:pitcher_crop", "minecraft:sapling", "minecraft:oak_sapling", "minecraft:spruce_sapling",
        "minecraft:birch_sapling", "minecraft:jungle_sapling", "minecraft:acacia_sapling", "minecraft:dark_oak_sapling",
        "minecraft:cherry_sapling", "minecraft:pale_oak_sapling", "minecraft:mangrove_propagule", "minecraft:bamboo_sapling", "minecraft:reeds",
        "minecraft:cactus", "minecraft:bamboo", "minecraft:kelp", "minecraft:vine", "minecraft:grass_block", "minecraft:mycelium",
        "minecraft:farmland", "minecraft:ice", "minecraft:snow_layer", "minecraft:fire", "minecraft:soul_fire", "minecraft:cocoa",
        "minecraft:nether_wart", "minecraft:sweet_berry_bush", "minecraft:chorus_flower", "minecraft:turtle_egg", "minecraft:pointed_dripstone",
        "minecraft:budding_amethyst", "minecraft:cave_vines", "minecraft:cave_vines_body_with_berries", "minecraft:cave_vines_head_with_berries",
        "minecraft:twisting_vines", "minecraft:weeping_vines", "minecraft:copper_block", "minecraft:exposed_copper",
        "minecraft:weathered_copper", "minecraft:cut_copper", "minecraft:exposed_cut_copper", "minecraft:weathered_cut_copper",
        "minecraft:oak_leaves", "minecraft:spruce_leaves", "minecraft:birch_leaves", "minecraft:jungle_leaves", "minecraft:acacia_leaves",
        "minecraft:dark_oak_leaves", "minecraft:mangrove_leaves", "minecraft:cherry_leaves", "minecraft:azalea_leaves",
        "minecraft:azalea_leaves_flowered", "minecraft:pale_oak_leaves", "minecraft:sniffer_egg", "minecraft:crimson_nylium",
        "minecraft:warped_nylium", "minecraft:small_amethyst_bud", "minecraft:big_dripleaf", "minecraft:sculk_shrieker",
    };
    return ids.contains(id);
}

/// Vanilla block entities with a server ticker in Java (getTicker != null on the server), by Bedrock id.
bool vanillaTicker(std::string const& id) {
    static std::unordered_set<std::string> const ids{
        "minecraft:furnace", "minecraft:lit_furnace", "minecraft:blast_furnace", "minecraft:lit_blast_furnace", "minecraft:smoker",
        "minecraft:lit_smoker", "minecraft:brewing_stand", "minecraft:hopper", "minecraft:campfire", "minecraft:soul_campfire",
        "minecraft:mob_spawner", "minecraft:beacon", "minecraft:conduit", "minecraft:bell", "minecraft:end_gateway",
        "minecraft:sculk_catalyst", "minecraft:sculk_sensor", "minecraft:calibrated_sculk_sensor", "minecraft:sculk_shrieker",
        "minecraft:trial_spawner", "minecraft:vault", "minecraft:crafter", "minecraft:beehive", "minecraft:bee_nest",
    };
    return ids.contains(id);
}

int timeEffect(BlockSource& region, BlockPos const& pos) {
    auto&       random = coreRandom();
    int const   range  = coreRange(Core::Time);
    auto&       level  = region.getLevel();
    auto&       dim    = region.getDimension();
    int         worked = 0;
    int const   numTicks = 8 * 3 * kTickRate;
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
        // the particle at the top of the occlusion shape (an empty shape: -infinity, nothing shown)
        if (isSolidRender(state)) fx::particle(dim, "twilightforest:log_core_particle", Vec3{dPos.x + 0.5f, dPos.y + 1.0f, dPos.z + 0.5f}, Vec3{0.953f, 0.698f, 0.0f});
    }
    return worked;
}

// ---- transformation ----------------------------------------------------------------------------------

std::string biomeName(Biome const& b) {
    auto const& name = b.mHash->getString();
    return name.find(':') == std::string::npos ? "minecraft:" + name : name;
}

int transformationEffect(BlockSource& region, BlockPos const& pos) {
    auto&       random = coreRandom();
    int const   range  = coreRange(Core::Transformation);
    auto*       target = region.getLevel().getBiomeRegistry().lookupByName(kEnchanted);
    if (!target) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            gf::log().warn("trees: no biome {} in this world (the transformation core does nothing)", kEnchanted);
        }
        return 0;
    }
    for (int i = 0; i < 16; ++i) {
        BlockPos dPos = randomOffset(random, pos, range, 0, range);
        long long dx = dPos.x - pos.x, dy = dPos.y - pos.y, dz = dPos.z - pos.z;
        if (static_cast<double>(dx * dx + dy * dy + dz * dz) > 256.0) continue;
        if (!region.hasChunksAt(dPos, 0, false)) continue;
        if (biomeName(region.getBiome(dPos)) == kEnchanted) continue;
        auto* chunk = region.getChunkAt(dPos);
        if (!chunk) continue;
        // every 4x4x4 cell of the quart column in all sections (Bedrock keeps a biome per block)
        int   minY = region.getMinHeight();
        int   qx = (dPos.x & 15) & ~3, qz = (dPos.z & 15) & ~3;
        for (int lx = qx; lx < qx + 4; ++lx)
            for (int lz = qz; lz < qz + 4; ++lz)
                chunk->_setBiome(*target, ChunkBlockPos{static_cast<uchar>(lx), ChunkLocalHeight{0}, static_cast<uchar>(lz)}, true);
        (void)minY;
        // the chunk must save: a block change at the world's floor marks it (and is undone at once)
        BlockPos floor{dPos.x, region.getMinHeight(), dPos.z};
        Block const& was = region.getBlock(floor);
        if (auto const* other = game::blockWith(was.isAir() ? "minecraft:bedrock" : "minecraft:air")) {
            game::setBlock(region, floor, *other);
            game::setBlock(region, floor, was);
        }
        Vec3 xyz = center(dPos);
        auto& dim = region.getDimension();
        for (int j = 0; j < 9; ++j) {
            float angle = random.nextFloat() * 360.0F; // TF passes degrees to Math.cos (radians)
            Vec3  offset{static_cast<float>(std::cos(static_cast<double>(angle)) * 2.0), 0.0f, static_cast<float>(std::sin(static_cast<double>(angle)) * 2.0)};
            fx::particle(dim, "twilightforest:transformation_particle", Vec3{xyz.x + offset.x, xyz.y, xyz.z + offset.z}, Vec3{-offset.x, -0.0f, -offset.z});
        }
        gf::log().info("trees: the transformation core at {} {} {} turned the column at {} {} into {}", pos.x, pos.y, pos.z, dPos.x, dPos.z, kEnchanted);
        return 1;
    }
    return 0;
}

// ---- mining (OreMagnetItem.doMagnet with sourceIsMineCore) ----------------------------------------------

/// OreMagnetItem.TREE_ORE_TO_BLOCK_REPLACEMENTS: ore block id -> the ground it sits in, from the
/// c:ores_in_ground/<ground> and c:ore_bearing_ground/<ground> tags (minus
/// #twilightforest:mining_core_excluded), Java's NeoForge tag contents when the data has none.
std::map<std::string, std::string> const& treeOres() {
    static std::map<std::string, std::string> const map = [] {
        std::map<std::string, std::string> m;
        struct Ground {
            char const*              name;
            char const*              ground;
            std::vector<char const*> ores;
        };
        std::vector<Ground> const grounds{
            {"stone", "minecraft:stone", {"minecraft:coal_ore", "minecraft:copper_ore", "minecraft:diamond_ore", "minecraft:emerald_ore", "minecraft:gold_ore", "minecraft:iron_ore", "minecraft:lapis_ore", "minecraft:redstone_ore"}},
            {"deepslate", "minecraft:deepslate", {"minecraft:deepslate_coal_ore", "minecraft:deepslate_copper_ore", "minecraft:deepslate_diamond_ore", "minecraft:deepslate_emerald_ore", "minecraft:deepslate_gold_ore", "minecraft:deepslate_iron_ore", "minecraft:deepslate_lapis_ore", "minecraft:deepslate_redstone_ore"}},
            {"netherrack", "minecraft:netherrack", {"minecraft:nether_gold_ore", "minecraft:nether_quartz_ore"}},
        };
        for (auto const& g : grounds) {
            std::vector<std::string> ores;
            for (auto const& id : gf::tags::members(gf::tags::Registry::Block, std::string("c:ores_in_ground/") + g.name)) ores.push_back(id);
            if (ores.empty())
                for (auto const* id : g.ores) ores.push_back(id);
            std::string ground = g.ground;
            for (auto const& id : gf::tags::members(gf::tags::Registry::Block, std::string("c:ore_bearing_ground/") + g.name)) ground = id;
            for (auto const& ore : ores) {
                // the Bedrock block of each Java id
                Block const* ob = kit::javaBlock(ore);
                Block const* gb = kit::javaBlock(ground);
                if (!ob || !gb) continue;
                if (gf::tags::has(gf::tags::Registry::Block, "twilightforest:mining_core_excluded", ob->getTypeName())) continue;
                m[ob->getTypeName()] = gb->getTypeName();
                if (ob->getTypeName() == "minecraft:redstone_ore") m["minecraft:lit_redstone_ore"] = gb->getTypeName();
                if (ob->getTypeName() == "minecraft:deepslate_redstone_ore") m["minecraft:lit_deepslate_redstone_ore"] = gb->getTypeName();
            }
        }
        return m;
    }();
    return map;
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
    Block const* ore         = nullptr;
    std::string  replacement = "minecraft:stone";
    std::optional<BlockPos> foundPos, basePos;
    Bresenham line(usePos, destPos);
    BlockPos  coord;
    while (line.next(coord)) {
        if (coord.y < region.getMinHeight() || coord.y >= region.getMaxHeight()) continue;
        Block const& search = region.getBlock(coord);
        // the dirt / stone found first is where the ore goes: pushing ores away is a bad idea
        if (!basePos) {
            if (safeReplace(search)) basePos = coord;
        } else if (!foundPos && !search.isAir()) {
            auto it = treeOres().find(search.getTypeName());
            if (it != treeOres().end() && !region.getBlockEntity(coord)) {
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
    int  offX = basePos->x - foundPos->x, offY = basePos->y - foundPos->y, offZ = basePos->z - foundPos->z;
    int  moved = 0;
    auto& dim  = region.getDimension();
    for (auto const& c : vein) {
        BlockPos     replacePos = at(c, offX, offY, offZ);
        Block const& rs         = region.getBlock(replacePos);
        if (!(safeReplace(rs) || kit::isReplaceable(rs) || rs.isAir())) continue;
        game::setBlock(region, c, *repl);
        Vec3 xyz = center(replacePos);
        for (int i = 0; i < 16; ++i) {
            auto& r = coreRandom();
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

/// An item handler (IItemHandler): a vanilla container, an entity's container or a TF container of
/// the inventory contract.
struct Handler {
    std::function<int()>                                 slots;
    std::function<ItemStack(int)>                        get;
    std::function<void(int, ItemStack const&)>           set;
    Vec3                                                 at; ///< where the particles start / end
};

std::optional<Handler> blockHandler(BlockSource& region, BlockPos const& p) {
    Vec3 top{p.x + 0.5f, p.y + 1.9f, p.z + 0.5f}; // Vec3.upFromBottomCenterOf(pos, 1.9)
    if (auto h = gf::inventory::find(region, p); h && h->get && h->set) {
        GlobalPos gp = game::globalPos(region, p);
        int       n  = h->slots;
        auto      inv = std::make_shared<gf::inventory::Handler>(*h);
        return Handler{[n] { return n; }, [inv, gp](int s) { return inv->get(gp, s); }, [inv, gp](int s, ItemStack const& st) { inv->set(gp, s, st); }, top};
    }
    auto* actor = region.getBlockEntity(p);
    if (!actor) return std::nullopt;
    auto* main = actor->_getMainComponent();
    if (!main || !main->getContainer()) return std::nullopt;
    auto& level = region.getLevel();
    int   dim   = static_cast<int>(region.getDimensionId());
    auto  find  = [&level, dim, p]() -> Container* {
        auto* r = game::regionOf(dim);
        if (!r) return nullptr;
        auto* a = r->getBlockEntity(p);
        auto* m = a ? a->_getMainComponent() : nullptr;
        return m ? m->getContainer() : nullptr;
    };
    (void)level;
    return Handler{[find] { auto* c = find(); return c ? c->getContainerSize() : 0; },
                   [find](int s) { auto* c = find(); return c ? c->getItem(s) : game::items::empty(); },
                   [find](int s, ItemStack const& st) {
                       if (auto* c = find()) c->setItem(s, st);
                   },
                   top};
}

std::optional<Handler> entityHandler(Actor& e) {
    auto comp = e.getEntityContext().tryGetComponent<ContainerComponent>();
    if (!comp) return std::nullopt;
    Container* c = &comp->mContainer.get();
    auto       pos = e.getPosition();
    float      h   = e.getAABB().max.y - e.getAABB().min.y;
    return Handler{[c] { return c->getContainerSize(); }, [c](int s) { return c->getItem(s); }, [c](int s, ItemStack const& st) { c->setItem(s, st); },
                   Vec3{pos.x, e.getAABB().min.y + h + 0.9f, pos.z}};
}

bool sortable(Actor& e) { return e.isAlive() && gf::tags::has(gf::tags::Registry::EntityType, "twilightforest:sortable_entities", e.getTypeName()); }

int sortingEffect(BlockSource& region, BlockPos const& pos) {
    int const range = coreRange(Core::Sorting);
    std::vector<Handler> inputs, outputs;
    for (int dx = -range; dx <= range; ++dx)
        for (int dy = -range; dy <= range; ++dy)
            for (int dz = -range; dz <= range; ++dz) {
                if (dx == 0 && dy == 0 && dz == 0) continue;
                BlockPos p = at(pos, dx, dy, dz);
                if (p.y < region.getMinHeight() || p.y >= region.getMaxHeight() || !region.hasChunksAt(p, 0, false)) continue;
                auto h = blockHandler(region, p);
                if (!h) continue;
                if (std::abs(dx) <= 2 && std::abs(dy) <= 2 && std::abs(dz) <= 2) inputs.push_back(std::move(*h));
                else outputs.push_back(std::move(*h));
            }
    std::vector<Actor*> used;
    AABB inner{Vec3{pos.x - 2.0f, pos.y - 2.0f, pos.z - 2.0f}, Vec3{pos.x + 3.0f, pos.y + 3.0f, pos.z + 3.0f}};
    for (auto* e : region.getEntities(inner, 0.0f)) {
        if (!e || !sortable(*e)) continue;
        if (auto h = entityHandler(*e)) {
            inputs.push_back(std::move(*h));
            used.push_back(e);
        }
    }
    if (inputs.empty()) return 0;
    AABB outer{Vec3{pos.x - 16.0f, pos.y - 16.0f, pos.z - 16.0f}, Vec3{pos.x + 17.0f, pos.y + 17.0f, pos.z + 17.0f}};
    for (auto* e : region.getEntities(outer, 0.0f)) {
        if (!e || !sortable(*e) || std::find(used.begin(), used.end(), e) != used.end()) continue;
        if (auto h = entityHandler(*e)) outputs.push_back(std::move(*h));
    }
    if (outputs.empty()) return 0;
    int   sorted = 0;
    auto& random = coreRandom();
    for (auto& input : inputs) {
        int n = input.slots();
        for (int i = 0; i < n; ++i) {
            ItemStack inputStack = input.get(i);
            if (game::items::isEmpty(inputStack)) continue;
            // the outputs holding this item, by how many they hold (TF keys a map by the count)
            std::map<int, Handler*, std::greater<int>> byCount;
            for (auto& out : outputs) {
                int count = 0;
                for (int j = 0, m = out.slots(); j < m; ++j) {
                    ItemStack s = out.get(j);
                    if (!game::items::isEmpty(s) && game::items::sameItem(s, inputStack)) count += game::items::countOf(s);
                }
                if (count > 0) byCount[count] = &out;
            }
            bool transferred = false;
            for (auto& [count, out] : byCount) {
                int first = -1;
                for (int j = 0, m = out->slots(); j < m; ++j) {
                    ItemStack s = out->get(j);
                    if (first == -1 && game::items::isEmpty(s)) {
                        first = j; // the first empty slot, in case no stack has room
                    } else if (!game::items::isEmpty(s) && game::items::sameItemSameData(inputStack, s) && game::items::countOf(s) < s.getMaxStackSize()) {
                        first = j;
                        break;
                    }
                }
                if (first == -1) continue;
                // extract one, insert it
                ItemStack one  = game::items::withCount(inputStack, 1);
                int       left = game::items::countOf(inputStack) - 1;
                input.set(i, left > 0 ? game::items::withCount(inputStack, left) : game::items::empty());
                ItemStack dest = out->get(first);
                out->set(first, game::items::isEmpty(dest) ? one : game::items::withCount(dest, game::items::countOf(dest) + 1));
                transferred = true;
                ++sorted;
                Vec3   xyz  = out->at;
                Vec3   diff{input.at.x - xyz.x, input.at.y - xyz.y, input.at.z - xyz.z};
                double len  = std::sqrt(static_cast<double>(diff.x) * diff.x + static_cast<double>(diff.y) * diff.y + static_cast<double>(diff.z) * diff.z);
                double x    = diff.x - 0.25 + random.nextDouble() * 0.5;
                double y    = diff.y - 1.75 + random.nextDouble() * 0.5;
                double z    = diff.z - 0.25 + random.nextDouble() * 0.5;
                double s    = len > 0 ? 1.0 / len : 0.0;
                fx::particle(region.getDimension(), "twilightforest:sorting_particle", xyz, Vec3{static_cast<float>(x * s), static_cast<float>(y * s), static_cast<float>(z * s)});
                break;
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
    switch (core) {
    case Core::Time:
        return cfg.timeCoreRange;
    case Core::Transformation:
        return cfg.transformationCoreRange;
    case Core::Mining:
        return cfg.miningCoreRange;
    case Core::Sorting:
        return cfg.sortingCoreRange;
    }
    return 0;
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
        auto* region = game::regionOf(gp.dim);
        BlockPos bp  = game::blockPos(gp);
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
    }
    game::db::onWorldReady([] {
        state().loaded = false;
    });
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
