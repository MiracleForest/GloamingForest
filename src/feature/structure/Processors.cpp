// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/init/TFStructureProcessors.java
//   src/main/java/gloamingforest/world/components/processors/BoxCuttingProcessor.java
//   src/main/java/gloamingforest/world/components/processors/CobbleVariants.java
//   src/main/java/gloamingforest/world/components/processors/InfestBlocksProcessor.java
//   src/main/java/gloamingforest/world/components/processors/NagastoneVariants.java
//   src/main/java/gloamingforest/world/components/processors/SmartGrassProcessor.java
//   src/main/java/gloamingforest/world/components/processors/SmoothStoneVariants.java
//   src/main/java/gloamingforest/world/components/processors/SoftReplaceProcessor.java
//   src/main/java/gloamingforest/world/components/processors/SpawnerProcessor.java
//   src/main/java/gloamingforest/world/components/processors/StateTransfiguringProcessor.java
//   src/main/java/gloamingforest/world/components/processors/StoneBricksVariants.java
//   src/main/java/gloamingforest/world/components/processors/TargetedRotProcessor.java
//   src/main/java/gloamingforest/world/components/processors/UpdateMarkingProcessor.java
//   src/main/java/gloamingforest/world/components/processors/VerticalDecayProcessor.java
//   src/main/java/gloamingforest/world/components/processors/WoodMultiPaletteSwizzle.java
//   src/main/java/gloamingforest/world/components/processors/WoodPaletteSwizzle.java
//   src/main/java/gloamingforest/world/components/feature/templates/GraveyardFeature.java (WebTemplateProcessor)
//   src/main/java/gloamingforest/world/components/structures/courtyard/CourtyardTerraceTemplateProcessor.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// upstream's 17 structure processors plus the vanilla processor types upstream's data and templates use
// (rule, block_ignore, block_rot, gravity, protected_blocks, jigsaw_replacement, nop). Every
// processor uses StructurePlaceSettings#getRandom(pos) (processors::randomAt) exactly as Java does,
// so outcomes depend only on positions and the seed.
#include "feature/structure/Internal.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include "mc/world/level/block/Block.h"

#include "mod/Log.h"

namespace gf::structure::processors {

namespace {

thread_local BlockPos tPivot{0, 0, 0};

std::shared_mutex                 gListsMutex;
std::map<std::string, Json>       gLists; ///< in-memory processor lists

BlockInfo replaced(BlockInfo const& current, Block const& block, Json data = Json()) { return BlockInfo{current.pos, &block, std::move(data)}; }

/// Java's long multiplication (wraps on overflow; C++ signed overflow is undefined).
std::int64_t javaMul(std::int64_t a, std::int64_t b) { return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b)); }

bool isBlock(BlockInfo const& info, std::string_view javaId) { return info.block && blocks::is(*info.block, javaId); }

/// The Bedrock block of a Java default state, cached per id.
Block const* def(std::string_view javaId) { return blocks::fromJava(javaId); }

/// "Block.CODEC" lists: ids (strings) or {"Name": ...} states; matched by block id.
bool listHasBlock(Json const& list, Block const& block) {
    if (!list.is_array()) return false;
    for (auto const& e : list) {
        std::string id = e.is_string() ? e.get<std::string>() : e.value("Name", std::string{});
        if (!id.empty() && blocks::is(block, id)) return true;
    }
    return false;
}

// ---- RuleTest / PosRuleTest (vanilla) ---------------------------------------------------------------

bool ruleTest(Json const& test, Block const& state, RandomSource& random) {
    auto type = test.value("predicate_type", std::string("minecraft:always_true"));
    if (type == "minecraft:always_true") return true;
    if (type == "minecraft:block_match") return blocks::is(state, test.value("block", std::string{}));
    if (type == "minecraft:blockstate_match") {
        auto const* b = blocks::fromJavaState(test["block_state"]);
        return b && b == &state;
    }
    if (type == "minecraft:tag_match") return blocks::inTag(state, test.value("tag", std::string{}));
    if (type == "minecraft:random_block_match")
        return blocks::is(state, test.value("block", std::string{})) && random.nextFloat() < test.value("probability", 1.0f);
    if (type == "minecraft:random_blockstate_match") {
        auto const* b = blocks::fromJavaState(test["block_state"]);
        return b == &state && random.nextFloat() < test.value("probability", 1.0f);
    }
    warnOnce("ruletest:" + type, "structure: unsupported rule test " + type);
    return false;
}

float clampedLerp(float a, float b, float t) { return t < 0 ? a : t > 1 ? b : a + t * (b - a); }
float inverseLerp(float v, float a, float b) { return (v - a) / (b - a); }

