// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
//
// Selftests of the structure kit (rule R12: "structure.*", testPos dx 5700..5799).
//
// Placements are checked twice: on an in-memory region at a fixed origin (MemoryRegion), whose
// golden volume hashes do not depend on where the test area is, and in the world, whose
// snapshotVolume must equal the in-memory hash of the same placement (rule R19 golden hashes).
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/HangingActor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/Motif.h"
#include "mc/world/actor/Painting.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/BaseMobSpawner.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkBlockPos.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/BedBlockActor.h"
#include "mc/world/level/block/actor/FlowerPotBlockActor.h"
#include "mc/world/Container.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"
#include "mc/world/level/block/actor/HopperBlockActor.h"
#include "mc/world/level/block/actor/MobSpawnerBlockActor.h"
#include "mc/world/phys/AABB.h"

#include "core/Keys.h"
#include "core/VolumeHash.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/landmark/LandmarkTest.h"
#include "feature/ext/structure/HopperAccess.h"
#include "feature/ext/structure/Placement.h"
#include "feature/ext/structure/Processors.h"
#include "feature/ext/structure/SharedRandom.h"
#include "feature/ext/structure/TemplateDefinitions.h"
#include "feature/common/BlockData.h"
#include "feature/enchant/Enchant.h"
#include "feature/inventory/Inventory.h"
#include "feature/landmark/Landmark.h"
#include "feature/loot/Loot.h"
#include "feature/structure/Region.h"
#include "feature/structure/bases/BasesInternal.h"
#include "feature/structure/bases/HollowHill.h"
#include "feature/structure/bases/Maze.h"
#include "feature/structure/bases/Tower.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "selftest/SelfTest.h"

namespace gf::structure::selftests {

namespace {

using selftest::Context;

// ---- an in-memory region -----------------------------------------------------------------------------------

class MemoryRegion final : public worldgen::Region {
public:
    MemoryRegion() { resetRegionRandom(this); }
    [[nodiscard]] int          centerChunkX() const override { return 0; }
    [[nodiscard]] int          centerChunkZ() const override { return 0; }
    [[nodiscard]] int          minY() const override { return -64; }
    [[nodiscard]] int          maxY() const override { return 320; }
    [[nodiscard]] bool         isInside(int, int) const override { return true; }
    [[nodiscard]] Block const& getBlock(BlockPos const& pos) const override {
        auto it = mBlocks.find(key(pos.x, pos.y, pos.z));
        return it == mBlocks.end() ? blocks::air() : *it->second;
    }
    bool setBlock(BlockPos const& pos, Block const& block, int) override {
        if (pos.y < minY() || pos.y >= maxY()) return false;
        if (block.isAir()) mBlocks.erase(key(pos.x, pos.y, pos.z));
        else {
            mBlocks[key(pos.x, pos.y, pos.z)] = &block;
            auto& top                         = mTop[colKey(pos.x, pos.z)];
            top                               = std::max(top, pos.y);
        }
        return true;
    }
    [[nodiscard]] int height(worldgen::Heightmap type, int x, int z) const override {
        auto it = mTop.find(colKey(x, z));
        if (it == mTop.end()) return minY();
        for (int y = it->second; y >= minY(); y--) {
            Block const& b = getBlock(BlockPos{x, y, z});
            if (b.isAir()) continue;
            bool liquid = blocks::isLiquid(b);
            switch (type) {
            case worldgen::Heightmap::WorldSurfaceWg:
            case worldgen::Heightmap::WorldSurface:
                return y + 1;
            case worldgen::Heightmap::OceanFloorWg:
            case worldgen::Heightmap::OceanFloor:
                if (!liquid && !blocks::canBeReplaced(b)) return y + 1;
                break;
            default:
                if (liquid || !blocks::canBeReplaced(b)) return y + 1;
            }
        }
        return minY();
    }
    [[nodiscard]] std::string  biome(BlockPos const&) const override { return {}; }
    [[nodiscard]] std::int64_t seed() const override { return 1234567; }
    [[nodiscard]] int          dimension() const override { return -1; } // scratch: no block entity work

    /// The golden volume hash of `box` (same order and text as selftest::Context::snapshotVolume).
    [[nodiscard]] std::string hash(BlockBox const& box) const {
        core::VolumeHasher h;
        for (int y = box.minY; y <= box.maxY; y++)
            for (int z = box.minZ; z <= box.maxZ; z++)
                for (int x = box.minX; x <= box.maxX; x++) {
                    auto const& b = getBlock(BlockPos{x, y, z});
                    // upstream blocks count as air (their states are F1's, a table that may still grow; tests name them where it matters)
                    if (!b.getTypeName().starts_with("minecraft:")) h.add(x - box.minX, y - box.minY, z - box.minZ, "minecraft:air");
                    else h.add(x - box.minX, y - box.minY, z - box.minZ, b.getTypeName(), game::statesOf(b));
                }
        return h.hex();
    }
    [[nodiscard]] std::size_t size() const { return mBlocks.size(); }
    /// Bounds of everything placed.
    [[nodiscard]] BlockBox bounds() const {
        BlockBox b{INT_MAX, INT_MAX, INT_MAX, INT_MIN, INT_MIN, INT_MIN};
        for (auto const& [k, _] : mBlocks) {
            int x = static_cast<int>(k >> 40) - (1 << 23), y = static_cast<int>((k >> 20) & 0xfffff) - (1 << 19), z = static_cast<int>(k & 0xfffff) - (1 << 19);
            b.minX = std::min(b.minX, x);
            b.minY = std::min(b.minY, y);
            b.minZ = std::min(b.minZ, z);
            b.maxX = std::max(b.maxX, x);
            b.maxY = std::max(b.maxY, y);
            b.maxZ = std::max(b.maxZ, z);
        }
        return b;
    }

private:
    static long long key(int x, int y, int z) {
        return (static_cast<long long>(x + (1 << 23)) << 40) | (static_cast<long long>(y + (1 << 19)) << 20) | static_cast<long long>(z + (1 << 19));
    }
    static long long colKey(int x, int z) { return (static_cast<long long>(x) << 32) ^ static_cast<unsigned int>(z); }
    std::unordered_map<long long, Block const*> mBlocks;
    std::unordered_map<long long, int>          mTop;
};

/// A region as world generation sees one, over the loaded world: blocks go straight into the
/// chunks' storage (LevelChunk::setBlockSimple: no block events, no neighbour updates, as a
/// ProtoChunk is written) and it is not a live region (no BlockSource adapter), so block entity
/// work and shape updates are queued for their chunk.
class ChunkWriteRegion final : public worldgen::Region {
public:
    ChunkWriteRegion(BlockSource& source, BlockPos const& center) : mSource(source), mCenter(center) { resetRegionRandom(this); }
    [[nodiscard]] int          centerChunkX() const override { return core::chunkOf(mCenter.x); }
    [[nodiscard]] int          centerChunkZ() const override { return core::chunkOf(mCenter.z); }
    [[nodiscard]] int          minY() const override { return mSource.getMinHeight(); }
    [[nodiscard]] int          maxY() const override { return mSource.getMaxHeight(); }
    [[nodiscard]] bool         isInside(int x, int z) const override { return mSource.hasChunksAt(BlockPos{x, mSource.getMinHeight(), z}, 0, false); }
    [[nodiscard]] Block const& getBlock(BlockPos const& pos) const override {
        if (pos.y < minY() || pos.y >= maxY() || !isInside(pos.x, pos.z)) return blocks::air();
        return mSource.getBlock(pos);
    }
    bool setBlock(BlockPos const& pos, Block const& block, int) override {
        if (pos.y < minY() || pos.y >= maxY()) return false;
        auto* chunk = mSource.getChunkAt(pos);
        if (!chunk) return false;
        chunk->setBlockSimple(ChunkBlockPos(pos, static_cast<short>(mSource.getMinHeight())), block);
        return true;
    }
    [[nodiscard]] int height(worldgen::Heightmap, int x, int z) const override {
        for (int y = maxY() - 1; y >= minY(); y--)
            if (!getBlock(BlockPos{x, y, z}).isAir()) return y + 1;
        return minY();
    }
    [[nodiscard]] std::string  biome(BlockPos const&) const override { return {}; }
    [[nodiscard]] std::int64_t seed() const override { return static_cast<std::int64_t>(mSource.getLevel().getSeed()); }
    [[nodiscard]] int          dimension() const override { return static_cast<int>(mSource.getDimensionId()); }

private:
    BlockSource& mSource;
    BlockPos     mCenter;
};

constexpr BlockPos kOrigin{0, 64, 0};

/// The golden hashes (reviewed once, rule R19).
std::string golden(std::string const& name) {
    static std::map<std::string, std::string> const table{
#include "SelfTestGoldens.inc"
    };
    auto it = table.find(name);
    return it == table.end() ? std::string{} : it->second;
}

void checkGolden(Context& ctx, std::string const& name, std::string const& actual) {
    auto expected = golden(name);
    ctx.info("golden " + name + " = " + actual);
    ctx.check(!expected.empty() && expected == actual, "golden hash " + name + ": expected " + (expected.empty() ? "<unset>" : expected) + ", got " + actual);
}

Json state(std::string name, Json props = Json::object()) {
    Json s{{"Name", std::move(name)}};
    if (!props.empty()) s["Properties"] = std::move(props);
    return s;
}

/// A template document from layers of rows ('.' = palette 0, digits / letters = palette index).
Json makeTemplate(std::string const& id, std::vector<Json> const& palette, std::vector<std::vector<std::string>> const& layers) {
    int  sy = static_cast<int>(layers.size());
    int  sz = static_cast<int>(layers[0].size());
    int  sx = static_cast<int>(layers[0][0].size());
    Json rle = Json::array();
    int  last = -1, count = 0;
    for (auto const& layer : layers)
        for (auto const& row : layer)
            for (char c : row) {
                int idx = c == '.' ? 0 : (c >= '0' && c <= '9') ? c - '0' : 10 + (c - 'a');
                if (idx == last) count++;
                else {
                    if (count) rle.push_back(Json::array({last, count}));
                    last  = idx;
                    count = 1;
                }
            }
    rle.push_back(Json::array({last, count}));
    return Json{{"format", "gloamingforest:template"}, {"format_version", 1}, {"id", id}, {"size", Json::array({sx, sy, sz})}, {"palette", palette},
                {"blocks", rle}};
}

/// The fixture used by most tests: every block family whose states turn with the template.
Json fixtureTemplate() {
    std::vector<Json> palette{
        state("minecraft:air"),
        state("minecraft:stone_bricks"),
        state("minecraft:oak_stairs", {{"facing", "east"}, {"half", "bottom"}}),
        state("minecraft:oak_log", {{"axis", "x"}}),
        state("minecraft:chest", {{"facing", "north"}}),
        state("minecraft:wall_torch", {{"facing", "south"}}),
        state("minecraft:cobblestone"),
        state("minecraft:oak_slab", {{"type", "top"}}),
        state("minecraft:spawner"),
        state("minecraft:ladder", {{"facing", "west"}}),
        state("minecraft:oak_planks"),
        state("minecraft:grass_block"),
        state("minecraft:stone_brick_stairs", {{"facing", "north"}, {"half", "top"}}),
        state("minecraft:mossy_cobblestone"),
        state("minecraft:oak_fence"),
        state("minecraft:structure_void"),
    };
    auto doc = makeTemplate("gf_test:structure/fixture", palette,
                            {
                                {"1111b", "1666b", "16a6b", "1666b", "11111"},
                                {"1.4.1", "2...9", "3.8.c", "1...1", "1e7f1"},
                                {"1...1", "5...1", "1...1", "1.d.1", "11111"},
                            });
    doc["markers"] = Json::array({
        Json{{"pos", {2, 1, 0}}, {"marker", "loot"}, {"data", {{"table", "gf_test:chests/fixture_chest"}}}},
        Json{{"pos", {2, 1, 2}}, {"marker", "spawner"}, {"data", {{"entity", "minecraft:zombie"}}}},
        Json{{"pos", {1, 2, 1}}, {"marker", "data"}, {"data", {{"metadata", "gf_test:mark@east"}}}},
    });
    doc["entities"] = Json::array({Json{{"pos", {2.5, 1.0, 3.5}}, {"id", "minecraft:armor_stand"}, {"data", Json::object()}}});
    return doc;
}

std::string const& fixtureId() {
    static std::string const id = [] {
        std::string error;
        templates::add(fixtureTemplate(), &error);
        return std::string("gf_test:structure/fixture");
    }();
    return id;
}

/// Blocks every processor acts on (template x 0..4, y 0..1, z 0..4), placed at kTargetsOrigin over
/// the world prepareProcessorWorld builds: sandstone slabs over stone bricks / air
/// (courtyard_terrace), smooth stone (smooth_stone_variants), grass on dirt, under stone and next to
/// podzol / mycelium / grass (smart_grass), full and partial blocks over solid, partial and
/// replaceable world blocks (soft_replace), a spawner without spawn data (spawner_processor) and
/// upstream's nagastone (nagastone_variants).
constexpr BlockPos kTargetsOrigin{10, 64, 0};

std::string const& processorTargetsId() {
    static std::string const id = [] {
        std::vector<Json> palette{
            state("minecraft:air"),
            state("minecraft:sandstone_slab", {{"type", "bottom"}, {"waterlogged", "false"}}),
            state("minecraft:sandstone_slab", {{"type", "double"}, {"waterlogged", "false"}}),
            state("minecraft:smooth_stone"),
            state("minecraft:smooth_stone_slab", {{"type", "bottom"}, {"waterlogged", "false"}}),
            state("minecraft:grass_block", {{"snowy", "false"}}),
            state("minecraft:spawner"),
            state("minecraft:stone_bricks"),
            state("gloamingforest:etched_nagastone"),
            state("gloamingforest:nagastone_pillar", {{"axis", "y"}}),
            state("minecraft:cobblestone"),
            state("minecraft:oak_planks"),
            state("minecraft:stone_brick_stairs", {{"facing", "north"}, {"half", "bottom"}, {"waterlogged", "false"}}),
            state("minecraft:mossy_cobblestone"),
        };
        auto doc = makeTemplate("gf_test:structure/processor_targets", palette,
                                {
                                    {"12121", "34343", "55555", "7a6b7", "89c89"},
                                    {"12121", "3.4.3", "5.5.5", ".....", "d...d"},
                                });
        std::string error;
        templates::add(doc, &error);
        return std::string("gf_test:structure/processor_targets");
    }();
    return id;
}

/// The world the processors look at: the old dirt / podzol / fence pattern under the fixture, and
/// the cases above under the targets.
void prepareProcessorWorld(MemoryRegion& mem) {
    for (int x = -2; x < 8; x++)
        for (int z = -2; z < 8; z++) {
            (void)mem.setBlock(BlockPos{x, 63, z}, blocks::java((x + z) % 3 == 0 ? "minecraft:podzol" : "minecraft:dirt"), 3);
            if ((x * 7 + z) % 5 == 0) (void)mem.setBlock(BlockPos{x, 64 + (x + z) % 3, z}, blocks::java("minecraft:oak_fence"), 3);
        }
    for (int x = 8; x <= 17; x++)
        for (int z = -2; z < 8; z++) (void)mem.setBlock(BlockPos{x, 63, z}, blocks::java("minecraft:stone"), 3);
    auto put = [&](int x, int y, int z, char const* id) { (void)mem.setBlock(BlockPos{x, y, z}, blocks::java(id), 3); };
    // courtyard_terrace (row z 0)
    put(10, 64, 0, "minecraft:stone_bricks");
    put(12, 64, 0, "minecraft:mossy_stone_bricks");
    put(13, 64, 0, "minecraft:stone_brick_slab");
    put(14, 64, 0, "minecraft:cobblestone");
    put(11, 65, 0, "minecraft:cracked_stone_bricks");
    put(13, 65, 0, "minecraft:cobblestone");
    // smart_grass (row z 2, neighbours in z 1)
    put(10, 64, 2, "minecraft:dirt");
    put(11, 64, 1, "minecraft:podzol");
    put(12, 65, 2, "minecraft:stone");
    put(13, 64, 1, "minecraft:mycelium");
    put(15, 64, 2, "minecraft:grass_block");
    // soft_replace (rows z 3, 4)
    put(10, 64, 3, "minecraft:bricks"); // stone would be worldgen-replaceable (lush_ground_replaceable)
    put(11, 64, 3, "minecraft:oak_fence");
    put(13, 64, 3, "minecraft:short_grass");
    put(14, 64, 3, "minecraft:stone_slab");
    put(12, 64, 4, "minecraft:oak_fence");
}

/// Whether the container at `pos` holds what gf_test:chests/fixture_chest rolls once its loot
/// table is unpacked (the loot contract, F10: 1..3 rolls of fixture items or sticks, maybe a
/// diamond), and nothing else.
bool holdsFixtureLoot(BlockSource& source, BlockPos const& pos) {
    auto items = containerItems(source, pos);
    if (!items) return false;
    int stacks = 0;
    for (auto const& s : *items) {
        if (s.isNull()) continue;
        auto id = s.getTypeName();
        if (id != "gf_test:fixture_item" && id != "minecraft:stick" && id != "minecraft:diamond") return false;
        if (id != "minecraft:diamond") stacks++;
    }
    return stacks > 0;
}

/// A piece just to reach the helpers of ComponentOld in tests.
class TestPiece final : public bases::ComponentOld {
public:
    TestPiece(BlockBox const& box, std::optional<Orientation> o) : ComponentOld("gf_test:structure/piece", 0, box) { setOrientation(o); }
    void postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) override {}
    using ComponentOld::placeTreasureAtCurrentPosition;
    using ComponentOld::setSpawner;
    using ComponentOld::surroundBlockCardinalRotated;
};

} // namespace

// ---- block state conversion --------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("structure.block_states", [](Context& ctx) {
    auto const* stairs = blocks::fromJava("minecraft:oak_stairs", Json{{"facing", "east"}, {"half", "top"}});
    ctx.require(stairs != nullptr, "oak stairs resolve");
    auto st = game::statesOf(*stairs);
    ctx.check(st["weirdo_direction"] == "0" && st["upside_down_bit"] == "true", "stairs facing east / top -> weirdo 0, upside down");
    ctx.check(blocks::javaProperty(*stairs, "facing") == std::optional<std::string>("east"), "stairs read back facing east");
    ctx.check(blocks::javaProperty(blocks::rotate(*stairs, Rotation::Clockwise90), "facing") == std::optional<std::string>("south"), "CW90 turns east to south");
    ctx.check(blocks::javaProperty(blocks::mirror(blocks::java("minecraft:oak_stairs", Json{{"facing", "north"}}), Mirror::LeftRight), "facing") ==
                  std::optional<std::string>("south"),
              "LEFT_RIGHT mirrors north to south");

    auto const* dbl = blocks::fromJava("minecraft:oak_slab", Json{{"type", "double"}});
    ctx.check(dbl && blocks::id(*dbl) == "minecraft:oak_double_slab", "a double slab is the double slab block");
    auto const* top = blocks::fromJava("minecraft:oak_slab", Json{{"type", "top"}});
    ctx.check(top && blocks::javaProperty(*top, "type") == std::optional<std::string>("top"), "a top slab");

    auto const* log = blocks::fromJava("minecraft:oak_log", Json{{"axis", "x"}});
    ctx.check(log && game::statesOf(*log)["pillar_axis"] == "x", "log axis x");
    ctx.check(game::statesOf(blocks::rotate(*log, Rotation::Clockwise90))["pillar_axis"] == "z", "CW90 turns axis x to z");

    auto const* torch = blocks::fromJava("minecraft:wall_torch", Json{{"facing", "north"}});
    ctx.check(torch && blocks::id(*torch) == "minecraft:torch" && game::statesOf(*torch)["torch_facing_direction"] == "north", "wall torch facing north");
    auto const* standing = blocks::fromJava("minecraft:torch");
    ctx.check(standing && game::statesOf(*standing)["torch_facing_direction"] == "top", "a standing torch faces top");

    auto const* chest = blocks::fromJava("minecraft:chest", Json{{"facing", "south"}});
    ctx.check(chest && blocks::javaProperty(*chest, "facing") == std::optional<std::string>("south"), "chest facing south");

    ctx.check(blocks::bedrockId("minecraft:spawner") == "minecraft:mob_spawner", "spawner -> mob_spawner");
    ctx.check(blocks::bedrockId("minecraft:cobweb") == "minecraft:web", "cobweb -> web");
    ctx.check(blocks::bedrockId("minecraft:stone_stairs") == "minecraft:normal_stone_stairs", "Java stone stairs -> normal_stone_stairs");
    ctx.check(blocks::bedrockId("minecraft:cobblestone_stairs") == "minecraft:stone_stairs", "Java cobblestone stairs -> stone_stairs");
    ctx.check(blocks::bedrockId("minecraft:cave_air") == "minecraft:air", "cave air -> air");

    auto vine = blocks::fromJava("minecraft:vine", Json{{"north", true}});
    ctx.check(vine && game::statesOf(blocks::rotate(*vine, Rotation::Clockwise90))["vine_direction_bits"] == "8", "a north vine turns east");

    auto unresolved = blocks::unresolvedRenames();
    std::string list;
    for (auto const& u : unresolved) list += u + " ";
    ctx.info("renames without a Bedrock block: " + (list.empty() ? std::string("none") : list));
});

// ---- piece coordinates -----------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("structure.piece_coordinates", [](Context& ctx) {
    BlockBox box{100, 10, 200, 109, 19, 214};
    struct VanillaPiece final : Piece {
        VanillaPiece() : Piece("gf_test:structure/vanilla") {}
        void postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) override {}
    } vanilla;
    vanilla.bbox = box;
    // StructurePiece#getWorldX/Z
    vanilla.orientation = Orientation::North;
    ctx.check(vanilla.getWorldPos(1, 2, 3) == BlockPos(101, 12, 211), "vanilla NORTH");
    vanilla.orientation = Orientation::South;
    ctx.check(vanilla.getWorldPos(1, 2, 3) == BlockPos(101, 12, 203), "vanilla SOUTH");
    vanilla.orientation = Orientation::West;
    ctx.check(vanilla.getWorldPos(1, 2, 3) == BlockPos(106, 12, 201), "vanilla WEST");
    vanilla.orientation = Orientation::East;
    ctx.check(vanilla.getWorldPos(1, 2, 3) == BlockPos(103, 12, 201), "vanilla EAST");
    // TFStructureComponentOld#getWorldX/Z and setOrientation
    TestPiece north(box, Orientation::North), south(box, Orientation::South), west(box, Orientation::West), east(box, Orientation::East), none(box, std::nullopt);
    ctx.check(north.getWorldPos(1, 2, 3) == BlockPos(108, 12, 211) && north.rotation == Rotation::None, "upstream NORTH");
    ctx.check(south.getWorldPos(1, 2, 3) == BlockPos(101, 12, 203) && south.rotation == Rotation::Clockwise180, "upstream SOUTH");
    ctx.check(west.getWorldPos(1, 2, 3) == BlockPos(106, 12, 201) && west.rotation == Rotation::Counterclockwise90, "upstream WEST");
    ctx.check(east.getWorldPos(1, 2, 3) == BlockPos(103, 12, 213) && east.rotation == Rotation::Clockwise90, "upstream EAST");
    ctx.check(none.getWorldPos(1, 2, 3) == BlockPos(1, 2, 3), "no orientation: world coordinates");
    ctx.check(bases::ComponentOld::getComponentToAddBoundingBox2(10, 20, 30, -1, 0, -2, 5, 6, 7, Orientation::West) == BlockBox::of(5, 20, 29, 12, 26, 34),
              "getComponentToAddBoundingBox2 WEST");
    // clipping to the chunk box
    MemoryRegion mem;
    BlockBox     chunk{100, 10, 200, 104, 19, 214};
    south.fill(mem, chunk, 0, 0, 0, 9, 0, 0, blocks::java("minecraft:stone"), blocks::java("minecraft:stone"), false);
    ctx.check(mem.size() == 5, "a fill is clipped to the chunk box (" + std::to_string(mem.size()) + " blocks)");
    // F6 registers its fixture landmark's piece types itself (structure::registerPieceType), so a
    // persisted fixture start loads its pieces back through the kit
    for (auto const* id : {"gf_test:landmark_fixture_shell", "gf_test:landmark_fixture_porch"}) {
        auto loaded = loadPiece(Json{{"id", id}, {"BB", Json::array({0, 0, 0, 2, 2, 2})}});
        ctx.check(loaded && loaded->bbox == BlockBox::of(0, 0, 0, 2, 2, 2), std::string("F6's piece type ") + id + " loads through its own factory");
    }
});

