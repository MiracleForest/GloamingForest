// U1 `biomes`, client build selftests (dx 100-199, rule R12).
//
//   biomes.grass_tint  (client mod only, spec section 10) upstream's position-dependent grass and foliage
//                      colours of the spooky forest, dark forest centre and enchanted forest, as the
//                      tints compute them from a world's stored biomes and from F12's registry;
//                      the forest's colour-map colours, the snowy forest's white leaves (without
//                      Bedrock's seasons map), upstream's water colours from the game's water policy
//                      (the drawn water is measured by the client scenario's pool screenshots).

#ifdef GF_CLIENT

#include <format>
#include <optional>
#include <string>

#include "mc/client/player/LocalPlayer.h"
#include "mc/client/world/level/biome/biome_color_sampling/TessellationPolicy.h"
#include "mc/deps/core/math/Color.h"
#include "mc/world/level/block/TintMethod.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/registry/BiomeRegistry.h"
#include "client/Client.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/biome/biome_color_sampling/BiomeColorSampling.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockRenderLayer.h"
#include "mc/world/level/block/BlockType.h"

#include "client/biomes/Tints.h"
#include "core/biomes/Colors.h"
#include "feature/biomes/Biomes.h"
#include "feature/ext/biomes/Biomes.h"
#include "feature/ext/ui/Client.h"
#include "feature/worldgen/Worldgen.h"
#include "game/Blocks.h"
#include "selftest/SelfTest.h"

