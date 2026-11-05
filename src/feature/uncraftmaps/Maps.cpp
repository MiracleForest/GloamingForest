// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/item/EmptyMagicMapItem.java
//   src/main/java/twilightforest/item/EmptyMazeMapItem.java
//   src/main/java/twilightforest/item/MagicMapItem.java
//   src/main/java/twilightforest/item/MazeMapItem.java
//   src/main/java/twilightforest/item/mapdata/TFMagicMapData.java
//   src/main/java/twilightforest/item/mapdata/TFMazeMapData.java
//   src/main/java/twilightforest/item/recipe/MagicMapCloningRecipe.java
//   src/main/java/twilightforest/item/recipe/MazeMapCloningRecipe.java
//   src/main/java/twilightforest/network/MagicMapPacket.java
//   src/main/java/twilightforest/network/MazeMapPacket.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// TF's magic, maze and ore maps (see Maps.h for how they live on Bedrock's map item).
//
// - The empty maps (twilightforest:magic_map, maze_map, ore_map) make a filled map on use.
// - Every server tick each player's selected and off-hand TF maps update as MapItem#inventoryTick
//   does (update() while held): the magic map paints the biomes' magic_map_color around the holder
//   and records the landmark icons it passes (with their conquered state), the maze / ore map paints
//   one column stripe of blocks at its yCenter per tick (ores by ore_map_color with the ore map).
// - Changed pixels reach the holder in the map's full data packet (at most every 5 ticks); the TF
//   decorations reach companions over "uncraftmaps.magic_map" (MagicMapPacket's conquered list)
//   and vanilla clients as vanilla map decorations (vanillaDecorationFor; an X over a conquered one).
// - The map records (kind, yCenter, decorations) are kept in the world DB
//   (gf:uncraftmaps:map:<id>).
#include "feature/uncraftmaps/Maps.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/Packet.h"
#include "mc/network/packet/SetTitlePacket.h"
#include "mc/safety/RedactableString.h"
#include "mc/world/actor/player/AbilitiesIndex.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/MapItem.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/saveddata/maps/MapDecoration.h"
#include "mc/world/level/saveddata/maps/MapItemSavedData.h"
#include "mc/world/level/saveddata/maps/MapItemTrackedActor.h"

#include "core/landmark/Grid.h"
#include "core/landmark/Landmark.h"
#include "core/uncraftmaps/Maps.h"
#include "feature/datamaps/DataMaps.h"
#include "feature/dimension/World.h"
#include "feature/ext/item/Recipes.h"
#include "feature/fx/Fx.h"
#include "feature/item/Item.h"
#include "feature/landmark/Landmark.h"
#include "feature/net/Net.h"
#include "feature/recipes/Recipes.h"
#include "feature/stats/Stats.h"
#include "feature/tags/Tags.h"
#include "feature/worldgen/Worldgen.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::uncraftmaps {