// ---- templates ----------------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("structure.template_validate", [](Context& ctx) {
    ctx.check(templates::validate(fixtureTemplate()).empty(), "the fixture template is valid");
    Json bad    = fixtureTemplate();
    bad["size"] = Json::array({5, 3, 4});
    ctx.check(!templates::validate(bad).empty(), "a size that does not match the blocks is refused");
    Json badMarker                 = fixtureTemplate();
    badMarker["markers"][0]["pos"] = Json::array({9, 9, 9});
    ctx.check(!templates::validate(badMarker).empty(), "a marker outside the template is refused");
    ctx.check(templates::parsed(fixtureId()) != nullptr, "the fixture registers");
    ctx.check(templates::get("gf_test:fixture") != nullptr, "F0's assets/test fixture template is found on disk");
    // entity data: the fields the kit applies (Rotation, nbt) and nothing it would lose silently
    Json entity                         = fixtureTemplate();
    entity["entities"][0]["data"]       = Json{{"Rotation", {30.0, 0.0}}, {"nbt", "{CustomName:\"stand\"}"}};
    ctx.check(templates::validate(entity).empty(), "an entity with Rotation and nbt is valid");
    entity["entities"][0]["data"]["ArmorItems"] = Json::array();
    ctx.check(!templates::validate(entity).empty(), "an entity data field the kit does not apply is refused");
    entity["entities"][0]["data"].erase("ArmorItems");
    entity["entities"][0]["data"]["nbt"] = "{not snbt";
    ctx.check(!templates::validate(entity).empty(), "unreadable entity nbt is refused");
});

GF_REGISTER_SELFTEST("structure.template_transforms", [](Context& ctx) {
    auto const* t = templates::parsed(fixtureId());
    ctx.require(t != nullptr, "fixture");
    struct Case {
        char const* name;
        Rotation    r;
        Mirror      m;
    };
    for (auto const& c : {Case{"none", Rotation::None, Mirror::None}, Case{"cw90", Rotation::Clockwise90, Mirror::None},
                          Case{"cw180", Rotation::Clockwise180, Mirror::None}, Case{"ccw90", Rotation::Counterclockwise90, Mirror::None},
                          Case{"left_right", Rotation::None, Mirror::LeftRight}, Case{"front_back", Rotation::None, Mirror::FrontBack}}) {
        MemoryRegion         mem;
        templates::Placement p;
        p.offset            = kOrigin;
        p.pivotPos          = kOrigin;
        p.settings.rotation = c.r;
        p.settings.mirror   = c.m;
        p.rotationPivot     = BlockPos{2, 0, 2};
        JavaRandom                                    random(42);
        std::vector<std::pair<BlockPos, std::string>> data;
        ctx.require(templates::placeInWorld(*t, mem, p, random, &data), std::string("placed ") + c.name);
        BlockBox box = templateBox(kOrigin, c.r, p.rotationPivot, c.m, t->sx, t->sy, t->sz);
        checkGolden(ctx, std::string("template_") + c.name, mem.hash(box));
        ctx.check(data.size() == 1 && data[0].second == "gf_test:mark@east", std::string("one data marker with its label (") + c.name + ")");
    }
    // StructureTemplate#placeEntities turns an entity's yaw by Entity#rotate + Entity#mirror - yaw
    auto near = [](float a, float b) { return std::abs(std::remainder(a - b, 360.0f)) < 1e-3f; };
    ctx.check(near(templates::turnedYaw(30.0f, Rotation::None, Mirror::None), 30.0f), "an entity keeps its yaw without a transform");
    ctx.check(near(templates::turnedYaw(30.0f, Rotation::Clockwise90, Mirror::None), 120.0f), "CLOCKWISE_90 adds 90 degrees");
    ctx.check(near(templates::turnedYaw(30.0f, Rotation::Counterclockwise90, Mirror::None), 300.0f), "COUNTERCLOCKWISE_90 adds 270 degrees");
    ctx.check(near(templates::turnedYaw(30.0f, Rotation::None, Mirror::FrontBack), -30.0f), "FRONT_BACK negates the yaw");
    ctx.check(near(templates::turnedYaw(30.0f, Rotation::Clockwise90, Mirror::LeftRight), 240.0f), "LEFT_RIGHT and CLOCKWISE_90: 90 + 180 - yaw");
    // HangingEntity#rotate first, then #mirror on the turned direction
    ctx.check(templates::turnedPaintingFacing(Orientation::North, Rotation::Clockwise90, Mirror::LeftRight) == Orientation::East,
              "a north painting under LEFT_RIGHT and CLOCKWISE_90 faces east (rotated to east, which LEFT_RIGHT keeps)");
    ctx.check(templates::turnedPaintingFacing(Orientation::North, Rotation::None, Mirror::LeftRight) == Orientation::South, "LEFT_RIGHT flips a north painting");
    ctx.check(templates::turnedPaintingFacing(Orientation::East, Rotation::Clockwise90, Mirror::FrontBack) == Orientation::South,
              "FRONT_BACK keeps a painting turned to south");
});

GF_REGISTER_SELFTEST("structure.template_world", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto area = co_await ctx.flatArea(5700, 12, 8);
    auto pos  = ctx.testPos(5700);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto* source = ctx.region();
    ctx.require(source != nullptr, "block source");
    // CW90 turns the 5 x 5 footprint to x - 4 .. x: it stays inside the flat area and this unit's
    // row (dz 0..4); entities are left out (the area restore only puts blocks back)
    BlockPos         origin{area.minX + 6, area.minY, area.minZ};
    TemplateSettings settings;
    settings.rotation       = Rotation::Clockwise90;
    settings.ignoreEntities = true;
    {
        BlockSourceRegion region(*source, origin);
        ctx.require(templates::place(fixtureId(), region, origin, settings), "the fixture places in the world");
    }
    auto const* t = templates::parsed(fixtureId());
    BlockBox    box = templateBox(origin, settings.rotation, BlockPos{0, 0, 0}, Mirror::None, t->sx, t->sy, t->sz);
    // the same placement in memory, translated to the in-memory origin
    MemoryRegion mem;
    ctx.require(templates::place(fixtureId(), mem, kOrigin, settings), "the fixture places in memory");
    BlockBox memBox = templateBox(kOrigin, settings.rotation, BlockPos{0, 0, 0}, Mirror::None, t->sx, t->sy, t->sz);
    std::string diffs;
    for (int y = 0; y <= box.maxY - box.minY && diffs.size() < 400; y++)
        for (int z = 0; z <= box.maxZ - box.minZ; z++)
            for (int x = 0; x <= box.maxX - box.minX; x++) {
                auto const& w = source->getBlock(BlockPos{box.minX + x, box.minY + y, box.minZ + z});
                auto const& m = mem.getBlock(BlockPos{memBox.minX + x, memBox.minY + y, memBox.minZ + z});
                // the game derives fence / wall / pane connections from the neighbours on its own
                auto shape = [](Block const& b) {
                    auto s = game::statesOf(b);
                    std::erase_if(s, [](auto const& kv) { return kv.first.find("connection") != std::string::npos || kv.first == "wall_post_bit"; });
                    return s;
                };
                if (w.getTypeName() != m.getTypeName() || shape(w) != shape(m))
                    diffs += "(" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) + ") world " + w.getTypeName() + " " +
                             Json(game::statesOf(w)).dump() + " memory " + m.getTypeName() + " " + Json(game::statesOf(m)).dump() + "; ";
            }
    ctx.check(diffs.empty(), "the world and the in-memory placement match (connections aside): " + diffs);
    checkGolden(ctx, "template_world_cw90", mem.hash(memBox));
    co_await ctx.ticks(8); // block actors exist a tick later; queued work runs every 5 ticks
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    auto worldOf = [&](BlockPos const& local) {
        BlockPos rel = templates::relative(templates::Placement{origin, origin, settings, BlockPos{0, 0, 0}}, local);
        return BlockPos{rel.x + origin.x, rel.y + origin.y, rel.z + origin.z};
    };
    BlockPos spawnerPos = worldOf(BlockPos{2, 1, 2});
    ctx.check(game::blockIdAt(*source, spawnerPos) == "minecraft:mob_spawner", "the spawner stands");
    // the template's block entity markers ran in the world: the spawner's mob, the chest's loot table
    std::string mob;
    if (auto* actor = source->getBlockEntity(spawnerPos); actor && actor->getType() == BlockActorType::MobSpawner)
        if (auto* spawner = static_cast<MobSpawnerBlockActor&>(*actor).mSpawner.get()) mob = spawner->mActorDefId->mCanonicalName->getString();
    ctx.check(mob == "minecraft:zombie", "the spawner marker set the spawner's mob (" + mob + ")");
    BlockPos chestPos = worldOf(BlockPos{2, 1, 0});
    ctx.check(game::blockIdAt(*source, chestPos) == "minecraft:chest", "the chest stands");
    ctx.check(holdsFixtureLoot(*source, chestPos), "the loot marker gave the chest its loot table (rolled by the loot contract when unpacked)");
    co_return;
});

// ---- processors ---------------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("structure.processors", [](Context& ctx) {
    auto const* t = templates::parsed(fixtureId());
    ctx.require(t != nullptr, "fixture");
    // every upstream processor (and the vanilla ones upstream's data uses) on the fixture, seeded
    std::vector<std::pair<std::string, Json>> cases{
        {"box_cutting", Json{{"processor_type", "gloamingforest:box_cutting"}, {"boxes", Json::array({Json::array({0, 64, 0, 1, 65, 4})})}}},
        {"cobble_variants", Json{{"processor_type", "gloamingforest:cobble_variants"}}},
        {"courtyard_terrace", Json{{"processor_type", "gloamingforest:courtyard_terrace"}}},
        {"infest_blocks", Json{{"processor_type", "gloamingforest:infest_blocks"}}},
        {"nagastone_variants", Json{{"processor_type", "gloamingforest:nagastone_variants"}}},
        {"smart_grass", Json{{"processor_type", "gloamingforest:smart_grass"}}},
        {"smooth_stone_variants", Json{{"processor_type", "gloamingforest:smooth_stone_variants"}}},
        {"soft_replace", Json{{"processor_type", "gloamingforest:soft_replace"}}},
        {"spawner_processor",
         Json{{"processor_type", "gloamingforest:spawner_processor"},
              {"range", 6},
              {"start_delay_factor", 0.25},
              {"entity_width_max", 0.8},
              {"entities", Json::array({Json{{"data", {{"entity", {{"id", "minecraft:skeleton"}}}}}, {"weight", 3}},
                                        Json{{"data", {{"entity", {{"id", "minecraft:spider"}}}}}, {"weight", 1}}})}}},
        {"state_transfiguring",
         Json{{"processor_type", "gloamingforest:state_transfiguring"},
              {"rules", Json::array({Json{{"input_predicate", {{"predicate_type", "minecraft:random_block_match"}, {"block", "minecraft:stone_bricks"}, {"probability", 0.5}}},
                                          {"location_predicate", {{"predicate_type", "minecraft:always_true"}}},
                                          {"output_state", {{"Name", "minecraft:mossy_stone_bricks"}}}}})}}},
        {"stone_brick_variants", Json{{"processor_type", "gloamingforest:stone_brick_variants"}}},
        {"targeted_rot", Json{{"processor_type", "gloamingforest:targeted_rot"}, {"blocks_to_rot", Json::array({state("minecraft:stone_bricks")})}, {"integrity", 0.5}}},
        {"update_marking", Json{{"processor_type", "gloamingforest:update_marking"}, {"mark_updates", Json::array({"minecraft:oak_fence"})}}},
        {"vertical_decay", Json{{"processor_type", "gloamingforest:vertical_decay"}, {"decay_blocks", Json::array({"minecraft:stone_bricks"})}, {"decay_chance", 0.3}}},
        {"web", Json{{"processor_type", "gloamingforest:web"}}},
        {"wood_multiswizzle", Json{{"processor_type", "gloamingforest:wood_multiswizzle"}, {"palettes", Json::array({Json::array({"minecraft:oak", "minecraft:dark_oak"})})}}},
        {"wood_swizzle", Json{{"processor_type", "gloamingforest:wood_swizzle"}, {"target_palette", "minecraft:oak"}, {"replacement_palette", "minecraft:spruce"}}},
        {"rule", Json{{"processor_type", "minecraft:rule"},
                      {"rules", Json::array({Json{{"input_predicate", {{"predicate_type", "minecraft:block_match"}, {"block", "minecraft:cobblestone"}}},
                                                  {"location_predicate", {{"predicate_type", "minecraft:always_true"}}},
                                                  {"output_state", {{"Name", "minecraft:andesite"}}}}})}}},
        {"gravity", Json{{"processor_type", "minecraft:gravity"}, {"heightmap", "WORLD_SURFACE_WG"}, {"offset", 0}}},
        {"block_ignore", Json{{"processor_type", "minecraft:block_ignore"}, {"blocks", Json::array({state("minecraft:cobblestone")})}}},
    };
    auto const* targets = templates::parsed(processorTargetsId());
    ctx.require(targets != nullptr, "processor targets fixture");
    // the fixture at kOrigin, the processor targets 10 blocks east, over a prepared world; each
    // placement shares one random seeded 0x5eed between its blocks (StructurePlaceSettings
    // #setRandom), so the order the blocks are processed in (TemplateOrder.cpp) shows in the result
    struct Run {
        std::string                    hash;
        std::vector<templates::Placed> fixture; ///< the fixture's placement
        std::vector<templates::Placed> placed;  ///< the targets' placement
    };
    auto place = [&](std::vector<std::string> const& processors, MemoryRegion& mem) {
        prepareProcessorWorld(mem);
        Run run;
        for (int i = 0; i < 2; i++) {
            templates::Placement p;
            p.offset              = i == 0 ? kOrigin : kTargetsOrigin;
            p.pivotPos            = p.offset;
            p.settings.processors = processors;
            p.settings.seed       = 0x5eed;
            JavaRandom random(7);
            ctx.require(templates::placeInWorld(i == 0 ? *t : *targets, mem, p, random, nullptr, i == 1 ? &run.placed : &run.fixture), "placed");
        }
        run.hash = mem.hash(BlockBox{-2, 62, -2, 17, 70, 7});
        return run;
    };
    // what a placement put where (the later block wins; upstream blocks as air, as in the hashes), by
    // y, then z, then x
    auto blocksOf = [](Run const& r) {
        std::map<std::tuple<int, int, int>, std::string> m;
        for (auto const* list : {&r.fixture, &r.placed})
            for (auto const& p : *list) {
                std::string id = p.block ? blocks::id(*p.block) : std::string("minecraft:air");
                if (!id.starts_with("minecraft:")) id = "minecraft:air";
                m[{p.pos.y, p.pos.z, p.pos.x}] = id;
            }
        return m;
    };
    // the positions a processor changed against the plain placement ("x,y,z:block", "-": nothing
    // placed there), for comparison with a model of Java's StructureTemplate processing
    auto diffOf = [&](Run const& plainRun, Run const& r) {
        auto        a = blocksOf(plainRun), b = blocksOf(r);
        std::string out;
        std::set<std::tuple<int, int, int>> keys;
        for (auto const& [k, _] : a) keys.insert(k);
        for (auto const& [k, _] : b) keys.insert(k);
        for (auto const& k : keys) {
            auto ia = a.find(k), ib = b.find(k);
            std::string va = ia == a.end() ? "-" : ia->second, vb = ib == b.end() ? "-" : ib->second;
            if (va == vb) continue;
            if (vb.starts_with("minecraft:")) vb = vb.substr(10);
            out += std::to_string(std::get<2>(k)) + "," + std::to_string(std::get<0>(k)) + "," + std::to_string(std::get<1>(k)) + ":" + vb + ";";
        }
        return out;
    };
    MemoryRegion plainMem;
    auto         plain = place({}, plainMem);
    checkGolden(ctx, "processor_none", plain.hash);
    std::map<std::string, std::unique_ptr<MemoryRegion>> worlds;
    std::map<std::string, Run>                           runs;
    for (auto const& [name, config] : cases) {
        auto mem    = std::make_unique<MemoryRegion>();
        runs[name]  = place({config.dump()}, *mem);
        checkGolden(ctx, "processor_" + name, runs[name].hash);
        ctx.info("diff processor_" + name + ": " + diffOf(plain, runs[name]));
        worlds[name] = std::move(mem);
    }
    auto at = [&](std::string const& name, int x, int y, int z) -> Block const& { return worlds[name]->getBlock(BlockPos{x, y, z}); };
    auto is = [&](std::string const& name, int x, int y, int z, char const* id) { return blocks::is(at(name, x, y, z), id); };
    // each processor did its job on the targets (template x 0..4 at world x 10..14, y 64..65)
    ctx.check(is("courtyard_terrace", 10, 64, 0, "minecraft:stone_brick_slab") && at("courtyard_terrace", 11, 64, 0).isAir() &&
                  is("courtyard_terrace", 13, 64, 0, "minecraft:stone_brick_slab") && is("courtyard_terrace", 13, 65, 0, "minecraft:stone_bricks") &&
                  at("courtyard_terrace", 10, 65, 0).isAir() && is("courtyard_terrace", 11, 65, 0, "minecraft:stone_brick_slab"),
              "courtyard_terrace: sandstone slabs become stone brick slabs / bricks on stone and vanish over air");
    ctx.check(is("smart_grass", 10, 64, 2, "minecraft:dirt") && is("smart_grass", 11, 64, 2, "minecraft:podzol") && at("smart_grass", 12, 64, 2).isAir() &&
                  is("smart_grass", 13, 64, 2, "minecraft:mycelium") && is("smart_grass", 14, 64, 2, "minecraft:grass_block"),
              "smart_grass: grass is dropped on dirt or under a block, takes podzol / mycelium from a neighbour, stays grass next to grass");
    int smooth = 0, cobbled = 0;
    for (auto const& p : runs["smooth_stone_variants"].placed) {
        if (!p.block) continue;
        if (blocks::is(*p.block, "minecraft:smooth_stone") || blocks::is(*p.block, "minecraft:smooth_stone_slab")) smooth++;
        if (blocks::is(*p.block, "minecraft:cobblestone") || blocks::is(*p.block, "minecraft:cobblestone_slab")) cobbled++;
    }
    ctx.check(smooth > 0 && cobbled > 0, "smooth_stone_variants: some smooth stone turns to cobblestone (" + std::to_string(cobbled) + " of " +
                                             std::to_string(smooth + cobbled) + ")");
    auto softCheck = [&](int x, int y, int z, char const* id, char const* what) {
        ctx.check(is("soft_replace", x, y, z, id), std::string("soft_replace: ") + what + " (" + blocks::id(at("soft_replace", x, y, z)) + ")");
    };
    softCheck(10, 64, 3, "minecraft:bricks", "a solid world block stays");
    softCheck(11, 64, 3, "minecraft:cobblestone", "a full block goes over a fence");
    softCheck(13, 64, 3, "minecraft:oak_planks", "a plant is replaced");
    softCheck(14, 64, 3, "minecraft:stone_bricks", "a full block goes over a slab");
    softCheck(12, 64, 4, "minecraft:oak_fence", "no stairs over a fence");
    // spawner_processor: upstream always rolls the spawn data (and the delay from MinSpawnDelay 200)
    bool spawnerChecked = false;
    for (auto const& p : runs["spawner_processor"].placed) {
        if (!p.block || !blocks::is(*p.block, "minecraft:spawner")) continue;
        spawnerChecked = true;
        auto entity    = p.data.value("entity", std::string{});
        ctx.check(entity == "minecraft:skeleton" || entity == "minecraft:spider", "spawner_processor rolled the entity (" + entity + ")");
        ctx.check(p.data.value("SpawnRange", 0) == 6 && p.data.value("Delay", -1) == 50, "spawner_processor: range 6, delay round(200 * 0.25)");
        if (entity == "minecraft:spider")
            ctx.check(std::abs(p.data.value("entity_scale", 1.0f) - 0.8f / 1.4f) < 1e-5f, "a spider wider than 0.8 is scaled to fit");
    }
    ctx.check(spawnerChecked, "the targets' spawner was processed");
    // update_marking changes nothing here (Bedrock updates shapes itself)
    ctx.check(runs["update_marking"].hash == plain.hash, "update_marking places the template unchanged");
    // nagastone_variants on upstream's nagastone (F1's blocks; they hash as air): the targets' etched
    // nagastone and pillars become mossy or cracked as Java's processing order draws them, keeping
    // their states (FeaturePlacers.transferAllStateKeys), and vanilla blocks stay
    ctx.require(blocks::isRegisteredBlock("gloamingforest:etched_nagastone") && blocks::isRegisteredBlock("gloamingforest:nagastone_pillar") &&
                    blocks::isRegisteredBlock("gloamingforest:cracked_etched_nagastone") && blocks::isRegisteredBlock("gloamingforest:mossy_nagastone_pillar"),
                "upstream's nagastone blocks are registered");
    std::string nagastone;
    for (auto const& p : runs["nagastone_variants"].placed)
        if (p.block && blocks::id(*p.block).find("nagastone") != std::string::npos)
            nagastone += std::to_string(p.pos.x) + "," + std::to_string(p.pos.y) + "," + std::to_string(p.pos.z) + ":" + blocks::id(*p.block) + ";";
    checkGolden(ctx, "nagastone_variants_blocks", nagastone);
    ctx.check(runs["nagastone_variants"].hash == plain.hash, "nagastone_variants leaves vanilla blocks alone");
    auto const* etched = blocks::fromJava("gloamingforest:etched_nagastone");
    for (auto const& p : runs["nagastone_variants"].placed)
        if (p.block && etched && blocks::id(*p.block) == "gloamingforest:cracked_etched_nagastone")
            ctx.check(blocks::javaProperty(*p.block, "facing") == blocks::javaProperty(*etched, "facing"),
                      "the cracked etched nagastone keeps the etched nagastone's facing");
    ctx.check(processorTypes().size() >= 24, "17 upstream + 7 vanilla processor types registered (" + std::to_string(processorTypes().size()) + ")");
});

// ---- a feature's random shared by its placements (TemplateFeature, GraveyardFeature) ------------------------------

