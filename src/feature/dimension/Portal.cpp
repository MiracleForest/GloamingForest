// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/TFPortalBlock.java
//   src/main/java/twilightforest/events/ProgressionEvents.java (portal creation check)
//   src/main/java/twilightforest/config/TFConfig.java (getPortalLockingAdvancement)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Lighting a Twilight portal: a player's portal activator item (a diamond) thrown into a water
// pool whose rim is dirt topped with plants; the pool turns into portal blocks under a lightning
// bolt. Portal blocks fall back to water when their floor or rim breaks.

#include <random>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/SetTitlePacket.h"
#include "mc/server/commands/CommandUtils.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/global/LightningBolt.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "feature/dimension/DimensionConfig.h"
#include "feature/dimension/Internal.h"
#include "feature/dimension/Teleporter.h"
#include "feature/dimension/World.h"
#include "feature/families/BlockEvents.h"
#include "feature/fx/Fx.h"
#include "feature/landmark/Landmark.h"
#include "feature/progression/Progression.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::dimension {

namespace {

constexpr int kMinPortalSize = 4;

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

/// ItemEntity#getOwner: Bedrock keeps no thrower on dropped items, so the player behind every item
/// spawn is remembered here (item unique id -> player unique id).
std::unordered_map<std::int64_t, std::int64_t>& throwers() {
    static std::unordered_map<std::int64_t, std::int64_t> m;
    return m;
}

/// TFPortalBlock.playersNotified (unique ids, for the session).
std::unordered_set<std::int64_t>& playersNotified() {
    static std::unordered_set<std::int64_t> s;
    return s;
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    TwilightItemThrowerHook,
    ll::memory::HookPriority::Normal,
    Spawner,
    &Spawner::$spawnItem,
    ItemActor*,
    BlockSource&     region,
    ItemStack const& inst,
    Actor*           spawner,
    Vec3 const&      pos,
    int              throwTime
) {
    auto* item = origin(region, inst, spawner, pos, throwTime);
    if (item && spawner && spawner->isPlayer()) {
        auto& m = throwers();
        if (m.size() > 4096) {
            // forget the items that are gone
            auto level = ll::service::getLevel();
            for (auto it = m.begin(); it != m.end();) {
                if (!level || !level->fetchEntity(ActorUniqueID{it->first}, false)) it = m.erase(it);
                else ++it;
            }
        }
        m[idOf(*item)] = idOf(*spawner);
    }
    return item;
}

std::int64_t itemOwner(ItemActor const& item) {
    auto owner = item.getOwnerId().rawID;
    if (owner != -1 && owner != 0) return owner;
    auto it = throwers().find(idOf(item));
    return it == throwers().end() ? -1 : it->second;
}

void installItemOwners() { TwilightItemThrowerHook::hook(); }
void uninstallItemOwners() {
    TwilightItemThrowerHook::unhook();
    throwers().clear();
}

void actionBar(Player& player, std::string const& text) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::SetTitle);
    if (!packet) return;
    auto& title      = static_cast<SetTitlePacket&>(*packet);
    title.mType      = SetTitlePacketPayload::TitleType::Actionbar;
    title.mTitleText = text;
    packet->sendTo(player);
}

std::optional<std::string> portalLockingAdvancement(Player const&) {
    auto const& id = gf::config::get<DimensionConfig>().portalUnlockedByAdvancement;
    if (id.empty()) return std::nullopt;
    return id;
}

bool unworthy(Player& player) {
    if (player.isCreative() || player.isSpectator()) return false;
    auto requirement = portalLockingAdvancement(player);
    if (!requirement || progression::has(player, *requirement)) return false;
    actionBar(player, trFor(player, "misc.twilightforest.portal_unworthy", "The Portal pool is unresponsive. Perhaps something was neglected?"));
    auto id = idOf(player);
    if (!playersNotified().contains(id)) {
        // MissingAdvancementToastPacket with the advancement's title and icon
        progression::showMissing(player, *requirement);
        playersNotified().insert(id);
    }
    return true;
}

