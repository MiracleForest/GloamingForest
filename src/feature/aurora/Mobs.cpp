// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/monster/BaseIceMob.java
//   src/main/java/gloamingforest/entity/monster/SnowGuardian.java
//   src/main/java/gloamingforest/entity/monster/StableIceCore.java
//   src/main/java/gloamingforest/entity/monster/UnstableIceCore.java
//   src/main/java/gloamingforest/entity/monster/IceCrystal.java
//   src/main/java/gloamingforest/entity/projectile/IceSnowball.java
//   src/main/java/gloamingforest/block/AuroraBrickBlock.java (getDestroyProgress)
//   net/minecraft/world/entity/ai/goal/RangedAttackGoal.java (vanilla, the stable ice core's goal)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U28's ice mobs. Their vanilla goals (float, melee, stroll, look, hurt-by / nearest-player
// targets) are the BP's behaviours (assets/pack/bp/entities/aurora); what is upstream's own is native
// here: the slow fall, the snow particles, burning in hot biomes, the snow guardian's gear, the
// stable core's snowballs (RangedAttackGoal + IceSnowball), the unstable core's delayed explosion
// and block transformation, and the ice crystal's 30 second life.
#include "feature/aurora/Mobs.h"

#include <algorithm>
#include <bitset>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <set>
#include <random>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/deps/shared_types/legacy/EquipmentSlot.h"
#include "mc/deps/vanilla_components/OffsetsComponent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "core/jrand/JRand.h"
#include "feature/combat/Combat.h"
#include "feature/death/Death.h"
#include "feature/dimension/World.h"
#include "feature/ext/dimension/DimensionType.h"
#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/aurora/W0Internals.h"
#include "feature/fx/Fx.h"
#include "feature/loot/Loot.h"
#include "feature/projectile/Projectile.h"
#include "feature/spawn/Spawn.h"
#include "feature/tags/Tags.h"
#include "feature/worldgen/Worldgen.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::aurora {

namespace fe   = gf::feature::entity;
namespace mobs = gf::feature::entity::mobs;

gf::core::jrand::RandomSource& rng() {
    static gf::core::jrand::LegacyRandom r(static_cast<std::int64_t>(std::random_device{}()) << 20 ^ static_cast<std::int64_t>(std::random_device{}()));
    return r;
}

float rr() { return rng().nextFloat() - rng().nextFloat(); }

Vec3 feet(Actor const& a) { return a.getFeetPos(); }

bool mobGriefing() {
    auto level = ll::service::getLevel();
    if (!level) return false;
    GameRuleId griefing;
    griefing.mValue = static_cast<int>(GameRules::GameRulesIndex::MobGriefing);
    return level->getGameRules().getBool(griefing, true);
}

/// The Java biome id at the actor (upstream's biomes in the Gloaming Forest, Bedrock's names elsewhere).
std::string biomeIdAt(Actor& actor) {
    auto     p = feet(actor);
    BlockPos pos{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))};
    if (gf::world::isGloaming(actor.getDimension())) {
        auto id = gf::worldgen::biomeAt(pos);
        if (!id.empty()) return id;
    }
    auto const& name = actor.getDimensionBlockSource().getBiome(pos).mHash->getString();
    return name.find(':') == std::string::npos ? "minecraft:" + name : name;
}

/// #minecraft:snow_golem_melts (vanilla hot biomes, upstream's oak savannah and fire swamp).
bool meltsSnowIn(std::string const& id) {
    if (id == "gloamingforest:oak_savannah" || id == "gloamingforest:fire_swamp") return true;
    return gf::tags::has(gf::tags::Registry::Biome, "minecraft:snow_golem_melts", id);
}

bool meltsSnow(Actor& actor) { return meltsSnowIn(biomeIdAt(actor)); }

float eyeHeightOf(std::string const& type) {
    if (type == kStableIceCore || type == kUnstableIceCore) return 1.35f;
    if (type == kSnowQueen) return 2.125f;
    return 1.53f;
}

