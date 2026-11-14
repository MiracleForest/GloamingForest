// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/AbstractTrophyBlock.java
//   src/main/java/gloamingforest/block/TrophyBlock.java
//   src/main/java/gloamingforest/block/TrophyWallBlock.java
//   src/main/java/gloamingforest/item/TrophyItem.java
//   src/main/java/gloamingforest/dispenser/GloamingDispenserBehaviors.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U5 (building): the nine boss trophies (floor + wall block, one item each).
// - Placement (TrophyItem = StandingAndWallBlockItem, attachment DOWN): the item places the floor
//   trophy; right after, the looking directions (the clicked face's opposite first) choose the
//   standing trophy (rotation = floor(yaw * 16 / 360 + 0.5) & 15) or the wall trophy
//   (TrophyWallBlock#getStateForPlacement: the first horizontal looking direction whose block is
//   not replaceable; facing = its opposite).
// - Right-click (useWithoutItem): the variant's sound (AbstractTrophyBlock#playSound, volume,
//   pitch + nextFloat * 0.1) and particles (createParticle, sent to the players within 32).
// - Redstone (neighborChanged): `powered` follows Level#hasNeighborSignal, a rising edge plays the
//   sound. Custom blocks get no neighbour updates: the trophies are watched (redstone::watch) once
//   placed or touched by a neighbour change.
// - Dispensers equip trophies on the head of a living entity in front (ArmorItem.dispenseArmor);
//   otherwise they fail (OptionalDispenseItemBehavior) and keep the item.
// The variant table (sound, volume, pitch, particle, pedestal comparator value) is F8's
// gf::boss::trophyVariant (R20), so U25's / U30's trophy pedestal reads it without including U5.

#include "feature/building/Building.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "feature/boss/Boss.h"
#include "feature/common/Interactions.h"
#include "feature/common/Redstone.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/families/BlockEvents.h"
#include "feature/fx/Fx.h"
#include "feature/item/Item.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::building {

namespace ia = interactions;
namespace rs = redstone;

namespace {

std::vector<ia::HandlerId>& handlers() {
    static std::vector<ia::HandlerId> ids;
    return ids;
}

std::map<core::GlobalPos, rs::WatchId>& watches() {
    static std::map<core::GlobalPos, rs::WatchId> w;
    return w;
}

/// The watched trophies are kept in the world (gf:building:trophy_watches), so a trophy follows a
/// signal after a reload too, before anything next to it changes (the redstone contract forgets
/// its watches with the world).
constexpr char kWatchKey[] = "gf:building:trophy_watches";
bool gWatchesDirty = false;

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
float nextFloat() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng()); }
double nextDouble() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng()); }
double nextGaussian() { return std::normal_distribution<double>(0.0, 1.0)(rng()); }

Vec3 v(double x, double y, double z) { return Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)}; }

} // namespace

std::vector<std::string> const& trophyBosses() {
    static std::vector<std::string> const b{"naga", "lich", "hydra", "ur_ghast", "knight_phantom", "snow_queen", "minoshroom", "alpha_yeti", "quest_ram"};
    return b;
}

std::optional<std::string> trophyBoss(std::string const& blockId) {
    for (auto const& b : trophyBosses()) {
        if (blockId == std::string(kNs) + b + "_trophy" || blockId == std::string(kNs) + b + "_wall_trophy") return b;
    }
    return std::nullopt;
}

bool isWallTrophy(std::string const& blockId) { return blockId.ends_with("_wall_trophy") && trophyBoss(blockId).has_value(); }

int trophyRotation(float yaw) {
    // TrophyBlock#getStateForPlacement: Mth.floor(rotation * 16 / 360 + 0.5) & 15
    return static_cast<int>(std::floor(yaw * 16.0 / 360.0 + 0.5)) & 15;
}

std::optional<TrophySound> trophySound(std::string const& blockId, float roll) {
    auto variant = gf::boss::trophyVariant(blockId);
    if (!variant || variant->sound.empty()) return std::nullopt;
    TrophySound s{variant->sound, variant->volume, variant->pitch};
    // ALPHA_YETI: level.getRandom().nextInt(50) == 0 ? ALPHA_YETI_ROAR : ALPHA_YETI_GROWL
    if (auto rare = gf::boss::ext::trophyRareSound(blockId); rare && rare->oneIn > 0 && roll < 1.0f / static_cast<float>(rare->oneIn)) s.event = rare->sound;
    return s;
}

