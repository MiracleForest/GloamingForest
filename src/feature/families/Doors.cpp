// Adapted from LiteLDev/Levitate v0.1.0 (src/feature/building/Doors.cpp), LGPL-3.0-or-later.
//
// Block families (F11): doors (Java DoorBlock), trapdoors (TrapDoorBlock) and fence gates
// (FenceGateBlock): placement (facing, hinge, the door's upper half), use, redstone, the two door
// halves kept together and a door without support popping off (canSurvive / updateShape).
//
// Redstone: custom blocks are not told when a conducting block next to them is powered, so every
// door, trapdoor and gate is watched through gf::redstone (Java's neighborChanged sees the same
// signal): armed when placed and when its chunk ticks for the first time after loading (blocks
// from structures and saved worlds), dropped when the block is gone.

#include <map>
#include <set>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/chunk/LevelChunk.h"

#include "feature/common/Interactions.h"
#include "feature/common/Redstone.h"
#include "feature/families/BlockEvents.h"
#include "feature/families/Families.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::families {

namespace ia = interactions;

namespace detail {
void onChunkLoaded(std::function<void(BlockSource&, LevelChunk&)> fn);
void forEachInChunk(BlockSource& region, LevelChunk& chunk, std::function<bool(BlockType const&)> const& want, std::function<void(BlockPos const&, Block const&)> const& fn);
} // namespace detail

