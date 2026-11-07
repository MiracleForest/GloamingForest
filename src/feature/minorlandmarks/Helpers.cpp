// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/util/features/FeatureLogic.java
//   src/main/java/twilightforest/loot/TFLootTables.java
//   src/main/java/twilightforest/block/HedgeBlock.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U18's shared helpers: vanilla piece orientation, Java block predicates and providers, containers
// and spawners at world positions.
#include "feature/minorlandmarks/Internal.h"

#include <algorithm>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"

#include "feature/ext/worldgen/FeatureKit.h"

namespace gf::feature::minorlandmarks {

namespace kit = gf::features::kit;

void setVanillaOrientation(st::Piece& piece, std::optional<st::Orientation> facing) {
    // StructurePiece#setOrientation
    piece.orientation = facing.value_or(st::kNoOrientation);
    if (!facing) {
        piece.rotation = st::Rotation::None;
        piece.mirror   = st::Mirror::None;
        return;
    }
    switch (*facing) {
    case st::Orientation::South:
        piece.mirror   = st::Mirror::LeftRight;
        piece.rotation = st::Rotation::None;
        break;
    case st::Orientation::West:
        piece.mirror   = st::Mirror::LeftRight;
        piece.rotation = st::Rotation::Clockwise90;
        break;
    case st::Orientation::East:
        piece.mirror   = st::Mirror::None;
        piece.rotation = st::Rotation::Clockwise90;
        break;
    default:
        piece.mirror   = st::Mirror::None;
        piece.rotation = st::Rotation::None;
        break;
    }
}

st::Orientation toOrientation(cml::Dir d) {
    switch (d) {
    case cml::Dir::South: return st::Orientation::South;
    case cml::Dir::West: return st::Orientation::West;
    case cml::Dir::North: return st::Orientation::North;
    default: return st::Orientation::East;
    }
}

cml::Dir toDir(st::Orientation o) {
    switch (o) {
    case st::Orientation::South: return cml::Dir::South;
    case st::Orientation::West: return cml::Dir::West;
    case st::Orientation::North: return cml::Dir::North;
    default: return cml::Dir::East;
    }
}

BlockPos toBlockPos(cml::Pos const& p) { return BlockPos{p.x, p.y, p.z}; }
cml::Pos toPos(BlockPos const& p) { return {p.x, p.y, p.z}; }

bool treesReplaceable(Block const& block) {
    // FeatureLogic#isReplaceable(state, true)
    return (st::blocks::canBeReplaced(block) || st::blocks::inTag(block, "twilightforest:worldgen_replaceables") || st::blocks::inTag(block, "minecraft:flowers"))
        && !st::blocks::inTag(block, "minecraft:features_cannot_replace");
}

bool validTreePos(Region& region, BlockPos const& pos) { return kit::validTreePos(region, pos); }

Block const* sampleProvider(Json const& provider, RandomSource& random, BlockPos const& pos) { return kit::sampleState(provider, random, pos); }

std::string providerBlockId(Json const& provider) {
    if (!provider.is_object() || provider.value("type", std::string{}) != "minecraft:simple_state_provider") return {};
    auto it = provider.find("state");
    if (it == provider.end() || !it->is_object()) return {};
    return it->value("Name", std::string{});
}

Json simpleProvider(std::string const& javaId, Json const& props) {
    Json state{{"Name", javaId}};
    if (!props.empty()) state["Properties"] = props;
    return Json{{"type", "minecraft:simple_state_provider"}, {"state", std::move(state)}};
}

Block const& withLeafDistance(Block const& block, int distance) {
    if (!st::blocks::javaProperty(block, "distance")) return block;
    return st::blocks::withJava(block, Json{{"distance", std::to_string(distance)}});
}

void placeContainerAt(Region& region, BlockBox const& sbb, BlockPos const& pos, std::string const& blockId, std::optional<st::Orientation> facing,
                      std::string const& tableId, std::int64_t seed) {
    if (!sbb.contains(pos.x, pos.y, pos.z)) return;
    st::blockentities::container(region, pos, blockId, facing, tableId, seed);
}

void placeSpawnerAt(Region& region, BlockBox const& sbb, BlockPos const& pos, std::string const& entityId) {
    if (!sbb.contains(pos.x, pos.y, pos.z)) return;
    st::blockentities::spawner(region, pos, entityId, Json::object());
}

bool isSpiderType(std::string const& entityId) {
    // net.minecraft.world.entity.monster.Spider and its subclasses (CaveSpider, TF's HedgeSpider,
    // SwarmSpider, KingSpider, CarminiteBroodling)
    static constexpr std::string_view kSpiders[] = {"minecraft:spider", "minecraft:cave_spider", "twilightforest:hedge_spider",
                                                    "twilightforest:swarm_spider", "twilightforest:king_spider", "twilightforest:carminite_broodling"};
    return std::find(std::begin(kSpiders), std::end(kSpiders), entityId) != std::end(kSpiders);
}

bool naturalStart(Json const& startData, int chunkX, int chunkZ, int x, int z, int foundX, int foundZ) {
    if (startData.is_object())
        if (auto it = startData.find("forced"); it != startData.end() && it->is_boolean()) return !it->get<bool>();
    return (x == chunkX * 16 && z == chunkZ * 16) || (x == chunkX * 16 + 7 && z == chunkZ * 16 + 7) || (x == foundX && z == foundZ);
}

} // namespace gf::feature::minorlandmarks