namespace {
unsigned long long& trophySoundCounter() {
    static unsigned long long n = 0;
    return n;
}
} // namespace

unsigned long long trophySoundsPlayed() { return trophySoundCounter(); }

void playTrophySound(BlockSource& region, BlockPos const& pos, std::string const& blockId) {
    auto s = trophySound(blockId, nextFloat());
    if (!s) return;
    ++trophySoundCounter();
    // level.playSound(null, pos, sound, BLOCKS, volume, nextFloat() * 0.1 + pitch): at the block centre
    fx::sound(region.getDimension(), s->event, v(pos.x + 0.5, pos.y + 0.5, pos.z + 0.5), s->volume, nextFloat() * 0.1f + s->pitch);
    log().info("building: trophy {} sounds {} at {} {} {}", blockId, s->event, pos.x, pos.y, pos.z);
}

void createTrophyParticles(BlockSource& region, BlockPos const& pos, std::string const& blockId) {
    auto boss = trophyBoss(blockId);
    auto p    = gf::boss::ext::trophyParticle(blockId, &region, &pos);
    if (!boss || !p || p->particle.empty()) return;
    auto&  dim = region.getDimension();
    double x = pos.x, y = pos.y, z = pos.z;
    for (int i = 0; i < p->count; ++i) {
        Vec3 at{}, vel{};
        auto params = p->params;
        if (*boss == "naga") {
            at  = v(x + nextFloat() * 0.5 * 2.0, y + 0.25, z + nextFloat() * 0.5 * 2.0);
            vel = v(nextGaussian() * 0.02, nextGaussian() * 0.02, nextGaussian() * 0.02);
        } else if (*boss == "lich") {
            at = v(x + nextFloat() * 0.5 * 2.0 + nextGaussian() * 0.02, y + 0.5 + nextFloat() * 0.25 + nextGaussian() * 0.02, z + nextFloat() * 0.5 * 2.0 + nextGaussian() * 0.02);
        } else if (*boss == "minoshroom") {
            at = v(x + nextFloat() * 10.0 - 5.0, y + 0.1 + nextFloat() * 0.3, z + nextFloat() * 10.0 - 5.0);
        } else if (*boss == "knight_phantom") {
            at = v(x + 0.5 + (nextFloat() - 0.5), y + nextFloat() + 0.5 + 0.25 * nextGaussian(), z + 0.5 + (nextFloat() - 0.5));
        } else if (*boss == "ur_ghast" || *boss == "alpha_yeti") {
            at = v(x + nextDouble(), y + nextDouble() * 0.5 + 0.5, z + nextDouble());
        } else if (*boss == "snow_queen") {
            at = v(x - 1 + nextDouble() * 3.25, y + 5 + nextGaussian(), z - 1 + nextDouble() * 3.25);
        } else if (*boss == "quest_ram") {
            // ColorParticleOption.create(MAGIC_EFFECT, nextFloat(), nextFloat(), nextFloat())
            params["color"] = {nextFloat(), nextFloat(), nextFloat()};
            at              = v(x + 0.5 + (nextDouble() - 0.5), y + (nextDouble() - 0.5), z + 0.5 + (nextDouble() - 0.5));
            vel             = v(nextGaussian(), nextGaussian(), nextGaussian());
        } else {
            continue;
        }
        fx::particle(dim, p->particle, at, vel, 1, params);
    }
}

