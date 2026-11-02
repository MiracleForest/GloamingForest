// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Shared helpers of the landmark unit (F6): names, entity spawning, particles, and the test
// fixture landmark. The fixture's lock and hint copy the lich tower's record of
// The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757,
// src/generated/resources/data/twilightforest/worldgen/structure/lich_tower.json; LGPL-2.1-or-later).
#include <cctype>
#include <filesystem>
#include <mutex>
#include <optional>
#include <set>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/locale/I18n.h"
#include "mc/locale/Localization.h"
#include "mc/network/packet/SpawnParticleEffectPacket.h"
#include "mc/network/packet/SpawnParticleEffectPacketPayload.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/block/Block.h"

#include "core/Stub.h"
#include "feature/fx/Fx.h"
#include "feature/landmark/Internal.h"
#include "feature/net/Net.h"
#include "feature/spawn/Spawn.h"
#include "game/Blocks.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Main.h"

namespace gf::feature::landmark {

bool stubHit(std::string const& name) {
    for (auto const& s : core::stubCalls())
        if (s == name) return true;
    return false;
}

namespace {
/// "lich_tower" -> "Lich Tower".
std::string prettyPath(std::string const& path) {
    std::string pretty;
    bool        up = true;
    for (char c : path) {
        if (c == '_' || c == '/') {
            pretty += ' ';
            up = true;
        } else {
            pretty += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
            up = false;
        }
    }
    return pretty;
}
} // namespace

std::string structureName(std::string const& key, Player const* player) {
    // Component.translatable("structure.<ns>.<path>").getString(): a key without a translation shows
    // as itself (TF's structure.twilightforest.quest_island at a lake center, docs/spec/not-ported.md)
    auto [ns, path] = splitKey(key);
    auto langKey    = "structure." + ns + "." + path;
    return player ? trFor(*player, langKey, langKey) : tr(langKey, langKey);
}

std::string entityName(std::string const& id, Player const* player) {
    auto [ns, path] = splitKey(id);
    auto locale     = player ? localeOf(*player) : std::string{};
    // Java's key (EntityType#getDescriptionId): TF's names, imported from TF's lang files per locale
    auto javaKey = "entity." + ns + "." + path;
    if (hasTranslation(javaKey, locale)) return std::string(translate(javaKey, path, locale));
    // Bedrock's key: entity.<path>.name for vanilla entities, entity.<ns>:<path>.name otherwise, in the
    // game's language (vanilla's texts plus the resource packs' names PackDeployer appends on BDS)
    auto bedrockKey = "entity." + (ns == "minecraft" ? path : ns + ":" + path) + ".name";
    if (hasTranslation(bedrockKey, locale)) return std::string(translate(bedrockKey, path, locale));
    try {
        auto&                                i18n = getI18n();
        std::shared_ptr<Localization const> loc;
        if (!locale.empty()) loc = i18n.getLocaleFor(locale);
        if (!loc) loc = i18n.getCurrentLanguage();
        auto name = i18n.get(bedrockKey, loc);
        if (!name.empty() && name != bedrockKey) return name;
    } catch (...) {}
    return prettyPath(path);
}

Actor* spawnEntity(BlockSource& region, std::string const& id, float x, float y, float z, std::function<void(Actor&)> init) {
    if (auto* a = spawn::spawnMob(region, id, Vec3{x, y, z}, spawn::SpawnReason::Event, init)) return a;
    // the spawn contract is still the F0 stub: the engine's spawner directly
    static bool const contractIsStub = stubHit("spawn.spawnMob");
    if (!contractIsStub) return nullptr;
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorDefinitionIdentifier ident(id);
    Mob*                      mob = level->getSpawner().spawnMob(region, ident, nullptr, Vec3{x, y, z}, false, false, false);
    if (!mob) {
        static std::mutex                      mutex;
        static std::set<std::string>           warned;
        std::lock_guard                        lock(mutex);
        if (warned.insert(id).second) log().warn("landmark: entity {} could not be spawned (not registered?)", id);
        return nullptr;
    }
    if (init) init(*mob);
    return mob;
}

void emitParticles(int dim, std::string const& id, std::vector<std::array<float, 3>> const& points) {
    if (points.empty()) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    auto dimension = level->getDimension(DimensionType(dim)).lock();
    if (!dimension) return;
    for (auto const& p : points) fx::particle(*dimension, id, Vec3{p[0], p[1], p[2]});
    static bool const fxIsStub = stubHit("fx.particle");
    if (!fxIsStub) return;
    // the fx contract is still the F0 stub: the particle packets, to the players near the burst
    Vec3                  first{points[0][0], points[0][1], points[0][2]};
    std::vector<Player*>  near; // this call only (R16)
    level->forEachPlayer([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) == dim && p.distanceToSqr(first) <= 64.0f * 64.0f) near.push_back(&p);
        return true;
    });
    for (auto const& p : points) {
        SpawnParticleEffectPacket packet(SpawnParticleEffectPacketPayload(Vec3{p[0], p[1], p[2]}, id, static_cast<uchar>(dim), std::nullopt));
        for (auto* player : near) packet.sendTo(*player);
    }
}

