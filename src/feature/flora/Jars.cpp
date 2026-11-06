// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/JarBlock.java
//   src/main/java/twilightforest/block/FireflyJarBlock.java
//   src/main/java/twilightforest/block/CicadaJarBlock.java
//   src/main/java/twilightforest/block/MasonJarBlock.java
//   src/main/java/twilightforest/block/entity/JarBlockEntity.java
//   src/main/java/twilightforest/block/entity/MasonJarBlockEntity.java
//   src/main/java/twilightforest/item/JarItem.java
//   src/main/java/twilightforest/block/AbstractParticleSpawnerBlock.java
//   src/main/java/twilightforest/block/FireflySpawnerBlock.java
//   src/main/java/twilightforest/events/RegistrationEvents.java (the jar lids)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U6 flora: jars. Custom blocks have no block entities: a jar's lid is its block data
// "components" {"twilightforest:jar_lid": item id} (what the loot contract's copy_components moves
// into the dropped item's data, and placing the item reads back), the mason jar's stack is a
// one-slot gf::inventory container (BlockData backend, kept in the dropped item as
// "minecraft:container") and its item rotation is block data "flora" {"rotation"}. Every change of
// a mason jar is sent to the companions near it (SetMasonJarItemPacket, flora.mason_jar_item),
// and a companion that draws a jar it knows nothing about asks for it on the same channel.

#include "feature/flora/Flora.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "core/Holiday.h"
#include "core/flora/FloraMath.h"
#include "feature/common/BlockData.h"
#include "feature/common/Interactions.h"
#include "feature/common/Redstone.h"
#include "feature/inventory/Inventory.h"
#include "feature/item/Item.h"
#include "feature/net/Net.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::flora {

namespace ia = interactions;
namespace bd = blockdata;

namespace jars {

namespace {

constexpr char kLidKey[] = "twilightforest:jar_lid";

std::set<std::string> const& lids() {
    static std::set<std::string> const s = [] {
        std::set<std::string> out;
        for (char const* w : {"mangrove", "canopy", "dark", "mining", "sorting", "time", "transformation", "twilight_oak"}) {
            out.insert(std::string("twilightforest:") + w + "_log");
            out.insert(std::string("twilightforest:stripped_") + w + "_log");
        }
        for (char const* w : {"acacia", "birch", "cherry", "dark_oak", "jungle", "mangrove", "oak", "spruce"}) {
            out.insert(std::string("minecraft:") + w + "_log");
            out.insert(std::string("minecraft:stripped_") + w + "_log");
        }
        for (char const* w : {"crimson", "warped"}) {
            out.insert(std::string("minecraft:") + w + "_stem");
            out.insert(std::string("minecraft:stripped_") + w + "_stem");
        }
        out.insert("twilightforest:cinder_log");
        out.insert("minecraft:bamboo_block");
        out.insert("minecraft:stripped_bamboo_block");
        return out;
    }();
    return s;
}

void setLid(BlockSource& region, BlockPos const& pos, std::string const& lid) {
    Json comps = Json::object();
    if (auto const* c = bd::get(region, pos, "components"); c && c->is_object()) comps = *c;
    comps[kLidKey] = lid;
    bd::set(region, pos, "components", comps);
}

int rotationAt(BlockSource& region, BlockPos const& pos) {
    auto const* d = bd::get(region, pos, "flora");
    return d && d->is_object() ? d->value("rotation", 0) : 0;
}

void setRotation(BlockSource& region, BlockPos const& pos, int rotation) { bd::set(region, pos, "flora", {{"rotation", rotation}}); }

/// RotationSegment.convertToSegment(yaw + 180): 16 steps.
int segmentOf(Player const& player) {
    return core::flora::rotationSegment(player.getRotation().y + 180.0f);
}

/// DecoratedPotBlockEntity.WobbleStyle (positive: a lid swap / insert, negative: a wiggle).
void wobble(BlockSource& region, BlockPos const& pos, bool positive) {
    Json msg{{"pos", {pos.x, pos.y, pos.z}}, {"positive", positive}};
    int  dim = static_cast<int>(region.getDimensionId());
    region.getLevel().forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) != dim || !net::isCompanion(p)) return true;
        Vec3 at = p.getPosition();
        if (std::abs(at.x - pos.x) > 96 || std::abs(at.z - pos.z) > 96) return true;
        net::send(p, "flora.jar_wobble", msg);
        return true;
    }));
}

