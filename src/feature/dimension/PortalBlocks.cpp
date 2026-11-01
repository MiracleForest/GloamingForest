// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/data/tags/BlockTagGenerator.java
//   src/main/java/twilightforest/data/tags/ItemTagGenerator.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The portal block and the tags the portal rules read. The tags come from the tag store
// (gf::tags, F9, filled from TF's imported tag JSON); while it has no members for a tag the
// members below are used: TF's own lists (BlockTagGenerator / ItemTagGenerator) and the vanilla
// tags they include, with their Bedrock block ids.

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "mc/world/level/block/Block.h"

#include "feature/dimension/Internal.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"

namespace gf::feature::dimension {

namespace {

/// Members of the tags F4 reads, for when the tag store has none. '#' entries are nested tags.
std::unordered_map<std::string, std::vector<std::string>> const& fallbackTags() {
    static std::unordered_map<std::string, std::vector<std::string>> const tags = {
        // BlockTagGenerator: PORTAL_EDGE = farmland, dirt_path, #dirt
        {"twilightforest:portal/edge", {"minecraft:farmland", "minecraft:dirt_path", "minecraft:grass_path", "#minecraft:dirt"}},
        // PORTAL_POOL = water
        {"twilightforest:portal/fluid", {"minecraft:water"}},
        // PORTAL_DECO
        {"twilightforest:portal/decoration",
         {"minecraft:bamboo",
          "minecraft:short_grass",
          "minecraft:tall_grass",
          "minecraft:fern",
          "minecraft:large_fern",
          "minecraft:dead_bush",
          "minecraft:deadbush",
          "minecraft:sugar_cane",
          "minecraft:reeds",
          "minecraft:chorus_plant",
          "minecraft:chorus_flower",
          "minecraft:sweet_berry_bush",
          "minecraft:nether_wart",
          "minecraft:cocoa",
          "minecraft:vine",
          "minecraft:glow_lichen",
          "minecraft:red_mushroom",
          "minecraft:brown_mushroom",
          "minecraft:warped_fungus",
          "minecraft:crimson_fungus",
          "minecraft:melon_stem",
          "minecraft:pumpkin_stem",
          "minecraft:moss_carpet",
          "minecraft:pink_petals",
          "minecraft:big_dripleaf",
          "minecraft:small_dripleaf_block",
          "twilightforest:fiddlehead",
          "twilightforest:moss_patch",
          "twilightforest:mayapple",
          "twilightforest:clover_patch",
          "twilightforest:mushgloom",
          "twilightforest:fallen_leaves",
          "twilightforest:giant_leaves",
          "twilightforest:steeleaf_block",
          "twilightforest:hardened_dark_leaves",
          "#minecraft:flowers",
          "#minecraft:leaves",
          "#minecraft:saplings",
          "#minecraft:crops"}},
        // GENERATED_PORTAL_DECO
        {"twilightforest:portal/generated_decoration",
         {"minecraft:brown_mushroom",
          "minecraft:red_mushroom",
          "minecraft:short_grass",
          "minecraft:fern",
          "minecraft:blue_orchid",
          "minecraft:azure_bluet",
          "minecraft:lily_of_the_valley",
          "minecraft:oxeye_daisy",
          "minecraft:allium",
          "minecraft:cornflower",
          "minecraft:white_tulip",
          "minecraft:pink_tulip",
          "minecraft:orange_tulip",
          "minecraft:red_tulip",
          "twilightforest:mushgloom",
          "twilightforest:mayapple",
          "twilightforest:fiddlehead"}},
        // vanilla tags (Java 1.21.1 members, Bedrock ids) with TF's own additions
        // (BlockTagGenerator: UBEROUS_SOIL in #dirt, TF's saplings and leaves)
        {"minecraft:dirt",
         {"twilightforest:uberous_soil",
          "minecraft:dirt",
          "minecraft:grass_block",
          "minecraft:podzol",
          "minecraft:coarse_dirt",
          "minecraft:mycelium",
          "minecraft:dirt_with_roots",
          "minecraft:moss_block",
          "minecraft:mud",
          "minecraft:muddy_mangrove_roots"}},
        {"minecraft:flowers",
         {"minecraft:dandelion",
          "minecraft:poppy",
          "minecraft:blue_orchid",
          "minecraft:allium",
          "minecraft:azure_bluet",
          "minecraft:red_tulip",
          "minecraft:orange_tulip",
          "minecraft:white_tulip",
          "minecraft:pink_tulip",
          "minecraft:oxeye_daisy",
          "minecraft:cornflower",
          "minecraft:lily_of_the_valley",
          "minecraft:torchflower",
          "minecraft:wither_rose",
          "minecraft:sunflower",
          "minecraft:lilac",
          "minecraft:rose_bush",
          "minecraft:peony",
          "minecraft:pitcher_plant",
          "minecraft:flowering_azalea",
          "minecraft:azalea_leaves_flowered",
          "minecraft:mangrove_propagule",
          "minecraft:cherry_leaves",
          "minecraft:pink_petals",
          "minecraft:chorus_flower",
          "minecraft:spore_blossom"}},
        {"minecraft:leaves",
         {"minecraft:oak_leaves",
          "minecraft:spruce_leaves",
          "minecraft:birch_leaves",
          "minecraft:jungle_leaves",
          "minecraft:acacia_leaves",
          "minecraft:dark_oak_leaves",
          "minecraft:mangrove_leaves",
          "minecraft:cherry_leaves",
          "minecraft:azalea_leaves",
          "minecraft:azalea_leaves_flowered",
          "twilightforest:rainbow_oak_leaves",
          "twilightforest:twilight_oak_leaves",
          "twilightforest:canopy_leaves",
          "twilightforest:mangrove_leaves",
          "twilightforest:dark_leaves",
          "twilightforest:time_leaves",
          "twilightforest:transformation_leaves",
          "twilightforest:mining_leaves",
          "twilightforest:sorting_leaves",
          "twilightforest:thorn_leaves",
          "twilightforest:beanstalk_leaves"}},
        {"minecraft:saplings",
         {"minecraft:oak_sapling",
          "minecraft:spruce_sapling",
          "minecraft:birch_sapling",
          "minecraft:jungle_sapling",
          "minecraft:acacia_sapling",
          "minecraft:dark_oak_sapling",
          "minecraft:cherry_sapling",
          "minecraft:azalea",
          "minecraft:flowering_azalea",
          "minecraft:mangrove_propagule",
          "twilightforest:twilight_oak_sapling",
          "twilightforest:canopy_sapling",
          "twilightforest:mangrove_sapling",
          "twilightforest:darkwood_sapling",
          "twilightforest:time_sapling",
          "twilightforest:transformation_sapling",
          "twilightforest:mining_sapling",
          "twilightforest:sorting_sapling",
          "twilightforest:hollow_oak_sapling",
          "twilightforest:rainbow_oak_sapling"}},
        {"minecraft:crops",
         {"minecraft:wheat",
          "minecraft:carrots",
          "minecraft:potatoes",
          "minecraft:beetroot",
          "minecraft:melon_stem",
          "minecraft:pumpkin_stem",
          "minecraft:torchflower_crop",
          "minecraft:pitcher_crop"}},
        {"minecraft:features_cannot_replace",
         {"minecraft:bedrock",
          "minecraft:mob_spawner",
          "minecraft:chest",
          "minecraft:end_portal_frame",
          "minecraft:reinforced_deepslate",
          "minecraft:trial_spawner",
          "minecraft:vault"}},
        // ItemTagGenerator: PORTAL_ACTIVATOR = #c:gems/diamond
        {"twilightforest:portal/activator", {"#c:gems/diamond"}},
        {"c:gems/diamond", {"minecraft:diamond"}},
    };
    return tags;
}

void expandFallback(std::string const& tag, std::unordered_set<std::string>& out, int depth = 0) {
    if (depth > 8) return;
    auto const& all = fallbackTags();
    auto        it  = all.find(tag);
    if (it == all.end()) return;
    for (auto const& m : it->second) {
        if (!m.empty() && m[0] == '#') expandFallback(m.substr(1), out, depth + 1);
        else out.insert(m);
    }
}

std::mutex& tagMutex() {
    static std::mutex m;
    return m;
}
std::unordered_map<std::string, std::unordered_set<std::string>>& tagCache() {
    static std::unordered_map<std::string, std::unordered_set<std::string>> c;
    return c;
}

std::unordered_set<std::string> const& tagSet(tags::Registry registry, std::string const& tag) {
    std::lock_guard lock(tagMutex());
    auto            key = std::to_string(static_cast<int>(registry)) + "|" + tag;
    auto            it  = tagCache().find(key);
    if (it != tagCache().end()) return it->second;
    std::unordered_set<std::string> set;
    for (auto& m : tags::members(registry, tag)) set.insert(std::move(m));
    if (set.empty()) expandFallback(tag, set);
    return tagCache().emplace(std::move(key), std::move(set)).first->second;
}

struct TagCacheReset {
    TagCacheReset() {
        // the tag store loads with the world's data: look the members up again per world
        game::db::onWorldReady([] {
            std::lock_guard lock(tagMutex());
            tagCache().clear();
        });
    }
} const gTagCacheReset;

} // namespace

bool blockTagged(std::string const& tag, std::string const& blockId) { return tagSet(tags::Registry::Block, tag).contains(blockId); }

std::vector<std::string> blockTagMembers(std::string const& tag) {
    auto const&              set = tagSet(tags::Registry::Block, tag);
    std::vector<std::string> out(set.begin(), set.end());
    std::sort(out.begin(), out.end());
    return out;
}

bool itemTagged(std::string const& tag, std::string const& itemId) { return tagSet(tags::Registry::Item, tag).contains(itemId); }

bool isPortal(Block const& block) { return block.getTypeName() == kPortalBlock; }

bool isOpenPortal(Block const& block) { return isPortal(block) && game::intState(block, kOneWayState).value_or(0) == 0; }

Block const* portalBlock(bool oneWay) { return game::blockWithStates(kPortalBlock, {{kOneWayState, oneWay}}); }

} // namespace gf::feature::dimension
