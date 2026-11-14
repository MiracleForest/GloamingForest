// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/command/GloamingCommand.java (structure_util branch)
//   src/main/java/gloamingforest/command/DisplayPiecesCommand.java
//   src/main/java/gloamingforest/command/ClearDisplayCommand.java
//   src/main/java/gloamingforest/command/CountLootCommand.java
//   src/main/java/gloamingforest/command/CountTemplateCommand.java
//   src/main/java/gloamingforest/command/StructureDistanceCommand.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// /gloamingforest structure_util (operators):
//   display_pieces <structure>       outlines the start and its pieces around you (upstream: block
//                                    displays; Bedrock has no display entities: particle outlines
//                                    for a minute, piece list in chat)
//   clear_display                    stops the outlines
//   count_loot <structure> [common]  counts the items in the start's containers
//   count_template <structure>       counts the templates of the start's pieces
//   generator_radius <structure>     outlines the chunks 0..8 around the start's chunk
// GloamingForest development additions:
//   place <template> [rotation] [mirror]  places a template at your feet
//   capture <id> <x1> <y1> <z1> <x2> <y2> <z2>  saves a box with its entities as a .gft.json
//                                    template (original builds; paintings by the block they cover)
//   jigsaw <pool> [depth]            assembles a template pool at your feet
//   templates                        lists the known templates, pools and data counts
//   outline <x1> <y1> <z1> <x2> <y2> <z2> [seconds]  outlines a box (server e2e)
//   pending_probe / scale_probe      restart probes of the server e2e (a queued job in an unloaded
//                                    chunk and a forced fixture landmark start there; a scaled
//                                    mob left by the next structure.scale_probe)
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <tuple>
#include <unordered_map>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/Tag.h"
#include "mc/network/packet/TextPacket.h"
#include "mc/network/packet/TextPacketType.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/Container.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/inventory/FillingContainer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/BrewingStandBlockActor.h"
#include "mc/world/level/block/actor/ChiseledBookshelfBlockActor.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"
#include "mc/world/level/block/actor/HopperBlockActor.h"
#include "mc/world/level/block/actor/RandomizableBlockActorContainer.h"
#include "mc/world/level/block/actor/RandomizableBlockActorContainerBase.h"
#include "mc/world/level/block/actor/RandomizableBlockActorFillingContainer.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/chunk/LevelChunkBlockActorStorage.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "core/Keys.h"
#include "feature/common/BlockData.h"
#include "feature/ext/item/Item.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/structure/TemplateDefinitions.h"
#include "feature/inventory/Inventory.h"
#include "feature/loot/Loot.h"
#include "feature/landmark/Landmark.h"
#include "feature/structure/Region.h"
#include "feature/structure/bases/BasesInternal.h"
#include "feature/structure/bases/HollowHill.h"
#include "feature/structure/bases/Tower.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Commands.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Main.h"

