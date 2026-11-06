// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFBlocks.java
//   src/main/java/twilightforest/init/TFItems.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Unit U4 (woodwork): the id lists of the furniture blocks and the block / item helpers the
// unit's parts share.

#include "feature/woodwork/Woodwork.h"

#include <cmath>
#include <map>
#include <random>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/common/Interactions.h"
#include "feature/fx/Fx.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"

namespace gf::feature::woodwork {

namespace {

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

std::vector<std::string> furnitureIds(char const* suffix) {
    std::vector<std::string> out;
    for (auto w : kTfWoods) out.push_back(std::string(kNs) + std::string(w) + suffix);
    for (auto w : kVanillaFurnitureWoods) out.push_back(std::string(kNs) + std::string(w) + suffix);
    return out;
}

} // namespace

std::vector<std::string> const& banisterIds() {
    static auto const ids = furnitureIds("_banister");
    return ids;
}

std::vector<std::string> const& dryingRackIds() {
    static auto const ids = furnitureIds("_drying_rack");
    return ids;
}

std::vector<HollowLog> const& hollowLogs() {
    static auto const logs = [] {
        std::vector<HollowLog> out;
        auto add = [&](std::string_view wood) {
            // TFBlocks: hollow_<wood>_log_*, the nether woods hollow_<wood>_stem_*
            bool        stem = wood == "crimson" || wood == "warped";
            std::string base = std::string(kNs) + "hollow_" + std::string(wood) + (stem ? "_stem" : "_log");
            out.push_back({std::string(wood), base, base + "_horizontal", base + "_vertical", base + "_climbable"});
        };
        for (auto w : kTfWoods) add(w);
        for (auto w : kVanillaHollowWoods) add(w);
        return out;
    }();
    return logs;
}

HollowLog const* hollowLogOf(std::string const& id) {
    static auto const index = [] {
        std::map<std::string, HollowLog const*> m;
        for (auto const& h : hollowLogs()) {
            m[h.item]       = &h;
            m[h.horizontal] = &h;
            m[h.vertical]   = &h;
            m[h.climbable]  = &h;
        }
        return m;
    }();
    auto it = index.find(id);
    return it == index.end() ? nullptr : it->second;
}

char const* faceName(int face) {
    static char const* const names[] = {"down", "up", "north", "south", "west", "east"};
    return face >= 0 && face < 6 ? names[face] : "north";
}

int faceOf(std::string_view name) {
    for (int f = 0; f < 6; ++f) {
        if (name == faceName(f)) return f;
    }
    return -1;
}

std::array<int, 3> faceStep(int face) {
    switch (face) {
    case 0: return {0, -1, 0};
    case 1: return {0, 1, 0};
    case 2: return {0, 0, -1};
    case 3: return {0, 0, 1};
    case 4: return {-1, 0, 0};
    case 5: return {1, 0, 0};
    default: return {0, 0, 0};
    }
}

int oppositeFace(int face) { return face < 0 ? face : (face ^ 1); }

int lookFace(Actor const& actor) {
    // game::playerFacing: Java's 2D data value (0 south, 1 west, 2 north, 3 east)
    static constexpr int kToFace[4] = {3, 4, 2, 5};
    return kToFace[game::playerFacing(actor) & 3];
}

std::string stateOf(Block const& block, std::string const& name) {
    auto states = game::statesOf(block);
    auto it     = states.find(name);
    return it == states.end() ? std::string{} : it->second;
}

Block const* withStates(Block const& block, Json const& changes) {
    // every state the change names; the others keep their current value
    Json states = Json::object();
    for (auto const& [name, value] : game::statesOf(block)) {
        if (changes.contains(name)) {
            states[name] = changes[name];
        } else if (value == "true" || value == "false") {
            states[name] = value == "true";
        } else if (!value.empty() && value.find_first_not_of("0123456789") == std::string::npos) {
            states[name] = std::stoi(value);
        } else {
            states[name] = value;
        }
    }
    for (auto const& [name, _] : changes.items()) {
        if (!states.contains(name)) return nullptr; // the block has no such state
    }
    return game::blockWithStates(block.getTypeName(), states);
}

std::string idOf(Block const& block) { return game::blockId(block); }

namespace {
bool isWaterBlock(Block const& b, bool sourceOnly) {
    auto const& id = b.getTypeName();
    if (id != "minecraft:water" && id != "minecraft:flowing_water") return false;
    if (!sourceOnly) return true;
    auto depth = game::intState(b, "liquid_depth");
    return !depth || *depth == 0;
}
} // namespace

bool waterSourceAt(BlockSource& region, BlockPos const& pos) {
    return isWaterBlock(region.getBlock(pos), true) || isWaterBlock(region.getExtraBlock(pos), true);
}

bool waterAt(BlockSource& region, BlockPos const& pos) {
    return isWaterBlock(region.getBlock(pos), false) || isWaterBlock(region.getExtraBlock(pos), false);
}

void playSound(BlockSource& region, BlockPos const& pos, std::string const& event, float volume, float pitch) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto dim = level->getDimension(region.getDimensionId()).lock();
    if (!dim) return;
    fx::sound(*dim, event, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, volume, pitch);
}

