// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/KeepsakeCasketBlock.java
//   src/main/java/twilightforest/block/SkullChestBlock.java
//   src/main/java/twilightforest/block/entity/SkullChestBlockEntity.java
//   src/main/java/twilightforest/block/entity/KeepsakeCasketBlockEntity.java
//   src/main/java/twilightforest/item/KeepsakeCasketItem.java
//   src/main/java/twilightforest/events/CharmEvents.java (stockKeepsakeCasket)
//   src/main/java/twilightforest/events/EntityEvents.java (onCasketBreak)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The keepsake casket block: a 45-slot container of F10's inventory contract (declared in
// assets/data/containers/artifacts.json: storage, open / close sounds, comparator output, the
// contents dropping when it goes) with TF's own rules on top:
//   - use: not while solidly logged; a charm of keeping III repairs a damaged casket one level;
//     a locked casket (owner set) opens only for its owner or an operator, others get the red
//     "only <owner>" message and the locked click; the screen's title is the casket's name;
//   - breaking an owned, non-empty casket is refused unless the breaker is an operator and the owner
//     (TF's `!op || !owner` test); a survival break drops the casket item with its damage and name,
//     and the block it was logged with;
//   - placing restores the item's damage and faces the player; the fluid at the spot logs it;
//   - lava and water around it turn a lava / water log into obsidian, basalt or stone (fizz).
// Owner and name live in the block data ("artifacts"); the damage is the block state.
#include <cmath>
#include <map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/platform/UUID.h"
#include "mc/safety/RedactableString.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"

#include "feature/artifacts/Artifacts.h"
#include "feature/common/BlockData.h"
#include "feature/common/Interactions.h"
#include "feature/families/BlockEvents.h"
#include "feature/fx/Fx.h"
#include "feature/inventory/Inventory.h"
#include "feature/item/Item.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::artifacts {

namespace ia    = gf::feature::common::interactions;
namespace bd    = gf::feature::common::blockdata;
namespace items = game::items;

namespace {

constexpr char kBdKey[]     = "artifacts";
constexpr char kDamage[]    = "twilightforest:damage";
constexpr char kFacing[]    = "twilightforest:facing";
constexpr char kLogged[]    = "twilightforest:multilogged";
constexpr int  kCasketSlots = 45;

std::string stateText(Block const& b, char const* name) {
    auto states = game::statesOf(b);
    auto it     = states.find(name);
    return it == states.end() ? std::string() : it->second;
}

int damageOf(Block const& b) { return game::intState(b, kDamage).value_or(0); }

/// BlockLoggingEnum: a solid log (obsidian, stone, basalt) keeps the casket shut.
bool solidlyLogged(Block const& b) {
    auto l = stateText(b, kLogged);
    return l == "obsidian" || l == "stone" || l == "basalt";
}

/// BlockLoggingEnum#getFromFluid at `pos`.
std::string fluidLog(BlockSource& region, BlockPos const& pos) {
    auto id = game::blockIdAt(region, pos);
    if (id == "minecraft:water" || id == "minecraft:flowing_water") return "water";
    if (id == "minecraft:lava" || id == "minecraft:flowing_lava") return "lava";
    auto const& liquid = region.getLiquidBlock(pos);
    if (!liquid.isAir()) {
        auto lid = game::blockId(liquid);
        if (lid.find("water") != std::string::npos) return "water";
        if (lid.find("lava") != std::string::npos) return "lava";
    }
    return "air";
}

bool doTileDrops(Level& level) {
    GameRuleId id;
    id.mValue = static_cast<int>(GameRules::GameRulesIndex::DoTileDrops);
    return level.getGameRules().getBool(id, true);
}

std::map<core::GlobalPos, std::string>& pendingName() {
    static std::map<core::GlobalPos, std::string> m;
    return m;
}

bool isOperator(Player& p) { return p.getCommandPermissionLevel() >= CommandPermissionLevel::GameDirectors; }

bool containerEmpty(BlockSource& region, BlockPos const& pos) {
    auto h = gf::inventory::find(region, pos);
    if (!h || !h->get) return true;
    auto gpos = game::globalPos(region, pos);
    for (int i = 0; i < h->slots; ++i)
        if (!items::isEmpty(h->get(gpos, i))) return false;
    return true;
}

void spawnAt(BlockSource& region, BlockPos const& pos, ItemStack const& s) {
    region.getLevel().getSpawner().spawnItem(region, s, nullptr, Vec3{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)}, 10);
}