namespace {

/// AbstractTrophyBlock#neighborChanged: `powered` follows the neighbour signal; a rising edge sounds.
void updatePowered(BlockSource& region, BlockPos const& pos, bool power) {
    auto const* b = blockAt(region, pos);
    if (!b) return;
    auto id = idOf(*b);
    if (!trophyBoss(id)) return;
    bool was = boolState(*b, std::string(kNs) + "powered");
    if (power == was) return;
    if (power) playTrophySound(region, pos, id);
    setStates(region, pos, {{std::string(kNs) + "powered", power}});
}

void watchAt(core::GlobalPos const& gp) {
    if (watches().contains(gp)) return;
    gWatchesDirty = true;
    watches()[gp] = rs::watch(gp, [](core::GlobalPos const& at, int power) {
        auto* r = game::regionOf(at.dim);
        if (!r) return;
        BlockPos bp{at.x, at.y, at.z};
        auto const* b = blockAt(*r, bp);
        if (!b || !trophyBoss(idOf(*b))) {
            auto it = watches().find(at);
            if (it != watches().end()) {
                rs::unwatch(it->second);
                watches().erase(it);
                gWatchesDirty = true;
            }
            return;
        }
        updatePowered(*r, bp, power > 0);
    });
}

void watch(BlockSource& region, BlockPos const& pos) { watchAt(game::globalPos(region, pos)); }

void unwatch(BlockSource& region, BlockPos const& pos) {
    auto it = watches().find(game::globalPos(region, pos));
    if (it == watches().end()) return;
    rs::unwatch(it->second);
    watches().erase(it);
    gWatchesDirty = true;
}

void saveWatches() {
    if (!gWatchesDirty) return;
    Json list = Json::array();
    for (auto const& [p, id] : watches()) list.push_back({p.dim, p.x, p.y, p.z});
    game::db::put(kWatchKey, list);
    gWatchesDirty = false;
}

void loadWatches() {
    auto list = game::db::get(kWatchKey);
    if (!list || !list->is_array()) return;
    for (auto const& e : *list) {
        if (!e.is_array() || e.size() != 4) continue;
        watchAt(core::GlobalPos{e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()});
    }
    gWatchesDirty = false;
}

/// StandingAndWallBlockItem#getPlacementState with TrophyBlock / TrophyWallBlock#getStateForPlacement.
void placeTrophy(ia::PlaceContext const& c, std::string const& boss) {
    auto* region = game::regionOf(c.pos.dim);
    if (!region) return;
    BlockPos pos = game::blockPos(c.pos);
    // BlockPlaceContext#getNearestLookingDirections: the clicked face's opposite first (unless the
    // clicked block itself was replaced)
    auto dirs = nearestLooking(c.player);
    if (!(c.clicked == c.pos) && c.face >= 0 && c.face < 6) {
        int want = opposite(c.face), i = 0;
        while (i < 6 && dirs[i] != want) ++i;
        for (int j = i; j > 0 && j < 6; --j) dirs[j] = dirs[j - 1];
        if (i < 6) dirs[0] = want;
    }
    // TrophyWallBlock#getStateForPlacement
    int wallFacing = -1;
    for (int d : dirs) {
        if (!horizontal(d)) continue;
        if (!replaceable(*region, relative(pos, d))) {
            wallFacing = opposite(d);
            break;
        }
    }
    for (int d : dirs) {
        if (d == Up) continue; // attachmentDirection.getOpposite()
        if (d == Down) {
            setStates(*region, pos, {{std::string(kNs) + "rotation", trophyRotation(c.player.getRotation().y)}});
            break;
        }
        if (wallFacing >= 0) {
            setBlock(*region, pos, std::string(kNs) + boss + "_wall_trophy", {{std::string(kNs) + "facing", dirName(wallFacing)}});
            break;
        }
    }
    watch(*region, pos);
}

/// ArmorItem.dispenseArmor: the first living entity in the block in front with a free head slot.
bool dispenseOnHead(BlockSource& region, BlockPos const& dispenser, int facing, ItemStack& stack) {
    BlockPos front = relative(dispenser, facing);
    AABB     box{v(front.x, front.y, front.z), v(front.x + 1.0, front.y + 1.0, front.z + 1.0)};
    std::vector<Actor*> candidates = region.getEntities(box, 0.0f);
    auto dim = region.getDimensionId();
    region.getLevel().forEachPlayer([&](Player& p) {
        if (p.getDimensionId() == dim && p.getAABB().intersects(box) && std::find(candidates.begin(), candidates.end(), &p) == candidates.end())
            candidates.push_back(&p);
        return true;
    });
    for (Actor* a : candidates) {
        if (!a || !a->isAlive() || a->isSpectator()) continue;
        if (!a->hasCategory(ActorCategory::Mob) && !a->isPlayer()) continue; // LivingEntity
        if (!a->getArmor(SharedTypes::Legacy::ArmorSlot::Head).isNull()) continue;
        auto one = game::items::withCount(stack, 1);
        // MobCanWearArmorEntitySelector: a Mob only when it can pick up loot (Mob#canPickUpLoot);
        // players and armour stands always
        bool wearer = a->isPlayer() || a->getTypeName() == "minecraft:armor_stand" || a->mCanPickupItems;
        if (!wearer) continue;
        a->setArmor(SharedTypes::Legacy::ArmorSlot::Head, one);
        stack.remove(1);
        if (stack.mCount <= 0) stack = ItemStack::EMPTY_ITEM();
        if (a->isPlayer()) static_cast<Player*>(a)->sendInventory(false);
        log().info("building: dispenser put {} on {}", game::items::idOf(one), a->getTypeName());
        return true;
    }
    return false;
}

} // namespace

