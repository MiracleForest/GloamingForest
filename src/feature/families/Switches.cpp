// Block families (F11): buttons (Java ButtonBlock) and pressure plates (PressurePlateBlock).
//
// Pressed buttons and plates power vanilla redstone through their BP permutations
// (minecraft:redstone_producer: 15, strongly the block they hang on; tools/families). This file
// keeps the state: a button stays pressed for params.ticks (upstream's wooden buttons: 30 ticks), arrows
// keep a wooden button pressed, a plate is pressed while an entity is on it (params.sensitivity
// "everything" / "mobs") and checked again every 20 ticks; both break when the block they hang on
// is gone (canSurvive). Java keeps the scheduled release with the chunk: here a switch saved while
// pressed is checked again when its chunk ticks for the first time after loading.

#include <cmath>

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/phys/AABB.h"

#include "feature/common/Interactions.h"
#include "feature/families/BlockEvents.h"
#include "feature/families/Families.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::families {

namespace ia = interactions;

namespace detail {
void onChunkLoaded(std::function<void(BlockSource&, LevelChunk&)> fn);
void forEachInChunk(BlockSource& region, LevelChunk& chunk, std::function<bool(BlockType const&)> const& want, std::function<void(BlockPos const&, Block const&)> const& fn);
bool buttonCanSurvive(BlockSource& region, BlockPos const& pos, std::string const& face, int facing);
bool plateCanSurvive(BlockSource& region, BlockPos const& pos);
} // namespace detail

namespace {

std::vector<ia::HandlerId>& handlerIds() {
    static std::vector<ia::HandlerId> ids;
    return ids;
}

/// The face of the block a button / plate hangs on (FaceAttachedHorizontalDirectionalBlock
/// getConnectedDirection(state).getOpposite()).
int attachedFace(Block const& b, Family family) {
    if (family == Family::PressurePlate) return Down;
    auto face = strState(b, state::Face);
    if (face == "floor") return Down;
    if (face == "ceiling") return Up;
    return opposite(facingOf(b));
}

/// Java BlockSetType sounds: upstream's wood sets click as wooden buttons and plates (volume and pitch 1).
void clickSound(BlockSource& region, BlockPos const& pos, Entry const& e, bool on) {
    auto        set = e.str("block_set", "wood");
    std::string name;
    if (e.family == Family::Button) {
        name = set == "stone" || set == "iron" || set == "gold" ? "random.click" : "random.wood_click";
    } else {
        std::string kind = set == "stone" ? "stone" : (set == "iron" || set == "gold" ? "metal" : "wooden");
        name             = std::string(on ? "click_on." : "click_off.") + kind + "_pressure_plate";
    }
    bedrockSound(region, pos, name, 1.0f, 1.0f);
}

bool isArrow(Actor const& a) {
    std::string const& id = a.getTypeName();
    return id.find("arrow") != std::string::npos || id == "minecraft:thrown_trident";
}

/// Java ButtonBlock shapes (block pixels: x0 y0 z0 x1 y1 z1).
std::array<float, 6> buttonShape(Block const& b) {
    float d    = flagState(b, state::Powered) ? 1.f : 2.f;
    auto  face = strState(b, state::Face);
    int   f    = facingOf(b);
    bool  x    = f == East || f == West;
    if (face == "floor") return x ? std::array<float, 6>{6, 0, 5, 10, d, 11} : std::array<float, 6>{5, 0, 6, 11, d, 10};
    if (face == "ceiling") return x ? std::array<float, 6>{6, 16 - d, 5, 10, 16, 11} : std::array<float, 6>{5, 16 - d, 6, 11, 16, 10};
    switch (f) {
    case North: return {5, 6, 16 - d, 11, 10, 16};
    case South: return {5, 6, 0, 11, 10, d};
    case West: return {16 - d, 6, 5, 16, 10, 11};
    default: return {0, 6, 5, d, 10, 11};
    }
}

AABB boxAt(BlockPos const& pos, std::array<float, 6> const& b) {
    return AABB{Vec3{pos.x + b[0] / 16.f, pos.y + b[1] / 16.f, pos.z + b[2] / 16.f}, Vec3{pos.x + b[3] / 16.f, pos.y + b[4] / 16.f, pos.z + b[5] / 16.f}};
}

int buttonTicks(Entry const& e) { return e.integer("ticks", e.str("block_set", "wood") == "stone" ? 20 : 30); }
bool arrowsPress(Entry const& e) { return e.flag("arrows", e.str("block_set", "wood") != "stone" && e.str("block_set", "wood") != "iron" && e.str("block_set", "wood") != "gold"); }

/// Java ButtonBlock.checkPressed: arrows in the button's shape keep it pressed.
void checkButton(GlobalPos const& gp) {
    auto*        region = regionOf(gp.dim);
    BlockPos     pos{gp.x, gp.y, gp.z};
    Block const* b = region ? blockAt(*region, pos) : nullptr;
    auto const*  e = b ? entryOf(*b) : nullptr;
    if (!e || e->family != Family::Button) return;
    bool arrow = false;
    if (arrowsPress(*e)) {
        for (auto* a : entitiesIn(*region, boxAt(pos, buttonShape(*b))))
            if (a && isArrow(*a)) arrow = true;
    }
    bool powered = flagState(*b, state::Powered);
    if (arrow != powered) {
        setStates(gp, {{state::Powered, arrow}});
        clickSound(*region, pos, *e, arrow);
        log().info("families: {} at {} {} {} {}", e->id, gp.x, gp.y, gp.z, arrow ? "pressed by an arrow" : "released");
    }
    if (arrow) schedule(buttonTicks(*e), [gp] { checkButton(gp); });
}

/// Java PressurePlateBlock.getSignalStrength: 15 while an entity (or a mob) touches the plate.
int plateSignal(BlockSource& region, BlockPos const& pos, Entry const& e) {
    // TOUCH_AABB = (1/16, 0, 1/16) .. (15/16, 0.25, 15/16)
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
    int  signal = plateSignal(*region, pos, *e);
    bool was    = flagState(*b, state::Powered);
    bool now    = signal > 0;
    if (was != now) {
        setStates(gp, {{state::Powered, now}});
        clickSound(*region, pos, *e, now);
        log().info("families: {} at {} {} {} {}", e->id, gp.x, gp.y, gp.z, now ? "pressed" : "released");
    }
    if (now) schedule(20, [gp] { checkPlate(gp); }); // Java getPressedTime
}

/// Breaks a button / plate whose support is gone (Java updateShape: canSurvive, then AIR with drops).
void checkSupport(BlockSource& region, BlockPos const& pos, BlockPos const& from) {
    Block const* b = blockAt(region, pos);
    auto const*  e = b ? entryOf(*b) : nullptr;
    if (!e) return;
    int      face    = attachedFace(*b, e->family);
    BlockPos support = offset(pos, face);
    if (from.x != support.x || from.y != support.y || from.z != support.z) return;
    bool ok = e->family == Family::PressurePlate ? detail::plateCanSurvive(region, pos)
                                                  : detail::buttonCanSurvive(region, pos, strState(*b, state::Face), facingOf(*b));
    if (ok || !blockAt(region, support)) return;
    log().info("families: {} at {} {} {} lost its support", e->id, pos.x, pos.y, pos.z);
    destroyWithDrops(region, pos);
}

} // namespace