void resetNotified() { playersNotified().clear(); }

// ---- TFPortalBlock ------------------------------------------------------------------------------------------------------

void causeLightning(BlockSource& region, BlockPos const& pos, bool destructive) {
    Vec3          at{pos.x + 0.5f, static_cast<float>(pos.y), pos.z + 0.5f};
    ActorUniqueID id{};
    auto*         bolt = CommandUtils::spawnEntityAt(region, at, ActorDefinitionIdentifier("minecraft:lightning_bolt"), id, nullptr);
    // TF: bolt.setVisualOnly(destructive), then the destructive kind strikes the entities around itself
    if (bolt && destructive) static_cast<LightningBolt*>(bolt)->mCanHurt = false;
    if (!destructive) return;
    double const range = 3.0;
    AABB         box{
        Vec3{static_cast<float>(pos.x - range), static_cast<float>(pos.y - range), static_cast<float>(pos.z - range)},
        Vec3{static_cast<float>(pos.x + 1 + range), static_cast<float>(pos.y + 1 + range), static_cast<float>(pos.z + 1 + range)}
    };
    std::vector<std::int64_t> victims;
    for (auto* victim : region.getEntities(box, 0.0f)) {
        if (!victim || victim == bolt || victim->getEntityTypeId() == ActorType::LightningBolt) continue;
        victims.push_back(idOf(*victim));
    }
    auto level = ll::service::getLevel();
    if (!level) return;
    for (auto id2 : victims) {
        // Entity#thunderHit: on fire for 8 s and 5 lightning damage (plus the entity's own reaction)
        auto* victim = level->fetchEntity(ActorUniqueID{id2}, false);
        if (!victim) continue;
        victim->onLightningHit();
        victim->setOnFire(8);
        victim->hurtByCause(5.0f, SharedTypes::Legacy::ActorDamageCause::Lightning);
    }
}

bool tryToCreatePortal(BlockSource& region, BlockPos const& pos, ItemActor* catalyst, Player* player) {
    if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight()) return false;
    Block const& state = region.getBlock(pos);
    if (!canFormPortal(state) || !sturdyTop(region, BlockPos{pos.x, pos.y - 1, pos.z})) return false;
    PortalScan blocksChecked;
    blocksChecked[pos] = true;
    int size           = 0;
    if (!recursivelyValidatePortal(region, pos, blocksChecked, size, state) || size < kMinPortalSize) return false;

    auto const& cfg = gf::config::get<DimensionConfig>();
    if (!cfg.checkPortalPlacement) {
        bool checkProgression = landmark::enforcedProgression();
        if (!teleporter::isSafeAround(region, pos, player, checkProgression)) {
            // TODO in TF: "failure" effect - particles?
            if (player) actionBar(*player, trFor(*player, "misc.twilightforest.portal_unsafe", "It doesn't seem safe here..."));
            return false;
        }
    }
    if (catalyst) {
        auto& stack = catalyst->item();
        if (stack.mCount <= 1) catalyst->remove();
        else stack.remove(1);
    }
    causeLightning(region, pos, cfg.destructivePortalLightning);
    auto const* portal = portalBlock(false);
    if (!portal) {
        log().error("dimension: {} is not registered; no portal", kPortalBlock);
        return false;
    }
    for (auto const& [p, isPool] : blocksChecked)
        if (isPool) game::setBlock(region, p, *portal);
    log().info(
        "dimension: portal lit at {} {} {} in {} ({} blocks{})",
        pos.x,
        pos.y,
        pos.z,
        dimensionName(static_cast<int>(region.getDimensionId())),
        size,
        player ? ", by " + player->getRealName() : std::string()
    );
    return true;
}

