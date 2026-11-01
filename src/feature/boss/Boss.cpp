// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/boss/BaseTFBoss.java
//   src/main/java/twilightforest/entity/boss/IBossLootBuffer.java
//   src/main/java/twilightforest/entity/boss/bar/ServerTFBossBar.java
//   src/main/java/twilightforest/network/TFBossBarPacket.java
//   src/main/java/twilightforest/network/UpdateDeathTimePacket.java
//   src/main/java/twilightforest/block/entity/spawner/BossSpawnerBlockEntity.java
//   src/main/java/twilightforest/block/entity/spawner/NagaSpawnerBlockEntity.java
//   src/main/java/twilightforest/block/entity/spawner/LichSpawnerBlockEntity.java
//   src/main/java/twilightforest/block/entity/spawner/HydraSpawnerBlockEntity.java
//   src/main/java/twilightforest/block/entity/spawner/UrGhastSpawnerBlockEntity.java
//   src/main/java/twilightforest/block/entity/spawner/KnightPhantomSpawnerBlockEntity.java
//   src/main/java/twilightforest/block/entity/spawner/SnowQueenSpawnerBlockEntity.java
//   src/main/java/twilightforest/block/entity/spawner/MinoshroomSpawnerBlockEntity.java
//   src/main/java/twilightforest/block/entity/spawner/AlphaYetiSpawnerBlockEntity.java
//   src/main/java/twilightforest/block/entity/spawner/FinalBossSpawnerBlockEntity.java
//   src/main/java/twilightforest/block/entity/spawner/SinisterSpawnerBlockEntity.java
//   src/main/java/twilightforest/block/entity/spawner/SinisterSpawnerLogic.java
//   src/main/java/twilightforest/block/AbstractTrophyBlock.java
//   src/main/java/twilightforest/enums/BossVariant.java
//   src/main/java/twilightforest/components/entity/MultiplayerInclusivityAttachment.java
//   src/main/java/twilightforest/events/EntityEvents.java
//   src/main/java/twilightforest/util/entities/EntityUtil.java
//   src/main/java/twilightforest/util/landmarks/LandmarkUtil.java
//   src/main/java/twilightforest/config/TFConfig.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Contract "boss" (F8, rule R20): BaseTFBoss's lifecycle on top of F8's entity hooks.
//   - Boss bar: a vanilla BossEventPacket bar per boss for the players that see it (Java's
//     tracking: 160 blocks), coloured with the nearest of Bedrock's 8 BossBarColor values; the
//     companion protocol carries TFBossBarPacket's exact RGB, overlay, darken and fog plus the
//     viewer's title and the progress ("boss.bar"; the client mod paints the bar with the RGB) and
//     UpdateDeathTimePacket ("boss.death_time", when a player starts seeing a dying boss).
//   - Death (postmortem): the loot table rolls into the 27-slot buffer (IBossLootBuffer, saved
//     with the boss), the home landmark is conquered (gf::landmark), "hurt_boss" fires for every
//     qualified player; after the death animation the buffer goes into the death chest at
//     EntityUtil#bossChestLocation (home.below(), replacing the floor block when a piston could
//     move it) through gf::inventory or a vanilla container, with the chest sound and poof.
//   - Peaceful: the boss turns back into its spawner at home and disappears.
//   - Spawners (BossSpawnerBlockEntity, SinisterSpawnerLogic): custom blocks have no block
//     entities, so the spawner blocks around players are found with the chunk palettes
//     (BlockSource::fetchBlocksInBox, one slice of the box per tick, a margin beyond the longest
//     range so a spawner is known before a player gets in range) and ticked every level tick.
//   - Multiplayer: health adjuster on spawn, qualified players (participants), config "bosses".

#include "feature/boss/Boss.h"
#include "feature/boss/BossInternal.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/Config.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/legacy/ActorRuntimeID.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/BossEventPacket.h"
#include "mc/platform/UUID.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/util/BossBarColor.h"
#include "mc/world/actor/ai/util/BossBarOverlay.h"
#include "mc/world/actor/ai/util/BossEventUpdateType.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceRef.h"
#include "mc/world/attribute/AttributeModifier.h"
#include "mc/world/attribute/AttributeModifierOperation.h"
#include "mc/world/attribute/AttributeOperands.h"
#include "mc/world/attribute/MutableAttributeWithContext.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/world/level/levelgen/structure/BoundingBox.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/phys/AABB.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"

#include "feature/combat/Combat.h"
#include "feature/common/BlockData.h"
#include "feature/common/Interactions.h"
#include "feature/entity/EntityInternal.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/fx/Fx.h"
#include "feature/inventory/Inventory.h"
#include "feature/landmark/Landmark.h"
#include "feature/loot/Loot.h"
#include "feature/net/Net.h"
#include "feature/progression/Progression.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::boss {

/// TFConfig.MultiplayerFightAdjuster (its names are the config values; the config screen refuses
/// anything else, a config file with another value fails to load and keeps the previous value).
enum class MultiplayerFightAdjuster { NONE, MORE_LOOT, MORE_HEALTH, MORE_LOOT_AND_HEALTH };

/// Config section "bosses" (docs/spec/config.md): TFConfig.bossDropChests and
/// multiplayerFightAdjuster.
struct BossesConfig {
    int                      version                  = 1;
    bool                     bossesSpawnDropChests    = true;
    MultiplayerFightAdjuster multiplayerFightAdjuster = MultiplayerFightAdjuster::NONE;
};

} // namespace gf::feature::boss

GF_REGISTER_CONFIG("bosses", gf::feature::boss::BossesConfig);

