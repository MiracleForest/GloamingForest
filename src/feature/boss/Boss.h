#pragma once

// Contract "boss" (header owned by F0, implemented by F8; rule R20).
//
// BaseTFBoss lifecycle for upstream's eight bosses: home structure, conquest of the landmark on death,
// the 27-slot loot buffer deposited into a death chest (gf::inventory), peaceful difficulty
// turning the boss back into its spawner, the RGB boss bar (GloamingBossBarPacket: exact colour with
// the client mod, nearest of the 8 BossBarColor values on vanilla clients), the generic boss
// spawner block entity (range 9, particles, self-destruct), minions, the multiplayer health
// adjuster and the trophy variant table (10 BossVariant rows). Config section "bosses"
// (bossesSpawnDropChests, multiplayerFightAdjuster).
//
//     gf::boss::registerBoss({.entityId = "gloamingforest:naga", .homeStructure = "gloamingforest:naga_courtyard",
//                             .lootTable = "gloamingforest:entities/naga", .bar = {.rgb = 0x76C45A}});
//     gf::boss::registerSpawner({.blockId = "gloamingforest:naga_boss_spawner", .entityId = "gloamingforest:naga"});

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class Actor;
class BlockPos;
class BlockSource;
class Mob;
class Player;

namespace gf::boss {

struct BarSpec {
    unsigned rgb    = 0xFFFFFF; ///< 0xRRGGBB (GloamingBossBarPacket colour)
    bool     darken = false;    ///< BossEvent#setDarkenScreen
    bool     fog    = false;    ///< BossEvent#setCreateWorldFog
};

struct BossSpec {
    std::string entityId;          ///< "gloamingforest:naga"
    std::string homeStructure;     ///< landmark key conquered on death
    std::string lootTable;         ///< "gloamingforest:entities/naga"
    std::string deathChestBlock = "minecraft:chest";
    BarSpec     bar;
    int         deathTicks = 0;    ///< death animation length before drops (BaseTFBoss)
};
void registerBoss(BossSpec spec);

struct SpawnerSpec {
    std::string blockId;   ///< "gloamingforest:naga_boss_spawner"
    std::string entityId;  ///< the boss
    int         range = 9; ///< player detection range (BossSpawnerBlockEntity)
    std::string particle;  ///< particle id shown while waiting
    /// Custom spawning (default: spawn the boss at the block); returns the boss or nullptr.
    std::function<Mob*(BlockSource& region, BlockPos const& pos)> spawnFn;
};
void registerSpawner(SpawnerSpec spec);

/// The boss bar of a registered boss.
struct BossBar {
    std::int64_t mobId = 0;
    void         setProgress(float fraction) const;
    void         setName(std::string const& name) const;
    void         setVisible(bool visible) const;
};
[[nodiscard]] BossBar bar(Mob& boss);

void setMinion(Actor& actor, bool minion);
[[nodiscard]] bool isMinion(Actor const& actor);
/// The players taking part in the fight (multiplayer adjuster, loot rolls).
[[nodiscard]] std::vector<Player*> participants(Mob const& boss);

/// A trophy's variant (BossVariant): sound and particles when powered, comparator output.
struct TrophyVariant {
    std::string sound;
    float       volume = 1.0f;
    float       pitch  = 1.0f;
    std::string particle;
    int         comparator = 0;
};
[[nodiscard]] std::optional<TrophyVariant> trophyVariant(std::string const& blockId);

} // namespace gf::boss
