// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/CritterBlock.java
//   src/main/java/gloamingforest/block/CicadaBlock.java
//   src/main/java/gloamingforest/block/FireflyBlock.java
//   src/main/java/gloamingforest/block/MoonwormBlock.java
//   src/main/java/gloamingforest/events/MiscEvents.java (updateCicadaSoundsOnHead)
//   src/main/java/gloamingforest/dispenser/TFDispenserBehaviors.java (critters on heads)
//   src/main/java/gloamingforest/advancements/KillBugTrigger.java (fired with the bug's block)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U6 flora: the critters. Their client-side life (glow, yaw wiggle, singing, particles) is the
// client mod's (src/client/flora); the server places them against a supporting face, pops them
// when the support goes, squishes them under projectiles and falling blocks (stat bugs_squished,
// trigger kill_bug), lets an empty mason jar catch fireflies and cicadas, and tells the companions
// around a player who puts a cicada on (the looping cicada sound, flora.moving_cicada_sound).

#include "feature/flora/Flora.h"

#include <array>
#include <cmath>
#include <unordered_map>

#include "mc/deps/core/math/Vec3.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockSupportType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "feature/common/Interactions.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/families/BlockEvents.h"
#include "feature/fx/Fx.h"
#include "feature/item/Item.h"
#include "feature/loot/Loot.h"
#include "feature/net/Net.h"
#include "feature/progression/Progression.h"
#include "feature/stats/Stats.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::flora {

namespace ia = interactions;

namespace critters {

namespace {

constexpr std::array<char const*, 3> kCritters{ids::Firefly, ids::Cicada, ids::Moonworm};

std::uint64_t gSquished = 0;

/// Java Direction.orderedByNearest(entity): the six directions by how much the look follows them.
std::array<int, 6> nearestLooking(Actor const& actor) {
    auto  rot   = actor.getRotation();
    float pitch = rot.x * 3.14159265f / 180.0f, yaw = -rot.y * 3.14159265f / 180.0f;
    float f2 = std::sin(pitch), f3 = std::cos(pitch), f4 = std::sin(yaw), f5 = std::cos(yaw);
    bool  flag = f4 > 0.0f, flag1 = f2 < 0.0f, flag2 = f5 > 0.0f;
    float f6 = flag ? f4 : -f4, f7 = flag1 ? -f2 : f2, f8 = flag2 ? f5 : -f5;
    float f9 = f6 * f3, f10 = f8 * f3;
    int   x = flag ? East : West, y = flag1 ? Up : Down, z = flag2 ? South : North;
    auto  make = [](int a, int b, int c) { return std::array<int, 6>{a, b, c, opposite(c), opposite(b), opposite(a)}; };
    if (f6 > f8) {
        if (f7 > f9) return make(y, x, z);
        return f10 > f7 ? make(x, z, y) : make(x, y, z);
    }
    if (f7 > f10) return make(y, z, x);
    return f9 > f7 ? make(z, x, y) : make(z, y, x);
}

std::string squishTable(std::string const& id) { return "gloamingforest:blocks/" + id.substr(15) + "_squish"; }

/// The mason jar item's stored stack is empty (CritterBlock#useItemOn: no container or an empty one).
bool emptyMasonJar(ItemStack const& jar) {
    auto comps = game::items::getData(jar, "components");
    if (!comps.is_object()) return true;
    auto const& c = comps.value("minecraft:container", Json::array());
    if (!c.is_array() || c.empty()) return true;
    return game::items::isEmpty(game::items::fromJson(c[0]));
}

} // namespace

bool isCritter(std::string const& blockId) {
    for (auto const* c : kCritters)
        if (blockId == c) return true;
    return false;
}

bool canSurvive(BlockSource& region, BlockPos const& pos, int facing) {
    BlockPos     resting = relative(pos, opposite(facing));
    Block const* rest    = blockAt(region, resting);
    if (!rest || rest->isAir()) return false;
    return rest->canProvideSupport(static_cast<uchar>(facing), BlockSupportType::Center) || features::kit::isLeaves(*rest);
}

std::optional<int> placementFacing(BlockSource& region, BlockPos const& pos, int face, Actor const* placer) {
    if (face >= 0 && face < 6 && canSurvive(region, pos, face)) return face;
    if (!placer) return std::nullopt;
    for (int dir : nearestLooking(*placer))
        if (canSurvive(region, pos, opposite(dir))) return opposite(dir);
    return std::nullopt;
}

bool squish(BlockSource& region, BlockPos const& pos, Actor* projectile, Player* owner) {
    Block const* b = blockAt(region, pos);
    if (!b || !isCritter(b->getTypeName())) return false;
    Block const& state  = *b;
    std::string  id     = state.getTypeName();
    bool         wall   = stateOf(state, st::Facing) != "up" && !stateOf(state, st::Facing).empty();
    // to air (the water stays for a waterlogged moonworm)
    removeBlock(region, pos);
    sound(region, pos, "block.gloamingforest.bug.squish", 1.0f, 1.0f);
    loot::LootContext ctx;
    ctx.region = &region;
    ctx.origin = Vec3{pos.x + .5f, pos.y + .5f, pos.z + .5f};
    ctx.block  = &state;
    ctx.tool   = &game::items::empty();
    for (auto const& s : loot::roll(squishTable(id), ctx)) popResource(region, pos, s);
    // 50 slime block particles
    if (auto level = &region.getLevel()) {
        if (auto dim = level->getDimension(region.getDimensionId()).lock()) {
            auto& random = levelRandom();
            (void)random;
            // x / z at 0.25..0.75 of the block: one call, spread around the centre
            fx::particle(*dim, "minecraft:block", Vec3{pos.x + .5f, pos.y + (wall ? 0.5f : 0.0f), pos.z + .5f}, {}, 50,
                         {{"block", "minecraft:slime"}, {"spread", {0.15, 0.0, 0.15}}});
        }
    }
    ++gSquished;
    if (owner) {
        stats::award(*owner, "gloamingforest:bugs_squished");
        progression::trigger(*owner, "gloamingforest:kill_bug", {{"block", id}});
    }
    log().info("flora: a {} was squished at {} {} {}{}", id.substr(15), pos.x, pos.y, pos.z, owner ? " by " + owner->getRealName() : std::string());
    (void)projectile;
    return true;
}

std::uint64_t squished() { return gSquished; }

} // namespace critters