namespace {

/// The hut and basement of structure.shared_random. The hut (3 x 2 x 3) has its data marker on a
/// structure_void cell (the template DSL's default fill), which is Java's DATA structure block, and
/// a chest after it in Java's block entity list (its LootTableSeed is drawn from the placement's
/// random); the basement is all stone bricks.
std::string const& sharedHutId(bool marker) {
    static auto make = [](bool withMarker) {
        std::vector<Json> palette{state("minecraft:air"), state("minecraft:stone_bricks"), state("minecraft:structure_void"),
                                  state("minecraft:chest", {{"facing", "north"}, {"type", "single"}, {"waterlogged", "false"}}),
                                  state("minecraft:cobblestone")};
        std::string id  = withMarker ? "gf_test:structure/shared_hut" : "gf_test:structure/shared_hut_unmarked";
        auto        doc = makeTemplate(id, palette, {{"111", "141", "111"}, {"1.2", "...", "1.3"}});
        if (withMarker) doc["markers"] = Json::array({Json{{"pos", {2, 1, 0}}, {"marker", "data"}, {"data", {{"metadata", "gf_test:void_mark"}}}}});
        templates::add(doc);
        return id;
    };
    static std::string const marked = make(true), unmarked = make(false);
    return marker ? marked : unmarked;
}

std::string const& sharedBasementId() {
    static std::string const id = [] {
        templates::add(makeTemplate("gf_test:structure/shared_basement", {state("minecraft:air"), state("minecraft:stone_bricks")}, {{"111", "111", "111"}}));
        return std::string("gf_test:structure/shared_basement");
    }();
    return id;
}

} // namespace

GF_REGISTER_SELFTEST("structure.shared_random", [](Context& ctx) {
    // TemplateFeature#place with a random of its own (the druid hut): settings.setRandom(random);
    // the hut placed (palette pick, its blocks processed in Java's order: the DATA structure block of
    // its data marker too, then the chest's LootTableSeed), filterBlocks for the data markers (the
    // palette pick again), then the basement with the same random (postPlacement). StoneBricksVariants
    // reseeds the random from every block it sees, so each draw moves the whole rest of the sequence.
    // tests/structure/oracle/TemplateOracle.java places the same with 1.21.1's StructureTemplate ("shared_random:").
    auto run = [&](bool marker, std::vector<std::pair<BlockPos, std::string>>* markers) {
        MemoryRegion     mem;
        JavaRandom       random(0x5eed);
        TemplateSettings settings;
        settings.processors = {Json{{"processor_type", "gloamingforest:stone_brick_variants"}}.dump()};
        BlockPos hut{30, 64, 0}, basement{30, 60, 0};
        ctx.check(ext::placeTemplate(sharedHutId(marker), mem, hut, hut, settings, random), "the hut places");
        auto found = ext::filterDataMarkers(sharedHutId(marker), hut, settings, random);
        if (markers) *markers = found;
        ctx.check(ext::placeTemplate(sharedBasementId(), mem, basement, basement, settings, random), "the basement places with the same random");
        // the variants the two placements left, by y, then z, then x
        std::string out;
        for (int y = 60; y <= 65; y++)
            for (int z = 0; z <= 2; z++)
                for (int x = 30; x <= 32; x++) {
                    auto const& id = blocks::id(mem.getBlock(BlockPos{x, y, z}));
                    if (id == "minecraft:mossy_stone_bricks" || id == "minecraft:cracked_stone_bricks")
                        out += std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) + ":" + id.substr(10) + ";";
                }
        // the data marker's cell is the marker handler's: nothing is placed there
        ctx.check(mem.getBlock(BlockPos{32, 65, 0}).isAir(), "nothing is placed on the data marker's structure_void cell");
        return out;
    };
    std::vector<std::pair<BlockPos, std::string>> markers;
    auto withMarker = run(true, &markers);
    auto without    = run(false, nullptr);
    ctx.info("shared_random: " + withMarker);
    ctx.check(markers.size() == 1 && markers[0].first == BlockPos{32, 65, 0} && markers[0].second == "gf_test:void_mark",
              "filterDataMarkers lists the data marker on the structure_void cell");
    checkGolden(ctx, "shared_random", withMarker);
    ctx.check(withMarker != without, "the data marker's structure block draws from the shared random like Java's (" + without + ")");
    // ext::placeTemplate takes the caller's random: the same seed gives the same result
    ctx.check(run(true, nullptr) == withMarker, "the same seed places the same");
    // StructureTemplate#addToLists for the blocks whose collision shape depends on their state
    auto list = [](char const* name, Json props) { return templates::javaList(Json{{"Name", name}, {"Properties", std::move(props)}}); };
    ctx.check(list("minecraft:piston", {{"extended", "false"}, {"facing", "up"}}) == 0 && list("minecraft:sticky_piston", {{"extended", "true"}, {"facing", "up"}}) == 1,
              "a retracted piston is a full block, an extended one is not (list 0 / 1)");
    ctx.check(list("minecraft:stone_slab", {{"type", "double"}}) == 0 && list("minecraft:stone_slab", {{"type", "bottom"}}) == 1 &&
                  list("gloamingforest:aurora_slab", {{"type", "double"}}) == 0,
              "a double slab is a full block, a single one is not");
    ctx.check(list("gloamingforest:pink_castle_door", {{"vanish", "false"}, {"active", "false"}}) == 0 &&
                  list("gloamingforest:pink_castle_door", {{"vanish", "true"}, {"active", "false"}}) == 1 &&
                  list("gloamingforest:reappearing_block", {{"vanished", "false"}, {"active", "false"}}) == 0 &&
                  list("gloamingforest:reappearing_block", {{"vanished", "true"}, {"active", "false"}}) == 1,
              "a vanished castle door or reappearing block has no collision shape (list 1)");
    ctx.check(list("minecraft:chest", Json::object()) == 2 && list("minecraft:stone_bricks", Json::object()) == 0, "block entities in list 2, full blocks in list 0");
});

namespace {

/// StructureTemplate#addToLists of Minecraft 1.21.1's block states (JavaListsGolden.inc).
struct JavaListEntry {
    char const* id;
    char const* properties;
    int         list;
};
JavaListEntry const kJavaLists[] = {
#include "JavaListsGolden.inc"
};

} // namespace

GF_REGISTER_SELFTEST("structure.java_lists", [](Context& ctx) {
    // StructureTemplate#addToLists of every Minecraft 1.21.1 block state, from the game itself
    // (tests/structure/oracle/TemplateOracle.java "lists" runs the server jar's own method over every
    // block's default state and every state whose list differs from it, into JavaListsGolden.inc):
    // the kit's templates::javaList classifies each vanilla state as the game does
    auto const& table = kJavaLists;
    int         bad   = 0;
    std::string mismatches;
    for (auto const& e : table) {
        int got = templates::javaList(Json{{"Name", e.id}, {"Properties", Json::parse(e.properties)}});
        if (got == e.list) continue;
        if (bad++ < 16) mismatches += std::string(e.id) + e.properties + ": " + std::to_string(got) + " (Java " + std::to_string(e.list) + "); ";
    }
    ctx.check(std::size(table) > 1000, "the table holds every 1.21.1 block (" + std::to_string(std::size(table)) + " states)");
    ctx.check(bad == 0, std::to_string(bad) + " vanilla states in another list than Java's: " + mismatches);
});

// ---- upstream marker handlers and jigsaw pieces ----------------------------------------------------------------------------

namespace {

void registerJigsawFixtures() {
    static bool done = false;
    if (done) return;
    done                  = true;
    std::vector<Json> pal = {state("minecraft:air"), state("minecraft:stone_bricks"), state("minecraft:oak_planks"), state("minecraft:glass")};
    // the start: a 5x3x5 hall with a DOWN jigsaw (bottom center) and doors north, east and west
    auto start        = makeTemplate("gf_test:structure/jig/start", pal,
                                     {{"11111", "12221", "12221", "12221", "11111"}, {"1...1", ".....", ".....", ".....", "1...1"}, {"11111", "11111", "11111", "11111", "11111"}});
    start["jigsaw"]   = Json::array({
        Json{{"pos", {2, 0, 2}}, {"front", "down"}, {"top", "north"}, {"name", "gf_test:structure/jig/start"}, {"pool", "minecraft:empty"}},
        Json{{"pos", {2, 1, 0}}, {"front", "north"}, {"pool", "gf_test:structure/jig/rooms"}, {"name", "gf_test:door"}, {"target", "gf_test:door"}},
        Json{{"pos", {0, 1, 2}}, {"front", "west"}, {"pool", "gf_test:structure/jig/rooms"}, {"name", "gf_test:door"}, {"target", "gf_test:door"}, {"priority", 1}},
        Json{{"pos", {4, 1, 2}}, {"front", "east"}, {"pool", "gf_test:structure/jig/rooms"}, {"name", "gf_test:door"}, {"target", "gf_test:door"}},
    });
    start["markers"]  = Json::array({Json{{"pos", {2, 1, 2}}, {"marker", "data"}, {"data", {{"metadata", "seat@north|lamp"}}}}});
    auto roomA        = makeTemplate("gf_test:structure/jig/room_a", pal,
                                     {{"111", "131", "111"}, {"1.1", "...", "1.1"}, {"111", "111", "111"}});
    roomA["jigsaw"]   = Json::array({Json{{"pos", {1, 1, 2}}, {"front", "south"}, {"pool", "gf_test:structure/jig/rooms"}, {"name", "gf_test:door"},
                                          {"target", "gf_test:door"}},
                                     Json{{"pos", {1, 1, 0}}, {"front", "north"}, {"pool", "gf_test:structure/jig/rooms"}, {"name", "gf_test:door"},
                                          {"target", "gf_test:door"}}});
    auto roomB        = makeTemplate("gf_test:structure/jig/room_b", pal, {{"2222", "2222", "2222"}, {"2..2", "....", "2..2"}, {"3333", "3333", "3333"}});
    roomB["jigsaw"]   = Json::array({Json{{"pos", {0, 1, 1}}, {"front", "west"}, {"pool", "gf_test:structure/jig/rooms"}, {"name", "gf_test:door"},
                                          {"target", "gf_test:door"}}});
    for (auto const* doc : {&start, &roomA, &roomB}) templates::add(*doc);
    // an upstream marker handler list in the shape of camp_marker_handlers, with vanilla blocks
    Json handlers = Json::array({Json{
        {"type", "gloamingforest:rotation"},
        {"rotated",
         {{"type", "gloamingforest:handler_switch"},
          {"element_processor",
           {{"seat",
             {{"type", "gloamingforest:block_placement"},
              {"block_state",
               {{"type", "minecraft:weighted_state_provider"},
                {"entries", Json::array({Json{{"data", state("minecraft:oak_stairs", {{"facing", "north"}})}, {"weight", 1}},
                                         Json{{"data", state("minecraft:air")}, {"weight", 1}}})}}}}},
            {"lamp", {{"type", "gloamingforest:loot"}, {"loot_block", {{"type", "minecraft:simple_state_provider"}, {"state", state("minecraft:barrel", {{"facing", "up"}})}}}, {"loot_table", "gf_test:chests/fixture_chest"}}}}}}}}});
    markers::addList("gf_test:structure/handlers", handlers);
    pools::addDefinition("gf_test:structure/jig/start",
                         Json{{"gf_test:structure/jig/start", {{"weight", 1}, {"marker_handlers", "gf_test:structure/handlers"}}}});
    pools::addDefinition("gf_test:structure/jig/room_a", Json{{"gf_test:structure/jig/rooms", 3}});
    pools::addDefinition("gf_test:structure/jig/room_b",
                         Json{{"gf_test:structure/jig/rooms",
                               {{"weight", 2},
                                {"randomized_processors",
                                 Json::array({Json::array({Json{{"data", {{"processor_type", "gloamingforest:wood_swizzle"}, {"target_palette", "minecraft:oak"},
                                                                          {"replacement_palette", "minecraft:birch"}}},
                                                                {"weight", 1}},
                                                           Json{{"data", {{"processor_type", "minecraft:nop"}}}, {"weight", 1}}})})}}}});
}

std::string describe(std::vector<std::unique_ptr<Piece>> const& pieces, BlockPos const& origin) {
    std::string out;
    for (auto const& p : pieces) {
        auto j = p->save();
        out += j.value("Template", std::string{}) + "@" + std::to_string(p->bbox.minX - origin.x) + "," + std::to_string(p->bbox.minY - origin.y) + "," +
               std::to_string(p->bbox.minZ - origin.z) + "r" + std::to_string(j.value("rotation", 0)) + ";";
    }
    return out;
}

} // namespace

GF_REGISTER_SELFTEST("structure.jigsaw", [](Context& ctx) {
    registerJigsawFixtures();
    JavaRandom r1(20261102), r2(20261102);
    // the jigsaw reseed mixes in the world seed (GenerationContext#seed): fixed for the golden
    auto& seedOverride = bases::detail::jigsawSeedOverride();
    seedOverride       = 0;
    auto a             = jigsaw::assemble("gf_test:structure/jig/start", kOrigin, 4, r1);
    auto b             = jigsaw::assemble("gf_test:structure/jig/start", kOrigin, 4, r2);
    seedOverride.reset();
    ctx.require(!a.empty(), "the pool assembles");
    ctx.check(describe(a, kOrigin) == describe(b, kOrigin), "the same seed assembles the same pieces");
    ctx.check(a.size() >= 2, "rooms attach to the hall (" + std::to_string(a.size()) + " pieces)");
    for (std::size_t i = 0; i < a.size(); i++)
        for (std::size_t j = i + 1; j < a.size(); j++) ctx.check(!a[i]->bbox.intersects(a[j]->bbox), "pieces do not overlap");
    ctx.info("pieces: " + describe(a, kOrigin));
    MemoryRegion mem;
    BlockBox     all = a[0]->bbox;
    for (auto& p : a) {
        all = BlockBox::of(std::min(all.minX, p->bbox.minX), std::min(all.minY, p->bbox.minY), std::min(all.minZ, p->bbox.minZ), std::max(all.maxX, p->bbox.maxX),
                           std::max(all.maxY, p->bbox.maxY), std::max(all.maxZ, p->bbox.maxZ));
        JavaRandom pr(pure::mthGetSeed(p->bbox.minX, p->bbox.minY, p->bbox.minZ));
        p->postProcess(mem, pr, p->bbox, kOrigin);
    }
    checkGolden(ctx, "jigsaw_layout", describe(a, kOrigin));
    // the hall's data marker ran through the marker handler list (rotation + switch + block / loot)
    checkGolden(ctx, "jigsaw_blocks", mem.hash(all));
    // persistence: a saved jigsaw piece loads back identical
    auto saved  = a.back()->save();
    auto loaded = loadPiece(saved);
    ctx.check(loaded && loaded->save() == saved, "a jigsaw piece saves and loads back");
});

// ---- palettes, selectors, speleothems ----------------------------------------------------------------------------

GF_REGISTER_SELFTEST("structure.palettes_selectors", [](Context& ctx) {
    ctx.check(palettes::ids().size() == 17, "17 wood palettes (" + std::to_string(palettes::ids().size()) + ")");
    auto const* stairs = blocks::fromJava("minecraft:oak_stairs", Json{{"facing", "east"}});
    auto const* out    = palettes::swizzle("minecraft:oak", "minecraft:spruce", *stairs);
    ctx.check(out && blocks::is(*out, "minecraft:spruce_stairs") && blocks::javaProperty(*out, "facing") == std::optional<std::string>("east"),
              "oak stairs swizzle to spruce stairs, facing kept");
    JavaRandom r(1);
    ctx.check(blocks::is(palettes::apply("minecraft:birch", blocks::java("minecraft:oak_planks"), r), "minecraft:birch_planks"), "planks to birch");
    ctx.check(&palettes::apply("minecraft:birch", blocks::java("minecraft:stone"), r) == &blocks::java("minecraft:stone"), "stone stays stone");
    ctx.check(selectors::ids().size() == 6, "6 random block selectors");
    auto* stones = selectors::get("gloamingforest:stronghold_stones");
    ctx.require(stones != nullptr, "stronghold stones");
    std::map<std::string, int> seen;
    for (int i = 0; i < 400; i++) seen[blocks::id(stones->next(r, 0, 0, 0, true))]++;
    ctx.check(seen.size() == 4, "stronghold stones pick four blocks (" + std::to_string(seen.size()) + ")");
    ctx.check(stones->next(r, 0, 0, 0, false).isAir(), "insides are air");
    std::string dist;
    for (auto const& [k, v] : seen) dist += k + "=" + std::to_string(v) + " ";
    checkGolden(ctx, "stronghold_stones_400", dist);
});

GF_REGISTER_SELFTEST("structure.speleothems", [](Context& ctx) {
    ctx.check(speleothems::ids().size() == 6, "6 speleothem configs (" + std::to_string(speleothems::ids().size()) + ")");
    MemoryRegion mem;
    for (int x = -8; x <= 8; x++)
        for (int z = -8; z <= 8; z++) {
            for (int y = 80; y <= 84; y++) (void)mem.setBlock(BlockPos{x, y, z}, blocks::java("minecraft:stone"), 3);
            for (int y = 60; y <= 62; y++) (void)mem.setBlock(BlockPos{x, y, z}, blocks::java("minecraft:stone"), 3);
        }
    JavaRandom random(99);
    int        placed = 0;
    for (int i = 0; i < 6; i++) {
        placed += speleothems::place(mem, BlockPos{-6 + i * 2, 79, 0}, "gloamingforest:large_hollow_hill", random, true) ? 1 : 0;
        placed += speleothems::place(mem, BlockPos{-6 + i * 2, 63, 4}, "gloamingforest:troll_cave", random, false) ? 1 : 0;
    }
    ctx.check(placed > 0, "speleothems grow (" + std::to_string(placed) + ")");
    checkGolden(ctx, "speleothems", mem.hash(BlockBox{-8, 60, -8, 8, 84, 8}));
    auto lattice = speleothems::lattice("gloamingforest:small_hollow_hill");
    auto points  = speleothems::latticePositions(lattice, BlockBox{0, 0, 0, 15, 0, 15}, 0);
    std::string text;
    for (auto const& p : points) text += std::to_string(p.x) + "," + std::to_string(p.z) + ";";
    checkGolden(ctx, "lattice_chunk00", text);
});

// ---- shared bases ---------------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("structure.maze", [](Context& ctx) {
    MemoryRegion mem;
    JavaRandom   random(4242);
    bases::Maze  maze(6, 6, random);
    maze.wallBlockState  = &blocks::java("minecraft:stone_bricks");
    maze.rootBlockState  = &blocks::java("minecraft:cobblestone");
    maze.headBlockState  = &blocks::java("minecraft:mossy_stone_bricks");
    maze.torchBlockState = &blocks::java("minecraft:torch");
    maze.head            = 1;
    maze.roots           = 1;
    maze.oddBias         = 2;
    maze.carveRoom1(3, 3);
    maze.generateRecursiveBacktracker(0, 0, random);
    maze.add4Exits();
    ctx.check(maze.getCell(0, 0) != 0, "the start cell is carved");
    TestPiece piece(BlockBox{0, 64, 0, 20, 70, 20}, Orientation::South);
    maze.copyToStructure(mem, 1, 1, 1, piece, piece.bbox, random);
    checkGolden(ctx, "maze_6x6", mem.hash(piece.bbox));
});

GF_REGISTER_SELFTEST("structure.hollow_hill", [](Context& ctx) {
    MemoryRegion      mem;
    bases::HollowHill hill("gloamingforest:TFHill", 0, 1, 0, 0, 0, "gloamingforest:small_hollow_hill");
    ctx.check(hill.getOrientation() == std::optional<Orientation>(Orientation::South), "hills face south");
    // a stone dome with water in it, so the drain and the speleothems have work
    BlockBox box = hill.bbox;
    for (int x = box.minX; x <= box.maxX; x++)
        for (int z = box.minZ; z <= box.maxZ; z++) {
            (void)mem.setBlock(BlockPos{x, box.minY - 1, z}, blocks::java("minecraft:stone"), 3);
            (void)mem.setBlock(BlockPos{x, box.maxY + 1, z}, blocks::java("minecraft:stone"), 3);
            if ((x + z) % 7 == 0) (void)mem.setBlock(BlockPos{x, -2, z}, blocks::java("minecraft:water"), 3);
        }
    JavaRandom random(31337);
    hill.postProcess(mem, random, box, BlockPos{0, 0, 0});
    checkGolden(ctx, "hollow_hill_small", mem.hash(BlockBox{box.minX, box.minY - 1, box.minZ, box.maxX, box.maxY + 1, box.maxZ}));
    auto saved  = hill.save();
    bases::HollowHill back("gloamingforest:TFHill", 0, 1, 0, 0, 0, "x");
    back.load(saved);
    ctx.check(back.save() == saved, "a hill saves and loads back");
});

GF_REGISTER_SELFTEST("structure.tower", [](Context& ctx) {
    // the old lich tower: TowerMain grows its wings, bridges, outbuildings, beards and roofs
    JavaRandom       random(987654321);
    bases::PieceList pieces;
    auto             main = std::make_unique<bases::TowerMain>(random, 0, 0, 64, 0);
    auto*            raw  = main.get();
    pieces.push_back(std::move(main));
    raw->addChildren(*raw, pieces, random);
    std::map<std::string, int> types;
    BlockBox                   all = raw->bbox;
    for (auto const& p : pieces) {
        types[p->typeId]++;
        all = BlockBox::of(std::min(all.minX, p->bbox.minX), std::min(all.minY, p->bbox.minY), std::min(all.minZ, p->bbox.minZ), std::max(all.maxX, p->bbox.maxX),
                           std::max(all.maxY, p->bbox.maxY), std::max(all.maxZ, p->bbox.maxZ));
    }
    std::string layout;
    for (auto const& [t, n] : types) layout += t + "=" + std::to_string(n) + " ";
    ctx.check(types["gloamingforest:TFLTWin"] > 0, "wings grow from the main tower");
    ctx.check(types.size() >= 4, "wings, beards and roofs (" + layout + ")");
    checkGolden(ctx, "tower_layout", layout + std::to_string(all.minX) + "," + std::to_string(all.minZ) + "," + std::to_string(all.maxX) + "," +
                                         std::to_string(all.maxY) + "," + std::to_string(all.maxZ));
    MemoryRegion mem;
    for (auto& p : pieces) {
        JavaRandom pr(pure::mthGetSeed(p->bbox.minX, p->bbox.minY, p->bbox.minZ));
        p->postProcess(mem, pr, p->bbox, BlockPos{0, 64, 0});
    }
    checkGolden(ctx, "tower_blocks", mem.hash(all));
    // persistence of a wing
    for (auto const& p : pieces)
        if (p->typeId == "gloamingforest:TFLTWin") {
            auto&           wing = static_cast<bases::TowerWing&>(*p);
            auto            saved = wing.save();
            bases::TowerWing copy("gloamingforest:TFLTWin", 0, 0, 0, 0, 3, 6, Orientation::North);
            copy.load(saved);
            ctx.check(copy.save() == saved, "a tower wing saves and loads back");
            break;
        }
});