/// The casket item's name by damage (KeepsakeCasketItem#getDescriptionId).
std::string itemName(int damage, Player const* viewer) {
    if (damage == 1) return viewer ? trFor(*viewer, "block.twilightforest.chipped_keepsake_casket", "Chipped Keepsake Casket") : tr("block.twilightforest.chipped_keepsake_casket", "Chipped Keepsake Casket");
    if (damage >= 2) return viewer ? trFor(*viewer, "block.twilightforest.damaged_keepsake_casket", "Damaged Keepsake Casket") : tr("block.twilightforest.damaged_keepsake_casket", "Damaged Keepsake Casket");
    return {};
}

/// SkullChestBlock#reactWithNeighbors.
void reactWithNeighbors(BlockSource& region, BlockPos const& pos) {
    auto const& b   = region.getBlock(pos);
    auto        log = stateText(b, kLogged);
    if (log != "lava" && log != "water") return;
    static constexpr int d[5][3] = {{0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}}; // every side but down
    bool soulSoil = game::blockIdAt(region, BlockPos{pos.x, pos.y - 1, pos.z}) == "minecraft:soul_soil";
    for (auto const& o : d) {
        BlockPos    n{pos.x + o[0], pos.y + o[1], pos.z + o[2]};
        std::string nf = fluidLog(region, n);
        std::string to;
        if (log == "lava") {
            if (nf == "water") to = "obsidian";
            if (soulSoil && game::blockIdAt(region, n) == "minecraft:blue_ice") to = "basalt";
        } else if (nf == "lava") {
            to = "stone";
        }
        if (to.empty()) continue;
        auto states   = game::statesOf(region.getBlock(pos));
        Json j        = Json::object();
        j[kFacing]    = states[kFacing];
        j[kDamage]    = damageOf(region.getBlock(pos));
        j[kLogged]    = to;
        if (auto const* nb = game::blockWithStates(kCasket, j)) {
            game::setBlock(region, pos, *nb);
            region.getLevel().broadcastLevelEvent(SharedTypes::Legacy::LevelEvent::SoundFizz, Vec3{pos.x + 0.5F, pos.y + 0.5F, pos.z + 0.5F}, 0, nullptr);
        }
        return;
    }
}

} // namespace

std::optional<CasketInfo> casketInfo(BlockSource& region, BlockPos const& pos) {
    auto const* j = bd::get(region, pos, kBdKey);
    if (!j || !j->is_object()) return std::nullopt;
    return CasketInfo{j->value("owner", std::string{}), j->value("owner_name", std::string{}), j->value("name", std::string{})};
}

void setCasketInfo(BlockSource& region, BlockPos const& pos, CasketInfo const& info) {
    bd::set(region, pos, kBdKey, Json{{"owner", info.ownerUuid}, {"owner_name", info.ownerName}, {"name", info.name}});
}

bool canOpenCasket(Player& player, BlockSource& region, BlockPos const& pos) {
    auto info = casketInfo(region, pos);
    if (!info || info->ownerUuid.empty()) return true;
    return isOperator(player) || player.getUuid().asString() == info->ownerUuid;
}