float randomFloat() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng()); }

void spawnItem(BlockSource& region, Vec3 const& at, ItemStack const& stack) {
    if (stack.isNull()) return;
    region.getLevel().getSpawner().spawnItem(region, stack, nullptr, at, 0);
}

void dropItemStack(BlockSource& region, BlockPos const& pos, ItemStack const& stack) {
    if (stack.isNull()) return;
    // Containers.dropItemStack: a random spot in the inner box of the block corner (x, y, z), a
    // motion of random.triangle(0, 0.11485) on x / z and triangle(0.2, 0.11485) on y, in blocks
    // per tick (RandomSource#triangle: mode + deviation * (nextDouble() - nextDouble()))
    constexpr double kDeviation = 0.11485000171139836;
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    auto triangle = [&](double mode) { return mode + kDeviation * (unit(rng()) - unit(rng())); };
    double size = 0.25; // EntityType.ITEM.getWidth()
    double span = 1.0 - size;
    double half = size / 2.0;
    Vec3   at{
        static_cast<float>(std::floor(pos.x) + randomFloat() * span + half),
        static_cast<float>(std::floor(pos.y) + randomFloat() * span),
        static_cast<float>(std::floor(pos.z) + randomFloat() * span + half)
    };
    auto* item = region.getLevel().getSpawner().spawnItem(region, stack, nullptr, at, 0);
    if (!item) return;
    const_cast<Vec3&>(item->getPosDelta()) = Vec3{
        static_cast<float>(triangle(0.0)),
        static_cast<float>(triangle(0.2)),
        static_cast<float>(triangle(0.0))
    };
}

void giveOrDrop(Player& player, ItemStack const& stack) {
    if (stack.isNull()) return;
    ItemStack rest = stack;
    if (!player.add(rest) || !rest.isNull()) {
        // Player#drop(item, false): in front of the player
        if (!rest.isNull()) player.drop(rest, false);
    }
    player.sendInventory(false);
}

void consumeOne(Player& player, ItemStack const& held) {
    if (player.isCreative()) return;
    auto& item = interactions::mutableItem(held);
    if (item.isNull()) return;
    item.remove(1);
    if (item.mCount <= 0) item = ItemStack::EMPTY_ITEM();
    player.sendInventory(false);
}

void hurtTool(Player& player, ItemStack const& held, int n) {
    if (player.isCreative()) return;
    auto& item = interactions::mutableItem(held);
    if (item.isNull()) return;
    item.hurtAndBreak(n, &player);
    player.sendInventory(false);
}

bool isAxe(std::string const& itemId) {
    if (itemId.empty()) return false;
    return tags::has(tags::Registry::Item, "minecraft:axes", itemId) || itemId.ends_with("_axe");
}

bool isShovel(std::string const& itemId) {
    if (itemId.empty()) return false;
    return tags::has(tags::Registry::Item, "minecraft:shovels", itemId) || itemId.ends_with("_shovel");
}

bool isShears(std::string const& itemId) {
    if (itemId.empty()) return false;
    return itemId == "minecraft:shears" || tags::has(tags::Registry::Item, "c:tools/shear", itemId);
}

bool repeatedUse(Player const& player, GlobalPos const& pos, bool firstEvent) {
    struct LastUse {
        GlobalPos    pos;
        std::int64_t tick;
    };
    static std::map<std::int64_t, LastUse> last;
    auto level = ll::service::getLevel();
    auto now   = level ? static_cast<std::int64_t>(level->getCurrentServerTick().tickID) : 0;
    auto uid   = const_cast<Player&>(player).getOrCreateUniqueID().rawID;
    auto it    = last.find(uid);
    bool held  = !firstEvent && it != last.end() && it->second.pos == pos && now - it->second.tick < 5;
    last[uid]  = {pos, now};
    if (last.size() > 256) last.clear();
    return held;
}

} // namespace gf::feature::woodwork