GF_REGISTER_SELFTEST("structure.component_helpers", [](Context& ctx) {
    MemoryRegion mem;
    TestPiece    piece(BlockBox{0, 64, 0, 9, 70, 9}, Orientation::East);
    piece.surroundBlockCardinalRotated(mem, blocks::java("minecraft:oak_stairs"), 4, 1, 4, piece.bbox);
    piece.placeTreasureAtCurrentPosition(mem, 2, 1, 2, "gloamingforest:hill_1", piece.bbox);
    piece.setSpawner(mem, 6, 1, 6, piece.bbox, "minecraft:zombie");
    BlockPos chest = piece.getWorldPos(2, 1, 2);
    ctx.check(blocks::is(mem.getBlock(chest), "minecraft:chest"), "a treasure chest stands");
    ctx.check(blocks::is(mem.getBlock(piece.getWorldPos(6, 1, 6)), "minecraft:spawner"), "a spawner stands");
    checkGolden(ctx, "component_helpers", mem.hash(piece.bbox));
    // UtilityPiece persistence
    bases::UtilityPiece u(3, BlockBox{1, 2, 3, 4, 5, 6}, false);
    auto                back = loadPiece(u.save());
    ctx.check(back && back->save() == u.save(), "a utility piece saves and loads back");
    // createChest: a default chest turns away from the wall, any other container keeps its facing,
    // and nothing is placed where a chest stands
    MemoryRegion walls;
    TestPiece    box(BlockBox{0, 64, 0, 9, 70, 9}, std::nullopt);
    (void)walls.setBlock(BlockPos{3, 65, 2}, blocks::java("minecraft:stone"), 3);
    JavaRandom r(5);
    ctx.check(box.placeChest(walls, box.bbox, r, 3, 65, 3, "", "gf_test:chests/fixture_chest"), "a chest is placed");
    ctx.check(blocks::javaProperty(walls.getBlock(BlockPos{3, 65, 3}), "facing") == std::optional<std::string>("south"),
              "a default chest faces away from the wall north of it");
    ctx.check(!box.placeChest(walls, box.bbox, r, 3, 65, 3, "minecraft:trapped_chest", "gf_test:chests/fixture_chest"),
              "no container where a chest stands");
    ctx.check(box.placeChest(walls, box.bbox, r, 5, 65, 3, "minecraft:barrel", "gf_test:chests/fixture_chest") &&
                  blocks::is(walls.getBlock(BlockPos{5, 65, 3}), "minecraft:barrel"),
              "another container block is placed as given");
    // placeSpawner: a registered block id is a boss spawner block, anything else a spawner's mob
    ctx.check(box.placeSpawner(walls, box.bbox, 7, 65, 7, "minecraft:zombie") && blocks::is(walls.getBlock(BlockPos{7, 65, 7}), "minecraft:spawner"),
              "a mob id makes a mob spawner");
    ctx.check(box.placeSpawner(walls, box.bbox, 8, 65, 7, "minecraft:bedrock") && blocks::is(walls.getBlock(BlockPos{8, 65, 7}), "minecraft:bedrock"),
              "a block id is placed as that block");
});

// ---- waterlogging -------------------------------------------------------------------------------------------------

namespace {

Json waterloggingTemplate() {
    std::vector<Json> palette{
        state("minecraft:air"),
        state("minecraft:oak_stairs", {{"facing", "north"}, {"half", "bottom"}, {"shape", "straight"}, {"waterlogged", "false"}}),
        state("minecraft:oak_stairs", {{"facing", "north"}, {"half", "bottom"}, {"shape", "straight"}, {"waterlogged", "true"}}),
        state("minecraft:stone"),
        state("minecraft:oak_slab", {{"type", "bottom"}, {"waterlogged", "false"}}),
    };
    // x: dry stairs over world water, waterlogged stairs over air, stone over water, a dry slab next
    // to world water, dry stairs next to the slab (which the slab's water reaches)
    return makeTemplate("gf_test:structure/waterlog", palette, {{"12341"}});
}

} // namespace

GF_REGISTER_SELFTEST("structure.waterlogging", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto area = co_await ctx.flatArea(5715, 8, 6);
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5715)), "test area loaded");
    auto* source = ctx.region();
    ctx.require(source != nullptr, "block source");
    std::string error;
    ctx.require(templates::add(waterloggingTemplate(), &error), "the waterlogging fixture registers " + error);
    Block const& water = blocks::java("minecraft:water");
    auto         wet   = [&](BlockSourceRegion& region, BlockPos const& p) { return liquids::isWaterSource(region.getLiquidLayer(p)); };
    std::vector<BlockPos> touched;
    // the world: water at x 0 and 2 and beside x 3 (z + 1), for both rows
    auto prepare = [&](BlockSourceRegion& region, BlockPos const& o) {
        for (auto const& p : {BlockPos{o.x, o.y, o.z}, BlockPos{o.x + 2, o.y, o.z}, BlockPos{o.x + 3, o.y, o.z + 1}}) {
            (void)region.setBlock(p, water, 2);
            touched.push_back(p);
        }
        for (int x = 0; x < 5; x++) touched.push_back(BlockPos{o.x + x, o.y, o.z});
    };
    BlockPos keep{area.minX + 1, area.minY + 1, area.minZ + 1};
    BlockPos ignore{area.minX + 1, area.minY + 1, area.minZ + 4};
    {
        BlockSourceRegion region(*source, keep);
        prepare(region, keep);
        prepare(region, ignore);
        TemplateSettings settings;
        ctx.require(templates::place("gf_test:structure/waterlog", region, keep, settings), "placed with the world's water");
        settings.processors = {liquids::kIgnoreWaterlogging};
        ctx.require(templates::place("gf_test:structure/waterlog", region, ignore, settings), "placed ignoring the world's water");
        auto at = [](BlockPos const& o, int dx) { return BlockPos{o.x + dx, o.y, o.z}; };
        // LiquidSettings.APPLY_WATERLOGGING
        ctx.check(wet(region, at(keep, 0)), "dry stairs placed into water are waterlogged");
        ctx.check(wet(region, at(keep, 1)), "waterlogged template stairs hold water");
        ctx.check(!wet(region, at(keep, 2)) && blocks::is(region.getBlock(at(keep, 2)), "minecraft:stone"), "stone replaces the water");
        ctx.check(wet(region, at(keep, 3)), "a dry slab next to world water is waterlogged");
        ctx.check(wet(region, at(keep, 4)), "dry stairs next to a waterlogged slab are waterlogged in the next pass");
        // LiquidSettings.IGNORE_WATERLOGGING
        ctx.check(!wet(region, at(ignore, 0)), "ignoring waterlogging: dry stairs replace the water");
        ctx.check(wet(region, at(ignore, 1)), "ignoring waterlogging: waterlogged template stairs still hold water");
        ctx.check(!wet(region, at(ignore, 3)) && !wet(region, at(ignore, 4)), "ignoring waterlogging: no neighbour pass");
        // code that sets WATERLOGGED (Piece::placeBlock by id, ext::setWaterlogged)
        TestPiece piece(BlockBox{area.minX, area.minY, area.minZ, area.maxX, area.maxY, area.maxZ}, std::nullopt);
        BlockPos  coded{area.minX + 7, area.minY + 1, area.minZ + 1};
        piece.placeBlock(region, "minecraft:oak_fence", Json{{"waterlogged", true}}, coded.x, coded.y, coded.z, piece.bbox);
        ctx.check(blocks::is(region.getBlock(coded), "minecraft:oak_fence") && wet(region, coded), "placeBlock with waterlogged=true holds water");
        piece.placeBlock(region, "minecraft:oak_fence", Json::object(), coded.x, coded.y, coded.z, piece.bbox);
        ctx.check(!wet(region, coded), "the default state carries no water");
        touched.push_back(coded);
        // clean up the liquid layers (the area restore puts back the blocks only)
        for (auto const& p : touched) {
            (void)region.setLiquidLayer(p, blocks::air(), 2);
            (void)region.setBlock(p, blocks::air(), 2);
        }
    }
    co_return;
});

// ---- flower pots -----------------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("structure.flower_pot", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto area = co_await ctx.flatArea(5725, 6, 4);
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5725)), "test area loaded");
    auto* source = ctx.region();
    ctx.require(source != nullptr, "block source");
    ctx.check(blocks::pottedPlant("minecraft:potted_red_mushroom") == blocks::bedrockId("minecraft:red_mushroom"), "a potted mushroom holds the mushroom");
    ctx.check(blocks::pottedPlant("minecraft:potted_azalea_bush") == blocks::bedrockId("minecraft:azalea"), "a potted azalea bush holds the azalea");
    std::string error;
    ctx.require(templates::add(makeTemplate("gf_test:structure/pot", {state("minecraft:air"), state("minecraft:potted_oak_sapling")}, {{"1"}}), &error),
                "the pot fixture registers " + error);
    BlockPos byPiece{area.minX + 1, area.minY, area.minZ + 1};
    BlockPos byTemplate{area.minX + 3, area.minY, area.minZ + 1};
    {
        BlockSourceRegion region(*source, byPiece);
        TestPiece         piece(BlockBox{area.minX, area.minY, area.minZ, area.maxX, area.maxY, area.maxZ}, std::nullopt);
        piece.placeBlock(region, "minecraft:potted_red_mushroom", Json::object(), byPiece.x, byPiece.y, byPiece.z, piece.bbox);
        ctx.require(templates::place("gf_test:structure/pot", region, byTemplate, TemplateSettings{}), "the pot template places");
    }
    co_await ctx.ticks(8); // the pot's block actor exists a tick later; queued work runs every 5 ticks
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    auto plantAt = [&](BlockPos const& p) -> std::string {
        auto* actor = source->getBlockEntity(p);
        if (!actor || actor->getType() != BlockActorType::FlowerPot) return "<no pot actor>";
        auto const* plant = static_cast<FlowerPotBlockActor&>(*actor).mPlant;
        return plant ? plant->getTypeName() : std::string("<empty>");
    };
    ctx.check(game::blockIdAt(*source, byPiece) == "minecraft:flower_pot", "the piece placed a flower pot");
    ctx.check(plantAt(byPiece) == blocks::bedrockId("minecraft:red_mushroom"), "the piece's pot holds a red mushroom (" + plantAt(byPiece) + ")");
    ctx.check(plantAt(byTemplate) == blocks::bedrockId("minecraft:oak_sapling"), "the template's pot holds an oak sapling (" + plantAt(byTemplate) + ")");
});

// ---- beds ------------------------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("structure.bed_color", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto area = co_await ctx.flatArea(5725, 6, 4);
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5725)), "test area loaded");
    auto* source = ctx.region();
    ctx.require(source != nullptr, "block source");
    // Java's 16 <colour>_bed blocks are Bedrock's one bed block (DyeColor numbering)
    ctx.check(blocks::bedColor("minecraft:white_bed") == 0 && blocks::bedColor("minecraft:light_blue_bed") == 3 &&
                  blocks::bedColor("minecraft:red_bed") == 14 && blocks::bedColor("minecraft:black_bed") == 15 && blocks::bedColor("minecraft:bed") < 0,
              "bed colours follow DyeColor");
    for (auto const* c : {"white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray", "light_gray", "cyan", "purple", "blue",
                          "brown", "green", "red", "black"})
        ctx.check(blocks::bedrockId(std::string("minecraft:") + c + "_bed") == "minecraft:bed", std::string(c) + "_bed is a Bedrock bed");
    // a red bed turned lime by a rule processor (CampStructureDefinitionGenerator's bed colours)
    Json lime{{"processor_type", "minecraft:rule"},
              {"rules", Json::array({Json{{"input_predicate", {{"predicate_type", "minecraft:block_match"}, {"block", "minecraft:red_bed"}}},
                                          {"location_predicate", {{"predicate_type", "minecraft:always_true"}}},
                                          {"output_state", {{"Name", "minecraft:lime_bed"}, {"Properties", {{"part", "foot"}, {"facing", "east"}}}}}}})}};
    std::string error;
    ctx.require(templates::add(makeTemplate("gf_test:structure/bed",
                                            {state("minecraft:air"), state("minecraft:red_bed", {{"part", "foot"}, {"facing", "east"}}),
                                             state("minecraft:red_bed", {{"part", "head"}, {"facing", "east"}})},
                                            {{"12"}}),
                               &error),
                "the bed fixture registers " + error);
    BlockPos byPiece{area.minX + 1, area.minY, area.minZ + 3};
    BlockPos plain{area.minX + 3, area.minY, area.minZ + 2};
    BlockPos processed{area.minX + 3, area.minY, area.minZ + 3};
    {
        BlockSourceRegion region(*source, byPiece);
        TestPiece         piece(BlockBox{area.minX, area.minY, area.minZ, area.maxX, area.maxY, area.maxZ}, std::nullopt);
        piece.placeBlock(region, "minecraft:light_blue_bed", Json{{"part", "foot"}, {"facing", "east"}}, byPiece.x, byPiece.y, byPiece.z, piece.bbox);
        piece.placeBlock(region, "minecraft:light_blue_bed", Json{{"part", "head"}, {"facing", "east"}}, byPiece.x + 1, byPiece.y, byPiece.z, piece.bbox);
        ctx.require(templates::place("gf_test:structure/bed", region, plain, TemplateSettings{}), "the bed template places");
        TemplateSettings settings;
        settings.processors = {lime.dump()};
        ctx.require(templates::place("gf_test:structure/bed", region, processed, settings), "the processed bed template places");
    }
    co_await ctx.ticks(8); // the bed's block actor exists a tick later; queued work runs every 5 ticks
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    auto colorAt = [&](BlockPos const& p) -> int {
        auto* actor = source->getBlockEntity(p);
        if (!actor || actor->getType() != BlockActorType::Bed) return -1;
        return static_cast<BedBlockActor&>(*actor).mDyeColor;
    };
    ctx.check(game::blockIdAt(*source, byPiece) == "minecraft:bed", "the piece placed a bed");
    ctx.check(colorAt(byPiece) == 3, "the piece's bed is light blue (" + std::to_string(colorAt(byPiece)) + ")");
    ctx.check(colorAt(plain) == 14, "the template's bed is red (" + std::to_string(colorAt(plain)) + ")");
    ctx.check(colorAt(processed) == 5, "the processed bed is lime (" + std::to_string(colorAt(processed)) + ")");
});

// ---- paintings -------------------------------------------------------------------------------------------------------

namespace {

std::vector<Actor*> paintingsIn(BlockSource& source, BlockBox const& box) {
    AABB range{Vec3{static_cast<float>(box.minX), static_cast<float>(box.minY), static_cast<float>(box.minZ)},
               Vec3{static_cast<float>(box.maxX + 1), static_cast<float>(box.maxY + 1), static_cast<float>(box.maxZ + 1)}};
    std::vector<Actor*> out;
    for (auto* a : source.getEntities(range, 0.0f, ActorType::Painting))
        if (a && a->isType(ActorType::Painting)) out.push_back(a);
    return out;
}

std::vector<Actor*> itemsIn(BlockSource& source, BlockBox const& box, std::string const& itemId) {
    AABB range{Vec3{static_cast<float>(box.minX), static_cast<float>(box.minY), static_cast<float>(box.minZ)},
               Vec3{static_cast<float>(box.maxX + 1), static_cast<float>(box.maxY + 1), static_cast<float>(box.maxZ + 1)}};
    std::vector<Actor*> out;
    for (auto* a : source.getEntities(range, 0.0f, ActorType::ItemEntity))
        if (a && a->isType(ActorType::ItemEntity) && static_cast<ItemActor*>(a)->mItem->getTypeName() == itemId) out.push_back(a);
    return out;
}

std::string boxText(double const* b) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "[%.4f %.4f %.4f .. %.4f %.4f %.4f]", b[0], b[1], b[2], b[3], b[4], b[5]);
    return buf;
}

/// The box of a painting is upstream's box for its block and facing: the same center and the same width
/// and height (Bedrock insets a painting's box by 1/32 at each edge).
bool sameBox(double const* actual, double const* tf, Orientation facing) {
    for (int i = 0; i < 3; i++)
        if (std::abs((actual[i] + actual[i + 3]) / 2 - (tf[i] + tf[i + 3]) / 2) > 0.05) return false;
    bool alongX = facing == Orientation::East || facing == Orientation::West;
    int  wide   = alongX ? 2 : 0; // the axis the painting's width lies along
    return std::abs((actual[wide + 3] - actual[wide]) - (tf[wide + 3] - tf[wide])) < 0.07 && std::abs((actual[4] - actual[1]) - (tf[4] - tf[1])) < 0.07;
}

/// A painting template: a 3 x 3 stone wall at z 0, air in front at z 1, a 2 x 2 painting (baroque)
/// covering block (0, 0, 1) and facing south (block_pos names the block).
std::string const& paintingTemplateId() {
    static std::string const id = [] {
        auto doc = makeTemplate("gf_test:structure/painting_wall", {state("minecraft:air"), state("minecraft:stone")},
                                {{"111", "000"}, {"111", "000"}, {"111", "000"}});
        doc["entities"] = Json::array({Json{{"pos", {0, 0, 1}},
                                            {"id", "minecraft:painting"},
                                            {"data", {{"block_pos", {0, 0, 1}}, {"facing", "south"}, {"variant", "minecraft:baroque"}}}}});
        std::string error;
        templates::add(doc, &error);
        return std::string("gf_test:structure/painting_wall");
    }();
    return id;
}

} // namespace

GF_REGISTER_SELFTEST("structure.paintings", [](Context& ctx) -> ll::coro::CoroTask<> {
    // upstream's spot check on a scratch region: blocks in the painting's box refuse it
    {
        MemoryRegion mem;
        auto         kebab = paintings::byId("minecraft:kebab");
        ctx.require(kebab.has_value(), "kebab is a painting variant");
        double box[6];
        paintings::boundingBox(BlockPos{0, 64, 0}, Orientation::South, *kebab, box);
        ctx.check(box[0] == 0 && box[3] == 1 && box[1] == 64 && box[4] == 65 && box[2] == 0 && box[5] == 0.0625,
                  "Painting#calculateBoundingBox for a 1x1 facing south");
        auto wide = paintings::byId("minecraft:fighters");
        paintings::boundingBox(BlockPos{0, 64, 0}, Orientation::East, *wide, box);
        // facing east: half a block to the counter-clockwise side (north) for the even width
        ctx.check(box[0] == 0 && box[3] == 0.0625 && box[2] == -2 && box[5] == 2 && box[1] == 64 && box[4] == 66,
                  "a 4x2 painting facing east spans z -2..2, y 64..66");
        ctx.check(paintings::tryHang(mem, BlockPos{0, 64, 0}, Orientation::South, kebab), "an empty spot takes a painting");
        (void)mem.setBlock(BlockPos{0, 64, 0}, blocks::java("minecraft:stone"), 3);
        ctx.check(!paintings::tryHang(mem, BlockPos{0, 64, 0}, Orientation::South, kebab), "a block in the painting's box refuses it");
        ctx.check(!paintings::tryHang(mem, BlockPos{0, 64, 0}, Orientation::South, std::nullopt), "no variant: no painting");
        auto placeable = paintings::tag("minecraft:placeable");
        ctx.check(placeable.size() == 46 && placeable.front().id == "minecraft:kebab", "minecraft:placeable in the tag's order (" +
                                                                                           std::to_string(placeable.size()) + ")");
        ctx.check(paintings::ofSizeOrSmaller("gloamingforest:tower_paintings", 1, 1).size() == 8, "eight 1x1 tower paintings");
        // a painting entity of a template is placed on its block: pos as a center (Java's saved
        // entity position) without block_pos is refused, block_pos is the block
        auto bad = makeTemplate("gf_test:structure/bad_painting", {state("minecraft:air")}, {{"..", ".."}, {"..", ".."}});
        bad["entities"] = Json::array({Json{{"pos", {1.0, 1.0, 0.96875}}, {"id", "minecraft:painting"}, {"data", {{"facing", "north"}, {"variant", "minecraft:baroque"}}}}});
        ctx.check(!templates::validate(bad).empty(), "a painting at its center position without block_pos is refused");
        bad["entities"][0]["data"]["block_pos"] = Json::array({0, 0, 1});
        ctx.check(templates::validate(bad).empty(), "with block_pos the painting entry is valid");
    }
    // in the world: a wall, a painting in front of it, then the marker handler next to it
    auto area = co_await ctx.flatArea(5735, 10, 6);
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5735)), "test area loaded");
    auto* source = ctx.region();
    ctx.require(source != nullptr, "block source");
    BlockBox wallBox{area.minX + 1, area.minY, area.minZ + 1, area.minX + 8, area.minY + 3, area.minZ + 1};
    BlockPos front{area.minX + 2, area.minY + 1, area.minZ + 2};
    BlockPos noWall{area.minX + 2, area.minY + 1, area.minZ + 5};
    // the 2 x 2 template painting, rotated: the wall (template z 0) ends up at x = origin.x, the
    // painting's block (0, 0, 1) at origin + (-1, 0, 0), facing west
    BlockPos origin{area.minX + 6, area.minY, area.minZ + 6};
    BlockPos templatePainting{origin.x - 1, origin.y, origin.z};
    auto     kebabVariant   = *paintings::byId("minecraft:kebab");
    auto     baroqueVariant = *paintings::byId("minecraft:baroque");
    // which block Bedrock's placement position is: the box the game gives a painting placed from
    // the covered block and from the wall block, against upstream's box for the covered block
    {
        double tf[6];
        paintings::boundingBox(front, Orientation::South, kebabVariant, tf);
        auto covered = blockentities::paintingBoxFor(front, Orientation::South, "kebab");
        auto wall    = blockentities::paintingBoxFor(BlockPos{front.x, front.y, front.z - 1}, Orientation::South, "kebab");
        ctx.info("upstream box " + boxText(tf) + "; placed from the covered block " + (covered ? boxText(covered->data()) : std::string("-")) +
                 ", from the wall " + (wall ? boxText(wall->data()) : std::string("-")));
        auto used = blockentities::paintingBoxFor(blockentities::paintingPlacement(front, Orientation::South), Orientation::South, "kebab");
        ctx.check(used && sameBox(used->data(), tf, Orientation::South), "the placement position F7 gives the game yields upstream's box");
    }
    int before = 0;
    {
        BlockSourceRegion region(*source, BlockPos{area.minX, area.minY, area.minZ});
        for (int x = wallBox.minX; x <= wallBox.maxX; x++)
            for (int y = wallBox.minY; y <= wallBox.maxY; y++) (void)region.setBlock(BlockPos{x, y, wallBox.minZ}, blocks::java("minecraft:stone"), 2);
        before = static_cast<int>(paintingsIn(*source, area).size());
        ctx.check(paintings::tryHang(region, front, Orientation::South, kebabVariant), "a painting hangs on the wall");
        // upstream's check reads ProtoChunk entities only: in a live world it does not see the painting
        // that hangs there (the game refuses the overlapping one, which drops as an item)
        ctx.check(paintings::tryHang(region, front, Orientation::South, paintings::byId("minecraft:aztec")),
                  "upstream's spot check does not see a painting that hangs in a loaded chunk");
        ctx.check(!paintings::occupied(region, front), "nor does EntityUtil.getEntitiesInAABB in a loaded chunk");
        // upstream's check passes without a wall (upstream hangs it and it drops off as an item); the game
        // hangs nothing, so the item drops at once
        ctx.check(paintings::tryHang(region, noWall, Orientation::South, kebabVariant), "upstream's spot check ignores the wall");
        ctx.check(paintingsIn(*source, BlockBox{noWall.x, noWall.y, noWall.z, noWall.x, noWall.y, noWall.z}).empty(), "no painting hangs without a wall");
        // the camp's painting marker handler: LichBossRoom.placePainting(pos, random, 1, placeable, 1, 1, rotation.rotate(NORTH))
        JavaRandom random(77);
        Json       handler{{"type", "gloamingforest:painting"}, {"paintings", "minecraft:placeable"}};
        BlockPos   marker{area.minX + 5, area.minY + 1, area.minZ + 2};
        ctx.check(markers::handle(handler, "painting", marker, region, random, area, Rotation::Clockwise180), "the painting marker hangs a 1x1 painting");
        // a 2 x 2 painting of a template placed with a rotation hangs on the template's wall
        TemplateSettings settings;
        settings.rotation = Rotation::Clockwise90;
        ctx.check(templates::place(paintingTemplateId(), region, origin, settings), "the painting template places");
    }
    co_await ctx.ticks(2);
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    auto hung = paintingsIn(*source, area);
    ctx.check(static_cast<int>(hung.size()) - before == 3, "three paintings hang (" + std::to_string(hung.size() - before) + ")");
    Actor* kebab   = nullptr;
    Actor* baroque = nullptr;
    Actor* aztec   = nullptr;
    for (auto* a : hung) {
        auto const* motif = static_cast<Painting*>(a)->mMotif;
        BlockPos    at    = game::blockPosOf(a->getPosition());
        if (motif == &Motif::mKebab() && at.x == front.x && at.y == front.y) kebab = a;
        if (motif == &Motif::mBaroque()) baroque = a;
        if (motif == &Motif::mAztec()) aztec = a;
    }
    ctx.check(kebab != nullptr, "the hung painting shows the requested motive (kebab)");
    ctx.check(aztec == nullptr, "the game does not hang a painting over one that hangs there");
    auto overlapDrop = itemsIn(*source, BlockBox{front.x - 1, front.y - 1, front.z - 1, front.x + 1, front.y + 1, front.z + 1}, "minecraft:painting");
    ctx.check(!overlapDrop.empty(), "the overlapping painting dropped as a painting item");
    for (auto* a : overlapDrop) a->despawn();
    if (kebab) {
        // where it hangs: upstream's box for the requested block
        double tf[6];
        paintings::boundingBox(front, Orientation::South, kebabVariant, tf);
        AABB const& b = kebab->getAABB();
        double      actual[6]{b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z};
        ctx.check(sameBox(actual, tf, Orientation::South), "the kebab hangs where upstream hangs it: " + boxText(actual) + ", upstream " + boxText(tf));
        auto d = blockentities::describePainting(*kebab);
        ctx.check(d && d->block == front && d->facing == Orientation::South && d->variant == "minecraft:kebab", "the painting reads back as its block, facing and variant");
        // structure_util capture keeps it: the template's painting entity names the block it covers
        int      skipped = 0;
        BlockBox captured{front.x - 1, front.y - 1, front.z - 1, front.x + 1, front.y + 1, front.z};
        auto     doc     = templates::capture(*source, captured, "gf_test:structure/captured", skipped);
        Json     entry;
        for (auto const& e : doc.value("entities", Json::array()))
            if (e.value("id", std::string{}) == "minecraft:painting") entry = e;
        ctx.check(entry.is_object() && entry["data"].value("block_pos", Json()) == Json::array({1, 1, 1}) &&
                      entry["data"].value("facing", std::string{}) == "south" && entry["data"].value("variant", std::string{}) == "minecraft:kebab",
                  "capture keeps the painting by its block, facing and variant (" + entry.dump() + ")");
        ctx.check(templates::validate(doc).empty(), "the captured template is valid");
    }
    ctx.check(baroque != nullptr, "the template's 2 x 2 painting hangs (baroque)");
    if (baroque) {
        double tf[6];
        paintings::boundingBox(templatePainting, Orientation::West, baroqueVariant, tf);
        AABB const& b = baroque->getAABB();
        double      actual[6]{b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z};
        ctx.check(sameBox(actual, tf, Orientation::West), "the rotated template painting covers its block, facing west: " + boxText(actual) + ", upstream " + boxText(tf));
    }
    auto dropped = itemsIn(*source, BlockBox{noWall.x - 1, noWall.y - 1, noWall.z - 1, noWall.x + 1, noWall.y + 1, noWall.z + 1}, "minecraft:painting");
    ctx.check(!dropped.empty(), "the painting the game refused dropped as a painting item");
    for (auto* a : dropped) a->despawn();
    for (auto* a : hung) a->despawn();
});