bool posRuleTest(Json const& test, BlockPos const& local, BlockPos const& world, BlockPos const& structure, RandomSource& random) {
    auto type = test.value("predicate_type", std::string("minecraft:always_true"));
    if (type == "minecraft:always_true") return true;
    float minChance = test.value("min_chance", 0.0f), maxChance = test.value("max_chance", 0.0f);
    int   minDist = test.value("min_dist", 0), maxDist = test.value("max_dist", 0);
    if (type == "minecraft:linear_pos") {
        int   i = std::abs(world.x - structure.x) + std::abs(world.y - structure.y) + std::abs(world.z - structure.z);
        float f = random.nextFloat();
        return f <= clampedLerp(minChance, maxChance, inverseLerp(static_cast<float>(i), static_cast<float>(minDist), static_cast<float>(maxDist)));
    }
    if (type == "minecraft:axis_aligned_linear_pos") {
        auto  axis = test.value("axis", std::string("y"));
        float fx   = axis == "x" ? static_cast<float>(std::abs(world.x - structure.x)) : 0.f;
        float fy   = axis == "y" ? static_cast<float>(std::abs(world.y - structure.y)) : 0.f;
        float fz   = axis == "z" ? static_cast<float>(std::abs(world.z - structure.z)) : 0.f;
        int   i    = static_cast<int>(fx + fy + fz);
        float f3   = random.nextFloat();
        return f3 <= clampedLerp(minChance, maxChance, inverseLerp(static_cast<float>(i), static_cast<float>(minDist), static_cast<float>(maxDist)));
    }
    warnOnce("posruletest:" + type, "structure: unsupported position rule test " + type);
    (void)local;
    return false;
}

bool processorRuleTest(Json const& rule, Block const& input, Block const& existing, BlockPos const& local, BlockPos const& world,
                       BlockPos const& structure, RandomSource& random) {
    return ruleTest(rule.value("input_predicate", Json::object()), input, random) &&
           ruleTest(rule.value("location_predicate", Json::object()), existing, random) &&
           posRuleTest(rule.value("position_predicate", Json::object()), local, world, structure, random);
}

/// A processor's output data with the colour of a Java bed state (Bedrock has one bed block).
Json withBedColor(Json data, Json const& state) {
    std::string name = state.is_string() ? state.get<std::string>() : state.is_object() ? state.value("Name", std::string{}) : std::string{};
    int         c    = blocks::bedColor(name);
    if (c < 0) return data;
    if (!data.is_object()) data = Json::object();
    data[blockentities::kBedColorKey] = c;
    return data;
}

/// RuleBlockEntityModifier#apply.
Json outputTag(Json const& rule, RandomSource& random, Json const& data) {
    if (!rule.contains("block_entity_modifier")) return data;
    auto const& mod  = rule["block_entity_modifier"];
    auto        type = mod.value("type", std::string("minecraft:passthrough"));
    if (type == "minecraft:passthrough") return data;
    if (type == "minecraft:clear") return Json();
    if (type == "minecraft:append_loot") {
        Json d      = data.is_object() ? data : Json::object();
        d["marker"] = "loot";
        d["table"]  = mod.value("loot_table", std::string{});
        d["seed"]   = random.nextLong();
        return d;
    }
    if (type == "minecraft:append_static") {
        Json d = data.is_object() ? data : Json::object();
        if (mod.contains("data") && mod["data"].is_object()) d.update(mod["data"]);
        return d;
    }
    return data;
}

// ---- upstream processors -----------------------------------------------------------------------------------

std::optional<BlockInfo> boxCutting(Region&, BlockInfo const&, BlockInfo const& current, TemplateSettings const&, Json const& config) {
    for (auto const& b : config.value("boxes", Json::array())) {
        auto box = boxFromJson(b);
        if (box.contains(current.pos.x, current.pos.y, current.pos.z)) return std::nullopt;
    }
    return current;
}

std::optional<BlockInfo> cobbleVariants(Region&, BlockInfo const&, BlockInfo const& current, TemplateSettings const&, Json const&) {
    RandomSource& random = randomAt(current.pos);
    random.setSeed(javaMul(random.nextLong(), 2));
    if (isBlock(current, "minecraft:cobblestone") && random.nextBoolean()) return replaced(current, blocks::java("minecraft:mossy_cobblestone"));
    if (isBlock(current, "minecraft:cobblestone_stairs") && random.nextBoolean())
        return replaced(current, blocks::transferStates(*current.block, blocks::java("minecraft:mossy_cobblestone_stairs")));
    if (isBlock(current, "minecraft:cobblestone_slab") && random.nextBoolean())
        return replaced(current, blocks::transferStates(*current.block, blocks::java("minecraft:mossy_cobblestone_slab")));
    if (isBlock(current, "minecraft:cobblestone_wall") && random.nextBoolean())
        return replaced(current, blocks::transferStates(*current.block, blocks::java("minecraft:mossy_cobblestone_wall")));
    return current;
}