namespace gf::feature::boss {

namespace fe = gf::feature::entity;
namespace ia = gf::interactions;
namespace bd = gf::feature::common::blockdata;
using gf::boss::BossSpec;
using gf::boss::SpawnerSpec;
using Json = nlohmann::json;

namespace {

constexpr int    kContainerSize = 27;   ///< IBossLootBuffer.CONTAINER_SIZE
constexpr double kTrackRange    = 160.; ///< Java tracks bosses as far as the view distance (10 chunks)
constexpr int    kShortRange    = 9;    ///< BossSpawnerBlockEntity.SHORT_RANGE
constexpr int    kLongRange     = 50;   ///< BossSpawnerBlockEntity.LONG_RANGE
constexpr int    kScanMargin    = 16;   ///< spawners are found this far beyond their range
constexpr int    kScanSlices    = 20;   ///< the scan box is covered in 20 slices, one per tick
constexpr char const* kInclusiveTag = "twilightforest:multiplayer_inclusive_entities";
constexpr char const* kSinisterBlock = "twilightforest:sinister_spawner";
constexpr char const* kSinisterKey   = "sinister_spawner"; ///< the block data key (the block's path)
constexpr char const* kCoronationCarpet = "twilightforest:coronation_carpet";

BossesConfig const& config() { return gf::config::get<BossesConfig>(); }

/// The bar of one boss (ServerTFBossBar).
struct Bar {
    unsigned                rgb      = 0xFFFFFF;
    int                     overlay  = 0;
    bool                    darken   = false;
    bool                    fog      = false;
    bool                    visible  = true;
    float                   progress = 1.0f;
    std::optional<std::string> name;
    std::string             lastTag; ///< the name tag the bar's title was made from
    std::set<std::int64_t>  players; ///< the players the bar was added for
};

/// A dying boss: the death animation counts on after the engine removed the actor.
struct Dying {
    std::int64_t                 uid = 0;
    std::string                  type;
    int                          dim = 0;
    Vec3                         pos{};
    std::optional<core::GlobalPos> home;
    int                          deathTime = 0;
    int                          finish    = 20;
    Json                         buffer; ///< the 27 slots (items as JSON)
    bool                         loot = true;
    std::string                  chestBlock;
    std::int64_t                 seed = 0;
};

/// SinisterSpawnerLogic's runtime (the spawn buffer and scan position are not saved in Java).
struct SinisterRuntime {
    gf::boss::ext::SinisterSpawner cfg;
    bool                           loaded = false;
    std::optional<BlockPos>        checkPos;
    std::vector<BlockPos>          spawnBuffer;
    int                            countNextToSpawn = 0;
};

/// Per-spawner position state (KnightPhantomSpawnerBlockEntity#spawned, SinisterSpawnerLogic).
struct SpawnerRuntime {
    std::string     blockId;
    int             spawned = 0;
    bool            sinister = false;
    SinisterRuntime sin;
};

struct State {
    std::unordered_map<std::string, BossSpec>    bosses;
    std::unordered_map<std::string, SpawnerSpec> spawners; ///< by block id
    std::unordered_map<std::int64_t, Bar>        bars;     ///< by boss uid
    std::unordered_map<std::int64_t, std::vector<std::int64_t>> qualified; ///< MultiplayerInclusivityAttachment
    std::vector<Dying>                           dying;
    std::map<core::GlobalPos, SpawnerRuntime>    activeSpawners;
    std::unordered_set<std::string>              inclusiveForTest;
    std::unordered_set<std::string>              sinisterBlocks{kSinisterBlock};
    std::vector<ia::HandlerId>                   handlers;
    // the last death chest (selftests)
    std::optional<core::GlobalPos>               lastChest;
    struct PendingFill {
        core::GlobalPos        pos;
        std::vector<ItemStack> items;
        int                    tries = 20;
        std::string            type;
    };
    std::vector<PendingFill> pendingFills;
    std::unordered_set<std::int64_t> discarding; ///< bosses removed on peaceful (removal pending)
    int                              spawnerVersion = 0;
    std::optional<std::pair<core::GlobalPos, std::string>> lastConquest;
    int                              sinisterSpawned = 0; ///< mobs spawned by sinister spawners (selftests)
    std::unordered_map<std::string, SpawnerKind> kindsForTest; ///< fixture spawners with a variant's rules
    int sinisterRange = 16; ///< the longest requiredPlayerRange a sinister spawner was given (scan box)
};
State& S() {
    static State s;
    return s;
}

BossSpec const* bossSpec(std::string const& type) {
    auto it = S().bosses.find(type);
    return it != S().bosses.end() ? &it->second : nullptr;
}

std::int64_t uidOf(Actor const& a) { return fe::uidOf(a); }

Level* level() {
    auto l = ll::service::getLevel();
    return l ? l.as_ptr() : nullptr;
}

bool gameRule(GameRules::GameRulesIndex rule) {
    auto* lv = level();
    if (!lv) return true;
    GameRuleId id;
    id.mValue = static_cast<int>(rule);
    return lv->getGameRules().getBool(id, true);
}

bool peaceful() {
    auto* lv = level();
    return lv && lv->getDifficulty() == SharedTypes::Legacy::Difficulty::Peaceful;
}

// ---- multiplayer --------------------------------------------------------------------------------

bool inclusive(std::string const& type) {
    if (S().inclusiveForTest.contains(type)) return true;
    if (tags::has(tags::Registry::EntityType, kInclusiveTag, type)) return true;
    if (!tags::members(tags::Registry::EntityType, kInclusiveTag).empty()) return false;
    // the tag of TF 4.8 while the tag data is not available
    static std::unordered_set<std::string> const fallback{
        "twilightforest:naga",
        "twilightforest:lich",
        "twilightforest:minoshroom",
        "twilightforest:hydra",
        "twilightforest:ur_ghast",
        "twilightforest:alpha_yeti",
        "twilightforest:snow_queen",
        "twilightforest:plateau_boss",
    };
    return fallback.contains(type);
}

/// MultiplayerInclusivityAttachment#maybeAddQualifiedPlayer.
void qualify(Actor& boss, Actor* source) {
    if (!source || !source->isPlayer() || !inclusive(boss.getTypeName())) return;
    auto& list = S().qualified[uidOf(boss)];
    auto  id   = uidOf(*source);
    if (std::find(list.begin(), list.end(), id) == list.end()) list.push_back(id);
}

/// EntityEvents#adjustEntityHealthInMultiplayerFights (FinalizeSpawnEvent).
void adjustHealth(Mob& mob) {
    if (!gf::boss::ext::adjustsHealth() || !inclusive(mob.getTypeName())) return;
    int n = 0;
    auto bb = mob.getAABB();
    for (auto* p : fe::playersNear(static_cast<int>(mob.getDimensionId()), mob.getFeetPos(), 64.0)) {
        auto f = p->getFeetPos();
        if (f.x < bb.min.x - 32 || f.x > bb.max.x + 32 || f.y < bb.min.y - 10 || f.y > bb.max.y + 10 || f.z < bb.min.z - 32 || f.z > bb.max.z + 32) continue;
        if (p->isCreative() || p->isSpectator() || !p->isAlive()) continue;
        ++n;
    }
    if (n <= 1) return;
    double perPlayer = 0.0;
    auto*  lv        = level();
    switch (lv ? lv->getDifficulty() : SharedTypes::Legacy::Difficulty::Normal) {
    case SharedTypes::Legacy::Difficulty::Easy: perPlayer = 20.0; break;
    case SharedTypes::Legacy::Difficulty::Normal: perPlayer = 40.0; break;
    case SharedTypes::Legacy::Difficulty::Hard: perPlayer = 60.0; break;
    default: break;
    }
    if (perPlayer <= 0) return;
    auto attr = mob.getMutableAttribute(SharedAttributes::HEALTH());
    auto* inst = attr.mInstance->mPtr;
    if (!inst) return;
    AttributeModifier mod(
        mce::UUID::fromStringHash("twilightforest:group_health_boost"),
        "twilightforest:group_health_boost",
        static_cast<float>(perPlayer * (n - 1)),
        AttributeModifierOperation::OperationAddition,
        AttributeOperands::OperandMax,
        true
    );
    inst->addModifier(mod, attr.mContext);
    log().info("boss: {} got {} extra health for {} players", mob.getTypeName(), perPlayer * (n - 1), n);
}

// ---- boss bar -----------------------------------------------------------------------------------

/// The 8 BossBarColor values of Bedrock, as their bar textures look.
constexpr unsigned kBarColors[8] = {0xEC00B8, 0x00B7EC, 0xEC3500, 0x1DEC00, 0xE9EC00, 0x7B00EC, 0x663399, 0xECECEC};

int nearest(unsigned rgb) {
    int    best  = 7;
    double bestD = 1e30;
    int    r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    for (int i = 0; i < 8; ++i) {
        int    cr = (kBarColors[i] >> 16) & 255, cg = (kBarColors[i] >> 8) & 255, cb = kBarColors[i] & 255;
        double rm = (r + cr) / 2.0;
        double dr = r - cr, dg = g - cg, db = b - cb;
        // "redmean" weighted distance
        double d = (2 + rm / 256) * dr * dr + 4 * dg * dg + (2 + (255 - rm) / 256) * db * db;
        if (d < bestD) bestD = d, best = i;
    }
    return best;
}

std::string barTitle(Mob& boss, Player const& viewer) {
    auto& bar = S().bars[uidOf(boss)];
    if (bar.name) return *bar.name;
    if (!boss.getNameTag().empty()) return boss.getNameTag();
    // BaseTFBoss#getBossBarTitle: the type name ("entity.twilightforest.naga")
    std::string const& type = boss.getTypeName();
    auto               colon = type.find(':');
    std::string        ns    = colon == std::string::npos ? "minecraft" : type.substr(0, colon);
    std::string        path  = colon == std::string::npos ? type : type.substr(colon + 1);
    std::string        fallback = path;
    for (auto& c : fallback)
        if (c == '_') c = ' ';
    if (!fallback.empty()) fallback[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(fallback[0])));
    return gf::trFor(viewer, "entity." + ns + "." + path, fallback);
}

void sendBossEvent(Player& player, std::int64_t bossId, BossEventUpdateType type, std::string const& name, float percent, int color, int overlay) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::BossEvent);
    if (!packet) return;
    auto& p           = static_cast<BossEventPacket&>(*packet);
    p.mBossID->rawID  = bossId;
    p.mEventType      = type;
    p.mName->set(name);
    p.mHealthPercent = percent;
    p.mColor         = static_cast<BossBarColor>(color);
    p.mOverlay       = static_cast<BossBarOverlay>(overlay);
    packet->sendTo(player);
}

/// TFBossBarPacket for companions (with the title the viewer's vanilla bar shows: the client mod
/// finds that bar by it).
void sendCompanionBar(Player& player, Mob& boss, Bar const& bar, char const* op, bool allowLerp = true) {
    if (!gf::net::isCompanion(player)) return;
    gf::net::send(
        player,
        "boss.bar",
        {
            {"op",       op                                                  },
            {"id",       uidOf(boss)                                         },
            {"rid",      static_cast<std::int64_t>(boss.getRuntimeID().rawID)},
            {"rgb",      bar.rgb                                             },
            {"overlay",  bar.overlay                                         },
            {"darken",   bar.darken                                          },
            {"fog",      bar.fog                                             },
            {"lerp",     allowLerp                                           },
            {"progress", bar.progress                                        },
            {"title",    barTitle(boss, player)                              },
        }
    );
}

int deathTimeOf(Mob& boss) {
    for (auto const& d : S().dying)
        if (d.uid == uidOf(boss)) return d.deathTime;
    return boss.getDeathTime();
}

void addViewer(Mob& boss, Bar& bar, Player& player) {
    if (!bar.players.insert(uidOf(player)).second) return;
    if (!bar.visible) return;
    sendBossEvent(player, uidOf(boss), BossEventUpdateType::Add, barTitle(boss, player), bar.progress, nearest(bar.rgb), bar.overlay);
    sendCompanionBar(player, boss, bar, "add");
    // BaseTFBoss#startSeenByPlayer: a dying boss sends its death time (UpdateDeathTimePacket)
    if (int dt = deathTimeOf(boss); dt > 0 && gf::net::isCompanion(player))
        gf::net::send(player, "boss.death_time", {{"id", uidOf(boss)}, {"rid", static_cast<std::int64_t>(boss.getRuntimeID().rawID)}, {"deathTime", dt}});
}

void removeViewer(std::int64_t bossId, Player& player) {
    sendBossEvent(player, bossId, BossEventUpdateType::Remove, "", 0.0f, 7, 0);
    if (gf::net::isCompanion(player)) gf::net::send(player, "boss.bar", {{"op", "remove"}, {"id", bossId}});
}

void removeBar(std::int64_t bossId) {
    auto it = S().bars.find(bossId);
    if (it == S().bars.end()) return;
    for (auto pid : it->second.players)
        if (auto* p = fe::playerById(pid)) removeViewer(bossId, *p);
    S().bars.erase(it);
    S().qualified.erase(bossId);
}

void forEachViewer(Bar const& bar, std::function<void(Player&)> const& fn) {
    if (!bar.visible) return;
    for (auto pid : bar.players)
        if (auto* p = fe::playerById(pid)) fn(*p);
}

void updateAudience(Mob& boss, Bar& bar) {
    auto near = fe::playersNear(static_cast<int>(boss.getDimensionId()), boss.getFeetPos(), kTrackRange);
    std::set<std::int64_t> now;
    for (auto* p : near) now.insert(uidOf(*p));
    for (auto it = bar.players.begin(); it != bar.players.end();) {
        if (!now.contains(*it)) {
            if (auto* p = fe::playerById(*it); p && bar.visible) removeViewer(uidOf(boss), *p);
            it = bar.players.erase(it);
        } else ++it;
    }
    for (auto* p : near) addViewer(boss, bar, *p);
}

void setProgress(Mob& boss, Bar& bar, float progress) {
    progress = std::clamp(progress, 0.0f, 1.0f);
    if (std::abs(progress - bar.progress) < 1e-4f) return;
    bar.progress = progress;
    forEachViewer(bar, [&](Player& p) {
        sendBossEvent(p, uidOf(boss), BossEventUpdateType::UpdatePercent, barTitle(boss, p), progress, nearest(bar.rgb), bar.overlay);
        if (gf::net::isCompanion(p)) gf::net::send(p, "boss.bar", {{"op", "progress"}, {"id", uidOf(boss)}, {"progress", progress}});
    });
}

/// A new title for the bar's viewers (BaseTFBoss#setCustomName / BossBar::setName).
void sendTitle(Mob& boss, Bar& bar) {
    forEachViewer(bar, [&](Player& p) {
        auto title = barTitle(boss, p);
        sendBossEvent(p, uidOf(boss), BossEventUpdateType::UpdateName, title, bar.progress, nearest(bar.rgb), bar.overlay);
        if (gf::net::isCompanion(p)) gf::net::send(p, "boss.bar", {{"op", "name"}, {"id", uidOf(boss)}, {"title", title}});
    });
}

// ---- loot buffer (IBossLootBuffer) ------------------------------------------------------------------

Json& bufferOf(Actor& boss) {
    auto& b = gf::entity::data(boss, "boss:buffer");
    if (!b.is_array() || b.size() != kContainerSize) {
        b = Json::array();
        for (int i = 0; i < kContainerSize; ++i) b.push_back(nullptr);
    }
    return b;
}