namespace {

Entry const* familyAt(GlobalPos const& p, Family family) {
    auto const* b = blockAt(p);
    auto const* e = b ? entryOf(*b) : nullptr;
    return e && e->family == family ? e : nullptr;
}

bool redstoneFamily(Entry const* e) { return e && (e->family == Family::Door || e->family == Family::Trapdoor || e->family == Family::FenceGate); }

/// Java BlockSetType / WoodType sounds: TF's wood sets have the wooden door, trapdoor and gate
/// sounds (params.block_set "iron": the iron ones), at volume 1 and pitch nextFloat() * 0.1 + 0.9.
void doorSound(BlockSource& region, BlockPos const& pos, Entry const& e, bool open) {
    bool        iron = e.str("block_set") == "iron";
    std::string name;
    switch (e.family) {
    case Family::Door: name = open ? (iron ? "open.iron_door" : "open.wooden_door") : (iron ? "close.iron_door" : "close.wooden_door"); break;
    case Family::Trapdoor: name = open ? (iron ? "open.iron_trapdoor" : "open.wooden_trapdoor") : (iron ? "close.iron_trapdoor" : "close.wooden_trapdoor"); break;
    default: name = open ? "open.fence_gate" : "close.fence_gate"; break;
    }
    bedrockSound(region, pos, name, 1.0f, wobblePitch());
}

bool upperHalf(Block const& b) { return strState(b, state::Half) == "upper"; }

GlobalPos lowerHalf(GlobalPos const& p, Block const& b) { return upperHalf(b) ? offset(p, Down) : p; }

/// Sets states on both halves of the door whose lower half is at `lower`.
void setDoor(GlobalPos const& lower, Json const& states) {
    for (auto const& p : {lower, offset(lower, Up)}) {
        if (familyAt(p, Family::Door)) setStates(p, states);
    }
}

// ---- redstone ---------------------------------------------------------------------------------------

std::map<GlobalPos, std::vector<redstone::WatchId>>& armed() {
    static std::map<GlobalPos, std::vector<redstone::WatchId>> m;
    return m;
}

void disarm(GlobalPos const& p) {
    auto it = armed().find(p);
    if (it == armed().end()) return;
    for (auto id : it->second) redstone::unwatch(id);
    armed().erase(it);
}

/// Java DoorBlock / TrapDoorBlock / FenceGateBlock.neighborChanged: open while powered.
void redstoneChanged(GlobalPos const& p) {
    auto* region = regionOf(p.dim);
    if (!region) return;
    BlockPos     bp{p.x, p.y, p.z};
    Block const* b = blockAt(*region, bp);
    if (!b) return; // not loaded: kept
    auto const* e = entryOf(*b);
    if (!redstoneFamily(e) || (e->family == Family::Door && upperHalf(*b))) {
        defer([p] { disarm(p); }); // the block is gone (not inside the redstone poll)
        return;
    }
    bool powered = hasNeighborSignal(*region, bp);
    if (e->family == Family::Door) powered = powered || hasNeighborSignal(*region, offset(bp, Up));
    if (powered == flagState(*b, state::Powered)) return;
    bool wasOpen = flagState(*b, state::Open);
    Json s{{state::Powered, powered}, {state::Open, powered}};
    if (e->family == Family::Door) setDoor(p, s);
    else setStates(p, s);
    if (powered != wasOpen) {
        doorSound(*region, bp, *e, powered);
        log().info("families: {} at {} {} {} {} by redstone", e->id, p.x, p.y, p.z, powered ? "opened" : "closed");
    }
}

/// Watches the door (both halves' positions), trapdoor or gate whose (lower) block is at `p`.
void arm(GlobalPos const& p) {
    if (armed().contains(p)) return;
    auto const* b = blockAt(p);
    auto const* e = b ? entryOf(*b) : nullptr;
    if (!redstoneFamily(e)) return;
    if (e->family == Family::Door && upperHalf(*b)) return arm(offset(p, Down));
    auto& ids = armed()[p];
    ids.push_back(redstone::watch(p, [p](GlobalPos const&, int) { redstoneChanged(p); }));
    if (e->family == Family::Door) ids.push_back(redstone::watch(offset(p, Up), [p](GlobalPos const&, int) { redstoneChanged(p); }));
}

ia::Result useOn(ia::UseOnContext const& c) {
    if (c.sneaking && !c.itemId.empty()) return ia::Result::Pass; // Java: sneaking with an item uses the item
    if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
    auto const* e = entryOf(c.block);
    if (!e) return ia::Result::Pass;
    bool done = false;
    switch (e->family) {
    case Family::Door: done = useDoor(c.pos, &c.player); break;
    case Family::Trapdoor: done = useTrapdoor(c.pos, &c.player); break;
    case Family::FenceGate: done = useFenceGate(c.pos, &c.player); break;
    default: break;
    }
    return done ? ia::Result::Consume : ia::Result::Pass;
}

/// Java DoorBlock.getHinge for a door at `pos` facing `facing`, clicked at (hitX, hitZ) (absolute).
std::string doorHinge(BlockSource& region, BlockPos const& pos, std::string const& id, int facing, double hitX, double hitZ) {
    BlockPos above = offset(pos, Up);
    int      left = counterClockwise(facing), right = clockwise(facing);
    auto     full = [&](BlockPos const& q) { return solidBlock(region, q); };
    int i = (full(offset(pos, left)) ? -1 : 0) + (full(offset(above, left)) ? -1 : 0) + (full(offset(pos, right)) ? 1 : 0) + (full(offset(above, right)) ? 1 : 0);
    auto doorAt = [&](BlockPos const& q) {
        Block const* d = blockAt(region, q);
        return d && d->getTypeName() == id && !upperHalf(*d);
    };
    bool leftDoor = doorAt(offset(pos, left)), rightDoor = doorAt(offset(pos, right));
    if ((!leftDoor || rightDoor) && i <= 0) {
        if ((!rightDoor || leftDoor) && i >= 0) {
            int    j = kDx[facing], k = kDz[facing];
            double d0 = hitX - pos.x, d1 = hitZ - pos.z;
            bool   isLeft = (j >= 0 || !(d1 < 0.5)) && (j <= 0 || !(d1 > 0.5)) && (k >= 0 || !(d0 > 0.5)) && (k <= 0 || !(d0 < 0.5));
            return isLeft ? "left" : "right";
        }
        return "left";
    }
    return "right";
}

/// Java DoorBlock.canSurvive for the lower half at `pos`.
bool doorCanSurvive(BlockSource& region, BlockPos const& pos) { return sturdy(region, offset(pos, Down), Up); }

std::vector<ia::HandlerId>& handlerIds() {
    static std::vector<ia::HandlerId> ids;
    return ids;
}

} // namespace

