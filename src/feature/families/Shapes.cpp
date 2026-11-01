// Adapted from LiteLDev/Levitate v0.1.0 (src/feature/materials/MaterialsConnections.cpp and
// src/feature/materials/MaterialsModule.cpp), LGPL-3.0-or-later.
// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/ConnectableRotatedPillarBlock.java
//   src/main/java/twilightforest/block/WallPillarBlock.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Block families (F11): the shapes that follow their neighbours (Java updateShape), kept by the
// server for every client:
//   fence (FenceBlock), pane (IronBarsBlock) and wall (WallBlock) connections with Java's rules,
//   the wall post and tall sides, TF's wall pillars and connectable pillars (six connections),
//   connected textures (connections to the same block), fence gates lowered in walls, and the
//   24 px collision of fences, walls and closed fence gates; slab doubling (SlabBlock).

#include <algorithm>
#include <cmath>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/GetCollisionShapeInterface.h"
#include "mc/world/phys/AABB.h"

#include "feature/common/Interactions.h"
#include "feature/families/BlockEvents.h"
#include "feature/families/Families.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::families {

namespace ia = interactions;

namespace {

// ---- neighbour kinds --------------------------------------------------------------------------------

enum class Kind { Other, Fence, FenceGate, Wall, Pane };

bool hasProperty(Block const& b, BlockProperty p) {
    return (static_cast<std::uint64_t>(b.getBlockType().mProperties) & static_cast<std::uint64_t>(p)) != 0;
}

Kind kindOf(Block const& b, bool* wooden = nullptr) {
    if (auto const* e = entryOf(b)) {
        switch (e->family) {
        case Family::Fence:
            if (wooden) *wooden = e->flag("wooden", true);
            return Kind::Fence;
        case Family::FenceGate: return Kind::FenceGate;
        case Family::Wall: return Kind::Wall;
        case Family::Pane: return Kind::Pane;
        default: return Kind::Other;
        }
    }
    auto const& type = b.getBlockType();
    if (type.isFenceBlock()) {
        if (wooden) *wooden = b.getTypeName() != "minecraft:nether_brick_fence";
        return Kind::Fence;
    }
    if (type.isWallBlock()) return Kind::Wall;
    if (type.isThinFenceBlock()) return Kind::Pane;
    if (hasProperty(b, BlockProperty::FenceGate) || b.getTypeName().ends_with("fence_gate")) return Kind::FenceGate;
    return Kind::Other;
}

/// The facing of a fence gate (ours or vanilla's) as a FacingID.
int gateFacing(Block const& b) {
    if (auto d = dirOf(strState(b, state::Cardinal))) return *d;
    // vanilla: direction 0 south, 1 west, 2 north, 3 east
    static constexpr int legacy[4]{South, West, North, East};
    if (auto d = intState(b, "direction")) return legacy[*d & 3];
    return North;
}

int axisOf(int dir) { return dir < 2 ? 1 : (dir < 4 ? 2 : 0); } // 0 x, 1 y, 2 z

/// Java FenceGateBlock.connectsToDirection(state, direction).
bool gateConnects(Block const& gate, int dir) { return axisOf(gateFacing(gate)) == axisOf(clockwise(dir)); }

/// Java FenceBlock.connectsTo for the neighbour on `dir` of a fence (`wooden`).
bool fenceConnects(BlockSource& region, BlockPos const& pos, int dir, bool wooden) {
    BlockPos     n  = offset(pos, dir);
    Block const* nb = blockAt(region, n);
    if (!nb || nb->isAir()) return false;
    bool nWooden = false;
    Kind k       = kindOf(*nb, &nWooden);
    if (k == Kind::Fence && nWooden == wooden) return true;
    if (k == Kind::FenceGate && gateConnects(*nb, opposite(dir))) return true;
    return !exceptionForConnection(*nb) && sturdy(region, n, opposite(dir));
}

/// Java IronBarsBlock.attachsTo.
bool paneConnects(BlockSource& region, BlockPos const& pos, int dir) {
    BlockPos     n  = offset(pos, dir);
    Block const* nb = blockAt(region, n);
    if (!nb || nb->isAir()) return false;
    Kind k = kindOf(*nb);
    if (k == Kind::Pane || k == Kind::Wall) return true;
    return !exceptionForConnection(*nb) && sturdy(region, n, opposite(dir));
}

/// Java WallBlock.connectsTo.
bool wallConnects(BlockSource& region, BlockPos const& pos, int dir) {
    BlockPos     n  = offset(pos, dir);
    Block const* nb = blockAt(region, n);
    if (!nb || nb->isAir()) return false;
    Kind k = kindOf(*nb);
    if (k == Kind::Wall || k == Kind::Pane) return true;
    if (k == Kind::FenceGate && gateConnects(*nb, opposite(dir))) return true;
    return !exceptionForConnection(*nb) && sturdy(region, n, opposite(dir));
}

// ---- walls (Java WallBlock) -------------------------------------------------------------------------

/// The collision boxes of the block at `p` touching its bottom face, in block pixels (x0 z0 x1 z1).
std::vector<std::array<float, 4>> bottomFace(BlockSource& region, BlockPos const& p) {
    std::vector<std::array<float, 4>> out;
    Block const*                      b = blockAt(region, p);
    if (!b || b->isAir()) return out;
    std::vector<AABB> boxes;
    b->getBlockType().addCollisionShapes(*b, region, p, nullptr, boxes, {});
    for (auto const& box : boxes) {
        if (box.min.y > p.y + 1e-4f) continue; // Java getFaceShape(DOWN): only what touches the bottom
        out.push_back({(box.min.x - p.x) * 16.f, (box.min.z - p.z) * 16.f, (box.max.x - p.x) * 16.f, (box.max.z - p.z) * 16.f});
    }
    return out;
}

/// Java WallBlock.isCovered: the face shape covers the test rectangle (x0 z0 x1 z1, pixels).
bool covered(std::vector<std::array<float, 4>> const& face, std::array<float, 4> const& test) {
    if (face.empty()) return false;
    for (float x = test[0] + .25f; x < test[2]; x += .5f) {
        for (float z = test[1] + .25f; z < test[3]; z += .5f) {
            bool in = false;
            for (auto const& r : face) {
                if (x >= r[0] - 1e-3f && x <= r[2] + 1e-3f && z >= r[1] - 1e-3f && z <= r[3] + 1e-3f) {
                    in = true;
                    break;
                }
            }
            if (!in) return false;
        }
    }
    return true;
}

// Java WallBlock's test shapes (x0 z0 x1 z1) for north, east, south, west and the post
constexpr std::array<int, 4>                 kWallDirs{North, East, South, West};
constexpr std::array<std::array<float, 4>, 4> kSideTest{{{7, 0, 9, 9}, {7, 7, 16, 9}, {7, 7, 9, 16}, {0, 7, 9, 9}}};
constexpr std::array<float, 4>               kPostTest{7, 7, 9, 9};

/// Java BlockTags.WALL_POST_OVERRIDE: torches, signs, banners, pressure plates.
bool postOverride(Block const& b) {
    if (auto const* e = entryOf(b)) {
        return e->family == Family::Sign || e->family == Family::WallSign || e->family == Family::HangingSign || e->family == Family::WallHangingSign
            || e->family == Family::PressurePlate;
    }
    std::string const& id = b.getTypeName();
    if (id.find("torch") != std::string::npos && id.find("torchflower") == std::string::npos) return true;
    if (id.find("sign") != std::string::npos || id.find("banner") != std::string::npos) return true;
    return id.ends_with("pressure_plate");
}

bool wallPostAbove(Block const& above) {
    if (kindOf(above) != Kind::Wall) return false;
    if (auto up = intState(above, state::Up)) return *up != 0;
    auto bit = strState(above, "wall_post_bit"); // vanilla walls
    return bit == "1" || bit == "true";
}

bool updateWall(BlockSource& region, BlockPos const& pos, Block const& block) {
    BlockPos     abovePos = offset(pos, Up);
    Block const* above    = blockAt(region, abovePos);
    auto         face     = bottomFace(region, abovePos);
    enum class Side { None, Low, Tall };
    Side side[4];
    for (int i = 0; i < 4; ++i) {
        bool conn = wallConnects(region, pos, kWallDirs[i]);
        side[i]   = !conn ? Side::None : (covered(face, kSideTest[i]) ? Side::Tall : Side::Low);
    }
    // Java WallBlock.shouldRaisePost
    bool up = true;
    if (!above || !wallPostAbove(*above)) {
        bool n = side[0] == Side::None, e = side[1] == Side::None, s = side[2] == Side::None, w = side[3] == Side::None;
        bool open = (n && s && w && e) || n != s || w != e;
        if (!open) {
            bool tallLine = (side[0] == Side::Tall && side[2] == Side::Tall) || (side[1] == Side::Tall && side[3] == Side::Tall);
            up            = !tallLine && ((above && postOverride(*above)) || covered(face, kPostTest));
        }
    }
    Json s{{state::Up, up ? 1 : 0}};
    for (int i = 0; i < 4; ++i) s[state::wallSide(kWallDirs[i])] = side[i] == Side::Tall ? 2 : (side[i] == Side::Low ? 1 : 0);
    Block const* next = withStates(block, s);
    return next && next != &block && game::setBlock(region, pos, *next);
}

// ---- pillars ------------------------------------------------------------------------------------------

/// The axis (0 x, 1 y, 2 z) of a block placed on minecraft:block_face.
int pillarAxis(Block const& b) {
    auto f = dirOf(strState(b, state::BlockFace));
    return f ? axisOf(*f) : 1;
}

bool updatePillarConnections(BlockSource& region, BlockPos const& pos, Block const& block, Entry const& e) {
    int  axis = pillarAxis(block);
    Json s    = Json::object();
    for (int d = 0; d < 6; ++d) {
        BlockPos     n  = offset(pos, d);
        Block const* nb = blockAt(region, n);
        bool         on = false;
        if (nb && !nb->isAir()) {
            if (e.family == Family::WallPillar) {
                // TF WallPillarBlock.canConnectTo
                auto const* ne = entryOf(*nb);
                on = ne && ne->family == Family::WallPillar && (axisOf(d) == axis || axisOf(d) == pillarAxis(*nb));
            } else if (e.family == Family::ConnectedTexture) {
                // TF ConnectedTextureModel: the blocks it connects to (default: itself)
                auto list = e.params.find("connects_to");
                if (list != e.params.end() && list->is_array()) {
                    for (auto const& id : *list)
                        if (id.is_string() && id.get<std::string>() == nb->getTypeName()) on = true;
                } else {
                    on = nb->getTypeName() == e.id;
                }
            } else {
                // TF ConnectableRotatedPillarBlock.canConnectTo
                on = !exceptionForConnection(*nb) && sturdy(region, n, opposite(d));
            }
        }
        s[state::side(d)] = on ? 1 : 0;
    }
    Block const* next = withStates(block, s);
    return next && next != &block && game::setBlock(region, pos, *next);
}

bool updateConnections4(BlockSource& region, BlockPos const& pos, Block const& block, Entry const& e) {
    Json s = Json::object();
    for (int d : kHorizontal) {
        bool on = e.family == Family::Fence ? fenceConnects(region, pos, d, e.flag("wooden", true)) : paneConnects(region, pos, d);
        s[state::side(d)] = on ? 1 : 0;
    }
    Block const* next = withStates(block, s);
    return next && next != &block && game::setBlock(region, pos, *next);
}

/// Java FenceGateBlock.updateShape: lowered between two walls on its sides.
bool updateGateInWall(BlockSource& region, BlockPos const& pos, Block const& block) {
    int  facing = gateFacing(block);
    auto isWall = [&](int d) {
        Block const* b = blockAt(region, offset(pos, d));
        return b && kindOf(*b) == Kind::Wall;
    };
    bool         inWall = isWall(clockwise(facing)) || isWall(counterClockwise(facing));
    Block const* next   = withStates(block, {{state::InWall, inWall ? 1 : 0}});
    return next && next != &block && game::setBlock(region, pos, *next);
}

// ---- slabs ----------------------------------------------------------------------------------------------

/// A single slab's half (true = top); nullopt for double slabs and other blocks.
std::optional<bool> singleSlabHalf(Block const& block) {
    auto const* e = entryOf(block);
    if (!e || e->family != Family::Slab) return std::nullopt;
    if (intState(block, state::Double).value_or(0) != 0) return std::nullopt;
    return strState(block, state::VerticalHalf) == "top";
}

/// Java SlabBlock.canBeReplaced: a single slab takes a second one of its kind into its empty half.
ia::Result slabUseOn(ia::UseOnContext const& c) {
    if (c.itemId.empty()) return ia::Result::Pass;
    auto const* itemEntry = entryOf(c.itemId);
    if (!itemEntry || itemEntry->family != Family::Slab) return ia::Result::Pass;
    if (!c.firstEvent) return ia::Result::Pass;
    auto&                   player  = c.player;
    auto&                   region  = player.getDimensionBlockSource();
    Block const&            clicked = c.block;
    BlockPos                at      = game::blockPos(c.pos);
    int                     face    = c.face;
    std::optional<BlockPos> mergeAt;
    if (clicked.getTypeName() == c.itemId) {
        // the clicked slab: its top face (bottom slab), bottom face (top slab), or a side clicked
        // in the empty half
        if (auto top = singleSlabHalf(clicked)) {
            bool upper = c.hit[1] - std::floor(c.hit[1]) > 0.5;
            bool side  = face >= 2 && face < 6;
            if ((!*top && (face == Up || (upper && side))) || (*top && (face == Down || (!upper && side)))) mergeAt = at;
        }
    }
    if (!mergeAt && !game::replaceable(region, at) && face >= 0 && face < 6) {
        // placing into the block beside the clicked face: a single slab there takes it
        BlockPos     n     = offset(at, face);
        Block const* there = blockAt(region, n);
        if (there && there->getTypeName() == c.itemId && singleSlabHalf(*there)) mergeAt = n;
    }
    if (mergeAt) {
        // vanilla placement rules: no adventure mode, nobody standing in the block
        AABB box{Vec3{float(mergeAt->x), float(mergeAt->y), float(mergeAt->z)}, Vec3{mergeAt->x + 1.0f, mergeAt->y + 1.0f, mergeAt->z + 1.0f}};
        if (player.getPlayerGameType() == GameType::Adventure || !region.isUnobstructedByEntities(box, nullptr)) mergeAt.reset();
    }
    if (!mergeAt || !mergeSlab(region, *mergeAt)) return ia::Result::Pass;
    consumeHeld(player, c.item);
    bedrockSound(region, *mergeAt, "use.wood");
    log().info("families: {} merged {} into a double slab", player.getRealName(), c.itemId);
    return ia::Result::Consume;
}

std::vector<ia::HandlerId>& handlerIds() {
    static std::vector<ia::HandlerId> ids;
    return ids;
}

// ---- collision (Java: fences, walls and closed gates are 24 px high) ------------------------------------

std::vector<std::array<float, 6>> tallCollision(Block const& block, Entry const& e) {
    std::vector<std::array<float, 6>> out;
    auto conn = [&](int d) { return intState(block, state::side(d)).value_or(0) != 0; };
    if (e.family == Family::Fence) {
        out.push_back({6, 0, 6, 10, 24, 10});
        if (conn(North)) out.push_back({6, 0, 0, 10, 24, 6});
        if (conn(South)) out.push_back({6, 0, 10, 10, 24, 16});
        if (conn(West)) out.push_back({0, 0, 6, 6, 24, 10});
        if (conn(East)) out.push_back({10, 0, 6, 16, 24, 10});
    } else if (e.family == Family::Wall) {
        // Java WallBlock.makeShapes(4, 3, 24, 0, 24, 24): post 4..12, sides 5..11, all 24 px high
        if (intState(block, state::Up).value_or(1) != 0) out.push_back({4, 0, 4, 12, 24, 12});
        auto side = [&](int d) { return intState(block, state::wallSide(d)).value_or(0) != 0; };
        if (side(North)) out.push_back({5, 0, 0, 11, 24, 11});
        if (side(South)) out.push_back({5, 0, 5, 11, 24, 16});
        if (side(West)) out.push_back({0, 0, 5, 11, 24, 11});
        if (side(East)) out.push_back({5, 0, 5, 16, 24, 11});
        if (out.empty()) out.push_back({4, 0, 4, 12, 24, 12});
    } else if (e.family == Family::FenceGate && intState(block, state::Open).value_or(0) == 0) {
        int axis = axisOf(gateFacing(block));
        if (axis == 2) out.push_back({0, 0, 6, 16, 24, 10});
        else out.push_back({6, 0, 0, 10, 24, 16});
    }
    return out;
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    FamiliesCollisionHook,
    ll::memory::HookPriority::Normal,
    BlockType,
    &BlockType::$addCollisionShapes,
    bool,
    ::Block const&                                     block,
    ::IConstBlockSource const&                         region,
    ::BlockPos const&                                  pos,
    ::AABB const*                                      intersectTestBox,
    ::std::vector<::AABB>&                             inoutBoxes,
    ::optional_ref<::GetCollisionShapeInterface const> entity
) {
    auto const* e = entryOf(block);
    if (!e || (e->family != Family::Fence && e->family != Family::Wall && e->family != Family::FenceGate)) {
        return origin(block, region, pos, intersectTestBox, inoutBoxes, entity);
    }
    auto boxes = tallCollision(block, *e);
    if (boxes.empty()) return origin(block, region, pos, intersectTestBox, inoutBoxes, entity);
    bool added = false;
    for (auto const& b : boxes) {
        AABB box{
            Vec3{pos.x + b[0] / 16.f, pos.y + b[1] / 16.f, pos.z + b[2] / 16.f},
            Vec3{pos.x + b[3] / 16.f, pos.y + b[4] / 16.f, pos.z + b[5] / 16.f}
        };
        if (intersectTestBox && !intersectTestBox->intersects(box)) continue;
        inoutBoxes.push_back(box);
        added = true;
    }
    return added;
}

bool mergeSlab(BlockSource& region, BlockPos const& pos) {
    Block const* block = blockAt(region, pos);
    if (!block || !singleSlabHalf(*block)) return false;
    auto const* merged = withStates(*block, {{state::Double, 1}});
    return merged && merged != block && game::setBlock(region, pos, *merged);
}

bool updateShape(BlockSource& region, BlockPos const& pos) {
    Block const* block = blockAt(region, pos);
    if (!block) return false;
    auto const* e = entryOf(*block);
    if (!e) return false;
    switch (e->family) {
    case Family::Fence:
    case Family::Pane: return updateConnections4(region, pos, *block, *e);
    case Family::Wall: return updateWall(region, pos, *block);
    case Family::FenceGate: return updateGateInWall(region, pos, *block);
    case Family::WallPillar:
    case Family::ConnectablePillar:
    case Family::ConnectedTexture: return updatePillarConnections(region, pos, *block, *e);
    default: return false;
    }
}

void installShapes() {
    FamiliesCollisionHook::hook();
    handlerIds().push_back(ia::onUseOn({}, slabUseOn));
    // neighbour-dependent shapes: placed, and every neighbour change (Java updateShape); the
    // block event registry has no removal, so the handlers are registered once per process
    static bool registered = false;
    if (registered) return;
    registered = true;
    for (auto family : {Family::Fence, Family::Pane, Family::Wall, Family::FenceGate, Family::WallPillar, Family::ConnectablePillar, Family::ConnectedTexture}) {
        for (auto const* e : entriesOf(family)) {
            blockevents::onNeighborChanged(e->id, [](BlockSource& region, BlockPos const& pos, BlockPos const&) { updateShape(region, pos); });
            blockevents::onPlaced(e->id, [](BlockSource& region, BlockPos const& pos, Block const&, Actor*) { updateShape(region, pos); });
        }
    }
}

void uninstallShapes() {
    for (auto id : handlerIds()) ia::remove(id);
    handlerIds().clear();
    FamiliesCollisionHook::unhook();
}

} // namespace gf::feature::families
