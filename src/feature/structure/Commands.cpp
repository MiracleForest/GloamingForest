// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/command/TFCommand.java (structure_util branch)
//   src/main/java/twilightforest/command/DisplayPiecesCommand.java
//   src/main/java/twilightforest/command/ClearDisplayCommand.java
//   src/main/java/twilightforest/command/CountLootCommand.java
//   src/main/java/twilightforest/command/CountTemplateCommand.java
//   src/main/java/twilightforest/command/StructureDistanceCommand.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// /twilightforest structure_util (operators):
//   display_pieces <structure>       outlines the start and its pieces around you (TF: block
//                                    displays; Bedrock: particle outlines for a minute, piece list in chat)
//   clear_display                    stops the outlines
//   count_loot <structure> [common]  counts the items in the start's containers
//   count_template <structure>       counts the templates of the start's pieces
//   generator_radius <structure>     outlines the chunks 0..8 around the start's chunk
// GloamingForest development additions:
//   place <template> [rotation] [mirror]  places a template at your feet
//   capture <id> <x1> <y1> <z1> <x2> <y2> <z2>  saves a box as a .gft.json template (original builds)
//   jigsaw <pool> [depth]            assembles a template pool at your feet
//   templates                        lists the known templates, pools and data counts
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
#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/network/packet/TextPacket.h"
#include "mc/network/packet/TextPacketType.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/chunk/LevelChunkBlockActorStorage.h"
#include "mc/world/level/dimension/Dimension.h"

#include "core/Keys.h"
#include "feature/common/BlockData.h"
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

namespace {

/// Outlines drawn with particles until cleared or expired (TF's block displays).
struct Outline {
    int         dim;
    BlockBox    box;
    std::string particle;
    int         ticksLeft;
    float       spacing = 2.0f; ///< blocks between two particles of an edge
};
/// generator_radius's chunk labels (TF's text displays): shown on the action bar of the
/// operator while they stand in a labelled chunk.
struct ChunkLabels {
    int                         dim = 0;
    int                         centerX = 0, centerZ = 0; ///< the start's chunk
    int                         ticksLeft = 0;
    std::int64_t                playerId  = 0;            ///< ActorUniqueID of the operator
};
std::vector<Outline>       gOutlines;
std::optional<ChunkLabels> gLabels;
ll::event::ListenerPtr     gOutlineTick;
int                        gTickCounter = 0;

void stopTicking() {
    if (gOutlineTick) ll::event::EventBus::getInstance().removeListener(gOutlineTick);
    gOutlineTick.reset();
}

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
    std::string radius = r == 0 ? tr("commands.tffeature.generator_radius.center_chunk", "Structure start's center chunk")
                                : tr("commands.tffeature.generator_radius.radius", "Radius from center chunk: %s", r);
    // a tip (action bar text) while the operator stands in the chunk
    auto        pkt = TextPacket{};
    TextPacketPayload::MessageOnly message;
    message.mType = TextPacketType::Tip;
    message.mMessage->assign(std::to_string(diffX) + ", " + std::to_string(diffZ) + "\n" + radius);
    pkt.mBody = message;
    pkt.sendTo(*player);
}

void drawOutlines() {
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
        edge(x0, y1, z0, x1, y1, z0);
        edge(x0, y1, z1, x1, y1, z1);
        edge(x0, y0, z0, x0, y1, z0);
        edge(x1, y0, z0, x1, y1, z0);
        edge(x0, y0, z1, x0, y1, z1);
        edge(x1, y0, z1, x1, y1, z1);
        edge(x0, y0, z0, x0, y0, z1);
        edge(x1, y0, z0, x1, y0, z1);
        edge(x0, y1, z0, x0, y1, z1);
        edge(x1, y1, z0, x1, y1, z1);
    }
    std::erase_if(gOutlines, [](Outline const& o) { return o.ticksLeft <= 0; });
    showLabels(*level);
    if (gLabels && gLabels->ticksLeft <= 0) gLabels.reset();
    if (gOutlines.empty() && !gLabels) stopTicking();
}