namespace {

/// The players wearing a cicada (unique id -> next vanilla sound tick).
std::unordered_map<std::int64_t, std::int64_t>& cicadaWearers() {
    static std::unordered_map<std::int64_t, std::int64_t> m;
    return m;
}

/// MiscEvents#updateCicadaSoundsOnHead: a cicada put on starts the looping sound for the wearer's
/// trackers (companions: the client's MovingCicadaSoundInstance; vanilla clients: the cicada's
/// sound now and then at the wearer, best effort).
void tickCicadaWearers(Level& level) {
    auto& wearers = cicadaWearers();
    std::erase_if(wearers, [&](auto const& e) { return level.fetchEntity(ActorUniqueID{e.first}, false) == nullptr; });
    level.forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
        bool wears = game::items::idOf(p.getArmor(SharedTypes::Legacy::ArmorSlot::Head)) == ids::Cicada;
        auto uid   = p.getOrCreateUniqueID().rawID;
        auto it    = wearers.find(uid);
        if (!wears) {
            if (it != wearers.end()) wearers.erase(it);
            return true;
        }
        if (it == wearers.end()) {
            // just equipped: CreateMovingCicadaSoundPacket to the trackers and the wearer
            Json msg{{"entity", p.getOrCreateUniqueID().rawID}};
            Vec3 at = p.getPosition();
            level.forEachPlayer(std::function<bool(Player&)>([&](Player& other) {
                if (other.getDimensionId() != p.getDimensionId()) return true;
                Vec3 o = other.getPosition();
                if ((o.x - at.x) * (o.x - at.x) + (o.z - at.z) * (o.z - at.z) > 64.0 * 64.0) return true;
                if (net::isCompanion(other)) net::send(other, "flora.moving_cicada_sound", msg);
                return true;
            }));
            wearers[uid] = now() + 100 + levelRandom().nextInt(100);
            log().info("flora: {} wears a cicada", p.getRealName());
            return true;
        }
        if (now() >= it->second) {
            // vanilla clients near the wearer: the cicada sound now and then (companions loop it themselves)
            Vec3 at = p.getPosition();
            level.forEachPlayer(std::function<bool(Player&)>([&](Player& other) {
                if (other.getDimensionId() != p.getDimensionId() || net::isCompanion(other)) return true;
                Vec3 o = other.getPosition();
                if ((o.x - at.x) * (o.x - at.x) + (o.y - at.y) * (o.y - at.y) + (o.z - at.z) * (o.z - at.z) <= 16.0 * 16.0)
                    fx::soundTo(other, "block.gloamingforest.cicada", 1.0f, 1.0f);
                return true;
            }));
            it->second = now() + 100 + levelRandom().nextInt(100);
        }
        return true;
    }));
}

} // namespace

void tickCritters(Level& level) { tickCicadaWearers(level); }

