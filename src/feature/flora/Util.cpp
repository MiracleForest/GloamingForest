// U6 (flora): block, item and effect helpers shared by the unit's files (see Flora.h).

#include "feature/flora/Flora.h"

#include <cmath>
#include <map>
#include <mutex>
#include <random>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/network/packet/SetTitlePacket.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockSupportType.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/BrightnessPair.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/material/Material.h"

#include "feature/fx/Fx.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Compat.h"
#include "game/Items.h"

namespace gf::feature::flora {

// ---- directions --------------------------------------------------------------------------------------

char const* dirName(int d) {
    static constexpr char const* names[]{"down", "up", "north", "south", "west", "east"};
    return d >= 0 && d < 6 ? names[d] : "up";
}

int dirFromName(std::string_view name) {
    for (int d = 0; d < 6; ++d)
        if (name == dirName(d)) return d;
    return -1;
}

BlockPos relative(BlockPos const& p, int d, int n) { return {p.x + kDx[d] * n, p.y + kDy[d] * n, p.z + kDz[d] * n}; }
BlockPos offset(BlockPos const& p, int dx, int dy, int dz) { return {p.x + dx, p.y + dy, p.z + dz}; }

// ---- blocks ------------------------------------------------------------------------------------------

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

int intState(Block const& block, std::string const& name, int fallback) {
    auto const* t = stateTag(block, name);
    if (!t) return fallback;
    if (t->hold(Tag::Type::Int)) return t->get<IntTag>().data;
    if (t->hold(Tag::Type::Byte)) return t->get<ByteTag>().data;
    return fallback;
}

bool boolState(Block const& block, std::string const& name) { return stateOf(block, name) == "true"; }

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

bool place(BlockSource& region, BlockPos const& pos, std::string const& id, Json const& states) {
    auto const* b = game::blockWithStates(id, states);
    return b && game::setBlock(region, pos, *b);
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

bool isAir(BlockSource& region, BlockPos const& pos) {
    auto const* b = blockAt(region, pos);
    return b && b->isAir();
}

namespace {
bool waterBlock(Block const& b) {
    auto const& n = b.getTypeName();
    return n == "minecraft:water" || n == "minecraft:flowing_water";
}
bool lavaBlock(Block const& b) {
    auto const& n = b.getTypeName();
    return n == "minecraft:lava" || n == "minecraft:flowing_lava";
}
} // namespace

bool hasWater(BlockSource& region, BlockPos const& pos) {
    // Fluids.WATER: a water source (Bedrock liquid_depth 0), in the block or its liquid layer
    auto source = [](Block const& w) { return waterBlock(w) && intState(w, "liquid_depth", 0) == 0; };
    auto const* b = blockAt(region, pos);
    if (!b) return false;
    if (source(*b)) return true;
    return source(region.getLiquidBlock(pos));
}

bool hasFluid(BlockSource& region, BlockPos const& pos) {
    auto const* b = blockAt(region, pos);
    if (!b) return false;
    if (waterBlock(*b) || lavaBlock(*b)) return true;
    auto const& l = region.getLiquidBlock(pos);
    return waterBlock(l) || lavaBlock(l);
}

bool tagged(Block const& block, std::string const& tag) { return tags::has(tags::Registry::Block, tag, block.getTypeName()); }

bool tagged(BlockSource& region, BlockPos const& pos, std::string const& tag) {
    auto const* b = blockAt(region, pos);
    return b && !b->isAir() && tagged(*b, tag);
}

int rawBrightness(BlockSource& region, BlockPos const& pos) {
    auto pair = region.getBrightnessPair(pos);
    return std::max<int>(pair.sky->mValue, pair.block->mValue);
}

int blockLight(BlockSource& region, BlockPos const& pos) { return region.getBrightnessPair(pos).block->mValue; }

void destroyBlock(BlockSource& region, BlockPos const& pos, bool drops) { gf::game::compat::destroyBlock(region, pos, drops, BlockChangeContext{}); }

void removeBlock(BlockSource& region, BlockPos const& pos) { region.removeBlock(pos, BlockChangeContext{}); }

// ---- items and players ----------------------------------------------------------------------------------

std::optional<ItemStack> makeItem(std::string_view itemId, int count) { return game::items::make(itemId, count); }

void popResource(BlockSource& region, BlockPos const& pos, ItemStack const& stack) {
    if (stack.isNull()) return;
    region.getLevel().getSpawner().spawnItem(region, stack, nullptr, Vec3{pos.x + .5f, pos.y + .5f, pos.z + .5f}, 10);
}

void spawnItemAt(BlockSource& region, float x, float y, float z, ItemStack const& stack) {
    if (stack.isNull()) return;
    region.getLevel().getSpawner().spawnItem(region, stack, nullptr, Vec3{x, y, z}, 10);
}

void give(Player& player, ItemStack const& item) {
    if (item.isNull()) return;
    ItemStack stack = item;
    if (!player.add(stack) || !stack.isNull()) {
        if (!stack.isNull()) player.getLevel().getSpawner().spawnItem(player.getDimensionBlockSource(), stack, nullptr, player.getPosition(), 0);
    }
    player.sendInventory(false);
}

void consumeHeld(Player& player, ItemStack const& contextItem, int count) {
    if (player.isCreative()) return;
    auto& item = const_cast<ItemStack&>(contextItem);
    if (item.isNull()) return;
    item.remove(count);
    if (item.mCount <= 0) item = ItemStack::EMPTY_ITEM();
    player.sendInventory(false);
}

namespace {
std::int64_t& tickCounter() {
    static std::int64_t t = 0;
    return t;
}
struct Scheduled {
    std::int64_t          due;
    std::function<void()> fn;
};
std::vector<Scheduled>& scheduled() {
    static std::vector<Scheduled> s;
    return s;
}
} // namespace

std::int64_t now() { return tickCounter(); }

void schedule(int ticks, std::function<void()> fn) { scheduled().push_back({tickCounter() + std::max(1, ticks), std::move(fn)}); }

void tick() {
    ++tickCounter();
    auto& s = scheduled();
    if (s.empty()) return;
    std::vector<Scheduled> due;
    for (auto it = s.begin(); it != s.end();) {
        if (it->due <= tickCounter()) {
            due.push_back(std::move(*it));
            it = s.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& d : due) d.fn();
}

bool repeatedUse(Player& player, GlobalPos const& pos, bool firstEvent) {
    struct LastUse {
        GlobalPos    pos;
        std::int64_t tick;
    };
    static std::map<std::int64_t, LastUse> last;
    auto uid  = player.getOrCreateUniqueID().rawID;
    auto t    = tickCounter();
    auto it   = last.find(uid);
    bool held = !firstEvent && it != last.end() && it->second.pos == pos && t - it->second.tick < 5;
    last[uid] = {pos, t};
    if (last.size() > 256) last.clear();
    return held;
}

void actionBar(Player& player, std::string const& text) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::SetTitle);
    if (!packet) return;
    auto& title      = static_cast<SetTitlePacket&>(*packet);
    title.mType      = SetTitlePacketPayload::TitleType::Actionbar;
    title.mTitleText = text;
    packet->sendTo(player);
}

// ---- effects ---------------------------------------------------------------------------------------

void sound(BlockSource& region, BlockPos const& pos, std::string const& event, float volume, float pitch) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto dim = level->getDimension(region.getDimensionId()).lock();
    if (!dim) return;
    fx::sound(*dim, event, Vec3{pos.x + .5f, pos.y + .5f, pos.z + .5f}, volume, pitch);
}