void startTicking() {
    if (!gOutlineTick)
        gOutlineTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { drawOutlines(); });
}

void addOutline(int dim, BlockBox const& box, std::string particle, float spacing = 2.0f) {
    gOutlines.push_back(Outline{dim, box, std::move(particle), 60 * 20, spacing});
    startTicking();
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
        output.error(tr("commands.tffeature.structure.outside", "You are not in a structure."));
        return std::nullopt;
    }
    return start;
}

/// The saved pieces of a start ({"pieces": [Piece#save]} in the start's data), or its boxes.
std::vector<Json> piecesOf(landmark::Start const& start) {
    std::vector<Json> out;
    if (start.data.is_object() && start.data.contains("pieces") && start.data["pieces"].is_array())
        for (auto const& p : start.data["pieces"]) out.push_back(p);
    if (out.empty())
        for (auto const& b : start.pieceBoxes) out.push_back(Json{{"BB", boxToJson(b)}});
    return out;
}

void displayPieces(CommandOrigin const& origin, CommandOutput& output, std::string const& key) {
    auto w = where(origin, output);
    if (!w) return;
    auto start = startAt(*w, key, output);
    if (!start) return;
    int successes = 1;
    addOutline(w->dim, start->box, "minecraft:redstone_ore_dust_particle");
    auto pieces = piecesOf(*start);
    for (auto const& p : pieces) {
        BlockBox box = boxFromJson(p.value("BB", Json::array()));
        std::string id = p.value("id", std::string{});
        bool protectedPiece = !id.empty() && start->generated; // ProgressionPiece#isComponentProtected defaults to true
        addOutline(w->dim, box, protectedPiece ? "minecraft:villager_happy" : "minecraft:water_wake_particle");
        w->player->sendMessage((id.empty() ? tr("commands.tffeature.display_pieces.missing_key", "missing key") : id) + " " + boxToJson(box).dump());
        successes++;
    }
    log().info("structure_util display_pieces {}: {} outlines", start->key, successes);
    output.success(std::to_string(successes) + " outlines");
}

void clearDisplay(CommandOrigin const& origin, CommandOutput& output) {
    if (!commands::isAdmin(origin)) return output.error("You need operator permission.");
    gOutlines.clear();
    gLabels.reset();
    stopTicking();
    output.success("cleared");
}

