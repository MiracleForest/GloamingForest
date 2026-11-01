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
//     companion protocol carries TFBossBarPacket's exact RGB, overlay, darken and fog
//     ("boss.bar") and UpdateDeathTimePacket ("boss.death_time").
//   - Death (postmortem): the loot table rolls into the 27-slot buffer (IBossLootBuffer, saved
//     with the boss), the home landmark is conquered (gf::landmark), "hurt_boss" fires for every
//     qualified player; after the death animation the buffer goes into the death chest under the
//     home point (gf::inventory, or a vanilla container) with the chest sound and poof.
//   - Peaceful: the boss turns back into its spawner at home and disappears.
//   - Spawners (BossSpawnerBlockEntity): custom blocks have no block entities, so the spawner
//     blocks around players are found with the chunk palettes (BlockSource::fetchBlocksInBox)
//     and ticked from the level tick (range, particles, peaceful, self-destruct).
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
#include "mc/deps/shared_types/legacy/Difficulty.h"

#include "feature/combat/Combat.h"
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

/// Config section "bosses" (docs/spec/config.md): TFConfig.bossDropChests and
/// multiplayerFightAdjuster (NONE, MORE_LOOT, MORE_HEALTH, MORE_LOOT_AND_HEALTH).
struct BossesConfig {
    int         version                  = 1;
    bool        bossesSpawnDropChests    = true;
    std::string multiplayerFightAdjuster = "NONE";
};

} // namespace gf::feature::boss

GF_REGISTER_CONFIG("bosses", gf::feature::boss::BossesConfig);