namespace gf::structure {

// Command parameter types need external linkage.
struct StructureUtilKeyParams {
    std::string filter_structure;
};
struct StructureUtilLootParams {
    std::string filter_structure;
    bool        show_common = true;
};
struct StructureUtilPlaceParams {
    std::string template_id;
    int         rotation = 0;
    int         mirror   = 0;
};
struct StructureUtilJigsawParams {
    std::string pool;
    int         depth = 7;
};
struct StructureUtilPieceParams {
    std::string kind;
};
struct StructureUtilCaptureParams {
    std::string id;
    int         x1 = 0, y1 = 0, z1 = 0, x2 = 0, y2 = 0, z2 = 0;
};
struct StructureUtilOutlineParams {
    int x1 = 0, y1 = 0, z1 = 0, x2 = 0, y2 = 0, z2 = 0;
    int seconds = 60;
};

namespace {

/// Outlines drawn with particles until cleared or expired (upstream's block displays).
struct Outline {
    int         dim;
    BlockBox    box;
    std::string particle;
    int         ticksLeft;
    float       spacing   = 2.0f;  ///< blocks between two particles of an edge
    bool        floorOnly = false; ///< only the four bottom edges
};
/// generator_radius's chunk labels (upstream's text displays): shown on the action bar of the
/// operator while they stand in a labelled chunk.
struct ChunkLabels {
    int                         dim = 0;
    int                         centerX = 0, centerZ = 0; ///< the start's chunk
    int                         ticksLeft = 0;
    std::int64_t                playerId  = 0;            ///< ActorUniqueID of the operator
};
std::vector<Outline>       gOutlines;
std::optional<ChunkLabels> gLabels;
int                        gTickCounter = 0;

void showLabels(Level& level) {
    if (!gLabels) return;
    gLabels->ticksLeft -= 10;
    Player* player = nullptr;
    level.forEachPlayer([&](Player& p) {
        if (p.getOrCreateUniqueID().rawID != gLabels->playerId) return true;
        player = &p;
        return false;
    });
    if (!player || static_cast<int>(player->getDimensionId()) != gLabels->dim) return;
    auto pos   = game::blockPosOf(player->getFeetPos());
    int  diffX = core::chunkOf(pos.x) - gLabels->centerX;
    int  diffZ = core::chunkOf(pos.z) - gLabels->centerZ;
    int  r     = std::max(std::abs(diffX), std::abs(diffZ));
    if (r > 8) return;
    // the two texts above each chunk: "dx, dz" and the radius (center chunk named)
    std::string radius = r == 0 ? tr("commands.gloamingfeature.generator_radius.center_chunk", "Structure start's center chunk")
                                : tr("commands.gloamingfeature.generator_radius.radius", "Radius from center chunk: %s", r);
    // a tip (action bar text) while the operator stands in the chunk
    auto        pkt = TextPacket{};
    TextPacketPayload::MessageOnly message;
    message.mType = TextPacketType::Tip;
    message.mMessage->assign(std::to_string(diffX) + ", " + std::to_string(diffZ) + "\n" + radius);
    pkt.mBody = message;
    pkt.sendTo(*player);
}

void drawOutlines() {
    // (called from the kit's one tick listener: nothing to do while nothing is displayed)
    if ((gOutlines.empty() && !gLabels) || (++gTickCounter % 10) != 0) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    for (auto& o : gOutlines) {
        o.ticksLeft -= 10;
        auto dim = level->getDimension(o.dim).lock();
        if (!dim) continue;
        auto const& b = o.box;
        // the twelve edges, one particle every `spacing` blocks
        auto edge = [&](float x0, float y0, float z0, float x1, float y1, float z1) {
            float len   = std::max({std::abs(x1 - x0), std::abs(y1 - y0), std::abs(z1 - z0)});
            int   steps = std::max(1, static_cast<int>(len / o.spacing));
            for (int i = 0; i <= steps; i++) {
                float t = static_cast<float>(i) / static_cast<float>(steps);
                level->spawnParticleEffect(o.particle, Vec3{x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, z0 + (z1 - z0) * t}, dim.get());
            }
        };
        auto  x0 = static_cast<float>(b.minX), y0 = static_cast<float>(b.minY), z0 = static_cast<float>(b.minZ);
        auto  x1 = static_cast<float>(b.maxX + 1), y1 = static_cast<float>(b.maxY + 1), z1 = static_cast<float>(b.maxZ + 1);
        edge(x0, y0, z0, x1, y0, z0);
        edge(x0, y0, z1, x1, y0, z1);
        edge(x0, y0, z0, x0, y0, z1);
        edge(x1, y0, z0, x1, y0, z1);
        if (o.floorOnly) continue; // the bottom square only (generator_radius's 289 chunks)
        edge(x0, y1, z0, x1, y1, z0);
        edge(x0, y1, z1, x1, y1, z1);
        edge(x0, y0, z0, x0, y1, z0);
        edge(x1, y0, z0, x1, y1, z0);
        edge(x0, y0, z1, x0, y1, z1);
        edge(x1, y0, z1, x1, y1, z1);
        edge(x0, y1, z0, x0, y1, z1);
        edge(x1, y1, z0, x1, y1, z1);
    }
    std::erase_if(gOutlines, [](Outline const& o) { return o.ticksLeft <= 0; });
    showLabels(*level);
    if (gLabels && gLabels->ticksLeft <= 0) gLabels.reset();
    if (gOutlines.empty() && !gLabels) log().info("structure_util outlines expired");
}

/// upstream's block and text displays (DisplayUtil) stay until clear_display; particles must be sent
/// again and again, so an outline is drawn for a minute unless `ticks` says otherwise.
void addOutline(int dim, BlockBox const& box, std::string particle, float spacing = 2.0f, bool floorOnly = false, int ticks = 60 * 20) {
    gOutlines.push_back(Outline{dim, box, std::move(particle), ticks, spacing, floorOnly});
}

struct Where {
    Player*      player = nullptr;
    BlockSource* source = nullptr;
    BlockPos     pos{0, 0, 0};
    int          dim = 0;
};

std::optional<Where> where(CommandOrigin const& origin, CommandOutput& output) {
    if (!commands::isAdmin(origin)) {
        output.error("You need operator permission.");
        return std::nullopt;
    }
    auto* player = commands::playerOf(origin);
    if (!player) {
        output.error(tr("gf.structure.need_player", "Run this as a player."));
        return std::nullopt;
    }
    Where w;
    w.player = player;
    w.source = &player->getDimensionBlockSource();
    w.pos    = game::blockPosOf(player->getFeetPos());
    w.pos.y  = std::max<int>(w.pos.y, w.source->getMinHeight());
    w.dim    = static_cast<int>(w.source->getDimensionId());
    return w;
}

/// The landmark start `key` at the player (StructureManager#getStructureAt).
std::optional<landmark::Start> startAt(Where const& w, std::string const& key, CommandOutput& output) {
    auto start = landmark::startAt(w.dim, w.pos);
    if (!start || (start->key != key && !key.empty())) {
        output.error(tr("commands.gloamingfeature.structure.outside", "You are not in a structure."));
        return std::nullopt;
    }
    return start;
}

void displayPieces(CommandOrigin const& origin, CommandOutput& output, std::string const& key) {
    auto w = where(origin, output);
    if (!w) return;
    auto start = startAt(*w, key, output);
    if (!start) return;
    int successes = 1;
    addOutline(w->dim, start->box, "minecraft:redstone_ore_dust_particle");
    // StructureStart#getPieces with each piece's type id and protection flag (F6's landmark ext;
    // startAt's data["pieces"] carries the tags only, so the pieces are saved a second time here:
    // fine for a one-off command)
    for (auto const& p : landmark::ext::startPieces(*start)) {
        // lime glass for a ProgressionPiece whose isComponentProtected is true, light blue otherwise
        addOutline(w->dim, p.box, p.protectedPiece ? "minecraft:villager_happy" : "minecraft:water_wake_particle");
        w->player->sendMessage((p.type.empty() ? tr("commands.gloamingfeature.display_pieces.missing_key", "missing key") : p.type) + " " +
                               boxToJson(p.box).dump());
        successes++;
    }
    log().info("structure_util display_pieces {}: {} outlines", start->key, successes);
    output.success(std::to_string(successes) + " outlines");
}

void clearDisplay(CommandOrigin const& origin, CommandOutput& output) {
    if (!commands::isAdmin(origin)) return output.error("You need operator permission.");
    gOutlines.clear();
    gLabels.reset();
    output.success("cleared");
}

/// Development (server e2e): outlines a box for `seconds` (the display_pieces outline without a
/// structure start), so the scenario can watch an outline expire.
void outlineBox(CommandOrigin const& origin, CommandOutput& output, StructureUtilOutlineParams const& p) {
    if (!commands::isAdmin(origin)) return output.error("You need operator permission.");
    int dim = 0;
    if (auto* player = commands::playerOf(origin)) dim = static_cast<int>(player->getDimensionId());
    int seconds = p.seconds > 0 ? std::min(p.seconds, 3600) : 60;
    addOutline(dim, BlockBox::of(p.x1, p.y1, p.z1, p.x2, p.y2, p.z2), "minecraft:villager_happy", 2.0f, false, seconds * 20);
    log().info("structure_util outline: {} outlines for {} s", gOutlines.size(), seconds);
    output.success("outlined");
}

/// Item#getRarity for the items whose rarity F9's rarity table does not carry: 0 common, 1
/// uncommon, 2 rare, 3 epic. F9's table (gf::item::ext::rarityOf) knows upstream's GLOAMING items (the
/// trophies and banner patterns) and what units register; this one stays until every item's
/// rarity is registered there.
///   - Minecraft 1.21.1: every Item.Properties#rarity of net.minecraft.world.item.Items (read from
///     the server-1.21.1 jar's Items.<clinit>: the Rarity before each item's registration; no other
///     class sets one), by Bedrock id where it differs (Java's light is Bedrock's light_block).
///     Every other vanilla item is common (trial keys, the ominous bottle, smithing templates).
///   - upstream 4.8: the UNCOMMON / RARE / EPIC rarities of GloamingItems and GloamingBlocks (and fiery_block, whose
///     registerFireResistantItem sets UNCOMMON).
int baseRarity(std::string const& id) {
    static std::unordered_map<std::string, int> const table = [] {
        std::unordered_map<std::string, int> t;
        // 1.21.1 UNCOMMON
        for (auto const* s : {"minecraft:elytra", "minecraft:experience_bottle", "minecraft:skeleton_skull", "minecraft:wither_skeleton_skull",
                              "minecraft:player_head", "minecraft:zombie_head", "minecraft:creeper_head", "minecraft:dragon_head",
                              "minecraft:piglin_head", "minecraft:nether_star", "minecraft:enchanted_book", "minecraft:dragon_breath",
                              "minecraft:totem_of_undying", "minecraft:heart_of_the_sea", "minecraft:creeper_banner_pattern",
                              "minecraft:skull_banner_pattern", "minecraft:piglin_banner_pattern"})
            t[s] = 1;
        // 1.21.1 RARE
        for (auto const* s : {"minecraft:beacon", "minecraft:conduit", "minecraft:golden_apple", "minecraft:end_crystal", "minecraft:music_disc_13",
                              "minecraft:music_disc_cat", "minecraft:music_disc_blocks", "minecraft:music_disc_chirp", "minecraft:music_disc_creator",
                              "minecraft:music_disc_creator_music_box", "minecraft:music_disc_far", "minecraft:music_disc_mall",
                              "minecraft:music_disc_mellohi", "minecraft:music_disc_stal", "minecraft:music_disc_strad", "minecraft:music_disc_ward",
                              "minecraft:music_disc_11", "minecraft:music_disc_wait", "minecraft:music_disc_otherside", "minecraft:music_disc_relic",
                              "minecraft:music_disc_5", "minecraft:music_disc_pigstep", "minecraft:music_disc_precipice", "minecraft:flow_banner_pattern",
                              "minecraft:guster_banner_pattern"})
            t[s] = 2;
        // 1.21.1 EPIC
        for (auto const* s : {"minecraft:heavy_core", "minecraft:dragon_egg", "minecraft:command_block", "minecraft:barrier", "minecraft:light",
                              "minecraft:light_block", "minecraft:repeating_command_block", "minecraft:chain_command_block", "minecraft:structure_void",
                              "minecraft:structure_block", "minecraft:jigsaw", "minecraft:enchanted_golden_apple", "minecraft:mace",
                              "minecraft:command_block_minecart", "minecraft:knowledge_book", "minecraft:debug_stick", "minecraft:trident",
                              "minecraft:mojang_banner_pattern"})
            t[s] = 3;
        // Bedrock's light blocks, one item per light level
        for (int level = 0; level <= 15; level++) t["minecraft:light_block_" + std::to_string(level)] = 3;
        // upstream 4.8 UNCOMMON
        for (auto const* s : {"gloamingforest:naga_scale", "gloamingforest:naga_chestplate", "gloamingforest:naga_leggings", "gloamingforest:gloaming_scepter",
                              "gloamingforest:lifedrain_scepter", "gloamingforest:zombie_scepter", "gloamingforest:fortification_scepter",
                              "gloamingforest:exanimate_essence", "gloamingforest:ore_meter", "gloamingforest:hydra_chop", "gloamingforest:fiery_blood",
                              "gloamingforest:fiery_tears", "gloamingforest:fiery_ingot", "gloamingforest:fiery_helmet", "gloamingforest:fiery_chestplate",
                              "gloamingforest:fiery_leggings", "gloamingforest:fiery_boots", "gloamingforest:fiery_sword", "gloamingforest:fiery_pickaxe",
                              "gloamingforest:diamond_minotaur_axe", "gloamingforest:meef_stroganoff", "gloamingforest:greater_potion_flask",
                              "gloamingforest:charm_of_life_1", "gloamingforest:charm_of_life_2", "gloamingforest:charm_of_keeping_1",
                              "gloamingforest:charm_of_keeping_2", "gloamingforest:charm_of_keeping_3", "gloamingforest:tower_key",
                              "gloamingforest:phantom_helmet", "gloamingforest:phantom_chestplate", "gloamingforest:alpha_yeti_fur",
                              "gloamingforest:yeti_helmet", "gloamingforest:yeti_chestplate", "gloamingforest:yeti_leggings", "gloamingforest:yeti_boots",
                              "gloamingforest:triple_bow", "gloamingforest:seeker_bow", "gloamingforest:ice_bow", "gloamingforest:ender_bow",
                              "gloamingforest:glass_sword", "gloamingforest:lamp_of_cinders", "gloamingforest:cube_talisman",
                              "gloamingforest:cube_of_annihilation", "gloamingforest:moon_dial", "gloamingforest:pocket_watch",
                              "gloamingforest:emperors_cloth", "gloamingforest:crown_splinter", "gloamingforest:mystic_crown",
                              "gloamingforest:keepsake_casket", "gloamingforest:zombie_skull_candle", "gloamingforest:skeleton_skull_candle",
                              "gloamingforest:wither_skeleton_skull_candle", "gloamingforest:creeper_skull_candle", "gloamingforest:player_skull_candle",
                              "gloamingforest:piglin_skull_candle", "gloamingforest:fiery_block"})
            t[s] = 1;
        // upstream 4.8 RARE
        for (auto const* s : {"gloamingforest:mazebreaker_pickaxe", "gloamingforest:crumble_horn",
                              "gloamingforest:peacock_feather_fan", "gloamingforest:moonworm_queen", "gloamingforest:music_disc_radiance",
                              "gloamingforest:music_disc_steps", "gloamingforest:music_disc_superstitious", "gloamingforest:music_disc_home",
                              "gloamingforest:music_disc_wayfarer", "gloamingforest:music_disc_findings", "gloamingforest:music_disc_maker",
                              "gloamingforest:music_disc_thread", "gloamingforest:music_disc_motion"})
            t[s] = 2;
        return t;
    }();
    auto it = table.find(id);
    return it != table.end() ? it->second : 0;
}

/// A vanilla block actor whose Java block entity is a Container (CountLootCommand counts those):
/// its Container; for chests, barrels and shulker boxes the FillingContainer that holds its own
/// slots; whether Java's is a RandomizableContainer (its loot table is unpacked before counting;
/// Bedrock's loot table field for all but the hopper, whose table the kit keeps).
struct VanillaContainer {
    Container*        container    = nullptr;
    FillingContainer* filling      = nullptr;
    bool              randomizable = false; ///< Bedrock's own loot table field (RandomizableBlockActorContainerBase)
    bool              hopper       = false; ///< RandomizableContainer in Java, the kit keeps its table
};
std::optional<VanillaContainer> vanillaContainer(BlockActor& actor) {
    switch (actor.getType()) {
    case BlockActorType::Chest:
    case BlockActorType::BarrelBlock:
    case BlockActorType::ShulkerBox: {
        auto* filling = static_cast<FillingContainer*>(static_cast<RandomizableBlockActorFillingContainer*>(&actor));
        return VanillaContainer{static_cast<Container*>(filling), filling, true};
    }
    case BlockActorType::Dispenser:
    case BlockActorType::Dropper:
    case BlockActorType::Crafter:
    case BlockActorType::DecoratedPot:
        return VanillaContainer{static_cast<Container*>(static_cast<RandomizableBlockActorContainer*>(&actor)), nullptr, true};
    case BlockActorType::Jukebox:
        // a Container in Java (ContainerSingleItem), not a RandomizableContainer: never unpacked
        return VanillaContainer{static_cast<Container*>(static_cast<RandomizableBlockActorContainer*>(&actor)), nullptr, false};
    case BlockActorType::Hopper:
        return VanillaContainer{static_cast<Container*>(static_cast<HopperBlockActor*>(&actor)), nullptr, false, true};
    case BlockActorType::Furnace:
    case BlockActorType::BlastFurnace:
    case BlockActorType::Smoker:
        return VanillaContainer{static_cast<Container*>(static_cast<FurnaceBlockActor*>(&actor)), nullptr, false};
    case BlockActorType::BrewingStand:
        return VanillaContainer{static_cast<Container*>(static_cast<BrewingStandBlockActor*>(&actor)), nullptr, false};
    case BlockActorType::ChiseledBookshelf:
        return VanillaContainer{static_cast<Container*>(static_cast<ChiseledBookshelfBlockActor*>(&actor)), nullptr, false};
    default:
        // not a Container in Java: ender chests, lecterns, beacons, cauldrons, campfires, item
        // frames (entities in Java), ...
        return std::nullopt;
    }
}

} // namespace

bool notCommon(ItemStack const& stack) {
    // ItemStack#getRarity through F9 (GLOAMING, units' registrations and the enchantment bump: an
    // enchanted stack is never common); a stack F9 calls common is an unenchanted item without a
    // registered rarity, whose base rarity is the table's above
    return item::ext::rarityOf(stack) != item::ext::Rarity::Common || baseRarity(stack.getTypeName()) != 0;
}

std::optional<std::vector<ItemStack>> containerItems(BlockSource& source, BlockPos const& pos) {
    // CountLootCommand#countItemsInContainer: every Container block entity, a RandomizableContainer
    // unpacked first (unpackLootTable(null): no player, no luck) through the loot contract (F10):
    // vanilla containers by the game's own unpacking (F10 rolls its tables there), a hopper by the
    // kit (Bedrock's hopper has no loot table field), custom ones (gf::inventory) by reading their
    // storage, which unpacks a pending table
    if (auto* actor = source.getBlockEntity(pos)) {
        if (auto vanilla = vanillaContainer(*actor)) {
            if (vanilla->randomizable)
                static_cast<RandomizableBlockActorContainerBase&>(*actor).unPackLootTable(source.getLevel(), *vanilla->container, source.getDimensionId(),
                                                                                          nullptr);
            if (vanilla->hopper) (void)blockentities::unpackHopper(source, pos);
            // a chest's own slots: the Container view of a paired chest spans both halves, and Java
            // counts each half's block entity on its own
            if (vanilla->filling) return std::vector<ItemStack>(vanilla->filling->mItems->begin(), vanilla->filling->mItems->end());
            std::vector<ItemStack> items;
            for (int slot = 0; slot < vanilla->container->getContainerSize(); slot++) items.push_back(vanilla->container->getItem(slot));
            return items;
        }
    }
    auto handler = inventory::find(source, pos);
    if (!handler || !handler->get) return std::nullopt;
    auto                   gpos = game::globalPos(source, pos);
    std::vector<ItemStack> items;
    for (int slot = 0; slot < handler->slots; slot++) items.push_back(handler->get(gpos, slot));
    return items;
}

std::map<std::string, int> countLootIn(BlockSource& source, landmark::Start const& start, bool showCommon) {
    // CountLootCommand: the block entities of the start box's loaded chunks, once each, that lie
    // inside any piece box, each container unpacked first
    std::vector<BlockBox> const& boxes = start.pieceBoxes;
    auto inside = [&](BlockPos const& pos) {
        return std::any_of(boxes.begin(), boxes.end(), [&](BlockBox const& b) { return b.contains(pos.x, pos.y, pos.z); });
    };
    int                        dim   = static_cast<int>(source.getDimensionId());
    BlockBox const&            total = start.box;
    std::map<std::string, int> counts;
    for (int cz = core::chunkOf(total.minZ); cz <= core::chunkOf(total.maxZ); cz++)
        for (int cx = core::chunkOf(total.minX); cx <= core::chunkOf(total.maxX); cx++) {
            if (!source.hasChunksAt(BlockPos{cx * 16 + 8, source.getMinHeight(), cz * 16 + 8}, 0, false)) continue;
            std::set<std::tuple<int, int, int>> seen;
            auto count = [&](BlockPos const& pos) {
                if (!seen.insert({pos.x, pos.y, pos.z}).second || !inside(pos)) return;
                auto items = containerItems(source, pos);
                if (!items) return;
                for (auto const& stack : *items) {
                    if (stack.isNull() || (!showCommon && !notCommon(stack))) continue;
                    counts[stack.getTypeName()] += stack.mCount;
                }
            };
            // the chunk's block entities: vanilla block actors, then the custom containers (their
            // storage is block data)
            if (auto* chunk = source.getChunk(cx, cz)) {
                std::vector<BlockPos> actors;
                for (auto const& [cbp, actor] : chunk->mBlockEntities->mMap.get())
                    if (actor) actors.push_back(actor->getPosition());
                for (auto const& pos : actors) count(pos);
            }
            // (collected first: unpacking edits block data, which must not change under the iteration)
            std::vector<BlockPos> documents;
            ::gf::feature::common::blockdata::forEachInChunk(dim, cx, cz, [&](core::GlobalPos const& gp, Json const&) { documents.push_back(game::blockPos(gp)); });
            for (auto const& pos : documents) count(pos);
        }
    return counts;
}

namespace {

void countLoot(CommandOrigin const& origin, CommandOutput& output, std::string const& key, bool showCommon) {
    auto w = where(origin, output);
    if (!w) return;
    auto start = startAt(*w, key, output);
    if (!start) return;
    auto counts = countLootIn(*w->source, *start, showCommon);
    std::vector<std::pair<std::string, int>> sorted(counts.begin(), counts.end());
    std::stable_sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) { return a.second < b.second; });
    int sum = 0;
    for (auto const& [item, n] : sorted) {
        w->player->sendMessage(item + ": " + std::to_string(n));
        sum += n;
    }
    log().info("structure_util count_loot {}: {} items", start->key, sum);
    output.success(std::to_string(sum));
}

