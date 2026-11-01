// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// Selftests of the structure kit (rule R12: "structure.*", testPos dx 5700..5799).
//
// Placements are checked twice: on an in-memory region at a fixed origin (MemoryRegion), whose
// golden volume hashes do not depend on where the test area is, and in the world, whose
// snapshotVolume must equal the in-memory hash of the same placement (rule R19 golden hashes).
#include <algorithm>
#include <climits>
#include <map>
#include <unordered_map>

#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "core/VolumeHash.h"
#include "feature/structure/Region.h"
#include "feature/structure/bases/BasesInternal.h"
#include "feature/structure/bases/HollowHill.h"
#include "feature/structure/bases/Maze.h"
#include "feature/structure/bases/Tower.h"
#include "game/Blocks.h"
#include "selftest/SelfTest.h"

namespace gf::structure::selftests {

namespace {

using selftest::Context;

// ---- an in-memory region -----------------------------------------------------------------------------------

class MemoryRegion final : public worldgen::Region {
public:
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
                    h.add(x - box.minX, y - box.minY, z - box.minZ, b.getTypeName(), game::statesOf(b));
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
    ctx.check(bases::ComponentOld::getComponentToAddBoundingBox2(10, 20, 30, -1, 0, -2, 5, 6, 7, Orientation::West) == BlockBox::of(1, 20, 29, 12, 26, 35),
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
    ctx.check(ctx.snapshotVolume(box) == mem.hash(memBox), "the world and the in-memory placement match");
    checkGolden(ctx, "template_world_cw90", mem.hash(memBox));
    co_await ctx.ticks(2);
    BlockPos spawnerPos = templates::relative(templates::Placement{origin, origin, settings, BlockPos{0, 0, 0}}, BlockPos{2, 1, 2});
    spawnerPos          = BlockPos{spawnerPos.x + origin.x, spawnerPos.y + origin.y, spawnerPos.z + origin.z};
    ctx.check(game::blockIdAt(*source, spawnerPos) == "minecraft:mob_spawner", "the spawner stands");
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
    for (auto const& [name, config] : cases) {
        MemoryRegion mem;
        // a little world for the processors that look at it (soft_replace, smart_grass, gravity, courtyard)
        for (int x = -2; x < 8; x++)
            for (int z = -2; z < 8; z++) {
                (void)mem.setBlock(BlockPos{x, 63, z}, blocks::java((x + z) % 3 == 0 ? "minecraft:podzol" : "minecraft:dirt"), 3);
                if ((x * 7 + z) % 5 == 0) (void)mem.setBlock(BlockPos{x, 64 + (x + z) % 3, z}, blocks::java("minecraft:oak_fence"), 3);
            }
        templates::Placement p;
        p.offset              = kOrigin;
        p.pivotPos            = kOrigin;
        p.settings.processors = {config.dump()};
        p.settings.seed       = 0x5eed;
        JavaRandom random(7);
        ctx.require(templates::placeInWorld(*t, mem, p, random), "placed with " + name);
        checkGolden(ctx, "processor_" + name, mem.hash(BlockBox{-2, 62, -2, 7, 70, 7}));
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
    auto       a = jigsaw::assemble("gf_test:structure/jig/start", kOrigin, 4, r1);
    auto       b = jigsaw::assemble("gf_test:structure/jig/start", kOrigin, 4, r2);
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
});

} // namespace gf::structure::selftests