// ---- the kit's paintings keep Java's survival rules -------------------------------------------------------------------

GF_REGISTER_SELFTEST("structure.painting_rules", [](Context& ctx) -> ll::coro::CoroTask<> {
    // A painting the kit hung stays while Java's HangingEntity#survives holds (the game asks the
    // hooked HangingActor::wouldSurvive every few seconds): no block that stops movement in its box,
    // solid blocks (or diodes) behind it, no other hanging entity in its box. The game's own rule
    // is logged next to Java's for every case.
    auto area = co_await ctx.flatArea(5731, 4, 4);
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5731)), "test area loaded");
    auto* source = ctx.region();
    ctx.require(source != nullptr, "block source");
    BlockPos covered{area.minX + 1, area.minY, area.minZ + 1};
    BlockPos wall{covered.x, covered.y, covered.z - 1};
    for (int x = area.minX; x <= area.maxX; x++)
        for (int y = area.minY; y <= area.minY + 1; y++) (void)game::setBlock(*source, BlockPos{x, y, wall.z}, blocks::java("minecraft:stone"));
    {
        BlockSourceRegion region(*source, covered);
        ctx.require(paintings::tryHang(region, covered, Orientation::South, paintings::byId("minecraft:kebab")), "upstream's spot check takes the spot");
    }
    co_await ctx.ticks(1);
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    auto hung = paintingsIn(*source, BlockBox{covered.x, covered.y, covered.z, covered.x, covered.y, covered.z});
    ctx.require(hung.size() == 1, "the painting hangs");
    auto*    painting  = static_cast<HangingActor*>(hung.front());
    BlockPos placement = blockentities::paintingPlacement(covered, Orientation::South);
    ctx.check(painting->hasTag("gf_structure_painting"), "the kit's painting is marked for Java's survival rules");
    struct Case {
        char const* name;
        char const* wallBlock;  ///< the block behind the painting
        char const* frontBlock; ///< the block in the painting's cell
        bool        item;       ///< an item lies against the wall in the painting's cell
        bool        java;       ///< HangingEntity#survives
    };
    for (auto const& c : {Case{"stone behind", "minecraft:stone", "minecraft:air", false, true},
                          Case{"a torch in front", "minecraft:stone", "minecraft:torch", false, true},
                          Case{"a glass pane behind", "minecraft:glass_pane", "minecraft:air", false, true},
                          Case{"a bottom slab behind", "minecraft:stone_slab", "minecraft:air", false, true},
                          Case{"an oak fence behind", "minecraft:oak_fence", "minecraft:air", false, true},
                          Case{"an item in front", "minecraft:stone", "minecraft:air", true, true},
                          Case{"a carpet behind", "minecraft:white_carpet", "minecraft:air", false, false},
                          Case{"nothing behind", "minecraft:air", "minecraft:air", false, false},
                          Case{"a stone block in front", "minecraft:stone", "minecraft:stone", false, false}}) {
        source = ctx.region();
        ctx.require(source != nullptr, "block source");
        (void)game::setBlock(*source, wall, blocks::java(c.wallBlock));
        (void)game::setBlock(*source, covered, blocks::java(c.frontBlock));
        Actor* item = nullptr;
        if (c.item)
            if (auto stack = game::items::make("minecraft:stick", 1, 0); stack && ll::service::getLevel())
                item = ll::service::getLevel()->getSpawner().spawnItem(*source, *stack, nullptr,
                                                                      Vec3{covered.x + 0.5f, covered.y + 0.3f, covered.z + 0.05f}, 0);
        auto  java  = blockentities::javaSurvives(*painting);
        bool  kept  = painting->wouldSurvive(*source);
        bool  own   = painting->_wouldSurvive(*source, placement, false);
        bool  place = painting->_wouldSurvive(*source, placement, true);
        ctx.info(std::string(c.name) + ": Java " + (java && *java ? "keeps" : "drops") + " the painting, the game's own rule " + (own ? "keeps" : "drops") +
                 " it and " + (place ? "would" : "would not") + " let it be placed");
        ctx.check(java && *java == c.java, std::string(c.name) + ": HangingEntity#survives is " + (c.java ? "true" : "false"));
        ctx.check(kept == c.java, std::string(c.name) + ": the game's survival check of the kit's painting follows Java");
        if (item) item->despawn();
        (void)game::setBlock(*source, covered, blocks::air());
    }
    source = ctx.region();
    if (source) (void)game::setBlock(*source, wall, blocks::java("minecraft:stone"));
    painting->despawn();
});

// ---- upstream marker handlers and template markers in the world --------------------------------------------------------------

GF_REGISTER_SELFTEST("structure.markers", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto area = co_await ctx.flatArea(5750, 8, 4);
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5750)), "test area loaded");
    auto* source = ctx.region();
    ctx.require(source != nullptr, "block source");
    JavaRandom random(3);
    BlockPos   lootPos{area.minX + 1, area.minY, area.minZ + 1};
    BlockPos   rackPos{area.minX + 3, area.minY, area.minZ + 1};
    BlockPos   notRack{area.minX + 5, area.minY, area.minZ + 1};
    {
        BlockSourceRegion region(*source, lootPos);
        // gloamingforest:loot: the container, then its loot table (RandomizableContainer)
        Json loot{{"type", "gloamingforest:loot"},
                  {"loot_block", {{"type", "minecraft:simple_state_provider"}, {"state", state("minecraft:barrel", {{"facing", "up"}})}}},
                  {"loot_table", "gf_test:chests/fixture_chest"}};
        ctx.check(markers::handle(loot, "", lootPos, region, random, area, Rotation::None), "the loot marker places its container");
        ctx.check(game::blockIdAt(*source, lootPos) == "minecraft:barrel", "a barrel stands");
        ctx.check(holdsFixtureLoot(*source, lootPos), "the barrel got the loot table (rolled by the loot contract when unpacked)");
        // gloamingforest:drying_rack: only a DryingRackBlock is placed
        Json notARack{{"type", "gloamingforest:drying_rack"},
                      {"drying_rack_block", {{"type", "minecraft:simple_state_provider"}, {"state", state("minecraft:oak_planks")}}},
                      {"loot_table", "gloamingforest:camp_drying_rack"}};
        ctx.check(!markers::handle(notARack, "", notRack, region, random, area, Rotation::None) && game::blockIdAt(*source, notRack) == "minecraft:air",
                  "the drying rack marker refuses a block that is not a drying rack");
        // upstream's drying rack (F1's block): placed turned with the rotation (BlockState#rotate), then
        // DryingRackBlockEntity#fillFromLootTable keeps the last stack the table rolls
        ctx.require(blocks::isRegisteredBlock("gloamingforest:birch_drying_rack"), "upstream's drying rack is registered");
        Json rack{{"type", "gloamingforest:drying_rack"},
                  {"drying_rack_block",
                   {{"type", "minecraft:simple_state_provider"}, {"state", state("gloamingforest:birch_drying_rack", {{"facing", "north"}})}}},
                  {"loot_table", "gloamingforest:camp_drying_rack"}};
        ctx.check(markers::handle(rack, "", rackPos, region, random, area, Rotation::Clockwise90) &&
                      game::blockIdAt(*source, rackPos) == "gloamingforest:birch_drying_rack",
                  "the drying rack marker places the rack");
        auto facing = blocks::javaProperty(source->getBlock(rackPos), "facing");
        ctx.check(facing == std::optional<std::string>("east"), "the rack faces north turned clockwise: east (" + facing.value_or("none") + ")");
        // the rack's storage is its owner's (gf::inventory handler of the drying racks, U4)
        if (auto h = inventory::find(*source, rackPos); h && h->get) {
            auto stack = h->get(game::globalPos(*source, rackPos), 0);
            ctx.check(!stack.isNull(), "the rack holds the stack its loot table rolled (" + std::string(stack.isNull() ? "empty" : stack.getTypeName()) + ")");
        } else {
            ctx.info("no gf::inventory storage for gloamingforest:birch_drying_rack yet (U4): the rack's fill from its loot table is not checked");
        }
    }
});

/// ---- structure_util count_loot over a landmark start -------------------------------------------------------------------

namespace {

/// The items of the container at `pos` (0 when none).
int itemCount(BlockSource& source, BlockPos const& pos) {
    int n = 0;
    if (auto items = containerItems(source, pos))
        for (auto const& s : *items)
            if (!s.isNull()) n += s.mCount;
    return n;
}

} // namespace

GF_REGISTER_SELFTEST("structure.count_loot", [](Context& ctx) -> ll::coro::CoroTask<> {
    // CountLootCommand on a real landmark start (F6's forceStart of the open fixture landmark, which
    // protects nothing: a 7 x 7 x 7 shell and a porch in front of it): every Container block entity
    // inside a piece counts, each RandomizableContainer unpacked first (the loot contract's chests,
    // F10; hoppers, whose table the kit keeps), the other Containers as they are (a furnace)
    auto area = co_await ctx.flatArea(5790, 10, 8);
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5790)), "test area loaded");
    auto* source = ctx.region();
    ctx.require(source != nullptr, "block source");
    // the shell (x and z within 3 of the center) and the porch (z 4..6 ahead) inside the 10 x 10 area
    core::GlobalPos center{ctx.dimension(), area.minX + 6, area.minY, area.minZ + 3};
    // the forced start is removed again when the test ends (or aborts), so the test runs again in
    // the same world (F6's ScopedStart; it puts back a start of the key it replaced)
    landmark::ext::ScopedStart forced("gf_test:landmark_fixture_open", center);
    ctx.require(forced && forced->pieceBoxes.size() == 2, "the fixture landmark starts with its shell and porch");
    BlockPos chestPos{center.x, center.y + 1, center.z};
    BlockPos hopperPos{center.x + 2, center.y + 1, center.z};      // an enabled hopper
    BlockPos poweredPos{center.x - 2, center.y + 1, center.z - 2}; // a hopper powered by the block beside it
    BlockPos redstonePos{center.x - 2, center.y + 1, center.z - 1};
    BlockPos furnacePos{center.x - 2, center.y + 1, center.z + 2};
    BlockPos abovePorch{center.x, center.y + 3, center.z + 5}; // in the start's box, in no piece
    // StructureManager#getStructureAt, as the command finds it
    auto start = landmark::startAt(center.dim, chestPos);
    ctx.require(start.has_value() && start->key == "gf_test:landmark_fixture_open" && start->box.contains(abovePorch.x, abovePorch.y, abovePorch.z),
                "the start is found where the command looks");
    // StructureStart#getPieces as display_pieces, count_loot and count_template read them: F6's
    // piece type ids and saved tags, the same pieces startAt hands out in data["pieces"]
    {
        auto        pieces = landmark::ext::startPieces(*start);
        std::string types;
        for (auto const& p : pieces) types += p.type + " ";
        ctx.check(pieces.size() == 2 && pieces[0].type == "gf_test:landmark_fixture_shell" && pieces[1].type == "gf_test:landmark_fixture_porch" &&
                      pieces[0].tag.is_object() && pieces[0].tag.value("id", std::string{}) == pieces[0].type && pieces[0].box == forced->pieceBoxes[0],
                  "the start's pieces carry their type ids, boxes and saved tags (" + types + ")");
        ctx.check(start->data.contains("pieces") && start->data["pieces"].is_array() && start->data["pieces"].size() == pieces.size(),
                  "startAt hands out the same pieces' tags");
    }
    auto const* hopper   = game::blockWith("minecraft:hopper");
    auto const* redstone = game::blockWith("minecraft:redstone_block");
    auto const* furnace  = game::blockWith("minecraft:furnace");
    ctx.require(hopper && redstone && furnace, "hopper, redstone block and furnace blocks");
    (void)game::setBlock(*source, poweredPos, *hopper);
    (void)game::setBlock(*source, redstonePos, *redstone);
    (void)game::setBlock(*source, furnacePos, *furnace);
    co_await ctx.ticks(4); // block actors exist a tick later
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    auto powered = game::statesOf(source->getBlock(poweredPos));
    ctx.check(powered.contains("toggle_bit") && (powered["toggle_bit"] == "true" || powered["toggle_bit"] == "1"),
              "the redstone block powers the hopper beside it (" + powered["toggle_bit"] + ")");
    {
        BlockSourceRegion region(*source, chestPos);
        blockentities::container(region, chestPos, "minecraft:chest", Orientation::North, "gf_test:chests/fixture_chest", 1234);
        blockentities::container(region, hopperPos, "minecraft:hopper", std::nullopt, "gf_test:chests/fixture_chest", 99);
        blockentities::lootTable(region, poweredPos, "gf_test:chests/fixture_chest", 77);
        blockentities::container(region, abovePorch, "minecraft:chest", Orientation::North, "gf_test:chests/fixture_chest", 1234);
    }
    // a furnace holding an uncommon item and its (common) fuel
    auto* furnaceActor = source->getBlockEntity(furnacePos);
    ctx.require(furnaceActor && furnaceActor->getType() == BlockActorType::Furnace, "the furnace's block actor");
    {
        auto& slots = static_cast<Container&>(static_cast<FurnaceBlockActor&>(*furnaceActor));
        auto  scale = game::items::make("gloamingforest:naga_scale", 3, 0);
        auto  coal  = game::items::make("minecraft:coal", 5, 0);
        ctx.require(scale && coal, "the furnace's items");
        slots.setItem(0, *scale);
        slots.setItem(1, *coal);
    }
    co_await ctx.ticks(8); // queued work runs every 5 ticks
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    ctx.check(game::blockIdAt(*source, chestPos) == "minecraft:chest" && game::blockIdAt(*source, abovePorch) == "minecraft:chest" &&
                  game::blockIdAt(*source, hopperPos) == "minecraft:hopper" && game::blockIdAt(*source, furnacePos) == "minecraft:furnace",
              "the containers stand");
    // Java's hopper unpacks its loot table on its first tick while it is enabled; a powered one keeps it
    ctx.check(!blockentities::pendingHopperLoot(*source, hopperPos) && holdsFixtureLoot(*source, hopperPos),
              "the enabled hopper holds its loot table's items at once");
    ctx.check(blockentities::pendingHopperLoot(*source, poweredPos).has_value(), "the powered hopper keeps its loot table");
    // show_common false first (the containers are unpacked then): only the stacks that are not common
    auto uncommonOnly = countLootIn(*source, *start, false);
    ctx.check(!blockentities::pendingHopperLoot(*source, poweredPos) && holdsFixtureLoot(*source, poweredPos),
              "count_loot unpacked the powered hopper's loot table");
    auto        counts = countLootIn(*source, *start, true);
    std::string list, uncommonList;
    for (auto const& [item, n] : counts) list += item + "=" + std::to_string(n) + " ";
    for (auto const& [item, n] : uncommonOnly) uncommonList += item + "=" + std::to_string(n) + " ";
    // the fixture table rolls common items (fixture items, sticks, diamonds): the naga scales in the
    // furnace are counted, each other item exactly when it is not common
    ctx.check(uncommonOnly.contains("gloamingforest:naga_scale") && uncommonOnly["gloamingforest:naga_scale"] == 3,
              "count_loot without common items counts the furnace's naga scales (" + uncommonList + ")");
    for (auto const& [item, n] : counts) {
        auto stack = game::items::make(item, 1, 0);
        ctx.check(stack && (notCommon(*stack) == uncommonOnly.contains(item)),
                  "count_loot without common items " + std::string(uncommonOnly.contains(item) ? "counts " : "leaves out ") + item);
    }
    int inPieces = itemCount(*source, chestPos) + itemCount(*source, hopperPos) + itemCount(*source, poweredPos) + itemCount(*source, furnacePos);
    int counted  = 0;
    for (auto const& [item, n] : counts) counted += n;
    ctx.check(counts.contains("minecraft:coal") && counts["minecraft:coal"] == 5, "the furnace's fuel counts with the common items (" + list + ")");
    ctx.check(counted > 8 && counted == inPieces,
              "count_loot counts the chest, both hoppers and the furnace in the shell (" + list + "), not the chest outside the pieces");
    ctx.check(holdsFixtureLoot(*source, abovePorch), "the chest outside the pieces still unpacks its own loot");
    // ItemStack#getRarity: upstream's UNCOMMON items (a table here), GLOAMING trophies and banner
    // patterns (F9's rarity), the fiery block's UNCOMMON item; 1.21.1's rarities (Items): banner
    // patterns, music discs, the elytra, the heavy core and the trident are not common, trial keys,
    // the ominous bottle and the globe banner pattern are; sticks are common
    for (auto const* id : {"gloamingforest:naga_scale", "gloamingforest:naga_trophy", "gloamingforest:naga_banner_pattern", "gloamingforest:fiery_block",
                           "minecraft:creeper_banner_pattern", "minecraft:mojang_banner_pattern", "minecraft:flow_banner_pattern", "minecraft:music_disc_cat",
                           "minecraft:elytra", "minecraft:heavy_core", "minecraft:trident"}) {
        auto stack = game::items::make(id, 1, 0);
        ctx.check(stack && notCommon(*stack), std::string(id) + " is not common");
    }
    for (auto const* id : {"minecraft:stick", "minecraft:trial_key", "minecraft:ominous_trial_key", "minecraft:ominous_bottle", "minecraft:globe_banner_pattern"}) {
        auto stack = game::items::make(id, 1, 0);
        ctx.check(stack && !notCommon(*stack), std::string(id) + " is common");
    }
    // an enchanted stack is never common (ItemStack#getRarity's bump, F9's)
    if (auto sword = game::items::make("minecraft:iron_sword", 1, 0)) {
        ctx.check(!notCommon(*sword), "an iron sword is common");
        gf::enchant::set(*sword, "minecraft:unbreaking", 1);
        ctx.check(notCommon(*sword), "an enchanted iron sword is not common");
    }
    co_return;
});

// ---- a hopper's loot table is unpacked on any access ----------------------------------------------------------------

