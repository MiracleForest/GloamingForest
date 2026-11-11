// U30 (finalcastle): helpers of the castle blocks, their scheduled ticks and device tickers (see
// BlocksCommon.h).
#include "feature/finalcastle/BlocksCommon.h"

#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/level/block/BlockSupportType.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/common/Redstone.h"
#include "feature/fx/Fx.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Compat.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::finalcastle::blocks {

bool isDoor(std::string const& blockId) {
    return std::any_of(kDoors.begin(), kDoors.end(), [&](char const* d) { return blockId == d; });
}
bool isField(std::string const& blockId) {
    return std::any_of(kFields.begin(), kFields.end(), [&](char const* d) { return blockId == d; });
}
bool isThorns(std::string const& blockId) {
    return std::any_of(kThorns.begin(), kThorns.end(), [&](char const* d) { return blockId == d; });
}

// ---- directions --------------------------------------------------------------------------------------

char const* dirName(int d) {
    static constexpr std::array<char const*, 6> names{"down", "up", "north", "south", "west", "east"};
    return d >= 0 && d < 6 ? names[static_cast<std::size_t>(d)] : "up";
}
char const* axisName(int axis) { return axis == 0 ? "x" : (axis == 2 ? "z" : "y"); }
int         axisFromName(std::string const& name) { return name == "x" ? 0 : (name == "z" ? 2 : 1); }
std::optional<int> dirFromName(std::string const& name) {
    for (int d = 0; d < 6; ++d)
        if (name == dirName(d)) return d;
    return std::nullopt;
}
BlockPos offset(BlockPos const& p, int d, int n) {
    return BlockPos{p.x + kDx[static_cast<std::size_t>(d)] * n, p.y + kDy[static_cast<std::size_t>(d)] * n, p.z + kDz[static_cast<std::size_t>(d)] * n};
}
std::string sideState(int d) { return std::string("twilightforest:") + dirName(d); }

// ---- level -------------------------------------------------------------------------------------------

Level* level() {
    auto l = ll::service::getLevel();
    return l ? l.as_ptr() : nullptr;
}

std::uint64_t now() {
    auto* l = level();
    return l ? l->getCurrentServerTick().tickID : 0;
}

core::jrand::RandomSource& random() {
    static core::jrand::LegacyRandom r(static_cast<std::int64_t>(std::random_device{}()) << 16 ^ std::random_device{}());
    return r;
}

int dimOf(BlockSource& region) { return static_cast<int>(region.getDimensionId()); }

// ---- blocks ------------------------------------------------------------------------------------------

std::string idAt(BlockSource& region, BlockPos const& pos) { return game::blockIdAt(region, pos); }

std::string textState(Block const& block, char const* state) {
    auto all = game::statesOf(block);
    auto it  = all.find(state);
    return it == all.end() ? std::string{} : it->second;
}

bool boolState(Block const& block, char const* state) { return textState(block, state) == "true"; }

bool boolStateAt(BlockSource& region, BlockPos const& pos, char const* state) { return boolState(region.getBlock(pos), state); }

Json statesJson(Block const& block) {
    Json j = Json::object();
    for (auto const& [k, v] : game::statesOf(block)) {
        if (v == "true" || v == "false") {
            j[k] = v == "true";
            continue;
        }
        char* end = nullptr;
        long  n   = std::strtol(v.c_str(), &end, 10);
        if (!v.empty() && end && *end == '\0') j[k] = static_cast<int>(n);
        else j[k] = v;
    }
    return j;
}

bool set(BlockSource& region, BlockPos const& pos, std::string const& blockId, Json const& states) {
    auto const* b = game::blockWithStates(blockId, states);
    return b && game::setBlock(region, pos, *b);
}

bool setStates(BlockSource& region, BlockPos const& pos, Json const& changes) {
    Block const& b  = region.getBlock(pos);
    auto         id = game::blockId(b);
    Json         s  = statesJson(b);
    for (auto const& [k, v] : changes.items()) s[k] = v;
    auto const* next = game::blockWithStates(id, s);
    if (!next || next == &b) return false;
    return game::setBlock(region, pos, *next);
}

bool isAir(BlockSource& region, BlockPos const& pos) { return region.getBlock(pos).isAir(); }

bool loaded(BlockSource& region, BlockPos const& pos) { return region.hasChunksAt(pos, 0, false); }