/// Item#getRarity of 1.21.1 (vanilla Items and TF's TFItems / TFBlocks rarities): 0 common,
/// 1 uncommon, 2 rare, 3 epic.
int baseRarity(std::string const& id) {
    static std::unordered_map<std::string, int> const table = [] {
        std::unordered_map<std::string, int> t;
        for (auto const* s : {"minecraft:experience_bottle", "minecraft:dragon_breath", "minecraft:heart_of_the_sea", "minecraft:totem_of_undying",
                              "minecraft:creeper_head", "minecraft:zombie_head", "minecraft:skeleton_skull", "minecraft:wither_skeleton_skull",
                              "minecraft:player_head", "minecraft:dragon_head", "minecraft:piglin_head", "minecraft:nether_star",
                              "minecraft:enchanted_book", "minecraft:trial_key", "minecraft:ominous_trial_key", "minecraft:ominous_bottle",
                              "minecraft:music_disc_13", "minecraft:music_disc_cat", "minecraft:music_disc_blocks", "minecraft:music_disc_chirp",
                              "minecraft:music_disc_far", "minecraft:music_disc_mall", "minecraft:music_disc_mellohi", "minecraft:music_disc_stal",
                              "minecraft:music_disc_strad", "minecraft:music_disc_ward", "minecraft:music_disc_11", "minecraft:music_disc_wait",
                              "minecraft:music_disc_otherside", "minecraft:music_disc_5", "minecraft:music_disc_pigstep", "minecraft:music_disc_relic",
                              "minecraft:music_disc_creator", "minecraft:music_disc_creator_music_box", "minecraft:music_disc_precipice",
                              "twilightforest:naga_scale", "twilightforest:naga_chestplate", "twilightforest:naga_leggings", "twilightforest:twilight_scepter",
                              "twilightforest:lifedrain_scepter", "twilightforest:zombie_scepter", "twilightforest:fortification_scepter",
                              "twilightforest:exanimate_essence", "twilightforest:ore_meter", "twilightforest:hydra_chop", "twilightforest:fiery_blood",
                              "twilightforest:fiery_tears", "twilightforest:fiery_ingot", "twilightforest:fiery_helmet", "twilightforest:fiery_chestplate",
                              "twilightforest:fiery_leggings", "twilightforest:fiery_boots", "twilightforest:fiery_sword", "twilightforest:fiery_pickaxe",
                              "twilightforest:diamond_minotaur_axe", "twilightforest:meef_stroganoff", "twilightforest:greater_potion_flask",
                              "twilightforest:charm_of_life_1", "twilightforest:charm_of_life_2", "twilightforest:charm_of_keeping_1",
                              "twilightforest:charm_of_keeping_2", "twilightforest:charm_of_keeping_3", "twilightforest:tower_key",
                              "twilightforest:phantom_helmet", "twilightforest:phantom_chestplate", "twilightforest:alpha_yeti_fur",
                              "twilightforest:yeti_helmet", "twilightforest:yeti_chestplate", "twilightforest:yeti_leggings", "twilightforest:yeti_boots",
                              "twilightforest:triple_bow", "twilightforest:seeker_bow", "twilightforest:ice_bow", "twilightforest:ender_bow",
                              "twilightforest:glass_sword", "twilightforest:lamp_of_cinders", "twilightforest:cube_talisman",
                              "twilightforest:cube_of_annihilation", "twilightforest:moon_dial", "twilightforest:pocket_watch",
                              "twilightforest:emperors_cloth", "twilightforest:crown_splinter", "twilightforest:mystic_crown",
                              "twilightforest:keepsake_casket", "twilightforest:zombie_skull_candle", "twilightforest:skeleton_skull_candle",
                              "twilightforest:wither_skeleton_skull_candle", "twilightforest:creeper_skull_candle", "twilightforest:player_skull_candle",
                              "twilightforest:piglin_skull_candle"})
            t[s] = 1;
        for (auto const* s : {"minecraft:golden_apple", "minecraft:beacon", "minecraft:conduit", "minecraft:end_crystal", "minecraft:trident",
                              "minecraft:heavy_core", "twilightforest:mazebreaker_pickaxe", "twilightforest:crumble_horn",
                              "twilightforest:peacock_feather_fan", "twilightforest:moonworm_queen", "twilightforest:music_disc_radiance",
                              "twilightforest:music_disc_steps", "twilightforest:music_disc_superstitious", "twilightforest:music_disc_home",
                              "twilightforest:music_disc_wayfarer", "twilightforest:music_disc_findings", "twilightforest:music_disc_maker",
                              "twilightforest:music_disc_thread", "twilightforest:music_disc_motion"})
            t[s] = 2;
        for (auto const* s : {"minecraft:enchanted_golden_apple", "minecraft:elytra", "minecraft:dragon_egg", "minecraft:command_block",
                              "minecraft:chain_command_block", "minecraft:repeating_command_block", "minecraft:command_block_minecart",
                              "minecraft:structure_block", "minecraft:jigsaw", "minecraft:barrier", "minecraft:light_block", "minecraft:debug_stick",
                              "minecraft:knowledge_book", "minecraft:mace"})
            t[s] = 3;
        return t;
    }();
    auto it = table.find(id);
    if (it != table.end()) return it->second;
    // TF's trophies (TFBlocks: fireResistant().rarity(UNCOMMON))
    if (id.starts_with("twilightforest:") && id.ends_with("_trophy")) return 1;
    return 0;
}