void countTemplate(CommandOrigin const& origin, CommandOutput& output, std::string const& key) {
    auto w = where(origin, output);
    if (!w) return;
    auto start = startAt(*w, key, output);
    if (!start) return;
    std::map<std::string, int> counts;
    // TemplateStructurePiece#makeTemplateLocation: the "Template" a template piece saves (the piece
    // tags landmark::startAt hands out in data["pieces"]; F6's startPieces for a start without them)
    Json fallback = Json::array();
    bool saved    = start->data.is_object() && start->data.contains("pieces") && start->data["pieces"].is_array();
    if (!saved)
        for (auto& p : landmark::ext::startPieces(*start)) fallback.push_back(std::move(p.tag));
    Json const& tags = saved ? start->data["pieces"] : fallback;
    for (auto const& tag : tags)
        if (tag.is_object() && tag.contains("Template") && tag["Template"].is_string()) counts[tag["Template"].get<std::string>()]++;
    std::vector<std::pair<std::string, int>> sorted(counts.begin(), counts.end());
    std::stable_sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) { return a.second < b.second; });
    for (auto const& [t, n] : sorted) w->player->sendMessage(t + "    " + std::to_string(n));
    log().info("structure_util count_template {}: {} templates", start->key, counts.size());
    output.success(std::to_string(counts.size()));
}