bool useDoor(GlobalPos const& pos, Player* player) {
    auto const* b = blockAt(pos);
    auto const* e = b ? entryOf(*b) : nullptr;
    if (!e || e->family != Family::Door) return false;
    if (!e->flag("open_by_hand", e->str("block_set") != "iron")) return false; // Java BlockSetType.canOpenByHand
    auto        lower = lowerHalf(pos, *b);
    auto const* lb    = blockAt(lower);
    if (!lb || !familyAt(lower, Family::Door)) return false;
    bool open = !flagState(*lb, state::Open);
    setDoor(lower, {{state::Open, open}});
    if (auto* region = regionOf(pos.dim)) doorSound(*region, BlockPos{lower.x, lower.y, lower.z}, *e, open);
    log().info("families: {} at {} {} {} {}{}", e->id, lower.x, lower.y, lower.z, open ? "opened" : "closed", player ? " by " + player->getRealName() : std::string());
    return true;
}

bool useTrapdoor(GlobalPos const& pos, Player* player) {
    auto const* b = blockAt(pos);
    auto const* e = b ? entryOf(*b) : nullptr;
    if (!e || e->family != Family::Trapdoor || !e->flag("open_by_hand", e->str("block_set") != "iron")) return false;
    bool open = !flagState(*b, state::Open);
    setStates(pos, {{state::Open, open}});
    if (auto* region = regionOf(pos.dim)) doorSound(*region, BlockPos{pos.x, pos.y, pos.z}, *e, open);
    log().info("families: {} at {} {} {} {}{}", e->id, pos.x, pos.y, pos.z, open ? "opened" : "closed", player ? " by " + player->getRealName() : std::string());
    return true;
}

bool useFenceGate(GlobalPos const& pos, Player* player) {
    auto const* b = blockAt(pos);
    auto const* e = b ? entryOf(*b) : nullptr;
    if (!e || e->family != Family::FenceGate) return false;
    // Java FenceGateBlock.useWithoutItem: an opening gate swings away from the player
    Json s    = Json::object();
    bool open = flagState(*b, state::Open);
    if (open) {
        s[state::Open] = false;
    } else {
        if (player) {
            int d = lookDir(*player);
            if (facingOf(*b) == opposite(d)) s[state::Facing] = dirName(d);
        }
        s[state::Open] = true;
    }
    setStates(pos, s);
    if (auto* region = regionOf(pos.dim)) doorSound(*region, BlockPos{pos.x, pos.y, pos.z}, *e, !open);
    log().info("families: {} at {} {} {} {}{}", e->id, pos.x, pos.y, pos.z, open ? "closed" : "opened", player ? " by " + player->getRealName() : std::string());
    return true;
}

bool placeDoor(GlobalPos const& pos, Actor* placer, double hitX, double hitZ) {
    auto*       region = regionOf(pos.dim);
    auto const* b      = blockAt(pos);
    auto const* e      = b ? entryOf(*b) : nullptr;
    if (!region || !e || e->family != Family::Door) return false;
    BlockPos bp{pos.x, pos.y, pos.z};
    auto     upper = offset(pos, Up);
    // Java getStateForPlacement: the upper half must fit; BlockItem: the door must survive
    if (pos.y >= region->getMaxHeight() - 1 || !replaceableAt(upper) || !doorCanSurvive(*region, bp)) {
        clear(pos);
        if (placer && placer->isPlayer()) {
            auto& player = static_cast<Player&>(*placer);
            if (!player.isCreative()) give(player, makeItem(e->id));
        }
        return false;
    }
    int  facing = placer ? lookDir(*placer) : facingOf(*b);
    auto hinge  = doorHinge(*region, bp, e->id, facing, hitX, hitZ);
    bool power  = hasNeighborSignal(*region, bp) || hasNeighborSignal(*region, offset(bp, Up));
    Json s{{state::Facing, dirName(facing)}, {state::Hinge, hinge}, {state::Half, "lower"}, {state::Open, power}, {state::Powered, power}};
    auto const* lower = withStates(*b, s);
    if (!lower) return false;
    set(pos, *lower);
    // Java DoorBlock.setPlacedBy: the upper half
    if (auto const* up = withStates(*lower, {{state::Half, "upper"}})) set(upper, *up);
    arm(pos);
    log().info("families: {} placed at {} {} {} (facing {}, hinge {})", e->id, pos.x, pos.y, pos.z, dirName(facing), hinge);
    return true;
}

