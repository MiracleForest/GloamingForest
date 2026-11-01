// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// Selftests of the structure kit (rule R12: "structure.*", testPos dx 5700..5799).
//
// Placements are checked twice: on an in-memory region at a fixed origin (MemoryRegion), whose
// golden volume hashes do not depend on where the test area is, and in the world, whose
// snapshotVolume must equal the in-memory hash of the same placement (rule R19 golden hashes).
#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <optional>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/Motif.h"
#include "mc/world/actor/Painting.h"
#include "mc/world/level/BaseMobSpawner.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/FlowerPotBlockActor.h"
#include "mc/world/level/block/actor/MobSpawnerBlockActor.h"
#include "mc/world/phys/AABB.h"

#include "core/VolumeHash.h"
#include "feature/ext/structure/TemplateDefinitions.h"
#include "feature/structure/Region.h"
#include "feature/structure/bases/BasesInternal.h"
#include "feature/structure/bases/HollowHill.h"
#include "feature/structure/bases/Maze.h"
#include "feature/structure/bases/Tower.h"
#include "game/Blocks.h"
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
                    // TF blocks count as air: the goldens stay valid once the block skeleton (F1) registers them
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
/// TF's nagastone (nagastone_variants, once TF's blocks are registered).
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
            state("twilightforest:etched_nagastone"),
            state("twilightforest:nagastone_pillar", {{"axis", "y"}}),
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
    ctx.check(north.getWorldPos(1, 2, 3) == BlockPos(108, 12, 211) && north.rotation == Rotation::None, "TF NORTH");
    ctx.check(south.getWorldPos(1, 2, 3) == BlockPos(101, 12, 203) && south.rotation == Rotation::Clockwise180, "TF SOUTH");
    ctx.check(west.getWorldPos(1, 2, 3) == BlockPos(106, 12, 201) && west.rotation == Rotation::Counterclockwise90, "TF WEST");
    ctx.check(east.getWorldPos(1, 2, 3) == BlockPos(103, 12, 213) && east.rotation == Rotation::Clockwise90, "TF EAST");
    ctx.check(none.getWorldPos(1, 2, 3) == BlockPos(1, 2, 3), "no orientation: world coordinates");
    ctx.check(bases::ComponentOld::getComponentToAddBoundingBox2(10, 20, 30, -1, 0, -2, 5, 6, 7, Orientation::West) == BlockBox::of(5, 20, 29, 12, 26, 34),
              "getComponentToAddBoundingBox2 WEST");
    // clipping to the chunk box
    MemoryRegion mem;
    BlockBox     chunk{100, 10, 200, 104, 19, 214};
    south.fill(mem, chunk, 0, 0, 0, 9, 0, 0, blocks::java("minecraft:stone"), blocks::java("minecraft:stone"), false);
    ctx.check(mem.size() == 5, "a fill is clipped to the chunk box (" + std::to_string(mem.size()) + " blocks)");
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
    auto     record   = blockentities::lootRecord(ctx.dimension(), chestPos);
    ctx.check(game::blockIdAt(*source, chestPos) == "minecraft:chest" && record && record->table == "gf_test:chests/fixture_chest",
              "the loot marker gave the chest its loot table");
    co_return;
});