namespace {

constexpr char kIceSnowball[] = "gloamingforest:ice_snowball";

/// BaseIceMob#aiStep: slow fall, burning in hot biomes. The snow particles around the eyes (3 a tick,
/// the snow guardian 3 more) are client side as in upstream: a looping emitter of the client entity
/// (gloamingforest:aurora_snow_*, controller.animation.gloamingforest.ice_mob.snow).
void baseIceMobTick(Actor& self, std::string const& type) {
    if (!self.isAlive()) return;
    auto v = self.getPosDelta();
    if (!self.isOnGround() && v.y < 0.0f) fe::setMotion(self, Vec3{v.x, v.y * 0.6f, v.z});
    bool melts = meltsSnow(self);
    // the melting particles of upstream's snow particle loop: 3 spots a tick around the eyes, each a cloud
    // (1 in 4) and a falling water drop (1 in 2); not while dying (!isDeadOrDying, the unstable core's
    // 60 ticks included). Sent through fx (the vanilla particles a client draws): upstream spawns them on
    // the client, the server's draws give the same rate.
    bool dying = type == kUnstableIceCore && gf::entity::data(self, "aurora:dying").is_number_integer();
    if (melts && !dying) {
        auto p = feet(self);
        for (int i = 0; i < 3; i++) {
            Vec3 at{p.x + rr() * 0.3f, p.y + eyeHeightOf(type) + rr() * 0.5f, p.z + rr() * 0.3f};
            if (rng().nextInt(4) == 0) gf::fx::particle(self.getDimension(), "minecraft:cloud", at, Vec3{0, 0.1f, 0});
            if (rng().nextBoolean()) gf::fx::particle(self.getDimension(), "minecraft:falling_water", at);
        }
    }
    if (melts && self.mTickCount % 20 == 0) {
        // BURN!!!
        gf::combat::hurt(self, 1.0f, "minecraft:on_fire");
    }
}

// ---- SnowGuardian#populateDefaultEquipmentSlots ----------------------------------------------------

using Slot = SharedTypes::Legacy::EquipmentSlot;
constexpr Slot kGuardianSlots[] = {Slot::Mainhand, Slot::Torso, Slot::Head};

/// DifficultyInstance#getSpecialMultiplier of the region (no chunk inhabited time on Bedrock: 0).
float specialMultiplier(Actor&) {
    auto level = ll::service::getLevel();
    if (!level) return 0.0F;
    auto diff = level->getDifficulty();
    if (diff == SharedTypes::Legacy::Difficulty::Peaceful) return 0.0F;
    bool  hard = diff == SharedTypes::Legacy::Difficulty::Hard;
    // DifficultyInstance#calculateDifficulty(difficulty, dayTime, inhabitedTime = 0, moonBrightness)
    float f  = 0.75F;
    float f1 = std::clamp((static_cast<float>(level->getTime()) + -72000.0F) / 1440000.0F, 0.0F, 1.0F) * 0.25F; // the day time (level.getDayTime())
    f += f1;
    float f2      = 0.0F;
    // DimensionType.MOON_BRIGHTNESS_PER_PHASE[moonPhase(dayTime)]
    static constexpr float kMoon[] = {1.0F, 0.75F, 0.5F, 0.25F, 0.0F, 0.25F, 0.5F, 0.75F};
    float moon    = kMoon[(static_cast<long long>(level->getTime()) / 24000LL % 8 + 8) % 8];
    f2           += std::clamp(moon * 0.25F, 0.0F, f1);
    if (diff == SharedTypes::Legacy::Difficulty::Easy) f2 *= 0.5F;
    f += f2;
    float effective = static_cast<float>(static_cast<int>(diff)) * f;
    (void)hard;
    // getSpecialMultiplier: 0 below 2, 1 above 4
    if (effective < 2.0F) return 0.0F;
    return effective > 4.0F ? 1.0F : (effective - 2.0F) / 2.0F;
}

/// Mob#populateDefaultEquipmentEnchantments: the weapon (chance 0.25) and each armour piece (0.5) times
/// the special multiplier, enchanted by VanillaEnchantmentProviders.MOB_SPAWN_EQUIPMENT (cost 5 ..
/// 5 + multiplier x 17).
void enchantSpawnedEquipment(Mob& mob, float special) {
    auto level = ll::service::getLevel();
    if (!level) return;
    for (auto slot : kGuardianSlots) {
        float chance = slot == Slot::Mainhand ? 0.25F : 0.5F;
        ItemStack stack(mob.getItemSlot(slot));
        if (gf::game::items::isEmpty(stack) || !(rng().nextFloat() < chance * special)) continue;
        int maxCost = 5 + static_cast<int>(special * 17.0F);
        int cost    = 5 + rng().nextInt(maxCost - 5 + 1);
        EnchantUtils::randomlyEnchant(level->getIRandom(), stack, cost, 0, false);
        mob.setItemSlot(slot, stack);
    }
}

void syncEquipment(Mob& mob) {
    // MobEquipment / MobArmorEquipment packets to every client that sees the guardian
    mob.sendInventory(false);
    mob.sendArmor(std::bitset<5>{0b1111});
}

void equipSnowGuardian(Actor& self) {
    auto* mob = fe::asMob(self);
    if (!mob) return;
    int type = rng().nextInt(4);
    static char const* swords[]  = {"gloamingforest:ironwood_sword", "gloamingforest:steeleaf_sword", "gloamingforest:knightmetal_sword", "gloamingforest:knightmetal_sword"};
    static char const* chests[]  = {"gloamingforest:ironwood_chestplate", "gloamingforest:steeleaf_chestplate", "gloamingforest:knightmetal_chestplate", "gloamingforest:arctic_chestplate"};
    static char const* helmets[] = {"gloamingforest:ironwood_helmet", "gloamingforest:steeleaf_helmet", "gloamingforest:knightmetal_helmet", "gloamingforest:arctic_helmet"};
    if (auto s = gf::game::items::make(swords[type], 1, 0)) mob->setItemSlot(Slot::Mainhand, *s);
    if (auto s = gf::game::items::make(chests[type], 1, 0)) {
        // the engine's setItemSlot refuses an item without minecraft:wearable in the chest slot (the
        // upstream chestplates' wearable component is U7's): the armour container takes it directly
        if (!mob->setItemSlot(Slot::Torso, *s) || gf::game::items::isEmpty(mob->getItemSlot(Slot::Torso))) mob->setArmor(SharedTypes::Legacy::ArmorSlot::Torso, *s);
    }
    if (auto s = gf::game::items::make(helmets[type], 1, 0)) mob->setItemSlot(Slot::Head, *s);
    enchantSpawnedEquipment(*mob, specialMultiplier(self));
    syncEquipment(*mob);
}

/// Mob#dropCustomDeathLoot for the guardian's gear: drop chance 0.085 per slot (+ 0.01 per looting
/// level of the killer's weapon), only when a player hit it recently, damaged at random.
void dropSnowGuardianGear(Actor& dead, std::vector<ItemStack>& drops, gf::loot::LootContext const& ctx) {
    auto* mob = fe::asMob(dead);
    if (!mob) return;
    int looting = 0;
    if (ctx.attacker) {
        if (auto* killer = fe::asMob(*ctx.attacker))
            looting = EnchantUtils::getEnchantLevel(Enchant::Type::Looting, killer->getCarriedItem());
    }
    for (auto slot : kGuardianSlots) {
        ItemStack stack(mob->getItemSlot(slot));
        if (gf::game::items::isEmpty(stack)) continue;
        float chance = 0.085F + 0.01F * static_cast<float>(looting);
        if (ctx.player != nullptr && rng().nextFloat() < chance) {
            if (stack.isDamageableItem()) {
                auto* item      = stack.mItem.get();
                int   maxDamage = item ? item->getMaxDamage() : 0;
                if (maxDamage > 0) stack.setDamageValue(static_cast<short>(maxDamage - rng().nextInt(1 + rng().nextInt(std::max(maxDamage - 3, 1)))));
            }
            drops.push_back(stack);
        }
        // the engine never drops it again (dropEquipmentOnDeath)
        mob->setItemSlot(slot, gf::game::items::empty());
    }
}

/// Monster#checkMonsterSpawnRules: not on peaceful, dark enough unless a spawner places it.
bool checkMonsterSpawnRules(BlockSource& region, std::string const&, BlockPos const& pos, gf::spawn::SpawnReason reason, gf::core::jrand::RandomSource& random) {
    auto level = ll::service::getLevel();
    if (!level || level->getDifficulty() == SharedTypes::Legacy::Difficulty::Peaceful) return false;
    if (reason == gf::spawn::SpawnReason::Spawner || reason == gf::spawn::SpawnReason::TrialSpawner) return true;
    return gf::world::ext::isDarkEnoughToSpawn(region, pos, random);
}

// ---- StableIceCore: RangedAttackGoal(1.25, 20, 10) + performRangedAttack -------------------------

void performRangedAttack(Mob& self, Actor& target) {
    // [VanillaCopy] SnowGolem: aim at the target's eyes - 1.4, raised by a fifth of the distance
    auto   from = feet(self);
    double sy   = from.y + 1.35;
    auto   to   = feet(target);
    double d0   = target.getEyePos().y - 1.4;
    double d1   = to.x - from.x;
    double d2   = d0 - sy;
    double d3   = to.z - from.z;
    float  f    = static_cast<float>(std::sqrt(static_cast<float>(d1 * d1 + d3 * d3))) * 0.2F;
    gf::projectile::shoot(self, kIceSnowball, Vec3{static_cast<float>(d1), static_cast<float>(d2 + f), static_cast<float>(d3)}, 1.6F, 6.0F);
    gf::fx::sound(self.getDimension(), "entity.gloamingforest.ice.shoot", from, 1.0F, 1.0F / (rng().nextFloat() * 0.4F + 0.8F));
}

class RangedAttackGoal final : public gf::entity::Goal {
public:
    RangedAttackGoal(Mob& mob, double speed, int interval, float radius)
    : mUid(mob.getOrCreateUniqueID().rawID), mSpeed(speed), mInterval(interval), mRadius(radius), mRadiusSqr(radius * radius) {
        flags = Move | Look;
    }
    bool canUse() override {
        auto* mob = self();
        if (!mob) return false;
        auto* t = mobs::target(*mob);
        if (t && t->isAlive()) {
            mTarget = t->getOrCreateUniqueID().rawID;
            return true;
        }
        return false;
    }
    bool canContinueToUse() override {
        auto* mob = self();
        auto* t   = target();
        return canUse() || (mob && t && t->isAlive() && !mobs::navigationDone(*mob));
    }
    void stop() override {
        mTarget     = 0;
        mSeeTime    = 0;
        mAttackTime = -1;
    }
    bool requiresUpdateEveryTick() override { return true; }
    void tick() override {
        auto* mob = self();
        auto* t   = target();
        if (!mob || !t) return;
        double d0   = mobs::distanceSqr(*mob, *t);
        bool   flag = mobs::hasLineOfSight(*mob, *t);
        if (flag) ++mSeeTime;
        else mSeeTime = 0;
        if (d0 <= mRadiusSqr && mSeeTime >= 5) mobs::stopNavigation(*mob);
        else mobs::moveTo(*mob, feet(*t), mSpeed);
        mobs::lookAt(*mob, *t, 30.0F, 30.0F);
        if (--mAttackTime == 0) {
            if (!flag) return;
            float f  = static_cast<float>(std::sqrt(d0)) / mRadius;
            performRangedAttack(*mob, *t);
            mAttackTime = static_cast<int>(std::floor(f * static_cast<float>(mInterval - mInterval) + static_cast<float>(mInterval)));
        } else if (mAttackTime < 0) {
            mAttackTime = static_cast<int>(std::floor(static_cast<float>(mInterval))); // Mth.lerp(d / r, min, max) with min == max
        }
    }

private:
    Mob* self() const {
        auto* a = fe::actorById(mUid);
        return a ? fe::asMob(*a) : nullptr;
    }
    Actor* target() const { return mTarget ? fe::actorById(mTarget) : nullptr; }
    std::int64_t mUid;
    std::int64_t mTarget = 0;
    double       mSpeed;
    int          mInterval;
    float        mRadius, mRadiusSqr;
    int          mSeeTime    = 0;
    int          mAttackTime = -1;
};

// ---- UnstableIceCore#tickDeath / transformBlocks -------------------------------------------------------

/// DyeColor#getTextureDiffuseColor, in DyeColor order.
struct Dye {
    char const* name;
    unsigned    rgb;
};
constexpr Dye kDyes[] = {{"white", 0xF9FFFE},  {"orange", 0xF9801D},     {"magenta", 0xC74EBD}, {"light_blue", 0x3AB3DA}, {"yellow", 0xFED83D},
                         {"lime", 0x80C71F},   {"pink", 0xF38BAA},       {"gray", 0x474F52},    {"light_gray", 0x9D9D97}, {"cyan", 0x169C9C},
                         {"purple", 0x8932B8}, {"blue", 0x3C44AA},       {"brown", 0x835432},   {"green", 0x5E7C16},      {"red", 0xB02E26},
                         {"black", 0x1D1D21}};

char const* closestDye(unsigned blockColor) {
    int         red = static_cast<int>((blockColor >> 16) & 255), green = static_cast<int>((blockColor >> 8) & 255), blue = static_cast<int>(blockColor & 255);
    char const* best           = "white";
    int         bestDifference = 1024;
    for (auto const& d : kDyes) {
        int diff = std::abs(red - static_cast<int>((d.rgb >> 16) & 255)) + std::abs(green - static_cast<int>((d.rgb >> 8) & 255)) +
                   std::abs(blue - static_cast<int>(d.rgb & 255));
        if (diff < bestDifference) {
            best           = d.name;
            bestDifference = diff;
        }
    }
    return best;
}

} // namespace