namespace gf::feature::boss {

namespace fe = gf::feature::entity;
namespace ia = gf::interactions;
using gf::boss::BossSpec;
using gf::boss::SpawnerSpec;
using Json = nlohmann::json;

namespace {

constexpr int    kContainerSize = 27;   ///< IBossLootBuffer.CONTAINER_SIZE
constexpr double kTrackRange    = 160.; ///< Java tracks bosses as far as the view distance (10 chunks)
constexpr int    kShortRange    = 9;    ///< BossSpawnerBlockEntity.SHORT_RANGE
constexpr int    kLongRange     = 50;   ///< BossSpawnerBlockEntity.LONG_RANGE
constexpr char const* kInclusiveTag = "twilightforest:multiplayer_inclusive_entities";

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

/// Per-spawner position state (KnightPhantomSpawnerBlockEntity#spawned).
struct SpawnerRuntime {
    std::string blockId;
    int         spawned = 0;
};

struct State {
    std::unordered_map<std::string, BossSpec>    bosses;
    std::unordered_map<std::string, SpawnerSpec> spawners; ///< by block id
    std::unordered_map<std::int64_t, Bar>        bars;     ///< by boss uid
    std::unordered_map<std::int64_t, std::vector<std::int64_t>> qualified; ///< MultiplayerInclusivityAttachment
    std::vector<Dying>                           dying;
    std::map<core::GlobalPos, SpawnerRuntime>    activeSpawners;
    std::unordered_set<std::string>              inclusiveForTest;
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

/// TFBossBarPacket for companions.
void sendCompanionBar(Player& player, Mob& boss, Bar const& bar, char const* op, bool allowLerp = true) {
    if (!gf::net::isCompanion(player)) return;
    gf::net::send(
        player,
        "boss.bar",
        {
            {"op",      op                                                 },
            {"id",      uidOf(boss)                                        },
            {"rid",     static_cast<std::int64_t>(boss.getRuntimeID().rawID)},
            {"rgb",     bar.rgb                                            },
            {"overlay", bar.overlay                                        },
            {"darken",  bar.darken                                         },
            {"fog",     bar.fog                                            },
            {"lerp",    allowLerp                                          },
            {"progress", bar.progress                                      },
        }
    );
}

void addViewer(Mob& boss, Bar& bar, Player& player) {
    if (!bar.players.insert(uidOf(player)).second) return;
    if (!bar.visible) return;
    sendBossEvent(player, uidOf(boss), BossEventUpdateType::Add, barTitle(boss, player), bar.progress, nearest(bar.rgb), bar.overlay);
    sendCompanionBar(player, boss, bar, "add");
    // BaseTFBoss#startSeenByPlayer: a dying boss sends its death time
    if (boss.getDeathTime() > 0 && gf::net::isCompanion(player))
        gf::net::send(player, "boss.death_time", {{"id", uidOf(boss)}, {"rid", static_cast<std::int64_t>(boss.getRuntimeID().rawID)}, {"deathTime", boss.getDeathTime()}});
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

/// IBossLootBuffer#tryDeposit.
bool tryDeposit(Dying const& d, Block const& chest, BlockPos const& pos, fe::JavaRandom& random, std::vector<ItemStack> const& items) {
    auto* region = game::regionOf(d.dim);
    if (!region || !region->hasChunksAt(pos, 0, false)) return false;
    bool isChest = game::blockIdAt(*region, pos) == d.chestBlock;
    if (!isChest) {
        if (!game::replaceable(*region, pos) || region->getBlockEntity(pos)) return false;
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
        // BaseTFBoss#shouldDropLoot: without death chests the loot drops as usual
        if (!config().bossesSpawnDropChests) {
            auto* lv = level();
            for (auto const& s : rollLoot(boss, *spec, damageType, killer))
                if (lv && !game::items::isEmpty(s)) lv->getSpawner().spawnItem(boss.getDimensionBlockSource(), s, nullptr, boss.getFeetPos(), 0);
        }
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

/// BaseTFBoss#tickDeath for the dying bosses, then #postRemoval.
void tickDying() {
    auto& dying = S().dying;
    for (auto it = dying.begin(); it != dying.end();) {
        auto& d = *it;
        d.deathTime++;
        auto* actor = fe::actorById(d.uid);
        if (d.deathTime < d.finish) {
            if (actor)
                if (auto bar = S().bars.find(d.uid); bar != S().bars.end())
                    forEachViewer(bar->second, [&](Player& p) {
                        if (gf::net::isCompanion(p))
                            gf::net::send(p, "boss.death_time", {{"id", d.uid}, {"rid", static_cast<std::int64_t>(actor->getRuntimeID().rawID)}, {"deathTime", d.deathTime}});
                    });
            ++it;
            continue;
        }
        removeBar(d.uid);
        if (actor) actor->remove(); // RemovalReason.KILLED
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
        if (!bar.name)
            forEachViewer(bar, [&](Player& p) {
                sendBossEvent(p, uidOf(*boss), BossEventUpdateType::UpdateName, barTitle(*boss, p), bar.progress, nearest(bar.rgb), bar.overlay);
            });
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

/// The per-variant player conditions and spawn logic of TF's spawner block entities.
enum class SpawnerKind { Default, AboveMinus4, AboveMinus2, BelowPlus4, Lich, KnightPhantoms, Never };
struct SpawnerDefault {
    char const* entityId;
    int         range;
    char const* particle;
    SpawnerKind kind;
};
std::unordered_map<std::string, SpawnerDefault> const& spawnerDefaults() {
    static std::unordered_map<std::string, SpawnerDefault> const m{
        {"twilightforest:naga_boss_spawner",           {"twilightforest:naga", kLongRange, "minecraft:crit", SpawnerKind::Default}                         },
        {"twilightforest:lich_boss_spawner",           {"twilightforest:lich", kShortRange, "twilightforest:ominous_flame", SpawnerKind::Lich}            },
        {"twilightforest:hydra_boss_spawner",          {"twilightforest:hydra", kLongRange, "minecraft:flame", SpawnerKind::Default}                       },
        {"twilightforest:ur_ghast_boss_spawner",       {"twilightforest:ur_ghast", kShortRange, "minecraft:flame", SpawnerKind::Default}                   },
        {"twilightforest:knight_phantom_boss_spawner", {"twilightforest:knight_phantom", kShortRange, "twilightforest:ominous_flame", SpawnerKind::KnightPhantoms}},
        {"twilightforest:snow_queen_boss_spawner",     {"twilightforest:snow_queen", kShortRange, "minecraft:snowflake", SpawnerKind::Default}             },
        {"twilightforest:minoshroom_boss_spawner",     {"twilightforest:minoshroom", kShortRange, "minecraft:crimson_spore", SpawnerKind::BelowPlus4}      },
        {"twilightforest:alpha_yeti_boss_spawner",     {"twilightforest:alpha_yeti", kShortRange, "minecraft:falling_water", SpawnerKind::AboveMinus4}     },
        {"twilightforest:final_boss_boss_spawner",     {"twilightforest:plateau_boss", kShortRange, "twilightforest:annihilate", SpawnerKind::Never}       },
    };
    return m;
}

SpawnerKind kindOf(std::string const& blockId) {
    auto it = spawnerDefaults().find(blockId);
    return it != spawnerDefaults().end() ? it->second.kind : SpawnerKind::Default;
}

/// BossSpawnerBlockEntity#anyPlayerInRange (with the variants' height conditions).
bool anyPlayerInRange(int dim, BlockPos const& pos, SpawnerSpec const& spec) {
    Vec3        c{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f};
    SpawnerKind kind    = kindOf(spec.blockId);
    Player*     nearest = nullptr;
    double      best    = 1e30;
    for (auto* p : fe::playersNear(dim, c, spec.range)) {
        if (p->isSpectator()) continue;
        double d = fe::mobs::distanceSqr(*p, c);
        if (d < best) best = d, nearest = p;
    }
    if (!nearest) return false;
    double y = nearest->getFeetPos().y;
    switch (kind) {
    case SpawnerKind::AboveMinus4:
    case SpawnerKind::Lich: return y > pos.y - 4;
    case SpawnerKind::AboveMinus2:
    case SpawnerKind::KnightPhantoms: return y > pos.y - 2;
    case SpawnerKind::BelowPlus4: return y < pos.y + 4;
    default: return true;
    }
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

/// BossSpawnerBlockEntity#spawnMyBoss and the variants'.
bool spawnMyBoss(BlockSource& region, int dim, BlockPos const& pos, SpawnerSpec const& spec, SpawnerRuntime& rt) {
    auto&       random = spawnerRandom();
    SpawnerKind kind   = kindOf(spec.blockId);
    if (kind == SpawnerKind::Never && !spec.spawnFn) return false; // no spawning for you
    auto setHome = [&](Mob& mob, BlockPos const& home) {
        if (!gf::entity::home(mob)) gf::entity::setHome(mob, core::GlobalPos{dim, home.x, home.y, home.z}, 0);
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
        // the lich stands on the floor below its spawner
        BlockPos m = pos;
        while (region.getMinHeight() < m.y && region.getBlock(BlockPos{m.x, m.y - 1, m.z}).isAir()) m.y--;
        spawnPos = m;
    } else {
        // below the spawner when that block has no collision
        BlockPos below{pos.x, pos.y - 1, pos.z};
        if (game::replaceable(region, below)) spawnPos = below;
    }
    Mob* mob = spawnBoss(region, spec.entityId, Vec3{spawnPos.x + 0.5f, float(spawnPos.y), spawnPos.z + 0.5f}, random.nextFloat() * 360.0F);
    if (!mob) return false;
    setHome(*mob, kind == SpawnerKind::Lich ? spawnPos : pos);
    return true;
}

/// Finds registered spawner blocks around the players (palette-filtered, cheap).
void scanSpawners() {
    auto& s = S();
    if (s.spawners.empty()) return;
    // the registered spawner block types, rebuilt only when a registration changed
    static std::unordered_set<BlockType const*> types;
    static int                                  range = 0, version = -1;
    if (version != s.spawnerVersion) {
        types.clear();
        range = 0;
        for (auto const& [id, spec] : s.spawners) {
            if (auto const* b = game::blockWith(id)) types.insert(&b->getBlockType());
            range = std::max(range, spec.range);
        }
        version = s.spawnerVersion;
    }
    if (types.empty()) return;
    auto* lv = level();
    if (!lv) return;
    std::vector<std::pair<int, BlockPos>> found;
    lv->forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
        int   dim    = static_cast<int>(p.getDimensionId());
        auto* region = game::regionOf(dim);
        if (!region) return true;
        BlockPos c = p.getFeetBlockPos();
        int      r = range;
        while (r > 8 && !region->hasChunksAt(c, r, false)) r /= 2;
        BoundingBox box{BlockPos{c.x - r, std::max<int>(c.y - r, region->getMinHeight()), c.z - r}, BlockPos{c.x + r, std::min<int>(c.y + r, region->getMaxHeight() - 1), c.z + r}};
        auto        hits = region->fetchBlocksInBox(box, [&](Block const& b) { return types.contains(&b.getBlockType()); });
        for (auto const& h : hits) found.emplace_back(dim, h.mDataPos);
        return true;
    }));
    for (auto const& [dim, pos] : found) {
        core::GlobalPos gp{dim, pos.x, pos.y, pos.z};
        if (s.activeSpawners.contains(gp)) continue;
        auto* region = game::regionOf(dim);
        if (!region) continue;
        s.activeSpawners[gp].blockId = game::blockIdAt(*region, pos);
    }
}

/// BossSpawnerBlockEntity#tick for the known spawners.
void tickSpawners() {
    auto& s = S();
    if (fe::currentTick() % 20 == 0) scanSpawners();
    for (auto it = s.activeSpawners.begin(); it != s.activeSpawners.end();) {
        auto const& gp     = it->first;
        auto*       region = game::regionOf(gp.dim);
        BlockPos    pos{gp.x, gp.y, gp.z};
        if (!region || !region->hasChunksAt(pos, 0, false)) {
            it = s.activeSpawners.erase(it);
            continue;
        }
        auto id   = game::blockIdAt(*region, pos);
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
                gf::fx::particle(region->getDimension(), spec->second.particle, Vec3{pos.x + r.nextFloat(), pos.y + r.nextFloat(), pos.z + r.nextFloat()});
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
        // the default spawner logic of TF's ten BossSpawnerBlockEntity types (units may replace
        // a spec with registerSpawner; the height conditions stay with the block id)
        for (auto const& [block, def] : spawnerDefaults()) {
            if (S().spawners.contains(block)) continue;
            S().spawners[block] = SpawnerSpec{.blockId = block, .entityId = def.entityId, .range = def.range, .particle = def.particle};
        }
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

// test seams (BossTests.cpp)
void markInclusiveForTest(std::string const& type) { S().inclusiveForTest.insert(type); }
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

} // namespace gf::feature::boss

// ---- contract ---------------------------------------------------------------------------------------

namespace gf::boss {

namespace fb = gf::feature::boss;
namespace fe = gf::feature::entity;

void registerBoss(BossSpec spec) {
    auto id             = spec.entityId;
    fb::S().bosses[id] = std::move(spec);
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
    fb::forEachViewer(it->second, [&](Player& p) {
        fb::sendBossEvent(p, mobId, BossEventUpdateType::UpdateName, name, it->second.progress, fb::nearest(it->second.rgb), it->second.overlay);
    });
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
    auto const& a = fb::config().multiplayerFightAdjuster;
    return a == "MORE_LOOT" || a == "MORE_LOOT_AND_HEALTH";
}
bool adjustsHealth() {
    auto const& a = fb::config().multiplayerFightAdjuster;
    return a == "MORE_HEALTH" || a == "MORE_LOOT_AND_HEALTH";
}

std::optional<RareSound> trophyRareSound(std::string const& blockId) {
    if (blockId == "twilightforest:alpha_yeti_trophy" || blockId == "twilightforest:alpha_yeti_wall_trophy" || blockId == "alpha_yeti")
        return RareSound{"entity.twilightforest.alpha_yeti.roar", 50};
    return std::nullopt;
}

} // namespace ext

} // namespace gf::boss

GF_REGISTER_MODULE_ORDERED(gf::feature::boss::BossModule, gf::ModuleOrder::Contracts + 2);