/// SetMasonJarItemPacket to the companions tracking the chunk (and to one asking).
Json masonJarMessage(BlockSource& region, BlockPos const& pos) {
    return Json{{"pos", {pos.x, pos.y, pos.z}}, {"item", game::items::toJson(contents(region, pos))}, {"rotation", rotationAt(region, pos)},
                {"lid", lidAt(region, pos)}};
}

void syncMasonJar(BlockSource& region, BlockPos const& pos) {
    Json msg = masonJarMessage(region, pos);
    int  dim = static_cast<int>(region.getDimensionId());
    region.getLevel().forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) != dim || !net::isCompanion(p)) return true;
        Vec3 at = p.getPosition();
        if (std::abs(at.x - pos.x) > 160 || std::abs(at.z - pos.z) > 160) return true;
        net::send(p, "flora.mason_jar_item", msg);
        return true;
    }));
}

std::optional<inventory::Handler> handlerAt(BlockSource& region, BlockPos const& pos) { return inventory::find(region, pos); }

void setContents(BlockSource& region, BlockPos const& pos, ItemStack const& stack) {
    if (auto h = handlerAt(region, pos); h && h->set) h->set(game::globalPos(region, pos), 0, stack);
}

int maxStack(ItemStack const& s) { return std::max<int>(1, s.getMaxStackSize()); }

/// ItemStack#canFitInsideContainerItems: not a shulker box or a bundle (a container item).
bool fitsInside(std::string const& itemId) { return itemId.find("shulker_box") == std::string::npos && itemId.find("bundle") == std::string::npos; }

/// MasonJarBlock#wiggle.
void wiggle(BlockSource& region, BlockPos const& pos) {
    sound(region, pos, "block.twilightforest.jar.wiggle");
    wobble(region, pos, false);
}

} // namespace

bool isLid(std::string const& itemId) {
    if (lids().contains(itemId)) return true;
    return itemId == "minecraft:pumpkin" && core::holiday::isHalloweenWeek();
}

std::string defaultLid(std::string const& jarId) { return jarId == ids::CicadaJar ? "twilightforest:canopy_log" : "twilightforest:twilight_oak_log"; }

std::string lidAt(BlockSource& region, BlockPos const& pos) {
    if (auto const* c = bd::get(region, pos, "components"); c && c->is_object()) {
        auto lid = c->value(kLidKey, std::string{});
        if (!lid.empty()) return lid;
    }
    return defaultLid(idAt(region, pos));
}

ItemStack contents(BlockSource& region, BlockPos const& pos) {
    if (auto h = handlerAt(region, pos); h && h->get) return h->get(game::globalPos(region, pos), 0);
    return ItemStack{};
}

int analog(ItemStack const& stored) {
    // Mth.lerpDiscrete(count / max stack, 0, 15)
    if (game::items::isEmpty(stored)) return 0;
    return core::flora::jarAnalog(game::items::countOf(stored), maxStack(stored));
}

} // namespace jars

