// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/TFPortalBlock.java
//   src/main/java/twilightforest/events/ProgressionEvents.java (portal creation check)
//   src/main/java/twilightforest/config/TFConfig.java (getPortalLockingAdvancement)
//   src/main/java/twilightforest/asmhooks/BlockHooks.java (modifySoilDecisionForMushroomBlockSurvivability)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Lighting a Twilight portal: a player's portal activator item (a diamond) thrown into a water
// pool whose rim is dirt topped with plants; the pool turns into portal blocks under a lightning
// bolt. Portal blocks fall back to water when their floor or rim breaks. Mushrooms next to a portal
// survive in any light.

#include <cctype>
#include <deque>
#include <format>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/Config.h"
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
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/MushroomBlock.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "feature/dimension/DimensionConfig.h"
#include "feature/dimension/Internal.h"
#include "feature/dimension/Teleporter.h"
#include "feature/dimension/World.h"
#include "feature/families/BlockEvents.h"
#include "feature/landmark/Landmark.h"
#include "feature/progression/Progression.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Main.h"

namespace gf::feature::dimension {

namespace {

constexpr int kMinPortalSize = 4;

/// ItemEntity#getOwner: Bedrock keeps no thrower on dropped items, so the player behind every item
/// spawn is remembered here (item unique id -> player unique id), with the spawn order for pruning.
struct Throwers {
    std::unordered_map<std::int64_t, std::int64_t>     owner;
    std::deque<std::pair<std::uint64_t, std::int64_t>> order; ///< (server tick of the spawn, item id)
};
Throwers& throwers() {
    static Throwers t;
    return t;
}

/// Items despawn after 6000 ticks; an entry is checked once it is that old: kept (queued again)
/// while its item still exists, forgotten otherwise. A few entries per call: no spawn scans them all.
constexpr std::uint64_t kThrowerCheckAge = 6000;
void pruneThrowers(Level& level, int budget) {
    auto&         t   = throwers();
    std::uint64_t now = level.getCurrentServerTick().tickID;
    for (int i = 0; i < budget && !t.order.empty(); ++i) {
        auto [tick, id] = t.order.front();
        if (now < tick + kThrowerCheckAge) break;
        t.order.pop_front();
        if (!t.owner.contains(id)) continue;
        if (level.fetchEntity(ActorUniqueID{id}, false)) t.order.emplace_back(now, id);
        else t.owner.erase(id);
    }
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
        auto& level = region.getLevel();
        pruneThrowers(level, 4);
        auto id = idOf(*item);
        if (throwers().owner.insert_or_assign(id, idOf(*spawner)).second) {
            throwers().order.emplace_back(level.getCurrentServerTick().tickID, id);
        }
    }
    return item;
}

std::int64_t itemOwner(ItemActor const& item) {
    auto owner = item.getOwnerId().rawID;
    if (owner != -1 && owner != 0) return owner;
    auto it = throwers().owner.find(idOf(item));
    return it == throwers().owner.end() ? -1 : it->second;
}

void clearItemOwners() {
    throwers().owner.clear();
    throwers().order.clear();
}

void moveItemOwner(std::int64_t from, std::int64_t to) {
    auto it = throwers().owner.find(from);
    if (it == throwers().owner.end()) return;
    auto owner = it->second;
    throwers().owner.erase(it);
    throwers().owner[to] = owner;
    if (auto level = ll::service::getLevel()) throwers().order.emplace_back(level->getCurrentServerTick().tickID, to);
}

bool portalKeepsMushroom(BlockSource& region, BlockPos const& pos) {
    int y = pos.y - 1;
    if (y < region.getMinHeight() || y >= region.getMaxHeight()) return false;
    for (int x = -1; x <= 1; x++) {
        for (int z = -1; z <= 1; z++) {
            if (x == 0 && z == 0) continue;
            BlockPos p{pos.x + x, y, pos.z + z};
            if (region.hasChunksAt(p, 0, false) && isPortal(region.getBlock(p))) return true;
        }
    }
    return false;
}

/// BlockHooks#modifySoilDecisionForMushroomBlockSurvivability (MushroomBlock#canSurvive): the soil
/// decision is "yes" when a Twilight portal is next to the block under the mushroom.
LL_TYPE_INSTANCE_HOOK(
    TwilightMushroomHook,
    ll::memory::HookPriority::Normal,
    MushroomBlock,
    &MushroomBlock::$canSurvive,
    bool,
    BlockSource&    region,
    BlockPos const& pos
) {
    return portalKeepsMushroom(region, pos) || origin(region, pos);
}

void installItemOwners() {
    TwilightItemThrowerHook::hook();
    TwilightMushroomHook::hook();
}
void uninstallItemOwners() {
    TwilightMushroomHook::unhook();
    TwilightItemThrowerHook::unhook();
    clearItemOwners();
}

void actionBar(Player& player, std::string const& text) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::SetTitle);
    if (!packet) return;
    auto& title      = static_cast<SetTitlePacket&>(*packet);
    title.mType      = SetTitlePacketPayload::TitleType::Actionbar;
    title.mTitleText = text;
    packet->sendTo(player);
}

