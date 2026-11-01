// Block families (F11): buttons (Java ButtonBlock) and pressure plates (PressurePlateBlock).
//
// Pressed buttons and plates power vanilla redstone through their BP permutations
// (minecraft:redstone_producer: 15, strongly the block they hang on; tools/families). This file
// keeps the state: a button stays pressed for params.ticks (TF's wooden buttons: 30 ticks), arrows
// keep a wooden button pressed (params.arrows), a plate is pressed while an entity is on it
// (params.sensitivity "everything" / "mobs") and checked again every 20 ticks; both break when the
// block they hang on is gone.

#include <cmath>

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/common/Interactions.h"
#include "feature/families/BlockEvents.h"
#include "feature/families/Families.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::families {

namespace ia = interactions;

namespace {

std::vector<ia::HandlerId>& handlerIds() {
    static std::vector<ia::HandlerId> ids;
    return ids;
}

/// The face of the block a button / plate hangs on (FacingID from the button).
int attachedFace(Block const& b, Family family) {
    if (family == Family::PressurePlate) return Down;
    auto face = dirOf(strState(b, state::BlockFace)).value_or(Up);
    return opposite(face); // up (floor) hangs on the block below, ...
}

void clickSound(BlockSource& region, BlockPos const& pos, Entry const& e, bool on) {
    bool        wood = e.str("wood_type", "oak") != "stone";
    std::string name = e.family == Family::Button ? (on ? "click_on." : "click_off.") + std::string(wood ? "wooden_button" : "stone_button")
                                                  : (on ? "click_on." : "click_off.") + std::string(wood ? "wooden_pressure_plate" : "stone_pressure_plate");
    bedrockSound(region, pos, name, 0.3f, on ? 0.6f : 0.5f);
}

bool isArrow(Actor const& a) {
    std::string const& id = a.getTypeName();
    return id.find("arrow") != std::string::npos || id == "minecraft:thrown_trident";
}

/// Java ButtonBlock.checkPressed: arrows in the button keep it pressed.
void checkButton(GlobalPos const& gp) {
    auto*        region = regionOf(gp.dim);
    BlockPos     pos{gp.x, gp.y, gp.z};
    Block const* b = region ? blockAt(*region, pos) : nullptr;
    auto const*  e = b ? entryOf(*b) : nullptr;
    if (!e || e->family != Family::Button) return;
    bool arrow = false;
    if (e->flag("arrows", e->str("wood_type", "oak") != "stone")) {
        AABB box{Vec3{float(pos.x), float(pos.y), float(pos.z)}, Vec3{pos.x + 1.f, pos.y + 1.f, pos.z + 1.f}};
        for (auto* a : entitiesIn(*region, box))
            if (a && isArrow(*a)) arrow = true;
    }
    bool powered = intState(*b, state::Powered).value_or(0) != 0;
    if (arrow != powered) {
        setStates(gp, {{state::Powered, arrow ? 1 : 0}});
        clickSound(*region, pos, *e, arrow);
        log().info("families: {} at {} {} {} {}", e->id, gp.x, gp.y, gp.z, arrow ? "pressed by an arrow" : "released");
    }
    if (arrow) schedule(e->integer("ticks", 30), [gp] { checkButton(gp); });
}

/// Java PressurePlateBlock.getSignalStrength: 15 while an entity (or a mob) touches the plate.
int plateSignal(BlockSource& region, BlockPos const& pos, Entry const& e) {
    AABB box{Vec3{pos.x + 1 / 16.f, float(pos.y), pos.z + 1 / 16.f}, Vec3{pos.x + 15 / 16.f, pos.y + 4 / 16.f, pos.z + 15 / 16.f}};
    bool mobsOnly = e.str("sensitivity", "everything") == "mobs";
    for (auto* a : entitiesIn(region, box)) {
        if (!a || !a->isAlive()) continue;
        if (a->isPlayer() && static_cast<Player*>(a)->isSpectator()) continue;
        if (mobsOnly && !a->hasCategory(ActorCategory::Mob)) continue;
        return 15;
    }
    return 0;
}

/// Java BasePressurePlateBlock.checkPressed.
void checkPlate(GlobalPos const& gp) {
    auto*        region = regionOf(gp.dim);
    BlockPos     pos{gp.x, gp.y, gp.z};
    Block const* b = region ? blockAt(*region, pos) : nullptr;
    auto const*  e = b ? entryOf(*b) : nullptr;
    if (!e || e->family != Family::PressurePlate) return;
    int  signal  = plateSignal(*region, pos, *e);
    bool was     = intState(*b, state::Powered).value_or(0) != 0;
    bool now     = signal > 0;
    if (was != now) {
        setStates(gp, {{state::Powered, now ? 1 : 0}});
        clickSound(*region, pos, *e, now);
        log().info("families: {} at {} {} {} {}", e->id, gp.x, gp.y, gp.z, now ? "pressed" : "released");
    }
    if (now) schedule(20, [gp] { checkPlate(gp); }); // Java getPressedTime
}

/// Breaks a button / plate whose support is gone (Java canSurvive in updateShape).
void checkSupport(BlockSource& region, BlockPos const& pos) {
    Block const* b = blockAt(region, pos);
    auto const*  e = b ? entryOf(*b) : nullptr;
    if (!e) return;
    int      face    = attachedFace(*b, e->family);
    BlockPos support = offset(pos, face);
    bool     ok      = e->family == Family::PressurePlate ? sturdy(region, support, Up) || solidBlock(region, support)
                                                          : sturdy(region, support, opposite(face));
    Block const* s = blockAt(region, support);
    if (ok || !s) return;
    game::clearBlock(region, pos);
    drop(region, pos, makeItem(e->id));
}

} // namespace