void setBufferSlot(Json& buffer, int slot, ItemStack stack) {
    if (game::items::isEmpty(stack)) {
        buffer[slot] = nullptr;
        return;
    }
    int max = stack.getMaxStackSize();
    if (max > 0 && game::items::countOf(stack) > max) stack = game::items::withCount(stack, max);
    buffer[slot] = game::items::toJson(stack);
}

/// Util#shuffle.
template <class T>
void shuffle(std::vector<T>& list, fe::JavaRandom& random) {
    for (int i = static_cast<int>(list.size()); i > 1; --i) std::swap(list[i - 1], list[random.nextInt(i)]);
}

int nextIntBetween(fe::JavaRandom& random, int a, int b) { return a >= b ? a : random.nextInt(b - a + 1) + a; }

/// LootTable#shuffleAndSplitItems.
void shuffleAndSplit(std::vector<ItemStack>& stacks, int emptySlots, fe::JavaRandom& random) {
    std::vector<ItemStack> list;
    for (auto it = stacks.begin(); it != stacks.end();) {
        if (game::items::isEmpty(*it)) it = stacks.erase(it);
        else if (game::items::countOf(*it) > 1) {
            list.push_back(*it);
            it = stacks.erase(it);
        } else ++it;
    }
    while (emptySlots - static_cast<int>(stacks.size()) - static_cast<int>(list.size()) > 0 && !list.empty()) {
        int       idx   = nextIntBetween(random, 0, static_cast<int>(list.size()) - 1);
        ItemStack whole = list[idx];
        list.erase(list.begin() + idx);
        int       i     = nextIntBetween(random, 1, game::items::countOf(whole) / 2);
        ItemStack split = game::items::withCount(whole, i);
        whole           = game::items::withCount(whole, game::items::countOf(whole) - i);
        if (game::items::countOf(whole) > 1 && random.nextBoolean()) list.push_back(whole);
        else stacks.push_back(whole);
        if (game::items::countOf(split) > 1 && random.nextBoolean()) list.push_back(split);
        else stacks.push_back(split);
    }
    stacks.insert(stacks.end(), list.begin(), list.end());
    shuffle(stacks, random);
}

std::vector<ItemStack> rollLoot(Mob& boss, BossSpec const& spec, std::string const& damageType, Actor* killer) {
    gf::loot::LootContext ctx;
    ctx.region     = &boss.getDimensionBlockSource();
    ctx.origin     = boss.getFeetPos();
    ctx.thisEntity = &boss;
    ctx.attacker   = killer;
    ctx.damageType = damageType;
    if (auto* p = fe::playerById(boss.mLastHurtByPlayerId->rawID)) ctx.player = p;
    else if (killer && killer->isPlayer()) ctx.player = static_cast<Player*>(killer);
    return gf::loot::roll(spec.lootTable, ctx);
}

/// IBossLootBuffer#saveDropsIntoBoss (+ #fill).
void saveDropsIntoBoss(Mob& boss, BossSpec const& spec, std::string const& damageType, Actor* killer) {
    if (!config().bossesSpawnDropChests) return;
    auto& region = boss.getDimensionBlockSource();
    auto  stacks = rollLoot(boss, spec, damageType, killer);
    auto& random = fe::randomOf(boss);
    // fill: a second roll, spread over the shuffled free slots
    auto             items = rollLoot(boss, spec, damageType, killer);
    std::vector<int> slots;
    for (int i = 0; i < kContainerSize; ++i) slots.push_back(i);
    shuffle(slots, random);
    shuffleAndSplit(items, static_cast<int>(slots.size()), random);
    auto& buffer = bufferOf(boss);
    for (auto const& item : items) {
        if (slots.empty()) break;
        int slot = slots.back();
        slots.pop_back();
        setBufferSlot(buffer, slot, item);
    }
    // if our loot stack size is bigger than the inventory, drop everything else outside it
    if (stacks.size() > static_cast<std::size_t>(kContainerSize)) {
        auto* lv = level();
        for (std::size_t i = kContainerSize; lv && i < stacks.size(); ++i)
            lv->getSpawner().spawnItem(region, stacks[i], nullptr, boss.getFeetPos(), 0);
    }
}

// ---- death chest ------------------------------------------------------------------------------

/// IBossLootBuffer#celebrateAt: the chest sound and 40 poof particles.
void celebrateAt(int dim, Vec3 const& at, fe::JavaRandom& random) {
    auto* region = game::regionOf(dim);
    if (!region) return;
    auto& dimension = region->getDimension();
    float pitch     = (random.nextFloat() - random.nextFloat()) * 0.175F + 0.5F;
    gf::fx::sound(dimension, "block.twilightforest.boss_chest.appear", at, 128.0F, pitch);
    for (int i = 0; i < 40; ++i) {
        double x = (random.nextDouble() - 0.5) * 0.075 * i;
        double y = (random.nextDouble() - 0.5) * 0.075 * i;
        double z = (random.nextDouble() - 0.5) * 0.075 * i;
        gf::fx::particle(dimension, "minecraft:poof", Vec3{float(at.x + x), float(at.y + y), float(at.z + z)});
    }
}

Container* vanillaContainer(BlockSource& region, BlockPos const& pos) {
    auto* be = region.getBlockEntity(pos);
    if (!be) return nullptr;
    if (be->getType() == BlockActorType::Chest) return static_cast<ChestBlockActor*>(be)->getContainer();
    return nullptr;
}

Block const* chestState(std::string const& id, fe::JavaRandom& random) {
    static char const* const dirs[] = {"north", "east", "south", "west"};
    if (auto const* b = game::blockWithStates(id, {{"minecraft:cardinal_direction", dirs[random.nextInt(4)]}})) return b;
    return game::blockWith(id);
}

/// Puts the 27 slots into the container at `pos` (gf::inventory, else a vanilla container).
bool fillContainer(core::GlobalPos const& gpos, std::vector<ItemStack> const& items) {
    auto* region = game::regionOf(gpos.dim);
    if (!region) return false;
    BlockPos pos{gpos.x, gpos.y, gpos.z};
    if (auto handler = gf::inventory::find(*region, pos); handler && handler->set) {
        for (int i = 0; i < kContainerSize && i < handler->slots; ++i) handler->set(gpos, i, items[i]);
        return true;
    }
    if (auto* container = vanillaContainer(*region, pos)) {
        for (int i = 0; i < kContainerSize && i < container->getContainerSize(); ++i) container->setItem(i, items[i]);
        return true;
    }
    return false;
}

} // namespace

/// BlockBehaviour#getPistonPushReaction() != PushReaction.BLOCK: a piston may push or break the
/// block. Bedrock has no push reaction to read (its movable component's type ids are the game's),
/// so the BLOCK blocks are: unbreakable ones (destroy speed < 0: bedrock, barrier, portals,
/// end portal frame, force fields, ...), vanilla's BLOCK list and TF's (TFBlocks).
bool pistonMovable(BlockSource& region, BlockPos const& pos) {
    Block const& block = region.getBlock(pos);
    if (block.isAir()) return true;
    if (block.getBlockType().getDestroySpeed() < 0.0f) return false;
    static std::unordered_set<std::string> const blocked{
        // vanilla (Blocks: .pushReaction(PushReaction.BLOCK))
        "minecraft:obsidian", "minecraft:crying_obsidian", "minecraft:respawn_anchor", "minecraft:reinforced_deepslate",
        "minecraft:portal", "minecraft:end_portal", "minecraft:end_gateway", "minecraft:end_portal_frame",
        "minecraft:moving_block", "minecraft:piston_arm_collision", "minecraft:sticky_piston_arm_collision",
        "minecraft:barrier", "minecraft:bedrock", "minecraft:border_block", "minecraft:allow", "minecraft:deny",
        // TF (TFBlocks: .pushReaction(PushReaction.BLOCK))
        "twilightforest:twilight_portal", "twilightforest:skull_chest", "twilightforest:keepsake_casket",
        "twilightforest:stronghold_shield", "twilightforest:reappearing_block", "twilightforest:vanishing_block",
        "twilightforest:locked_vanishing_block", "twilightforest:carminite_builder", "twilightforest:built_block",
        "twilightforest:antibuilder", "twilightforest:antibuilt_block", "twilightforest:ghast_trap",
        "twilightforest:carminite_reactor", "twilightforest:reactor_debris", "twilightforest:fake_gold",
        "twilightforest:fake_diamond", "twilightforest:brown_thorns", "twilightforest:green_thorns",
        "twilightforest:giant_cobblestone", "twilightforest:giant_log", "twilightforest:giant_leaves",
        "twilightforest:giant_obsidian", "twilightforest:violet_force_field", "twilightforest:pink_force_field",
        "twilightforest:orange_force_field", "twilightforest:green_force_field", "twilightforest:blue_force_field",
        "twilightforest:yellow_castle_door", "twilightforest:violet_castle_door", "twilightforest:pink_castle_door",
        "twilightforest:blue_castle_door",
    };
    auto id = game::blockId(block);
    if (blocked.contains(id)) return false;
    if (id == "twilightforest:trophy_pedestal") {
        // TrophyPedestalBlock: NORMAL once active, BLOCK before
        for (auto const& [name, value] : game::statesOf(block))
            if (name.ends_with("active")) return value == "true" || value == "1";
        return false;
    }
    return true;
}