void generatorRadius(CommandOrigin const& origin, CommandOutput& output, std::string const& key) {
    auto w = where(origin, output);
    if (!w) return;
    auto start = startAt(*w, key, output);
    if (!start) return;
    // StructureDistanceCommand: every chunk of ChunkPos.rangeClosed(center, 8) outlined in its
    // radius's colour (upstream: stained glass block displays, glass for the center; here one particle
    // kind per radius) and labelled "dx, dz" plus its radius (upstream: two text displays; here the
    // label of the chunk the operator stands in, on the action bar)
    int sectionY = core::chunkOf(w->pos.y - 16) << 4;
    int cx = core::chunkOf(start->center.x), cz = core::chunkOf(start->center.z);
    // glass, red, orange, yellow, lime, blue, purple, magenta, pink
    static char const* const particles[] = {"minecraft:endrod",           "minecraft:redstone_ore_dust_particle", "minecraft:basic_flame_particle",
                                            "minecraft:wax_particle",     "minecraft:villager_happy",             "minecraft:blue_flame_particle",
                                            "minecraft:portal_directional", "minecraft:dragon_breath_trail",      "minecraft:heart_particle"};
    for (int dx = -8; dx <= 8; dx++)
        for (int dz = -8; dz <= 8; dz++) {
            int      r = std::max(std::abs(dx), std::abs(dz));
            BlockBox box{(cx + dx) << 4, sectionY, (cz + dz) << 4, ((cx + dx) << 4) + 15, sectionY + 15, ((cz + dz) << 4) + 15};
            // 289 chunks: the bottom square of each, a particle every 8 blocks (about 3,500
            // particles a draw; full boxes would be five times that)
            addOutline(w->dim, box, particles[r], 8.0f, true);
        }
    gLabels = ChunkLabels{w->dim, cx, cz, 60 * 20, w->player->getOrCreateUniqueID().rawID};
    w->player->sendMessage(tr("commands.gloamingfeature.generator_radius.center_chunk", "Structure start's center chunk") + ": " + std::to_string(cx) + ", " +
                           std::to_string(cz));
    log().info("structure_util generator_radius {}: 289 chunks around {}, {}", start->key, cx, cz);
    output.success("ok");
}