bool placeCasket(Player& player, std::vector<ItemStack> const& contents, int damage) {
    auto& region = player.getDimensionBlockSource();
    auto& dim    = player.getDimension();
    auto  feet   = player.getFeetPos();
    int   minY   = region.getMinHeight();
    int   logical = region.getMaxHeight(); // Bedrock has no separate logical height
    BlockPos pos{static_cast<int>(std::floor(feet.x)), core::artifacts::casketStartY(static_cast<int>(std::floor(feet.y)), minY, logical),
                 static_cast<int>(std::floor(feet.z))};
    while (!game::replaceable(region, pos) && pos.y < region.getMaxHeight() - 1) ++pos.y;
    auto& r      = levelRandom();
    auto  facing = core::artifacts::facingName(r.nextInt(3));
    auto  roll   = core::artifacts::rollCasketDamage(damage, randomOf(player).nextFloat());
    std::string id = roll.skullChest ? kSkullChest : kCasket;
    Json states{{kFacing, facing}, {kLogged, fluidLog(region, pos)}};
    if (!roll.skullChest) states[kDamage] = roll.damage;
    std::string  logged = fluidLog(region, pos);
    Block const* block  = game::blockWithStates(id, states);
    bool         placed = block && game::setBlock(region, pos, *block);
    if (roll.skullChest && (!placed || !gf::inventory::find(region, pos).has_value())) {
        // U22's skull chest is not a container (yet): the casket stays at damage 2
        block  = game::blockWithStates(kCasket, Json{{kFacing, facing}, {kLogged, logged}, {kDamage, 2}});
        id     = kCasket;
        placed = block && game::setBlock(region, pos, *block);
    } else if (roll.skullChest) {
        log().debug("artifacts: {}'s casket damage value was too high, placing a skull chest instead", player.getRealName());
    }
    if (!placed) {
        log().error("artifacts: could not place a keepsake casket at {} {} {}", pos.x, pos.y, pos.z);
        return false;
    }
    auto h = gf::inventory::find(region, pos);
    if (!h || !h->set) {
        log().error("artifacts: failed to set the keepsake casket data at {} {} {}", pos.x, pos.y, pos.z);
        return false;
    }
    CasketInfo info;
    if (config().casketUUIDLocking) {
        info.ownerUuid = player.getUuid().asString();
        info.ownerName = player.getRealName();
    }
    std::string blockName = tr("block.twilightforest.keepsake_casket", "Keepsake Casket");
    info.name = core::artifacts::casketName(player.getRealName(), r.nextInt(1000) == 0 ? std::string("Costco Casket") : blockName);
    setCasketInfo(region, pos, info);
    auto gpos = game::globalPos(region, pos);
    for (int i = 0; i < kCasketSlots && i < static_cast<int>(contents.size()); ++i) h->set(gpos, i, contents[static_cast<std::size_t>(i)]);
    (void)dim;
    log().info("artifacts: placed {}'s {} at {} {} {} (damage {})", player.getRealName(), id, pos.x, pos.y, pos.z, roll.damage);
    return true;
}