namespace {

/// IBossLootBuffer#tryDeposit: an existing chest of the type, or a block that is replaceable or
/// that a piston could move (stone, dirt, grass: the floor under the home point), without a block
/// entity; then its container takes the 27 slots.
bool tryDeposit(Dying const& d, Block const& chest, BlockPos const& pos, fe::JavaRandom& random, std::vector<ItemStack> const& items) {
    auto* region = game::regionOf(d.dim);
    if (!region || !region->hasChunksAt(pos, 0, false)) return false;
    bool isChest = game::blockIdAt(*region, pos) == d.chestBlock;
    if (!isChest) {
        if (!(game::replaceable(*region, pos) || pistonMovable(*region, pos)) || region->getBlockEntity(pos)) return false;
        if (!game::setBlock(*region, pos, chest)) return false;
    }
    core::GlobalPos gpos{d.dim, pos.x, pos.y, pos.z};
    // a chest placed just now may get its container on the next tick: the fill waits for it
    if (!fillContainer(gpos, items)) {
        if (isChest) return false;
        S().pendingFills.push_back({gpos, items, 20, d.type});
    }
    celebrateAt(d.dim, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, random);
    S().lastChest = gpos;
    int stacks    = 0;
    for (auto const& s : items) stacks += game::items::isEmpty(s) ? 0 : 1;
    log().info("boss: death chest of {} at {} {} {} ({} stacks)", d.type, pos.x, pos.y, pos.z, stacks);
    return true;
}

/// IBossLootBuffer#depositDropsIntoChest at EntityUtil#bossChestLocation.
void depositDropsIntoChest(Dying const& d) {
    if (!config().bossesSpawnDropChests) return;
    fe::JavaRandom random(d.seed);
    std::vector<ItemStack> items;
    for (int i = 0; i < kContainerSize; ++i) {
        auto const& j = d.buffer.is_array() && i < static_cast<int>(d.buffer.size()) ? d.buffer[i] : Json();
        items.push_back(j.is_null() ? game::items::empty() : game::items::fromJson(j));
    }
    BlockPos pos = d.home && d.home->dim == d.dim ? BlockPos{d.home->x, d.home->y - 1, d.home->z} : game::blockPosOf(d.pos);
    auto const* chest = chestState(d.chestBlock, random);
    auto*       region = game::regionOf(d.dim);
    if (!chest || !region) return;
    if (tryDeposit(d, *chest, pos, random, items)) return;
    for (int y = pos.y; y < region->getMaxHeight(); ++y)
        if (tryDeposit(d, *chest, BlockPos{pos.x, y, pos.z}, random, items)) return;
    // nowhere to put a chest: Block#popResource
    if (auto* lv = level())
        for (auto const& s : items)
            if (!game::items::isEmpty(s)) lv->getSpawner().spawnItem(*region, s, nullptr, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, 0);
    celebrateAt(d.dim, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, random);
    log().info("boss: no room for the death chest of {}, the loot dropped at {} {} {}", d.type, pos.x, pos.y, pos.z);
}

// ---- lifecycle ------------------------------------------------------------------------------------

/// BaseTFBoss#postmortem (+ the conquest and the group advancement).
void onBossDie(Mob& boss, std::string const& damageType, Actor* killer) {
    auto const* spec = bossSpec(boss.getTypeName());
    if (!spec) return;
    auto uid = uidOf(boss);
    auto it  = S().bars.find(uid);
    if (it != S().bars.end()) setProgress(boss, it->second, 0.0f);
    bool loot = gameRule(GameRules::GameRulesIndex::DoMobLoot);
    auto home = gf::entity::home(boss);
    if (loot) {
        saveDropsIntoBoss(boss, *spec, damageType, killer);
        // LandmarkUtil#markStructureConquered at the home point
        if (home && home->pos.dim == static_cast<int>(boss.getDimensionId()) && !spec->homeStructure.empty()) {
            BlockPos hp{home->pos.x, home->pos.y, home->pos.z};
            gf::landmark::setConquered(home->pos.dim, hp, spec->homeStructure, true);
            S().lastConquest = std::make_pair(home->pos, spec->homeStructure);
            log().info("boss: {} conquered {} at {} {} {}", boss.getTypeName(), spec->homeStructure, hp.x, hp.y, hp.z);
        }
        // BaseTFBoss#shouldDropLoot: without death chests the loot table drops as for any mob (the
        // death pipeline's entity drops, gf::loot); with them those drops are suppressed below
    }
    // EntityEvents#grantGroupAdvancementIfNeeded
    if (auto q = S().qualified.find(uid); q != S().qualified.end())
        for (auto pid : q->second)
            if (auto* p = fe::playerById(pid)) gf::progression::trigger(*p, "twilightforest:hurt_boss", {{"entity", boss.getTypeName()}});
    Dying d;
    d.uid        = uid;
    d.type       = boss.getTypeName();
    d.dim        = static_cast<int>(boss.getDimensionId());
    d.pos        = boss.getFeetPos();
    d.home       = home ? std::optional<core::GlobalPos>(home->pos) : std::nullopt;
    d.finish     = spec->deathTicks > 0 ? spec->deathTicks : 20; // BaseTFBoss#isDeathAnimationFinished
    d.buffer     = bufferOf(boss);
    d.loot       = loot;
    d.chestBlock = spec->deathChestBlock.empty() ? std::string("minecraft:chest") : spec->deathChestBlock;
    d.seed       = fe::randomOf(boss).nextLong();
    S().dying.push_back(std::move(d));
    log().info("boss: {} died ({})", boss.getTypeName(), damageType);
}

/// BaseTFBoss#tickDeath for the dying bosses, then #postRemoval. The death time reaches companion
/// clients only when they start seeing a dying boss (UpdateDeathTimePacket from
/// #startSeenByPlayer); clients that watched the death count it themselves.
void tickDying() {
    auto& dying = S().dying;
    for (auto it = dying.begin(); it != dying.end();) {
        auto& d = *it;
        d.deathTime++;
        if (d.deathTime < d.finish) {
            ++it;
            continue;
        }
        removeBar(d.uid);
        if (auto* actor = fe::actorById(d.uid)) actor->remove(); // RemovalReason.KILLED
        if (d.loot) depositDropsIntoChest(d);
        S().qualified.erase(d.uid);
        it = dying.erase(it);
    }
}

/// The spawner block of a boss type (BaseTFBoss#getBossSpawner).
std::string spawnerBlockOf(std::string const& type) {
    for (auto const& [block, spec] : S().spawners)
        if (spec.entityId == type) return block;
    return {};
}

/// BaseTFBoss#checkDespawn on peaceful: back to the spawner at home, then gone.
bool peacefulDespawn(Mob& boss) {
    if (!peaceful()) return false;
    if (!S().discarding.insert(uidOf(boss)).second) return true; // already on its way out
    auto home = gf::entity::home(boss);
    auto block = spawnerBlockOf(boss.getTypeName());
    if (home && home->pos.dim == static_cast<int>(boss.getDimensionId()) && !block.empty()) {
        auto&    region = boss.getDimensionBlockSource();
        BlockPos hp{home->pos.x, home->pos.y, home->pos.z};
        if (region.hasChunksAt(hp, 0, false))
            if (auto const* b = game::blockWith(block)) game::setBlock(region, hp, *b);
        log().info("boss: {} turned back into {} at {} {} {} (peaceful)", boss.getTypeName(), block, hp.x, hp.y, hp.z);
    } else {
        log().info("boss: {} left (peaceful)", boss.getTypeName());
    }
    removeBar(uidOf(boss));
    fe::removeLater(uidOf(boss)); // discard()
    return true;
}

/// The bar of a boss, made from its spec and the persisted style when it has none yet (a boss that
/// spawned, or came back with its chunk after its bar was dropped).
Bar& barOf(Mob& boss, BossSpec const& spec, fe::ActorState const& state) {
    auto [it, fresh] = S().bars.try_emplace(uidOf(boss));
    auto& bar        = it->second;
    if (!fresh) return bar;
    bar.lastTag = boss.getNameTag();
    bar.rgb     = spec.bar.rgb;
    bar.darken  = spec.bar.darken;
    bar.fog     = spec.bar.fog;
    // persisted style changes (Lich phases)
    if (state.data.is_object() && state.data.contains("boss:bar")) {
        auto const& b = state.data["boss:bar"];
        bar.rgb       = b.value("rgb", bar.rgb);
        bar.overlay   = b.value("overlay", bar.overlay);
        bar.darken    = b.value("darken", bar.darken);
        bar.fog       = b.value("fog", bar.fog);
    }
    return bar;
}

void onBossTick(Actor& actor, fe::ActorState const& state) {
    auto* boss = fe::asMob(actor);
    if (!boss) return;
    auto const* spec = bossSpec(actor.getTypeName());
    if (!spec) return;
    auto& bar = barOf(*boss, *spec, state);
    if (boss->isAlive()) {
        if (peacefulDespawn(*boss)) return;
        // BaseTFBoss#customServerAiStep -> tickBossBar
        int max = boss->getMaxHealth();
        setProgress(*boss, bar, max > 0 ? static_cast<float>(boss->getHealth()) / static_cast<float>(max) : 0.0f);
    }
    updateAudience(*boss, bar);
    // BaseTFBoss#setCustomName: a renamed boss renames its bar
    if (auto const& tag = boss->getNameTag(); tag != bar.lastTag) {
        bar.lastTag = tag;
        if (!bar.name) sendTitle(*boss, bar);
    }
}

void onBossSpawn(Actor& actor, fe::ActorState& state) {
    auto* boss = fe::asMob(actor);
    auto const* spec = bossSpec(actor.getTypeName());
    if (!boss || !spec) return;
    (void)barOf(*boss, *spec, state);
    // EntityEvents#adjustEntityHealthInMultiplayerFights runs once, on the boss's first spawn
    if (!state.loaded && !(state.data.is_object() && state.data.contains("boss:adjusted"))) {
        adjustHealth(*boss);
        gf::entity::data(actor, "boss:adjusted") = true;
    }
}

// ---- spawners (BossSpawnerBlockEntity) ----------------------------------------------------------

struct SpawnerDefault {
    char const* entityId;
    int         range;
    char const* particle;
    SpawnerKind kind;
    Json        params; ///< fx params of the particle (DustParticleOptions.REDSTONE: red, scale 1)
};
std::unordered_map<std::string, SpawnerDefault> const& spawnerDefaults() {
    static Json const redstone{{"color", {1.0, 0.0, 0.0}}, {"scale", 1.0}};
    static std::unordered_map<std::string, SpawnerDefault> const m{
        {"twilightforest:naga_boss_spawner",           {"twilightforest:naga", kLongRange, "minecraft:crit", SpawnerKind::Default, {}}                          },
        {"twilightforest:lich_boss_spawner",           {"twilightforest:lich", kShortRange, "twilightforest:ominous_flame", SpawnerKind::Lich, {}}             },
        {"twilightforest:hydra_boss_spawner",          {"twilightforest:hydra", kLongRange, "minecraft:flame", SpawnerKind::Default, {}}                        },
        {"twilightforest:ur_ghast_boss_spawner",       {"twilightforest:ur_ghast", kShortRange, "minecraft:dust", SpawnerKind::AboveMinus4, redstone}          },
        {"twilightforest:knight_phantom_boss_spawner", {"twilightforest:knight_phantom", kShortRange, "twilightforest:ominous_flame", SpawnerKind::KnightPhantoms, {}}},
        {"twilightforest:snow_queen_boss_spawner",     {"twilightforest:snow_queen", kShortRange, "twilightforest:snow", SpawnerKind::AboveMinus4, {}}         },
        {"twilightforest:minoshroom_boss_spawner",     {"twilightforest:minoshroom", kShortRange, "minecraft:crimson_spore", SpawnerKind::BelowPlus4, {}}       },
        {"twilightforest:alpha_yeti_boss_spawner",     {"twilightforest:alpha_yeti", kShortRange, "minecraft:falling_water", SpawnerKind::AboveMinus4, {}}      },
        {"twilightforest:final_boss_boss_spawner",     {"twilightforest:plateau_boss", kShortRange, "twilightforest:annihilate", SpawnerKind::Never, {}}        },
    };
    return m;
}

SpawnerKind kindOf(std::string const& blockId) {
    if (auto t = S().kindsForTest.find(blockId); t != S().kindsForTest.end()) return t->second;
    auto it = spawnerDefaults().find(blockId);
    return it != spawnerDefaults().end() ? it->second.kind : SpawnerKind::Default;
}

Json particleParamsOf(std::string const& blockId, std::string const& particle) {
    auto it = spawnerDefaults().find(blockId);
    return it != spawnerDefaults().end() && it->second.particle == particle ? it->second.params : Json();
}

/// The nearest player within `range` (Level#getNearestPlayer(x, y, z, range, false): no spectators).
Player* nearestPlayer(int dim, Vec3 const& c, double range) {
    Player* nearest = nullptr;
    double  best    = 1e30;
    for (auto* p : fe::playersNear(dim, c, range)) {
        if (p->isSpectator()) continue;
        double d = fe::mobs::distanceSqr(*p, c);
        if (d < best) best = d, nearest = p;
    }
    return nearest;
}

/// BossSpawnerBlockEntity#anyPlayerInRange (with the variants' height conditions).
bool anyPlayerInRange(int dim, BlockPos const& pos, SpawnerSpec const& spec) {
    Vec3    c{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f};
    Player* nearest = nearestPlayer(dim, c, spec.range);
    return nearest && heightRuleAllows(kindOf(spec.blockId), pos.y, nearest->getFeetPos().y);
}

/// The level's random source of the spawners (Level#getRandom).
fe::JavaRandom& spawnerRandom() {
    static fe::JavaRandom r(fe::freshSeed());
    return r;
}

Mob* spawnBoss(BlockSource& region, std::string const& entityId, Vec3 const& at, float yaw) {
    auto* lv = level();
    if (!lv) return nullptr;
    auto* mob = lv->getSpawner().spawnMob(region, ActorDefinitionIdentifier(entityId), nullptr, at, false, false, true);
    if (!mob) return nullptr;
    mob->setYBodyRotation(yaw);
    const_cast<Vec2&>(mob->getRotation()) = Vec2{0.0f, yaw};
    return mob;
}

/// BossSpawnerBlockEntity#spawnMyBoss and the variants'. Every spawned boss carries
/// data(boss, "boss:spawner") = the spawner block id, so its unit can run the variant's spawn
/// set-up (LichSpawnerBlockEntity: setAttackCooldown(40), setExtinguishTimer()).
bool spawnMyBoss(BlockSource& region, int dim, BlockPos const& pos, SpawnerSpec const& spec, SpawnerRuntime& rt) {
    auto&       random = spawnerRandom();
    SpawnerKind kind   = kindOf(spec.blockId);
    if (kind == SpawnerKind::Never && !spec.spawnFn) return false; // no spawning for you
    auto setHome = [&](Mob& mob, BlockPos const& home) {
        if (!gf::entity::home(mob)) gf::entity::setHome(mob, core::GlobalPos{dim, home.x, home.y, home.z}, 0);
        gf::entity::data(mob, "boss:spawner") = spec.blockId;
    };
    if (spec.spawnFn) {
        Mob* mob = spec.spawnFn(region, pos);
        if (!mob) return false;
        setHome(*mob, pos);
        return true;
    }
    if (kind == SpawnerKind::KnightPhantoms) {
        constexpr int count = 6;
        for (int i = rt.spawned; i < count; ++i) {
            float  angle    = (360.0F / count) * i;
            float  distance = 4.0F;
            double rx       = pos.x + 0.5 + std::cos(angle * std::numbers::pi / 180.0) * distance;
            double ry       = pos.y;
            double rz       = pos.z + 0.5 + std::sin(angle * std::numbers::pi / 180.0) * distance;
            Mob*   knight   = spawnBoss(region, spec.entityId, Vec3{float(rx), float(ry), float(rz)}, random.nextFloat() * 360.0F);
            if (!knight) continue;
            if (i == 5 && level() && level()->getDifficulty() == SharedTypes::Legacy::Difficulty::Hard)
                if (auto shield = game::items::make("twilightforest:knightmetal_shield")) knight->setOffhandSlot(*shield);
            setHome(*knight, pos);
            gf::entity::data(*knight, "knight_phantom:number") = i; // KnightPhantom#setNumber
            rt.spawned++;
        }
        return rt.spawned == count;
    }
    BlockPos spawnPos = pos;
    if (kind == SpawnerKind::Lich) {
        // LichSpawnerBlockEntity#spawnMyBoss: down through the air below the spawner, then one up
        BlockPos m = pos;
        while (region.getMinHeight() < m.y && region.getBlock(BlockPos{m.x, m.y - 1, m.z}).isAir()) m.y--;
        m.y += 1; // mutableBlockPos.move(Direction.UP)
        spawnPos = m;
    } else {
        // below the spawner when that block has no collision shape
        BlockPos below{pos.x, pos.y - 1, pos.z};
        if (fe::mobs::emptyCollision(region, below)) spawnPos = below;
    }
    Mob* mob = spawnBoss(region, spec.entityId, Vec3{spawnPos.x + 0.5f, float(spawnPos.y), spawnPos.z + 0.5f}, random.nextFloat() * 360.0F);
    if (!mob) return false;
    // BossSpawnerBlockEntity#initializeCreature: home at the spawner; the lich's at its spawn point
    setHome(*mob, kind == SpawnerKind::Lich ? spawnPos : pos);
    return true;
}

// ---- the sinister spawner (SinisterSpawnerLogic over BaseSpawner) --------------------------------

using gf::boss::ext::SinisterSpawner;

Json toJson(SinisterSpawner const& s) {
    Json potentials = Json::array();
    for (auto const& [id, w] : s.potentials) potentials.push_back({id, w});
    return {
        {"potentials",  potentials            },
        {"next",        s.nextEntity          },
        {"delay",       s.spawnDelay          },
        {"minDelay",    s.minSpawnDelay       },
        {"maxDelay",    s.maxSpawnDelay       },
        {"count",       s.spawnCount          },
        {"maxNearby",   s.maxNearbyEntities   },
        {"playerRange", s.requiredPlayerRange },
        {"range",       s.spawnRange          },
        {"scanRange",   s.entityScanRange     },
        {"particles",   s.particles           },
        {"loot",        s.lootTable           },
        {"params",      s.particleParams      },
    };
}

SinisterSpawner sinisterFromJson(Json const* j) {
    SinisterSpawner s;
    if (!j || !j->is_object()) return s;
    if (auto p = j->find("potentials"); p != j->end() && p->is_array())
        for (auto const& e : *p)
            if (e.is_array() && e.size() == 2 && e[0].is_string() && e[1].is_number_integer()) s.potentials.emplace_back(e[0].get<std::string>(), e[1].get<int>());
    s.nextEntity          = j->value("next", s.nextEntity);
    s.spawnDelay          = j->value("delay", s.spawnDelay);
    s.minSpawnDelay       = j->value("minDelay", s.minSpawnDelay);
    s.maxSpawnDelay       = j->value("maxDelay", s.maxSpawnDelay);
    s.spawnCount          = j->value("count", s.spawnCount);
    s.maxNearbyEntities   = j->value("maxNearby", s.maxNearbyEntities);
    s.requiredPlayerRange = j->value("playerRange", s.requiredPlayerRange);
    s.spawnRange          = j->value("range", s.spawnRange);
    s.entityScanRange     = j->value("scanRange", s.entityScanRange);
    if (auto p = j->find("particles"); p != j->end() && p->is_array())
        for (auto const& e : *p)
            if (e.is_string()) s.particles.push_back(e.get<std::string>());
    s.lootTable      = j->value("loot", s.lootTable);
    s.particleParams = j->value("params", Json());
    return s;
}

bool isSinisterBlock(std::string const& id) { return S().sinisterBlocks.contains(id); }

/// The spawner of a sinister block, read once from its block data.
SinisterRuntime& sinisterOf(BlockSource& region, BlockPos const& pos, SpawnerRuntime& rt) {
    if (!rt.sin.loaded) {
        rt.sin.cfg    = sinisterFromJson(bd::get(region, pos, kSinisterKey));
        rt.sin.loaded = true;
    }
    return rt.sin;
}

void saveSinister(BlockSource& region, BlockPos const& pos, SinisterSpawner const& cfg) { bd::set(region, pos, kSinisterKey, toJson(cfg)); }

/// WeightedRandomList#getRandom (empty: none).
std::string drawPotential(SinisterSpawner const& s, fe::JavaRandom& random) {
    int total = 0;
    for (auto const& [id, w] : s.potentials) total += std::max(0, w);
    if (total <= 0) return {};
    int i = random.nextInt(total);
    for (auto const& [id, w] : s.potentials) {
        i -= std::max(0, w);
        if (i < 0) return id;
    }
    return {};
}

/// BaseSpawner#delay (the spin event of broadcastEvent(1) is the block renderer's).
void sinisterDelay(SinisterSpawner& s, fe::JavaRandom& random) {
    if (s.maxSpawnDelay <= s.minSpawnDelay) s.spawnDelay = s.minSpawnDelay;
    else s.spawnDelay = s.minSpawnDelay + random.nextInt(s.maxSpawnDelay - s.minSpawnDelay);
    if (auto next = drawPotential(s, random); !next.empty()) s.nextEntity = next;
}

/// BoundingBoxUtils#isPosWithinBox: |dx|, |dy|, |dz| <= radius.
bool withinBox(BlockPos const& c, BlockPos const& p, int radius) {
    return std::abs(p.x - c.x) <= radius && std::abs(p.y - c.y) <= radius && std::abs(p.z - c.z) <= radius;
}

/// SinisterSpawnerLogic#scanSpawnPositions.
void scanSpawnPositions(BlockSource& region, BlockPos const& pos, SinisterRuntime& rt, fe::JavaRandom& random) {
    if (static_cast<int>(rt.spawnBuffer.size()) >= rt.countNextToSpawn) return; // spawn limit reached
    if (!rt.checkPos || !withinBox(pos, *rt.checkPos, rt.cfg.spawnRange)) {
        // restart from beginning, under the spawning block
        BlockPos below{pos.x, pos.y - 1, pos.z};
        rt.checkPos = region.getBlock(below).isAir() ? below : BlockPos{pos.x, pos.y + 1, pos.z};
    }
    auto&    c = *rt.checkPos;
    BlockPos under{c.x, c.y - 1, c.z};
    if (region.getBlock(under).isAir()) {
        c.y--;
        return;
    }
    bool floor = fe::mobs::legacySolid(region, under) || game::blockIdAt(region, under) == kCoronationCarpet;
    // if below is solid, then maybe record this position for being clear to spawn
    if (floor && random.nextBoolean() && std::find(rt.spawnBuffer.begin(), rt.spawnBuffer.end(), c) == rt.spawnBuffer.end()
        && region.getBlock(c).isAir())
        rt.spawnBuffer.push_back(c);
    // Direction.Plane.HORIZONTAL.getRandomDirection: north, east, south, west
    static int const dx[] = {0, 1, 0, -1}, dz[] = {-1, 0, 1, 0};
    int              d    = random.nextInt(4);
    BlockPos         next{c.x + dx[d], c.y, c.z + dz[d]};
    if (!fe::mobs::legacySolid(region, next)) c = next;
    else rt.checkPos.reset();
}

/// EntityType#getSpawnAABB is free of blocks and entities (the type's size is the engine's: the
/// standard 0.6 x 1.95 mob box is checked).
bool noCollision(BlockSource& region, Vec3 const& at) {
    AABB box{Vec3{at.x - 0.3f, at.y, at.z - 0.3f}, Vec3{at.x + 0.3f, at.y + 1.95f, at.z + 0.3f}};
    std::vector<AABB> shapes;
    region.fetchAABBs(shapes, box, false);
    for (auto const& s : shapes)
        if (s.min.x < box.max.x && s.max.x > box.min.x && s.min.y < box.max.y && s.max.y > box.min.y && s.min.z < box.max.z && s.max.z > box.min.z) return false;
    return true;
}

/// SinisterSpawnerLogic#serverTick.
void tickSinister(BlockSource& region, int dim, BlockPos const& pos, SpawnerRuntime& srt) {
    auto& rt     = sinisterOf(region, pos, srt);
    auto& cfg    = rt.cfg;
    auto& random = spawnerRandom();
    Vec3  c{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f};
    // BaseSpawner#isNearPlayer: Level#hasNearbyAlivePlayer (no spectators)
    if (!nearestPlayer(dim, c, cfg.requiredPlayerRange)) return;
    // the client's particles (SinisterSpawnerLogic#clientTick), one of each per tick
    for (auto const& p : cfg.particles)
        gf::fx::particle(region.getDimension(), p, Vec3{pos.x + float(random.nextDouble()), pos.y + float(random.nextDouble()), pos.z + float(random.nextDouble())}, {}, 1, cfg.particleParams.is_object() ? cfg.particleParams.value(p, Json()) : Json());
    if (rt.countNextToSpawn < 1) rt.countNextToSpawn = random.nextInt(1 + std::max(0, cfg.spawnCount));
    if (cfg.spawnDelay <= -1) sinisterDelay(cfg, random);
    scanSpawnPositions(region, pos, rt, random);
    if (cfg.spawnDelay > 0) {
        cfg.spawnDelay--;
        return;
    }
    if (rt.spawnBuffer.empty()) {
        // spawn buffer is empty, let spawnDelay tick over into resetting delay
        if (cfg.spawnDelay == 0) cfg.spawnDelay--;
        return;
    }
    // BaseSpawner#getOrCreateNextSpawnData
    if (cfg.nextEntity.empty()) cfg.nextEntity = drawPotential(cfg, random);
    std::string const entityId = cfg.nextEntity;
    bool              spawned  = false;
    int               scan     = cfg.entityScanRange >= 0 ? cfg.entityScanRange : cfg.spawnRange;
    auto*             lv       = level();
    for (auto const& at : rt.spawnBuffer) {
        if (entityId.empty() || !lv) {
            sinisterDelay(cfg, random); // EntityType.by(...) is empty: delay, the buffer stays
            return;
        }
        Vec3 spawnAt{at.x + 0.5f, float(at.y), at.z + 0.5f};
        if (!noCollision(region, spawnAt)) continue;
        // the mobs of the type around the spawner (counted before the new one is added, as Java)
        AABB around{Vec3{float(pos.x - scan), float(pos.y - scan), float(pos.z - scan)}, Vec3{float(pos.x + 1 + scan), float(pos.y + 1 + scan), float(pos.z + 1 + scan)}};
        int  like = 0;
        for (auto* e : region.getEntities(around, 0.0f))
            if (e && e->isAlive() && e->getTypeName() == entityId && !(e->isPlayer() && static_cast<Player*>(e)->isSpectator())) ++like;
        if (like >= cfg.maxNearbyEntities) {
            sinisterDelay(cfg, random);
            rt.spawnBuffer.clear();
            return;
        }
        Mob* mob = lv->getSpawner().spawnMob(region, ActorDefinitionIdentifier(entityId), nullptr, spawnAt, false, false, true);
        if (!mob) {
            sinisterDelay(cfg, random);
            rt.spawnBuffer.clear();
            return;
        }
        // SpawnPlacements#checkSpawnRules for SPAWNER: hostile mobs need a difficulty
        if (peaceful() && mob->hasCategory(ActorCategory::Monster)) {
            mob->remove();
            continue;
        }
        float yaw = random.nextFloat() * 360.0F;
        mob->setYBodyRotation(yaw);
        const_cast<Vec2&>(mob->getRotation()) = Vec2{0.0f, yaw};
        rt.checkPos = pos; // this.checkPos = blockEntityPos.mutable()
        auto const& bb = mob->getAABB();
        float       w  = bb.max.x - bb.min.x, h = bb.max.y - bb.min.y;
        for (auto const& p : cfg.particles) {
            Json params = cfg.particleParams.is_object() ? cfg.particleParams.value(p, Json::object()) : Json::object();
            if (!params.is_object()) params = Json::object();
            params["spread"] = {w * 0.5f, h * 0.5f, w * 0.5f};
            gf::fx::particle(region.getDimension(), p, Vec3{spawnAt.x, spawnAt.y + h * 0.5f, spawnAt.z}, {}, 10, params);
        }
        mob->spawnAnim();
        ++S().sinisterSpawned;
        spawned = true;
    }
    rt.spawnBuffer.clear(); // loop iterated, now reset
    if (spawned) {
        for (auto const& p : cfg.particles) {
            Json params = cfg.particleParams.is_object() ? cfg.particleParams.value(p, Json::object()) : Json::object();
            if (!params.is_object()) params = Json::object();
            params["spread"] = {1, 1, 1};
            gf::fx::particle(region.getDimension(), p, c, {}, 10, params);
        }
        log().info("boss: sinister spawner at {} {} {} spawned {}", pos.x, pos.y, pos.z, entityId);
        sinisterDelay(cfg, random);
    }
}

// ---- spawner discovery and ticking ----------------------------------------------------------------

/// Finds the registered spawner blocks around the players: every tick one horizontal slice of the
/// box (a margin beyond the longest range) of each player, chunk by chunk (palette-filtered).
void scanSpawners() {
    auto& s = S();
    // the spawner block types, rebuilt only when a registration changed
    static std::unordered_set<BlockType const*> types;
    static int                                  range = 0, version = -1;
    if (version != s.spawnerVersion) {
        types.clear();
        range = s.sinisterRange; // BaseSpawner#requiredPlayerRange of the sinister spawners
        for (auto const& [id, spec] : s.spawners) {
            if (auto const* b = game::blockWith(id)) types.insert(&b->getBlockType());
            range = std::max(range, spec.range);
        }
        for (auto const& id : s.sinisterBlocks)
            if (auto const* b = game::blockWith(id)) types.insert(&b->getBlockType());
        version = s.spawnerVersion;
    }
    if (types.empty()) return;
    auto* lv = level();
    if (!lv) return;
    int const r     = range + kScanMargin;
    int const slice = static_cast<int>(fe::currentTick() % kScanSlices);
    std::vector<std::pair<int, BlockPos>> found;
    lv->forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
        int   dim    = static_cast<int>(p.getDimensionId());
        auto* region = game::regionOf(dim);
        if (!region) return true;
        BlockPos c   = p.getFeetBlockPos();
        int      y0  = std::max<int>(c.y - r, region->getMinHeight());
        int      y1  = std::min<int>(c.y + r, region->getMaxHeight() - 1);
        int      h   = (y1 - y0 + kScanSlices) / kScanSlices;
        int      ys  = y0 + slice * h;
        int      ye  = std::min(y1, ys + h - 1);
        if (ys > ye) return true;
        for (int cx = (c.x - r) >> 4; cx <= (c.x + r) >> 4; ++cx)
            for (int cz = (c.z - r) >> 4; cz <= (c.z + r) >> 4; ++cz) {
                if (!region->hasChunksAt(BlockPos{cx * 16, ys, cz * 16}, 0, false)) continue;
                BoundingBox box{
                    BlockPos{std::max(cx * 16, c.x - r), ys, std::max(cz * 16, c.z - r)},
                    BlockPos{std::min(cx * 16 + 15, c.x + r), ye, std::min(cz * 16 + 15, c.z + r)}
                };
                auto hits = region->fetchBlocksInBox(box, [&](Block const& b) { return types.contains(&b.getBlockType()); });
                for (auto const& hit : hits) found.emplace_back(dim, hit.mDataPos);
            }
        return true;
    }));
    for (auto const& [dim, pos] : found) {
        core::GlobalPos gp{dim, pos.x, pos.y, pos.z};
        if (s.activeSpawners.contains(gp)) continue;
        auto* region = game::regionOf(dim);
        if (!region) continue;
        auto& rt    = s.activeSpawners[gp];
        rt.blockId  = game::blockIdAt(*region, pos);
        rt.sinister = isSinisterBlock(rt.blockId);
    }
}

