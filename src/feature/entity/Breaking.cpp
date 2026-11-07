// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/util/entities/EntityUtil.java
//   src/main/java/twilightforest/entity/ai/goal/ChargeAttackGoal.java
//   src/main/java/twilightforest/init/TFBlockEntities.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Contract "entity" (F8): the block breaking of mobs (EntityUtil#canDestroyBlock and the
// ChargeAttackGoal's stricter "no block entity" rule) and the Peaceful despawn of TF's monsters
// (Monster#shouldDespawnInPeaceful).

#include "feature/ext/entity/World.h"

#include <string_view>
#include <unordered_set>

#include "ll/api/service/Bedrock.h"

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"

#include "feature/entity/EntityInternal.h"
#include "feature/inventory/Inventory.h"
#include "game/Blocks.h"

namespace gf::feature::entity {

/// `be instanceof Container` of Java's block entities (BaseContainerBlockEntity, the
/// ContainerSingleItem jukebox / decorated pot, the chiseled bookshelf). Bedrock's item frames are
/// block actors where Java's are entities that destroyBlock never touches: they are spared too.
bool isContainerBlockActor(BlockActor const& be) {
    switch (be.getType()) {
    case BlockActorType::Furnace:
    case BlockActorType::BlastFurnace:
    case BlockActorType::Smoker:
    case BlockActorType::Chest:
    case BlockActorType::Dispenser:
    case BlockActorType::Dropper:
    case BlockActorType::Hopper:
    case BlockActorType::ShulkerBox:
    case BlockActorType::BarrelBlock:
    case BlockActorType::BrewingStand:
    case BlockActorType::Crafter:
    case BlockActorType::Jukebox:
    case BlockActorType::ChiseledBookshelf:
    case BlockActorType::DecoratedPot:
    case BlockActorType::ItemFrame:
    case BlockActorType::GlowItemFrame: return true;
    default: return false;
    }
}

/// Whether Java has a block entity where Bedrock has this block actor: Bedrock keeps block actors for
/// flower pots, note blocks, cauldrons, pistons, lodestones, spore blossoms and its education blocks,
/// which are plain blocks in Java (ChargeAttackGoal's getBlockEntity(pos) == null breaks them).
bool javaHasBlockEntity(BlockActor const& be) {
    switch (be.getType()) {
    case BlockActorType::FlowerPot:
    case BlockActorType::Music:
    case BlockActorType::Cauldron:
    case BlockActorType::PistonArm:
    case BlockActorType::Lodestone:
    case BlockActorType::SporeBlossom:
    case BlockActorType::NetherReactor:
    case BlockActorType::Chalkboard:
    case BlockActorType::ChemistryTable: return false;
    default: return true;
    }
}

namespace {

/// The TF blocks of TFBlockEntities' valid block lists (custom blocks have no block actor on
/// Bedrock). TF's chests, trapped chests and signs (vanilla block entity types) are matched by
/// their suffix.
std::unordered_set<std::string_view> const& javaBlockEntityBlocks() {
    static std::unordered_set<std::string_view> const set{
        "antibuilder", "cinder_furnace", "carminite_reactor", "reactor_debris", "fire_jet", "encased_fire_jet",
        "ghast_trap", "smoker", "encased_smoker", "carminite_builder", "naga_trophy", "naga_wall_trophy",
        "lich_trophy", "lich_wall_trophy", "minoshroom_trophy", "minoshroom_wall_trophy", "hydra_trophy",
        "hydra_wall_trophy", "knight_phantom_trophy", "knight_phantom_wall_trophy", "ur_ghast_trophy",
        "ur_ghast_wall_trophy", "alpha_yeti_trophy", "alpha_yeti_wall_trophy", "snow_queen_trophy",
        "snow_queen_wall_trophy", "quest_ram_trophy", "quest_ram_wall_trophy", "naga_boss_spawner",
        "lich_boss_spawner", "minoshroom_boss_spawner", "hydra_boss_spawner", "knight_phantom_boss_spawner",
        "ur_ghast_boss_spawner", "alpha_yeti_boss_spawner", "snow_queen_boss_spawner", "final_boss_boss_spawner",
        "cicada", "firefly", "moonworm", "cicada_jar", "firefly_jar", "mason_jar", "skull_chest",
        "keepsake_casket", "brazier", "chiseled_canopy_bookshelf", "beanstalk_grower", "red_thread",
        "candelabra", "sinister_spawner", "zombie_skull_candle", "zombie_wall_skull_candle",
        "skeleton_skull_candle", "skeleton_wall_skull_candle", "wither_skeleton_skull_candle",
        "wither_skeleton_wall_skull_candle", "creeper_skull_candle", "creeper_wall_skull_candle",
        "player_skull_candle", "player_wall_skull_candle", "piglin_skull_candle", "piglin_wall_skull_candle",
        "ominous_candle", "ominous_white_candle", "ominous_orange_candle", "ominous_magenta_candle",
        "ominous_light_blue_candle", "ominous_yellow_candle", "ominous_lime_candle", "ominous_pink_candle",
        "ominous_gray_candle", "ominous_light_gray_candle", "ominous_cyan_candle", "ominous_purple_candle",
        "ominous_blue_candle", "ominous_brown_candle", "ominous_green_candle", "ominous_red_candle",
        "ominous_black_candle", "oak_drying_rack", "spruce_drying_rack", "birch_drying_rack",
        "jungle_drying_rack", "acacia_drying_rack", "dark_oak_drying_rack", "mangrove_drying_rack",
        "cherry_drying_rack", "bamboo_drying_rack", "crimson_drying_rack", "warped_drying_rack",
        "twilight_oak_drying_rack", "canopy_drying_rack", "dark_drying_rack",
        "time_drying_rack", "transformation_drying_rack", "mining_drying_rack", "sorting_drying_rack",
        "vangrove_drying_rack",
    };
    return set;
}

bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

} // namespace

bool hasJavaBlockEntity(std::string_view blockId) {
    constexpr std::string_view ns = "twilightforest:";
    if (!blockId.starts_with(ns)) return false;
    auto path = blockId.substr(ns.size());
    return javaBlockEntityBlocks().contains(path) || endsWith(path, "_chest") || endsWith(path, "_sign");
}

} // namespace gf::feature::entity

