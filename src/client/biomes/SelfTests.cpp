// U1 `biomes`, client build selftests (dx 100-199, rule R12).
//
//   biomes.grass_tint  (client mod only, spec section 10) TF's position-dependent grass and foliage
//                      colours of the spooky forest, dark forest centre and enchanted forest, as the
//                      tints compute them from a world's stored biomes and from F12's registry;
//                      the vanilla tint hook (feature/ext/biomes/VanillaTint.cpp) drew them.

#ifdef GF_CLIENT

#include <format>
#include <optional>
#include <string>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

#include "client/biomes/Tints.h"
#include "core/biomes/Colors.h"
#include "feature/biomes/Biomes.h"
#include "feature/ext/biomes/Biomes.h"
#include "feature/ext/ui/Client.h"
#include "feature/worldgen/Worldgen.h"
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
    ctx.require(co_await ctx.inTwilight(), "in the Twilight Forest");
    FixedColors const plain(0x79C05A, 0x59AE30); // a forest-like game colour: never a marker
    struct Case {
        char const* biome;
        unsigned    staticGrass, staticFoliage; ///< the client biome file's colours
    } const cases[] = {
        {"twilightforest:spooky_forest", 0xC45123, 0xFF8501},
        {"twilightforest:dark_forest_center", 0x554114, 0xE94E14},
        {"twilightforest:enchanted_forest", 0x00FF80, 0x00FF80},
    };
    for (auto const& c : cases) {
        auto at = inside(c.biome);
        if (!at) {
            ctx.info(std::string(c.biome) + " not found within 4096 blocks for this seed: skipped");
            continue;
        }
        core::GlobalPos gp{ctx.dimension(), at->x, at->y, at->z};
        ctx.require(co_await ctx.ensureLoaded(gp), std::string(c.biome) + " chunk loaded");
        auto* region = ctx.region();
        ctx.require(region != nullptr, "the Twilight Forest block source");
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
        ctx.check(g == eg, std::format("{} grass from the stored biomes: #{:06X} (TF #{:06X})", c.biome, g, eg));
        ctx.check(f == ef, std::format("{} foliage from the stored biomes: #{:06X} (TF #{:06X})", c.biome, f, ef));
        // without a block source: recognised by the game's static colour, through F12's registry
        FixedColors const marker(c.staticGrass, c.staticFoliage);
        auto              rg = render::tintOf("minecraft:grass_block", at->x, y, at->z, marker);
        auto              rf = render::tintOf("minecraft:oak_leaves", at->x, y, at->z, marker);
        ctx.check(rg && *rg == eg, std::format("{} grass block through the tint registry: #{:06X} (TF #{:06X})", c.biome, rg.value_or(0), eg));
        ctx.check(rf && *rf == ef, std::format("{} oak leaves through the tint registry: #{:06X} (TF #{:06X})", c.biome, rf.value_or(0), ef));
        ctx.info(std::format("{} at {} {}: grass #{:06X}, foliage #{:06X}", c.biome, at->x, at->z, eg, ef));
    }
    // TF's known values: the dark forest centre's leaves are always #E94E14 (BiomeColorAlgorithms:
    // (noise + 1) / 2 is never below -0.1), the spooky forest's grass is a blend of TF's red and green
    ctx.check(colors::darkForestCenterFoliage(123.0, -456.0) == 0xE94E14, "the dark forest centre's foliage is #E94E14");
    // a biome without a position-dependent colour keeps the game's colour
    if (auto at = inside("twilightforest:forest")) {
        core::GlobalPos gp{ctx.dimension(), at->x, at->y, at->z};
        if (co_await ctx.ensureLoaded(gp)) {
            unsigned g = grassAt(ctx.region(), *at, plain);
            ctx.check(g == 0x79C05A, std::format("the forest keeps the game's grass colour (#{:06X})", g));
        }
    }
    // the client draws the world: the vanilla tint hook applied the registry to vanilla blocks
    ctx.check(gf::biomes::ext::vanillaTintsApplied() > 0, "the vanilla tint hook applied TF's tints to vanilla grass and leaves");
});

} // namespace gf::client::biomes

#endif