void installTrophies() {
    // registered once per process: the lifecycle listeners and block events have no removal, so a
    // re-install (uninstall, install) must not add them again; the interaction handlers (removed in
    // uninstall) are added on every install
    static bool once = false;
    if (!once) {
        once = true;
        // the redstone contract drops its watches with the world: forget them too, and take them up
        // again from the world's list when it is loaded
        game::db::onWorldSave([] { saveWatches(); });
        game::db::onWorldClosed([] {
            watches().clear();
            gWatchesDirty = false;
        });
        game::db::onWorldReady([] { loadWatches(); });
        for (auto const& boss : trophyBosses()) {
            std::string floor = std::string(kNs) + boss + "_trophy";
            std::string wall  = std::string(kNs) + boss + "_wall_trophy";
            for (auto const& id : {floor, wall}) {
                blockevents::onNeighborChanged(id, [](BlockSource& region, BlockPos const& pos, BlockPos const&) {
                    updatePowered(region, pos, hasNeighborSignal(region, pos));
                    watch(region, pos);
                });
                blockevents::onPlaced(id, [](BlockSource& region, BlockPos const& pos, Block const&, Actor*) { watch(region, pos); });
                blockevents::onRemoved(id, [](BlockSource& region, BlockPos const& pos, Block const&) { unwatch(region, pos); });
            }
            item::registerDispense(floor, dispenseOnHead);
        }
    }
    if (game::db::available()) loadWatches(); // BDS: the world was ready before the mod was enabled
    for (auto const& boss : trophyBosses()) {
        std::string floor = std::string(kNs) + boss + "_trophy";
        std::string wall  = std::string(kNs) + boss + "_wall_trophy";
        handlers().push_back(ia::onPlace({.block = floor}, [boss](ia::PlaceContext const& c) { placeTrophy(c, boss); }));
        for (auto const& id : {floor, wall}) {
            // AbstractTrophyBlock#useWithoutItem: any hand, the sound and the particles
            handlers().push_back(ia::onUseOn({.block = id}, [](ia::UseOnContext const& c) {
                // UseItemOn: a sneaking player with an item in either hand skips useWithoutItem
                // (Player#isSecondaryUseActive && !(main empty && off-hand empty))
                if (c.sneaking && (!c.player.getCarriedItem().isNull() || !c.player.getOffhandSlot().isNull())) return ia::Result::Pass;
                if (!c.firstEvent) return ia::Result::Consume;
                auto* region = game::regionOf(c.pos.dim);
                if (!region) return ia::Result::Pass;
                BlockPos pos = game::blockPos(c.pos);
                playTrophySound(*region, pos, c.blockId);
                createTrophyParticles(*region, pos, c.blockId);
                return ia::Result::Consume;
            }));
        }
    }
}

void uninstallTrophies() {
    for (auto id : handlers()) ia::remove(id);
    handlers().clear();
    for (auto const& [pos, id] : watches()) rs::unwatch(id);
    watches().clear();
}

} // namespace gf::feature::building