namespace {

namespace items = game::items;
namespace cu    = core::uncraftmaps;
namespace cl    = core::landmark;
using Json      = nlohmann::json;

constexpr char kFilledMap[] = "minecraft:filled_map";

// ---- records -----------------------------------------------------------------------------------------

struct MapRecord {
    MapKind                              kind    = MapKind::Magic;
    int                                  yCenter = 0;
    std::map<std::string, TfDecoration>  decorations; ///< by MagicMapItem#makeName
    std::unordered_map<std::int64_t, int> steps;      ///< HoldingPlayer#step per holder
    bool                                 dirty = false; ///< to save
    bool                                 decorationsChanged = true;
    std::unordered_map<std::int64_t, std::uint64_t> sent; ///< last full packet tick per holder
    bool                                 pixelsChanged = false;
    std::unordered_map<std::int64_t, bool> mazeSent; ///< MazeMapPacket sent to a companion holder
};

std::map<std::int64_t, MapRecord>& records() {
    static std::map<std::int64_t, MapRecord> r;
    return r;
}

std::string keyOf(std::int64_t id) { return "gf:uncraftmaps:map:" + std::to_string(id); }

char const* kindName(MapKind k) { return k == MapKind::Magic ? "magic" : k == MapKind::Maze ? "maze" : "ore"; }
std::optional<MapKind> kindFrom(std::string const& s) {
    if (s == "magic") return MapKind::Magic;
    if (s == "maze") return MapKind::Maze;
    if (s == "ore") return MapKind::Ore;
    return std::nullopt;
}

Json recordJson(MapRecord const& r) {
    Json decos = Json::array();
    for (auto const& [name, d] : r.decorations)
        decos.push_back({{"name", name}, {"type", d.type}, {"x", d.x}, {"z", d.z}, {"rotation", d.rotation}, {"conquered", d.conquered}});
    return {{"kind", kindName(r.kind)}, {"y", r.yCenter}, {"decorations", decos}};
}

MapRecord* recordOf(std::int64_t id) {
    auto& rs = records();
    if (auto it = rs.find(id); it != rs.end()) return &it->second;
    auto doc = game::db::get(keyOf(id));
    if (!doc || !doc->is_object()) return nullptr;
    auto kind = kindFrom(doc->value("kind", std::string()));
    if (!kind) return nullptr;
    MapRecord r;
    r.kind    = *kind;
    r.yCenter = doc->value("y", 0);
    for (auto const& d : doc->value("decorations", Json::array())) {
        TfDecoration t{d.value("type", std::string()), d.value("x", 0), d.value("z", 0), d.value("rotation", 0), d.value("conquered", false)};
        r.decorations[d.value("name", std::string())] = t;
    }
    return &(rs[id] = std::move(r));
}

void saveRecords() {
    for (auto& [id, r] : records()) {
        if (!r.dirty) continue;
        game::db::put(keyOf(id), recordJson(r));
        r.dirty = false;
    }
}

// ---- helpers -----------------------------------------------------------------------------------------

void actionBar(Player& player, std::string const& text) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::SetTitle);
    if (!packet) return;
    auto& title      = static_cast<SetTitlePacket&>(*packet);
    title.mType      = SetTitlePacketPayload::TitleType::Actionbar;
    title.mTitleText = text;
    packet->sendTo(player);
}

bool instabuild(Player const& p) { return p.canUseAbility(AbilitiesIndex::Instabuild); }

/// The colour of a magic_map_color / ore_map_color entry ({"map_color": {"color"}, "brightness"}).
std::optional<std::pair<std::uint32_t, int>> dataMapColour(std::string const& map, std::string const& key) {
    auto const* v = datamaps::get(map, key);
    if (!v || !v->is_object()) return std::nullopt;
    auto mc = v->value("map_color", Json::object());
    return std::pair{static_cast<std::uint32_t>(mc.value("color", 0)) & 0xFFFFFFu, v->value("brightness", 1)};
}

/// MapColor.COLOR_MAGENTA (id 16), the default of an uncoloured biome.
constexpr std::uint32_t kMagenta = 0xB24CD8;
/// MapColor.COLOR_PINK (id 20), ores outside the data map.
constexpr std::uint32_t kPink = 0xF27FA5;

std::uint32_t abgr(std::uint32_t argb) { return cu::argbToAbgr(argb); }

/// Sets a pixel (ARGB; 0 transparent); true when it changed.
bool setPixel(MapItemSavedData& data, int x, int z, std::uint32_t argb) {
    auto& pixels = data.mPixels.get();
    auto  i      = static_cast<std::size_t>(x + z * 128);
    if (pixels.size() < 128 * 128) pixels.resize(128 * 128, 0);
    std::uint32_t v = argb == 0 ? 0u : abgr(argb);
    if (pixels[i] == v) return false;
    pixels[i] = v;
    return true;
}

/// A world (x, z) as a map's vanilla decoration (landmark icons for vanilla clients).
MapItemTrackedActor::UniqueId decorationId(int x, int z, int salt) {
    MapItemTrackedActor::UniqueId id{};
    id.type        = MapItemTrackedActor::Type::Other;
    id.keyEntityId.get() = ActorUniqueID{-1 - salt};
    id.keyBlockPos.get() = BlockPos{x, salt, z};
    return id;
}

bool sameId(MapItemTrackedActor::UniqueId const& a, MapItemTrackedActor::UniqueId const& b) {
    return a.type == b.type && a.keyEntityId.get().rawID == b.keyEntityId.get().rawID && a.keyBlockPos.get() == b.keyBlockPos.get();
}