/// BossSpawnerBlockEntity#tick / SinisterSpawnerBlockEntity#serverTick for the known spawners.
void tickSpawners() {
    auto& s = S();
    scanSpawners();
    for (auto it = s.activeSpawners.begin(); it != s.activeSpawners.end();) {
        auto const& gp     = it->first;
        auto*       region = game::regionOf(gp.dim);
        BlockPos    pos{gp.x, gp.y, gp.z};
        if (!region || !region->hasChunksAt(pos, 0, false)) {
            it = s.activeSpawners.erase(it);
            continue;
        }
        auto id = game::blockIdAt(*region, pos);
        if (it->second.sinister) {
            if (!isSinisterBlock(id)) {
                it = s.activeSpawners.erase(it);
                continue;
            }
            tickSinister(*region, gp.dim, pos, it->second);
            ++it;
            continue;
        }
        auto spec = s.spawners.find(id);
        if (spec == s.spawners.end()) {
            it = s.activeSpawners.erase(it);
            continue;
        }
        if (!anyPlayerInRange(gp.dim, pos, spec->second)) {
            ++it;
            continue;
        }
        if (peaceful()) {
            // the waiting particles (client side in Java)
            if (!spec->second.particle.empty()) {
                auto& r = spawnerRandom();
                gf::fx::particle(region->getDimension(), spec->second.particle, Vec3{pos.x + r.nextFloat(), pos.y + r.nextFloat(), pos.z + r.nextFloat()}, {}, 1, particleParamsOf(id, spec->second.particle));
            }
            ++it;
            continue;
        }
        if (spawnMyBoss(*region, gp.dim, pos, spec->second, it->second)) {
            if (auto* lv = level()) {
                BlockChangeContext ctx{};
                lv->destroyBlock(*region, pos, false, ctx);
            }
            log().info("boss: {} spawned {} at {} {} {}", id, spec->second.entityId, pos.x, pos.y, pos.z);
            it = s.activeSpawners.erase(it);
        } else ++it;
    }
}