void registerCasket() {
    // KeepsakeCasketBlock#useItemOn (+ SkullChestBlockEntity#canOpen): before the container opens
    ia::onUseOn({.block = kCasket}, [](ia::UseOnContext const& c) {
        auto* region = game::regionOf(c.pos.dim);
        if (!region) return ia::Result::Pass;
        bool emptyHands = c.itemId.empty() && items::isEmpty(c.player.getOffhandSlot());
        if (c.sneaking && !emptyHands) return ia::Result::Pass;
        if (solidlyLogged(c.block)) return ia::Result::Pass;
        if (!c.firstEvent) return ia::Result::Consume;
        BlockPos pos = game::blockPos(c.pos);
        int      dmg = damageOf(c.block);
        if (c.itemId == kKeeping3 && dmg > 0) {
            if (!creative(c.player)) {
                ia::mutableItem(c.item).remove(1);
                c.player.sendInventory(false);
            }
            Json j = Json{{kFacing, stateText(c.block, kFacing)}, {kLogged, stateText(c.block, kLogged)}, {kDamage, dmg - 1}};
            if (auto const* b = game::blockWithStates(kCasket, j)) game::setBlock(*region, pos, *b);
            auto& r = levelRandom();
            gf::fx::sound(c.player.getDimension(), "block.twilightforest.casket.repair", Vec3{pos.x + 0.5F, pos.y + 0.5F, pos.z + 0.5F}, 0.5F, r.nextFloat() * 0.1F + 0.9F);
            log().info("artifacts: {} repaired a keepsake casket to damage {}", c.player.getRealName(), dmg - 1);
            return ia::Result::Consume;
        }
        if (!canOpenCasket(c.player, *region, pos)) {
            auto info = casketInfo(*region, pos);
            gf::fx::soundTo(c.player, "block.twilightforest.casket.locked", 0.5F, 0.5F);
            actionBar(c.player, "§c" + trFor(c.player, "block.twilightforest.casket.locked", "This Casket can only be opened by %s!", info ? info->ownerName : std::string()));
            log().info("artifacts: {} was refused a locked keepsake casket", c.player.getRealName());
            return ia::Result::Deny;
        }
        auto info  = casketInfo(*region, pos);
        auto title = info && !info->name.empty() ? info->name : std::string("block.twilightforest.keepsake_casket");
        gf::inventory::openChestLike(c.player, c.pos, 5, title);
        return ia::Result::Consume;
    }, ia::priority::High);
    // EntityEvents#onCasketBreak + SkullChestBlock#playerWillDestroy
    ia::onDestroy({.block = kCasket}, [](ia::DestroyContext const& c) {
        auto* region = game::regionOf(c.pos.dim);
        if (!region) return ia::Result::Pass;
        BlockPos pos  = game::blockPos(c.pos);
        auto     info = casketInfo(*region, pos);
        if (info && !info->ownerUuid.empty() && !containerEmpty(*region, pos)) {
            bool owner = c.player.getUuid().asString() == info->ownerUuid;
            if (!isOperator(c.player) || !owner) {
                log().info("artifacts: {} may not break an owned keepsake casket", c.player.getRealName());
                return ia::Result::Deny;
            }
        }
        // remembered for the drop once the block is really gone (onDestroyed)
        pendingName()[c.pos] = info ? info->name : std::string();
        return ia::Result::Pass;
    }, ia::priority::High);
    ia::onDestroyed({.block = kCasket}, [](ia::DestroyContext const& c, bool destroyed) {
        auto it   = pendingName().find(c.pos);
        auto name = it != pendingName().end() ? it->second : std::string();
        if (it != pendingName().end()) pendingName().erase(it);
        auto* region = game::regionOf(c.pos.dim);
        if (!destroyed || !region || c.player.isCreative() || !doTileDrops(c.player.getLevel())) return;
        BlockPos pos = game::blockPos(c.pos);
        int      dmg = damageOf(c.block);
        if (auto s = items::make(kCasket)) {
            if (dmg > 0) items::setData(*s, kDataCasketDamage, dmg);
            if (!name.empty()) s->setCustomName(Bedrock::Safety::RedactableString(name));
            spawnAt(*region, pos, *s);
        }
        auto log = stateText(c.block, kLogged);
        if (log == "obsidian" || log == "stone" || log == "basalt")
            if (auto s = items::make("minecraft:" + log)) spawnAt(*region, pos, *s);
    });
    // KeepsakeCasketBlock#setPlacedBy / SkullChestBlock#getStateForPlacement
    ia::onPlace({.block = kCasket}, [](ia::PlaceContext const& c) {
        auto* region = game::regionOf(c.pos.dim);
        if (!region) return;
        BlockPos pos    = game::blockPos(c.pos);
        int      facing = (game::playerFacing(c.player) + 2) % 4; // getHorizontalDirection().getOpposite()
        int      dmg    = 0;
        if (c.itemBefore) {
            auto d = items::getData(*c.itemBefore, kDataCasketDamage);
            if (d.is_number_integer()) dmg = std::clamp(d.get<int>(), 0, 2);
        }
        Json j{{kFacing, core::artifacts::facingName(facing)}, {kDamage, dmg}, {kLogged, stateText(c.block, kLogged).empty() ? "air" : stateText(c.block, kLogged)}};
        if (auto const* b = game::blockWithStates(kCasket, j)) game::setBlock(*region, pos, *b);
        if (c.itemBefore && !c.itemBefore->getCustomName().empty()) setCasketInfo(*region, pos, CasketInfo{{}, {}, c.itemBefore->getCustomName()});
    });
    gf::blockevents::onNeighborChanged(kCasket, [](BlockSource& region, BlockPos const& pos, BlockPos const&) { reactWithNeighbors(region, pos); });
    // the item name by damage (KeepsakeCasketItem#getDescriptionId) as its first lore line
    gf::item::addTooltip(kCasket, [](ItemStack const& stack, std::vector<std::string>& lines, Player const* viewer) {
        auto d = items::getData(stack, kDataCasketDamage);
        if (d.is_number_integer() && d.get<int>() > 0) lines.insert(lines.begin(), "§e" + itemName(d.get<int>(), viewer));
    });
}

} // namespace gf::feature::artifacts

