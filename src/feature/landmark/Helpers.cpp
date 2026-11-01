// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Shared helpers of the landmark unit (F6): names, entity spawning, particles, and the test
// fixture landmark. The fixture's lock and hint copy the lich tower's record of
// The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757,
// src/generated/resources/data/twilightforest/worldgen/structure/lich_tower.json; LGPL-2.1-or-later).
#include <cctype>
#include <mutex>
#include <optional>
#include <set>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
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

namespace gf::feature::landmark {

bool stubHit(std::string const& name) {
    for (auto const& s : core::stubCalls())
        if (s == name) return true;
    return false;
}

std::string structureName(std::string const& key, Player const* player) {
    auto        colon = key.find(':');
    std::string ns    = colon == std::string::npos ? "minecraft" : key.substr(0, colon);
    std::string path  = colon == std::string::npos ? key : key.substr(colon + 1);
    // fallback: "lich_tower" -> "Lich Tower"
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
    auto langKey = "structure." + ns + "." + path;
    return player ? trFor(*player, langKey, pretty) : tr(langKey, pretty);
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

void emitParticle(int dim, std::string const& id, float x, float y, float z, bool onlyVanilla) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto dimension = level->getDimension(DimensionType(dim)).lock();
    if (!dimension) return;
    if (!onlyVanilla) {
        fx::particle(*dimension, id, Vec3{x, y, z});
        static bool const fxIsStub = stubHit("fx.particle");
        if (!fxIsStub) return;
    }
    // the fx contract is still the F0 stub (or only vanilla clients are wanted): the particle packet
    level->forEachPlayer([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) != dim) return true;
        if (onlyVanilla && net::isCompanion(p)) return true;
        if (p.distanceToSqr(Vec3{x, y, z}) > 64.0f * 64.0f) return true;
        SpawnParticleEffectPacket packet(SpawnParticleEffectPacketPayload(Vec3{x, y, z}, id, static_cast<uchar>(dim), std::nullopt));
        packet.sendTo(p);
        return true;
    });
}

// ---- the test fixture landmark -----------------------------------------------------------------------

namespace {

constexpr char kFixtureKey[] = "gf_test:landmark_fixture";

/// A 7x7x7 stone brick shell (the protected main piece) and a 7x1x3 porch in front of it (not
/// protected, through Generator::pieceProtected).
class FixturePiece final : public structure::Piece {
public:
    FixturePiece(BlockBox box, bool shell) : Piece(shell ? "gf_test:landmark_fixture_shell" : "gf_test:landmark_fixture_porch"), mShell(shell) {
        bbox = box;
    }
    void postProcess(worldgen::Region& region, worldgen::RandomSource&, BlockBox const& chunkBox, BlockPos const&) override {
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

void registerFixture() {
    if (info(kFixtureKey)) return;
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
    setGenerator(kFixtureKey, Generator{
        .layout =
            [](gf::landmark::LayoutContext& ctx) {
                ctx.pieces.push_back(std::make_unique<FixturePiece>(shellBox(ctx.start), true));
                ctx.pieces.push_back(std::make_unique<FixturePiece>(porchBox(ctx.start), false));
                return true;
            },
        .terraform      = {},
        .spawnIndex     = {},
        .pieceProtected = [](Start const& s, BlockPos const& pos) { return shellBox(s).contains(pos.x, pos.y, pos.z); },
    });
}

} // namespace gf::feature::landmark