std::optional<BlockInfo> infestBlocks(Region&, BlockInfo const&, BlockInfo const& current, TemplateSettings const&, Json const&) {
    // settings.getRandom(pos.below(-10)): ten blocks up
    RandomSource& random = randomAt(BlockPos{current.pos.x, current.pos.y + 10, current.pos.z});
    random.setSeed(javaMul(random.nextLong(), 2));
    static std::vector<std::pair<char const*, char const*>> const conversions{
        {"minecraft:stone", "minecraft:infested_stone"},
        {"minecraft:cobblestone", "minecraft:infested_cobblestone"},
        {"minecraft:stone_bricks", "minecraft:infested_stone_bricks"},
        {"minecraft:mossy_stone_bricks", "minecraft:infested_mossy_stone_bricks"},
        {"minecraft:cracked_stone_bricks", "minecraft:infested_cracked_stone_bricks"},
        {"minecraft:chiseled_stone_bricks", "minecraft:infested_chiseled_stone_bricks"},
    };
    char const* replacement = nullptr;
    for (auto const& [from, to] : conversions)
        if (isBlock(current, from)) replacement = to;
    if (!replacement || random.nextFloat() > 1.0f / 12.0f) return current;
    return replaced(current, blocks::java(replacement));
}

std::optional<BlockInfo> nagastoneVariants(Region&, BlockInfo const&, BlockInfo const& current, TemplateSettings const&, Json const&) {
    RandomSource& random = randomAt(current.pos);
    random.setSeed(javaMul(random.nextLong(), 5));
    auto swap = [&](char const* mossy, char const* cracked) {
        return replaced(current, blocks::transferStates(*current.block, blocks::java(random.nextBoolean() ? mossy : cracked)));
    };
    if (isBlock(current, "gloamingforest:etched_nagastone") && random.nextBoolean())
        return swap("gloamingforest:mossy_etched_nagastone", "gloamingforest:cracked_etched_nagastone");
    if (isBlock(current, "gloamingforest:nagastone_pillar") && random.nextBoolean())
        return swap("gloamingforest:mossy_nagastone_pillar", "gloamingforest:cracked_nagastone_pillar");
    if (isBlock(current, "gloamingforest:nagastone_stairs_left") && random.nextBoolean())
        return swap("gloamingforest:mossy_nagastone_stairs_left", "gloamingforest:cracked_nagastone_stairs_left");
    if (isBlock(current, "gloamingforest:nagastone_stairs_right") && random.nextBoolean())
        return swap("gloamingforest:mossy_nagastone_stairs_right", "gloamingforest:cracked_nagastone_stairs_right");
    return current;
}

std::optional<BlockInfo> smartGrass(Region& region, BlockInfo const& original, BlockInfo const& current, TemplateSettings const&, Json const&) {
    if (!original.block || !blocks::is(*original.block, "minecraft:grass_block")) return current;
    BlockPos above{current.pos.x, current.pos.y + 1, current.pos.z};
    if (blocks::inTag(region.getBlock(current.pos), "minecraft:dirt") || !region.getBlock(above).isAir()) return std::nullopt;
    for (auto d : {Orientation::North, Orientation::South, Orientation::East, Orientation::West}) {
        Block const& at = region.getBlock(BlockPos{current.pos.x + stepX(d), current.pos.y, current.pos.z + stepZ(d)});
        if (blocks::is(at, "minecraft:podzol")) return replaced(current, blocks::java("minecraft:podzol"));
        if (blocks::is(at, "minecraft:grass_block")) return current;
        if (blocks::is(at, "minecraft:mycelium")) return replaced(current, blocks::java("minecraft:mycelium"));
        if (blocks::is(at, "minecraft:dirt_path")) return replaced(current, blocks::java("minecraft:dirt_path"));
        if (blocks::is(at, "minecraft:coarse_dirt")) return replaced(current, blocks::java("minecraft:coarse_dirt"));
        if (blocks::is(at, "gloamingforest:uberous_soil")) return replaced(current, blocks::java("gloamingforest:uberous_soil"));
    }
    return current;
}

std::optional<BlockInfo> smoothStoneVariants(Region&, BlockInfo const&, BlockInfo const& current, TemplateSettings const&, Json const&) {
    RandomSource& random = randomAt(current.pos);
    random.setSeed(javaMul(random.nextLong(), 4));
    if (isBlock(current, "minecraft:smooth_stone_slab") && random.nextBoolean())
        return replaced(current, blocks::transferStates(*current.block, blocks::java("minecraft:cobblestone_slab")));
    if (isBlock(current, "minecraft:smooth_stone") && random.nextBoolean()) return replaced(current, blocks::java("minecraft:cobblestone"));
    return current;
}

std::optional<BlockInfo> softReplace(Region& region, BlockInfo const&, BlockInfo const& current, TemplateSettings const&, Json const&) {
    Block const& at = region.getBlock(current.pos);
    if (blocks::canBeReplaced(at) || blocks::inTag(at, "gloamingforest:worldgen_replaceables")) return current;
    if (blocks::isPartial(at) && current.block && !blocks::isPartial(*current.block)) return current;
    return std::nullopt;
}

