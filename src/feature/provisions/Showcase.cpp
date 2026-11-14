// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// U10 (provisions): the client e2e's showcase (provisions.real_arm / provisions.real_clear). It gives
// every real player U10's items (the foods, stale bread, Experiment 115 and the discs) and frosted II,
// and builds a row of Experiment 115 in its 16 states in front of them, for the screenshots of
// tools/e2e/client/scenarios/provisions.ps1. Only players carrying the entity tag
// gf_provisions_showcase (the scenario adds it) are touched: in a 'full' run it does nothing.
#include <cstdint>
#include <string>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"

#include "core/provisions/Provisions.h"
#include "feature/effects/Effects.h"
#include "feature/spawn/Spawn.h"
#include "feature/provisions/Internal.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace {

namespace fp    = gf::feature::provisions;
namespace cp    = gf::core::provisions;
namespace items = gf::game::items;
using gf::selftest::Context;

template <class Fn>
int forRealPlayers(Fn&& fn) {
    int  n     = 0;
    auto level = ll::service::getLevel();
    if (!level) return 0;
    level->forEachPlayer([&](Player& p) {
        if (p.isSimulatedPlayer() || p.isClientSide() || !p.hasTag("gf_provisions_showcase")) return true;
        fn(p);
        ++n;
        return true;
    });
    return n;
}

/// The frosted pigs of the showcase (IceLayer's cubes in the screenshot), removed by real_clear.
std::vector<std::int64_t>& showcasePigs() {
    static std::vector<std::int64_t> v;
    return v;
}

} // namespace

GF_REGISTER_SELFTEST("provisions.real_arm", [](Context&) {
    int n = forRealPlayers([](Player& p) {
        auto& inv = p.getInventory();
        inv.removeAllItems();
        int slot = 0;
        auto put = [&](std::string const& id) {
            if (auto s = items::make(id)) inv.setItem(slot++, *s);
        };
        for (auto const& f : cp::foods()) put(std::string(f.id));
        put(fp::ids::kStaleBread);
        for (auto const& s : cp::songs()) put("gloamingforest:music_disc_" + std::string(s.track));
        p.sendInventory(false);
        gf::effects::apply(p, fp::ids::kFrosted, 20 * 60, 1);
        // Experiment 115 in its 16 states, 2 blocks ahead of the player on a stone floor
        auto& region = p.getDimensionBlockSource();
        auto  feet   = gf::game::blockPosOf(p.getFeetPos());
        int   i      = 0;
        for (int regen = 0; regen <= 1; ++regen)
            for (int bites = 0; bites <= 7; ++bites, ++i) {
                BlockPos at{feet.x - 8 + i, feet.y, feet.z + 3};
                if (auto const* stone = gf::game::blockWith("minecraft:stone")) gf::game::setBlock(region, BlockPos{at.x, at.y - 1, at.z}, *stone);
                fp::setE115(region, at, {bites, regen == 1});
            }
        // a pig with frosted III 5 blocks ahead: the client mod draws IceLayer's cubes on it
        Vec3 pigAt{static_cast<float>(feet.x) + 0.5f, static_cast<float>(feet.y), static_cast<float>(feet.z) + 5.5f};
        if (auto* pig = gf::spawn::spawnMob(region, "minecraft:pig", pigAt, gf::spawn::SpawnReason::Triggered)) {
            gf::effects::apply(*pig, fp::ids::kFrosted, 20 * 60, 2);
            showcasePigs().push_back(pig->getOrCreateUniqueID().rawID);
        }
        gf::log().info("provisions: armed {}", p.getRealName());
    });
    gf::log().info("provisions: real_arm armed {} player(s)", n);
});

GF_REGISTER_SELFTEST("provisions.real_clear", [](Context&) {
    forRealPlayers([](Player& p) {
        gf::effects::remove(p, fp::ids::kFrosted);
        p.getInventory().removeAllItems();
        p.sendInventory(false);
    });
    if (auto level = ll::service::getLevel())
        for (auto uid : showcasePigs())
            if (auto* pig = level->fetchEntity(ActorUniqueID{uid}, false)) pig->remove();
    showcasePigs().clear();
});
