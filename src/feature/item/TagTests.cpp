// Selftests of F9 (prefix "item.", no world positions): the tag runtime's nesting (a tag's includes
// bring their data members as well as the bridge's rules, recursively, cycles and NeoForge's
// "remove" included), the vanilla tags of Java 1.21.1 the W0 runtimes read (fences, doors,
// trapdoors, overworld_natural_logs, the river biome tags, replaceable, the structure tags), the
// lock-free snapshot across reloads, and the custom statistics (TF's and Minecraft's).

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/block/Block.h"

#include "feature/item/DataFiles.h"
#include "feature/stats/Stats.h"
#include "feature/stats/StatsInternal.h"
#include "feature/tags/Tags.h"
#include "feature/tags/TagsInternal.h"
#include "feature/tags/VanillaTags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
using gf::tags::Registry;
using JsonDoc = nlohmann::json;
namespace data  = gf::feature::item::data;
namespace items = gf::game::items;

/// A synthetic data tree (files by path below data/) loaded for the duration of a test.
struct Tree {
    std::filesystem::path dir;
    explicit Tree(std::vector<std::pair<std::string, JsonDoc>> const& files) {
        std::random_device rd;
        dir = std::filesystem::temp_directory_path() / ("gf-item-tags-" + std::to_string(rd()));
        for (auto const& [rel, doc] : files) {
            auto p = dir / rel;
            std::filesystem::create_directories(p.parent_path());
            std::ofstream(p) << doc.dump(2);
        }
        data::setRoot(dir);
        data::reloadAll();
    }
    ~Tree() {
        data::setRoot({});
        data::reloadAll();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

bool contains(std::vector<std::string> const& v, std::string const& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

/// "" when `id` is an id of the game's block (item) registry itself, else why not.
std::string unknownBlock(std::string const& id) {
    auto const* b = gf::game::blockWith(id);
    if (!b) return "no block";
    if (auto real = gf::game::blockId(*b); real != id) return "an alias of " + real;
    return "";
}
std::string unknownItem(std::string const& id) {
    auto s = items::make(id);
    if (!s) return "no item";
    if (auto real = items::idOf(*s); real != id) return "an alias of " + real;
    return "";
}

} // namespace

// ---- nesting -----------------------------------------------------------------------------------------

// A tag that includes a bridged vanilla tag holds that tag's data members too, at any depth (the
// W0c bug: #minecraft:overworld_carver_replaceables reached #minecraft:dirt's rule but not the
// uberous soil TF's data adds to minecraft:dirt).
GF_REGISTER_SELFTEST("item.tags_nested_data", [](Context& ctx) {
    // TF's own data, when F1's import is in this build
    if (contains(gf::tags::internal::tagsOf("block"), "twilightforest:worldgen_replaceables")) {
        ctx.check(gf::tags::has(Registry::Block, "twilightforest:carver_replaceables", "twilightforest:uberous_soil"),
                  "twilightforest:carver_replaceables holds uberous_soil (#overworld_carver_replaceables > #dirt)");
        for (auto const* id : {"twilightforest:root", "twilightforest:liveroot_block", "twilightforest:trollsteinn", "twilightforest:uberous_soil"})
            ctx.check(gf::tags::has(Registry::Block, "twilightforest:worldgen_replaceables", id),
                      std::string("twilightforest:worldgen_replaceables holds ") + id + " (#lush_ground_replaceable > #moss_replaceable)");
        for (auto const* id : {"twilightforest:canopy_leaves", "twilightforest:twilight_oak_leaves", "twilightforest:twilight_oak_log", "twilightforest:stripped_canopy_log"})
            ctx.check(gf::tags::has(Registry::Block, "minecraft:lava_pool_stone_cannot_replace", id),
                      std::string("minecraft:lava_pool_stone_cannot_replace holds ") + id + " (#leaves, #logs > #twilightforest:logs)");
        ctx.check(gf::tags::has(Registry::Block, "minecraft:lava_pool_stone_cannot_replace", "twilightforest:liveroot_block"),
                  "and TF's additions to #features_cannot_replace");
        // NeoForge's "remove" of TF's data: the fiery and ice swords take no fire aspect
        ctx.check(gf::tags::has(Registry::Item, "minecraft:enchantable/sword", "twilightforest:fiery_sword"), "the fiery sword is an enchantable sword");
        ctx.check(!gf::tags::has(Registry::Item, "minecraft:enchantable/fire_aspect", "twilightforest:fiery_sword")
                      && !gf::tags::has(Registry::Item, "minecraft:enchantable/fire_aspect", "twilightforest:ice_sword"),
                  "TF's remove: no fire aspect for the fiery and ice swords");
        ctx.check(gf::tags::has(Registry::Item, "minecraft:enchantable/fire_aspect", "twilightforest:knightmetal_sword"), "but for the knightmetal sword");
        ctx.check(!gf::tags::has(Registry::Item, "minecraft:enchantable/vanishing", "twilightforest:phantom_helmet")
                      && gf::tags::has(Registry::Item, "minecraft:enchantable/vanishing", "twilightforest:ironwood_helmet"),
                  "TF's remove: no curse of vanishing for the phantom armour");
    } else {
        ctx.info("TF's tag data is not in this build (F1's import): the synthetic tree stands in");
    }
    Tree tree({
        {"minecraft/tags/block/dirt.json", {{"values", {"gf_test:soil"}}}},
        {"minecraft/tags/block/moss_replaceable.json", {{"values", {"gf_test:root"}}}},
        {"minecraft/tags/block/leaves.json", {{"values", {"gf_test:leaves"}}}},
        {"minecraft/tags/block/logs.json", {{"values", {"#gf_test:logs"}}}},
        {"gf_test/tags/block/logs.json", {{"values", {"gf_test:log"}}}},
        {"gf_test/tags/block/carver.json", {{"values", {"#minecraft:overworld_carver_replaceables", "minecraft:snow_block"}}}},
        {"gf_test/tags/block/worldgen.json", {{"values", {"#minecraft:lush_ground_replaceable", "#minecraft:replaceable_by_trees"}}}},
        {"gf_test/tags/block/deep.json", {{"values", {"#gf_test:worldgen"}}}},
    });
    auto has = [](char const* tag, char const* id) { return gf::tags::has(Registry::Block, tag, id); };
    ctx.check(has("minecraft:overworld_carver_replaceables", "gf_test:soil"), "#overworld_carver_replaceables > #dirt: the data member");
    ctx.check(has("gf_test:carver", "gf_test:soil"), "a data tag including it: too");
    ctx.check(has("gf_test:carver", "minecraft:dirt") && has("gf_test:carver", "minecraft:stone"), "and the bridge's members");
    ctx.check(has("gf_test:carver", "minecraft:snow") && has("gf_test:carver", "minecraft:snow_layer") && !has("gf_test:carver", "minecraft:powder_snow"),
              "snow_block (data) and Java's minecraft:snow layer (overworld_carver_replaceables), not #minecraft:snow's powder snow");
    for (auto const* id : {"gf_test:root", "gf_test:soil", "gf_test:leaves", "minecraft:clay", "minecraft:deepslate", "minecraft:oak_leaves", "minecraft:short_grass"})
        ctx.check(has("gf_test:worldgen", id) && has("gf_test:deep", id), std::string("#lush_ground_replaceable / #replaceable_by_trees, two levels down: ") + id);
    ctx.check(!has("gf_test:worldgen", "minecraft:obsidian"), "obsidian is no worldgen replaceable");
    for (auto const* id : {"gf_test:leaves", "gf_test:log", "minecraft:bedrock", "minecraft:oak_log"})
        ctx.check(has("minecraft:lava_pool_stone_cannot_replace", id), std::string("#lava_pool_stone_cannot_replace (#leaves, #logs > #gf_test:logs) holds ") + id);
    auto members = gf::tags::members(Registry::Block, "gf_test:worldgen");
    for (auto const* id : {"gf_test:root", "gf_test:soil", "gf_test:leaves", "minecraft:clay"})
        ctx.check(contains(members, id), std::string("members lists ") + id + " (" + std::to_string(members.size()) + " members)");
    auto bridged = gf::tags::internal::bridgedOf(Registry::Block, "gf_test:worldgen");
    ctx.check(contains(bridged, "minecraft:dirt") && contains(bridged, "minecraft:leaves") && contains(bridged, "minecraft:base_stone_overworld"),
              "the bridged tags reached through the includes (" + std::to_string(bridged.size()) + ")");
    // the data members: every one the includes reach (bridged tags' data too), none of the bridge's
    auto dataOf = gf::tags::internal::dataMembers("block", "gf_test:worldgen");
    ctx.check(contains(dataOf, "gf_test:soil") && contains(dataOf, "gf_test:root") && contains(dataOf, "gf_test:leaves"), "dataMembers: the nested data members");
    ctx.check(!contains(dataOf, "minecraft:clay"), "but not the bridge's");
});

// Tags that include each other: no endless recursion, both hold every member of the cycle.
GF_REGISTER_SELFTEST("item.tags_cycles", [](Context& ctx) {
    Tree tree({
        {"gf_test/tags/block/cycle_a.json", {{"values", {"#gf_test:cycle_b", "gf_test:a"}}}},
        {"gf_test/tags/block/cycle_b.json", {{"values", {"#gf_test:cycle_c", "gf_test:b"}}}},
        {"gf_test/tags/block/cycle_c.json", {{"values", {"#gf_test:cycle_a", "gf_test:c", "#minecraft:planks"}}}},
        {"gf_test/tags/block/outside.json", {{"values", {"#gf_test:cycle_b"}}}},
        {"gf_test/tags/block/self.json", {{"values", {"#gf_test:self", "gf_test:s"}}}},
        {"minecraft/tags/block/planks.json", {{"values", {"#gf_test:cycle_a"}}}},
    });
    for (auto const* tag : {"gf_test:cycle_a", "gf_test:cycle_b", "gf_test:cycle_c", "gf_test:outside", "minecraft:planks"}) {
        for (auto const* id : {"gf_test:a", "gf_test:b", "gf_test:c", "minecraft:oak_planks"})
            ctx.check(gf::tags::has(Registry::Block, tag, id), std::string(tag) + " holds " + id);
        auto m = gf::tags::internal::dataMembers("block", tag);
        ctx.check(contains(m, "gf_test:a") && contains(m, "gf_test:b") && contains(m, "gf_test:c"), std::string(tag) + ": the cycle's data members");
    }
    ctx.check(gf::tags::has(Registry::Block, "gf_test:self", "gf_test:s"), "a tag including itself keeps its members");
    ctx.check(!gf::tags::has(Registry::Block, "gf_test:cycle_a", "gf_test:s"), "and nothing else");
});

// NeoForge's "remove" entries (TF's data uses them on minecraft:enchantable/*): an id or a #tag taken
// out of the tag, bridge members included, and out of every tag that includes it.
GF_REGISTER_SELFTEST("item.tags_remove", [](Context& ctx) {
    Tree tree({
        {"minecraft/tags/item/swords.json", {{"values", {"gf_test:fiery", "gf_test:plain"}}}},
        {"minecraft/tags/item/enchantable/fire_aspect.json", {{"remove", {"gf_test:fiery", "minecraft:golden_sword"}}, {"values", JsonDoc::array()}}},
        {"gf_test/tags/item/hot.json", {{"values", {"#minecraft:enchantable/fire_aspect", "gf_test:other"}}}},
        {"gf_test/tags/item/blunt.json", {{"values", {"#minecraft:enchantable/weapon"}}, {"remove", {"#minecraft:swords"}}}},
    });
    auto has = [](char const* tag, char const* id) { return gf::tags::has(Registry::Item, tag, id); };
    ctx.check(has("minecraft:enchantable/sword", "gf_test:fiery") && has("minecraft:enchantable/sword", "minecraft:golden_sword"), "both swords are enchantable swords");
    ctx.check(!has("minecraft:enchantable/fire_aspect", "gf_test:fiery"), "a removed data id");
    ctx.check(!has("minecraft:enchantable/fire_aspect", "minecraft:golden_sword"), "a removed id the bridge would answer");
    ctx.check(has("minecraft:enchantable/fire_aspect", "gf_test:plain") && has("minecraft:enchantable/fire_aspect", "minecraft:iron_sword")
                  && has("minecraft:enchantable/fire_aspect", "minecraft:mace"),
              "the others stay (the mace: Java 1.21.1's fire_aspect)");
    ctx.check(!has("gf_test:hot", "gf_test:fiery") && !has("gf_test:hot", "minecraft:golden_sword"), "an including tag does not get the removed ids back");
    ctx.check(has("gf_test:hot", "gf_test:plain") && has("gf_test:hot", "gf_test:other"), "but the rest");
    auto hot = gf::tags::members(Registry::Item, "gf_test:hot");
    ctx.check(contains(hot, "gf_test:plain") && !contains(hot, "gf_test:fiery") && contains(hot, "minecraft:mace"), "members agree");
    ctx.check(has("gf_test:blunt", "minecraft:iron_axe") && has("gf_test:blunt", "minecraft:mace"), "#tag removal: the axes and the mace stay");
    ctx.check(!has("gf_test:blunt", "minecraft:iron_sword") && !has("gf_test:blunt", "gf_test:plain"), "the swords (bridge and data) are gone");
});

// ---- the vanilla tags of Java 1.21.1 ---------------------------------------------------------------

GF_REGISTER_SELFTEST("item.tags_vanilla_java", [](Context& ctx) {
    auto block = [](char const* tag, char const* id) { return gf::tags::has(Registry::Block, tag, id); };
    auto item  = [](char const* tag, char const* id) { return gf::tags::has(Registry::Item, tag, id); };
    // fences: #wooden_fences + nether_brick_fence (block and item)
    for (auto const* id : {"minecraft:oak_fence", "minecraft:crimson_fence", "minecraft:nether_brick_fence"})
        ctx.check(block("minecraft:fences", id) && item("minecraft:fences", id), std::string("#minecraft:fences holds ") + id);
    ctx.check(!block("minecraft:fences", "minecraft:cobblestone_wall") && !block("minecraft:fences", "minecraft:oak_fence_gate"), "no walls or fence gates");
    // doors / trapdoors: the wooden ones, iron's and the eight copper ones
    for (auto const* id : {"minecraft:wooden_door", "minecraft:dark_oak_door", "minecraft:iron_door", "minecraft:copper_door", "minecraft:waxed_oxidized_copper_door"})
        ctx.check(block("minecraft:doors", id) && item("minecraft:doors", id), std::string("#minecraft:doors holds ") + id);
    ctx.check(!block("minecraft:doors", "minecraft:iron_trapdoor"), "no trapdoor is a door");
    for (auto const* id : {"minecraft:trapdoor", "minecraft:spruce_trapdoor", "minecraft:iron_trapdoor", "minecraft:exposed_copper_trapdoor", "minecraft:waxed_copper_trapdoor"})
        ctx.check(block("minecraft:trapdoors", id) && item("minecraft:trapdoors", id), std::string("#minecraft:trapdoors holds ") + id);
    ctx.check(!block("minecraft:trapdoors", "minecraft:iron_door"), "no door is a trapdoor");
    // overworld_natural_logs (block): the plain logs of the overworld trees
    for (auto const* id : {"minecraft:oak_log", "minecraft:birch_log", "minecraft:cherry_log", "minecraft:mangrove_log"})
        ctx.check(block("minecraft:overworld_natural_logs", id), std::string("#minecraft:overworld_natural_logs holds ") + id);
    for (auto const* id : {"minecraft:stripped_oak_log", "minecraft:oak_wood", "minecraft:crimson_stem", "minecraft:pale_oak_log", "twilightforest:twilight_oak_log"})
        ctx.check(!block("minecraft:overworld_natural_logs", id), std::string("but not ") + id);
    // replaceable: bubble columns and the light blocks (Java's light)
    for (auto const* id : {"minecraft:bubble_column", "minecraft:light_block_0", "minecraft:light_block_15", "minecraft:short_grass", "minecraft:snow_layer"})
        ctx.check(block("minecraft:replaceable", id), std::string("#minecraft:replaceable holds ") + id);
    ctx.check(!block("minecraft:replaceable", "minecraft:stone") && !block("minecraft:replaceable", "minecraft:snow"), "stone and snow blocks are not replaceable");
    // the river biome tags (BiomeTags.REDUCED_WATER_AMBIENT_SPAWNS: #is_river)
    auto biome = [](char const* tag, char const* id) { return gf::tags::has(Registry::Biome, tag, id); };
    for (auto const* tag : {"minecraft:is_river", "minecraft:reduce_water_ambient_spawns"}) {
        ctx.check(biome(tag, "minecraft:river") && biome(tag, "frozen_river"), std::string(tag) + " holds the river and the frozen river");
        ctx.check(!biome(tag, "minecraft:ocean") && !biome(tag, "twilightforest:stream"), std::string(tag) + ": no ocean, no TF biome");
    }
    // the bridge lists ids of the game: the new rules' every listed id resolves
    std::vector<std::string> unknown;
    auto                     resolveAll = [&](Registry r, char const* tag) {
        auto const* rule = gf::tags::vanilla::find(r, tag);
        if (!rule) {
            unknown.push_back(std::string(tag) + " has no rule");
            return;
        }
        for (auto const& id : gf::tags::vanilla::listedIds(*rule)) {
            if (id == "minecraft:light_block") continue; // the legacy alias, kept for queries by it
            auto why = r == Registry::Block ? unknownBlock(id) : unknownItem(id);
            if (!why.empty()) unknown.push_back(std::string(tag) + " " + id + ": " + why);
        }
    };
    for (auto const* tag : {"minecraft:fences", "minecraft:doors", "minecraft:trapdoors", "minecraft:overworld_natural_logs", "minecraft:stone_buttons"})
        resolveAll(Registry::Block, tag);
    for (auto const* tag : {"minecraft:fences", "minecraft:doors", "minecraft:trapdoors"}) resolveAll(Registry::Item, tag);
    auto const* replaceable = gf::tags::vanilla::find(Registry::Block, "minecraft:replaceable");
    ctx.require(replaceable != nullptr, "minecraft:replaceable is bridged");
    for (std::string id : {"minecraft:bubble_column", "minecraft:light_block_0", "minecraft:light_block_8", "minecraft:light_block_15"})
        if (auto why = unknownBlock(id); !why.empty()) unknown.push_back("minecraft:replaceable " + id + ": " + why);
    std::string list;
    for (auto const& u : unknown) list += (list.empty() ? "" : "; ") + u;
    ctx.check(unknown.empty(), "the added rules list ids of the game" + (list.empty() ? std::string() : ": " + list));
    // the whole bridge, for the record (it keeps legacy and newer names for queries by them)
    int listed = 0, missing = 0;
    for (auto r : {Registry::Block, Registry::Item})
        for (auto const& tag : gf::tags::vanilla::knownTags(r))
            for (auto const& id : gf::tags::vanilla::listedIds(*gf::tags::vanilla::find(r, tag))) {
                ++listed;
                if (!(r == Registry::Block ? unknownBlock(id) : unknownItem(id)).empty()) ++missing;
            }
    ctx.info("the bridge lists " + std::to_string(listed) + " block / item ids, " + std::to_string(missing) + " of them not ids of this game (legacy or newer names)");
});

// Minecraft 1.21.1's structure tags (data/minecraft/tags/worldgen/structure, all 20): /tf locate
// #minecraft:village finds the tag (and no village in the Twilight Forest).
GF_REGISTER_SELFTEST("item.tags_structures", [](Context& ctx) {
    struct Expect {
        char const*              tag;
        std::vector<std::string> members;
    };
    std::vector<Expect> const expected{
        {"minecraft:cats_spawn_as_black", {"minecraft:swamp_hut"}},
        {"minecraft:cats_spawn_in", {"minecraft:swamp_hut"}},
        {"minecraft:dolphin_located", {"minecraft:ocean_ruin_cold", "minecraft:ocean_ruin_warm", "minecraft:shipwreck", "minecraft:shipwreck_beached"}},
        {"minecraft:eye_of_ender_located", {"minecraft:stronghold"}},
        {"minecraft:mineshaft", {"minecraft:mineshaft", "minecraft:mineshaft_mesa"}},
        {"minecraft:ocean_ruin", {"minecraft:ocean_ruin_cold", "minecraft:ocean_ruin_warm"}},
        {"minecraft:on_ocean_explorer_maps", {"minecraft:monument"}},
        {"minecraft:on_desert_village_maps", {"minecraft:village_desert"}},
        {"minecraft:on_plains_village_maps", {"minecraft:village_plains"}},
        {"minecraft:on_savanna_village_maps", {"minecraft:village_savanna"}},
        {"minecraft:on_snowy_village_maps", {"minecraft:village_snowy"}},
        {"minecraft:on_taiga_village_maps", {"minecraft:village_taiga"}},
        {"minecraft:on_jungle_explorer_maps", {"minecraft:jungle_pyramid"}},
        {"minecraft:on_swamp_explorer_maps", {"minecraft:swamp_hut"}},
        {"minecraft:on_treasure_maps", {"minecraft:buried_treasure"}},
        {"minecraft:on_trial_chambers_maps", {"minecraft:trial_chambers"}},
        {"minecraft:on_woodland_explorer_maps", {"minecraft:mansion"}},
        {"minecraft:ruined_portal",
         {"minecraft:ruined_portal", "minecraft:ruined_portal_desert", "minecraft:ruined_portal_jungle", "minecraft:ruined_portal_mountain",
          "minecraft:ruined_portal_nether", "minecraft:ruined_portal_ocean", "minecraft:ruined_portal_swamp"}},
        {"minecraft:shipwreck", {"minecraft:shipwreck", "minecraft:shipwreck_beached"}},
        {"minecraft:village",
         {"minecraft:village_desert", "minecraft:village_plains", "minecraft:village_savanna", "minecraft:village_snowy", "minecraft:village_taiga"}},
    };
    auto known = gf::tags::vanilla::knownTags(Registry::Structure);
    ctx.check(known.size() == expected.size(), "the bridge knows Minecraft's 20 structure tags (got " + std::to_string(known.size()) + ")");
    for (auto const& e : expected) {
        auto got = gf::tags::members(Registry::Structure, e.tag);
        auto want = e.members;
        std::sort(want.begin(), want.end());
        ctx.check(got == want, std::string("#") + e.tag + " has Java's members (got " + std::to_string(got.size()) + ")");
        ctx.check(gf::tags::has(Registry::Structure, std::string("#") + e.tag, want.front()), std::string("has(#") + e.tag + ", " + want.front() + ")");
    }
    ctx.check(!gf::tags::has(Registry::Structure, "minecraft:village", "twilightforest:lich_tower"), "no TF structure is a village");
    ctx.check(gf::tags::members(Registry::Structure, "minecraft:not_a_structure_tag").empty(), "an unknown structure tag has no members (locate: invalid)");
    // TF's own structure tag is still data
    auto landmark = gf::tags::members(Registry::Structure, "twilightforest:landmark");
    if (!landmark.empty()) ctx.check(!contains(landmark, "minecraft:village_plains"), "#twilightforest:landmark holds TF's structures only");
});

// ---- the snapshot ------------------------------------------------------------------------------------

// Queries from other threads (world generation asks per block) while the data reloads: no lock on
// the read path, the answers stay right through every reload, and each reload publishes a new
// snapshot.
GF_REGISTER_SELFTEST("item.tags_concurrent_reload", [](Context& ctx) {
    auto              before = gf::tags::internal::generation();
    std::atomic<bool> stop{false};
    std::atomic<int>  wrong{0};
    std::atomic<long> asked{0};
    // answers from the data and the bridge's id lists (no pattern rule: the game's registries are
    // read on the server thread only here)
    auto worker = [&] {
        long n = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            if (!gf::tags::has(Registry::Block, "gf_test:fixture_blocks", "gf_test:fixture_block")) ++wrong;
            if (!gf::tags::has(Registry::Block, "minecraft:fences", "minecraft:nether_brick_fence")) ++wrong;
            if (gf::tags::has(Registry::Block, "minecraft:fences", "minecraft:cobblestone_wall")) ++wrong;
            if (!gf::tags::has(Registry::Structure, "minecraft:village", "minecraft:village_plains")) ++wrong;
            if (!gf::tags::has(Registry::Biome, "minecraft:reduce_water_ambient_spawns", "minecraft:river")) ++wrong;
            n += 5;
        }
        asked += n;
    };
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) threads.emplace_back(worker);
    for (int i = 0; i < 3; ++i) gf::tags::internal::reload();
    stop = true;
    for (auto& t : threads) t.join();
    ctx.check(gf::tags::internal::generation() >= before + 3, "each reload published a snapshot");
    ctx.check(wrong.load() == 0, std::to_string(wrong.load()) + " wrong answers of " + std::to_string(asked.load()) + " during 3 reloads");
    ctx.check(gf::tags::has(Registry::Block, "gf_test:fixture_blocks", "gf_test:fixture_block"), "the server thread reads the newest snapshot");
    ctx.info(std::to_string(asked.load()) + " queries from 4 threads during 3 reloads");
});