/// EntityType#getWidth of 1.21.1 (vanilla) and upstream 4.8 (TFEntities) mobs, for SpawnerProcessor's
/// entity_width_max; 0 for an unknown type (no rescale, as upstream's orElse(0f)).
float entityWidth(std::string const& id) {
    static std::unordered_map<std::string, float> const widths{
        // vanilla
        {"minecraft:zombie", 0.6f}, {"minecraft:husk", 0.6f}, {"minecraft:drowned", 0.6f}, {"minecraft:zombie_villager", 0.6f},
        {"minecraft:skeleton", 0.6f}, {"minecraft:stray", 0.6f}, {"minecraft:bogged", 0.6f}, {"minecraft:wither_skeleton", 0.7f},
        {"minecraft:spider", 1.4f}, {"minecraft:cave_spider", 0.7f}, {"minecraft:creeper", 0.6f}, {"minecraft:blaze", 0.6f},
        {"minecraft:silverfish", 0.4f}, {"minecraft:endermite", 0.4f}, {"minecraft:enderman", 0.6f}, {"minecraft:witch", 0.6f},
        {"minecraft:slime", 0.52f}, {"minecraft:magma_cube", 0.52f}, {"minecraft:ghast", 4.0f}, {"minecraft:pillager", 0.6f},
        {"minecraft:vindicator", 0.6f}, {"minecraft:evoker", 0.6f}, {"minecraft:phantom", 0.9f}, {"minecraft:piglin", 0.6f},
        {"minecraft:zombified_piglin", 0.6f}, {"minecraft:hoglin", 1.3964844f}, {"minecraft:guardian", 0.85f}, {"minecraft:breeze", 0.6f},
        {"minecraft:wolf", 0.6f}, {"minecraft:pig", 0.9f}, {"minecraft:cow", 0.9f}, {"minecraft:sheep", 0.9f}, {"minecraft:chicken", 0.4f},
        {"minecraft:bat", 0.5f}, {"minecraft:iron_golem", 1.4f}, {"minecraft:snow_golem", 0.7f}, {"minecraft:villager", 0.6f},
        // Gloaming Forest
        {"gloamingforest:adherent", 0.8f}, {"gloamingforest:alpha_yeti", 3.8f}, {"gloamingforest:armored_giant", 2.4f},
        {"gloamingforest:bighorn_sheep", 0.9f}, {"gloamingforest:blockchain_goblin", 0.9f}, {"gloamingforest:boar", 0.9f},
        {"gloamingforest:boggard", 0.8f}, {"gloamingforest:carminite_broodling", 0.7f}, {"gloamingforest:carminite_ghastguard", 4.0f},
        {"gloamingforest:carminite_ghastling", 1.1f}, {"gloamingforest:carminite_golem", 1.4f}, {"gloamingforest:death_tome", 0.75f},
        {"gloamingforest:deer", 0.7f}, {"gloamingforest:dwarf_rabbit", 0.4f}, {"gloamingforest:fire_beetle", 1.1f},
        {"gloamingforest:giant_miner", 2.4f}, {"gloamingforest:harbinger_cube", 1.9f}, {"gloamingforest:hedge_spider", 1.4f},
        {"gloamingforest:helmet_crab", 0.8f}, {"gloamingforest:hostile_wolf", 0.6f}, {"gloamingforest:hydra", 16.0f},
        {"gloamingforest:ice_crystal", 0.6f}, {"gloamingforest:king_spider", 1.6f}, {"gloamingforest:knight_phantom", 1.25f},
        {"gloamingforest:kobold", 0.8f}, {"gloamingforest:lich", 0.8f}, {"gloamingforest:lich_minion", 0.6f},
        {"gloamingforest:lower_goblin_knight", 0.7f}, {"gloamingforest:loyal_zombie", 0.6f}, {"gloamingforest:maze_slime", 0.52f},
        {"gloamingforest:minoshroom", 1.49f}, {"gloamingforest:minotaur", 0.6f}, {"gloamingforest:mist_wolf", 1.4f},
        {"gloamingforest:mosquito_swarm", 0.7f}, {"gloamingforest:naga", 2.0f}, {"gloamingforest:penguin", 0.5f},
        {"gloamingforest:pinch_beetle", 1.2f}, {"gloamingforest:quest_ram", 1.25f}, {"gloamingforest:raven", 0.3f},
        {"gloamingforest:redcap", 0.9f}, {"gloamingforest:redcap_sapper", 0.9f}, {"gloamingforest:rising_zombie", 0.6f},
        {"gloamingforest:roving_cube", 1.2f}, {"gloamingforest:skeleton_druid", 0.6f}, {"gloamingforest:slime_beetle", 0.9f},
        {"gloamingforest:snow_guardian", 0.6f}, {"gloamingforest:snow_queen", 0.7f}, {"gloamingforest:squirrel", 0.3f},
        {"gloamingforest:stable_ice_core", 0.8f}, {"gloamingforest:swarm_spider", 0.8f}, {"gloamingforest:tiny_bird", 0.3f},
        {"gloamingforest:towerwood_borer", 0.4f}, {"gloamingforest:troll", 1.4f}, {"gloamingforest:unstable_ice_core", 0.8f},
        {"gloamingforest:upper_goblin_knight", 1.1f}, {"gloamingforest:ur_ghast", 14.0f}, {"gloamingforest:winter_wolf", 1.4f},
        {"gloamingforest:wraith", 0.6f}, {"gloamingforest:yeti", 1.4f},
    };
    auto it = widths.find(id);
    return it == widths.end() ? 0.0f : it->second;
}