void placeTemplate(CommandOrigin const& origin, CommandOutput& output, StructureUtilPlaceParams const& p) {
    auto w = where(origin, output);
    if (!w) return;
    if (!templates::parsed(p.template_id)) return output.error("unknown template " + p.template_id);
    BlockSourceRegion region(*w->source, w->pos);
    TemplateSettings  settings;
    settings.rotation = rotationFromIndex(p.rotation);
    settings.mirror   = static_cast<Mirror>(std::clamp(p.mirror, 0, 2));
    bool ok           = templates::place(p.template_id, region, w->pos, settings);
    log().info("structure_util place {} at {} {} {}: {}", p.template_id, w->pos.x, w->pos.y, w->pos.z, ok ? "placed" : "failed");
    if (ok) output.success("placed " + p.template_id);
    else output.error("could not place " + p.template_id);
}

void capture(CommandOrigin const& origin, CommandOutput& output, StructureUtilCaptureParams const& p) {
    auto w = where(origin, output);
    if (!w) return;
    BlockBox box = BlockBox::of(p.x1, p.y1, p.z1, p.x2, p.y2, p.z2);
    if (box.volume() > 4'000'000) return output.error("box too large");
    int  skipped = 0;
    Json doc     = templates::capture(*w->source, box, p.id, skipped);
    std::string error;
    if (!templates::add(doc, &error)) return output.error("invalid capture: " + error);
    if (skipped > 0) output.success(std::to_string(skipped) + " entities reach outside the box and were left out");
    auto colon = p.id.find(':');
    auto file  = Mod::getInstance().getSelf().getModDir() / "structures" / "captured" /
                (p.id.substr(0, colon) + "_" + p.id.substr(colon + 1) + ".gft.json");
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream(file) << doc.dump(1);
    log().info("structure_util capture {} -> {} ({} entities)", p.id, file.string(), doc.value("entities", Json::array()).size());
    output.success("captured " + p.id);
}