/// Adds (or updates) a vanilla decoration at world (x, z) and puts it at the map coordinates TF
/// computed (MapItemSavedData#addDecoration).
void putVanillaDecoration(MapItemSavedData& data, MapDecoration::Type type, int worldX, int worldZ, int salt, cu::DecorationPos const& at) {
    auto id = decorationId(worldX, worldZ, salt);
    for (auto const& [k, d] : data.mDecorations.get())
        if (sameId(k, id) && d && d->mImage == type && d->mX == at.x && d->mY == at.z) return; // already there
    data._addDecoration(type, id, Vec3{static_cast<float>(worldX), 0.f, static_cast<float>(worldZ)}, "", mce::Color::WHITE());
    for (auto& [k, d] : data.mDecorations.get()) {
        if (!sameId(k, id) || !d) continue;
        d->mImage    = type;
        d->mX        = static_cast<schar>(at.x);
        d->mY        = static_cast<schar>(at.z);
        d->mRotation = static_cast<schar>(at.rotation & 15);
    }
    data.mDirtyForSave = true;
}

/// Sends the map's full data to a holder when its pixels changed (at most every 5 ticks).
void sendIfChanged(Player& player, std::int64_t id, MapRecord& r, MapItemSavedData& data, std::uint64_t tick) {
    if (!r.pixelsChanged) return;
    auto  uid  = player.getOrCreateUniqueID().rawID;
    auto& last = r.sent[uid];
    if (tick - last < 5 && last != 0) return;
    last = tick;
    if (auto packet = data.getFullDataPacket()) packet->sendTo(player);
    r.pixelsChanged = false;
    (void)id;
}

/// MagicMapPacket for a companion: the TF decorations with their conquered state.
void sendDecorations(Player& player, std::int64_t id, MapRecord& r) {
    if (!net::isCompanion(player)) return;
    Json list = Json::array();
    for (auto const& [name, d] : r.decorations)
        list.push_back({{"type", d.type}, {"x", d.x}, {"z", d.z}, {"rotation", d.rotation}, {"conquered", d.conquered}});
    net::send(player, kMagicChannel, {{"map", id}, {"decorations", list}});
}

// ---- the biome cache ---------------------------------------------------------------------------------

/// MagicMapItem's CACHE: the biomes sampled every 4 blocks over a map region, computed lazily (Java
/// fills the 512 x 512 array at once; the values are the same).
struct BiomeCache {
    std::unordered_map<std::uint64_t, std::string> cells;
    std::string const& at(int x, int z) {
        auto key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) | static_cast<std::uint32_t>(z);
        if (auto it = cells.find(key); it != cells.end()) return it->second;
        if (cells.size() > 1u << 20) cells.clear();
        return cells[key] = worldgen::biomeAt(BlockPos{x, 0, z});
    }
};
BiomeCache& biomeCache() {
    static BiomeCache c;
    return c;
}

std::pair<std::uint32_t, int> magicColour(std::string const& biome) {
    if (auto c = dataMapColour("twilightforest:magic_map_color", biome)) return *c;
    return {kMagenta, 1};
}

/// LandmarkUtil#isConquered at a landmark centre: the start there is conquered.
bool conqueredAt(int dim, int x, int z, std::string const& key) {
    for (auto const& s : gf::landmark::startsIntersecting(dim, {x >> 4, z >> 4})) {
        if (s.key != key) continue;
        if (s.box.minX <= x && x <= s.box.maxX && s.box.minZ <= z && z <= s.box.maxZ) return gf::landmark::isConquered(s);
    }
    return false;
}