/// InclusiveRange<Integer> of SpawnData.CustomSpawnRules (ExtraCodecs.intervalCodec: [min, max]
/// or {min_inclusive, max_inclusive}; a single number is both ends), clamped to 0..15; the
/// default is the whole light range.
Json lightRange(Json const& v) {
    int lo = 0, hi = 15;
    if (v.is_number_integer()) lo = hi = v.get<int>();
    else if (v.is_array() && v.size() == 2 && v[0].is_number_integer() && v[1].is_number_integer()) {
        lo = v[0].get<int>();
        hi = v[1].get<int>();
    } else if (v.is_object()) {
        lo = v.value("min_inclusive", 0);
        hi = v.value("max_inclusive", 15);
    }
    return Json::array({std::clamp(lo, 0, 15), std::clamp(hi, 0, 15)});
}

std::optional<BlockInfo> spawnerProcessor(Region&, BlockInfo const&, BlockInfo const& current, TemplateSettings const&, Json const& config) {
    // a template's spawner always carries block entity data (vanilla saves it with the block)
    if (!current.block) return current;
    if (!blocks::is(*current.block, "minecraft:spawner") && !blocks::is(*current.block, "gloamingforest:sinister_spawner")) return current;
    BlockInfo out = current;
    if (!out.data.is_object()) out.data = Json{{"marker", "spawner"}};
    if (!out.data.contains("marker")) out.data["marker"] = "spawner";
    if (config.contains("range")) out.data["SpawnRange"] = config["range"].get<int>();
    // nbtInfo.getShort("MinSpawnDelay"): a spawner's saved data has BaseSpawner's 200 unless set
    if (config.contains("start_delay_factor"))
        out.data["Delay"] = pure::javaRound(static_cast<float>(out.data.value("MinSpawnDelay", 200)) * config["start_delay_factor"].get<float>());
    // `getList("SpawnData", TAG_COMPOUND).isEmpty()` is always true for the compound SpawnData, so
    // upstream always replaces the spawn data: SimpleWeightedRandomList<SpawnData>#getRandomValue(settings.getRandom(pos))
    auto const& entries = config.value("entities", Json::array());
    int         total   = 0;
    for (auto const& e : entries) total += e.value("weight", 1);
    if (total > 0) {
        int pick = randomAt(current.pos).nextInt(total);
        for (auto const& e : entries) {
            pick -= e.value("weight", 1);
            if (pick >= 0) continue;
            auto const& data = e.contains("data") ? e["data"] : e;
            auto const& ent  = data.contains("entity") ? data["entity"] : data;
            std::string id   = ent.is_object() ? ent.value("id", std::string{}) : ent.get<std::string>();
            out.data["entity"] = id;
            out.data.erase("entity_scale");
            out.data.erase("spawn_rules");
            // SpawnData.custom_spawn_rules (SpawnerProcessor.compile always gives block light
            // 0..7, sky light 0..15): BaseSpawner checks them instead of the mob's spawn rules
            if (data.is_object() && data.contains("custom_spawn_rules") && data["custom_spawn_rules"].is_object()) {
                auto const& rules         = data["custom_spawn_rules"];
                out.data["spawn_rules"] = Json{{"block_light", lightRange(rules.value("block_light_limit", Json()))},
                                               {"sky_light", lightRange(rules.value("sky_light_limit", Json()))}};
            }
            // entity_width_max: a generic.scale attribute shrinking wider mobs to the width
            float maxWidth = config.value("entity_width_max", 0.0f);
            if (maxWidth > 0) {
                float width = entityWidth(id);
                float scale = (width == 0 || width < maxWidth) ? 1.0f : maxWidth / width; // rescaleToFitWidth
                if (std::isfinite(scale) && scale != 1.0f) out.data["entity_scale"] = scale;
            }
            break;
        }
    }
    return out;
}