/// ItemStack#getRarity: enchanted common / uncommon items are rare, rare ones epic.
int rarityOf(ItemStack const& stack) {
    int r = baseRarity(stack.getTypeName());
    if (!stack.isEnchanted()) return r;
    return r <= 1 ? 2 : 3;
}

void countLoot(CommandOrigin const& origin, CommandOutput& output, std::string const& key, bool showCommon) {
    auto w = where(origin, output);
    if (!w) return;
    auto start = startAt(*w, key, output);
    if (!start) return;
    // CountLootCommand: the block entities of the start box's loaded chunks, once each, that lie
    // inside any piece box; a container's unrolled loot table is unpacked first
    std::vector<BlockBox> boxes;
    for (auto const& p : piecesOf(*start)) boxes.push_back(boxFromJson(p.value("BB", Json::array())));
    auto inside = [&](BlockPos const& pos) {
        return std::any_of(boxes.begin(), boxes.end(), [&](BlockBox const& b) { return b.contains(pos.x, pos.y, pos.z); });
    };
    BlockBox const& total = start->box;
    std::map<std::string, int> counts;
    for (int cz = core::chunkOf(total.minZ); cz <= core::chunkOf(total.maxZ); cz++)
        for (int cx = core::chunkOf(total.minX); cx <= core::chunkOf(total.maxX); cx++) {
            if (!w->source->hasChunksAt(BlockPos{cx * 16 + 8, w->source->getMinHeight(), cz * 16 + 8}, 0, false)) continue;
            std::set<std::tuple<int, int, int>> seen;
            auto count = [&](BlockPos const& pos) {
                if (!seen.insert({pos.x, pos.y, pos.z}).second || !inside(pos)) return;
                auto handler = inventory::find(*w->source, pos);
                if (!handler || !handler->get) return;
                auto gpos = game::globalPos(*w->source, pos);
                // RandomizableContainer#unpackLootTable: the table F7 gave it, rolled now
                bool empty = true;
                for (int slot = 0; slot < handler->slots && empty; slot++) empty = handler->get(gpos, slot).isNull();
                if (auto record = blockentities::lootRecord(w->dim, pos)) {
                    if (empty && !record->table.empty() && handler->set) {
                        loot::LootContext ctx;
                        ctx.region = w->source;
                        ctx.origin = Vec3{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
                        ctx.player = w->player;
                        ctx.seed   = record->seed;
                        auto items = loot::roll(record->table, ctx);
                        for (int slot = 0; slot < handler->slots && slot < static_cast<int>(items.size()); slot++) handler->set(gpos, slot, items[slot]);
                        if (handler->setLootTable) handler->setLootTable(gpos, std::string{}, 0);
                    }
                    blockentities::forgetLootRecord(w->dim, pos);
                }
                for (int slot = 0; slot < handler->slots; slot++) {
                    ItemStack stack = handler->get(gpos, slot);
                    if (stack.isNull() || (!showCommon && rarityOf(stack) == 0)) continue;
                    counts[stack.getTypeName()] += stack.mCount;
                }
            };
            // the chunk's block entities: vanilla block actors, then the custom containers F7 placed
            if (auto* chunk = w->source->getChunk(cx, cz)) {
                std::vector<BlockPos> actors;
                for (auto const& [cbp, actor] : chunk->mBlockEntities->mMap.get())
                    if (actor) actors.push_back(actor->getPosition());
                for (auto const& pos : actors) count(pos);
            }
            ::gf::feature::common::blockdata::forEachInChunk(w->dim, cx, cz, [&](core::GlobalPos const& gp, Json const&) { count(game::blockPos(gp)); });
        }
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
    for (auto const& p : piecesOf(*start))
        if (p.contains("Template")) counts[p["Template"].get<std::string>()]++;
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
    // radius's colour (TF: stained glass block displays, glass for the center; here one particle
    // kind per radius) and labelled "dx, dz" plus its radius (TF: two text displays; here the
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
            // 289 chunks: a particle every 4 blocks keeps the outlines readable and the packets few
            addOutline(w->dim, box, particles[r], 4.0f);
        }
    gLabels = ChunkLabels{w->dim, cx, cz, 60 * 20, w->player->getOrCreateUniqueID().rawID};
    startTicking();
    w->player->sendMessage(tr("commands.tffeature.generator_radius.center_chunk", "Structure start's center chunk") + ": " + std::to_string(cx) + ", " +
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
    // palette of Java states: Bedrock blocks are written by their Bedrock id and states (an
    // original build captured in game; tools/structures reads both)
    std::map<std::string, int> paletteIndex;
    Json                       palette = Json::array();
    std::vector<int>           cells;
    for (int y = box.minY; y <= box.maxY; y++)
        for (int z = box.minZ; z <= box.maxZ; z++)
            for (int x = box.minX; x <= box.maxX; x++) {
                auto const& b     = w->source->getBlock(BlockPos{x, y, z});
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
    Json doc{{"format", "gloamingforest:template"},
             {"format_version", 1},
             {"id", p.id},
             {"size", Json::array({box.maxX - box.minX + 1, box.maxY - box.minY + 1, box.maxZ - box.minZ + 1})},
             {"palette", palette},
             {"blocks", blocksRle}};
    std::string error;
    if (!templates::add(doc, &error)) return output.error("invalid capture: " + error);
    auto colon = p.id.find(':');
    auto file  = Mod::getInstance().getSelf().getModDir() / "structures" / "captured" /
                (p.id.substr(0, colon) + "_" + p.id.substr(colon + 1) + ".gft.json");
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream(file) << doc.dump(1);
    log().info("structure_util capture {} -> {}", p.id, file.string());
    output.success("captured " + p.id);
}

void jigsawCmd(CommandOrigin const& origin, CommandOutput& output, StructureUtilJigsawParams const& p) {
    auto w = where(origin, output);
    if (!w) return;
    JavaRandom        random(pure::mthGetSeed(w->pos.x, w->pos.y, w->pos.z));
    BlockSourceRegion region(*w->source, w->pos);
    // the world's terrain is the generation context: height_adjustment puts pieces on the ground
    auto context = ext::contextOf(region, random, region.seed());
    auto pieces  = ext::assemble(p.pool, w->pos, p.depth, context);
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
        auto hill = std::make_unique<bases::HollowHill>("twilightforest:TFHill", 0, 1, w->pos.x, w->pos.y, w->pos.z, "twilightforest:small_hollow_hill");
        // the hill digs into the ground: a stone dome over it first
        BlockBox const& b = hill->bbox;
        Block const&    stone = blocks::java("minecraft:stone");
        for (int x = b.minX; x <= b.maxX; x++)
            for (int z = b.minZ; z <= b.maxZ; z++)
                for (int y = b.minY; y <= b.maxY; y++) region.setBlock(BlockPos{x, y, z}, stone, 2);
        pieces.push_back(std::move(hill));
    } else {
        return output.error("unknown piece kind " + p.kind + " (tower | hill)");
    }
    for (auto& piece : pieces) {
        JavaRandom pr(pure::mthGetSeed(piece->bbox.minX, piece->bbox.minY, piece->bbox.minZ));
        piece->postProcess(region, pr, piece->bbox, w->pos);
    }
    log().info("structure_util piece {}: {} pieces", p.kind, pieces.size());
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
    blockentities::queueTestMark(0, at, nonce);
    game::db::put("gf:structure:probe", Json{{"nonce", nonce}, {"dim", 0}, {"pos", {at.x, at.y, at.z}}});
    log().info("structure_util pending_probe queued nonce={} at {} {} {} ({} jobs pending)", nonce, at.x, at.y, at.z, blockentities::pending());
    output.success("queued " + nonce);
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

void outlines::shutdown() {
    gOutlines.clear();
    gLabels.reset();
    stopTicking();
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
});

} // namespace gf::structure
