// Adapted from LiteLDev/Levitate v0.1.0 (src/feature/building/World.cpp), LGPL-3.0-or-later.
//
// Block families (F11): world helpers shared by the family behaviours (blocks and states,
// directions, support, redstone input, sounds, items, deferred and scheduled work).

#include <algorithm>
#include <cmath>
#include <list>
#include <numbers>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
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
#include "mc/world/level/block/CachedComponentData.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "feature/common/Redstone.h"
#include "feature/families/Families.h"
#include "feature/fx/Fx.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::families {

namespace state {
std::string side(int dir) { return std::string("twilightforest:") + dirName(dir); }
std::string wallSide(int dir) { return std::string("twilightforest:") + dirName(dir) + "_wall"; }
} // namespace state

char const* dirName(int dir) {
    static constexpr char const* names[]{"down", "up", "north", "south", "west", "east"};
    return dir >= 0 && dir < 6 ? names[dir] : "north";
}

std::optional<int> dirOf(std::string_view name) {
    for (int d = 0; d < 6; ++d)
        if (name == dirName(d)) return d;
    return std::nullopt;
}

int clockwise(int dir) {
    switch (dir) {
    case North: return East;
    case East: return South;
    case South: return West;
    case West: return North;
    default: return dir;
    }
}

int counterClockwise(int dir) {
    switch (dir) {
    case North: return West;
    case West: return South;
    case South: return East;
    case East: return North;
    default: return dir;
    }
}

GlobalPos offset(GlobalPos p, int dir, int n) {
    p.x += kDx[dir] * n;
    p.y += kDy[dir] * n;
    p.z += kDz[dir] * n;
    return p;
}

BlockPos offset(BlockPos const& p, int dir, int n) { return {p.x + kDx[dir] * n, p.y + kDy[dir] * n, p.z + kDz[dir] * n}; }

BlockSource* regionOf(int dim) { return game::regionOf(dim); }

Block const* blockAt(BlockSource& region, BlockPos const& p) {
    if (p.y < region.getMinHeight() || p.y >= region.getMaxHeight()) return nullptr;
    if (!region.hasChunksAt(p, 0, false)) return nullptr;
    return &region.getBlock(p);
}

Block const* blockAt(GlobalPos const& p) {
    auto* region = regionOf(p.dim);
    return region ? blockAt(*region, BlockPos{p.x, p.y, p.z}) : nullptr;
}

std::string idAt(GlobalPos const& p) {
    auto const* b = blockAt(p);
    return b ? b->getTypeName() : std::string();
}

bool set(GlobalPos const& p, Block const& block) {
    auto* region = regionOf(p.dim);
    return region && game::setBlock(*region, BlockPos{p.x, p.y, p.z}, block);
}

bool clear(GlobalPos const& p) {
    auto* region = regionOf(p.dim);
    return region && game::clearBlock(*region, BlockPos{p.x, p.y, p.z});
}

bool replaceableAt(GlobalPos const& p) {
    auto* region = regionOf(p.dim);
    return region && game::replaceable(*region, BlockPos{p.x, p.y, p.z});
}

std::optional<int> intState(Block const& block, std::string_view name) { return game::intState(block, name); }

std::string strState(Block const& block, std::string_view name) {
    auto const& nbt = *block.mSerializationId;
    if (!nbt.contains("states")) return {};
    auto const& states = nbt["states"];
    if (!states.is_object()) return {};
    auto const& c = states.get<CompoundTag>();
    if (!c.contains(std::string(name))) return {};
    auto const& v = c[std::string(name)];
    if (v.is_string()) return std::string(v.get<StringTag>());
    if (v.hold(Tag::Type::Byte)) return static_cast<int>(v.get<ByteTag>().data) ? "1" : "0";
    if (v.hold(Tag::Type::Int)) return std::to_string(v.get<IntTag>().data);
    return {};
}