/// ProgressionEvents#checkForPortalCreation.
void checkForPortalCreation(Player& player, BlockSource& world, float rangeToCheck) {
    auto const& cfg    = gf::config::get<DimensionConfig>();
    int         dim    = static_cast<int>(player.getDimensionId());
    auto        origin = dimensionByName(cfg.originDimension);
    if (!(origin && dim == *origin) && !world::isTwilight(DimensionType(dim)) && !cfg.allowPortalsInOtherDimensions) return;

    AABB box = player.getAABB();
    box      = AABB{box.min - Vec3(rangeToCheck, rangeToCheck, rangeToCheck), box.max + Vec3(rangeToCheck, rangeToCheck, rangeToCheck)};
    ItemActor* qualified = nullptr;
    auto const owner     = player.getOrCreateUniqueID();
    for (auto* actor : world.getEntities(box, 0.0f, ActorType::ItemEntity)) {
        if (!actor || actor->getEntityTypeId() != ActorType::ItemEntity) continue;
        auto& item = static_cast<ItemActor&>(*actor);
        if (!itemTagged("twilightforest:portal/activator", game::items::idOf(item.item()))) continue;
        BlockPos at = game::blockPosOf(item.getFeetPos());
        if (at.y < world.getMinHeight() || at.y >= world.getMaxHeight() || !canFormPortal(world.getBlock(at))) continue;
        if (itemOwner(item) != owner.rawID) continue;
        qualified = &item;
        break;
    }
    if (!qualified) return;
    if (unworthy(player)) return; // Item qualifies, but the player doesn't

    Vec3                             at = qualified->getFeetPos();
    std::normal_distribution<double> gauss(0.0, 1.0);
    for (int i = 0; i < 2; i++) {
        Vec3 v{static_cast<float>(gauss(rng()) * 0.02), static_cast<float>(gauss(rng()) * 0.02), static_cast<float>(gauss(rng()) * 0.02)};
        fx::particle(world.getDimension(), "minecraft:effect", Vec3{at.x, at.y + 0.2f, at.z}, v);
    }
    if (tryToCreatePortal(world, game::blockPosOf(at), qualified, &player)) {
        progression::trigger(player, kMakePortalTrigger);
    }
}

/// TFPortalBlock#neighborChanged: the portal turns back into water when its floor or rim breaks.
void portalNeighborChanged(BlockSource& region, BlockPos const& pos) {
    Block const& state = region.getBlock(pos);
    if (!isPortal(state)) return;
    bool good = sturdyTop(region, BlockPos{pos.x, pos.y - 1, pos.z});
    for (auto const& [dx, dz] : {std::pair{0, -1}, std::pair{1, 0}, std::pair{0, 1}, std::pair{-1, 0}}) {
        if (!good) break;
        Block const& neighboringState = region.getBlock(BlockPos{pos.x + dx, pos.y, pos.z + dz});
        good = blockTagged("twilightforest:portal/edge", neighboringState.getTypeName()) || &neighboringState == &state;
    }
    if (good) return;
    region.getLevel().broadcastLevelEvent(
        SharedTypes::Legacy::LevelEvent::ParticlesDestroyBlock,
        Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f},
        static_cast<int>(state.mNetworkId),
        nullptr
    );
    if (auto const* water = game::blockWith("minecraft:water")) game::setBlock(region, pos, *water);
    log().info("dimension: portal block at {} {} {} broke back into water", pos.x, pos.y, pos.z);
}

void registerPortalBlockEvents() {
    blockevents::onNeighborChanged(kPortalBlock, [](BlockSource& region, BlockPos const& pos, BlockPos const&) {
        portalNeighborChanged(region, pos);
    });
}

} // namespace gf::feature::dimension

namespace gf::portal {

bool tryActivate(BlockSource& region, BlockPos const& pos) {
    return feature::dimension::tryToCreatePortal(region, pos, nullptr, nullptr);
}

} // namespace gf::portal
