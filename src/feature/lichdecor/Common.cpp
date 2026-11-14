// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/LightableBlock.java
//   src/main/java/gloamingforest/enums/extensions/GloamingCandleColors.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Unit U22 (lichdecor): the helpers the unit's files share and the config section.

#include "feature/lichdecor/Lichdecor.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"

#include "feature/common/Interactions.h"
#include "feature/fx/Fx.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Config.h"

GF_REGISTER_CONFIG("lichdecor", gf::feature::lichdecor::LichdecorConfig);

namespace gf::feature::lichdecor {

namespace {

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

std::string prefixed(std::string_view name) {
    if (name.find(':') != std::string_view::npos) return std::string(name);
    return "gloamingforest:" + std::string(name);
}

} // namespace

char const* lightingName(Lighting l) {
    switch (l) {
    case Lighting::None: return "none";
    case Lighting::Normal: return "normal";
    case Lighting::Dim: return "dim";
    case Lighting::Ominous: return "ominous";
    }
    return "none";
}

std::optional<Lighting> lightingOf(Block const& block) {
    auto s = state(block, "lighting");
    if (s == "none") return Lighting::None;
    if (s == "normal") return Lighting::Normal;
    if (s == "dim") return Lighting::Dim;
    if (s == "ominous") return Lighting::Ominous;
    return std::nullopt;
}

std::string state(Block const& block, std::string_view name) {
    auto states = game::statesOf(block);
    if (auto it = states.find(prefixed(name)); it != states.end()) return it->second;
    if (auto it = states.find(std::string(name)); it != states.end()) return it->second;
    if (name.find(':') == std::string_view::npos) {
        if (auto it = states.find("minecraft:" + std::string(name)); it != states.end()) return it->second;
    }
    return {};
}

int intStateOr(Block const& block, std::string_view name, int fallback) {
    auto s = state(block, name);
    if (s.empty() || s.find_first_not_of("-0123456789") != std::string::npos) return fallback;
    return std::stoi(s);
}

bool boolState(Block const& block, std::string_view name) {
    auto s = state(block, name);
    return s == "true" || s == "1";
}

Block const* withStates(Block const& block, Json const& changes) {
    Json named = Json::object();
    auto current = game::statesOf(block);
    for (auto const& [k, v] : changes.items()) {
        std::string key = prefixed(k);
        if (!current.contains(key) && current.contains(k)) key = k;
        named[key] = v;
    }
    Json states = Json::object();
    for (auto const& [name, value] : current) {
        if (named.contains(name)) states[name] = named[name];
        else if (value == "true" || value == "false") states[name] = value == "true";
        else if (!value.empty() && value.find_first_not_of("-0123456789") == std::string::npos) states[name] = std::stoi(value);
        else states[name] = value;
    }
    for (auto const& [name, _] : named.items())
        if (!states.contains(name)) return nullptr;
    return game::blockWithStates(block.getTypeName(), states);
}

bool setStates(BlockSource& region, BlockPos const& pos, Block const& block, Json const& changes) {
    auto const* b = withStates(block, changes);
    return b && game::setBlock(region, pos, *b);
}

char const* faceName(int face) {
    static char const* const names[] = {"down", "up", "north", "south", "west", "east"};
    return face >= 0 && face < 6 ? names[face] : "north";
}

int faceOf(std::string_view name) {
    for (int f = 0; f < 6; ++f)
        if (name == faceName(f)) return f;
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

BlockPos relative(BlockPos const& pos, int face, int n) {
    auto s = faceStep(face);
    return BlockPos{pos.x + s[0] * n, pos.y + s[1] * n, pos.z + s[2] * n};
}

int lookFace(Actor const& actor) {
    static constexpr int kToFace[4] = {3, 4, 2, 5}; // Java 2D data value (0 south, 1 west, 2 north, 3 east)
    return kToFace[game::playerFacing(actor) & 3];
}

std::array<int, 6> nearestLookingDirections(Actor const& actor) {
    // Direction.orderedByNearest(entity): Java's look vector from yaw / pitch
    auto        rot   = actor.getRotation(); // x pitch, y yaw (degrees)
    float       pitch = rot.x * 0.017453292F, yaw = -rot.y * 0.017453292F;
    float       sy = std::sin(yaw), cy = std::cos(yaw), sp = std::sin(pitch), cp = std::cos(pitch);
    bool        east = sy > 0.0F, up = sp < 0.0F, south = cy > 0.0F;
    float       fx = east ? sy : -sy, fy = up ? -sp : sp, fz = south ? cy : -cy;
    float       hx = fx * cp, hz = fz * cp;
    int         dx = east ? 5 : 4, dy = up ? 1 : 0, dz = south ? 3 : 2;
    auto        make = [](int a, int b, int c) { return std::array<int, 6>{a, b, c, oppositeFace(c), oppositeFace(b), oppositeFace(a)}; };
    if (fx > fz) {
        if (fy > hx) return make(dy, dx, dz);
        return hz > fy ? make(dx, dz, dy) : make(dx, dy, dz);
    }
    if (fy > hz) return make(dy, dz, dx);
    return hx > fy ? make(dz, dx, dy) : make(dz, dy, dx);
}

bool creative(Player const& player) { return player.isCreative(); }

bool doBlockDrops() {
    auto level = ll::service::getLevel();
    if (!level) return true;
    GameRuleId rule;
    rule.mValue = static_cast<int>(GameRules::GameRulesIndex::DoTileDrops);
    return level->getGameRules().getBool(rule, true);
}

void giveOrDrop(Player& player, ItemStack const& stack) {
    if (stack.isNull()) return;
    ItemStack rest = stack;
    if (!player.add(rest) || !rest.isNull()) {
        if (!rest.isNull()) player.drop(rest, false);
    }
    player.sendInventory(false);
}

void consumeOne(Player& player, ItemStack const& handSlot) {
    if (player.isCreative()) return;
    auto& item = interactions::mutableItem(handSlot);
    if (item.isNull()) return;
    item.remove(1);
    if (item.mCount <= 0) item = ItemStack::EMPTY_ITEM();
    player.sendInventory(false);
}

void popResource(BlockSource& region, BlockPos const& pos, ItemStack const& stack) {
    if (stack.isNull()) return;
    // Block.popResource: centre +- 0.25 on each axis (y - 0.125 for the item's half height)
    std::uniform_real_distribution<float> d(-0.25F, 0.25F);
    Vec3 at{pos.x + 0.5F + d(rng()), pos.y + 0.5F + d(rng()) - 0.125F, pos.z + 0.5F + d(rng())};
    region.getLevel().getSpawner().spawnItem(region, stack, nullptr, at, 10);
}

float randomFloat(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng()); }
int   randomInt(int bound) { return bound <= 0 ? 0 : std::uniform_int_distribution<int>(0, bound - 1)(rng()); }

std::array<std::string_view, 17> const& candleColors() {
    static constexpr std::array<std::string_view, 17> k{"",     "white", "orange", "magenta", "light_blue", "yellow", "lime",  "pink", "gray",
                                                        "light_gray", "cyan",  "purple", "blue",    "brown",      "green",  "red", "black"};
    return k;
}

std::string vanillaCandle(int color) {
    if (color <= 0 || color > 16) return "minecraft:candle";
    return "minecraft:" + std::string(candleColors()[color]) + "_candle";
}

int candleColorOf(std::string_view id) {
    for (int i = 0; i < 17; ++i)
        if (id == vanillaCandle(i)) return i;
    return -1;
}

std::string ominousCandle(int color) {
    if (color <= 0 || color > 16) return "gloamingforest:ominous_candle";
    return "gloamingforest:ominous_" + std::string(candleColors()[color]) + "_candle";
}

int ominousColorOf(std::string_view id) {
    for (int i = 0; i < 17; ++i)
        if (id == ominousCandle(i)) return i;
    return -1;
}

void playSound(BlockSource& region, Vec3 const& at, std::string const& event, float volume, float pitch) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto dim = level->getDimension(region.getDimensionId()).lock();
    if (!dim) return;
    fx::sound(*dim, event, at, volume, pitch);
}

void particle(BlockSource& region, std::string const& id, Vec3 const& at, Vec3 const& velocity, int count) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto dim = level->getDimension(region.getDimensionId()).lock();
    if (!dim) return;
    fx::particle(*dim, id, at, velocity, count);
}

} // namespace gf::feature::lichdecor