/// MagicMapItem#update.
void updateMagic(Player& player, std::int64_t id, MapRecord& r, MapItemSavedData& data) {
    int  dim     = static_cast<int>(player.getDimensionId());
    int  centerX = data.mOrigin.get().x, centerZ = data.mOrigin.get().z;
    auto pos     = player.getFeetPos();
    int  viewerX = cu::viewerPixel(pos.x, centerX, cu::kMagicBlocksPerPixel);
    int  viewerZ = cu::viewerPixel(pos.z, centerZ, cu::kMagicBlocksPerPixel);
    auto& cache  = biomeCache();
    bool  changed = false, decoChanged = false;
    cu::magicMapScan(viewerX, viewerZ, [&](int xPixel, int zPixel) {
        int sx = xPixel * cu::kMagicBiomesPerPixel, sz = zPixel * cu::kMagicBiomesPerPixel;
        auto biome = cache.at(cu::magicSampleBlock(centerX, sx), cu::magicSampleBlock(centerZ, sz));
        // make streams more visible
        auto const& over = cache.at(cu::magicSampleBlock(centerX, sx + 1), cu::magicSampleBlock(centerZ, sz));
        auto const& down = cache.at(cu::magicSampleBlock(centerX, sx), cu::magicSampleBlock(centerZ, sz + 1));
        if (over == "twilightforest:stream") biome = over;
        else if (down == "twilightforest:stream") biome = down;
        auto [rgb, brightness] = magicColour(biome);
        if (setPixel(data, xPixel, zPixel, cu::shade(rgb, brightness))) changed = true;
        // look for TF features
        int worldX = cu::magicPixelBlock(centerX, xPixel);
        int worldZ = cu::magicPixelBlock(centerZ, zPixel);
        if (!cl::blockIsInCenter(worldX, worldZ)) return;
        auto key = cl::pickForChunk(worldgen::seed(), worldX >> 4, worldZ >> 4, [](int x, int z) { return worldgen::biomeAt(BlockPos{x, 0, z}); });
        if (!tags::has(tags::Registry::Structure, "twilightforest:landmark", key)) return;
        auto const* desc = gf::landmark::descriptor(key);
        if (!desc || desc->icon.empty()) return;
        auto at = cu::decorationPos(worldX, worldZ, centerX, centerZ, data.mScale, 180.0);
        if (!at) return;
        bool conquered = conqueredAt(dim, worldX, worldZ, key);
        auto name      = cu::decorationName(desc->icon, worldX, worldZ);
        TfDecoration d{desc->icon, at->x, at->z, at->rotation, conquered};
        auto it = r.decorations.find(name);
        if (it == r.decorations.end() || it->second.x != d.x || it->second.z != d.z || it->second.conquered != d.conquered || it->second.rotation != d.rotation) {
            r.decorations[name] = d;
            decoChanged         = true;
        }
        putVanillaDecoration(data, static_cast<MapDecoration::Type>(vanillaDecorationFor(desc->icon)), worldX, worldZ, 0, *at);
        if (conquered) putVanillaDecoration(data, MapDecoration::Type::XWhite, worldX, worldZ, 1, *at);
    });
    if (changed) {
        data.mDirtyForSave = true;
        r.pixelsChanged    = true;
    }
    if (decoChanged) {
        r.dirty              = true;
        r.decorationsChanged = true;
    }
    (void)id;
}

/// The Java map colour of a block for a maze map: Bedrock's map colour of the block (alpha 0: none).
std::optional<std::uint32_t> blockColour(BlockSource& region, BlockPos const& pos, Block const& block) {
    auto c = block.getBlockType().getMapColor(region, pos, block);
    if (c.a <= 0.f) return std::nullopt;
    auto ch = [](float f) { return static_cast<std::uint32_t>(std::clamp(f, 0.f, 1.f) * 255.f + 0.5f); };
    return (ch(c.r) << 16) | (ch(c.g) << 8) | ch(c.b);
}