std::optional<BlockInfo> stateTransfiguring(Region& region, BlockInfo const& original, BlockInfo const& current, TemplateSettings const&,
                                            Json const& config) {
    if (!current.block) return current;
    Block const& state = region.getBlock(current.pos);
    JavaRandom   random(pure::mthGetSeed(current.pos.x, current.pos.y, current.pos.z));
    std::int64_t i = random.nextLong();
    for (auto const& rule : config.value("rules", Json::array())) {
        random.setSeed(javaMul(i, 3));
        i = static_cast<std::int64_t>(static_cast<std::uint64_t>(i) + 115u);
        if (processorRuleTest(rule, *current.block, state, original.pos, current.pos, tPivot, random)) {
            auto const* out = blocks::fromJavaState(rule["output_state"]);
            if (!out) return current;
            return BlockInfo{current.pos, &blocks::transferStates(*current.block, *out), withBedColor(outputTag(rule, random, current.data), rule["output_state"])};
        }
    }
    return current;
}

std::optional<BlockInfo> stoneBricksVariants(Region&, BlockInfo const&, BlockInfo const& current, TemplateSettings const&, Json const&) {
    RandomSource& random = randomAt(current.pos);
    random.setSeed(javaMul(random.nextLong(), 3));
    if (isBlock(current, "minecraft:stone_bricks") && random.nextBoolean())
        return replaced(current, blocks::java(random.nextBoolean() ? "minecraft:mossy_stone_bricks" : "minecraft:cracked_stone_bricks"));
    if (isBlock(current, "minecraft:stone_brick_stairs") && random.nextBoolean())
        return replaced(current, blocks::transferStates(*current.block, blocks::java("minecraft:mossy_stone_brick_stairs")));
    if (isBlock(current, "minecraft:stone_brick_slab") && random.nextBoolean())
        return replaced(current, blocks::transferStates(*current.block, blocks::java("minecraft:mossy_stone_brick_slab")));
    if (isBlock(current, "minecraft:stone_brick_wall") && random.nextBoolean())
        return replaced(current, blocks::transferStates(*current.block, blocks::java("minecraft:mossy_stone_brick_wall")));
    return current;
}

std::optional<BlockInfo> targetedRot(Region&, BlockInfo const&, BlockInfo const& current, TemplateSettings const&, Json const& config) {
    bool target = false;
    for (auto const& s : config.value("blocks_to_rot", Json::array())) {
        auto const* b = blocks::fromJavaState(s);
        if (b && b == current.block) target = true;
    }
    if (!target) return current;
    // BlockRotProcessor#processBlock (no rottable_blocks tag)
    float integrity = config.value("integrity", 1.0f);
    return randomAt(current.pos).nextFloat() <= integrity ? std::optional<BlockInfo>(current) : std::nullopt;
}

std::optional<BlockInfo> updateMarking(Region& region, BlockInfo const&, BlockInfo const& current, TemplateSettings const&, Json const& config) {
    // level.getChunk(pos).markPosForPostprocessing(pos): once the chunk is complete (Java: FULL
    // status, LevelChunk#postProcessGeneration) the block there takes its shape from its
    // neighbours (Block.updateFromNeighbourShapes); here a queued job runs once the chunk is
    // loaded, after the whole structure stands. Only for the blocks this placement places: a
    // shared-random placement processes the blocks outside its clip too, and the placement of
    // their own chunk marks them (Java marks them twice)
    if (current.block && insideCurrentClip(current.pos) && listHasBlock(config.value("mark_updates", Json::array()), *current.block))
        blockentities::updateShape(region, current.pos);
    return current;
}

std::optional<BlockInfo> verticalDecay(Region&, BlockInfo const&, BlockInfo const& current, TemplateSettings const&, Json const& config) {
    if (!current.block || !listHasBlock(config.value("decay_blocks", Json::array()), *current.block)) return current;
    int      lookDown = blocks::id(*current.block).ends_with("_banister") ? -1 : 0;
    BlockPos randomAtPos{current.pos.x, current.pos.y + lookDown, current.pos.z};
    if (randomAt(randomAtPos).nextFloat() < config.value("decay_chance", 0.0f)) return std::nullopt;
    return current;
}

std::optional<BlockInfo> web(Region&, BlockInfo const&, BlockInfo const& current, TemplateSettings const&, Json const&) {
    // WebTemplateProcessor: the random is seeded with the template offset (upstream's "pos" argument),
    // and the cobweb goes to that offset, exactly as written in upstream.
    if (isBlock(current, "minecraft:grass_block")) return current;
    BlockPos offset = currentOffset();
    if (randomAt(offset).nextInt(5) == 0) return BlockInfo{offset, &blocks::java("minecraft:cobweb"), Json()};
    return current;
}

