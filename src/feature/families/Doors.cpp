// Adapted from LiteLDev/Levitate v0.1.0 (src/feature/building/Doors.cpp), LGPL-3.0-or-later.
//
// Block families (F11): doors (Java DoorBlock), trapdoors (TrapDoorBlock) and fence gates
// (FenceGateBlock): placement (facing, hinge, the door's upper half), use, redstone and the two
// door halves kept together.
//
// Redstone: custom blocks are not told when a conducting block next to them is powered, so the
// family blocks that react to redstone are tracked (placed by anyone, persisted per world in
// gf:families:redstone) and their input polled every 2 ticks, as Java's neighborChanged would see it.

#include <set>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include "feature/common/Interactions.h"
#include "feature/families/BlockEvents.h"
#include "feature/families/Families.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::families {

namespace ia = interactions;

namespace {

constexpr char kDbRedstone[] = "gf:families:redstone";

std::set<GlobalPos>& tracked() {
    static std::set<GlobalPos> s;
    return s;
}
bool& trackedDirty() {
    static bool d = false;
    return d;
}

Entry const* familyAt(GlobalPos const& p, Family family) {
    auto const* b = blockAt(p);
    auto const* e = b ? entryOf(*b) : nullptr;
    return e && e->family == family ? e : nullptr;
}

int facingOf(Block const& b) { return dirOf(strState(b, state::Cardinal)).value_or(North); }

/// The open / close sound of a door, trapdoor or gate (Java BlockSetType / WoodType sounds).
void doorSound(BlockSource& region, BlockPos const& pos, Family family, bool open) {
    char const* name = nullptr;
    switch (family) {
    case Family::Door: name = open ? "random.door_open" : "random.door_close"; break;
    case Family::Trapdoor: name = open ? "random.door_open" : "random.door_close"; break;
    default: name = open ? "random.door_open" : "random.door_close"; break;
    }
    bedrockSound(region, pos, name, 1.0f, family == Family::Trapdoor ? 1.1f : 1.0f);
}

GlobalPos lowerHalf(GlobalPos const& p, Block const& b) { return intState(b, state::Upper).value_or(0) ? offset(p, Down) : p; }

/// Sets states on both halves of the door whose lower half is at `lower`.
void setDoor(GlobalPos const& lower, Json const& states) {
    for (auto const& p : {lower, offset(lower, Up)}) {
        if (familyAt(p, Family::Door)) setStates(p, states);
    }
}

void track(GlobalPos const& pos) {
    if (tracked().insert(pos).second) trackedDirty() = true;
}

/// Java DoorBlock / TrapDoorBlock / FenceGateBlock.neighborChanged: open while powered.
void redstoneTick() {
    std::vector<GlobalPos> gone;
    for (auto const& p : tracked()) {
        auto* region = regionOf(p.dim);
        if (!region) continue;
        BlockPos     bp{p.x, p.y, p.z};
        Block const* b = blockAt(*region, bp);
        if (!b) continue; // not loaded: keep
        auto const* e = entryOf(*b);
        if (!e || (e->family != Family::Door && e->family != Family::Trapdoor && e->family != Family::FenceGate)) {
            gone.push_back(p);
            continue;
        }
        if (e->family == Family::Door && intState(*b, state::Upper).value_or(0) != 0) {
            gone.push_back(p); // the lower half is tracked
            continue;
        }
        bool powered = hasNeighborSignal(*region, bp);
        if (e->family == Family::Door) powered = powered || hasNeighborSignal(*region, offset(bp, Up));
        if (powered == (intState(*b, state::Powered).value_or(0) != 0)) continue;
        bool wasOpen = intState(*b, state::Open).value_or(0) != 0;
        Json s{{state::Powered, powered ? 1 : 0}, {state::Open, powered ? 1 : 0}};
        if (e->family == Family::Door) setDoor(p, s);
        else setStates(p, s);
        if (powered != wasOpen) {
            doorSound(*region, bp, e->family, powered);
            log().info("families: {} at {} {} {} {} by redstone", e->id, p.x, p.y, p.z, powered ? "opened" : "closed");
        }
    }
    for (auto const& p : gone) tracked().erase(p);
    if (!gone.empty()) trackedDirty() = true;
}

ia::Result useOn(ia::UseOnContext const& c) {
    if (c.sneaking && !c.itemId.empty()) return ia::Result::Pass; // Java: sneaking with an item uses the item
    if (!c.firstEvent) return ia::Result::Consume;
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
int doorHinge(BlockSource& region, BlockPos const& pos, std::string const& id, int facing, double hitX, double hitZ) {
    BlockPos above = offset(pos, Up);
    int      left = counterClockwise(facing), right = clockwise(facing);
    auto     full = [&](BlockPos const& q) { return solidBlock(region, q); };
    int i = (full(offset(pos, left)) ? -1 : 0) + (full(offset(above, left)) ? -1 : 0) + (full(offset(pos, right)) ? 1 : 0) + (full(offset(above, right)) ? 1 : 0);
    auto doorAt = [&](BlockPos const& q) {
        Block const* d = blockAt(region, q);
        return d && d->getTypeName() == id && intState(*d, state::Upper).value_or(0) == 0;
    };
    bool leftDoor = doorAt(offset(pos, left)), rightDoor = doorAt(offset(pos, right));
    if ((!leftDoor || rightDoor) && i <= 0) {
        if ((!rightDoor || leftDoor) && i >= 0) {
            int    j = kDx[facing], k = kDz[facing];
            double d0 = hitX - pos.x, d1 = hitZ - pos.z;
            bool   isLeft = (j >= 0 || !(d1 < 0.5)) && (j <= 0 || !(d1 > 0.5)) && (k >= 0 || !(d0 > 0.5)) && (k <= 0 || !(d0 < 0.5));
            return isLeft ? 0 : 1;
        }
        return 0;
    }
    return 1;
}

std::vector<ia::HandlerId>& handlerIds() {
    static std::vector<ia::HandlerId> ids;
    return ids;
}

} // namespace

bool useDoor(GlobalPos const& pos, Player* player) {
    auto const* b = blockAt(pos);
    auto const* e = b ? entryOf(*b) : nullptr;
    if (!e || e->family != Family::Door) return false;
    if (!e->flag("open_by_hand", true)) return false; // Java BlockSetType.canOpenByHand (iron doors)
    auto        lower = lowerHalf(pos, *b);
    auto const* lb    = blockAt(lower);
    if (!lb || !familyAt(lower, Family::Door)) return false;
    int open = intState(*lb, state::Open).value_or(0) ? 0 : 1;
    setDoor(lower, {{state::Open, open}});
    if (auto* region = regionOf(pos.dim)) doorSound(*region, BlockPos{lower.x, lower.y, lower.z}, Family::Door, open != 0);
    log().info("families: {} at {} {} {} {}{}", e->id, lower.x, lower.y, lower.z, open ? "opened" : "closed", player ? " by " + player->getRealName() : std::string());
    return true;
}

bool useTrapdoor(GlobalPos const& pos, Player* player) {
    auto const* b = blockAt(pos);
    auto const* e = b ? entryOf(*b) : nullptr;
    if (!e || e->family != Family::Trapdoor || !e->flag("open_by_hand", true)) return false;
    int open = intState(*b, state::Open).value_or(0) ? 0 : 1;
    setStates(pos, {{state::Open, open}});
    if (auto* region = regionOf(pos.dim)) doorSound(*region, BlockPos{pos.x, pos.y, pos.z}, Family::Trapdoor, open != 0);
    log().info("families: {} at {} {} {} {}{}", e->id, pos.x, pos.y, pos.z, open ? "opened" : "closed", player ? " by " + player->getRealName() : std::string());
    return true;
}

bool useFenceGate(GlobalPos const& pos, Player* player) {
    auto const* b = blockAt(pos);
    auto const* e = b ? entryOf(*b) : nullptr;
    if (!e || e->family != Family::FenceGate) return false;
    // Java FenceGateBlock.useWithoutItem: an opening gate swings away from the player
    Json s = Json::object();
    bool open = intState(*b, state::Open).value_or(0) != 0;
    if (open) {
        s[state::Open] = 0;
    } else {
        if (player) {
            int d = lookDir(*player);
            if (facingOf(*b) == opposite(d)) s[state::Cardinal] = dirName(d);
        }
        s[state::Open] = 1;
    }
    setStates(pos, s);
    if (auto* region = regionOf(pos.dim)) bedrockSound(*region, BlockPos{pos.x, pos.y, pos.z}, open ? "close.fence_gate" : "open.fence_gate");
    log().info("families: {} at {} {} {} {}{}", e->id, pos.x, pos.y, pos.z, open ? "closed" : "opened", player ? " by " + player->getRealName() : std::string());
    return true;
}

bool placeDoor(GlobalPos const& pos, Actor* placer, double hitX, double hitZ) {
    auto* region = regionOf(pos.dim);
    auto const* b = blockAt(pos);
    auto const* e = b ? entryOf(*b) : nullptr;
    if (!region || !e || e->family != Family::Door) return false;
    BlockPos bp{pos.x, pos.y, pos.z};
    auto     upper = offset(pos, Up);
    if (!replaceableAt(upper)) {
        // Java: no placement when the upper half doesn't fit
        clear(pos);
        if (placer && placer->isPlayer()) {
            auto& player = static_cast<Player&>(*placer);
            if (!player.isCreative()) give(player, makeItem(e->id));
        }
        return false;
    }
    int  facing = placer ? lookDir(*placer) : facingOf(*b);
    int  hinge  = doorHinge(*region, bp, e->id, facing, hitX, hitZ);
    bool power  = hasNeighborSignal(*region, bp) || hasNeighborSignal(*region, offset(bp, Up));
    Json s{{state::Cardinal, dirName(facing)}, {state::Hinge, hinge}, {state::Upper, 0}, {state::Open, power ? 1 : 0}, {state::Powered, power ? 1 : 0}};
    auto const* lower = withStates(*b, s);
    if (!lower) return false;
    set(pos, *lower);
    if (auto const* up = withStates(*lower, {{state::Upper, 1}})) set(upper, *up);
    track(pos);
    log().info("families: {} placed at {} {} {} (facing {}, hinge {})", e->id, pos.x, pos.y, pos.z, dirName(facing), hinge ? "right" : "left");
    return true;
}

namespace detail {
void doorsTick(int tick) {
    if (tick % 2 == 0) redstoneTick();
}
void loadDoors() {
    tracked().clear();
    if (auto j = game::db::get(kDbRedstone); j && j->is_array()) {
        for (auto const& e : *j)
            if (e.is_array() && e.size() == 4) tracked().insert({e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()});
    }
    trackedDirty() = false;
}
void saveDoors() {
    if (!trackedDirty() || !game::db::available()) return;
    Json j = Json::array();
    for (auto const& p : tracked()) j.push_back({p.dim, p.x, p.y, p.z});
    game::db::put(kDbRedstone, j);
    trackedDirty() = false;
}
void clearDoors() { tracked().clear(); }
} // namespace detail

void installDoors() {
    for (auto family : {Family::Door, Family::Trapdoor, Family::FenceGate}) {
        for (auto const* e : entriesOf(family)) {
            ia::Match m;
            m.block = e->id;
            handlerIds().push_back(ia::onUseOn(m, useOn));
            // placement by a player: Java getStateForPlacement / setPlacedBy
            handlerIds().push_back(ia::onPlace(m, [family](ia::PlaceContext const& c) {
                double hx = c.clicked.x + c.hit[0], hz = c.clicked.z + c.hit[2];
                if (family == Family::Door) {
                    placeDoor(c.pos, &c.player, hx, hz);
                } else if (family == Family::Trapdoor) {
                    // facing: the clicked face when horizontal, else away from the player
                    int facing = horizontal(c.face) ? c.face : opposite(lookDir(c.player));
                    auto* region = regionOf(c.pos.dim);
                    bool power = region && hasNeighborSignal(*region, game::blockPos(c.pos));
                    setStates(c.pos, {{state::Cardinal, dirName(facing)}, {state::Open, power ? 1 : 0}, {state::Powered, power ? 1 : 0}});
                } else {
                    auto* region = regionOf(c.pos.dim);
                    bool power = region && hasNeighborSignal(*region, game::blockPos(c.pos));
                    setStates(c.pos, {{state::Cardinal, dirName(lookDir(c.player))}, {state::Open, power ? 1 : 0}, {state::Powered, power ? 1 : 0}});
                }
            }));
        }
    }
    static bool registered = false;
    if (registered) return;
    registered = true;
    for (auto family : {Family::Door, Family::Trapdoor, Family::FenceGate}) {
        for (auto const* e : entriesOf(family)) {
            // tracked for redstone wherever they come from (players, structures, commands)
            blockevents::onPlaced(e->id, [](BlockSource& region, BlockPos const& pos, Block const&, Actor*) { track(game::globalPos(region, pos)); });
            if (family != Family::Door) continue;
            // Java DoorBlock.updateShape: a half without its other half breaks
            blockevents::onNeighborChanged(e->id, [id = e->id](BlockSource& region, BlockPos const& pos, BlockPos const& from) {
                Block const* b = blockAt(region, pos);
                if (!b || b->getTypeName() != id) return;
                bool upper = intState(*b, state::Upper).value_or(0) != 0;
                BlockPos other = offset(pos, upper ? Down : Up);
                if (from.x != other.x || from.y != other.y || from.z != other.z) return;
                Block const* o = blockAt(region, other);
                if (!o) return;
                if (o->getTypeName() == id && (intState(*o, state::Upper).value_or(0) != 0) != upper) {
                    // the other half changed: copy its open / powered / hinge / facing
                    Json s{{state::Open, intState(*o, state::Open).value_or(0)}, {state::Powered, intState(*o, state::Powered).value_or(0)},
                           {state::Hinge, intState(*o, state::Hinge).value_or(0)}, {state::Cardinal, strState(*o, state::Cardinal)}};
                    if (auto const* n = withStates(*b, s); n && n != b) game::setBlock(region, pos, *n);
                    return;
                }
                // the other half is gone: this one goes too, without drops (the broken half
                // dropped the door; Java turns the other half into air in updateShape)
                game::clearBlock(region, pos);
            });
        }
    }
}

void uninstallDoors() {
    for (auto id : handlerIds()) ia::remove(id);
    handlerIds().clear();
}

} // namespace gf::feature::families