// ---- processors ---------------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("structure.processors", [](Context& ctx) {
    auto const* t = templates::parsed(fixtureId());
    ctx.require(t != nullptr, "fixture");
    // every TF processor (and the vanilla ones TF's data uses) on the fixture, seeded
    std::vector<std::pair<std::string, Json>> cases{
        {"box_cutting", Json{{"processor_type", "twilightforest:box_cutting"}, {"boxes", Json::array({Json::array({0, 64, 0, 1, 65, 4})})}}},
        {"cobble_variants", Json{{"processor_type", "twilightforest:cobble_variants"}}},
        {"courtyard_terrace", Json{{"processor_type", "twilightforest:courtyard_terrace"}}},
        {"infest_blocks", Json{{"processor_type", "twilightforest:infest_blocks"}}},
        {"nagastone_variants", Json{{"processor_type", "twilightforest:nagastone_variants"}}},
        {"smart_grass", Json{{"processor_type", "twilightforest:smart_grass"}}},
        {"smooth_stone_variants", Json{{"processor_type", "twilightforest:smooth_stone_variants"}}},
        {"soft_replace", Json{{"processor_type", "twilightforest:soft_replace"}}},
        {"spawner_processor",
         Json{{"processor_type", "twilightforest:spawner_processor"},
              {"range", 6},
              {"start_delay_factor", 0.25},
              {"entity_width_max", 0.8},
              {"entities", Json::array({Json{{"data", {{"entity", {{"id", "minecraft:skeleton"}}}}}, {"weight", 3}},
                                        Json{{"data", {{"entity", {{"id", "minecraft:spider"}}}}}, {"weight", 1}}})}}},
        {"state_transfiguring",
         Json{{"processor_type", "twilightforest:state_transfiguring"},
              {"rules", Json::array({Json{{"input_predicate", {{"predicate_type", "minecraft:random_block_match"}, {"block", "minecraft:stone_bricks"}, {"probability", 0.5}}},
                                          {"location_predicate", {{"predicate_type", "minecraft:always_true"}}},
                                          {"output_state", {{"Name", "minecraft:mossy_stone_bricks"}}}}})}}},
        {"stone_brick_variants", Json{{"processor_type", "twilightforest:stone_brick_variants"}}},
        {"targeted_rot", Json{{"processor_type", "twilightforest:targeted_rot"}, {"blocks_to_rot", Json::array({state("minecraft:stone_bricks")})}, {"integrity", 0.5}}},
        {"update_marking", Json{{"processor_type", "twilightforest:update_marking"}, {"mark_updates", Json::array({"minecraft:oak_fence"})}}},
        {"vertical_decay", Json{{"processor_type", "twilightforest:vertical_decay"}, {"decay_blocks", Json::array({"minecraft:stone_bricks"})}, {"decay_chance", 0.3}}},
        {"web", Json{{"processor_type", "twilightforest:web"}}},
        {"wood_multiswizzle", Json{{"processor_type", "twilightforest:wood_multiswizzle"}, {"palettes", Json::array({Json::array({"minecraft:oak", "minecraft:dark_oak"})})}}},
        {"wood_swizzle", Json{{"processor_type", "twilightforest:wood_swizzle"}, {"target_palette", "minecraft:oak"}, {"replacement_palette", "minecraft:spruce"}}},
        {"rule", Json{{"processor_type", "minecraft:rule"},
                      {"rules", Json::array({Json{{"input_predicate", {{"predicate_type", "minecraft:block_match"}, {"block", "minecraft:cobblestone"}}},
                                                  {"location_predicate", {{"predicate_type", "minecraft:always_true"}}},
                                                  {"output_state", {{"Name", "minecraft:andesite"}}}}})}}},
        {"gravity", Json{{"processor_type", "minecraft:gravity"}, {"heightmap", "WORLD_SURFACE_WG"}, {"offset", 0}}},
        {"block_ignore", Json{{"processor_type", "minecraft:block_ignore"}, {"blocks", Json::array({state("minecraft:cobblestone")})}}},
    };
    auto const* targets = templates::parsed(processorTargetsId());
    ctx.require(targets != nullptr, "processor targets fixture");
    // the fixture at kOrigin, the processor targets 10 blocks east, over a prepared world
    struct Run {
        std::string               hash;
        std::vector<templates::Placed> placed; ///< the targets' placement
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
            ctx.require(templates::placeInWorld(i == 0 ? *t : *targets, mem, p, random, nullptr, i == 1 ? &run.placed : nullptr), "placed");
        }
        run.hash = mem.hash(BlockBox{-2, 62, -2, 17, 70, 7});
        return run;
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
    // spawner_processor: TF always rolls the spawn data (and the delay from MinSpawnDelay 200)
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
    // the processors that change nothing here: update_marking (Bedrock updates shapes itself) and,
    // without TF's block skeleton, nagastone_variants
    ctx.check(runs["update_marking"].hash == plain.hash, "update_marking places the template unchanged");
    if (blocks::isRegisteredBlock("twilightforest:etched_nagastone")) {
        int variants = 0;
        for (auto const& p : runs["nagastone_variants"].placed)
            if (p.block && (blocks::id(*p.block).find("mossy") != std::string::npos || blocks::id(*p.block).find("cracked") != std::string::npos))
                variants++;
        ctx.check(variants > 0, "nagastone_variants turns some nagastone mossy or cracked (" + std::to_string(variants) + ")");
    } else {
        ctx.check(runs["nagastone_variants"].hash == plain.hash, "nagastone_variants leaves vanilla blocks alone");
        ctx.info("TF's nagastone blocks are not registered (block skeleton): nagastone_variants checked on vanilla blocks only");
    }
    ctx.check(processorTypes().size() >= 24, "17 TF + 7 vanilla processor types registered (" + std::to_string(processorTypes().size()) + ")");
});