GF_REGISTER_SELFTEST("structure.hopper_loot", [](Context& ctx) -> ll::coro::CoroTask<> {
    // Java's hopper is a RandomizableContainer: its loot table is unpacked on the first access to its
    // container. A powered hopper (HopperBlockEntity#tryMoveItems needs ENABLED: its own ticks leave
    // the container alone) keeps the table the kit gave it until another hopper pushes into it, its
    // container is read, a comparator reads it, it is removed (HopperBlock#onRemove ->
    // Containers#dropContentsOnDestroy: the table is rolled, then the contents drop) or a hopper
    // below pulls from it (HopperBlockEntity#suckInItems -> tryTakeInItemFromSlot: getItem first).
    auto area = co_await ctx.flatArea(5783, 6, 4);
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5783)), "test area loaded");
    auto* source = ctx.region();
    ctx.require(source != nullptr, "block source");
    auto const* hopper   = game::blockWith("minecraft:hopper");
    auto const* redstone = game::blockWith("minecraft:redstone_block");
    ctx.require(hopper && redstone, "hopper and redstone block");
    int      y = area.minY;
    BlockPos fed{area.minX + 2, y, area.minZ + 1};     // a hopper above pushes into it
    BlockPos peeked{area.minX + 4, y, area.minZ + 1};  // its container is read
    BlockPos read{area.minX + 2, y, area.minZ + 3};    // a comparator reads it
    BlockPos removed{area.minX + 4, y, area.minZ + 4}; // removed
    BlockPos pulled{area.minX, y + 1, area.minZ + 4};  // a hopper below pulls from it
    BlockPos feeder{fed.x, fed.y + 1, fed.z};
    BlockPos puller{pulled.x, pulled.y - 1, pulled.z};
    for (auto const& pos : {fed, peeked, read, removed, pulled}) {
        (void)game::setBlock(*source, pos, *hopper);
        (void)game::setBlock(*source, BlockPos{pos.x + 1, pos.y, pos.z}, *redstone); // powers it
    }
    co_await ctx.ticks(4); // block actors exist a tick later
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    {
        BlockSourceRegion region(*source, fed);
        std::int64_t      seed = 41;
        for (auto const& pos : {fed, peeked, read, removed, pulled}) blockentities::lootTable(region, pos, "gf_test:chests/fixture_chest", seed++);
    }
    co_await ctx.ticks(10); // the powered hoppers tick: their own ticks unpack nothing
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    bool allPending = true;
    for (auto const& pos : {fed, peeked, read, removed, pulled}) allPending = allPending && blockentities::pendingHopperLoot(*source, pos).has_value();
    ctx.require(allPending, "the powered hoppers keep their loot tables through their own ticks");
    ctx.check(blockentities::pendingHoppers() >= 5, "the kit's hooks watch the pending hoppers (" + std::to_string(blockentities::pendingHoppers()) + ")");
    int  dim         = static_cast<int>(source->getDimensionId());
    auto listedNow   = [dim](BlockPos const& pos) {
        // the stored list (gf:structure:hoppers, [dim, x, y, z] each)
        auto stored = game::db::get("gf:structure:hoppers");
        if (!stored || !stored->is_array()) return false;
        for (auto const& h : *stored)
            if (h == Json::array({dim, pos.x, pos.y, pos.z})) return true;
        return false;
    };
    bool allWatched = true;
    for (auto const& pos : {fed, peeked, read, removed, pulled}) allWatched = allWatched && structure::ext::hoppers::watched(dim, pos);
    ctx.check(allWatched, "the pending hoppers are watched by the hopper access dispatch");
    // the block data's write (autosave): the hopper list is written first
    blockdata::flush();
    bool allListed = true;
    for (auto const& pos : {fed, peeked, read, removed, pulled}) allListed = allListed && listedNow(pos);
    ctx.check(allListed, "the stored hopper list holds the pending hoppers once the block data is written");
    // a hopper above pushes a cobblestone into the powered one: the table is unpacked first
    // (HopperBlockEntity#addItem reads the target's slots), then the stone goes into a free slot
    (void)game::setBlock(*source, feeder, *hopper); // facing down, not powered (the redstone block is diagonal)
    co_await ctx.ticks(2);
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    auto* feederActor = source->getBlockEntity(feeder);
    ctx.require(feederActor && feederActor->getType() == BlockActorType::Hopper, "the feeding hopper's block actor");
    auto stone = game::items::make("minecraft:cobblestone", 1, 0);
    ctx.require(stone.has_value(), "a cobblestone");
    static_cast<Container&>(static_cast<HopperBlockActor&>(*feederActor)).setItem(0, *stone);
    co_await ctx.ticks(30);
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    auto countOf = [&](BlockPos const& pos, std::string const& id) {
        int n = 0;
        if (auto items = containerItems(*source, pos))
            for (auto const& s : *items)
                if (!s.isNull() && s.getTypeName() == id) n += s.mCount;
        return n;
    };
    // (read before the items: reading them through containerItems unpacks a pending hopper)
    bool fedUnpacked = !blockentities::pendingHopperLoot(*source, fed).has_value();
    int  stonesIn  = countOf(fed, "minecraft:cobblestone");
    int  stonesOut = countOf(feeder, "minecraft:cobblestone");
    bool loot      = countOf(fed, "gf_test:fixture_item") + countOf(fed, "minecraft:stick") > 0;
    ctx.check(fedUnpacked && loot,
              "a hopper pushing into the powered hopper unpacked its loot table (" + std::to_string(stonesIn) + " stone moved in)");
    ctx.check(stonesIn + stonesOut == 1, "the stone went into the unpacked hopper or stayed in the feeder when no slot was free");
    // a read of its container (Container#getItem: RandomizableContainerBlockEntity unpacks first)
    if (auto* actor = source->getBlockEntity(peeked); actor && actor->getType() == BlockActorType::Hopper)
        (void)static_cast<Container&>(static_cast<HopperBlockActor&>(*actor)).getItem(0);
    ctx.check(!blockentities::pendingHopperLoot(*source, peeked) && holdsFixtureLoot(*source, peeked), "reading the powered hopper's container unpacked it");
    ctx.check(!structure::ext::hoppers::watched(dim, peeked), "an unpacked hopper is no longer watched");
    // an unpacked hopper stays in the stored list for one more block data write (a crash between the
    // list's write and the block data's never leaves a table the list misses), then leaves it
    blockdata::flush();
    bool listedOnce = listedNow(peeked);
    blockdata::flush();
    ctx.check(listedOnce && !listedNow(peeked) && listedNow(read),
              "an unpacked hopper leaves the stored list one block data write later; a pending one stays");
    // a comparator's reading (HopperBlock#getComparatorSignal reads every slot)
    Block const& readBlock = source->getBlock(read);
    int          signal    = readBlock.getBlockType().getComparatorSignal(*source, read, readBlock, 2);
    ctx.check(!blockentities::pendingHopperLoot(*source, read) && signal > 0 && holdsFixtureLoot(*source, read),
              "a comparator reading the powered hopper unpacked it (signal " + std::to_string(signal) + ")");
    // its removal (not by a player: an explosion, a command): the table is rolled and the contents drop
    BlockBox around{removed.x - 2, removed.y - 1, removed.z - 2, removed.x + 2, removed.y + 2, removed.z + 2};
    (void)game::setBlock(*source, removed, blocks::air());
    co_await ctx.ticks(2);
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    auto fixtureDrops = itemsIn(*source, around, "gf_test:fixture_item");
    auto stickDrops   = itemsIn(*source, around, "minecraft:stick");
    ctx.check(!fixtureDrops.empty() || !stickDrops.empty(),
              "the removed powered hopper dropped its loot table's items (" + std::to_string(fixtureDrops.size() + stickDrops.size()) + " item entities)");
    for (auto* a : fixtureDrops) a->despawn();
    for (auto* a : stickDrops) a->despawn();
    for (auto* a : itemsIn(*source, around, "minecraft:diamond")) a->despawn();
    // an enabled hopper below the powered one pulls from it (HopperBlockEntity#suckInItems ->
    // tryTakeInItemFromSlot reads the source's slot: the table is unpacked before anything moves)
    ctx.check(blockentities::pendingHopperLoot(*source, pulled).has_value(), "the hopper to pull from is still pending");
    (void)game::setBlock(*source, puller, *hopper); // facing down, not powered (the redstone block is diagonal)
    co_await ctx.ticks(40);
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    bool pulledUnpacked = !blockentities::pendingHopperLoot(*source, pulled).has_value();
    // what the table rolls (LootTable#fill rolls it with the same random before spreading it)
    loot::LootContext lc;
    lc.region = source;
    lc.origin = Vec3{static_cast<float>(pulled.x) + 0.5f, static_cast<float>(pulled.y) + 0.5f, static_cast<float>(pulled.z) + 0.5f};
    lc.seed   = 45;
    std::map<std::string, int> rolled, both;
    for (auto const& s : loot::roll("gf_test:chests/fixture_chest", lc))
        if (!s.isNull()) rolled[s.getTypeName()] += s.mCount;
    int pulledOut = 0;
    for (auto const& pos : {pulled, puller})
        if (auto items = containerItems(*source, pos))
            for (auto const& s : *items) {
                if (s.isNull()) continue;
                both[s.getTypeName()] += s.mCount;
                if (pos == puller) pulledOut += s.mCount;
            }
    auto describe = [](std::map<std::string, int> const& m) {
        std::string out;
        for (auto const& [id, n] : m) out += (out.empty() ? "" : ", ") + id + " x" + std::to_string(n);
        return out.empty() ? std::string("nothing") : out;
    };
    ctx.check(pulledUnpacked && pulledOut > 0 && both == rolled,
              "a hopper below pulled the unpacked loot out of the powered one (" + std::to_string(pulledOut) + " items pulled; the two hold " + describe(both) +
                  ", the table rolls " + describe(rolled) + ")");
    // a table whose block is no longer a hopper (block data that outlived it: here moved onto a
    // stone block) is dropped at its first unpack, and its position no longer watched
    BlockPos    gone{area.minX + 5, y, area.minZ + 2};
    auto const* stoneBlock = game::blockWith("minecraft:stone");
    ctx.require(stoneBlock != nullptr, "stone");
    (void)game::setBlock(*source, gone, *stoneBlock);
    blockdata::put(game::globalPos(*source, gone), Json{{kBlockDataKey, {{"loot", {{"table", "gf_test:chests/fixture_chest"}, {"seed", 7}}}}}});
    bool staleWatched = structure::ext::hoppers::watched(dim, gone);
    bool unpacked     = blockentities::unpackHopper(*source, gone);
    ctx.check(staleWatched && !unpacked && !blockentities::pendingHopperLoot(*source, gone) && !structure::ext::hoppers::watched(dim, gone),
              "a table on a block that is no longer a hopper is dropped and its position no longer watched");
    bool leftPending = false;
    for (auto const& pos : {fed, peeked, read, removed, pulled}) leftPending = leftPending || blockentities::pendingHopperLoot(*source, pos).has_value();
    ctx.check(!leftPending, "no hopper of the test is left pending (" + std::to_string(blockentities::pendingHoppers()) + " watched)");
    // empty the hoppers (the restored area removes them: nothing drops)
    for (auto const& pos : {fed, peeked, read, feeder, pulled, puller})
        if (auto* actor = source->getBlockEntity(pos); actor && actor->getType() == BlockActorType::Hopper) {
            auto& container = static_cast<Container&>(static_cast<HopperBlockActor&>(*actor));
            for (int slot = 0; slot < container.getContainerSize(); slot++) container.setItem(slot, ItemStack::EMPTY_ITEM());
        }
});

// ---- upstream's sinister spawner in a template keeps its block ---------------------------------------------------------------

GF_REGISTER_SELFTEST("structure.sinister_spawner", [](Context& ctx) -> ll::coro::CoroTask<> {
    // SpawnerProcessor handles upstream's sinister spawner as it handles the vanilla one; the template's
    // block stays a sinister spawner and its block entity (F8's) loads the processed spawner data
    // (BaseSpawner#load); the kit keeps what F8's spawner does not carry (generic.scale, custom
    // spawn rules) with the block for its spawns.
    std::string const blockId = "gloamingforest:sinister_spawner";
    ctx.require(blocks::isRegisteredBlock(blockId), "upstream's sinister spawner is registered");
    float const scale = 0.8f / 1.4f; // entity_width_max 0.8 over the spider's width
    // a second sinister spawner whose template data has what BaseSpawner#load and
    // SinisterSpawnerLogic#load read besides SpawnData: SpawnPotentials, EntityScanRange and
    // ParticleOptions (SpawnerProcessor leaves them as they are)
    Json doc = makeTemplate("gf_test:structure/sinister", {state("minecraft:air"), state(blockId), state("minecraft:spawner")}, {{"121"}});
    doc["markers"] = Json::array({Json{
        {"pos", {2, 0, 0}},
        {"marker", "spawner"},
        {"data",
         {{"SpawnPotentials", Json::array({Json{{"weight", 2}, {"data", {{"entity", {{"id", "minecraft:zombie"}}}}}},
                                           Json{{"weight", 1}, {"data", {{"entity", {{"id", "minecraft:skeleton"}}}}}}})},
          {"EntityScanRange", 7},
          {"ParticleOptions", Json::array({Json{{"type", "minecraft:dust"}, {"color", {1.0, 0.0, 0.0}}, {"scale", 1.0}}, Json{{"type", "minecraft:flame"}}})}}}}});
    std::string error;
    ctx.require(templates::add(doc, &error), "the sinister spawner template registers " + error);
    auto area = co_await ctx.flatArea(5745, 4, 4);
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5745)), "test area loaded");
    auto* source = ctx.region();
    ctx.require(source != nullptr, "block source");
    BlockPos origin{area.minX + 1, area.minY, area.minZ + 1};
    BlockPos sinister = origin, vanilla{origin.x + 1, origin.y, origin.z}, withData{origin.x + 2, origin.y, origin.z};
    {
        BlockSourceRegion region(*source, origin);
        TemplateSettings  settings;
        settings.processors = {ext::compileSpawnerProcessor(4, 0.8f, {{"minecraft:spider", 1}})};
        ctx.require(templates::place("gf_test:structure/sinister", region, origin, settings), "the template places");
    }
    // (read at once: once a player is near, F8 ticks the spawner and its delay counts down)
    auto loaded     = boss::ext::sinisterSpawner(*source, sinister);
    auto loadedData = boss::ext::sinisterSpawner(*source, withData);
    co_await ctx.ticks(8); // block actors exist a tick later; queued work runs every 5 ticks
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    auto standing = game::blockIdAt(*source, sinister);
    ctx.check(standing == blockId, "the template's spawner block stays " + blockId + " (" + standing + ")");
    auto* actor = source->getBlockEntity(sinister);
    ctx.check(!actor || actor->getType() != BlockActorType::MobSpawner, "no vanilla spawner block entity at the sinister spawner");
    // the block entity (F8) loaded the data: SpawnData spider (the only potential), SpawnRange 4,
    // Delay round(200 * 0.25), the other fields BaseSpawner's
    ctx.check(loaded.has_value(), "the sinister spawner has its block entity");
    if (loaded) {
        bool onlySpider = loaded->potentials.size() == 1 && loaded->potentials[0] == std::pair<std::string, int>{"minecraft:spider", 1};
        ctx.check(loaded->nextEntity == "minecraft:spider" && onlySpider, "its block entity spawns spiders (" + loaded->nextEntity + ")");
        ctx.check(loaded->spawnRange == 4 && loaded->spawnDelay == 50, "its block entity's range 4 and delay round(200 * 0.25) (" +
                                                                           std::to_string(loaded->spawnRange) + ", " + std::to_string(loaded->spawnDelay) + ")");
        ctx.check(loaded->minSpawnDelay == 200 && loaded->maxSpawnDelay == 800 && loaded->spawnCount == 4 && loaded->maxNearbyEntities == 6 &&
                      loaded->requiredPlayerRange == 16,
                  "the fields the data leaves out keep BaseSpawner's values");
        // SinisterSpawnerLogic#load: no EntityScanRange: the SpawnRange; no ParticleOptions: none
        ctx.check(loaded->entityScanRange == 4 && loaded->particles.empty(),
                  "without EntityScanRange it scans its SpawnRange, without ParticleOptions it shows none (" + std::to_string(loaded->entityScanRange) + ")");
    }
    ctx.check(loadedData.has_value(), "the second sinister spawner has its block entity");
    if (loadedData) {
        std::vector<std::pair<std::string, int>> const potentials{{"minecraft:zombie", 2}, {"minecraft:skeleton", 1}};
        ctx.check(loadedData->nextEntity == "minecraft:spider" && loadedData->potentials == potentials,
                  "SpawnPotentials load weighted and SpawnData stays the processor's (" + loadedData->nextEntity + ", " +
                      std::to_string(loadedData->potentials.size()) + " potentials)");
        ctx.check(loadedData->entityScanRange == 7, "EntityScanRange loads (" + std::to_string(loadedData->entityScanRange) + ")");
        ctx.check(loadedData->particles == std::vector<std::string>{"minecraft:dust", "minecraft:flame"} &&
                      loadedData->particleParams.is_object() && loadedData->particleParams.value("minecraft:dust", Json())["color"] == Json({1.0, 0.0, 0.0}),
                  "ParticleOptions load with the dust's colour (" + loadedData->particleParams.dump() + ")");
    }
    auto record = blockentities::spawnerRecord(*source, sinister);
    ctx.check(record.has_value(), "the kit keeps the sinister spawner's SpawnData extras");
    if (record) {
        ctx.check(record->value("entity", std::string{}) == "minecraft:spider", "for its mob (" + record->dump() + ")");
        ctx.check(std::abs(record->value("scale", 0.0f) - scale) < 1e-4f, "its generic.scale for entity_width_max 0.8");
        ctx.check(record->contains("spawn_rules") && (*record)["spawn_rules"] == Json{{"block_light", {0, 7}}, {"sky_light", {0, 15}}}, "its custom spawn rules");
    }
    // the kit's spawn events find the sinister spawner for its spiders (scale, light rules): four
    // blocks up is inside its spawn box (SpawnRange on every axis) and out of the vanilla spawner's
    // (one block up or down)
    Vec3 spot{static_cast<float>(sinister.x) + 0.5f, static_cast<float>(sinister.y + 4), static_cast<float>(sinister.z) + 0.5f};
    auto firing = spawners::firingSpawner(*source, spot, "minecraft:spider");
    ctx.check(firing.has_value() && *firing == sinister,
              "a spider spawning in its box is the sinister spawner's (" + (firing ? std::to_string(firing->x) + " " + std::to_string(firing->y) : std::string("none")) + ")");
    // the vanilla spawner next to it gets the same processor's data in its block entity
    ctx.check(game::blockIdAt(*source, vanilla) == "minecraft:mob_spawner", "the vanilla spawner stands");
    std::string mob;
    int         range = 0;
    if (auto* v = source->getBlockEntity(vanilla); v && v->getType() == BlockActorType::MobSpawner)
        if (auto* sp = static_cast<MobSpawnerBlockActor&>(*v).mSpawner.get()) {
            mob   = sp->mActorDefId->mCanonicalName->getString();
            range = sp->mSpawnRange;
        }
    ctx.check(mob == "minecraft:spider" && range == 4, "the vanilla spawner spawns spiders within 4 (" + mob + ", " + std::to_string(range) + ")");
    blockentities::forgetSpawnerRecord(*source, sinister);
    blockentities::forgetSpawnerRecord(*source, vanilla);
    blockentities::forgetSpawnerRecord(*source, withData);
});

GF_REGISTER_SELFTEST("structure.spawner_load", [](Context& ctx) {
    // BaseSpawner#load and SinisterSpawnerLogic#load on hand-written spawner data (a Java template
    // saves every field; the kit's templates may leave some out)
    using boss::ext::SinisterSpawner;
    auto load = [](Json data, std::string const& entity = {}) {
        SinisterSpawner s;
        s.spawnDelay = 123;
        s.particles  = {"minecraft:smoke"};
        blockentities::loadSinisterSpawnerData(s, entity, data);
        return s;
    };
    SinisterSpawner const defaults;
    // Delay = getShort("Delay"): 0 without it, the low 16 bits of a larger number
    ctx.check(load(Json::object()).spawnDelay == 0, "no Delay: the delay is 0");
    ctx.check(load(Json{{"Delay", 65535}}).spawnDelay == -1 && load(Json{{"Delay", 40}}).spawnDelay == 40, "Delay is a short");
    // MinSpawnDelay, MaxSpawnDelay and SpawnCount only together, when MinSpawnDelay is there
    auto noMin = load(Json{{"MaxSpawnDelay", 500}, {"SpawnCount", 2}});
    ctx.check(noMin.minSpawnDelay == defaults.minSpawnDelay && noMin.maxSpawnDelay == defaults.maxSpawnDelay && noMin.spawnCount == defaults.spawnCount,
              "MaxSpawnDelay and SpawnCount without MinSpawnDelay are not read");
    auto onlyMin = load(Json{{"MinSpawnDelay", 100}});
    ctx.check(onlyMin.minSpawnDelay == 100 && onlyMin.maxSpawnDelay == 0 && onlyMin.spawnCount == 0,
              "MinSpawnDelay alone reads its partners as 0 (" + std::to_string(onlyMin.maxSpawnDelay) + ", " + std::to_string(onlyMin.spawnCount) + ")");
    // MaxNearbyEntities and RequiredPlayerRange only together, when MaxNearbyEntities is there
    auto noMax = load(Json{{"RequiredPlayerRange", 8}});
    ctx.check(noMax.requiredPlayerRange == defaults.requiredPlayerRange && noMax.maxNearbyEntities == defaults.maxNearbyEntities,
              "RequiredPlayerRange without MaxNearbyEntities is not read");
    auto onlyMax = load(Json{{"MaxNearbyEntities", 3}});
    ctx.check(onlyMax.maxNearbyEntities == 3 && onlyMax.requiredPlayerRange == 0, "MaxNearbyEntities alone reads RequiredPlayerRange as 0");
    auto range = load(Json{{"SpawnRange", 6}});
    ctx.check(range.spawnRange == 6 && range.entityScanRange == 6, "SpawnRange alone, EntityScanRange follows it");
    // SpawnData: the next spawn and, without SpawnPotentials, the only potential
    auto spawnData = load(Json{{"SpawnData", {{"entity", {{"id", "minecraft:zombie"}}}}}});
    ctx.check(spawnData.nextEntity == "minecraft:zombie" && spawnData.potentials == std::vector<std::pair<std::string, int>>{{"minecraft:zombie", 1}},
              "SpawnData is the next spawn and the only potential");
    // ParticleOptions: cleared, then the data's only when the whole list decodes
    auto particles = [&](Json list) { return load(Json{{"ParticleOptions", std::move(list)}}).particles; };
    using Ids      = std::vector<std::string>;
    ctx.check(load(Json::object()).particles.empty(), "no ParticleOptions: no particles (the old ones are cleared)");
    ctx.check(particles(Json::array({Json{{"type", "minecraft:flame"}}, Json{{"type", "gloamingforest:firefly"}}})) == Ids{"minecraft:flame", "gloamingforest:firefly"},
              "a list of particle compounds loads");
    ctx.check(particles(Json::array({Json{{"type", "minecraft:flame"}}, Json{{"type", "minecraft:flame"}}})) == Ids{"minecraft:flame"},
              "a repeated particle is kept once (a set)");
    ctx.check(particles(Json::array({Json{{"type", "minecraft:flame"}}, "minecraft:smoke"})).empty(), "a bare id fails the whole list");
    ctx.check(particles(Json::array({Json{{"type", "minecraft:flame"}}, Json{{"type", "minecraft:dust"}}})).empty(),
              "a dust without its colour and scale fails the whole list");
    ctx.check(particles(Json::array({Json{{"type", "minecraft:dust"}, {"color", {1.0, 0.0, 0.0}}, {"scale", 9.0}}})).empty(),
              "a dust scale outside 0.01..4 fails the list");
    ctx.check(particles(Json::array({Json{{"type", "minecraft:no_such_particle"}}})).empty(), "an unknown particle type fails the list");
    ctx.check(particles(Json::array({Json{{"type", "minecraft:block"}, {"block_state", {{"Name", "minecraft:stone"}}}},
                                     Json{{"type", "minecraft:item"}, {"item", {{"id", "minecraft:stick"}}}},
                                     Json{{"type", "gloamingforest:fallen_leaf"}, {"r", 1}, {"g", 2}, {"b", 3}}})) ==
                  Ids{"minecraft:block", "minecraft:item", "gloamingforest:fallen_leaf"},
              "block, item and leaf particles load with their options");
    ctx.check(particles(Json::array({Json{{"type", "minecraft:item"}, {"item", "minecraft:air"}}})).empty(), "an item particle of air fails the list");
    ctx.check(load(Json{{"EntityScanRange", 9}, {"SpawnRange", 2}}).entityScanRange == 9, "EntityScanRange loads over the SpawnRange");
    // SpawnPotentials: each entry a WeightedEntry.Wrapper with both "data" (a SpawnData and its
    // entity compound) and "weight" (Codec.INT); an entry that does not decode is dropped alone
    // (resultOrPartial)
    using Weighted  = std::vector<std::pair<std::string, int>>;
    auto potentials = [&](Json list) { return load(Json{{"SpawnPotentials", std::move(list)}}).potentials; };
    Json zombie     = {{"entity", {{"id", "minecraft:zombie"}}}};
    Json spider     = {{"entity", {{"id", "minecraft:spider"}}}};
    ctx.check(potentials(Json::array({Json{{"data", zombie}}, Json{{"data", spider}, {"weight", 2}}})) == Weighted{{"minecraft:spider", 2}},
              "a potential without a weight is dropped");
    ctx.check(potentials(Json::array({Json{{"data", zombie}, {"weight", "3"}}, Json{{"data", spider}, {"weight", 2}}})) == Weighted{{"minecraft:spider", 2}},
              "a weight that is not a number drops its entry");
    ctx.check(potentials(Json::array({Json{{"data", zombie}, {"weight", 2.9}}})) == Weighted{{"minecraft:zombie", 2}}, "a floating weight is truncated (Codec.INT)");
    ctx.check(potentials(Json::array({Json{{"entity", "minecraft:zombie"}, {"weight", 1}}, Json{{"data", {{"id", "minecraft:zombie"}}}, {"weight", 1}}})).empty(),
              "an entry without a SpawnData compound and its entity is dropped");
    ctx.check(potentials(Json::array({Json{{"data", zombie}, {"weight", 3}}, Json{{"data", spider}, {"weight", 1}}})) ==
                  Weighted{{"minecraft:zombie", 3}, {"minecraft:spider", 1}},
              "weighted potentials load in order");
});

// ---- jigsaw over terrain, the template definitions API --------------------------------------------------------------