/// The delay counters of the sinister spawners being ticked go into their block data before a save
/// (BaseSpawner#save writes Delay).
void writeBackSinister(core::GlobalPos const* only) {
    for (auto& [gp, rt] : S().activeSpawners) {
        if (!rt.sinister || !rt.sin.loaded || (only && !(*only == gp))) continue;
        if (auto* region = game::regionOf(gp.dim)) saveSinister(*region, BlockPos{gp.x, gp.y, gp.z}, rt.sin.cfg);
    }
}

// ---- trophies (BossVariant) ------------------------------------------------------------------------

struct VariantRow {
    char const*                 variant;
    gf::boss::TrophyVariant     floor;
};
std::vector<VariantRow> const& variants() {
    // AbstractTrophyBlock#playSound / #createParticle and TFBlocks' pedestal comparator strengths;
    // the pitch is the base (TF adds nextFloat() * 0.1), wall trophies give no comparator signal
    static std::vector<VariantRow> const rows{
        {"naga",           {"entity.twilightforest.naga.rattle", 1.25F, 1.2F, "minecraft:crit", 5}                       },
        {"lich",           {"entity.twilightforest.lich.ambient", 0.35F, 1.1F, "twilightforest:angry_lich", 6}           },
        {"hydra",          {"entity.twilightforest.hydra.growl", 1.0F, 1.2F, "", 12}                                     },
        {"ur_ghast",       {"entity.twilightforest.ur_ghast.ambient", 1.0F, 0.6F, "minecraft:dust", 13}                  },
        {"knight_phantom", {"entity.twilightforest.knight_phantom.ambient", 1.0F, 1.1F, "minecraft:item", 8}             },
        {"snow_queen",     {"entity.twilightforest.snow_queen.ambient", 1.0F, 0.9F, "twilightforest:snow_warning", 14}   },
        {"minoshroom",     {"entity.twilightforest.minoshroom.ambient", 0.75F, 0.7F, "minecraft:block", 7}               },
        {"alpha_yeti",     {"entity.twilightforest.alpha_yeti.growl", 0.75F, 0.75F, "minecraft:splash", 9}               },
        {"quest_ram",      {"entity.twilightforest.quest_ram.ambient", 1.0F, 0.7F, "twilightforest:magic_effect", 1}     },
        {"final_boss",     {"", 1.0F, 0.9F, "", 0}                                                                       },
    };
    return rows;
}