std::optional<BlockInfo> courtyardTerrace(Region& region, BlockInfo const&, BlockInfo const& current, TemplateSettings const&, Json const&) {
    if (!current.block) return current;
    // Java's sandstone_slab, its double form being Bedrock's separate double slab block
    bool dbl  = blocks::id(*current.block) == "minecraft:sandstone_double_slab";
    bool slab = dbl || isBlock(current, "minecraft:sandstone_slab");
    if (!slab) return current;
    Block const& at = region.getBlock(current.pos);
    if (dbl) {
        Block const* const replaceToSlab[] = {def("minecraft:stone_bricks"), def("minecraft:mossy_stone_bricks"), def("minecraft:cracked_stone_bricks"),
                                               def("minecraft:stone_brick_slab"), def("minecraft:mossy_stone_brick_slab")};
        for (auto const* b : replaceToSlab)
            if (b == &at) return replaced(current, blocks::java("minecraft:stone_brick_slab"));
        if (at.isAir()) return std::nullopt;
        return replaced(current, blocks::java("minecraft:stone_bricks"));
    }
    if (at.isAir()) return std::nullopt;
    return replaced(current, blocks::java("minecraft:stone_brick_slab"));
}

std::optional<BlockInfo> woodSwizzle(Region&, BlockInfo const&, BlockInfo const& current, TemplateSettings const&, Json const& config) {
    if (!current.block) return current;
    auto const* b = palettes::swizzle(config.value("target_palette", std::string{}), config.value("replacement_palette", std::string{}), *current.block);
    return b ? replaced(current, *b, current.data) : current;
}

std::optional<BlockInfo> woodMultiswizzle(Region&, BlockInfo const&, BlockInfo const& current, TemplateSettings const&, Json const& config) {
    if (!current.block) return current;
    for (auto const& pair : config.value("palettes", Json::array())) {
        std::string target, replacement;
        if (pair.is_array() && pair.size() == 2) {
            target      = pair[0].get<std::string>();
            replacement = pair[1].get<std::string>();
        } else if (pair.is_object()) {
            target      = pair.value("first", std::string{});
            replacement = pair.value("second", std::string{});
        }
        if (auto const* b = palettes::swizzle(target, replacement, *current.block)) return replaced(current, *b, current.data);
    }
    return current;
}

// ---- vanilla processors --------------------------------------------------------------------------------

std::optional<BlockInfo> rule(Region& region, BlockInfo const& original, BlockInfo const& current, TemplateSettings const&, Json const& config) {
    if (!current.block) return current;
    JavaRandom   random(pure::mthGetSeed(current.pos.x, current.pos.y, current.pos.z));
    Block const& state = region.getBlock(current.pos);
    for (auto const& r : config.value("rules", Json::array())) {
        if (processorRuleTest(r, *current.block, state, original.pos, current.pos, tPivot, random)) {
            auto const* out = blocks::fromJavaState(r["output_state"]);
            if (!out) return current;
            return BlockInfo{current.pos, out, withBedColor(outputTag(r, random, current.data), r["output_state"])};
        }
    }
    return current;
}

std::optional<BlockInfo> blockIgnore(Region&, BlockInfo const&, BlockInfo const& current, TemplateSettings const&, Json const& config) {
    for (auto const& s : config.value("blocks", Json::array())) {
        auto const* b = blocks::fromJavaState(s);
        if (b && current.block && blocks::id(*b) == blocks::id(*current.block)) return std::nullopt;
    }
    return current;
}

std::optional<BlockInfo> blockRot(Region&, BlockInfo const& original, BlockInfo const& current, TemplateSettings const&, Json const& config) {
    if (config.contains("rottable_blocks") && original.block) {
        auto tag = config["rottable_blocks"].is_string() ? config["rottable_blocks"].get<std::string>() : std::string{};
        if (!tag.empty() && tag[0] == '#') tag = tag.substr(1);
        if (!tag.empty() && !blocks::inTag(*original.block, tag)) return current;
    }
    return randomAt(current.pos).nextFloat() <= config.value("integrity", 1.0f) ? std::optional<BlockInfo>(current) : std::nullopt;
}

std::optional<BlockInfo> gravity(Region& region, BlockInfo const& original, BlockInfo const& current, TemplateSettings const&, Json const& config) {
    auto        hm   = config.value("heightmap", std::string("WORLD_SURFACE_WG"));
    auto        type = worldgen::Heightmap::WorldSurfaceWg;
    if (hm == "WORLD_SURFACE") type = worldgen::Heightmap::WorldSurface;
    else if (hm == "OCEAN_FLOOR_WG") type = worldgen::Heightmap::OceanFloorWg;
    else if (hm == "OCEAN_FLOOR") type = worldgen::Heightmap::OceanFloor;
    else if (hm == "MOTION_BLOCKING") type = worldgen::Heightmap::MotionBlocking;
    else if (hm == "MOTION_BLOCKING_NO_LEAVES") type = worldgen::Heightmap::MotionBlockingNoLeaves;
    int j = region.height(type, current.pos.x, current.pos.z) + config.value("offset", 0);
    return BlockInfo{BlockPos{current.pos.x, j + original.pos.y, current.pos.z}, current.block, current.data};
}

std::optional<BlockInfo> protectedBlocks(Region& region, BlockInfo const&, BlockInfo const& current, TemplateSettings const&, Json const& config) {
    auto tag = config.value("value", std::string{});
    if (!tag.empty() && tag[0] == '#') tag = tag.substr(1);
    if (blocks::inTag(region.getBlock(current.pos), tag)) return std::nullopt;
    return current;
}

