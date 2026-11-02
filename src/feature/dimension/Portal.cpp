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
// survive in any light, and sugar cane takes a portal next to its soil for its water
// (TFPortalBlock#getFluidState; the rest of what the portal's water does is in PortalFluid.cpp).
// Explosions: ExplosionDamageCalculator#getBlockExplosionResistance takes the larger of the block's
// resistance (strength(-1.0F): 0) and its fluid's (water: 100): during an explosion the portal holds
// the water's resistance, the block keeps TF's 0 otherwise.

#include <algorithm>
#include <atomic>
#include <cctype>
#include <deque>
#include <format>
#include <filesystem>
#include <mutex>
#include <tuple>
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
#include "mc/world/level/Explosion.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/MushroomBlock.h"
#include "mc/world/level/block/SugarCaneBlock.h"
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

/// The player whose throw is being spawned (0: none). Java's Player#drop(stack, dropAround,
/// includeThrowerName) names the thrower only for throws (the drop key, items dragged out of a
/// screen, the creative inventory: dropAround false); the death drops of Inventory#dropAll
/// (dropAround true, includeThrowerName false) have no thrower. Bedrock's Player::drop has the same
/// `randomly` (= dropAround) flag; a dead player's drops never count.
thread_local std::int64_t gThrower = 0;

} // namespace

LL_TYPE_INSTANCE_HOOK(
    TwilightPlayerDropHook,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::$drop,
    bool,
    ItemStack const& item,
    bool const       randomly
) {
    if (randomly || !isAlive()) return origin(item, randomly);
    auto outer = gThrower;
    gThrower   = idOf(*this);
    bool r     = origin(item, randomly);
    gThrower   = outer;
    return r;
}

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
    if (item && spawner && gThrower != 0 && spawner->isPlayer() && idOf(*spawner) == gThrower) {
        auto& level = region.getLevel();
        pruneThrowers(level, 4);
        auto id = idOf(*item);
        if (throwers().owner.insert_or_assign(id, gThrower).second) {
            throwers().order.emplace_back(level.getCurrentServerTick().tickID, id);
        }
    }
    return item;
}

std::int64_t itemOwner(ItemActor const& item) {
    // (the engine's own owner id of an item is set for every item a player drops, thrown or not:
    // only the throws recorded here count)
    auto it = throwers().owner.find(idOf(item));
    return it == throwers().owner.end() ? -1 : it->second;
}