bool sturdy(BlockSource& region, BlockPos const& pos, int face) {
    Block const& b = region.getBlock(pos);
    if (b.isAir()) return false;
    return b.canProvideSupport(static_cast<uchar>(face), BlockSupportType::Edge);
}

bool isLeaves(Block const& block) {
    auto const& type = block.getBlockType();
    if ((static_cast<std::uint64_t>(type.mProperties) & static_cast<std::uint64_t>(BlockProperty::Leaves)) != 0) return true;
    return tags::has(tags::Registry::Block, "minecraft:leaves", block.getTypeName());
}

bool isLog(Block const& block) {
    if (block.getBlockType().hasTag(HashedString("log"))) return true;
    return tags::has(tags::Registry::Block, "minecraft:logs", block.getTypeName());
}

bool exceptionForConnection(Block const& block) {
    if (isLeaves(block)) return true;
    std::string const& id = block.getTypeName();
    if (id.find("shulker_box") != std::string::npos) return true;
    return id == "minecraft:barrier" || id == "minecraft:carved_pumpkin" || id == "minecraft:lit_pumpkin" || id == "minecraft:melon_block"
        || id == "minecraft:pumpkin";
}

bool powered(BlockSource& region, BlockPos const& pos) { return redstone::powered(region, pos); }

void destroy(BlockSource& region, BlockPos const& pos, bool drops) { gf::game::compat::destroyBlock(region, pos, drops, BlockChangeContext{}); }

Vec3 center(BlockPos const& p) { return Vec3{p.x + 0.5f, p.y + 0.5f, p.z + 0.5f}; }

// ---- effects -----------------------------------------------------------------------------------------

void sound(BlockSource& region, BlockPos const& pos, char const* event, float volume, float pitch) {
    fx::sound(region.getDimension(), event, center(pos), volume, pitch);
}

void particle(BlockSource& region, char const* particleId, Vec3 const& pos, Vec3 const& velocity) {
    fx::particle(region.getDimension(), particleId, pos, velocity);
}

void levelEvent(BlockSource& region, int event, Vec3 const& pos, int data) {
    region.getLevel().broadcastLevelEvent(static_cast<SharedTypes::Legacy::LevelEvent>(event), pos, data, nullptr);
}

bool creative(Player const& player) { return player.getPlayerGameType() == GameType::Creative; }

// ---- runtime -----------------------------------------------------------------------------------------