/// MazeMapItem#update (one tick).
void updateMaze(Player& player, MapRecord& r, MapItemSavedData& data) {
    auto& region   = player.getDimensionBlockSource();
    int   bpp      = 1 << data.mScale;
    int   centerX  = data.mOrigin.get().x, centerZ = data.mOrigin.get().z;
    auto  pos      = player.getFeetPos();
    int   viewerX  = cu::viewerPixel(pos.x, centerX, bpp);
    int   viewerZ  = cu::viewerPixel(pos.z, centerZ, bpp);
    bool  ceiling  = static_cast<int>(player.getDimensionId()) == 1;
    int   radius   = ceiling ? 8 : 16; // TF: smaller on the maze map
    auto  uid      = player.getOrCreateUniqueID().rawID;
    int   step     = ++r.steps[uid];
    bool  ore      = r.kind == MapKind::Ore;
    bool  changed  = false;
    cu::mazeMapScan(
        viewerX, viewerZ, radius, step,
        [&](int xPixel, int zPixel) -> std::optional<std::uint32_t> {
            int worldX = (cu::javaDiv(centerX, bpp) + xPixel - 64) * bpp;
            int worldZ = (cu::javaDiv(centerZ, bpp) + zPixel - 64) * bpp;
            if (!region.hasChunksAt(BlockPos{worldX, 0, worldZ}, 0, false)) return std::nullopt; // chunk.isEmpty()
            int brightness = 1;
            std::optional<std::uint32_t> colour;
            if (ceiling) {
                int l3 = worldX + worldZ * 231871;
                l3     = l3 * l3 * 31287121 + l3 * 11;
                colour = ((l3 >> 20) & 1) == 0 ? 0x976D4Du : 0x707070u; // DIRT : STONE
            } else {
                BlockPos     at{worldX, r.yCenter, worldZ};
                Block const* state = &region.getBlock(at);
                colour             = blockColour(region, at, *state);
                auto id            = game::blockId(*state);
                if (id == "minecraft:stone" || id == "minecraft:air") {
                    for (int i = -3; i <= 3; ++i) {
                        BlockPos     search{worldX, r.yCenter + i, worldZ};
                        Block const& found = region.getBlock(search);
                        if (game::blockId(found) == "minecraft:stone") {
                            state = &found;
                            if (i > 0) brightness = 2;
                            if (i < 0) brightness = 0;
                            break;
                        }
                    }
                }
                if (ore) {
                    // recolour ores (weight 1000: wins over the block)
                    auto stateId = game::blockId(*state);
                    if (auto c = dataMapColour("twilightforest:ore_map_color", stateId)) colour = c->first;
                    else if (stateId != "minecraft:air" && tags::has(tags::Registry::Block, "c:ores", stateId)) colour = kPink;
                }
            }
            // MapColor.NONE (air) is transparent
            return colour ? cu::shade(*colour, brightness) : 0u;
        },
        [&](int xPixel, int zPixel, std::uint32_t c) {
            bool ch = setPixel(data, xPixel, zPixel, c);
            if (ch) changed = true;
            return ch;
        }
    );
    if (changed) {
        data.mDirtyForSave = true;
        r.pixelsChanged    = true;
    }
    // MazeMapItem#inventoryTick: a holder more than 3 blocks above / below yCenter shows as a dot
    int yProximity = static_cast<int>(std::floor(pos.y - static_cast<float>(r.yCenter)));
    if (yProximity < -3 || yProximity > 3) {
        for (auto& [k, d] : data.mDecorations.get())
            if (d && k.type == MapItemTrackedActor::Type::Entity && k.keyEntityId.get().rawID == uid && d->mImage == MapDecoration::Type::Player)
                d->mImage = MapDecoration::Type::PlayerOffMap;
    }
}

MapItemSavedData* createData(Player& player, int centerX, int centerZ, int scale) {
    auto& level = player.getLevel();
    auto  id    = level.getNewUniqueID();
    auto& data  = level.createMapSavedData(id, BlockPos{centerX, 0, centerZ}, player.getDimensionId(), scale);
    data.mOrigin.get() = BlockPos{centerX, 0, centerZ};
    data.mScale     = static_cast<schar>(scale);
    data.mDimension.get() = player.getDimensionId();
    data.mLocked    = true; // painted by U13, never by Bedrock's terrain sampling
    data.mUnlimitedTracking = false;
    data.mPixels.get().assign(128 * 128, 0u);
    data.mDirtyForSave = true;
    return &data;
}

ItemStack stackFor(Player& player, MapItemSavedData& data, MapKind kind) {
    auto stack = items::make(kFilledMap);
    if (!stack) return {};
    MapItem::setItemInstanceInfo(*stack, data);
    items::setData(*stack, kMapData, {{"kind", kindName(kind)}});
    char const* key      = kind == MapKind::Magic ? "item.twilightforest.filled_magic_map" : kind == MapKind::Maze ? "item.twilightforest.filled_maze_map" : "item.twilightforest.filled_ore_map";
    char const* fallback = kind == MapKind::Magic ? "Magic Map" : kind == MapKind::Maze ? "Maze Map" : "Maze/Ore Map";
    stack->setCustomName(Bedrock::Safety::RedactableString("§r" + trFor(player, key, fallback)));
    return *stack;
}

