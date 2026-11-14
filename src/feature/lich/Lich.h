#pragma once

// U21 (lich): the Lich's state and the operations its minions, projectiles, the loot condition and
// the selftests share. Internal to src/feature/lich (rule R2: other units use the entity ids only).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mc/deps/core/math/Vec3.h"

class Actor;
class BlockPos;
class BlockSource;
class Mob;

namespace gf::feature::lich {

/// Lich (upstream's entity/boss/Lich.java): synced and saved data plus the server fields.
struct LichState {
    std::int64_t              master = 0;  ///< MASTER_LICH (unique id of the master; 0 = not a clone)
    std::vector<std::int64_t> clones;      ///< summonedClones
    int shield          = 6;               ///< SHIELD_STRENGTH
    int minionsLeft     = 9;               ///< MINIONS_LEFT
    int attackType      = 0;               ///< ATTACK_TYPE (0 bolt, 1 bomb)
    int tpInvisibility  = 0;               ///< TELEPORT_INVISIBILITY
    int attackCooldown  = 0;
    int popCooldown     = 0;
    int heldScepterTime = 0;
    int spawnTime       = 0;
    int previousPhase   = 1;
    int babies          = 0;               ///< babyMinionsSummoned
    int hitsWithoutTeleport = 0;
    // attributes (Bedrock has no custom attributes: actor data with upstream's defaults)
    int cloneCount   = 2;                  ///< gloamingforest:clone_count
    int minionCount  = 9;                  ///< gloamingforest:minion_count
    int maxShield    = 6;                  ///< gloamingforest:max_shield_strength (U9's attribute)
    // bookkeeping
    int  tickCount   = 0;
    bool goalsAdded  = false;
    int  lastPropPhase = -1, lastPropShield = -1, lastPropClone = -1;
    bool lastInvisible = false;
    std::uint64_t minionCountTick  = 0; ///< countMyMinions cache (one count per tick)
    int           minionCountCache = 0;
};

/// Drops the per-tick minion count of `lich` (a minion was added or removed this tick).
void forgetMinionCount(Mob& lich);

/// The state of a Lich actor (nullptr: not a Lich).
[[nodiscard]] LichState* lichState(Actor const& actor);
[[nodiscard]] bool       isLich(Actor const& actor);
[[nodiscard]] bool       isShadowClone(Mob& lich);
/// Lich#getPhase.
[[nodiscard]] int phaseOf(Mob& lich);
[[nodiscard]] int shieldOf(Mob& lich); ///< getShieldStrength (0 for clones)
/// The Lich master of a clone (nullptr: none or gone).
[[nodiscard]] Mob* masterOf(Mob& lich);
/// Lich#getAllClones.
[[nodiscard]] std::vector<Mob*> clonesOf(Mob& lich);
[[nodiscard]] int countMyClones(Mob& lich);
[[nodiscard]] int countMyMinions(Mob& lich);
[[nodiscard]] bool wantsNewMinion(Mob& lich);
/// LichMinionsGoal#checkAndSpawnMinions.
void checkAndSpawnMinions(Mob& lich);
void makeMagicTrail(Mob& lich, Vec3 const& from, Vec3 const& to, float r, float g, float b);
bool teleportToSightOfEntity(Mob& lich, Actor* entity);
bool teleportToNewTarget(Mob& lich, Actor* target, float range, bool fromShadows);
[[nodiscard]] std::optional<Vec3> findVecInLOSOf(Mob& lich, Actor* target);
void teleportHome(Mob& lich);
void despawnClones(Mob& lich);
/// LichShadowsGoal#checkAndSpawnClones.
void checkAndSpawnClones(Mob& lich, Actor& target);
/// The spawn sequence of the spawner (LichSpawnerBlockEntity: attackCooldown 40, extinguish timer).
void setExtinguishTimer(Mob& lich);
void lightNearbyCandles(Mob& lich);
void turnNearbyCandles(Mob& lich, int tick, bool done);
/// Lich#launchProjectileAt: a LichBolt (type 0) or LichBomb (1) from the hand toward the target.
Actor* launchProjectile(Mob& lich, int type);
void   setShield(Mob& lich, int shield);
void   setMinionsLeft(Mob& lich, int left);
void   setTeleportInvisibility(Mob& lich, int ticks);
/// Lich#deflection for a projectile of #gloamingforest:lich_deflects_phase_2 hitting the Lich:
/// true when it was deflected (phase > 1 and owner player / lich / none).
bool deflect(Mob& lich, Actor& projectile, Actor* owner);
/// Creates the clone of `master` at `pos` (Lich(level, master)).
Mob* spawnClone(Mob& master, Vec3 const& pos);

// ---- minions (LichMinion) ----------------------------------------------------------------------

[[nodiscard]] Mob* minionMaster(Actor const& minion);
void               setMinionMaster(Actor const& minion, Mob* master);
/// LichMinionsGoal#spawnMinionAt (the minion at `pos`).
Mob* spawnMinion(Mob& lich, Vec3 const& pos, Actor* target);

// ---- death tome --------------------------------------------------------------------------------

/// Where a lectern tome rests: on the lectern's top (Bedrock pushes a mob out of the lectern's
/// collision, which Java's tome stands in; the client pose sits that much lower).
inline constexpr float kLecternTop = 0.9F;
[[nodiscard]] bool tomeOnLectern(Actor const& tome);
/// DeathTome#setOnLectern. With `seat` (the lectern's position) the tome is moved onto that lectern
/// and checks it from then on (Bedrock's collision may push it out of the lectern's block).
void setTomeOnLectern(Actor& tome, bool on, BlockPos const* seat = nullptr);
/// MiscEvents#addTomesToLecterns / LichTowerWingRoom#putTrappableLectern: a Death Tome mimic on the
/// lectern at `pos` (persistent, facing the lectern). nullptr when it could not spawn.
Mob* spawnLecternTome(BlockSource& region, BlockPos const& pos);

// ---- rising zombie -----------------------------------------------------------------------------

[[nodiscard]] int risingTicks(Actor const& zombie);
void              setRisingTicks(Actor& zombie, int ticks);

// ---- projectiles -------------------------------------------------------------------------------

/// The owner of a lich bolt / bomb / tome bolt (the reflected owner first, then the shooter).
[[nodiscard]] Actor* projectileOwner(Actor const& projectile);
void                 setProjectileOwner(Actor const& projectile, Actor* owner);
/// LichBolt#hurt by an entity: re-shot along `by`'s look (the source's entity) at 1.5 (inaccuracy
/// 0.1); the direct entity becomes the owner when it is a living one.
void reflectBolt(Actor& bolt, Actor& by, Actor* direct);
/// LichBomb#hurt / #explode.
void explodeBomb(Actor& bomb);

// ---- registration (Module.cpp calls these once) -------------------------------------------------

void registerLich();
void registerMobs();
void registerProjectiles();
/// The per-tick work outside the actors' ticks (boss bar progress of phase 1).
void levelTick();
/// Drop the per-actor records of actors that are gone (levelTick, every 200 ticks).
void pruneMobs();
void pruneProjectiles();

} // namespace gf::feature::lich