/// Java's MapColor#col of vanilla blocks (net.minecraft.world.level.block.Blocks' mapColor), for the
/// blocks transformBlocks meets most: the terrain, the biome-tinted grass and leaves (fixed colours
/// in Java, biome colours on Bedrock), wood, wool, concrete and terracotta.
std::optional<unsigned> javaMapColor(std::string const& id) {
    static std::unordered_map<std::string, unsigned> const table = [] {
        std::unordered_map<std::string, unsigned> m{
            {"minecraft:grass_block", 0x7FB238}, {"minecraft:dirt", 0x976D4D},         {"minecraft:coarse_dirt", 0x976D4D}, {"minecraft:rooted_dirt", 0x976D4D},
            {"minecraft:podzol", 0x815631},      {"minecraft:mycelium", 0x7F3FB2},     {"minecraft:farmland", 0x976D4D},    {"minecraft:dirt_path", 0x976D4D},
            {"minecraft:stone", 0x707070},       {"minecraft:cobblestone", 0x707070},  {"minecraft:stone_bricks", 0x707070}, {"minecraft:mossy_cobblestone", 0x707070},
            {"minecraft:andesite", 0x707070},    {"minecraft:granite", 0x976D4D},      {"minecraft:diorite", 0xFFFCF5},     {"minecraft:gravel", 0x707070},
            {"minecraft:deepslate", 0x646464},   {"minecraft:cobbled_deepslate", 0x646464}, {"minecraft:tuff", 0x392923},  {"minecraft:calcite", 0xD1B1A1},
            {"minecraft:sand", 0xF7E9A3},        {"minecraft:sandstone", 0xF7E9A3},    {"minecraft:red_sand", 0xD87F33},    {"minecraft:clay", 0xA4A8B8},
            {"minecraft:snow", 0xFFFFFF},        {"minecraft:snow_layer", 0xFFFFFF},   {"minecraft:powder_snow", 0xFFFFFF}, {"minecraft:ice", 0xA0A0FF},
            {"minecraft:packed_ice", 0xA0A0FF},  {"minecraft:blue_ice", 0xA0A0FF},     {"minecraft:moss_block", 0x667F33},  {"minecraft:glass", 0x000000},
            {"minecraft:netherrack", 0x700200},  {"minecraft:obsidian", 0x191919},     {"minecraft:quartz_block", 0xFFFCF5}, {"minecraft:terracotta", 0xD87F33},
            {"minecraft:gold_block", 0xFAEE4D},  {"minecraft:diamond_block", 0x5CDBD5}, {"minecraft:lapis_block", 0x4A80FF}, {"minecraft:emerald_block", 0x00D93A},
            {"minecraft:iron_block", 0xA7A7A7},  {"minecraft:bookshelf", 0x8F7748},    {"minecraft:pumpkin", 0xD87F33},     {"minecraft:melon_block", 0x7FCC19},
            {"minecraft:hay_block", 0xE5E533},   {"minecraft:oak_planks", 0x8F7748},   {"minecraft:spruce_planks", 0x815631}, {"minecraft:birch_planks", 0xF7E9A3},
            {"minecraft:jungle_planks", 0x976D4D}, {"minecraft:acacia_planks", 0xD87F33}, {"minecraft:dark_oak_planks", 0x664C33}, {"minecraft:mangrove_planks", 0x993333},
            {"minecraft:cherry_planks", 0xD1B1A1}, {"minecraft:bamboo_planks", 0xE5E533}, {"minecraft:crimson_planks", 0x943F61}, {"minecraft:warped_planks", 0x3A8E8C},
            {"minecraft:cherry_leaves", 0xF27FA5},
        };
        // the leaves: MapColor.PLANT
        for (char const* leaves : {"oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "mangrove", "azalea", "azalea_leaves_flowered"})
            m[std::string(leaves).starts_with("azalea_") ? std::string("minecraft:") + leaves : std::string("minecraft:") + leaves + "_leaves"] = 0x007C00;
        // wool and concrete: DyeColor#getMapColor; terracotta: MapColor.TERRACOTTA_*
        struct DyeRow {
            char const* name;
            unsigned    map, terracotta;
        };
        for (auto d : {DyeRow{"white", 0xFFFFFF, 0xD1B1A1}, DyeRow{"orange", 0xD87F33, 0x9F5224}, DyeRow{"magenta", 0xB24CD8, 0x95576C}, DyeRow{"light_blue", 0x6699D8, 0x706C8A},
                       DyeRow{"yellow", 0xE5E533, 0xBA8524}, DyeRow{"lime", 0x7FCC19, 0x677535}, DyeRow{"pink", 0xF27FA5, 0xA04D4E}, DyeRow{"gray", 0x4C4C4C, 0x392923},
                       DyeRow{"light_gray", 0x999999, 0x876B62}, DyeRow{"cyan", 0x4C7F99, 0x575C5C}, DyeRow{"purple", 0x7F3FB2, 0x7A4958}, DyeRow{"blue", 0x334CB2, 0x4C3E5C},
                       DyeRow{"brown", 0x664C33, 0x4C3223}, DyeRow{"green", 0x667F33, 0x4C522A}, DyeRow{"red", 0x993333, 0x8E3C2E}, DyeRow{"black", 0x191919, 0x251610}}) {
            m[std::string("minecraft:") + d.name + "_wool"]       = d.map;
            m[std::string("minecraft:") + d.name + "_concrete"]   = d.map;
            m[std::string("minecraft:") + d.name + "_terracotta"] = d.terracotta;
        }
        return m;
    }();
    auto it = table.find(id);
    if (it == table.end()) return std::nullopt;
    return it->second;
}

namespace {

/// UnstableIceCore#isBlockNormalBounds: Block.isShapeFullBlock(state.getShape(level, pos)), the outline.
bool isBlockNormalBounds(BlockSource& region, BlockPos const& pos, Block const& state) {
    AABB        buffer{};
    AABB const& shape = state.getBlockType().getOutline(state, region, pos, buffer);
    auto        span  = [](float lo, float hi) { return hi - lo >= 0.999F; };
    return span(shape.min.x, shape.max.x) && span(shape.min.y, shape.max.y) && span(shape.min.z, shape.max.z);
}

void transformBlock(BlockSource& region, BlockPos const& pos) {
    Block const& state = region.getBlock(pos);
    if (state.isAir()) return;
    auto const& type = state.getBlockType();
    // getExplosionResistance() < 8 and breakable (destroy speed >= 0)
    if (type.getExplosionResistance() >= 8.0f * 5.0f || type.getDestroySpeed() < 0.0f) return; // Bedrock keeps Java resistance x 5
    // MapColor#col: Java's colour for the vanilla blocks of javaMapColor's table (Bedrock tints grass
    // and leaves by biome), else Bedrock's own map colour (Java's palette for most vanilla blocks)
    auto     id    = gf::game::blockId(state);
    unsigned color = 0;
    if (auto java = javaMapColor(id)) color = *java;
    else {
        auto c = type.getMapColor(region, pos, state);
        color  = (static_cast<unsigned>(c.r * 255.0f + 0.5f) << 16) | (static_cast<unsigned>(c.g * 255.0f + 0.5f) << 8) | static_cast<unsigned>(c.b * 255.0f + 0.5f);
    }
    bool solid = state._isSolid();
    // shouldTransformGlass: a full-cube shape and (not solid, leaves, ice or the aurora block)
    bool glassLike = isBlockNormalBounds(region, pos, state) &&
                     (!solid || gf::tags::has(gf::tags::Registry::Block, "minecraft:leaves", id) || id == "minecraft:ice" || id == "gloamingforest:aurora_block");
    if (glassLike) {
        if (auto const* b = gf::game::blockWithStates(std::string("minecraft:") + closestDye(color) + "_stained_glass", nlohmann::json::object()))
            gf::game::setBlock(region, pos, *b);
    } else if (solid && isBlockNormalBounds(region, pos, state)) {
        // shouldTransformClay: a redstone conductor (a solid full cube on Bedrock): terracotta
        if (auto const* b = gf::game::blockWithStates(std::string("minecraft:") + closestDye(color) + "_terracotta", nlohmann::json::object()))
            gf::game::setBlock(region, pos, *b);
    }
}
} // namespace

void transformBlocksAt(BlockSource& reg, BlockPos const& at) {
    int range = 4;
    for (int dx = -range; dx <= range; dx++)
        for (int dy = -range; dy <= range; dy++)
            for (int dz = -range; dz <= range; dz++) {
                double distance  = std::sqrt(static_cast<double>(dx * dx + dy * dy + dz * dz));
                float  randRange = static_cast<float>(range) + rr() * 2.0F;
                if (distance < randRange) transformBlock(reg, BlockPos{at.x + dx, at.y + dy, at.z + dz});
            }
}

namespace {

void transformBlocks(Actor& self) {
    auto p = feet(self);
    transformBlocksAt(self.getDimensionBlockSource(), BlockPos{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))});
}

void unstableCoreTick(Actor& self) {
    auto& d = gf::entity::data(self, "aurora:dying");
    if (!d.is_number_integer()) return;
    int t = d.get<int>() + 1;
    d     = t;
    // the body lies still while it waits (the BP's behaviours would move and attack)
    if (auto* mob = fe::asMob(self)) {
        mobs::stopNavigation(*mob);
        mob->setTarget(nullptr);
        auto v = self.getPosDelta();
        fe::setMotion(self, Vec3{0.0f, std::min(v.y, 0.0f), 0.0f});
    }
    // deathTime, every tick (the client renderer flashes on (t / 2) % 2 and swells with sin(t x 100))
    gf::entity::setProperty(self, "gloamingforest:dying", std::min(t, 60));
    if (t == 60) {
        auto level = ll::service::getLevel();
        bool grief = mobGriefing();
        if (level) level->explode(self.getDimensionBlockSource(), &self, feet(self), 1.0f, false, grief, 8.0f * 5.0f, false);
        if (grief) transformBlocks(self);
        gf::entity::data(self, "aurora:explode") = true;
        // the death was already the killer's (upstream's tickDeath only delays the removal): finish it with
        // the lethal hit's type and entities, so the kill credit, looting and XP stay theirs
        auto const& lethal   = gf::entity::data(self, "aurora:lethal");
        std::string type     = lethal.is_object() ? lethal.value("type", std::string("minecraft:generic")) : std::string("minecraft:generic");
        Actor*      attacker = lethal.is_object() ? fe::actorById(lethal.value("attacker", std::int64_t{0})) : nullptr;
        Actor*      direct   = lethal.is_object() ? fe::actorById(lethal.value("direct", std::int64_t{0})) : nullptr;
        if (direct && !direct->isAlive()) direct = attacker;
        // a hit of its remaining health (getHealth() truncates: + 1 covers a fractional rest), so
        // onDealt listeners (stats, damage dealt) see no inflated amount
        float health = static_cast<float>(std::max(self.getHealth(), 0) + 1);
        if (!gf::combat::hurt(self, health, type, attacker, direct) && self.isAlive())
            gf::combat::hurt(self, health, "minecraft:generic", attacker, attacker);
        if (self.isAlive() && self.getHealth() > 0) self.kill();
    }
}

/// IceCrystal's crystalAge / maxCrystalAge (fields, not saved).
struct CrystalAge {
    int age = 0, max = 0;
};
std::map<std::int64_t, CrystalAge>& crystalAges() {
    static std::map<std::int64_t, CrystalAge> m;
    return m;
}

} // namespace