/// EmptyMagicMapItem#use / EmptyMazeMapItem#use: consume one, the stat, the sound, the new map.
gf::item::Result useEmpty(Player& player, ItemStack& held, std::string const& itemId, ItemStack made) {
    if (items::isEmpty(made)) return gf::item::Result::Fail;
    if (!instabuild(player)) {
        held = items::withCount(held, items::countOf(held) - 1);
        if (items::countOf(held) <= 0) held = ItemStack{};
    }
    gf::stats::award(player, "minecraft:used:" + itemId);
    gf::fx::sound(player.getDimension(), "ui.cartography_table.take_result", player.getPosition(), 1.0f, 1.0f);
    if (items::isEmpty(held)) {
        held = made; // the held stack becomes the map
    } else {
        ItemStack copy = made;
        if (!player.add(copy) && !items::isEmpty(copy)) player.drop(copy, false);
    }
    player.sendInventory(false);
    return gf::item::Result::Success;
}

/// The cloning recipes: one filled TF map of `kind` plus n empty ones -> n + 1 copies.
bool cloneMatch(recipes::CraftingGrid const& grid, MapKind kind, std::string const& emptyId, ItemStack* out) {
    ItemStack const* filled = nullptr;
    int              empties = 0;
    for (auto const* s : grid.slots) {
        if (!s || items::isEmpty(*s)) continue;
        auto k = mapKindOf(*s);
        if (k && *k == kind) { // FILLED_MAGIC_MAP / FILLED_MAZE_MAP (not the ore map)
            if (filled) return false;
            filled = s;
        } else if (!k && items::idOf(*s) == emptyId) {
            ++empties;
        } else {
            return false;
        }
    }
    if (!filled || empties < 1) return false;
    if (out) *out = items::withCount(*filled, empties + 1);
    return true;
}

} // namespace

// ---- public ------------------------------------------------------------------------------------------

std::optional<MapKind> mapKindOf(ItemStack const& stack) {
    if (items::isEmpty(stack) || items::idOf(stack) != kFilledMap) return std::nullopt;
    auto j = items::getData(stack, kMapData);
    if (!j.is_object()) return std::nullopt;
    return kindFrom(j.value("kind", std::string()));
}

std::int64_t mapIdOf(ItemStack const& stack) {
    auto const* tag = items::userData(stack);
    if (!tag) return 0;
    return MapItem::getMapId(tag).rawID;
}

MapItemSavedData* mapData(std::int64_t mapId) {
    auto level = ll::service::getLevel();
    return level ? level->getMapSavedData(ActorUniqueID{mapId}) : nullptr;
}

std::uint32_t mapPixel(MapItemSavedData const& data, int x, int z) {
    auto const& p = data.mPixels.get();
    auto        i = static_cast<std::size_t>(x + z * 128);
    if (i >= p.size() || p[i] == 0) return 0;
    return cu::argbToAbgr(p[i]);
}

std::vector<TfDecoration> decorationsOf(std::int64_t mapId) {
    std::vector<TfDecoration> out;
    if (auto* r = recordOf(mapId))
        for (auto const& [n, d] : r->decorations) out.push_back(d);
    return out;
}

std::optional<int> mazeYCenter(std::int64_t mapId) {
    auto* r = recordOf(mapId);
    if (!r || r->kind == MapKind::Magic) return std::nullopt;
    return r->yCenter;
}

ItemStack setupMagicMap(Player& player) {
    auto pos            = player.getFeetPos();
    auto [cx, cz]       = cu::magicMapCenter(static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.z)));
    auto* data          = createData(player, cx, cz, 4);
    if (!data) return {};
    auto  id            = data->mMapId.get().rawID;
    auto& r             = records()[id];
    r                   = MapRecord{};
    r.kind              = MapKind::Magic;
    r.dirty             = true;
    log().info("uncraftmaps: magic map {} for {} centred at {} {}", id, player.getRealName(), cx, cz);
    return stackFor(player, *data, MapKind::Magic);
}

std::pair<int, int> mazeCenterFor(int dim, int x, int z) {
    auto center = cu::mazeMapCenter(x, z, 0);
    // TFMazeMapData#calculateMapCenter: inside a labyrinth chunk the map snaps to its centre
    // (LegacyLandmarkPlacements#pickLandmarkForChunk of the level the map is made in)
    auto tf = gf::world::twilight();
    if (tf && dim == static_cast<int>(*tf)) {
        auto key = cl::pickForChunk(worldgen::seed(), x >> 4, z >> 4, [](int bx, int bz) { return worldgen::biomeAt(BlockPos{bx, 0, bz}); });
        if (key == "twilightforest:labyrinth") {
            auto c = cl::nearestCenter(x >> 4, z >> 4);
            center = {c.x, c.z};
        }
    }
    return center;
}