namespace gf::entity::ext {

namespace fe = gf::feature::entity;

bool canDestroyBlock(BlockSource& region, BlockPos const& pos, BreakRule rule) {
    Block const& block    = region.getBlock(pos);
    float        hardness = block.getBlockType().getDestroySpeed();
    if (block.isAir() || hardness < 0.0F || hardness >= 50.0F) return false;
    if (auto* be = region.getBlockEntity(pos)) {
        if (fe::isContainerBlockActor(*be)) return false;
        if (rule == BreakRule::SpareBlockEntities && fe::javaHasBlockEntity(*be)) return false;
    }
    if (block.getBlockType().mNameInfo->mNamespaceName.get() == "minecraft") return true;
    // custom blocks have no block actor: TF's own block entities, and gf::inventory's containers
    if (rule == BreakRule::SpareBlockEntities && fe::hasJavaBlockEntity(game::blockId(block))) return false;
    return !gf::inventory::find(region, pos).has_value();
}

int destroyBlocks(BlockSource& region, BlockPos const& min, BlockPos const& max, BreakRule rule, bool drop) {
    auto level = ll::service::getLevel();
    if (!level) return 0;
    if (!region.hasChunksAt(min, 0, false) || !region.hasChunksAt(max, 0, false)) return 0;
    int broken = 0;
    for (int x = min.x; x <= max.x; ++x)
        for (int y = min.y; y <= max.y; ++y)
            for (int z = min.z; z <= max.z; ++z) {
                BlockPos pos{x, y, z};
                if (!canDestroyBlock(region, pos, rule)) continue;
                BlockChangeContext ctx{};
                if (level->destroyBlock(region, pos, drop, ctx)) ++broken;
            }
    return broken;
}

} // namespace gf::entity::ext