bool advancementExists(std::string const& id) {
    // ResourceLocation.tryParse: namespace:path of [a-z0-9_.-] (path also '/')
    auto colon = id.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= id.size()) return false;
    for (std::size_t i = 0; i < id.size(); ++i) {
        auto c  = static_cast<unsigned char>(id[i]);
        bool ok = std::islower(c) || std::isdigit(c) || c == '_' || c == '-' || c == '.' || (i > colon && c == '/') || i == colon;
        if (!ok) return false;
    }
    auto file = Mod::getInstance().getSelf().getModDir() / "data" / id.substr(0, colon) / "advancement" / (id.substr(colon + 1) + ".json");
    std::error_code ec;
    return std::filesystem::is_regular_file(file, ec);
}

std::optional<std::string> portalLockingAdvancement(bool quiet) {
    // TFConfig#getPortalLockingAdvancement: checked once per configured value
    static std::string checked;
    auto&              cfg = gf::config::mut<DimensionConfig>();
    if (cfg.portalUnlockedByAdvancement.empty()) return std::nullopt;
    if (cfg.portalUnlockedByAdvancement != checked) {
        // a build without TF's advancement data (before the import ships it) can't tell: keep it
        std::error_code ec;
        auto shipped = Mod::getInstance().getSelf().getModDir() / "data" / "twilightforest" / "advancement";
        if (!std::filesystem::is_directory(shipped, ec)) {
            log().warn("dimension: no advancement data to check the portal locking advancement '{}' against", cfg.portalUnlockedByAdvancement);
            checked = cfg.portalUnlockedByAdvancement;
            return cfg.portalUnlockedByAdvancement;
        }
        if (!advancementExists(cfg.portalUnlockedByAdvancement)) {
            // TF logs it as fatal; a selftest provoking it asks for a warning (e2e runs fail on errors)
            auto line = std::format(
                "The portal locking advancement is not a valid advancement! Setting to null! ('{}', config/dimension.json)",
                cfg.portalUnlockedByAdvancement
            );
            if (quiet) log().warn("{}", line);
            else log().error("{}", line);
            cfg.portalUnlockedByAdvancement.clear();
            ll::config::saveConfig(cfg, gf::config::configDir() / "dimension.json");
            return std::nullopt;
        }
        checked = cfg.portalUnlockedByAdvancement;
        log().debug("dimension: portal locking advancement reloaded: {}", checked);
    }
    return cfg.portalUnlockedByAdvancement;
}

bool unworthy(Player& player) {
    if (player.isCreative() || player.isSpectator()) return false;
    auto requirement = portalLockingAdvancement();
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
    auto const* portal = portalBlock(false);
    if (!portal) {
        log().error("dimension: {} is not registered; no portal", kPortalBlock);
        return false;
    }
    Block const& state = region.getBlock(pos);
    if (!canFormPortal(state) || !sturdyTop(region, BlockPos{pos.x, pos.y - 1, pos.z})) return false;
    PortalScan blocksChecked;
    blocksChecked[pos] = true;
    int size           = 0;
    if (!recursivelyValidatePortal(region, pos, blocksChecked, size, state) || size < kMinPortalSize) return false;

    auto const& cfg = gf::config::get<DimensionConfig>();
    if (!cfg.checkPortalPlacement) {
        bool checkProgression = landmark::enforcedProgression();
        // TF checks around the catalyst item: the biome restrictions apply to players only, never here
        if (!teleporter::isSafeAround(region, pos, nullptr, checkProgression)) {
            // TODO in TF: "failure" effect - particles?
            if (player) actionBar(*player, trFor(*player, "misc.twilightforest.portal_unsafe", "It doesn't seem safe here..."));
            return false;
        }
    }
    if (catalyst) {
        // catalyst.getItem().shrink(1); Bedrock does not resend a floating item's stack, so the
        // rest of a stack floats on as a fresh item of the same thrower
        auto& stack = catalyst->item();
        if (stack.mCount > 1) {
            auto  rest  = game::items::withCount(stack, stack.mCount - 1);
            auto* owner = playerById(itemOwner(*catalyst));
            Vec3  at    = catalyst->getFeetPos();
            catalyst->remove();
            region.getLevel().getSpawner().spawnItem(region, rest, owner, at, 0);
        } else {
            catalyst->remove();
        }
    }
    causeLightning(region, pos, cfg.destructivePortalLightning);
    for (auto const& [p, isPool] : blocksChecked)
        if (isPool) game::setBlock(region, p, *portal);
    portalIndexTouch(static_cast<int>(region.getDimensionId()), pos);
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

    // (TF adds two EFFECT particles here with ServerLevel#addParticle, which does nothing on a
    // server: none)
    Vec3 at = qualified->getFeetPos();
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
    portalIndexTouch(static_cast<int>(region.getDimensionId()), pos);
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