Block const* withStates(Block const& block, Json const& states) {
    if (!states.is_object() || states.empty()) return &block;
    auto nbt = block.mSerializationId->clone();
    if (!nbt->contains("states") || !(*nbt)["states"].is_object()) return nullptr;
    auto& current = (*nbt)["states"].get<CompoundTag>();
    bool  changed = false;
    for (auto const& [name, value] : states.items()) {
        if (!current.contains(name)) continue; // the block has no such state
        auto& slot = current[name];
        if (slot.hold(Tag::Type::Byte)) {
            int v = value.is_boolean() ? (value.get<bool>() ? 1 : 0) : (value.is_number_integer() ? (value.get<int>() != 0 ? 1 : 0) : -1);
            if (v < 0) return nullptr;
            if (static_cast<int>(slot.get<ByteTag>().data) != v) changed = true;
            slot = ByteTag(static_cast<uchar>(v));
        } else if (slot.hold(Tag::Type::Int)) {
            int v = value.is_boolean() ? (value.get<bool>() ? 1 : 0) : (value.is_number_integer() ? value.get<int>() : -1);
            if (v < 0) return nullptr;
            if (slot.get<IntTag>().data != v) changed = true;
            slot = IntTag(v);
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

bool setStates(GlobalPos const& p, Json const& states) {
    auto const* b = blockAt(p);
    if (!b) return false;
    auto const* n = withStates(*b, states);
    if (!n || n == b) return false;
    return set(p, *n);
}

int lookDir(Actor const& actor) {
    // Java 2D data 0 S, 1 W, 2 N, 3 E (game::playerFacing) -> FacingID
    static constexpr int map[4]{South, West, North, East};
    return map[game::playerFacing(actor) & 3];
}

std::array<int, 6> nearestLooking(Actor const& actor) {
    // Bedrock rotation: x = pitch (down positive), y = yaw (0 = south, 90 = west)
    auto   rot   = actor.getRotation();
    double pitch = rot.x * std::numbers::pi / 180.0, yaw = rot.y * std::numbers::pi / 180.0;
    double look[3]{-std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    std::array<std::pair<double, int>, 6> scored;
    for (int d = 0; d < 6; ++d) scored[d] = {-(kDx[d] * look[0] + kDy[d] * look[1] + kDz[d] * look[2]), d};
    std::stable_sort(scored.begin(), scored.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
    std::array<int, 6> out{};
    for (int i = 0; i < 6; ++i) out[i] = scored[i].second;
    return out;
}

int signRotation(Actor const& actor) {
    // Java RotationSegment.convertToSegment(yRot + 180): floor((yRot + 180) * 16 / 360 + 0.5) & 15
    float yaw = actor.getRotation().y;
    return static_cast<int>(std::floor((yaw + 180.0f) * 16.0f / 360.0f + 0.5f)) & 15;
}

bool sturdy(BlockSource& region, BlockPos const& pos, int face) {
    auto const* b = blockAt(region, pos);
    if (!b || b->isAir()) return false;
    return b->canProvideSupport(static_cast<uchar>(face), BlockSupportType::Edge);
}

bool solidBlock(BlockSource& region, BlockPos const& pos) {
    auto const* b = blockAt(region, pos);
    return b && !b->isAir() && b->mCachedComponentData->mIsSolid;
}

bool exceptionForConnection(Block const& block) {
    auto const& type = block.getBlockType();
    if ((static_cast<std::uint64_t>(type.mProperties) & static_cast<std::uint64_t>(BlockProperty::Leaves)) != 0) return true;
    if (auto const* e = entryOf(block); e && e->family == Family::Leaves) return true;
    std::string const& id = block.getTypeName();
    if (id.find("leaves") != std::string::npos) return true;
    if (id.find("shulker_box") != std::string::npos) return true;
    return id == "minecraft:barrier" || id == "minecraft:carved_pumpkin" || id == "minecraft:lit_pumpkin" || id == "minecraft:melon_block"
        || id == "minecraft:pumpkin";
}

bool hasNeighborSignal(BlockSource& region, BlockPos const& pos) { return redstone::powered(region, pos); }

void sound(BlockSource& region, BlockPos const& pos, std::string const& event, float volume, float pitch) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto dim = level->getDimension(region.getDimensionId()).lock();
    if (!dim) return;
    fx::sound(*dim, event, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, volume, pitch);
}

void bedrockSound(BlockSource& region, BlockPos const& pos, std::string const& name, float volume, float pitch) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!packet) return;
    auto& pkt   = static_cast<PlaySoundPacket&>(*packet);
    pkt.mName   = name;
    pkt.mPos    = BlockPos{pos.x * 8 + 4, pos.y * 8 + 4, pos.z * 8 + 4};
    pkt.mVolume = volume;
    pkt.mPitch  = pitch;
    pkt.sendTo(pos, region.getDimensionId());
}

ItemStack makeItem(std::string_view id, int count) {
    auto s = game::items::make(id, count);
    return s ? *s : ItemStack{};
}

void drop(BlockSource& region, BlockPos const& pos, ItemStack const& item) {
    if (item.isNull()) return;
    region.getLevel().getSpawner().spawnItem(region, item, nullptr, Vec3{pos.x + .5f, pos.y + .5f, pos.z + .5f}, 10);
}

void give(Player& player, ItemStack const& item) {
    if (item.isNull()) return;
    ItemStack stack = item;
    if (!player.add(stack) || !stack.isNull()) {
        if (!stack.isNull()) player.getLevel().getSpawner().spawnItem(player.getDimensionBlockSource(), stack, nullptr, player.getPosition(), 0);
    }
    player.sendInventory(false);
}

void consumeHeld(Player& player, ItemStack const& contextItem) {
    if (player.isCreative()) return;
    auto& item = const_cast<ItemStack&>(contextItem);
    if (item.isNull()) return;
    item.remove(1);
    if (item.mCount <= 0) item = ItemStack::EMPTY_ITEM();
    player.sendInventory(false);
}

// ---- deferred and scheduled work --------------------------------------------------------------

namespace {
struct Scheduled {
    std::int64_t          due;
    std::function<void()> fn;
};
std::vector<std::function<void()>>& deferred() {
    static std::vector<std::function<void()>> d;
    return d;
}
std::list<Scheduled>& scheduled() {
    static std::list<Scheduled> s;
    return s;
}
std::int64_t& tickCounter() {
    static std::int64_t t = 0;
    return t;
}
} // namespace

void defer(std::function<void()> fn) { deferred().push_back(std::move(fn)); }

void schedule(int ticks, std::function<void()> fn) { scheduled().push_back({tickCounter() + std::max(1, ticks), std::move(fn)}); }

namespace detail {
/// One server tick of the deferred / scheduled queues (Module.cpp).
void runQueues() {
    ++tickCounter();
    auto run = [](std::function<void()>& fn) {
        try {
            fn();
        } catch (std::exception const& e) {
            log().error("families: deferred block update failed: {}", e.what());
        }
    };
    // deferred work may defer more (a door half updating the other): a few rounds per tick
    for (int round = 0; round < 8 && !deferred().empty(); ++round) {
        auto work = std::move(deferred());
        deferred().clear();
        for (auto& fn : work) run(fn);
    }
    auto& s = scheduled();
    std::vector<std::function<void()>> due;
    for (auto it = s.begin(); it != s.end();) {
        if (it->due <= tickCounter()) {
            due.push_back(std::move(it->fn));
            it = s.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& fn : due) run(fn);
}
void clearQueues() {
    deferred().clear();
    scheduled().clear();
}
} // namespace detail

} // namespace gf::feature::families