// ---- module ------------------------------------------------------------------------------------------

class BossModule {
public:
    void install() {
        static bool once = false;
        if (std::exchange(once, true)) return;
        fe::addInternalHooks({
            .interesting = [](std::string const& type) { return S().bosses.contains(type); },
            .tick        = [](Actor& actor, fe::ActorState& state) { onBossTick(actor, state); },
            .die =
                [](Actor& actor, std::string const& damageType, Actor* killer) {
                    if (auto* mob = fe::asMob(actor)) onBossDie(*mob, damageType, killer);
                },
            .spawn = [](Actor& actor, fe::ActorState& state) { onBossSpawn(actor, state); },
        });
        fe::onLevelTick([] {
            tickDying();
            for (auto it = S().pendingFills.begin(); it != S().pendingFills.end();) {
                if (fillContainer(it->pos, it->items)) {
                    it = S().pendingFills.erase(it);
                } else if (--it->tries <= 0) {
                    log().warn("boss: the death chest of {} at {} {} {} has no container", it->type, it->pos.x, it->pos.y, it->pos.z);
                    it = S().pendingFills.erase(it);
                } else ++it;
            }
            tickSpawners();
            // bars of bosses that vanished without dying (unloaded, /kill'ed past the hook)
            if (fe::currentTick() % 20 == 0) {
                std::vector<std::int64_t> gone;
                for (auto const& [uid, bar] : S().bars)
                    if (!fe::actorById(uid) && std::none_of(S().dying.begin(), S().dying.end(), [&](Dying const& d) { return d.uid == uid; }))
                        gone.push_back(uid);
                for (auto uid : gone) removeBar(uid);
                std::erase_if(S().discarding, [](std::int64_t uid) { return !fe::actorById(uid); });
            }
        });
        // EntityEvents#addQualifiedGroupPlayerIfNeeded: melee hits now, every damage through combat
        S().handlers.push_back(ia::onAttacked({}, [](ia::EntityContext const& c, bool hit) {
            if (hit) qualify(c.entity, &c.player);
        }));
        gf::combat::onDealt(gf::combat::priority::Normal, [](gf::combat::DamageInfo const& d) {
            qualify(d.victim, d.attacker);
            // BaseTFBoss#lavaHurt: a boss in lava clears the lava around it
            if (d.type == "minecraft:lava" && bossSpec(d.victim.getTypeName())) {
                auto& region = d.victim.getDimensionBlockSource();
                auto  bb     = d.victim.getAABB();
                for (double x = bb.min.x - 9; x < bb.max.x + 9; x++)
                    for (double z = bb.min.z - 9; z < bb.max.z + 9; z++)
                        for (double y = bb.min.y - 9; y < bb.max.y + 9; y++) {
                            BlockPos p{static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)), static_cast<int>(std::floor(z))};
                            auto     id = game::blockIdAt(region, p);
                            if (id == "minecraft:lava" || id == "minecraft:flowing_lava") game::clearBlock(region, p);
                        }
            }
        });
        // BaseTFBoss#shouldDropLoot = !bossDropChests: the loot table goes into the death chest
        // only, the death pipeline's drops of the table are dropped (equipment is not in them)
        gf::loot::onEntityDrops([](Actor& dead, std::vector<ItemStack>& drops, gf::loot::LootContext const&) {
            if (config().bossesSpawnDropChests && bossSpec(dead.getTypeName())) drops.clear();
        });
        // the default spawner logic of TF's ten BossSpawnerBlockEntity types (units may replace
        // a spec with registerSpawner; the height conditions stay with the block id)
        for (auto const& [block, def] : spawnerDefaults()) {
            if (S().spawners.contains(block)) continue;
            S().spawners[block] = SpawnerSpec{.blockId = block, .entityId = def.entityId, .range = def.range, .particle = def.particle};
        }
        S().spawnerVersion++;
        bd::onWriteBack([](core::GlobalPos const* pos) { writeBackSinister(pos); });
        game::db::onWorldClosed([] {
            S().bars.clear();
            S().qualified.clear();
            S().dying.clear();
            S().activeSpawners.clear();
            S().pendingFills.clear();
            S().discarding.clear();
        });
    }
    void uninstall() {}
};

} // namespace

bool heightRuleAllows(SpawnerKind kind, int spawnerY, double playerY) {
    switch (kind) {
    case SpawnerKind::AboveMinus4:
    case SpawnerKind::Lich: return playerY > spawnerY - 4; // closestPlayer.getY() > pos.getY() - 4
    case SpawnerKind::AboveMinus2:
    case SpawnerKind::KnightPhantoms: return playerY > spawnerY - 2;
    case SpawnerKind::BelowPlus4: return playerY < spawnerY + 4;
    default: return true;
    }
}

// test seams (BossTests.cpp)
void markInclusiveForTest(std::string const& type) { S().inclusiveForTest.insert(type); }
void unmarkInclusiveForTest(std::string const& type) { S().inclusiveForTest.erase(type); }
std::optional<core::GlobalPos> lastDeathChest() { return S().lastChest; }
std::optional<std::pair<core::GlobalPos, std::string>> lastConquest() { return S().lastConquest; }
void resetTestRecords() {
    S().lastChest.reset();
    S().lastConquest.reset();
}
bool hasBar(std::int64_t uid) { return S().bars.contains(uid); }
void unregisterSpawnerForTest(std::string const& blockId) {
    S().spawners.erase(blockId);
    S().spawnerVersion++;
    std::erase_if(S().activeSpawners, [&](auto const& kv) { return kv.second.blockId == blockId; });
}
void unregisterBossForTest(std::string const& entityId) {
    S().bosses.erase(entityId);
    fe::invalidateInterest();
}
std::optional<SpawnerRule> spawnerRuleForTest(std::string const& blockId) {
    auto it = S().spawners.find(blockId);
    if (it == S().spawners.end()) return std::nullopt;
    return SpawnerRule{it->second.entityId, it->second.range, it->second.particle, kindOf(blockId), particleParamsOf(blockId, it->second.particle)};
}
bool anyPlayerInRangeForTest(int dim, BlockPos const& pos, std::string const& blockId) {
    auto it = S().spawners.find(blockId);
    return it != S().spawners.end() && anyPlayerInRange(dim, pos, it->second);
}
void registerSinisterBlockForTest(std::string const& blockId) {
    S().sinisterBlocks.insert(blockId);
    S().spawnerVersion++;
}
void unregisterSinisterBlockForTest(std::string const& blockId) {
    S().sinisterBlocks.erase(blockId);
    S().spawnerVersion++;
    std::erase_if(S().activeSpawners, [&](auto const& kv) { return kv.second.blockId == blockId; });
}
int sinisterSpawnedForTest() { return S().sinisterSpawned; }
void setSpawnerKindForTest(std::string const& blockId, std::optional<SpawnerKind> kind) {
    if (kind) S().kindsForTest[blockId] = *kind;
    else S().kindsForTest.erase(blockId);
}

} // namespace gf::feature::boss