void setItemOwner(ItemActor const& item, std::int64_t owner) {
    if (owner == -1 || owner == 0) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    auto id = idOf(item);
    if (throwers().owner.insert_or_assign(id, owner).second) throwers().order.emplace_back(level->getCurrentServerTick().tickID, id);
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

bool portalWatersSugarCane(BlockSource& region, BlockPos const& pos) {
    BlockPos below{pos.x, pos.y - 1, pos.z};
    if (below.y < region.getMinHeight() || below.y >= region.getMaxHeight() || !region.hasChunksAt(below, 0, false)) return false;
    // SugarCaneBlock#canSurvive: the soil is #dirt or #sand, then one of its four sides is water
    auto soil = region.getBlock(below).getTypeName();
    if (!blockTagged("minecraft:dirt", soil) && !blockTagged("minecraft:sand", soil)) return false;
    for (auto const& [dx, dz] : {std::pair{0, -1}, std::pair{1, 0}, std::pair{0, 1}, std::pair{-1, 0}}) {
        BlockPos side{below.x + dx, below.y, below.z + dz};
        if (region.hasChunksAt(side, 0, false) && isPortal(region.getBlock(side))) return true;
    }
    return false;
}

/// TFPortalBlock#getFluidState (the portal is a little water, "and this checks the checkbox in
/// Sugar Cane logic to not destroy itself when portal is made"): a Twilight portal next to the
/// cane's soil counts as its water. Bedrock's sugar cane asks mayPlace when it is placed and
/// whenever it checks that it can stay.
LL_TYPE_INSTANCE_HOOK(
    TwilightSugarCaneHook,
    ll::memory::HookPriority::Normal,
    SugarCaneBlock,
    &SugarCaneBlock::$mayPlace,
    bool,
    BlockSource&    region,
    BlockPos const& pos
) {
    return origin(region, pos) || portalWatersSugarCane(region, pos);
}

namespace {
/// The portal's two states and the water's explosion resistance (looked up each world).
std::atomic<Block const*> gPortalOpen{nullptr};
std::atomic<Block const*> gPortalOneWay{nullptr};
std::atomic<float>        gWaterResistance{0.0f};

/// The explosions in flight on every thread, and what the first one changed: the window is shared
/// by all threads (a thread-local depth would let an explosion on another thread read the raised
/// value as the block's own and put it back for good).
struct ExplosionWindow {
    std::mutex   mutex;
    int          depth     = 0;
    Block const* states[2] = {nullptr, nullptr}; ///< the states the first explosion raised
    float        own[2]    = {0.0f, 0.0f};       ///< what they held before
};
ExplosionWindow& explosionWindow() {
    static ExplosionWindow w;
    return w;
}

/// The resistance in a permutation's direct data (written atomically: other threads may read it).
std::atomic_ref<float> resistanceOf(Block const& block) { return std::atomic_ref<float>(const_cast<Block&>(block).mDirectData->mExplosionResistance); }
} // namespace

void refreshPortalTypes() {
    auto const* water = game::blockWith("minecraft:water");
    gPortalOpen.store(portalBlock(false));
    gPortalOneWay.store(portalBlock(true));
    gWaterResistance.store(water ? water->mDirectData->mExplosionResistance : 0.0f);
}

void beginPortalExplosion() {
    auto&           w = explosionWindow();
    std::lock_guard lock(w.mutex);
    if (w.depth++ > 0) return; // an explosion is in flight: the portal already holds the water's
    float water = gWaterResistance.load();
    w.states[0] = gPortalOpen.load();
    w.states[1] = gPortalOneWay.load();
    for (int i = 0; i < 2; ++i) {
        if (!w.states[i]) continue;
        auto resistance = resistanceOf(*w.states[i]);
        w.own[i]        = resistance.load();
        if (water > 0.0f) resistance.store(std::max(w.own[i], water));
    }
}

void endPortalExplosion() {
    auto&           w = explosionWindow();
    std::lock_guard lock(w.mutex);
    if (w.depth <= 0) {
        w.depth = 0;
        return;
    }
    if (--w.depth > 0) return; // another explosion still runs
    // the last one out puts back what the first one found (in reverse order)
    for (int i = 1; i >= 0; --i) {
        if (w.states[i]) resistanceOf(*w.states[i]).store(w.own[i]);
        w.states[i] = nullptr;
    }
}

int portalExplosionDepth() {
    auto&           w = explosionWindow();
    std::lock_guard lock(w.mutex);
    return w.depth;
}

float portalWaterResistance() { return gWaterResistance.load(); }

/// TFPortalBlock#getFluidState for explosions. The explosion's rays read each block's resistance
/// straight from its permutation's direct data (inline: a hook on BlockType::getExplosionResistance
/// is not consulted, selftest round 4), so the portal's two permutations hold the water's
/// resistance while any explosion runs and TF's 0 otherwise (the explosion window above: nested
/// explosions, explosions on several threads and an explosion that throws all leave TF's 0 behind).
LL_TYPE_INSTANCE_HOOK(TwilightPortalExplosionHook, ll::memory::HookPriority::Normal, Explosion, &Explosion::explode, bool, IRandom& random) {
    PortalExplosionScope window;
    return origin(random);
}

void installItemOwners() {
    TwilightPlayerDropHook::hook();
    TwilightItemThrowerHook::hook();
    TwilightMushroomHook::hook();
    TwilightSugarCaneHook::hook();
    TwilightPortalExplosionHook::hook();
}
void uninstallItemOwners() {
    TwilightPortalExplosionHook::unhook();
    TwilightSugarCaneHook::unhook();
    TwilightMushroomHook::unhook();
    TwilightItemThrowerHook::unhook();
    TwilightPlayerDropHook::unhook();
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

namespace {
/// ResourceLocation.tryParse: namespace:path of [a-z0-9_.-] (path also '/').
bool validResourceLocation(std::string const& id) {
    auto colon = id.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= id.size()) return false;
    for (std::size_t i = 0; i < id.size(); ++i) {
        auto c  = static_cast<unsigned char>(id[i]);
        bool ok = std::islower(c) || std::isdigit(c) || c == '_' || c == '-' || c == '.' || (i > colon && c == '/') || i == colon;
        if (!ok) return false;
    }
    return true;
}
} // namespace

bool advancementExists(std::string const& id) {
    if (!validResourceLocation(id)) return false;
    auto colon = id.find(':');
    auto root  = Mod::getInstance().getSelf().getModDir() / "data" / id.substr(0, colon);
    std::error_code ec;
    for (char const* dir : {"advancement", "advancements"}) {
        if (std::filesystem::is_regular_file(root / dir / (id.substr(colon + 1) + ".json"), ec)) return true;
    }
    return false;
}

bool isVanillaAdvancement(std::string const& id) { return id.starts_with("minecraft:") && validResourceLocation(id); }

std::optional<std::string> portalLockingAdvancement(bool quiet) {
    // TFConfig#getPortalLockingAdvancement: checked once per configured value
    static std::string checked;
    auto&              cfg = gf::config::mut<DimensionConfig>();
    if (cfg.portalUnlockedByAdvancement.empty()) return std::nullopt;
    if (cfg.portalUnlockedByAdvancement != checked && isVanillaAdvancement(cfg.portalUnlockedByAdvancement)
        && !advancementExists(cfg.portalUnlockedByAdvancement)) {
        // PlayerHelper.getAdvancement finds every vanilla advancement on a Java server. Bedrock has
        // none and the mod ships no vanilla advancement data, so the id can't be checked: it is
        // kept, and a player meets it once the progression store has it (gf::progression::grant)
        log().warn(
            "dimension: the portal locking advancement '{}' is a vanilla advancement; Bedrock has no vanilla advancements, so it is "
            "kept unchecked and a player meets it only once the progression store has it",
            cfg.portalUnlockedByAdvancement
        );
        checked = cfg.portalUnlockedByAdvancement;
        return cfg.portalUnlockedByAdvancement;
    }
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
            auto  rest    = game::items::withCount(stack, stack.mCount - 1);
            auto  ownerId = itemOwner(*catalyst);
            auto* owner   = playerById(ownerId);
            Vec3  at      = catalyst->getFeetPos();
            catalyst->remove();
            if (auto* fresh = region.getLevel().getSpawner().spawnItem(region, rest, owner, at, 0)) setItemOwner(*fresh, ownerId);
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
/// Java sets the water with UPDATE_ALL, so the portal blocks next to it hear of it and go too: here
/// the water's block change reaches them through gf::blockevents (F11 tells the six neighbours of
/// every block change at the end of the tick), and the whole pool turns back into water.
bool portalNeighborChanged(BlockSource& region, BlockPos const& pos) {
    if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight()) return false;
    Block const& state = region.getBlock(pos);
    if (!isPortal(state)) return false;
    // Java's getBlockState loads the chunk it reads; here an unloaded neighbour would read as air
    // and break a valid portal: the rule waits until the floor and the four sides are loaded
    for (auto const& [dx, dy, dz] : {std::tuple{0, -1, 0}, std::tuple{0, 0, -1}, std::tuple{1, 0, 0}, std::tuple{0, 0, 1}, std::tuple{-1, 0, 0}}) {
        if (!region.hasChunksAt(BlockPos{pos.x + dx, pos.y + dy, pos.z + dz}, 0, false)) return false;
    }
    bool good = sturdyTop(region, BlockPos{pos.x, pos.y - 1, pos.z});
    for (auto const& [dx, dz] : {std::pair{0, -1}, std::pair{1, 0}, std::pair{0, 1}, std::pair{-1, 0}}) {
        if (!good) break;
        Block const& neighboringState = region.getBlock(BlockPos{pos.x + dx, pos.y, pos.z + dz});
        good = blockTagged("twilightforest:portal/edge", neighboringState.getTypeName()) || &neighboringState == &state;
    }
    if (good) return false;
    region.getLevel().broadcastLevelEvent(
        SharedTypes::Legacy::LevelEvent::ParticlesDestroyBlock,
        Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f},
        static_cast<int>(state.mNetworkId),
        nullptr
    );
    if (auto const* water = game::blockWith("minecraft:water")) game::setBlock(region, pos, *water);
    portalIndexTouch(static_cast<int>(region.getDimensionId()), pos);
    log().info("dimension: portal block at {} {} {} broke back into water", pos.x, pos.y, pos.z);
    return true;
}

void registerPortalBlockEvents() {
    // gf::blockevents keeps its handlers for the process: once, however often the module installs
    static bool const registered = [] {
        blockevents::onNeighborChanged(kPortalBlock, [](BlockSource& region, BlockPos const& pos, BlockPos const&) {
            portalNeighborChanged(region, pos);
        });
        // a portal block placed or removed by anything (a command, a structure, another unit) is
        // read by the portal index on the next tick instead of at its next rescan
        blockevents::onPlaced(kPortalBlock, [](BlockSource& region, BlockPos const& pos, Block const&, Actor*) {
            portalIndexTouch(static_cast<int>(region.getDimensionId()), pos);
        });
        blockevents::onRemoved(kPortalBlock, [](BlockSource& region, BlockPos const& pos, Block const&) {
            portalIndexTouch(static_cast<int>(region.getDimensionId()), pos);
        });
        return true;
    }();
    (void)registered;
}

} // namespace gf::feature::dimension

namespace gf::portal {

bool tryActivate(BlockSource& region, BlockPos const& pos) {
    return feature::dimension::tryToCreatePortal(region, pos, nullptr, nullptr);
}

} // namespace gf::portal