ItemStack cloneResult(std::vector<ItemStack const*> const& grid, MapKind kind) {
    recipes::CraftingGrid g;
    g.slots = grid;
    ItemStack out;
    if (!cloneMatch(g, kind, kind == MapKind::Magic ? "twilightforest:magic_map" : "twilightforest:maze_map", &out)) return {};
    return out;
}

ItemStack setupMazeMap(Player& player, bool ore) {
    auto pos      = player.getFeetPos();
    int  x        = static_cast<int>(std::floor(pos.x)), z = static_cast<int>(std::floor(pos.z));
    auto [cx, cz] = mazeCenterFor(static_cast<int>(player.getDimensionId()), x, z);
    auto* data = createData(player, cx, cz, 0);
    if (!data) return {};
    auto  id   = data->mMapId.get().rawID;
    auto& r    = records()[id];
    r          = MapRecord{};
    r.kind     = ore ? MapKind::Ore : MapKind::Maze;
    r.yCenter  = static_cast<int>(std::floor(pos.y));
    r.dirty    = true;
    log().info("uncraftmaps: {} map {} for {} centred at {} {} y {}", ore ? "ore" : "maze", id, player.getRealName(), cx, cz, r.yCenter);
    return stackFor(player, *data, r.kind);
}

void updateHeldMap(Player& player, ItemStack const& stack) {
    auto kind = mapKindOf(stack);
    if (!kind) return;
    auto  id   = mapIdOf(stack);
    auto* data = mapData(id);
    auto* r    = recordOf(id);
    if (!data || !r) return;
    if (static_cast<int>(data->mDimension.get()) != static_cast<int>(player.getDimensionId())) return; // level.dimension() == data.dimension
    static std::uint64_t tick = 0;
    ++tick;
    if (r->kind == MapKind::Magic) updateMagic(player, id, *r, *data);
    else updateMaze(player, *r, *data);
    sendIfChanged(player, id, *r, *data, tick);
    if (r->kind != MapKind::Magic && net::isCompanion(player) && !r->mazeSent[player.getOrCreateUniqueID().rawID]) {
        r->mazeSent[player.getOrCreateUniqueID().rawID] = true;
        net::send(player, kMazeChannel, {{"map", id}, {"y", r->yCenter}, {"ore", r->kind == MapKind::Ore}});
    }
    if (r->kind == MapKind::Magic && r->decorationsChanged) {
        sendDecorations(player, id, *r);
        r->decorationsChanged = false;
    }
}

int vanillaDecorationFor(std::string const& tfType) {
    using T = MapDecoration::Type;
    static std::unordered_map<std::string, T> const types{
        {"twilightforest:hedge_maze", T::TriangleGreen},          {"twilightforest:small_hollow_hill", T::BuriedMineshaft},
        {"twilightforest:medium_hollow_hill", T::BuriedMineshaft}, {"twilightforest:large_hollow_hill", T::BuriedMineshaft},
        {"twilightforest:quest_grove", T::MarkerTeal},            {"twilightforest:naga_courtyard", T::JungleTemple},
        {"twilightforest:lich_tower", T::Monument},               {"twilightforest:labyrinth", T::TrialChambers},
        {"twilightforest:hydra_lair", T::MarkerRed},              {"twilightforest:knight_stronghold", T::BuriedAncientCity},
        {"twilightforest:dark_tower", T::Mansion},                {"twilightforest:yeti_lair", T::VillageSnowy},
        {"twilightforest:aurora_palace", T::MarkerBlue},          {"twilightforest:troll_caves", T::MarkerOrange},
        {"twilightforest:final_castle", T::MarkerYellow},
    };
    auto it = types.find(tfType);
    return static_cast<int>(it == types.end() ? T::TriangleRed : it->second);
}