namespace {

void registerTerrainPools() {
    static bool done = false;
    if (done) return;
    done = true;
    registerJigsawFixtures();
    // a start like the jigsaw fixture's whose DOWN jigsaw is named after its own pool (CampStructure
    // selects the start's jigsaw by the pool's name) and whose doors lead to the terrain rooms
    std::vector<Json> pal   = {state("minecraft:air"), state("minecraft:stone_bricks"), state("minecraft:oak_planks"), state("minecraft:glass")};
    auto              start = makeTemplate("gf_test:structure/jig/terrain_start", pal,
                                           {{"11111", "12221", "12221", "12221", "11111"}, {"1...1", ".....", ".....", ".....", "1...1"}, {"11111", "11111", "11111", "11111", "11111"}});
    start["jigsaw"] = Json::array({
        Json{{"pos", {2, 0, 2}}, {"front", "down"}, {"top", "north"}, {"name", "gf_test:structure/jig/terrain_start"}, {"pool", "minecraft:empty"}},
        Json{{"pos", {2, 1, 0}}, {"front", "north"}, {"pool", "gf_test:structure/jig/terrain_rooms"}, {"name", "gf_test:door"}, {"target", "gf_test:door"}},
        Json{{"pos", {0, 1, 2}}, {"front", "west"}, {"pool", "gf_test:structure/jig/terrain_rooms"}, {"name", "gf_test:door"}, {"target", "gf_test:door"}},
        Json{{"pos", {4, 1, 2}}, {"front", "east"}, {"pool", "gf_test:structure/jig/terrain_rooms"}, {"name", "gf_test:door"}, {"target", "gf_test:door"}},
    });
    templates::add(start);
    // pool entries with TemplatePoolInstance.height_adjustment (the start projects onto the terrain)
    pools::addDefinition("gf_test:structure/jig/terrain_start",
                         Json{{"gf_test:structure/jig/terrain_start",
                               {{"weight", 1},
                                {"projection", "terrain_matching"},
                                {"height_adjustment", {{"heightmap", "WORLD_SURFACE_WG"}, {"y_offset", 1}}}}}});
    pools::addDefinition("gf_test:structure/jig/room_a",
                         Json{{"gf_test:structure/jig/rooms", 3},
                              {"gf_test:structure/jig/terrain_rooms",
                               {{"weight", 1}, {"height_adjustment", {{"heightmap", "WORLD_SURFACE_WG"}, {"ground_junction_diff_clamp", 2}}}}}});
    pools::addDefinition("gf_test:structure/jig/room_b",
                         Json{{"gf_test:structure/jig/rooms",
                               {{"weight", 2},
                                {"randomized_processors",
                                 Json::array({Json::array({Json{{"data", {{"processor_type", "gloamingforest:wood_swizzle"}, {"target_palette", "minecraft:oak"},
                                                                          {"replacement_palette", "minecraft:birch"}}},
                                                                {"weight", 1}},
                                                           Json{{"data", {{"processor_type", "minecraft:nop"}}}, {"weight", 1}}})})}}},
                              {"gf_test:structure/jig/terrain_rooms", {{"weight", 2}, {"ignore_world_waterlog", true}}}});
}

/// Uneven ground: stone up to y 60 + (x * 3 + z * 5) % 7.
void hillyGround(MemoryRegion& mem) {
    for (int x = -40; x <= 40; x++)
        for (int z = -40; z <= 40; z++) {
            int top = 60 + (((x * 3 + z * 5) % 7) + 7) % 7;
            for (int y = 56; y <= top; y++) (void)mem.setBlock(BlockPos{x, y, z}, blocks::java("minecraft:stone"), 3);
        }
}

} // namespace

GF_REGISTER_SELFTEST("structure.template_definitions", [](Context& ctx) {
    registerTerrainPools();
    // getRandomTemplate / getShuffledSequence (reservoir sampling) are deterministic per seed
    JavaRandom a(4242), b(4242);
    auto       t1 = ext::getRandomTemplate(a, "gf_test:structure/jig/rooms");
    auto       t2 = ext::getRandomTemplate(b, "gf_test:structure/jig/rooms");
    ctx.check(t1 && t1 == t2, "getRandomTemplate is deterministic");
    ctx.check(!ext::getRandomTemplate(a, "gf_test:structure/no_pool"), "an unknown pool has no template");
    std::string order;
    JavaRandom  sequenceRandom(1000);
    for (int i = 0; i < 8; i++) {
        auto seq = ext::getShuffledSequence(sequenceRandom, "gf_test:structure/jig/rooms");
        ctx.check(seq.size() == 2, "the sequence lists every template once");
        order += (seq.empty() ? std::string("?") : seq.front().substr(seq.front().rfind('/') + 1)) + ";";
    }
    checkGolden(ctx, "shuffled_sequence", order);
    ctx.check(ext::getShuffledSequence(a, "gf_test:structure/no_pool").empty(), "an unknown pool has an empty sequence");
    // the deprecated initializeTemplateFromPool keeps the junction height (the template's DOWN
    // jigsaw stands on the block above the junction)
    JavaRandom r(7);
    auto piece = ext::initializeTemplateFromPool("gf_test:structure/jig/terrain_start", BlockPos{0, 100, 0}, ext::FrontAndTop::UpSouth,
                                                 "gf_test:structure/jig/terrain_start", r, 0);
    ctx.require(piece != nullptr, "a template from the pool");
    ctx.check(piece->bbox.minY == 101, "no generation context: the junction height (" + std::to_string(piece->bbox.minY) + ")");
    ctx.check(ext::rotate(ext::FrontAndTop::UpSouth, Rotation::Clockwise90) == ext::FrontAndTop::UpWest &&
                  ext::rotate(ext::FrontAndTop::NorthUp, Rotation::Clockwise180) == ext::FrontAndTop::SouthUp,
              "Rotation#rotate(FrontAndTop)");
    // with a generation context: HeightAdjustment#adjustForTerrain
    MemoryRegion mem;
    hillyGround(mem);
    JavaRandom genRandom(7);
    auto       gen      = ext::contextOf(mem, genRandom, 0);
    auto       adjusted = ext::initializeTemplateFromPool("gf_test:structure/jig/terrain_start", BlockPos{0, 100, 0}, ext::FrontAndTop::UpSouth,
                                                         "gf_test:structure/jig/terrain_start", gen, 0, false);
    ctx.require(adjusted != nullptr, "a template from the pool with a context");
    // the expected height: WorldUtil.adjustForTerrain over the corners of the template's box (at the
    // junction height), rank weighted, plus y_offset 1
    BlockBox const& box = piece->bbox;
    std::vector<int> h;
    for (auto [x, z] : {std::pair{box.minX, box.minZ}, std::pair{box.maxX, box.minZ}, std::pair{box.minX, box.maxZ}, std::pair{box.maxX, box.maxZ}})
        h.push_back(mem.height(worldgen::Heightmap::WorldSurfaceWg, x, z) - 1);
    std::sort(h.begin(), h.end(), std::greater<int>());
    int expected = static_cast<int>(std::floor((1.0 * h[0] + 2.0 * h[1] + 3.0 * h[2] + 4.0 * h[3]) / 10.0 + 0.5)) + 1;
    ctx.check(adjusted->bbox.minY == expected, "height_adjustment moves the template to the terrain (y " + std::to_string(adjusted->bbox.minY) +
                                                   ", expected " + std::to_string(expected) + ")");
    // children: room_a (ground_junction_diff_clamp 2, at the parent's jigsaw as the parent
    // projects onto the terrain) stays within 2 of its junction; room_b (no height_adjustment,
    // ignore_world_waterlog) keeps it
    for (int seed = 9; seed < 15; seed++) {
        JavaRandom plainRandom(seed), childRandom(seed);
        auto       unadjusted = ext::initializeTemplateFromPool("gf_test:structure/jig/terrain_rooms", BlockPos{0, 100, 0}, ext::FrontAndTop::NorthUp,
                                                              "gf_test:door", plainRandom, 1);
        auto       childGen   = ext::contextOf(mem, childRandom, 0);
        auto       child      = ext::initializeTemplateFromPool("gf_test:structure/jig/terrain_rooms", BlockPos{0, 100, 0}, ext::FrontAndTop::NorthUp,
                                                               "gf_test:door", childGen, 1, true);
        ctx.require(unadjusted && child, "a room from the terrain pool");
        auto saved = child->save();
        int  y0    = unadjusted->bbox.minY;
        if (saved.value("Template", std::string{}).ends_with("room_a")) {
            BlockPos at       = unadjusted->getSourcePosition();
            int      terrain  = mem.height(worldgen::Heightmap::WorldSurfaceWg, at.x, at.z) - 1;
            int      expected = std::clamp(terrain, y0 - 2, y0 + 2);
            ctx.check(child->bbox.minY == expected, "ground_junction_diff_clamp: y " + std::to_string(child->bbox.minY) + ", expected " +
                                                        std::to_string(expected));
        } else {
            ctx.check(child->bbox.minY == y0, "no height_adjustment: the junction height");
            ctx.check(saved.value("ignore_waterlog", false), "ignore_world_waterlog is saved with the piece");
            auto back = loadPiece(saved);
            ctx.check(back && back->save().value("ignore_waterlog", false), "and loads back");
        }
    }
    // the assembly with a context is deterministic and adjusts the start
    JavaRandom x1(20261102), x2(20261102);
    auto& seedOverride = bases::detail::jigsawSeedOverride();
    seedOverride       = 0;
    auto c1            = ext::contextOf(mem, x1, 0);
    auto c2            = ext::contextOf(mem, x2, 0);
    auto p1            = ext::assemble("gf_test:structure/jig/terrain_start", BlockPos{0, 100, 0}, 3, c1);
    auto p2            = ext::assemble("gf_test:structure/jig/terrain_start", BlockPos{0, 100, 0}, 3, c2);
    seedOverride.reset();
    ctx.require(!p1.empty(), "the terrain pool assembles");
    ctx.check(describe(p1, kOrigin) == describe(p2, kOrigin), "the same seed assembles the same pieces");
    ctx.check(p1.front()->bbox.minY < 80, "the start sits on the terrain, not at the junction height");
    checkGolden(ctx, "jigsaw_terrain_layout", describe(p1, kOrigin));
    // WorldUtil.adjustForTerrain(context, x, z, radius, gridLength) and Mth.lerpDiscrete
    ctx.check(ext::lerpDiscrete(0.0f, 3, 9) == 3 && ext::lerpDiscrete(1.0f, 3, 9) == 9 && ext::lerpDiscrete(0.5f, 0, 15) == 8, "Mth.lerpDiscrete");
    checkGolden(ctx, "adjust_for_terrain", std::to_string(ext::adjustForTerrain(gen, 5, 5, 12, 3)));
    // WorldGenRegion#getRandom: Minecraft 1.21.1's region random (Xoroshiro, "minecraft:worldgen_region_random"
    // at the center chunk); a memory region has seed 1234567 and center chunk 0, 0 (the value is
    // Minecraft's own, tests/structure/XoroshiroGolden.inc)
    {
        MemoryRegion region;
        ext::regionStarted(region);
        int first  = ext::worldRandom(region).nextInt();
        int second = ext::worldRandom(region).nextInt();
        ctx.check(first == 1186928939, "the region random is Minecraft's (" + std::to_string(first) + ")");
        ext::regionStarted(region);
        ctx.check(ext::worldRandom(region).nextInt() == first && second != first, "the region keeps drawing from its random; a new region starts over");
    }
});

// ---- spawners: mobs scaled by entity_width_max, custom spawn rules ---------------------------------------------------

namespace {

AABB aabbOf(BlockBox const& b) {
    return AABB{Vec3{static_cast<float>(b.minX), static_cast<float>(b.minY), static_cast<float>(b.minZ)},
                Vec3{static_cast<float>(b.maxX + 1), static_cast<float>(b.maxY + 1), static_cast<float>(b.maxZ + 1)}};
}

std::vector<Actor*> mobsIn(BlockSource& source, BlockBox const& box, ActorType type) {
    std::vector<Actor*> out;
    for (auto* a : source.getEntities(aabbOf(box), 0.0f, type))
        if (a && a->isType(type)) out.push_back(a);
    return out;
}

std::vector<Actor*> cowsIn(BlockSource& source, BlockBox const& box) { return mobsIn(source, box, ActorType::Cow); }

float widthOf(Actor const& a) { return a.getAABB().max.x - a.getAABB().min.x; }

/// Waits up to `ticks` for a mob of `type` in `box` that is not in `ignore`; its unique id.
ll::coro::CoroTask<std::optional<std::int64_t>> awaitMob(Context& ctx, BlockBox box, std::vector<std::int64_t> ignore, int ticks, ActorType type) {
    for (int i = 0; i < ticks; i += 5) {
        co_await ctx.ticks(5);
        (void)ctx.player(); // keep the test player near the spawner (RequiredPlayerRange)
        auto* source = ctx.region();
        if (!source) co_return std::nullopt;
        for (auto* a : mobsIn(*source, box, type)) {
            auto id = a->getOrCreateUniqueID().rawID;
            if (std::find(ignore.begin(), ignore.end(), id) == ignore.end()) co_return id;
        }
    }
    co_return std::nullopt;
}

ll::coro::CoroTask<std::optional<std::int64_t>> awaitCow(Context& ctx, BlockBox box, std::vector<std::int64_t> ignore, int ticks) {
    co_return co_await awaitMob(ctx, box, std::move(ignore), ticks, ActorType::Cow);
}

void despawnMobs(Context& ctx, BlockBox const& box, ActorType type) {
    if (auto* source = ctx.region())
        for (auto* a : mobsIn(*source, box, type)) a->despawn();
}

/// What a spawn egg does to a spawner: another mob (and a short delay, so the test does not wait).
bool changeSpawnerMob(BlockSource& source, BlockPos const& pos, std::string const& mob) {
    auto* actor = source.getBlockEntity(pos);
    if (!actor || actor->getType() != BlockActorType::MobSpawner) return false;
    auto& spawner = static_cast<MobSpawnerBlockActor&>(*actor);
    spawner.setMob(source, ActorDefinitionIdentifier(mob), nullptr);
    if (auto* sp = spawner.mSpawner.get()) sp->mSpawnDelay = 1;
    actor->refresh(source);
    return true;
}

Actor* fetch(std::int64_t raw) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID id;
    id.rawID = raw;
    return level->fetchEntity(id, false);
}

void despawnCows(Context& ctx, BlockBox const& box) {
    if (auto* source = ctx.region())
        for (auto* a : cowsIn(*source, box)) a->despawn();
}

Json spawnerData(Json extra) {
    Json d{{"Delay", 1}, {"MinSpawnDelay", 10}, {"MaxSpawnDelay", 20}, {"SpawnCount", 1}, {"SpawnRange", 2}, {"RequiredPlayerRange", 16}, {"MaxNearbyEntities", 6}};
    d.update(extra);
    return d;
}

} // namespace

GF_REGISTER_SELFTEST("structure.spawner_scale", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto area = co_await ctx.flatArea(5765, 8, 6);
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5765)), "test area loaded");
    auto* source = ctx.region();
    ctx.require(source != nullptr, "block source");
    auto level = ll::service::getLevel();
    ctx.require(level != nullptr, "level");
    BlockBox near{area.minX - 2, area.minY - 1, area.minZ - 2, area.maxX + 2, area.minY + 4, area.maxZ + 2};
    Vec3     at{static_cast<float>(area.minX) + 4.5f, static_cast<float>(area.minY), static_cast<float>(area.minZ) + 4.5f};
    // a cow (test worlds may be peaceful): the scale is the same for every mob
    auto* mob = level->getSpawner().spawnMob(*source, ActorDefinitionIdentifier("minecraft:cow"), nullptr, at, false, false, false);
    ctx.require(mob != nullptr, "a cow spawns");
    float before = widthOf(*mob);
    float scale  = 0.8f / 1.4f;
    ctx.check(spawners::applyScale(*mob, scale), "the scale applies");
    float after = widthOf(*mob);
    ctx.check(std::abs(after - before * scale) < 0.05f, "the cow's box shrinks to " + std::to_string(after) + " (from " + std::to_string(before) + ")");
    mob->despawn();

    // growing up: a calf given the factor keeps it as an adult (the game sets the adult size from
    // the cow's definition, the kit puts the factor on top again)
    auto summoned = gf::selftest::runServerCommand("summon minecraft:cow " + std::to_string(area.minX + 2) + " " + std::to_string(area.minY) + " " +
                                                   std::to_string(area.minZ + 2) + " 0 0 minecraft:entity_born gf_scale_calf");
    ctx.require(summoned.ok, "a calf is summoned (" + summoned.messages + ")");
    co_await ctx.ticks(2);
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    std::optional<std::int64_t> calfId;
    for (auto* a : cowsIn(*source, near))
        if (a->getNameTag() == "gf_scale_calf") calfId = a->getOrCreateUniqueID().rawID;
    ctx.require(calfId.has_value(), "the calf is there");
    auto* calf = fetch(*calfId);
    ctx.require(calf != nullptr, "the calf can be fetched");
    float calfWidth = widthOf(*calf);
    spawners::scaleMob(*calf, 0.5f);
    co_await ctx.ticks(5);
    calf = fetch(*calfId);
    ctx.require(calf != nullptr, "the calf is still there");
    ctx.check(std::abs(widthOf(*calf) - calfWidth * 0.5f) < 0.02f, "the calf is scaled to 0.5 (width " + std::to_string(widthOf(*calf)) + ", unscaled " +
                                                                        std::to_string(calfWidth) + ")");
    ctx.check(spawners::scaleOf(*calf) == 0.5f, "the factor is kept on the mob");
    auto grown = gf::selftest::runServerCommand("event entity @e[name=gf_scale_calf] minecraft:ageable_grow_up");
    ctx.require(grown.ok, "the calf grows up (" + grown.messages + ")");
    co_await ctx.ticks(25);
    calf = fetch(*calfId);
    ctx.require(calf != nullptr, "the grown cow is there");
    ctx.check(std::abs(widthOf(*calf) - 0.9f * 0.5f) < 0.03f, "the grown cow keeps the factor (width " + std::to_string(widthOf(*calf)) + ", 0.45 expected)");
    calf->despawn();

    // end to end: a spawner placed with an entity_scale record scales the mobs it spawns, and a
    // spawner of the same mob next to it without one does not (the record of the spawner that
    // spawns counts, not any record nearby)
    BlockPos scaledPos{area.minX + 4, area.minY, area.minZ + 4};
    BlockPos plainPos{area.minX + 1, area.minY, area.minZ + 1};
    {
        BlockSourceRegion region(*source, scaledPos);
        // the scaled spawner waits (a long delay): only the plain one spawns at first
        blockentities::spawner(region, scaledPos, "minecraft:cow", spawnerData(Json{{"entity_scale", 0.4f}, {"Delay", 100000}, {"MinSpawnDelay", 100000},
                                                                                   {"MaxSpawnDelay", 100000}}));
        blockentities::spawner(region, plainPos, "minecraft:cow", spawnerData(Json::object()));
    }
    auto plainCow = co_await awaitCow(ctx, near, {}, 200);
    std::optional<float> plainWidth;
    if (plainCow) {
        co_await ctx.ticks(5);
        if (auto* a = fetch(*plainCow)) plainWidth = widthOf(*a);
    }
    ctx.check(plainWidth && (std::abs(*plainWidth - 0.9f) < 0.03f || std::abs(*plainWidth - 0.45f) < 0.03f),
              "a cow from the plain spawner next to a scaled one keeps its size (width " + (plainWidth ? std::to_string(*plainWidth) : std::string("-")) + ")");
    despawnCows(ctx, near);
    // now the scaled spawner spawns (the plain one is gone)
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    (void)game::setBlock(*source, plainPos, blocks::air());
    if (auto* actor = source->getBlockEntity(scaledPos); actor && actor->getType() == BlockActorType::MobSpawner)
        if (auto* sp = static_cast<MobSpawnerBlockActor&>(*actor).mSpawner.get()) sp->mSpawnDelay = 1;
    auto scaledCow = co_await awaitCow(ctx, near, {}, 200);
    std::optional<float> width;
    if (scaledCow) {
        co_await ctx.ticks(5); // the scale applies once the mob is set up in the level
        if (auto* a = fetch(*scaledCow)) width = widthOf(*a);
    }
    if (width) ctx.info("a cow from the scaled spawner: width " + std::to_string(*width));
    despawnCows(ctx, near);
    if (!width) ctx.check(false, "the scaled spawner spawned a cow within 200 ticks");
    // 0.4 of an adult (0.9) or of a calf (0.45); unscaled they are 0.9 / 0.45
    else ctx.check(std::abs(*width - 0.36f) < 0.03f || std::abs(*width - 0.18f) < 0.03f, "a cow from the scaled spawner is scaled to 0.4 (width " + std::to_string(*width) + ")");

    // a spawn egg turns the scaled spawner into a sheep spawner: Java replaces the whole SpawnData,
    // its generic.scale with it, so the sheep spawn at their own size and the cow's record goes
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    ctx.require(changeSpawnerMob(*source, scaledPos, "minecraft:sheep"), "the spawner takes the sheep");
    auto sheep = co_await awaitMob(ctx, near, {}, 200, ActorType::Sheep);
    std::optional<float> sheepWidth;
    if (sheep) {
        co_await ctx.ticks(5); // (a scale would apply by now)
        if (auto* a = fetch(*sheep)) sheepWidth = widthOf(*a);
    }
    despawnMobs(ctx, near, ActorType::Sheep);
    ctx.check(sheepWidth && (std::abs(*sheepWidth - 0.9f) < 0.03f || std::abs(*sheepWidth - 0.45f) < 0.03f),
              "a sheep from the changed spawner keeps its size (width " + (sheepWidth ? std::to_string(*sheepWidth) : std::string("-")) + ")");
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    ctx.check(!blockentities::spawnerRecord(*source, scaledPos), "the cow's scale record was dropped with the cow");
});