namespace detail {
/// Java FaceAttachedHorizontalDirectionalBlock.canSurvive: the block it hangs on has a sturdy face.
bool buttonCanSurvive(BlockSource& region, BlockPos const& pos, std::string const& face, int facing) {
    int towards = face == "floor" ? Down : (face == "ceiling" ? Up : opposite(facing));
    return sturdy(region, offset(pos, towards), opposite(towards));
}
/// Java BasePressurePlateBlock.canSurvive: canSupportRigidBlock or canSupportCenter below.
bool plateCanSurvive(BlockSource& region, BlockPos const& pos) {
    BlockPos below = offset(pos, Down);
    return sturdy(region, below, Up) || sturdyCenter(region, below, Up);
}
} // namespace detail

bool pressButton(GlobalPos const& gp) {
    auto*        region = regionOf(gp.dim);
    BlockPos     pos{gp.x, gp.y, gp.z};
    Block const* b = region ? blockAt(*region, pos) : nullptr;
    auto const*  e = b ? entryOf(*b) : nullptr;
    if (!e || e->family != Family::Button) return false;
    if (flagState(*b, state::Powered)) return false;
    setStates(gp, {{state::Powered, true}});
    clickSound(*region, pos, *e, true);
    // Java ButtonBlock.tick after ticksToStayPressed
    schedule(buttonTicks(*e), [gp] {
        auto const* now = blockAt(gp);
        auto const* ne  = now ? entryOf(*now) : nullptr;
        if (!ne || ne->family != Family::Button || !flagState(*now, state::Powered)) return;
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
    // a switch saved while pressed: its release is checked again once its chunk ticks
    detail::onChunkLoaded([](BlockSource& region, LevelChunk& chunk) {
        detail::forEachInChunk(region, chunk, [](BlockType const& t) {
            auto const* e = entryOf(t);
            return e && (e->family == Family::Button || e->family == Family::PressurePlate);
        }, [&region](BlockPos const& p, Block const& b) {
            if (!flagState(b, state::Powered)) return;
            auto const* e  = entryOf(b);
            auto        gp = game::globalPos(region, p);
            if (e->family == Family::Button) schedule(buttonTicks(*e), [gp] { checkButton(gp); });
            else schedule(20, [gp] { checkPlate(gp); });
        });
    });
    for (auto const* e : entriesOf(Family::Button)) {
        blockevents::onEntityInside(e->id, [](BlockSource& region, BlockPos const& pos, Actor& entity) {
            if (!isArrow(entity)) return;
            Block const* b = blockAt(region, pos);
            if (b && !flagState(*b, state::Powered)) checkButton(game::globalPos(region, pos));
        });
        blockevents::onNeighborChanged(e->id, checkSupport);
    }
    for (auto const* e : entriesOf(Family::PressurePlate)) {
        blockevents::onEntityInside(e->id, [](BlockSource& region, BlockPos const& pos, Actor&) {
            Block const* b = blockAt(region, pos);
            if (b && !flagState(*b, state::Powered)) checkPlate(game::globalPos(region, pos));
        });
        blockevents::onNeighborChanged(e->id, checkSupport);
        // a plate pressed by a command or a structure is checked like one pressed by an entity
        blockevents::onPlaced(e->id, [](BlockSource& region, BlockPos const& pos, Block const& b, Actor*) {
            if (flagState(b, state::Powered)) schedule(20, [gp = game::globalPos(region, pos)] { checkPlate(gp); });
        });
    }
}

void uninstallSwitches() {
    for (auto id : handlerIds()) ia::remove(id);
    handlerIds().clear();
}

} // namespace gf::feature::families