// ---- statistics ----------------------------------------------------------------------------------------

// Stats.CUSTOM: Minecraft's custom statistics count silently (TF's chests award minecraft:open_chest
// and minecraft:trigger_trapped_chest), as TF's do; an id of neither is counted with one warning.
GF_REGISTER_SELFTEST("item.stats_vanilla", [](Context& ctx) {
    namespace st = gf::stats;
    ctx.check(st::internal::registeredCount() == 85, "TF's 10 and Minecraft 1.21.1's 75 custom statistics (got " + std::to_string(st::internal::registeredCount()) + ")");
    for (auto const* id : {"minecraft:open_chest", "minecraft:trigger_trapped_chest", "open_chest", "minecraft:interact_with_smithing_table", "twilightforest:bugs_squished"})
        ctx.check(st::internal::isRegistered(id), std::string(id) + " is a custom statistic");
    ctx.check(!st::internal::isRegistered("minecraft:mined") && !st::internal::isRegistered("gf_test:not_a_statistic"), "stat types and unknown ids are not");
    auto& player = ctx.player();
    auto  opens  = st::value(player, "minecraft:open_chest");
    auto  traps  = st::value(player, "minecraft:trigger_trapped_chest");
    st::award(player, "minecraft:open_chest");
    st::award(player, "open_chest"); // a ResourceLocation without namespace is minecraft's
    st::award(player, "minecraft:trigger_trapped_chest", 3);
    ctx.check(st::value(player, "minecraft:open_chest") == opens + 2, "minecraft:open_chest counted twice");
    ctx.check(st::value(player, "minecraft:trigger_trapped_chest") == traps + 3, "minecraft:trigger_trapped_chest counted");
    ctx.check(!st::internal::warnedAbout("minecraft:open_chest") && !st::internal::warnedAbout("minecraft:trigger_trapped_chest"), "without a warning");
    st::internal::forget(player); // no record of the simulated player stays in the world DB
});