void installJars() {
    using namespace jars;
    // the mason jar's slot (BlockData backend: dropped only through the item's container component)
    inventory::registerBlock(ids::MasonJar, {.slots = 1});
    redstone::registerAnalog(ids::MasonJar, [](BlockSource& region, BlockPos const& pos, Block const&) -> std::optional<int> {
        return analog(contents(region, pos));
    });

    for (std::string const& jar : {std::string(ids::FireflyJar), std::string(ids::CicadaJar), std::string(ids::MasonJar)}) {
        // JarItem / BlockItem: the item's components back into the jar (lid, stored stack, rotation)
        ia::onPlace({.block = jar}, [](ia::PlaceContext const& c) {
            auto&    region = c.player.getDimensionBlockSource();
            BlockPos pos    = game::blockPos(c.pos);
            Json     comps  = c.itemBefore ? game::items::getData(*c.itemBefore, "components") : Json{};
            std::string lid = comps.is_object() ? comps.value(kLidKey, std::string{}) : std::string{};
            setLid(region, pos, lid.empty() ? defaultLid(c.blockId) : lid);
            if (c.blockId == ids::MasonJar) {
                if (comps.is_object() && comps.contains("minecraft:container") && comps["minecraft:container"].is_array()
                    && !comps["minecraft:container"].empty()) {
                    setContents(region, pos, game::items::fromJson(comps["minecraft:container"][0]));
                }
                setRotation(region, pos, segmentOf(c.player));
                syncMasonJar(region, pos);
            }
        });

        ia::onUseOn({.block = jar}, [](ia::UseOnContext const& c) {
            bool emptyHands = c.itemId.empty() && game::items::isEmpty(c.player.getOffhandSlot());
            if (c.sneaking && !emptyHands) return ia::Result::Pass;
            if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
            auto&    region = c.player.getDimensionBlockSource();
            BlockPos pos    = game::blockPos(c.pos);
            // JarBlock#useItemOn: a registered lid used on the top 2 px swaps the lid
            if (!c.itemId.empty() && c.hit[1] >= 14.0 / 16.0 && isLid(c.itemId) && c.itemId != lidAt(region, pos)) {
                setLid(region, pos, c.itemId);
                sound(region, pos, "block.twilightforest.jar.lid_swap");
                wobble(region, pos, true);
                if (c.blockId == ids::MasonJar) syncMasonJar(region, pos);
                log().info("flora: {} put a {} lid on the jar at {} {} {}", c.player.getRealName(), c.itemId, pos.x, pos.y, pos.z);
                return ia::Result::Consume;
            }
            if (c.blockId == ids::MasonJar) {
                auto     gpos = game::globalPos(region, pos);
                auto     h    = inventory::find(region, pos);
                if (!h) return ia::Result::Consume;
                if (c.itemId.empty()) {
                    // MasonJarBlock#handleEmptyHand
                    ItemStack stored = contents(region, pos);
                    if (game::items::isEmpty(stored)) {
                        wiggle(region, pos);
                        return ia::Result::Consume;
                    }
                    if (c.sneaking) {
                        actionBar(c.player, game::items::displayName(stored) + " x" + std::to_string(game::items::countOf(stored)));
                        wiggle(region, pos);
                        return ia::Result::Consume;
                    }
                    setContents(region, pos, ItemStack{});
                    // the hand was empty: the stack goes into the selected slot
                    c.player.setSelectedItem(stored);
                    c.player.sendInventory(false);
                    sound(region, pos, "block.twilightforest.jar.remove");
                    wobble(region, pos, false);
                    syncMasonJar(region, pos);
                    return ia::Result::Consume;
                }
                // MasonJarBlock#handleInsert
                ItemStack stored = contents(region, pos);
                int       room   = 0;
                if (fitsInside(c.itemId)) {
                    if (game::items::isEmpty(stored)) room = maxStack(c.item);
                    else if (game::items::sameItemSameData(stored, c.item)) room = maxStack(stored) - game::items::countOf(stored);
                }
                int count    = game::items::countOf(c.item);
                int inserted = std::clamp(room, 0, count);
                if (inserted <= 0) {
                    wiggle(region, pos);
                    return ia::Result::Consume;
                }
                setRotation(region, pos, segmentOf(c.player));
                ItemStack before = c.item;
                ItemStack now    = game::items::isEmpty(stored) ? game::items::withCount(c.item, inserted)
                                                                : game::items::withCount(stored, game::items::countOf(stored) + inserted);
                setContents(region, pos, now);
                if (!c.player.isCreative()) {
                    auto& held = ia::mutableItem(c.item);
                    held       = count - inserted > 0 ? game::items::withCount(before, count - inserted) : ItemStack::EMPTY_ITEM();
                    c.player.sendInventory(false);
                }
                float filled = static_cast<float>(inserted) / static_cast<float>(maxStack(before));
                sound(region, pos, "block.twilightforest.jar.insert", 1.0f, 0.7f + 0.5f * filled);
                wobble(region, pos, true);
                syncMasonJar(region, pos);
                log().info("flora: {} put {} x{} in the mason jar at {} {} {}", c.player.getRealName(), c.itemId, inserted, pos.x, pos.y, pos.z);
                (void)gpos;
                return ia::Result::Consume;
            }
            // FireflyJarBlock / CicadaJarBlock#useWithoutItem: sneaking releases the critter
            if (c.sneaking && emptyHands) {
                std::string lid     = lidAt(region, pos);
                std::string critter = c.blockId == ids::FireflyJar ? ids::Firefly : ids::Cicada;
                removeBlock(region, pos);
                if (auto s = makeItem(critter)) spawnItemAt(region, static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z), *s);
                if (auto m = makeItem(ids::MasonJar)) {
                    game::items::setData(*m, "components", {{kLidKey, lid}});
                    spawnItemAt(region, static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z), *m);
                }
                log().info("flora: {} released a {} from its jar", c.player.getRealName(), critter.substr(15));
                return ia::Result::Consume;
            }
            // JarBlock#useWithoutItem: empty hands wiggle the jar
            if (emptyHands) {
                sound(region, pos, "block.twilightforest.jar.wiggle");
                wobble(region, pos, false);
                return ia::Result::Consume;
            }
            return ia::Result::Pass;
        });
    }

    // JarItem.MasonJarItem#appendHoverText: "<name> x<count>" of the stored stack
    item::addTooltip(ids::MasonJar, [](ItemStack const& stack, std::vector<std::string>& lines, Player const*) {
        auto comps = game::items::getData(stack, "components");
        if (!comps.is_object() || !comps.contains("minecraft:container") || !comps["minecraft:container"].is_array()) return;
        auto const& items = comps["minecraft:container"];
        if (items.empty()) return;
        ItemStack stored = game::items::fromJson(items[0]);
        if (game::items::isEmpty(stored)) return;
        lines.push_back("§7" + game::items::displayName(stored) + " x" + std::to_string(game::items::countOf(stored)));
    });

    // a companion asks for a mason jar it draws (the block entity's update tag on chunk load)
    net::on("flora.mason_jar_item", [](Player& player, Json const& msg) {
        auto const& p = msg.value("pos", Json::array());
        if (!p.is_array() || p.size() != 3) return;
        BlockPos pos{p[0].get<int>(), p[1].get<int>(), p[2].get<int>()};
        auto&    region = player.getDimensionBlockSource();
        if (idAt(region, pos) != ids::MasonJar) return;
        Vec3 at = player.getPosition();
        if (std::abs(at.x - pos.x) > 160 || std::abs(at.z - pos.z) > 160) return;
        net::send(player, "flora.mason_jar_item", masonJarMessage(region, pos));
    });

    // ---- firefly particle spawner (FireflySpawnerBlock#useItemOn)
    ia::onUseOn({.block = ids::FireflySpawner}, [](ia::UseOnContext const& c) {
        bool emptyHands = c.itemId.empty() && game::items::isEmpty(c.player.getOffhandSlot());
        if (c.sneaking && !emptyHands) return ia::Result::Pass;
        if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
        auto&        region = c.player.getDimensionBlockSource();
        BlockPos     pos    = game::blockPos(c.pos);
        Block const* b      = blockAt(region, pos);
        if (!b) return ia::Result::Pass;
        int radius = std::max(1, intState(*b, st::Radius, 1));
        if (c.itemId == ids::Firefly && !c.sneaking && radius < 10) {
            setStates(region, pos, {{st::Radius, radius + 1}});
            consumeHeld(c.player, c.item);
            actionBar(c.player, trFor(c.player, "misc.twilightforest.firefly_spawner_radius", "Firefly Particle Radius: %s Blocks", radius + 1));
            return ia::Result::Consume;
        }
        if (c.sneaking && radius > 1) {
            setStates(region, pos, {{st::Radius, radius - 1}});
            if (auto s = makeItem(ids::Firefly)) spawnItemAt(region, pos.x + .5f, pos.y + 1.0f, pos.z + .5f, *s);
            actionBar(c.player, trFor(c.player, "misc.twilightforest.firefly_spawner_radius", "Firefly Particle Radius: %s Blocks", radius - 1));
            return ia::Result::Consume;
        }
        return ia::Result::Pass;
    });
    // AbstractParticleSpawnerBlock: a new spawner starts at radius 1
    ia::onPlace({.block = ids::FireflySpawner}, [](ia::PlaceContext const& c) {
        auto& region = c.player.getDimensionBlockSource();
        BlockPos pos = game::blockPos(c.pos);
        if (auto const* b = blockAt(region, pos); b && intState(*b, st::Radius, 0) < 1) setStates(region, pos, {{st::Radius, 1}});
    });
}

} // namespace gf::feature::flora