bool nativeGoalsPresent(Actor& actor) {
    auto const* s = fe::findState(actor.getOrCreateUniqueID().rawID);
    return s && s->goals && s->goals->size() > 0;
}

void dropNativeStateForTest(Actor& actor) {
    // what F8's sweep leaves for an actor that comes back: a fresh state (no goals, no move
    // control, onSpawn due on its next tick)
    if (auto* s = fe::findState(actor.getOrCreateUniqueID().rawID)) {
        s->goals.reset();
        s->targets.reset();
        s->moveKind.reset();
        s->moveFn  = {};
        s->spawned = false;
    }
}

void applyJavaEyeHeight(Actor& actor) {
    auto const& id = actor.getTypeName();
    if (id != kSnowQueen && id != kStableIceCore && id != kUnstableIceCore && id != kSnowGuardian && id != kIceCrystal) return;
    float const eye = eyeHeightOf(id);
    if (auto oc = actor.getEntityContext().tryGetComponent<OffsetsComponent>()) {
        Vec3 e = oc->mEyeOffset;
        if (e.y != eye) oc->mEyeOffset = Vec3{e.x, eye, e.z};
    }
}

void installMobs() {
    for (char const* id : {kSnowGuardian, kStableIceCore, kUnstableIceCore, kIceCrystal}) {
        std::string type = id;
        gf::entity::Behaviour b;
        b.tick = [type](Actor& self) {
            applyJavaEyeHeight(self);
            // the native goals, whenever F8's state of the actor has none (spawned, loaded, or back
            // after F8 swept the state of its unloaded chunk)
            if (type == kStableIceCore && !nativeGoalsPresent(self))
                if (auto* mob = fe::asMob(self)) gf::entity::addGoal(*mob, 1, std::make_shared<RangedAttackGoal>(*mob, 1.25, 20, 10.0F));
            baseIceMobTick(self, type);
            if (type == kUnstableIceCore) unstableCoreTick(self);
            if (type == kIceCrystal) {
                // IceCrystal#aiStep: a summoned crystal lasts 600 ticks, then vanishes (no loot); the
                // age fields are not saved (a reloaded crystal stays), so they live in this process only
                auto it = crystalAges().find(self.getOrCreateUniqueID().rawID);
                if (it != crystalAges().end() && it->second.max > 0 && ++it->second.age >= it->second.max) {
                    fe::removeLater(it->first);
                    crystalAges().erase(it);
                }
            }
        };
        if (type == kSnowGuardian) {
            b.onSpawn = [](Actor& self) {
                auto& done = gf::entity::data(self, "aurora:equipped");
                if (done.is_boolean()) return;
                done = true;
                equipSnowGuardian(self);
            };
            // players who come into view later: the gear again once a second for the first minute,
            // then every 10 seconds (no tracking event in the contracts)
            b.tick = [inner = b.tick](Actor& self) {
                inner(self);
                int t = self.mTickCount;
                if ((t < 1200 && t % 20 == 5) || t % 200 == 5)
                    if (auto* mob = fe::asMob(self)) syncEquipment(*mob);
            };
        }

        if (type == kIceCrystal) {
            // a crystal read back from the world is a new Java entity: maxCrystalAge 0, it stays
            b.load = [](Actor& self, nlohmann::json const&) { crystalAges().erase(self.getOrCreateUniqueID().rawID); };
            b.die  = [](Actor& self, std::string const&, Actor*) { crystalAges().erase(self.getOrCreateUniqueID().rawID); };
        }
        if (type == kUnstableIceCore) b.hurt = [](Actor& self, float&, std::string const&, Actor*) {
            // the body is invulnerable while it waits for the explosion (then its final hit passes)
            return !gf::entity::data(self, "aurora:dying").is_number_integer() || gf::entity::data(self, "aurora:explode").is_boolean();
        };
        gf::entity::registerType(id, b);
        // RegistrationEvents: ON_GROUND, MOTION_BLOCKING_NO_LEAVES, Monster::checkMonsterSpawnRules
        gf::spawn::registerPlacement(id, gf::spawn::PlacementRule::OnGround, checkMonsterSpawnRules);
    }
    gf::spawn::registerPlacement(kSnowQueen, gf::spawn::PlacementRule::OnGround, checkMonsterSpawnRules);
    gf::loot::onEntityDrops([](Actor& dead, std::vector<ItemStack>& drops, gf::loot::LootContext const& ctx) {
        if (dead.getTypeName() == kSnowGuardian) dropSnowGuardianGear(dead, drops, ctx);
    });
    gf::game::db::onWorldClosed([] {
        crystalAges().clear();

    });

    // UnstableIceCore#tickDeath: the corpse stays 60 ticks, then explodes
    gf::death::onLethal(0, [](Actor& victim, gf::combat::DamageInfo const& info) {
        if (victim.getTypeName() != kUnstableIceCore) return false;
        if (gf::entity::data(victim, "aurora:explode").is_boolean()) return false; // the explosion's own kill: die now
        auto& d = gf::entity::data(victim, "aurora:dying");
        if (d.is_number_integer()) return true;
        d = 0;
        // the lethal hit, to finish the death with it 60 ticks later (kill credit, looting, XP)
        gf::entity::data(victim, "aurora:lethal") = nlohmann::json{{"type", info.type},
                                                                   {"attacker", info.attacker ? info.attacker->getOrCreateUniqueID().rawID : std::int64_t{0}},
                                                                   {"direct", info.direct ? info.direct->getOrCreateUniqueID().rawID : std::int64_t{0}}};
        // LivingEntity#hurt plays the death sound at the lethal hit (the RP gives the core no engine
        // death sound, so the final removal is silent)
        gf::fx::sound(victim.getDimension(), "entity.gloamingforest.ice.death", feet(victim));
        if (auto* mob = fe::asMob(victim)) {
            mobs::stopNavigation(*mob);
            mob->setTarget(nullptr);
        }
        gf::entity::setProperty(victim, "gloamingforest:dying", 1);
        gf::log().info("aurora: unstable ice core dying at {:.1f} {:.1f} {:.1f}", feet(victim).x, feet(victim).y, feet(victim).z);
        return true;
    });

    // IceSnowball (GloamingThrowable): gravity 0.006, a snowball trail, 2 snowball_fight damage, gone on any hit
    gf::projectile::Spec snowball;
    snowball.gravity     = 0.006f;
    snowball.drag        = 0.99f;
    snowball.tick        = [](Actor& p) { gf::projectile::ext::makeTrail(p, "minecraft:item_snowball", 2); };
    snowball.onHitEntity = [](Actor& p, Actor& target) {
        if (target.hasCategory(ActorCategory::Mob)) gf::combat::hurt(target, 2.0f, "gloamingforest:snowball_fight", gf::projectile::ext::owner(p), &p);
        gf::fx::particle(p.getDimension(), "minecraft:item_snowball", feet(p), Vec3{}, 8);
        return true;
    };
    snowball.onHitBlock = [](Actor& p, BlockPos const&, int) {
        gf::fx::particle(p.getDimension(), "minecraft:item_snowball", feet(p), Vec3{}, 8);
        return true;
    };
    gf::projectile::registerType(kIceSnowball, snowball);

}


void markSummonedCrystal(Actor& crystal) { crystalAges()[crystal.getOrCreateUniqueID().rawID].max = 600; }

void setCrystalAgeForTest(Actor& crystal, int age) { crystalAges()[crystal.getOrCreateUniqueID().rawID].age = age; }

int crystalMaxAgeForTest(Actor& crystal) {
    auto it = crystalAges().find(crystal.getOrCreateUniqueID().rawID);
    return it == crystalAges().end() ? 0 : it->second.max;
}

} // namespace gf::feature::aurora
