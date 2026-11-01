// Block families (F11): chests and trapped chests (Java ChestBlock / TrappedChestBlock): facing,
// double chests (twilightforest:type single / left / right) on placement and when a neighbour
// changes, and opening through the inventory contract (gf::inventory, F10): a single chest is a
// 27-slot container, a double chest a 54-slot one ("container.chestDouble") kept by its first half
// (Java DoubleBlockCombiner: the RIGHT half), refused while a solid block sits on either half.

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/common/Interactions.h"
#include "feature/families/BlockEvents.h"
#include "feature/families/Families.h"
#include "feature/inventory/Inventory.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::families {

namespace ia = interactions;

namespace {

std::vector<ia::HandlerId>& handlerIds() {
    static std::vector<ia::HandlerId> ids;
    return ids;
}

bool isChest(Entry const* e) { return e && (e->family == Family::Chest || e->family == Family::TrappedChest); }

int facingOf(Block const& b) { return dirOf(strState(b, state::Cardinal)).value_or(North); }
/// The chest type at `pos` ("" when nothing is there), for log lines.
std::string sst(BlockSource& region, BlockPos const& pos) {
    Block const* b = blockAt(region, pos);
    return b ? strState(*b, state::Type) : std::string();
}
std::string typeOf(Block const& b) {
    auto t = strState(b, state::Type);
    return t.empty() ? "single" : t;
}

/// Java ChestBlock.getConnectedDirection.
int connectedDirection(Block const& b) {
    int f = facingOf(b);
    return typeOf(b) == "left" ? clockwise(f) : counterClockwise(f);
}

/// Java ChestBlock.candidatePartnerFacing: the facing of a single chest of the same kind there.
std::optional<int> candidatePartnerFacing(BlockSource& region, BlockPos const& pos, std::string const& id, int dir) {
    Block const* b = blockAt(region, offset(pos, dir));
    if (!b || b->getTypeName() != id || typeOf(*b) != "single") return std::nullopt;
    return facingOf(*b);
}

/// Java ChestBlock.updateShape for the neighbour on `dir`.
void updateChest(BlockSource& region, BlockPos const& pos, int dir) {
    Block const* b = blockAt(region, pos);
    if (!b || !isChest(entryOf(*b))) return;
    Block const* n    = blockAt(region, offset(pos, dir));
    Json         s    = Json::object();
    bool         same = n && n->getTypeName() == b->getTypeName();
    if (same && horizontal(dir)) {
        auto nt = typeOf(*n);
        if (typeOf(*b) == "single" && nt != "single" && facingOf(*b) == facingOf(*n) && connectedDirection(*n) == opposite(dir)) {
            s[state::Type] = nt == "left" ? "right" : "left";
        }
    } else if (typeOf(*b) != "single" && connectedDirection(*b) == dir) {
        s[state::Type] = "single";
    }
    if (s.empty()) return;
    if (auto const* next = withStates(*b, s); next && next != b) game::setBlock(region, pos, *next);
}

/// Java ChestBlock.isChestBlockedAt: a redstone conductor (solid block) on top.
bool blocked(BlockSource& region, BlockPos const& pos) { return solidBlock(region, offset(pos, Up)); }

bool openChest(Player& player, GlobalPos const& pos) {
    auto* region = regionOf(pos.dim);
    if (!region) return false;
    BlockPos     bp = game::blockPos(pos);
    Block const* b  = blockAt(*region, bp);
    if (!b || !isChest(entryOf(*b))) return false;
    auto partner = chestPartner(*region, bp);
    if (blocked(*region, bp) || (partner && blocked(*region, *partner))) return false;
    if (!partner) return inventory::openChestLike(player, pos, 3, "container.chest");
    // the combined container belongs to the first half (Java: the RIGHT one)
    BlockPos first = typeOf(*b) == "right" ? bp : *partner;
    return inventory::openChestLike(player, game::globalPos(pos.dim, first), 6, "container.chestDouble");
}

} // namespace

std::optional<BlockPos> chestPartner(BlockSource& region, BlockPos const& pos) {
    Block const* b = blockAt(region, pos);
    if (!b || !isChest(entryOf(*b)) || typeOf(*b) == "single") return std::nullopt;
    BlockPos     p = offset(pos, connectedDirection(*b));
    Block const* o = blockAt(region, p);
    if (!o || o->getTypeName() != b->getTypeName() || typeOf(*o) == "single" || typeOf(*o) == typeOf(*b)) return std::nullopt;
    return p;
}

void pairChest(BlockSource& region, BlockPos const& pos, int facing, bool sneaking, std::optional<int> clickedFace) {
    Block const* b = blockAt(region, pos);
    if (!b || !isChest(entryOf(*b))) return;
    std::string const& id   = b->getTypeName();
    std::string        type = "single";
    int                dir  = facing;
    if (clickedFace && horizontal(*clickedFace) && sneaking) {
        auto d = candidatePartnerFacing(region, pos, id, opposite(*clickedFace));
        if (d && (*d < 4 ? 2 : 0) != (*clickedFace < 4 ? 2 : 0)) {
            dir  = *d;
            type = counterClockwise(*d) == opposite(*clickedFace) ? "right" : "left";
        }
    }
    if (type == "single" && !sneaking) {
        if (auto d = candidatePartnerFacing(region, pos, id, clockwise(dir)); d && *d == dir) type = "left";
        else if (auto d2 = candidatePartnerFacing(region, pos, id, counterClockwise(dir)); d2 && *d2 == dir) type = "right";
    }
    if (auto const* next = withStates(*b, {{state::Cardinal, dirName(dir)}, {state::Type, type}}); next && next != b) game::setBlock(region, pos, *next);
}

void installChests() {
    for (auto family : {Family::Chest, Family::TrappedChest}) {
        for (auto const* e : entriesOf(family)) {
            ia::Match m;
            m.block = e->id;
            handlerIds().push_back(ia::onPlace(m, [](ia::PlaceContext const& c) {
                auto* region = regionOf(c.pos.dim);
                if (!region) return;
                // Java: the chest faces the player; sneaking against a chest's side joins that one
                pairChest(*region, game::blockPos(c.pos), opposite(lookDir(c.player)), c.sneaking, c.face);
                log().info("families: {} placed at {} {} {} ({})", c.blockId, c.pos.x, c.pos.y, c.pos.z, sst(*region, game::blockPos(c.pos)));
            }));
            handlerIds().push_back(ia::onUseOn(m, [](ia::UseOnContext const& c) {
                if (c.sneaking && !c.itemId.empty()) return ia::Result::Pass;
                if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
                openChest(c.player, c.pos);
                return ia::Result::Consume;
            }));
        }
    }
    static bool registered = false;
    if (registered) return;
    registered = true;
    for (auto family : {Family::Chest, Family::TrappedChest}) {
        for (auto const* e : entriesOf(family)) {
            inventory::Handler h;
            h.slots = 27;
            h.open  = [](Player& player, GlobalPos const& pos) { return openChest(player, pos); };
            inventory::registerBlock(e->id, std::move(h));
            blockevents::onNeighborChanged(e->id, [](BlockSource& region, BlockPos const& pos, BlockPos const& from) {
                for (int d : kHorizontal) {
                    BlockPos n = offset(pos, d);
                    if (n.x == from.x && n.y == from.y && n.z == from.z) updateChest(region, pos, d);
                }
            });
        }
    }
}

void uninstallChests() {
    for (auto id : handlerIds()) ia::remove(id);
    handlerIds().clear();
}

} // namespace gf::feature::families