void installCritters() {
    using namespace critters;
    // a companion joining hears the cicadas already worn (CreateMovingCicadaSoundPacket of the trackers)
    net::onCompanionJoined([](Player& companion) {
        for (auto const& [uid, next] : cicadaWearers()) net::send(companion, "flora.moving_cicada_sound", {{"entity", uid}});
    });
    for (auto const* critter : kCritters) {
        std::string id = critter;
        // CritterBlock#getStateForPlacement: the clicked face if it is supported, else the
        // nearest looking direction that is; none: no placement
        ia::onPlace({.block = id}, [](ia::PlaceContext const& c) {
            auto&    region = c.player.getDimensionBlockSource();
            BlockPos pos    = game::blockPos(c.pos);
            auto     facing = placementFacing(region, pos, c.face, &c.player);
            if (!facing) {
                Block const* b = blockAt(region, pos);
                removeBlock(region, pos);
                if (b && !c.player.isCreative()) {
                    if (auto s = makeItem(c.blockId)) give(c.player, *s);
                }
                return;
            }
            setStates(region, pos, {{st::Facing, dirName(*facing)}});
        });
        // CritterBlock#updateShape: no support, no bug (with its drops)
        blockevents::onNeighborChanged(id, [](BlockSource& region, BlockPos const& pos, BlockPos const&) {
            Block const* b = blockAt(region, pos);
            if (!b) return;
            int facing = dirFromName(stateOf(*b, st::Facing));
            if (facing < 0) facing = Up;
            if (!canSurvive(region, pos, facing)) destroyBlock(region, pos, true);
        });
        // CritterBlock#entityInside: projectiles (not #dont_kill_bugs) and falling blocks squish it
        blockevents::onEntityInside(id, [](BlockSource& region, BlockPos const& pos, Actor& entity) {
            bool projectile = entity.hasType(ActorType::Projectile)
                           && !tags::has(tags::Registry::EntityType, "gloamingforest:dont_kill_bugs", entity.getTypeName());
            bool falling = entity.isType(ActorType::FallingBlock);
            if (!projectile && !falling) return;
            Player* owner = projectile ? entity.getPlayerOwner() : nullptr;
            squish(region, pos, &entity, owner);
        });
        // dispensers put critters on heads (ArmorItem.dispenseArmor); none in front: the default
        item::registerDispense(id, [](BlockSource& region, BlockPos const& dispenser, int facing, ItemStack& stack) {
            BlockPos front = relative(dispenser, facing);
            AABB     box{Vec3{static_cast<float>(front.x), static_cast<float>(front.y), static_cast<float>(front.z)},
                     Vec3{front.x + 1.0f, front.y + 1.0f, front.z + 1.0f}};
            for (Actor* a : region.fetchEntities(static_cast<Actor const*>(nullptr), box, false, false)) {
                if (!a || !a->isAlive() || !a->hasType(ActorType::Mob)) continue;
                if (!game::items::isEmpty(a->getArmor(SharedTypes::Legacy::ArmorSlot::Head))) continue;
                a->setArmor(SharedTypes::Legacy::ArmorSlot::Head, game::items::withCount(stack, 1));
                stack.remove(1);
                if (a->isPlayer()) static_cast<Player*>(a)->sendInventory(false);
                return true;
            }
            return false;
        });
    }
    // CritterBlock#useItemOn: an empty mason jar catches a firefly or a cicada (keeping its lid)
    for (std::string const& id : {std::string(ids::Firefly), std::string(ids::Cicada)}) {
        ia::onUseOn({.block = id, .item = ids::MasonJar}, [](ia::UseOnContext const& c) {
            if (!emptyMasonJar(c.item)) return ia::Result::Pass;
            if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
            auto&    region = c.player.getDimensionBlockSource();
            BlockPos pos    = game::blockPos(c.pos);
            auto     jar    = makeItem(c.blockId == ids::Firefly ? ids::FireflyJar : ids::CicadaJar);
            if (!jar) return ia::Result::Pass;
            auto comps = game::items::getData(c.item, "components");
            if (comps.is_object() && comps.contains("gloamingforest:jar_lid")) {
                game::items::setData(*jar, "components", {{"gloamingforest:jar_lid", comps["gloamingforest:jar_lid"]}});
            }
            consumeHeld(c.player, c.item); // ItemStack#consume: kept in creative
            give(c.player, *jar);
            removeBlock(region, pos);
            log().info("flora: {} caught a {} in a mason jar", c.player.getRealName(), c.blockId.substr(15));
            return ia::Result::Consume;
        }, ia::priority::High);
    }
}

} // namespace gf::feature::flora