void jigsawCmd(CommandOrigin const& origin, CommandOutput& output, StructureUtilJigsawParams const& p) {
    auto w = where(origin, output);
    if (!w) return;
    JavaRandom        random(pure::mthGetSeed(w->pos.x, w->pos.y, w->pos.z));
    BlockSourceRegion region(*w->source, w->pos);
    // the world's terrain is the generation context: height_adjustment puts pieces on the ground
    auto context = ext::contextOf(region, random, region.seed());
    // the junction is the ground block (CampStructure: the first occupied height), the start's
    // DOWN jigsaw stands on it
    auto pieces = ext::assemble(p.pool, BlockPos{w->pos.x, w->pos.y - 1, w->pos.z}, p.depth, context);
    if (pieces.empty()) return output.error("pool " + p.pool + " produced no pieces");
    for (auto& piece : pieces) {
        JavaRandom pr(pure::mthGetSeed(piece->bbox.minX, piece->bbox.minY, piece->bbox.minZ));
        piece->postProcess(region, pr, piece->bbox, w->pos);
    }
    log().info("structure_util jigsaw {}: {} pieces", p.pool, pieces.size());
    output.success(std::to_string(pieces.size()) + " pieces");
}

/// Development: builds a shared base at the player's feet (the R19 screenshot review of the
/// tower and hollow hill goldens): "tower" (TowerMain and the pieces it grows) or "hill" (a
/// small hollow hill dug into a stone dome).
void pieceCmd(CommandOrigin const& origin, CommandOutput& output, StructureUtilPieceParams const& p) {
    auto w = where(origin, output);
    if (!w) return;
    BlockSourceRegion region(*w->source, w->pos);
    JavaRandom        random(pure::mthGetSeed(w->pos.x, w->pos.y, w->pos.z));
    bases::PieceList  pieces;
    if (p.kind == "tower") {
        auto main = std::make_unique<bases::TowerMain>(random, 0, w->pos.x, w->pos.y, w->pos.z);
        auto* raw = main.get();
        pieces.push_back(std::move(main));
        raw->addChildren(*raw, pieces, random);
    } else if (p.kind == "hill") {
        auto hill = std::make_unique<bases::HollowHill>("gloamingforest:GloamingHill", 0, 1, w->pos.x, w->pos.y, w->pos.z, "gloamingforest:small_hollow_hill");
        // the hollow the terrain (U2) digs for the hill: a stone shell around the cave between the
        // component's floor and ceiling heights (HollowHillComponent's floorY / ceilingY curves)
        BlockBox const& b      = hill->bbox;
        BlockPos        center = boxCenter(b);
        Block const&    stone  = blocks::java("minecraft:stone");
        int const       radius = 18, hdiam = 48, hillSize = 1;
        for (int x = b.minX; x <= b.maxX; x++)
            for (int z = b.minZ; z <= b.maxZ; z++) {
                float d = static_cast<float>((x - center.x) * (x - center.x) + (z - center.z) * (z - center.z));
                if (d > static_cast<float>(radius * radius)) continue;
                float c       = pure::mthCos(std::sqrt(d) / static_cast<float>(hdiam) * pure::kMthPi);
                int   ceiling = b.minY + pure::mthCeilF(c * (static_cast<float>(hdiam) / 4.0f));
                int   floor   = b.minY + pure::mthFloorF(static_cast<float>(hillSize * 2) - c * (static_cast<float>(hdiam) / 20.0f) + 1 + 0.25f);
                for (int y = b.minY - 1; y <= ceiling + 2; y++)
                    region.setBlock(BlockPos{x, y, z}, y <= floor || y >= ceiling ? stone : blocks::air(), 2);
            }
        pieces.push_back(std::move(hill));
    } else {
        return output.error("unknown piece kind " + p.kind + " (tower | hill)");
    }
    for (auto& piece : pieces) {
        JavaRandom pr(pure::mthGetSeed(piece->bbox.minX, piece->bbox.minY, piece->bbox.minZ));
        piece->postProcess(region, pr, piece->bbox, w->pos);
    }
    BlockBox all = pieces.front()->bbox;
    for (auto const& piece : pieces)
        all = BlockBox::of(std::min(all.minX, piece->bbox.minX), std::min(all.minY, piece->bbox.minY), std::min(all.minZ, piece->bbox.minZ),
                           std::max(all.maxX, piece->bbox.maxX), std::max(all.maxY, piece->bbox.maxY), std::max(all.maxZ, piece->bbox.maxZ));
    log().info("structure_util piece {}: {} pieces in {}", p.kind, pieces.size(), boxToJson(all).dump());
    output.success(std::to_string(pieces.size()) + " pieces");
}