bool pressButton(GlobalPos const& gp) {
    auto*        region = regionOf(gp.dim);
    BlockPos     pos{gp.x, gp.y, gp.z};
    Block const* b = region ? blockAt(*region, pos) : nullptr;
    auto const*  e = b ? entryOf(*b) : nullptr;
    if (!e || e->family != Family::Button) return false;
    if (intState(*b, state::Powered).value_or(0) != 0) return false;
    setStates(gp, {{state::Powered, 1}});
    clickSound(*region, pos, *e, true);
    // Java ButtonBlock.tick after ticksToStayPressed
    schedule(e->integer("ticks", 30), [gp] {
        auto const* now = blockAt(gp);
        auto const* ne  = now ? entryOf(*now) : nullptr;
        if (!ne || ne->family != Family::Button || intState(*now, state::Powered).value_or(0) == 0) return;
        checkButton(gp);
    });
    log().info("families: {} at {} {} {} pressed", e->id, gp.x, gp.y, gp.z);
    return true;
}

void installSwitches() {
    for (auto const* e : entriesOf(Family::Button)) {
        ia::Match m;
        m.block = e->id;
        handlerIds().push_back(ia::onUseOn(m, [](ia::UseOnContext const& c) {
            if (c.sneaking && !c.itemId.empty()) return ia::Result::Pass;
            if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
            pressButton(c.pos);
            return ia::Result::Consume; // Java: a pressed button consumes the click too
        }));
    }
    static bool registered = false;
    if (registered) return;
    registered = true;
    for (auto const* e : entriesOf(Family::Button)) {
        blockevents::onEntityInside(e->id, [](BlockSource& region, BlockPos const& pos, Actor& entity) {
            if (!isArrow(entity)) return;
            Block const* b = blockAt(region, pos);
            if (b && intState(*b, state::Powered).value_or(0) == 0) checkButton(game::globalPos(region, pos));
        });
        blockevents::onNeighborChanged(e->id, [](BlockSource& region, BlockPos const& pos, BlockPos const&) { checkSupport(region, pos); });
    }
    for (auto const* e : entriesOf(Family::PressurePlate)) {
        blockevents::onEntityInside(e->id, [](BlockSource& region, BlockPos const& pos, Actor&) {
            Block const* b = blockAt(region, pos);
            if (b && intState(*b, state::Powered).value_or(0) == 0) checkPlate(game::globalPos(region, pos));
        });
        blockevents::onNeighborChanged(e->id, [](BlockSource& region, BlockPos const& pos, BlockPos const&) { checkSupport(region, pos); });
        // a plate saved while pressed rechecks when it is loaded and ticked
        blockevents::onPlaced(e->id, [](BlockSource& region, BlockPos const& pos, Block const& b, Actor*) {
            if (intState(b, state::Powered).value_or(0) != 0) schedule(20, [gp = game::globalPos(region, pos)] { checkPlate(gp); });
        });
    }
}

void uninstallSwitches() {
    for (auto id : handlerIds()) ia::remove(id);
    handlerIds().clear();
}

} // namespace gf::feature::families