namespace gf::client::biomes {

namespace {

namespace colors = core::biomes::colors;
using selftest::Context;

/// Fixed game colours (the biome files' static colours, as the game would blend them inside a biome).
class FixedColors final : public tint::GameColors {
public:
    FixedColors(unsigned grass, unsigned foliage) : mGrass(grass), mFoliage(foliage) {}
    std::optional<unsigned> foliage() const override { return mFoliage; }
    std::optional<unsigned> grass() const override { return mGrass; }
    std::optional<unsigned> water() const override { return 0x3F76E4u; }

private:
    unsigned mGrass, mFoliage;
};

/// A block column well inside `biome` (a 7x7-quart square of it) near the origin, at the surface
/// quart layer; nullopt when none within 4096 blocks.
std::optional<BlockPos> inside(std::string const& biome) {
    auto p = feature::biomes::provider();
    if (!p) return std::nullopt;
    for (int ring = 0; ring <= 1024; ring += 4) {
        for (int i = -ring; i <= ring; i += 4) {
            for (auto [qx, qz] : {std::pair{i, -ring}, {i, ring}, {-ring, i}, {ring, i}}) {
                bool ok = true;
                for (int dz = -3; dz <= 3 && ok; ++dz)
                    for (int dx = -3; dx <= 3 && ok; ++dx) ok = p->mainBiome(qx + dx, qz + dz) == biome;
                if (ok) return BlockPos{qx * 4 + 2, 40, qz * 4 + 2};
            }
        }
    }
    return std::nullopt;
}

} // namespace

GF_REGISTER_SELFTEST("biomes.grass_tint", [](Context& ctx) -> ll::coro::CoroTask<> {
    ctx.require(co_await ctx.inGloaming(), "in the Gloaming Forest");
    FixedColors const plain(0x79C05A, 0x59AE30); // a forest-like game colour: never a marker
    struct Case {
        char const* biome;
        unsigned    staticGrass, staticFoliage; ///< the client biome file's colours
    } const cases[] = {
        {"gloamingforest:spooky_forest", 0xC45123, 0xFF8501},
        {"gloamingforest:dark_forest_center", 0x554114, 0xE94E14},
        {"gloamingforest:enchanted_forest", 0x00FF80, 0x00FF80},
    };
    for (auto const& c : cases) {
        // every surface biome lies within 4096 blocks of the origin (biomes.survey finds them all)
        auto at = inside(c.biome);
        ctx.require(at.has_value(), std::string(c.biome) + " is found within 4096 blocks");
        core::GlobalPos gp{ctx.dimension(), at->x, at->y, at->z};
        ctx.require(co_await ctx.ensureLoaded(gp), std::string(c.biome) + " chunk loaded");
        auto* region = ctx.region();
        ctx.require(region != nullptr, "the Gloaming Forest block source");
        int y = at->y;
        // the stored biomes of the blend (5x5 at the default radius) are this biome
        bool uniform = true;
        for (int dz = -2; dz <= 2; ++dz)
            for (int dx = -2; dx <= 2; ++dx) uniform = uniform && worldgen::biomeAt(BlockPos{at->x + dx, y, at->z + dz}) == c.biome;
        ctx.require(uniform, std::format("{} surrounds {} {} {}", c.biome, at->x, y, at->z));
        auto expect = [&](bool foliage) {
            return static_cast<unsigned>(colors::blend(at->x, at->z, 2, [&](int x, int z) {
                auto v = foliage ? colors::foliage(c.biome, 0x00FFFF, x, z) : colors::grass(c.biome, 0x00FFFF, x, z);
                return v.value_or(0);
            }));
        };
        unsigned eg = expect(false), ef = expect(true);
        // from the world's biomes (what the vanilla tint hook passes: the tessellated block source),
        // whatever the game's colour is
        unsigned g = grassAt(region, *at, plain), f = foliageAt(region, *at, plain);
        ctx.check(g == eg, std::format("{} grass from the stored biomes: #{:06X} (upstream #{:06X})", c.biome, g, eg));
        ctx.check(f == ef, std::format("{} foliage from the stored biomes: #{:06X} (upstream #{:06X})", c.biome, f, ef));
        // without a block source: recognised by the game's static colour, through F12's registry
        FixedColors const marker(c.staticGrass, c.staticFoliage);
        auto              rg = render::tintOf("minecraft:grass_block", at->x, y, at->z, marker);
        auto              rf = render::tintOf("minecraft:oak_leaves", at->x, y, at->z, marker);
        ctx.check(rg && *rg == eg, std::format("{} grass block through the tint registry: #{:06X} (upstream #{:06X})", c.biome, rg.value_or(0), eg));
        ctx.check(rf && *rf == ef, std::format("{} oak leaves through the tint registry: #{:06X} (upstream #{:06X})", c.biome, rf.value_or(0), ef));
        ctx.info(std::format("{} at {} {}: grass #{:06X}, foliage #{:06X}", c.biome, at->x, at->z, eg, ef));
    }
    // upstream's known values: the dark forest centre's leaves are always #E94E14 (BiomeColorAlgorithms:
    // (noise + 1) / 2 is never below -0.1), the spooky forest's grass is a blend of upstream's red and green
    ctx.check(colors::darkForestCenterFoliage(123.0, -456.0) == 0xE94E14, "the dark forest centre's foliage is #E94E14");
    // a biome without a modifier: its own colour-map colour in every column (not the game's colour)
    std::optional<BlockPos> forestAt = inside("gloamingforest:forest");
    ctx.require(forestAt.has_value(), "the forest is found within 4096 blocks");
    {
        core::GlobalPos gp{ctx.dimension(), forestAt->x, forestAt->y, forestAt->z};
        ctx.require(co_await ctx.ensureLoaded(gp), "the forest chunk loaded");
        unsigned g = grassAt(ctx.region(), *forestAt, plain), f = foliageAt(ctx.region(), *forestAt, plain);
        ctx.check(g == 0x8DB870, std::format("the forest's grass is its colour-map colour #8DB870 (#{:06X})", g));
        ctx.check(f == 0x71A74D, std::format("the forest's foliage is its colour-map colour #71A74D (#{:06X})", f));
    }
    // the snowy forest: upstream's white grass and leaves; the leaves are drawn without Bedrock's seasons
    // map (as in the forest), so the white tint is a colour there
    {
        auto at = inside("gloamingforest:snowy_forest");
        ctx.require(at.has_value(), "the snowy forest is found within 4096 blocks");
        core::GlobalPos gp{ctx.dimension(), at->x, at->y, at->z};
        ctx.require(co_await ctx.ensureLoaded(gp), "the snowy forest chunk loaded");
        auto*    region = ctx.region();
        unsigned f = foliageAt(region, *at, plain), g = grassAt(region, *at, plain);
        ctx.check(g == 0xFFFFFF, std::format("the snowy forest's grass is upstream's white (#{:06X})", g));
        ctx.check(f == 0xFFFFFF, std::format("the snowy forest's leaves are upstream's white (#{:06X})", f));
        Block const* leaves = game::blockWith("minecraft:oak_leaves");
        ctx.require(leaves != nullptr, "oak leaves");
        ctx.check(!leaves->getBlockType().isSeasonTinted(*leaves, *region, *at), "oak leaves in the snowy forest are not season tinted");
        auto snowy  = static_cast<int>(leaves->getBlockType().getRenderLayer(*leaves, *region, *at));
        auto forest = static_cast<int>(leaves->getBlockType().getRenderLayer(*leaves, *region, *forestAt));
        ctx.check(snowy == forest, std::format("oak leaves draw in the forest's render layer in the snowy forest ({} / {})", snowy, forest));
    }
    // upstream's water colours (BiomeSpecialEffects#waterColor) from the policy the client's water mesh is
    // tessellated with: the hook sits on the object the game uses. Whether the drawn water shows them
    // is measured on the screenshots by the client scenario (its pools), not here.
    Block const* water = game::blockWith("minecraft:water");
    ctx.require(water != nullptr, "the water block");
    for (char const* biome : {"gloamingforest:fire_swamp", "gloamingforest:swamp", "gloamingforest:spooky_forest", "gloamingforest:dense_forest", "gloamingforest:forest"}) {
        auto at = inside(biome);
        ctx.require(at.has_value(), std::string(biome) + " is found within 4096 blocks");
        core::GlobalPos gp{ctx.dimension(), at->x, at->y, at->z};
        ctx.require(co_await ctx.ensureLoaded(gp), std::string(biome) + " chunk loaded");
        auto&      region = *ctx.region();
        mce::Color c      = BiomeColorSampling::getTessellationPolicy(TintMethod::Water).get(*water, region, *at, nullptr);
        unsigned   got    = (static_cast<unsigned>(c.r * 255.f + 0.5f) << 16) | (static_cast<unsigned>(c.g * 255.f + 0.5f) << 8) | static_cast<unsigned>(c.b * 255.f + 0.5f);
        unsigned   want   = static_cast<unsigned>(*colors::water(biome));
        ctx.check(got == want, std::format("{}: the water policy gives #{:06X} (upstream #{:06X})", biome, got, want));
    }
    // the client level's own biomes: which environment attributes the client biome files gave them
    // (an upstream biome against a vanilla one), and the water colour the client was sent
    if (auto* player = client::currentPlayer()) {
        auto& reg = player->getLevel().getBiomeRegistry();
        for (char const* id : {"gloamingforest:fire_swamp", "gloamingforest:forest", "minecraft:swampland", "minecraft:plains"}) {
            auto const* b = reg.lookupByName(id);
            if (!b) {
                ctx.info(std::format("client biome {}: not in the client's registry", id));
                continue;
            }
            std::string keys;
            for (auto const& [k, v] : *b->mEnvironmentAttributes) keys += (keys.empty() ? "" : ",") + k;
            ctx.info(std::format("client biome {}: map water #{:08X}, attributes [{}]", id, static_cast<unsigned>(static_cast<int>(b->mMapWaterColor)), keys));
        }
    }
    // the client draws the world: the vanilla tint hook applied the registry to vanilla blocks
    // (the client may not have drawn an upstream chunk yet: the e2e scenario checks the hook's log lines)
    ctx.info(std::format("the vanilla tint hook applied {} tints so far", gf::biomes::ext::vanillaTintsApplied()));
});

} // namespace gf::client::biomes

#endif