ItemStack locatorMap(int dimension, int x, int z) {
    auto level = ll::service::getLevel();
    if (!level) return {};
    auto [cx, cz] = cu::mazeMapCenter(x, z, 4); // MapItemSavedData#createFresh's snapping
    auto  id      = level->getNewUniqueID();
    auto& data    = level->createMapSavedData(id, BlockPos{cx, 0, cz}, DimensionType(dimension), 4);
    data.mOrigin.get()     = BlockPos{cx, 0, cz};
    data.mScale             = 4;
    data.mDimension.get()  = DimensionType(dimension);
    data.mUnlimitedTracking = true;
    data.mDirtyForSave      = true;
    // addTargetDecoration(stack, pos, "+", RED_X)
    if (auto at = cu::decorationPos(x, z, cx, cz, 4, 180.0)) putVanillaDecoration(data, MapDecoration::Type::XWhite, x, z, 2, *at);
    auto stack = items::make(kFilledMap);
    if (!stack) return {};
    MapItem::setItemInstanceInfo(*stack, data);
    return *stack;
}

// ---- module ------------------------------------------------------------------------------------------

class MapsModule {
public:
    void install() {
        static bool once = false;
        if (!once) {
            once = true;
            registerOnce();
        }
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([this](auto&) {
            auto level = ll::service::getLevel();
            if (!level) return;
            ++mTicks;
            std::vector<ActorUniqueID> holders;
            level->forEachPlayer([&](Player& p) {
                if (mapKindOf(p.getSelectedItem()) || mapKindOf(p.getOffhandSlot())) holders.push_back(p.getOrCreateUniqueID());
                return true;
            });
            for (auto uid : holders) {
                auto* p = level->getPlayer(uid);
                if (!p) continue;
                // MapItem#inventoryTick: update() while selected or in the off hand
                ItemStack selected = p->getSelectedItem();
                updateHeldMap(*p, selected);
                ItemStack off = p->getOffhandSlot();
                if (mapIdOf(off) != mapIdOf(selected)) updateHeldMap(*p, off);
            }
            if (mTicks % 200 == 0) saveRecords();
        });
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mTick);
        saveRecords();
    }

private:
    static void registerOnce() {
        game::db::onWorldSave([] { saveRecords(); });
        game::db::onWorldClosed([] {
            saveRecords();
            records().clear();
            biomeCache().cells.clear();
        });
        // EmptyMagicMapItem#use
        gf::item::registerItem("twilightforest:magic_map", {.use = [](Player& player, ItemStack& held) {
                                   if (!gf::world::isTwilight(player.getDimension())) {
                                       actionBar(player, trFor(player, "misc.twilightforest.magic_map_fail", "The Magic faltered. Perhaps it doesn't work here?"));
                                       return gf::item::Result::Fail;
                                   }
                                   return useEmpty(player, held, "twilightforest:magic_map", setupMagicMap(player));
                               }});
        // EmptyMazeMapItem#use
        for (bool ore : {false, true}) {
            std::string id = ore ? "twilightforest:ore_map" : "twilightforest:maze_map";
            gf::item::registerItem(id, {.use = [id, ore](Player& player, ItemStack& held) { return useEmpty(player, held, id, setupMazeMap(player, ore)); }});
        }
        // MagicMapCloningRecipe / MazeMapCloningRecipe (BP layouts _2.._9 in assets/pack/bp/recipes/uncraftmaps)
        struct Cloning {
            char const* id;
            MapKind     kind;
            char const* empty;
        };
        for (auto const& c : {Cloning{"twilightforest:magic_map_cloning_recipe", MapKind::Magic, "twilightforest:magic_map"},
                              Cloning{"twilightforest:maze_map_cloning_recipe", MapKind::Maze, "twilightforest:maze_map"}}) {
            auto kind  = c.kind;
            auto empty = std::string(c.empty);
            recipes::registerSpecialCrafting(
                c.id, [kind, empty](recipes::CraftingGrid const& g) { return cloneMatch(g, kind, empty, nullptr); },
                [kind, empty](recipes::CraftingGrid const& g) {
                    ItemStack out;
                    cloneMatch(g, kind, empty, &out);
                    return out;
                }
            );
            std::string base = std::string(c.id);
            base             = base.substr(0, base.size() - std::string("_recipe").size());
            for (int n = 2; n <= 9; ++n) recipes::ext::addSpecialLayout(c.id, base + "_" + std::to_string(n));
        }
        // the companion's MazeMapPacket data (yCenter, ore) when it starts holding one
        net::onCompanionJoined([](Player&) {});
    }

    ll::event::ListenerPtr mTick;
    std::uint64_t          mTicks = 0;
};

} // namespace gf::feature::uncraftmaps

GF_REGISTER_MODULE(gf::feature::uncraftmaps::MapsModule);