namespace detail {
void clearDoors() { armed().clear(); } // the watches go with the world
} // namespace detail

void installDoors() {
    for (auto family : {Family::Door, Family::Trapdoor, Family::FenceGate}) {
        for (auto const* e : entriesOf(family)) {
            ia::Match m;
            m.block = e->id;
            handlerIds().push_back(ia::onUseOn(m, useOn));
        }
    }
    static bool registered = false;
    if (registered) return;
    registered = true;
    // structures and saved worlds: the blocks of a chunk that starts ticking
    detail::onChunkLoaded([](BlockSource& region, LevelChunk& chunk) {
        detail::forEachInChunk(region, chunk, [](BlockType const& t) { return redstoneFamily(entryOf(t)); },
                               [&region](BlockPos const& p, Block const&) { arm(game::globalPos(region, p)); });
    });
    for (auto family : {Family::Door, Family::Trapdoor, Family::FenceGate}) {
        for (auto const* e : entriesOf(family)) {
            // watched for redstone wherever they come from (players, structures, commands)
            blockevents::onPlaced(e->id, [](BlockSource& region, BlockPos const& pos, Block const&, Actor*) { arm(game::globalPos(region, pos)); });
            if (family != Family::Door) continue;
            // Java DoorBlock.updateShape
            blockevents::onNeighborChanged(e->id, [id = e->id](BlockSource& region, BlockPos const& pos, BlockPos const& from) {
                Block const* b = blockAt(region, pos);
                if (!b || b->getTypeName() != id) return;
                bool     upper = upperHalf(*b);
                BlockPos other = offset(pos, upper ? Down : Up);
                if (from.x == other.x && from.y == other.y && from.z == other.z) {
                    Block const* o = blockAt(region, other);
                    if (!o) return;
                    if (o->getTypeName() == id && upperHalf(*o) != upper) {
                        // the other half changed: this one takes its states (its own half)
                        Json s{{state::Open, flagState(*o, state::Open)}, {state::Powered, flagState(*o, state::Powered)},
                               {state::Hinge, strState(*o, state::Hinge)}, {state::Facing, strState(*o, state::Facing)}};
                        if (auto const* n = withStates(*b, s); n && n != b) game::setBlock(region, pos, *n);
                        return;
                    }
                    // the other half is gone: this one goes too (the upper half drops nothing:
                    // the door's loot comes from the lower half)
                    breakEffect(region, pos, *b);
                    if (upper) game::clearBlock(region, pos);
                    else destroyWithDrops(region, pos);
                    return;
                }
                // DoorBlock.canSurvive: the lower half needs a sturdy block below
                BlockPos below = offset(pos, Down);
                if (!upper && from.x == below.x && from.y == below.y && from.z == below.z && !doorCanSurvive(region, pos)) {
                    log().info("families: {} at {} {} {} lost its support", id, pos.x, pos.y, pos.z);
                    destroyWithDrops(region, pos);
                }
            });
        }
    }
}

void uninstallDoors() {
    for (auto id : handlerIds()) ia::remove(id);
    handlerIds().clear();
    for (auto& [p, ids] : armed())
        for (auto id : ids) redstone::unwatch(id);
    armed().clear();
}

} // namespace gf::feature::families