void vanillaSound(BlockSource& region, BlockPos const& pos, std::string const& name, float volume, float pitch) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!packet) return;
    auto& pkt   = static_cast<PlaySoundPacket&>(*packet);
    pkt.mName   = name;
    pkt.mPos    = BlockPos{pos.x * 8 + 4, pos.y * 8 + 4, pos.z * 8 + 4};
    pkt.mVolume = volume;
    pkt.mPitch  = pitch;
    pkt.sendTo(pos, region.getDimensionId());
}

void particleAt(BlockSource& region, float x, float y, float z, std::string const& effect) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto dim = level->getDimension(region.getDimensionId()).lock();
    if (!dim) return;
    level->spawnParticleEffect(effect, Vec3{x, y, z}, dim.get());
}

void breakEffect(BlockSource& region, BlockPos const& pos, Block const& block) {
    region.getLevel().broadcastLevelEvent(
        SharedTypes::Legacy::LevelEvent::ParticlesDestroyBlock, Vec3{pos.x + .5f, pos.y + .5f, pos.z + .5f}, static_cast<int>(block.mNetworkId), nullptr
    );
}

void boneMealEffect(BlockSource& region, BlockPos const& pos) {
    particleAt(region, pos.x + .5f, pos.y + .5f, pos.z + .5f, "minecraft:crop_growth_emitter");
    vanillaSound(region, pos, "item.bone_meal.use");
}

namespace {
/// Java's RandomSource over a 64-bit Mersenne twister (the level random is not seeded in Java either).
class LevelRandom final : public RandomSource {
public:
    void         setSeed(std::int64_t s) override { r.seed(static_cast<std::uint64_t>(s)); }
    int          nextInt() override { return static_cast<int>(r() >> 32); }
    int          nextInt(int bound) override { return bound <= 0 ? 0 : static_cast<int>(r() % static_cast<std::uint64_t>(bound)); }
    std::int64_t nextLong() override { return static_cast<std::int64_t>(r()); }
    bool         nextBoolean() override { return (r() >> 63) != 0; }
    float        nextFloat() override { return static_cast<float>(r() >> 40) * 0x1.0p-24f; }
    double       nextDouble() override { return static_cast<double>(r() >> 11) * 0x1.0p-53; }
    double       nextGaussian() override { return std::normal_distribution<double>(0, 1)(r); }
    std::unique_ptr<RandomSource> fork() override {
        auto f = std::make_unique<LevelRandom>();
        f->r.seed(r());
        return f;
    }
    std::unique_ptr<core::jrand::PositionalRandomFactory> forkPositional() override { return nullptr; }
    std::mt19937_64 r{std::random_device{}()};
};
} // namespace

RandomSource& levelRandom() {
    thread_local LevelRandom r;
    return r;
}

} // namespace gf::feature::flora