/// Development (server e2e restart step): queues a job in an unloaded chunk of the overworld
/// (4096 blocks east of the world spawn) that only runs once that chunk is loaded; the selftest
/// structure.pending_read loads it after the restart and checks that the job ran.
void pendingProbe(CommandOrigin const& origin, CommandOutput& output) {
    if (!commands::isAdmin(origin)) return output.error("You need operator permission.");
    auto level = ll::service::getLevel();
    if (!level) return output.error("no level");
    auto const& spawn = level->getDefaultSpawn();
    BlockPos    at{spawn.x + 4096, 64, spawn.z};
    auto*       source = game::regionOf(0);
    if (source && source->hasChunksAt(at, 0, false)) return output.error("the probe chunk is loaded");
    std::random_device rd;
    auto               nonce = std::to_string(rd()) + std::to_string(rd());
    // a landmark start whose pieces must load through F6's piece factories after the restart (the
    // open fixture landmark, 512 blocks south of the probe chunk, out of reach of the chunks
    // structure.pending_read loads; it stays unplaced until pending_read forgets it)
    core::GlobalPos startPos{0, at.x, at.y, at.z + 512};
    if (source && source->hasChunksAt(BlockPos{startPos.x, startPos.y, startPos.z}, 0, false)) return output.error("the probe start's chunk is loaded");
    blockentities::queueTestMark(0, at, nonce);
    Json probe{{"nonce", nonce}, {"dim", 0}, {"pos", {at.x, at.y, at.z}}};
    if (auto forced = landmark::forceStart("gf_test:landmark_fixture_open", startPos); forced && !forced->pieceBoxes.empty())
        probe["start"] = Json{{"key", forced->key}, {"pos", {startPos.x, startPos.y, startPos.z}}, {"pieces", forced->pieceBoxes.size()}};
    game::db::put("gf:structure:probe", probe);
    log().info("structure_util pending_probe queued nonce={} at {} {} {} ({} jobs pending, start probe {})", nonce, at.x, at.y, at.z,
               blockentities::pending(), probe.contains("start") ? "stored" : "not stored");
    output.success("queued " + nonce);
}

/// Development (server e2e restart step): the next structure.scale_probe run leaves a scaled mob
/// in the world for the run after the restart (plain selftest runs leave nothing behind).
void scaleProbe(CommandOrigin const& origin, CommandOutput& output) {
    if (!commands::isAdmin(origin)) return output.error("You need operator permission.");
    if (!game::db::available()) return output.error("no world");
    game::db::put("gf:structure:scale_probe_armed", Json(true));
    log().info("structure_util scale_probe armed");
    output.success("armed");
}

void listTemplates(CommandOrigin const& origin, CommandOutput& output) {
    if (!commands::isAdmin(origin)) return output.error("You need operator permission.");
    auto ids = templates::ids();
    log().info("structure kit: {} templates, {} template definitions, {} wood palettes, {} speleothem configs, {} processors, {} markers, {} piece types",
               ids.size(), pools::definitionCount(), palettes::ids().size(), speleothems::ids().size(), processorTypes().size(), markerTypes().size(),
               pieceTypes().size());
    output.success(std::to_string(ids.size()) + " templates");
}

} // namespace