// ---- the test fixture landmark -----------------------------------------------------------------------

namespace {

constexpr char kFixtureKey[]     = "gf_test:landmark_fixture";
constexpr char kOpenFixtureKey[] = "gf_test:landmark_fixture_open";

/// setFixtureProbe's function (the selftests' look at the random a piece is placed with).
std::mutex                                     gProbeMutex;
std::function<void(worldgen::RandomSource&, BlockBox const&)> gFixtureProbe;

/// A 7x7x7 stone brick shell (the protected main piece) and a 7x1x3 porch in front of it (not
/// protected, through Generator::pieceProtected).
class FixturePiece final : public structure::Piece {
public:
    FixturePiece(BlockBox box, bool shell) : Piece(shell ? "gf_test:landmark_fixture_shell" : "gf_test:landmark_fixture_porch"), mShell(shell) {
        bbox = box;
    }
    void postProcess(worldgen::Region& region, worldgen::RandomSource& random, BlockBox const& chunkBox, BlockPos const&) override {
        {
            std::lock_guard lock(gProbeMutex);
            if (gFixtureProbe) gFixtureProbe(random, bbox);
        }
        auto const* wall = game::blockWith(mShell ? "minecraft:stone_bricks" : "minecraft:oak_planks");
        if (!wall) return;
        for (int x = bbox.minX; x <= bbox.maxX; ++x)
            for (int y = bbox.minY; y <= bbox.maxY; ++y)
                for (int z = bbox.minZ; z <= bbox.maxZ; ++z) {
                    if (!chunkBox.contains(x, y, z)) continue;
                    bool edge = x == bbox.minX || x == bbox.maxX || y == bbox.minY || y == bbox.maxY || z == bbox.minZ || z == bbox.maxZ;
                    if (!mShell || edge) region.setBlock(BlockPos{x, y, z}, *wall);
                }
    }

private:
    bool mShell;
};

BlockBox shellBox(Start const& s) { return {s.center.x - 3, s.center.y, s.center.z - 3, s.center.x + 3, s.center.y + 6, s.center.z + 3}; }
BlockBox porchBox(Start const& s) { return {s.center.x - 3, s.center.y, s.center.z + 4, s.center.x + 3, s.center.y, s.center.z + 6}; }

} // namespace

void setFixtureProbe(std::function<void(worldgen::RandomSource& random, BlockBox const& pieceBox)> probe) {
    std::lock_guard lock(gProbeMutex);
    gFixtureProbe = std::move(probe);
}

bool registerFixture() {
    if (info(kFixtureKey)) return true;
    // only where the gf_test fixture data is deployed (development builds; a release build made with
    // GF_NO_TEST_PACKS=1 has no <mod>/data/gf_test, so /place, locate and genbook never see these)
    std::error_code ec;
    if (!std::filesystem::is_directory(Mod::getInstance().getSelf().getModDir() / "data" / "gf_test", ec)) return false;
    Info fixture = parseRecord(kFixtureKey, Json::parse(R"({
        "type": "twilightforest:progression",
        "advancements_required": ["twilightforest:progress_naga"],
        "decoration_clearance": {"adjust_structure_elevation": false, "allow_biome_surface_decorations": false,
                                 "allow_biome_underground_decorations": true, "allow_biome_vegetation": false,
                                 "chunk_clearance_radius": 0.0},
        "hint_creature": {"hint_mob": "twilightforest:kobold", "hint_item": {"id": "minecraft:written_book", "count": 1,
            "components": {"minecraft:written_book_content": {"title": {"raw": "twilightforest.book.lichtower"},
                "author": "twilightforest.book.author", "generation": 3, "resolved": true,
                "pages": [{"raw": "1"}, {"raw": "2"}, {"raw": "3"}, {"raw": "4"}]}}}},
        "controlled_spawns": {"ambient_spawns": [], "water_spawns": [],
            "labelled_monster_spawns": {"0": [{"type": "minecraft:zombie", "weight": 1, "minCount": 1, "maxCount": 1}]}},
        "step": "surface_structures"
    })"));
    fixture.traits.landmarkTag      = true;
    fixture.traits.controlledSpawns = true;
    registerInfo(std::move(fixture));
    // the same shape, conquerable without a lock (like TF's quest grove): never protects anything
    Info open = parseRecord(kOpenFixtureKey, Json::parse(R"({"type": "twilightforest:quest_grove", "step": "surface_structures"})"));
    open.traits.landmarkTag = true;
    registerInfo(std::move(open));
    auto layout = [](gf::landmark::LayoutContext& ctx) {
        ctx.pieces.push_back(std::make_unique<FixturePiece>(shellBox(ctx.start), true));
        ctx.pieces.push_back(std::make_unique<FixturePiece>(porchBox(ctx.start), false));
        return true;
    };
    setGenerator(kFixtureKey, Generator{
        .layout         = layout,
        .terraform      = {},
        .spawnIndex     = {},
        .pieceProtected = [](Start const& s, BlockPos const& pos) { return shellBox(s).contains(pos.x, pos.y, pos.z); },
    });
    setGenerator(kOpenFixtureKey, Generator{.layout = layout});
    return true;
}

} // namespace gf::feature::landmark
