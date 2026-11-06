// U5 (building): block helpers (see Util.h).

#include "feature/building/Util.h"

#include <cmath>
#include <numbers>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockSupportType.h"
#include "mc/world/level/block/BlockType.h"

#include "feature/common/Redstone.h"
#include "game/Blocks.h"

namespace gf::feature::building {

char const* dirName(int d) {
    static constexpr char const* names[]{"down", "up", "north", "south", "west", "east"};
    return d >= 0 && d < 6 ? names[d] : "north";
}

int dirFromName(std::string_view name) {
    for (int d = 0; d < 6; ++d)
        if (name == dirName(d)) return d;
    return -1;
}

char const* axisName(int axis) { return axis == 0 ? "x" : (axis == 2 ? "z" : "y"); }

BlockPos relative(BlockPos const& p, int d, int n) { return {p.x + kDx[d] * n, p.y + kDy[d] * n, p.z + kDz[d] * n}; }

std::string idOf(Block const& block) { return block.getTypeName(); }

Block const* blockAt(BlockSource& region, BlockPos const& pos) {
    if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight()) return nullptr;
    if (!region.hasChunksAt(pos, 0, false)) return nullptr;
    return &region.getBlock(pos);
}

std::string idAt(BlockSource& region, BlockPos const& pos) {
    auto const* b = blockAt(region, pos);
    return b ? b->getTypeName() : std::string();
}

namespace {
CompoundTagVariant const* stateTag(Block const& block, std::string const& name) {
    auto const& nbt = *block.mSerializationId;
    if (!nbt.contains("states")) return nullptr;
    auto const& states = nbt["states"];
    if (!states.is_object()) return nullptr;
    auto const& c = states.get<CompoundTag>();
    if (!c.contains(name)) return nullptr;
    return &c[name];
}
} // namespace

std::string stateOf(Block const& block, std::string const& name) {
    auto const* t = stateTag(block, name);
    if (!t) return {};
    if (t->is_string()) return std::string(t->get<StringTag>());
    if (t->hold(Tag::Type::Byte)) return t->get<ByteTag>().data ? "true" : "false";
    if (t->hold(Tag::Type::Int)) return std::to_string(t->get<IntTag>().data);
    return {};
}

bool boolState(Block const& block, std::string const& name) { return stateOf(block, name) == "true"; }

int intState(Block const& block, std::string const& name, int fallback) {
    auto const* t = stateTag(block, name);
    if (!t) return fallback;
    if (t->hold(Tag::Type::Int)) return t->get<IntTag>().data;
    if (t->hold(Tag::Type::Byte)) return t->get<ByteTag>().data;
    return fallback;
}

Block const* withStates(Block const& block, Json const& states) {
    if (!states.is_object() || states.empty()) return &block;
    auto nbt = block.mSerializationId->clone();
    if (!nbt->contains("states") || !(*nbt)["states"].is_object()) return nullptr;
    auto& current = (*nbt)["states"].get<CompoundTag>();
    bool  changed = false;
    for (auto const& [name, value] : states.items()) {
        if (!current.contains(name)) return nullptr;
        auto& slot = current[name];
        if (slot.hold(Tag::Type::Byte)) {
            int v = value.is_boolean() ? (value.get<bool>() ? 1 : 0) : (value.is_number_integer() ? (value.get<int>() != 0 ? 1 : 0) : -1);
            if (v < 0) return nullptr;
            if (static_cast<int>(slot.get<ByteTag>().data) != v) changed = true;
            slot = ByteTag(static_cast<uchar>(v));
        } else if (slot.hold(Tag::Type::Int)) {
            if (!value.is_number_integer()) return nullptr;
            if (slot.get<IntTag>().data != value.get<int>()) changed = true;
            slot = IntTag(value.get<int>());
        } else if (slot.is_string()) {
            if (!value.is_string()) return nullptr;
            if (std::string(slot.get<StringTag>()) != value.get<std::string>()) changed = true;
            slot = StringTag(value.get<std::string>());
        } else {
            return nullptr;
        }
    }
    if (!changed) return &block;
    auto found = Block::tryGetFromRegistry(*nbt);
    return found ? found.as_ptr() : nullptr;
}

bool setStates(BlockSource& region, BlockPos const& pos, Json const& states) {
    auto const* b = blockAt(region, pos);
    if (!b) return false;
    auto const* n = withStates(*b, states);
    if (!n || n == b) return false;
    return game::setBlock(region, pos, *n);
}

bool setBlock(BlockSource& region, BlockPos const& pos, std::string const& id, Json const& states) {
    auto const* b = game::blockWithStates(id, states);
    return b && game::setBlock(region, pos, *b);
}

std::array<int, 6> nearestLooking(Actor const& actor) {
    // Java Direction.orderedByNearest(entity)
    auto  rot   = actor.getRotation();
    float pitch = rot.x * std::numbers::pi_v<float> / 180.0f, yaw = -rot.y * std::numbers::pi_v<float> / 180.0f;
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

int horizontalFacing(Actor const& actor) {
    static constexpr int map[4]{South, West, North, East};
    return map[game::playerFacing(actor) & 3];
}

bool sturdy(BlockSource& region, BlockPos const& pos, int face) {
    auto const* b = blockAt(region, pos);
    if (!b || b->isAir()) return false;
    return b->canProvideSupport(static_cast<uchar>(face), BlockSupportType::Edge);
}

bool sturdyCenter(BlockSource& region, BlockPos const& pos, int face) {
    auto const* b = blockAt(region, pos);
    if (!b || b->isAir()) return false;
    return b->canProvideSupport(static_cast<uchar>(face), BlockSupportType::Center);
}

bool replaceable(BlockSource& region, BlockPos const& pos) { return game::replaceable(region, pos); }

bool hasNeighborSignal(BlockSource& region, BlockPos const& pos) { return redstone::powered(region, pos); }

void popResource(BlockSource& region, BlockPos const& pos, ItemStack const& stack) {
    if (stack.isNull()) return;
    region.getLevel().getSpawner().spawnItem(region, stack, nullptr, Vec3{pos.x + .5f, pos.y + .5f, pos.z + .5f}, 10);
}

void give(Player& player, ItemStack const& item) {
    if (item.isNull()) return;
    ItemStack stack = item;
    if (!player.add(stack) || !stack.isNull()) {
        if (!stack.isNull()) player.getLevel().getSpawner().spawnItem(player.getDimensionBlockSource(), stack, nullptr, player.getPosition(), 0);
    }
    player.sendInventory(false);
}

} // namespace gf::feature::building