// ---- contract ---------------------------------------------------------------------------------------

namespace gf::boss {

namespace fb = gf::feature::boss;
namespace fe = gf::feature::entity;

void registerBoss(BossSpec spec) {
    auto id             = spec.entityId;
    fb::S().bosses[id] = std::move(spec);
    fe::invalidateInterest();
}

void registerSpawner(SpawnerSpec spec) {
    auto id               = spec.blockId;
    fb::S().spawners[id] = std::move(spec);
    fb::S().spawnerVersion++;
}

void BossBar::setProgress(float fraction) const {
    auto* a  = fe::actorById(mobId);
    auto  it = fb::S().bars.find(mobId);
    if (!a || it == fb::S().bars.end()) return;
    if (auto* mob = fe::asMob(*a)) fb::setProgress(*mob, it->second, fraction);
}

void BossBar::setName(std::string const& name) const {
    auto* a  = fe::actorById(mobId);
    auto  it = fb::S().bars.find(mobId);
    if (!a || it == fb::S().bars.end()) return;
    auto* mob = fe::asMob(*a);
    if (!mob) return;
    it->second.name = name;
    fb::sendTitle(*mob, it->second);
}

void BossBar::setVisible(bool visible) const {
    auto* a  = fe::actorById(mobId);
    auto  it = fb::S().bars.find(mobId);
    if (!a || it == fb::S().bars.end() || it->second.visible == visible) return;
    auto* mob = fe::asMob(*a);
    if (!mob) return;
    auto& bar = it->second;
    if (!visible) {
        for (auto pid : bar.players)
            if (auto* p = fe::playerById(pid)) fb::removeViewer(mobId, *p);
        bar.visible = false;
    } else {
        bar.visible  = true;
        auto players = std::move(bar.players);
        bar.players.clear();
        for (auto pid : players)
            if (auto* p = fe::playerById(pid)) fb::addViewer(*mob, bar, *p);
    }
}

BossBar bar(Mob& boss) { return BossBar{fe::uidOf(boss)}; }

void setMinion(Actor& actor, bool minion) {
    auto& v = gf::entity::data(actor, "boss:minion");
    v       = minion;
}

bool isMinion(Actor const& actor) {
    auto* s = fe::findState(fe::uidOf(actor));
    if (!s || !s->data.is_object() || !s->data.contains("boss:minion")) return false;
    auto const& v = s->data["boss:minion"];
    return v.is_boolean() && v.get<bool>();
}

std::vector<Player*> participants(Mob const& boss) {
    std::vector<Player*> out;
    auto                 it = fb::S().qualified.find(fe::uidOf(boss));
    if (it == fb::S().qualified.end()) return out;
    for (auto pid : it->second)
        if (auto* p = fe::playerById(pid)) out.push_back(p);
    return out;
}

std::optional<TrophyVariant> trophyVariant(std::string const& blockId) {
    std::string key = blockId;
    if (key.starts_with("twilightforest:")) key = key.substr(15);
    bool wall = false;
    if (key.ends_with("_wall_trophy")) key = key.substr(0, key.size() - 12), wall = true;
    else if (key.ends_with("_trophy")) key = key.substr(0, key.size() - 7);
    for (auto const& row : fb::variants()) {
        if (key != row.variant) continue;
        TrophyVariant v = row.floor;
        if (wall) v.comparator = 0; // TrophyWallBlock passes 0
        return v;
    }
    return std::nullopt;
}

namespace ext {

void setBarStyle(Mob& boss, unsigned rgb, Overlay overlay, bool allowLerp) {
    auto const* spec = fb::bossSpec(boss.getTypeName());
    if (!spec) return;
    auto& bar    = fb::barOf(boss, *spec, fe::stateOf(boss));
    int   ov     = static_cast<int>(overlay);
    bool  change = bar.rgb != rgb || bar.overlay != ov;
    if (!change) return;
    bar.rgb                                 = rgb;
    bar.overlay                             = ov;
    gf::entity::data(boss, "boss:bar")      = {{"rgb", bar.rgb}, {"overlay", bar.overlay}, {"darken", bar.darken}, {"fog", bar.fog}};
    fb::forEachViewer(bar, [&](Player& p) {
        fb::sendBossEvent(p, fe::uidOf(boss), BossEventUpdateType::UpdateStyle, fb::barTitle(boss, p), bar.progress, fb::nearest(rgb), ov);
        fb::sendCompanionBar(p, boss, bar, "style", allowLerp);
    });
}

void setBarFlags(Mob& boss, bool darken, bool fog) {
    auto const* spec = fb::bossSpec(boss.getTypeName());
    if (!spec) return;
    auto& bar = fb::barOf(boss, *spec, fe::stateOf(boss));
    if (bar.darken == darken && bar.fog == fog) return;
    bar.darken                          = darken;
    bar.fog                             = fog;
    gf::entity::data(boss, "boss:bar") = {{"rgb", bar.rgb}, {"overlay", bar.overlay}, {"darken", bar.darken}, {"fog", bar.fog}};
    fb::forEachViewer(bar, [&](Player& p) { fb::sendCompanionBar(p, boss, bar, "style"); });
}

std::optional<unsigned> barColor(Mob& boss) {
    auto const* spec = fb::bossSpec(boss.getTypeName());
    if (!spec) return std::nullopt;
    return fb::barOf(boss, *spec, fe::stateOf(boss)).rgb;
}

int nearestBarColor(unsigned rgb) { return fb::nearest(rgb); }

int deathTime(Mob const& boss) {
    for (auto const& d : fb::S().dying)
        if (d.uid == fe::uidOf(boss)) return d.deathTime;
    return 0;
}

std::vector<ItemStack> lootBuffer(Mob& boss) {
    std::vector<ItemStack> out;
    auto&                  buffer = fb::bufferOf(boss);
    for (auto const& j : buffer) out.push_back(j.is_null() ? game::items::empty() : game::items::fromJson(j));
    return out;
}

void setLootBufferSlot(Mob& boss, int slot, ItemStack const& stack) {
    if (slot < 0 || slot >= fb::kContainerSize) return;
    fb::setBufferSlot(fb::bufferOf(boss), slot, stack);
}

bool dropChests() { return fb::config().bossesSpawnDropChests; }
bool adjustsLootRolls() {
    auto a = fb::config().multiplayerFightAdjuster;
    return a == fb::MultiplayerFightAdjuster::MORE_LOOT || a == fb::MultiplayerFightAdjuster::MORE_LOOT_AND_HEALTH;
}
bool adjustsHealth() {
    auto a = fb::config().multiplayerFightAdjuster;
    return a == fb::MultiplayerFightAdjuster::MORE_HEALTH || a == fb::MultiplayerFightAdjuster::MORE_LOOT_AND_HEALTH;
}

std::optional<RareSound> trophyRareSound(std::string const& blockId) {
    if (blockId == "twilightforest:alpha_yeti_trophy" || blockId == "twilightforest:alpha_yeti_wall_trophy" || blockId == "alpha_yeti")
        return RareSound{"entity.twilightforest.alpha_yeti.roar", 50};
    return std::nullopt;
}

namespace {
/// The sinister spawner runtime of `pos` when it is being ticked (nullptr: not active).
fb::SpawnerRuntime* activeSinister(BlockSource& region, BlockPos const& pos) {
    core::GlobalPos gp = game::globalPos(region, pos);
    auto            it = fb::S().activeSpawners.find(gp);
    return it != fb::S().activeSpawners.end() && it->second.sinister ? &it->second : nullptr;
}
} // namespace

bool setSinisterSpawner(BlockSource& region, BlockPos const& pos, SinisterSpawner const& spawner) {
    if (!fb::isSinisterBlock(game::blockIdAt(region, pos))) return false;
    fb::saveSinister(region, pos, spawner);
    if (spawner.requiredPlayerRange > fb::S().sinisterRange) {
        fb::S().sinisterRange = spawner.requiredPlayerRange; // the scan box reaches it in time
        fb::S().spawnerVersion++;
    }
    if (auto* rt = activeSinister(region, pos)) {
        rt->sin.cfg    = spawner;
        rt->sin.loaded = true;
    }
    return true;
}

std::optional<SinisterSpawner> sinisterSpawner(BlockSource& region, BlockPos const& pos) {
    if (!fb::isSinisterBlock(game::blockIdAt(region, pos))) return std::nullopt;
    if (auto* rt = activeSinister(region, pos); rt && rt->sin.loaded) return rt->sin.cfg;
    return fb::sinisterFromJson(fb::bd::get(region, pos, fb::kSinisterKey));
}

namespace {
template <class Fn>
bool editSinister(BlockSource& region, BlockPos const& pos, Fn&& fn) {
    auto s = sinisterSpawner(region, pos);
    if (!s) return false;
    if (!fn(*s)) return false;
    return setSinisterSpawner(region, pos, *s);
}
} // namespace

bool setSinisterParticles(BlockSource& region, BlockPos const& pos, std::vector<std::string> const& particles) {
    return editSinister(region, pos, [&](SinisterSpawner& s) {
        // a set: duplicates once, order kept
        std::vector<std::string> unique;
        for (auto const& p : particles)
            if (std::find(unique.begin(), unique.end(), p) == unique.end()) unique.push_back(p);
        if (unique == s.particles) return false;
        s.particles = std::move(unique);
        return true;
    });
}

bool addSinisterParticle(BlockSource& region, BlockPos const& pos, std::string const& particle) {
    return editSinister(region, pos, [&](SinisterSpawner& s) {
        if (std::find(s.particles.begin(), s.particles.end(), particle) != s.particles.end()) return false;
        s.particles.push_back(particle);
        return true;
    });
}

bool removeSinisterParticle(BlockSource& region, BlockPos const& pos, std::string const& particle) {
    return editSinister(region, pos, [&](SinisterSpawner& s) { return std::erase(s.particles, particle) > 0; });
}

bool setSinisterLootTable(BlockSource& region, BlockPos const& pos, std::string const& tableId) {
    return editSinister(region, pos, [&](SinisterSpawner& s) {
        if (s.lootTable == tableId) return false;
        s.lootTable = tableId;
        return true;
    });
}

std::string sinisterLootTable(BlockSource& region, BlockPos const& pos) {
    auto s = sinisterSpawner(region, pos);
    return s ? s->lootTable : std::string();
}

} // namespace ext

} // namespace gf::boss

GF_REGISTER_MODULE_ORDERED(gf::feature::boss::BossModule, gf::ModuleOrder::Contracts + 2);