GF_REGISTER_SELFTEST("structure.spawner_rules", [](Context& ctx) -> ll::coro::CoroTask<> {
    // SpawnerProcessor.compile's custom spawn rules (block light 0..7, sky light 0..15) go with
    // the spawner: under a glowstone ceiling (block light 12 at the floor) its mobs do not spawn,
    // without the ceiling they do
    auto area = co_await ctx.flatArea(5785, 8, 6);
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5785)), "test area loaded");
    auto* source = ctx.region();
    ctx.require(source != nullptr, "block source");
    // the processor carries the rules from the spawn data into the spawner data
    {
        auto entry = Json::parse(ext::compileSpawnerProcessor(2, 0.8f, {{"minecraft:cow", 1}}));
        ctx.check(entry.value("start_delay_factor", 0.0f) == 0.25f && entry.value("range", 0) == 2 && entry.value("entity_width_max", 0.0f) == 0.8f,
                  "SpawnerProcessor.compile: range, start delay factor and entity_width_max");
        processors::Instance p{"gloamingforest:spawner_processor", entry};
        MemoryRegion         mem;
        BlockInfo            info{BlockPos{0, 64, 0}, &blocks::java("minecraft:spawner"), Json{{"marker", "spawner"}}};
        TemplateSettings     settings;
        auto                 out = processors::run(p, mem, info, info, settings, BlockPos{0, 64, 0});
        ctx.check(out && out->data.value("entity", std::string{}) == "minecraft:cow", "the spawner gets the cow");
        ctx.check(out && out->data.contains("spawn_rules") && out->data["spawn_rules"] == Json{{"block_light", {0, 7}}, {"sky_light", {0, 15}}},
                  "the custom spawn rules go into the spawner data (" + (out ? out->data.dump() : std::string("-")) + ")");
    }
    BlockBox near{area.minX - 2, area.minY - 1, area.minZ - 2, area.maxX + 2, area.minY + 4, area.maxZ + 2};
    BlockPos spawnerPos{area.minX + 4, area.minY, area.minZ + 4};
    {
        BlockSourceRegion region(*source, spawnerPos);
        for (int x = area.minX; x <= area.maxX; x++)
            for (int z = area.minZ; z <= area.maxZ; z++) (void)region.setBlock(BlockPos{x, area.minY + 3, z}, blocks::java("minecraft:glowstone"), 3);
        blockentities::spawner(region, spawnerPos, "minecraft:cow",
                               spawnerData(Json{{"SpawnCount", 2}, {"spawn_rules", {{"block_light", {0, 7}}, {"sky_light", {0, 15}}}}}));
    }
    int vetoesBefore = spawners::vetoes();
    auto lit = co_await awaitCow(ctx, near, {}, 80);
    ctx.check(!lit, "no cow spawns where the block light is above 7");
    ctx.check(spawners::vetoes() > vetoesBefore, "the spawner tried and its spawns were refused (" + std::to_string(spawners::vetoes() - vetoesBefore) + ")");
    despawnCows(ctx, near);
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    {
        BlockSourceRegion region(*source, spawnerPos);
        for (int x = area.minX; x <= area.maxX; x++)
            for (int z = area.minZ; z <= area.maxZ; z++) (void)region.setBlock(BlockPos{x, area.minY + 3, z}, blocks::air(), 3);
    }
    auto dark = co_await awaitCow(ctx, near, {}, 200);
    ctx.check(dark.has_value(), "without the glowstone (block light 0) the spawner spawns");
    despawnCows(ctx, near);

    // a spawn egg gives the spawner another mob: Java replaces the whole SpawnData, the custom
    // spawn rules with it. Under the glowstone again, the sheep spawn and the cow's rules are gone.
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    {
        BlockSourceRegion region(*source, spawnerPos);
        for (int x = area.minX; x <= area.maxX; x++)
            for (int z = area.minZ; z <= area.maxZ; z++) (void)region.setBlock(BlockPos{x, area.minY + 3, z}, blocks::java("minecraft:glowstone"), 3);
    }
    ctx.require(changeSpawnerMob(*source, spawnerPos, "minecraft:sheep"), "the spawner takes the sheep");
    int  vetoesBeforeSheep = spawners::vetoes();
    auto sheep             = co_await awaitMob(ctx, near, {}, 200, ActorType::Sheep);
    ctx.check(sheep.has_value(), "the sheep spawn under the glowstone (the cow's light rules went with the cow)");
    ctx.check(spawners::vetoes() == vetoesBeforeSheep, "no sheep spawn was vetoed");
    despawnMobs(ctx, near, ActorType::Sheep);
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    ctx.check(!blockentities::spawnerRecord(*source, spawnerPos), "the cow's record was dropped");
});

// ---- template entities: capture -> place keeps their data and turns their facing -------------------------------------

GF_REGISTER_SELFTEST("structure.template_entities", [](Context& ctx) -> ll::coro::CoroTask<> {
    // StructureTemplate#fillEntityList / #placeEntities for an entity that is not a painting: its
    // saved data comes back (a named armor stand) and its facing turns with the placement
    auto area = co_await ctx.flatArea(5758, 6, 4);
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5758)), "test area loaded");
    BlockPos at{area.minX + 1, area.minY, area.minZ + 1};
    auto stand = gf::selftest::runServerCommand("summon minecraft:armor_stand " + std::to_string(at.x + 0.5) + " " + std::to_string(at.y) + " " +
                                                std::to_string(at.z + 0.5) + " 30 0 minecraft:entity_spawned gf_capture_stand");
    ctx.require(stand.ok, "an armor stand is summoned (" + stand.messages + ")");
    co_await ctx.ticks(2);
    auto* source = ctx.region();
    ctx.require(source != nullptr, "block source");
    BlockBox capturedBox{at.x - 1, at.y, at.z - 1, at.x + 1, at.y + 1, at.z + 1};
    Actor*   original = nullptr;
    for (auto* a : mobsIn(*source, capturedBox, ActorType::ArmorStand))
        if (a->getNameTag() == "gf_capture_stand") original = a;
    ctx.require(original != nullptr, "the armor stand is there");
    float yaw = original->getRotation().y;
    int   skipped = 0;
    Json  doc     = templates::capture(*source, capturedBox, "gf_test:structure/captured_stand", skipped);
    original->despawn();
    Json entry;
    for (auto const& e : doc.value("entities", Json::array()))
        if (e.value("id", std::string{}) == "minecraft:armor_stand") entry = e;
    ctx.require(entry.is_object(), "the capture holds the armor stand (" + doc.value("entities", Json::array()).dump() + ")");
    auto const& data = entry["data"];
    ctx.check(data.contains("Rotation") && std::abs(data["Rotation"][0].get<float>() - yaw) < 1e-3f, "the capture keeps its rotation");
    ctx.check(data.contains("nbt") && data["nbt"].get<std::string>().find("gf_capture_stand") != std::string::npos, "the capture keeps its saved data (its name)");
    std::string error;
    ctx.require(templates::add(doc, &error), "the captured template is valid " + error);
    // placed elsewhere, turned (CLOCKWISE_90) and mirrored (LEFT_RIGHT): the stand (template
    // 1.5, 0, 1.5) lands at the origin + (1.5, 0, 1.5), facing 90 + (180 - yaw)
    BlockPos         origin{area.minX + 3, area.minY, area.minZ + 1};
    TemplateSettings settings;
    settings.rotation = Rotation::Clockwise90;
    settings.mirror   = Mirror::LeftRight;
    {
        BlockSourceRegion region(*source, origin);
        ctx.require(templates::place("gf_test:structure/captured_stand", region, origin, settings), "the captured template places");
    }
    co_await ctx.ticks(2);
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    Actor* placed = nullptr;
    for (auto* a : mobsIn(*source, BlockBox{area.minX, area.minY - 1, area.minZ, area.maxX, area.minY + 3, area.maxZ}, ActorType::ArmorStand))
        if (a->getNameTag() == "gf_capture_stand") placed = a;
    ctx.check(placed != nullptr, "the placed armor stand carries its saved data (its name)");
    if (placed) {
        Vec3 const& p = placed->getPosition();
        ctx.check(std::abs(p.x - (origin.x + 1.5f)) < 0.01f && std::abs(p.z - (origin.z + 1.5f)) < 0.01f,
                  "it stands where the template's position turns to (" + std::to_string(p.x) + ", " + std::to_string(p.z) + ")");
        float expected = templates::turnedYaw(yaw, settings.rotation, settings.mirror);
        float actual   = placed->getRotation().y;
        ctx.check(std::abs(std::remainder(actual - expected, 360.0f)) < 0.5f,
                  "it faces the turned yaw (" + std::to_string(actual) + ", " + std::to_string(expected) + " expected from " + std::to_string(yaw) + ")");
        placed->despawn();
    }
});

// ---- a scaled mob keeps its size across a save and load (tools/e2e/server/scenarios/structure.ps1) ---------------------

GF_REGISTER_SELFTEST("structure.scale_probe", [](Context& ctx) -> ll::coro::CoroTask<> {
    // Only armed by `/gloamingforest structure_util scale_probe` (the server scenario, before a
    // restart): then a scaled calf is left in a glass pen and recorded; after the restart this
    // test loads it, measures it, makes the game set its size from its definition again (it grows
    // up: the adult's collision box) and checks that the kit, watching the mob since it loaded,
    // puts the factor on the new size. A plain run leaves nothing behind.
    constexpr char kProbeKey[] = "gf:structure:scale_probe";
    constexpr char kArmedKey[] = "gf:structure:scale_probe_armed";
    auto           probe       = game::db::get(kProbeKey);
    if (!probe) {
        if (!game::db::get(kArmedKey)) {
            ctx.info("scale probe not armed (structure_util scale_probe arms it before a restart): nothing to do");
            co_return;
        }
        game::db::erase(kArmedKey);
        // (dx 5712..5714: outside every flat area of the kit's tests and the boxes the spawner
        // tests count cows in, so the calf neither disturbs them nor is disturbed)
        auto base = ctx.testPos(5713, 2);
        ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
        auto* source = ctx.region();
        ctx.require(source != nullptr, "block source");
        BlockPos at = game::blockPos(base);
        ctx.require(blocks::blocksMotion(source->getBlock(BlockPos{at.x, at.y - 1, at.z})), "the probe stands on the ground");
        // a glass pen two blocks high keeps the cow in place until the restart
        Json pen = Json::array();
        for (auto [dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}})
            for (int dy = 0; dy < 2; dy++) {
                BlockPos p{at.x + dx, at.y + dy, at.z + dz};
                ctx.require(source->getBlock(p).isAir() && source->getBlock(BlockPos{at.x, at.y + dy, at.z}).isAir(), "the pen's place is free");
                pen.push_back(Json::array({p.x, p.y, p.z}));
            }
        for (auto const& p : pen) (void)game::setBlock(*source, BlockPos{p[0].get<int>(), p[1].get<int>(), p[2].get<int>()}, blocks::java("minecraft:glass"));
        auto summoned = gf::selftest::runServerCommand("summon minecraft:cow " + std::to_string(at.x + 0.5) + " " + std::to_string(at.y) + " " +
                                                       std::to_string(at.z + 0.5) + " 0 0 minecraft:entity_born gf_scale_probe");
        ctx.require(summoned.ok, "a calf is summoned (" + summoned.messages + ")");
        co_await ctx.ticks(2);
        source = ctx.region();
        ctx.require(source != nullptr, "block source");
        std::optional<std::int64_t> id;
        for (auto* a : cowsIn(*source, BlockBox{at.x - 1, at.y - 1, at.z - 1, at.x + 1, at.y + 2, at.z + 1}))
            if (a->getNameTag() == "gf_scale_probe") id = a->getOrCreateUniqueID().rawID;
        ctx.require(id.has_value(), "the probe cow is there");
        auto* mob = fetch(*id);
        ctx.require(mob != nullptr, "the probe cow can be fetched");
        float unscaled = widthOf(*mob);
        spawners::scaleMob(*mob, 0.5f);
        co_await ctx.ticks(5);
        auto* scaled = fetch(*id);
        ctx.require(scaled != nullptr, "the probe cow is still there");
        float width     = widthOf(*scaled);
        float dataScale = spawners::dataScaleOf(*scaled).value_or(0.0f);
        ctx.check(std::abs(width - unscaled * 0.5f) < 0.02f, "the calf is scaled to 0.5 (width " + std::to_string(width) + ")");
        game::db::put(kProbeKey, Json{{"id", *id}, {"pos", {at.x, at.y, at.z}}, {"width", width}, {"data_scale", dataScale}, {"pen", pen}});
        ctx.info("scale probe written id=" + std::to_string(*id));
        co_return;
    }
    game::db::erase(kProbeKey);
    auto id        = probe->value("id", std::int64_t{0});
    auto pos       = probe->value("pos", Json::array());
    auto width     = probe->value("width", 0.0f);
    auto dataScale = probe->value("data_scale", 0.0f);
    ctx.require(pos.is_array() && pos.size() == 3, "the probe record is complete");
    bool loaded = co_await ctx.ensureLoaded(core::GlobalPos{0, pos[0].get<int>(), pos[1].get<int>(), pos[2].get<int>()});
    if (auto* source = ctx.region()) // the pen goes first: nothing stays behind whatever the checks say
        for (auto const& p : probe->value("pen", Json::array())) {
            BlockPos at{p[0].get<int>(), p[1].get<int>(), p[2].get<int>()};
            if (source->hasChunksAt(at, 0, false)) (void)game::setBlock(*source, at, blocks::air());
        }
    ctx.require(loaded, "probe chunk loaded");
    Actor* mob = nullptr;
    for (int i = 0; i < 40 && !mob; i++) {
        co_await ctx.ticks(1);
        mob = fetch(id);
    }
    ctx.require(mob != nullptr, "the probe mob loaded");
    ctx.info("the probe mob as it loaded: width " + std::to_string(widthOf(*mob)) + ", data scale " + std::to_string(spawners::dataScaleOf(*mob).value_or(0)) +
             " (Bedrock keeps part of a mob's size)");
    co_await ctx.ticks(5); // the kit scales a loaded mob once it is set up
    mob = fetch(id);
    ctx.require(mob != nullptr, "the probe mob is still there");
    float now    = widthOf(*mob);
    float nowBox = spawners::dataScaleOf(*mob).value_or(0.0f);
    ctx.check(std::abs(now - width) < 0.02f, "the loaded mob keeps its scale (width " + std::to_string(now) + ", " + std::to_string(width) + " expected)");
    ctx.check(std::abs(nowBox - dataScale) < 0.02f, "the loaded mob is drawn at its scale (data scale " + std::to_string(nowBox) + ", " + std::to_string(dataScale) + " expected)");
    // the calf grows up: the game sets its size from the adult's definition (the kit's size checks
    // are held while that is measured), then the kit puts the factor on the new size
    spawners::holdSizeChecks(true);
    bool fired = gf::selftest::runServerCommand("event entity @e[name=gf_scale_probe] minecraft:ageable_grow_up").ok;
    co_await ctx.ticks(2);
    mob            = fetch(id);
    float reset    = mob ? widthOf(*mob) : 0.0f;
    float resetBox = mob ? spawners::dataScaleOf(*mob).value_or(0.0f) : 0.0f;
    spawners::holdSizeChecks(false);
    ctx.require(mob != nullptr && fired, "the probe mob takes the event");
    ctx.info("grown up, before the kit's check: width " + std::to_string(reset) + ", data scale " + std::to_string(resetBox));
    ctx.check(std::abs(reset - now) > 0.1f, "growing up set the definition's size (width " + std::to_string(reset) + " from " + std::to_string(now) + ")");
    co_await ctx.ticks(25);
    mob = fetch(id);
    ctx.require(mob != nullptr, "the probe mob is still there after growing up");
    float after       = widthOf(*mob);
    float afterBox    = spawners::dataScaleOf(*mob).value_or(0.0f);
    // the parts the game set are the definition's times the factor, the others stay as they were
    float expectBox   = std::abs(resetBox - nowBox) > 1e-3f ? resetBox * 0.5f : nowBox;
    bool  widthKept   = std::abs(after - reset * 0.5f) < 0.02f;
    bool  scaleKept   = std::abs(afterBox - expectBox) < 0.02f;
    ctx.check(widthKept, "the kit put the factor on the loaded mob's new size (width " + std::to_string(after) + ", " + std::to_string(reset * 0.5f) + " expected)");
    ctx.check(scaleKept, "and it is drawn at its scale (data scale " + std::to_string(afterBox) + ", " + std::to_string(expectBox) + " expected)");
    if (std::abs(now - width) < 0.02f && widthKept && scaleKept)
        ctx.info("scale probe verified width=" + std::to_string(after) + " data scale=" + std::to_string(afterBox));
    mob->despawn();
});

// ---- update_marking: walls take their shape from their neighbours after decay ------------------------------------------

GF_REGISTER_SELFTEST("structure.update_marking", [](Context& ctx) -> ll::coro::CoroTask<> {
    // three walls in a row (the third a mossy one that vertical_decay removes): the middle wall's
    // east arm (from the template) points at nothing. With update_marking the wall takes its shape
    // from its neighbours once the structure stands; without it, it keeps the stale arm.
    static std::string const id = [] {
        auto doc = makeTemplate("gf_test:structure/walls", {state("minecraft:air"),
                                                            state("minecraft:cobblestone_wall", {{"east", "low"}, {"west", "none"}, {"north", "none"}, {"south", "none"}, {"up", "true"}, {"waterlogged", "false"}}),
                                                            state("minecraft:cobblestone_wall", {{"east", "low"}, {"west", "low"}, {"north", "none"}, {"south", "none"}, {"up", "false"}, {"waterlogged", "false"}}),
                                                            state("minecraft:mossy_cobblestone_wall", {{"east", "none"}, {"west", "low"}, {"north", "none"}, {"south", "none"}, {"up", "true"}, {"waterlogged", "false"}})},
                                {{"123"}});
        templates::add(doc);
        return std::string("gf_test:structure/walls");
    }();
    // Placed as world generation places it: through a region that writes into the chunks without
    // block events or neighbour updates (a ProtoChunk) and is not a live region, so the marked
    // positions are queued for their chunk. Bedrock's own wall fix (on placement in a loaded world)
    // never runs: only update_marking changes a wall.
    auto area = co_await ctx.flatArea(5775, 8, 4);
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5775)), "test area loaded");
    auto* source = ctx.region();
    ctx.require(source != nullptr, "block source");
    std::string const decay = R"({"processor_type":"gloamingforest:vertical_decay","decay_blocks":["minecraft:mossy_cobblestone_wall"],"decay_chance":1.0})";
    std::string const mark  = R"({"processor_type":"gloamingforest:update_marking","mark_updates":["minecraft:cobblestone_wall"]})";
    BlockPos          marked{area.minX + 1, area.minY, area.minZ + 1};
    BlockPos          plain{area.minX + 1, area.minY, area.minZ + 3};
    std::size_t       before = blockentities::pending();
    std::size_t       afterMarked = 0, afterPlain = 0;
    {
        ChunkWriteRegion region(*source, marked);
        TemplateSettings withMark;
        withMark.processors = {decay, mark};
        TemplateSettings onlyDecay;
        onlyDecay.processors = {decay};
        ctx.require(templates::place(id, region, marked, withMark), "the walls place (update_marking)");
        afterMarked = blockentities::pending();
        ctx.require(templates::place(id, region, plain, onlyDecay), "the walls place (no update_marking)");
        afterPlain = blockentities::pending();
    }
    // markPosForPostprocessing: one marked position per marked block (the two cobblestone walls;
    // the decayed mossy wall is marked by nothing) in its chunk's list, none without the processor
    ctx.check(afterMarked - before == 2, "update_marking marked each marked wall for its chunk (" + std::to_string(afterMarked - before) + ")");
    ctx.check(afterPlain == afterMarked, "nothing is marked without update_marking");
    // stored as the chunk's list of packed positions (one record per chunk, not one job each)
    int  dim       = static_cast<int>(source->getDimensionId());
    auto recordKey = [&](BlockPos const& p) {
        return "gf:structure:pending:" + std::to_string(dim) + ":" + std::to_string(core::chunkOf(p.x)) + ":" + std::to_string(core::chunkOf(p.z));
    };
    std::set<std::string> keys{recordKey(marked), recordKey(BlockPos{marked.x + 1, marked.y, marked.z})};
    blockentities::save();
    std::size_t stored = 0;
    for (auto const& k : keys)
        if (auto doc = game::db::get(k); doc && doc->is_object()) stored += doc->value("shape", Json::array()).size();
    ctx.check(stored == 2, "the marked positions are stored packed in their chunk's record (" + std::to_string(stored) + ")");
    auto eastOf = [&](BlockPos const& p) {
        auto* s = ctx.region();
        if (!s) return std::string("?");
        auto states = game::statesOf(s->getBlock(p));
        auto it     = states.find("wall_connection_type_east");
        return it == states.end() ? std::string("-") : it->second;
    };
    BlockPos markedMiddle{marked.x + 1, marked.y, marked.z};
    BlockPos plainMiddle{plain.x + 1, plain.y, plain.z};
    ctx.check(game::blockIdAt(*source, BlockPos{marked.x + 2, marked.y, marked.z}) == "minecraft:air", "vertical_decay removed the mossy wall");
    ctx.check(eastOf(markedMiddle) == "short" && eastOf(plainMiddle) == "short", "both middle walls keep the template's east arm as written (" +
                                                                                   eastOf(markedMiddle) + ", " + eastOf(plainMiddle) + ")");
    co_await ctx.ticks(15);
    ctx.check(blockentities::pending() <= before, "the chunk's post-processing ran once it was loaded");
    ctx.check(eastOf(markedMiddle) == "none", "update_marking: the marked wall dropped the arm to the removed wall (" + eastOf(markedMiddle) + ")");
    ctx.check(eastOf(plainMiddle) == "short", "the unmarked wall keeps the template's stale arm, as in Java (" + eastOf(plainMiddle) + ")");
    blockentities::save();
    bool erased = true;
    for (auto const& k : keys) erased = erased && !game::db::get(k);
    ctx.check(erased, "the chunk's record is gone once its work ran");
});

// ---- queued work survives a restart (tools/e2e/server/scenarios/structure.ps1) -----------------------------------------

GF_REGISTER_SELFTEST("structure.pending_read", [](Context& ctx) -> ll::coro::CoroTask<> {
    // `/gloamingforest structure_util pending_probe` queued a job in an unloaded chunk before the
    // restart; the job (persisted in gf:structure:pending) runs once its chunk is loaded
    auto probe = game::db::get("gf:structure:probe");
    if (!probe) {
        ctx.info("no pending probe (structure_util pending_probe did not run): nothing to check");
        co_return;
    }
    auto nonce = probe->value("nonce", std::string{});
    auto pos   = probe->value("pos", Json::array());
    game::db::erase("gf:structure:probe");
    // the fixture landmark start pending_probe forced (unplaced, in an unloaded chunk) was reloaded
    // from the world DB: its pieces came back through F6's own piece factories (no "unknown piece
    // type" for them), with their type ids and saved tags; then it is forgotten again
    if (auto const startProbe = probe->value("start", Json()); startProbe.is_object()) {
        auto key = startProbe.value("key", std::string{});
        auto sp  = startProbe.value("pos", Json::array());
        if (!key.empty() && sp.is_array() && sp.size() == 3) {
            core::GlobalPos startPos{probe->value("dim", 0), sp[0].get<int>(), sp[1].get<int>(), sp[2].get<int>()};
            auto            start = landmark::startAt(startPos.dim, BlockPos{startPos.x, startPos.y + 1, startPos.z});
            ctx.check(start.has_value() && start->key == key, "the forced start of " + key + " is stored after the restart");
            if (start) {
                auto        pieces = landmark::ext::startPieces(*start);
                std::string types;
                bool        loaded = pieces.size() == startProbe.value("pieces", std::size_t{0});
                for (auto const& p : pieces) {
                    types += p.type + " ";
                    // the piece's type has a factory: the saved tag loads into a live piece again
                    std::unique_ptr<Piece> live;
                    if (p.tag.is_object() && p.tag.value("id", std::string{}) == p.type) {
                        try {
                            live = loadPiece(p.tag);
                        } catch (...) {}
                    }
                    loaded = loaded && live != nullptr && !p.type.empty();
                }
                ctx.check(loaded, "the reloaded start's pieces load through their piece types (" + types + ")");
                if (loaded) ctx.info("start probe verified " + key + " pieces=" + types);
            }
            ctx.check(landmark::ext::forgetStartForTest(startPos, key) == 1, "the probe's forced start is removed again");
        }
    }
    ctx.require(!nonce.empty() && pos.is_array() && pos.size() == 3, "the probe record is complete");
    core::GlobalPos at{probe->value("dim", 0), pos[0].get<int>(), pos[1].get<int>(), pos[2].get<int>()};
    ctx.check(!game::db::get("gf:structure:test_mark"), "the probe job did not run before its chunk was loaded");
    ctx.check(blockentities::pending() > 0, "the probe job is still queued after the restart");
    ctx.require(co_await ctx.ensureLoaded(at), "probe chunk loaded");
    std::string seen;
    for (int i = 0; i < 40 && seen != nonce; i++) {
        co_await ctx.ticks(1);
        if (auto mark = game::db::get("gf:structure:test_mark")) seen = mark->value("nonce", std::string{});
    }
    game::db::erase("gf:structure:test_mark");
    ctx.check(seen == nonce, "the probe job ran once its chunk was loaded (got '" + seen + "')");
    if (seen == nonce) ctx.info("pending probe verified nonce=" + nonce);
});

} // namespace gf::structure::selftests