std::optional<BlockInfo> passthrough(Region&, BlockInfo const&, BlockInfo const& current, TemplateSettings const&, Json const&) { return current; }

thread_local BlockPos                tOffset{0, 0, 0};
thread_local std::optional<BlockBox> tClip;

} // namespace

BlockPos currentPivot() { return tPivot; }
BlockPos currentOffset() { return tOffset; }
void     setCurrentOffset(BlockPos const& offset) { tOffset = offset; }
void     setCurrentClip(std::optional<BlockBox> const& clip) { tClip = clip; }
std::optional<BlockBox> currentClip() { return tClip; }
bool     insideCurrentClip(BlockPos const& pos) { return !tClip || tClip->contains(pos.x, pos.y, pos.z); }

void addList(std::string const& id, Json const& list) {
    std::unique_lock lock(gListsMutex);
    gLists[id] = list;
}

namespace {

void appendList(Json const& list, std::vector<Instance>& out) {
    Json const& procs = list.is_object() ? list.value("processors", Json::array()) : list;
    if (!procs.is_array()) return;
    for (auto const& p : procs) {
        if (!p.is_object()) continue;
        out.push_back(Instance{p.value("processor_type", std::string{}), p});
    }
}

} // namespace

std::vector<Instance> expand(std::vector<std::string> const& entries) {
    std::vector<Instance> out;
    for (auto const& e : entries) {
        if (e.empty() || e == liquids::kIgnoreWaterlogging) continue; // a liquid setting, not a processor
        if (e[0] == '{' || e[0] == '[') {
            try {
                Json j = Json::parse(e);
                if (j.is_object() && j.contains("processor_type")) out.push_back(Instance{j["processor_type"].get<std::string>(), j});
                else appendList(j, out);
            } catch (std::exception const& ex) {
                warnOnce("proc-json:" + e, std::string("structure: bad inline processor JSON: ") + ex.what());
            }
            continue;
        }
        {
            std::shared_lock lock(gListsMutex);
            if (auto it = gLists.find(e); it != gLists.end()) {
                appendList(it->second, out);
                continue;
            }
        }
        if (processorFor(e)) {
            out.push_back(Instance{e, Json{{"processor_type", e}}});
            continue;
        }
        if (auto list = data::load("worldgen/processor_list", e)) {
            appendList(*list, out);
            std::unique_lock lock(gListsMutex);
            gLists[e] = *list;
            continue;
        }
        warnOnce("proc:" + e, "structure: unknown processor or processor list " + e);
    }
    return out;
}

std::optional<BlockInfo> run(Instance const& p, Region& region, BlockInfo const& original, BlockInfo const& current, TemplateSettings const& settings,
                             BlockPos const& pivot) {
    auto const* fn = processorFor(p.type);
    if (!fn) {
        warnOnce("proc:" + p.type, "structure: unknown processor type " + p.type);
        return current;
    }
    tPivot = pivot;
    return (*fn)(region, original, current, settings, p.config);
}

void registerBuiltins() {
    registerProcessor("gloamingforest:box_cutting", boxCutting);
    registerProcessor("gloamingforest:cobble_variants", cobbleVariants);
    registerProcessor("gloamingforest:courtyard_terrace", courtyardTerrace);
    registerProcessor("gloamingforest:infest_blocks", infestBlocks);
    registerProcessor("gloamingforest:nagastone_variants", nagastoneVariants);
    registerProcessor("gloamingforest:smart_grass", smartGrass);
    registerProcessor("gloamingforest:smooth_stone_variants", smoothStoneVariants);
    registerProcessor("gloamingforest:soft_replace", softReplace);
    registerProcessor("gloamingforest:spawner_processor", spawnerProcessor);
    registerProcessor("gloamingforest:state_transfiguring", stateTransfiguring);
    registerProcessor("gloamingforest:stone_brick_variants", stoneBricksVariants);
    registerProcessor("gloamingforest:targeted_rot", targetedRot);
    registerProcessor("gloamingforest:update_marking", updateMarking);
    registerProcessor("gloamingforest:vertical_decay", verticalDecay);
    registerProcessor("gloamingforest:web", web);
    registerProcessor("gloamingforest:wood_multiswizzle", woodMultiswizzle);
    registerProcessor("gloamingforest:wood_swizzle", woodSwizzle);

    registerProcessor("minecraft:rule", rule);
    registerProcessor("minecraft:block_ignore", blockIgnore);
    registerProcessor("minecraft:block_rot", blockRot);
    registerProcessor("minecraft:gravity", gravity);
    registerProcessor("minecraft:protected_blocks", protectedBlocks);
    registerProcessor("minecraft:jigsaw_replacement", passthrough);
    registerProcessor("minecraft:nop", passthrough);
}

} // namespace gf::structure::processors