Json templates::capture(BlockSource& source, BlockBox const& box, std::string const& id, int& skipped) {
    // palette of Java states: Bedrock blocks are written by their Bedrock id and states (an
    // original build captured in game; tools/structures reads both)
    std::map<std::string, int> paletteIndex;
    Json                       palette = Json::array();
    std::vector<int>           cells;
    for (int y = box.minY; y <= box.maxY; y++)
        for (int z = box.minZ; z <= box.maxZ; z++)
            for (int x = box.minX; x <= box.maxX; x++) {
                auto const& b     = source.getBlock(BlockPos{x, y, z});
                Json        entry = Json{{"Name", b.getTypeName()}};
                auto        st    = game::statesOf(b);
                if (!st.empty()) entry["Properties"] = st;
                auto key = entry.dump();
                auto it  = paletteIndex.find(key);
                if (it == paletteIndex.end()) {
                    it = paletteIndex.emplace(key, static_cast<int>(palette.size())).first;
                    palette.push_back(entry);
                }
                cells.push_back(it->second);
            }
    Json blocksRle = Json::array();
    for (std::size_t i = 0; i < cells.size();) {
        std::size_t j = i;
        while (j < cells.size() && cells[j] == cells[i]) j++;
        blocksRle.push_back(Json::array({cells[i], static_cast<int>(j - i)}));
        i = j;
    }
    int  sx = box.maxX - box.minX + 1, sy = box.maxY - box.minY + 1, sz = box.maxZ - box.minZ + 1;
    Json doc{{"format", "gloamingforest:template"}, {"format_version", 1}, {"id", id}, {"size", Json::array({sx, sy, sz})},
             {"palette", palette},              {"blocks", blocksRle}};
    // StructureTemplate#fillEntityList (includeEntities): the entities in the box but players, a
    // painting by the block it covers (HangingEntity#pos), every other entity by its position
    Json entities = Json::array();
    skipped       = 0;
    AABB range{Vec3{static_cast<float>(box.minX), static_cast<float>(box.minY), static_cast<float>(box.minZ)},
               Vec3{static_cast<float>(box.maxX + 1), static_cast<float>(box.maxY + 1), static_cast<float>(box.maxZ + 1)}};
    for (auto* actor : source.getEntities(range, 0.0f)) {
        if (!actor || actor->isType(ActorType::Player)) continue;
        if (actor->isType(ActorType::Painting)) {
            auto hung = blockentities::describePainting(*actor);
            if (!hung || !box.contains(hung->block.x, hung->block.y, hung->block.z)) {
                skipped++;
                continue;
            }
            Json at = Json::array({hung->block.x - box.minX, hung->block.y - box.minY, hung->block.z - box.minZ});
            entities.push_back(Json{{"pos", at},
                                    {"id", "minecraft:painting"},
                                    {"data", {{"block_pos", at}, {"facing", blocks::directionName(hung->facing)}, {"variant", hung->variant}}}});
            continue;
        }
        Vec3   feet = actor->getFeetPos();
        double x = feet.x - box.minX, y = feet.y - box.minY, z = feet.z - box.minZ;
        if (x < 0 || y < 0 || z < 0 || x > sx || y > sy || z > sz) {
            skipped++;
            continue;
        }
        // StructureEntityInfo#nbt: the actor's saved data (its position, rotation and identity are
        // the placement's) and its rotation as [yaw, pitch]
        Vec2 const& rot  = actor->getRotation();
        Json        data = Json{{"Rotation", Json::array({rot.y, rot.x})}};
        CompoundTag tag;
        if (actor->save(tag)) {
            for (char const* k : {"UniqueID", "Pos", "Rotation", "LinksTag", "LeasherID"}) tag.erase(k);
            data["nbt"] = tag.toSnbt(SnbtFormat::Minimize);
        }
        entities.push_back(Json{{"pos", Json::array({x, y, z})}, {"id", actor->getTypeName()}, {"data", std::move(data)}});
    }
    if (!entities.empty()) doc["entities"] = entities;
    return doc;
}

void outlines::tick() { drawOutlines(); }

void outlines::shutdown() {
    gOutlines.clear();
    gLabels.reset();
}

GF_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload<StructureUtilKeyParams>().text("structure_util").text("display_pieces").required("filter_structure").execute(
        [](CommandOrigin const& o, CommandOutput& out, StructureUtilKeyParams const& p) { displayPieces(o, out, p.filter_structure); }
    );
    cmd.overload().text("structure_util").text("clear_display").execute([](CommandOrigin const& o, CommandOutput& out) { clearDisplay(o, out); });
    cmd.overload<StructureUtilKeyParams>().text("structure_util").text("count_loot").required("filter_structure").execute(
        [](CommandOrigin const& o, CommandOutput& out, StructureUtilKeyParams const& p) { countLoot(o, out, p.filter_structure, true); }
    );
    cmd.overload<StructureUtilLootParams>().text("structure_util").text("count_loot").required("filter_structure").required("show_common").execute(
        [](CommandOrigin const& o, CommandOutput& out, StructureUtilLootParams const& p) { countLoot(o, out, p.filter_structure, p.show_common); }
    );
    cmd.overload<StructureUtilKeyParams>().text("structure_util").text("count_template").required("filter_structure").execute(
        [](CommandOrigin const& o, CommandOutput& out, StructureUtilKeyParams const& p) { countTemplate(o, out, p.filter_structure); }
    );
    cmd.overload<StructureUtilKeyParams>().text("structure_util").text("generator_radius").required("filter_structure").execute(
        [](CommandOrigin const& o, CommandOutput& out, StructureUtilKeyParams const& p) { generatorRadius(o, out, p.filter_structure); }
    );
    cmd.overload<StructureUtilPlaceParams>().text("structure_util").text("place").required("template_id").optional("rotation").optional("mirror").execute(
        [](CommandOrigin const& o, CommandOutput& out, StructureUtilPlaceParams const& p) { placeTemplate(o, out, p); }
    );
    cmd.overload<StructureUtilCaptureParams>()
        .text("structure_util")
        .text("capture")
        .required("id")
        .required("x1")
        .required("y1")
        .required("z1")
        .required("x2")
        .required("y2")
        .required("z2")
        .execute([](CommandOrigin const& o, CommandOutput& out, StructureUtilCaptureParams const& p) { capture(o, out, p); });
    cmd.overload<StructureUtilJigsawParams>().text("structure_util").text("jigsaw").required("pool").optional("depth").execute(
        [](CommandOrigin const& o, CommandOutput& out, StructureUtilJigsawParams const& p) { jigsawCmd(o, out, p); }
    );
    cmd.overload().text("structure_util").text("templates").execute([](CommandOrigin const& o, CommandOutput& out) { listTemplates(o, out); });
    cmd.overload<StructureUtilPieceParams>().text("structure_util").text("piece").required("kind").execute(
        [](CommandOrigin const& o, CommandOutput& out, StructureUtilPieceParams const& p) { pieceCmd(o, out, p); }
    );
    cmd.overload().text("structure_util").text("pending_probe").execute([](CommandOrigin const& o, CommandOutput& out) { pendingProbe(o, out); });
    cmd.overload().text("structure_util").text("scale_probe").execute([](CommandOrigin const& o, CommandOutput& out) { scaleProbe(o, out); });
    cmd.overload<StructureUtilOutlineParams>()
        .text("structure_util")
        .text("outline")
        .required("x1")
        .required("y1")
        .required("z1")
        .required("x2")
        .required("y2")
        .required("z2")
        .optional("seconds")
        .execute([](CommandOrigin const& o, CommandOutput& out, StructureUtilOutlineParams const& p) { outlineBox(o, out, p); });
});

} // namespace gf::structure