// ---- TF marker handlers and jigsaw pieces ----------------------------------------------------------------------------

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
    // a TF marker handler list in the shape of camp_marker_handlers, with vanilla blocks
    Json handlers = Json::array({Json{
        {"type", "twilightforest:rotation"},
        {"rotated",
         {{"type", "twilightforest:handler_switch"},
          {"element_processor",
           {{"seat",
             {{"type", "twilightforest:block_placement"},
              {"block_state",
               {{"type", "minecraft:weighted_state_provider"},
                {"entries", Json::array({Json{{"data", state("minecraft:oak_stairs", {{"facing", "north"}})}, {"weight", 1}},
                                         Json{{"data", state("minecraft:air")}, {"weight", 1}}})}}}}},
            {"lamp", {{"type", "twilightforest:loot"}, {"loot_block", {{"type", "minecraft:simple_state_provider"}, {"state", state("minecraft:barrel", {{"facing", "up"}})}}}, {"loot_table", "gf_test:chests/fixture_chest"}}}}}}}}});
    markers::addList("gf_test:structure/handlers", handlers);
    pools::addDefinition("gf_test:structure/jig/start",
                         Json{{"gf_test:structure/jig/start", {{"weight", 1}, {"marker_handlers", "gf_test:structure/handlers"}}}});
    pools::addDefinition("gf_test:structure/jig/room_a", Json{{"gf_test:structure/jig/rooms", 3}});
    pools::addDefinition("gf_test:structure/jig/room_b",
                         Json{{"gf_test:structure/jig/rooms",
                               {{"weight", 2},
                                {"randomized_processors",
                                 Json::array({Json::array({Json{{"data", {{"processor_type", "twilightforest:wood_swizzle"}, {"target_palette", "minecraft:oak"},
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
    auto* stones = selectors::get("twilightforest:stronghold_stones");
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
        placed += speleothems::place(mem, BlockPos{-6 + i * 2, 79, 0}, "twilightforest:large_hollow_hill", random, true) ? 1 : 0;
        placed += speleothems::place(mem, BlockPos{-6 + i * 2, 63, 4}, "twilightforest:troll_cave", random, false) ? 1 : 0;
    }
    ctx.check(placed > 0, "speleothems grow (" + std::to_string(placed) + ")");
    checkGolden(ctx, "speleothems", mem.hash(BlockBox{-8, 60, -8, 8, 84, 8}));
    auto lattice = speleothems::lattice("twilightforest:small_hollow_hill");
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
    bases::HollowHill hill("twilightforest:TFHill", 0, 1, 0, 0, 0, "twilightforest:small_hollow_hill");
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
    bases::HollowHill back("twilightforest:TFHill", 0, 1, 0, 0, 0, "x");
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
    ctx.check(types["twilightforest:TFLTWin"] > 0, "wings grow from the main tower");
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
        if (p->typeId == "twilightforest:TFLTWin") {
            auto&           wing = static_cast<bases::TowerWing&>(*p);
            auto            saved = wing.save();
            bases::TowerWing copy("twilightforest:TFLTWin", 0, 0, 0, 0, 3, 6, Orientation::North);
            copy.load(saved);
            ctx.check(copy.save() == saved, "a tower wing saves and loads back");
            break;
        }
});

GF_REGISTER_SELFTEST("structure.component_helpers", [](Context& ctx) {
    MemoryRegion mem;
    TestPiece    piece(BlockBox{0, 64, 0, 9, 70, 9}, Orientation::East);
    piece.surroundBlockCardinalRotated(mem, blocks::java("minecraft:oak_stairs"), 4, 1, 4, piece.bbox);
    piece.placeTreasureAtCurrentPosition(mem, 2, 1, 2, "twilightforest:hill_1", piece.bbox);
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

} // namespace

GF_REGISTER_SELFTEST("structure.paintings", [](Context& ctx) -> ll::coro::CoroTask<> {
    // TF's spot check on a scratch region: blocks in the painting's box refuse it
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
        ctx.check(paintings::ofSizeOrSmaller("twilightforest:tower_paintings", 1, 1).size() == 8, "eight 1x1 tower paintings");
    }
    // in the world: a wall, a painting in front of it, then the marker handler next to it
    auto area = co_await ctx.flatArea(5735, 10, 6);
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5735)), "test area loaded");
    auto* source = ctx.region();
    ctx.require(source != nullptr, "block source");
    BlockBox wallBox{area.minX + 1, area.minY, area.minZ + 1, area.minX + 8, area.minY + 3, area.minZ + 1};
    int      before = 0;
    {
        BlockSourceRegion region(*source, BlockPos{area.minX, area.minY, area.minZ});
        for (int x = wallBox.minX; x <= wallBox.maxX; x++)
            for (int y = wallBox.minY; y <= wallBox.maxY; y++) (void)region.setBlock(BlockPos{x, y, wallBox.minZ}, blocks::java("minecraft:stone"), 2);
        before = static_cast<int>(paintingsIn(*source, area).size());
        BlockPos front{area.minX + 2, area.minY + 1, area.minZ + 2};
        ctx.check(paintings::tryHang(region, front, Orientation::South, paintings::byId("minecraft:kebab")), "a painting hangs on the wall");
        ctx.check(!paintings::tryHang(region, front, Orientation::South, paintings::byId("minecraft:aztec")), "a second painting on the same spot is refused");
        BlockPos noWall{area.minX + 2, area.minY + 1, area.minZ + 5};
        // TF's check passes without a wall (TF hangs it and lets it drop); the game hangs nothing
        ctx.check(paintings::tryHang(region, noWall, Orientation::South, paintings::byId("minecraft:kebab")), "TF's spot check ignores the wall");
        ctx.check(paintingsIn(*source, BlockBox{noWall.x, noWall.y, noWall.z, noWall.x, noWall.y, noWall.z}).empty(), "no painting hangs without a wall");
        // the camp's painting marker handler: LichBossRoom.placePainting(pos, random, 1, placeable, 1, 1, rotation.rotate(NORTH))
        JavaRandom random(77);
        Json       handler{{"type", "twilightforest:painting"}, {"paintings", "minecraft:placeable"}};
        BlockPos   marker{area.minX + 5, area.minY + 1, area.minZ + 2};
        ctx.check(markers::handle(handler, "painting", marker, region, random, area, Rotation::Clockwise180), "the painting marker hangs a 1x1 painting");
    }
    co_await ctx.ticks(2);
    source = ctx.region();
    ctx.require(source != nullptr, "block source");
    auto hung = paintingsIn(*source, area);
    ctx.check(static_cast<int>(hung.size()) - before == 2, "two paintings hang (" + std::to_string(hung.size() - before) + ")");
    bool kebab = false;
    for (auto* a : hung) {
        auto const* motif = static_cast<Painting*>(a)->mMotif;
        if (motif && motif == &Motif::mKebab()) kebab = true;
    }
    ctx.check(kebab, "the hung painting shows the requested motive (kebab)");
    for (auto* a : hung) a->despawn();
});

// ---- TF marker handlers and template markers in the world --------------------------------------------------------------

GF_REGISTER_SELFTEST("structure.markers", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto area = co_await ctx.flatArea(5750, 8, 4);
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5750)), "test area loaded");
    auto* source = ctx.region();
    ctx.require(source != nullptr, "block source");
    int        dim = static_cast<int>(source->getDimensionId());
    JavaRandom random(3);
    BlockPos   lootPos{area.minX + 1, area.minY, area.minZ + 1};
    BlockPos   rackPos{area.minX + 3, area.minY, area.minZ + 1};
    BlockPos   notRack{area.minX + 5, area.minY, area.minZ + 1};
    {
        BlockSourceRegion region(*source, lootPos);
        // twilightforest:loot: the container, then its loot table (RandomizableContainer)
        Json loot{{"type", "twilightforest:loot"},
                  {"loot_block", {{"type", "minecraft:simple_state_provider"}, {"state", state("minecraft:barrel", {{"facing", "up"}})}}},
                  {"loot_table", "gf_test:chests/fixture_chest"}};
        ctx.check(markers::handle(loot, "", lootPos, region, random, area, Rotation::None), "the loot marker places its container");
        ctx.check(game::blockIdAt(*source, lootPos) == "minecraft:barrel", "a barrel stands");
        auto record = blockentities::lootRecord(dim, lootPos);
        ctx.check(record && record->table == "gf_test:chests/fixture_chest", "the barrel got the loot table");
        // twilightforest:drying_rack: only a DryingRackBlock is placed
        Json notARack{{"type", "twilightforest:drying_rack"},
                      {"drying_rack_block", {{"type", "minecraft:simple_state_provider"}, {"state", state("minecraft:oak_planks")}}},
                      {"loot_table", "twilightforest:camp_drying_rack"}};
        ctx.check(!markers::handle(notARack, "", notRack, region, random, area, Rotation::None) && game::blockIdAt(*source, notRack) == "minecraft:air",
                  "the drying rack marker refuses a block that is not a drying rack");
        if (blocks::isRegisteredBlock("twilightforest:birch_drying_rack")) {
            Json rack{{"type", "twilightforest:drying_rack"},
                      {"drying_rack_block",
                       {{"type", "minecraft:simple_state_provider"}, {"state", state("twilightforest:birch_drying_rack", {{"facing", "north"}})}}},
                      {"loot_table", "twilightforest:camp_drying_rack"}};
            ctx.check(markers::handle(rack, "", rackPos, region, random, area, Rotation::Clockwise90) &&
                          game::blockIdAt(*source, rackPos) == "twilightforest:birch_drying_rack",
                      "the drying rack marker places the rack (and fills it from its loot table)");
        } else {
            ctx.info("twilightforest:birch_drying_rack is not registered (block skeleton): drying rack placement checked by its refusal only");
        }
    }
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
                                 Json::array({Json::array({Json{{"data", {{"processor_type", "twilightforest:wood_swizzle"}, {"target_palette", "minecraft:oak"},
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
});

// ---- spawned mobs scaled by entity_width_max ---------------------------------------------------------------------------

GF_REGISTER_SELFTEST("structure.spawner_scale", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto area = co_await ctx.flatArea(5765, 8, 6);
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5765)), "test area loaded");
    auto* source = ctx.region();
    ctx.require(source != nullptr, "block source");
    auto level = ll::service::getLevel();
    ctx.require(level != nullptr, "level");
    Vec3 at{static_cast<float>(area.minX) + 4.5f, static_cast<float>(area.minY), static_cast<float>(area.minZ) + 4.5f};
    // a cow (test worlds may be peaceful): the scale is the same for every mob
    auto* mob = level->getSpawner().spawnMob(*source, ActorDefinitionIdentifier("minecraft:cow"), nullptr, at, false, false, false);
    ctx.require(mob != nullptr, "a cow spawns");
    float before = mob->getAABB().max.x - mob->getAABB().min.x;
    float scale  = 0.8f / 1.4f;
    ctx.check(blockentities::applyScale(*mob, scale), "the scale applies");
    float after = mob->getAABB().max.x - mob->getAABB().min.x;
    ctx.check(std::abs(after - before * scale) < 0.05f, "the cow's box shrinks to " + std::to_string(after) + " (from " + std::to_string(before) + ")");
    auto scaledByHand = mob->getOrCreateUniqueID().rawID;
    mob->despawn();
    // end to end: a spawner placed with an entity_scale record scales the mobs it spawns
    BlockPos spawnerPos{area.minX + 4, area.minY, area.minZ + 4};
    {
        BlockSourceRegion region(*source, spawnerPos);
        blockentities::spawner(region, spawnerPos, "minecraft:cow",
                               Json{{"entity_scale", 0.5f}, {"Delay", 1}, {"MinSpawnDelay", 10}, {"MaxSpawnDelay", 20}, {"SpawnCount", 1},
                                    {"SpawnRange", 2}, {"RequiredPlayerRange", 16}, {"MaxNearbyEntities", 6}});
    }
    BlockBox      near{area.minX, area.minY - 1, area.minZ, area.maxX, area.minY + 4, area.maxZ};
    AABB          range{Vec3{static_cast<float>(near.minX), static_cast<float>(near.minY), static_cast<float>(near.minZ)},
                        Vec3{static_cast<float>(near.maxX + 1), static_cast<float>(near.maxY + 1), static_cast<float>(near.maxZ + 1)}};
    std::optional<float> width;
    for (int i = 0; i < 200 && !width; i += 5) {
        co_await ctx.ticks(5);
        (void)ctx.player(); // keep the test player near the spawner (RequiredPlayerRange)
        source = ctx.region();
        if (!source) break;
        for (auto* a : source->getEntities(range, 0.0f, ActorType::Cow))
            if (a && a->isType(ActorType::Cow) && a->getOrCreateUniqueID().rawID != scaledByHand) width = a->getAABB().max.x - a->getAABB().min.x;
    }
    if (width) ctx.info("a cow from the spawner: width " + std::to_string(*width));
    if (source)
        for (auto* a : source->getEntities(range, 0.0f, ActorType::Cow))
            if (a) a->despawn();
    if (!width) ctx.info("the spawner spawned no cow within 200 ticks (spawn conditions): spawned-mob scaling not observed");
    else ctx.check(*width < 0.9f * 0.75f, "a cow from the scaled spawner is scaled (width " + std::to_string(*width) + ")");
});

// ---- queued work survives a restart (tools/e2e/server/scenarios/structure.ps1) -----------------------------------------

GF_REGISTER_SELFTEST("structure.pending_read", [](Context& ctx) -> ll::coro::CoroTask<> {
    // `/twilightforest structure_util pending_probe` queued a job in an unloaded chunk before the
    // restart; the job (persisted in gf:structure:pending) runs once its chunk is loaded
    auto probe = game::db::get("gf:structure:probe");
    if (!probe) {
        ctx.info("no pending probe (structure_util pending_probe did not run): nothing to check");
        co_return;
    }
    auto nonce = probe->value("nonce", std::string{});
    auto pos   = probe->value("pos", Json::array());
    game::db::erase("gf:structure:probe");
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