namespace {

constexpr char kTicksKey[]   = "gf:finalcastle:ticks";
constexpr char kDevicesKey[] = "gf:finalcastle:devices";

struct Pending {
    std::uint64_t due = 0;
    std::string   blockId;
};

struct Runtime {
    std::map<GlobalPos, Pending>                                                      pending;
    std::set<GlobalPos>                                                               devices;
    std::map<GlobalPos, std::string>                                                  deviceIds;
    std::unordered_map<std::string, BlockFn>                                          tickFns;
    std::unordered_map<std::string, BlockFn>                                          deviceFns;
    std::unordered_map<std::string, std::vector<std::function<void(GlobalPos const&)>>> forgetFns;
    bool                                                                              loadedFromDb = false;
    ll::event::ListenerPtr                                                            listener;
};

Runtime& rt() {
    static Runtime r;
    return r;
}

void forget(GlobalPos const& p) {
    auto& s  = rt();
    auto  it = s.deviceIds.find(p);
    if (it != s.deviceIds.end()) {
        if (auto f = s.forgetFns.find(it->second); f != s.forgetFns.end())
            for (auto const& fn : f->second) fn(p);
        s.deviceIds.erase(it);
    }
    s.devices.erase(p);
}

void save() {
    auto& s = rt();
    if (!game::db::available()) return;
    auto t     = now();
    Json ticks = Json::array();
    for (auto const& [p, pend] : s.pending)
        ticks.push_back({p.dim, p.x, p.y, p.z, pend.blockId, pend.due > t ? static_cast<long long>(pend.due - t) : 0LL});
    game::db::put(kTicksKey, ticks);
    Json devs = Json::array();
    for (auto const& p : s.devices) devs.push_back({p.dim, p.x, p.y, p.z});
    game::db::put(kDevicesKey, devs);
}

void load() {
    auto& s = rt();
    // once per world: tick() may have lazy-loaded (BDS installs after the world exists) and scheduled
    // new ticks before onWorldReady; loading again would drop them (onWorldClosed resets the flag)
    if (s.loadedFromDb) return;
    auto t = now();
    // saved entries never replace live ones
    if (auto j = game::db::get(kTicksKey); j && j->is_array())
        for (auto const& e : *j)
            if (e.is_array() && e.size() == 6)
                s.pending.try_emplace(GlobalPos{e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()},
                                      Pending{t + e[5].get<std::uint64_t>(), e[4].get<std::string>()});
    if (auto j = game::db::get(kDevicesKey); j && j->is_array())
        for (auto const& e : *j)
            if (e.is_array() && e.size() == 4) s.devices.insert(GlobalPos{e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()});
    s.loadedFromDb = true;
}

void tick() {
    auto& s = rt();
    if (!s.loadedFromDb && game::db::available()) load();
    auto t = now();
    // scheduled ticks that are due (a handler may schedule more)
    std::vector<std::pair<GlobalPos, std::string>> due;
    for (auto it = s.pending.begin(); it != s.pending.end();) {
        if (it->second.due <= t) {
            auto* region = game::regionOf(it->first.dim);
            auto  bp     = game::blockPos(it->first);
            if (!region || !loaded(*region, bp)) {
                ++it; // waits for its chunk (Java saves chunk ticks with the chunk)
                continue;
            }
            due.emplace_back(it->first, it->second.blockId);
            it = s.pending.erase(it);
        } else {
            ++it;
        }
    }
    for (auto const& [p, blockId] : due) {
        auto* region = game::regionOf(p.dim);
        if (!region) continue;
        auto         bp    = game::blockPos(p);
        Block const& block = region->getBlock(bp);
        if (game::blockId(block) != blockId) continue; // the block changed: Java drops the tick
        if (auto f = s.tickFns.find(blockId); f != s.tickFns.end()) f->second(*region, bp, block);
    }
    // device tickers
    std::vector<GlobalPos> gone;
    std::vector<GlobalPos> list(s.devices.begin(), s.devices.end());
    for (auto const& p : list) {
        auto* region = game::regionOf(p.dim);
        if (!region) continue;
        auto bp = game::blockPos(p);
        if (!loaded(*region, bp)) continue;
        Block const& block = region->getBlock(bp);
        auto         id    = game::blockId(block);
        auto         f     = s.deviceFns.find(id);
        if (f == s.deviceFns.end()) {
            gone.push_back(p);
            continue;
        }
        if (auto known = s.deviceIds.find(p); known == s.deviceIds.end()) s.deviceIds[p] = id;
        f->second(*region, bp, block);
    }
    for (auto const& p : gone) forget(p);
}

} // namespace

void schedule(BlockSource& region, BlockPos const& pos, int delay) {
    auto& s  = rt();
    auto  p  = game::globalPos(region, pos);
    auto  id = game::blockIdAt(region, pos);
    if (auto it = s.pending.find(p); it != s.pending.end() && it->second.blockId == id) return;
    s.pending[p] = Pending{now() + static_cast<std::uint64_t>(std::max(delay, 0)), id};
}

bool scheduled(GlobalPos const& pos) { return rt().pending.contains(pos); }

void onTick(std::string const& blockId, BlockFn fn) { rt().tickFns[blockId] = std::move(fn); }

void track(BlockSource& region, BlockPos const& pos) {
    auto& s = rt();
    auto  p = game::globalPos(region, pos);
    if (s.devices.insert(p).second) s.deviceIds[p] = game::blockIdAt(region, pos);
}

void onDeviceTick(std::string const& blockId, BlockFn fn) { rt().deviceFns[blockId] = std::move(fn); }

void onForget(std::string const& blockId, std::function<void(GlobalPos const&)> fn) { rt().forgetFns[blockId].push_back(std::move(fn)); }

bool tracked(GlobalPos const& pos) { return rt().devices.contains(pos); }

void installRuntime() {
    auto& s    = rt();
    s.listener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { tick(); });
    game::db::onWorldReady([] { load(); });
    game::db::onWorldSave([] { save(); });
    game::db::onWorldClosed([] {
        auto&                  s = rt();
        std::vector<GlobalPos> all(s.devices.begin(), s.devices.end());
        for (auto const& p : all) forget(p);
        s.pending.clear();
        s.devices.clear();
        s.deviceIds.clear();
        s.loadedFromDb = false;
    });
}

} // namespace gf::feature::finalcastle::blocks
