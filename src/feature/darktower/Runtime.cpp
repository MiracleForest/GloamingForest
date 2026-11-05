// U26 (darktower): scheduled block ticks and device tickers (see Runtime.h).
#include "feature/darktower/Runtime.h"

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/darktower/Util.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::darktower::runtime {

namespace {

constexpr char kTicksKey[]   = "gf:darktower:ticks";
constexpr char kDevicesKey[] = "gf:darktower:devices";

struct Pending {
    std::uint64_t due = 0;
    std::string   blockId;
};

struct State {
    std::map<GlobalPos, Pending>                      pending;
    std::set<GlobalPos>                               devices;
    std::unordered_map<std::string, BlockFn>          tickFns;
    std::unordered_map<std::string, BlockFn>          deviceFns;
    std::unordered_map<std::string, std::vector<std::function<void(GlobalPos const&)>>> forgetFns;
    std::map<GlobalPos, std::string>                  deviceIds; ///< the block a device was tracked as
    bool                                              loadedFromDb = false;
    ll::event::ListenerPtr                            listener;
};

State& st() {
    static State s;
    return s;
}

void forget(GlobalPos const& p) {
    auto& s  = st();
    auto  it = s.deviceIds.find(p);
    if (it != s.deviceIds.end()) {
        if (auto f = s.forgetFns.find(it->second); f != s.forgetFns.end())
            for (auto const& fn : f->second) fn(p);
        s.deviceIds.erase(it);
    }
    s.devices.erase(p);
}

void save() {
    auto& s = st();
    if (!game::db::available()) return;
    auto     t     = now();
    Json     ticks = Json::array();
    for (auto const& [p, pend] : s.pending)
        ticks.push_back({p.dim, p.x, p.y, p.z, pend.blockId, pend.due > t ? static_cast<long long>(pend.due - t) : 0LL});
    game::db::put(kTicksKey, ticks);
    Json devs = Json::array();
    for (auto const& p : s.devices) devs.push_back({p.dim, p.x, p.y, p.z});
    game::db::put(kDevicesKey, devs);
}

void load() {
    auto& s = st();
    s.pending.clear();
    s.devices.clear();
    s.deviceIds.clear();
    auto t = now();
    if (auto j = game::db::get(kTicksKey); j && j->is_array())
        for (auto const& e : *j)
            if (e.is_array() && e.size() == 6)
                s.pending[GlobalPos{e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()}] =
                    Pending{t + e[5].get<std::uint64_t>(), e[4].get<std::string>()};
    if (auto j = game::db::get(kDevicesKey); j && j->is_array())
        for (auto const& e : *j)
            if (e.is_array() && e.size() == 4) s.devices.insert(GlobalPos{e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()});
    s.loadedFromDb = true;
}

void tick() {
    auto& s = st();
    if (!s.loadedFromDb && game::db::available()) load();
    auto t = now();
    // scheduled ticks that are due (Java runs them in due order; a handler may schedule more)
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
        else if (known->second != id) {
            forget(p);
            s.devices.insert(p);
            s.deviceIds[p] = id;
        }
        f->second(*region, bp, block);
    }
    for (auto const& p : gone) forget(p);
}

} // namespace

void schedule(BlockSource& region, BlockPos const& pos, int delay) {
    auto& s = st();
    auto  p = game::globalPos(region, pos);
    if (s.pending.contains(p)) return; // LevelTicks: one pending tick per position and block
    s.pending[p] = Pending{now() + static_cast<std::uint64_t>(std::max(delay, 0)), game::blockIdAt(region, pos)};
}

bool scheduled(GlobalPos const& pos) { return st().pending.contains(pos); }

void onTick(std::string const& blockId, BlockFn fn) { st().tickFns[blockId] = std::move(fn); }

void track(BlockSource& region, BlockPos const& pos) {
    auto& s = st();
    auto  p = game::globalPos(region, pos);
    if (s.devices.insert(p).second) s.deviceIds[p] = game::blockIdAt(region, pos);
}

void onDeviceTick(std::string const& blockId, BlockFn fn) { st().deviceFns[blockId] = std::move(fn); }

void onForget(std::string const& blockId, std::function<void(GlobalPos const&)> fn) { st().forgetFns[blockId].push_back(std::move(fn)); }

bool tracked(GlobalPos const& pos) { return st().devices.contains(pos); }

void install() {
    auto& s    = st();
    s.listener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { tick(); });
    static bool hooked = false;
    if (!hooked) {
        hooked = true;
        game::db::onWorldReady([] { load(); });
        game::db::onWorldSave([] { save(); });
        game::db::onWorldClosed([] {
            auto& s = st();
            std::vector<GlobalPos> all(s.devices.begin(), s.devices.end());
            for (auto const& p : all) forget(p);
            s.pending.clear();
            s.devices.clear();
            s.loadedFromDb = false;
        });
    }
}

void uninstall() {
    auto& s = st();
    if (s.listener) ll::event::EventBus::getInstance().removeListener(s.listener);
    s.listener = {};
    save();
}

} // namespace gf::feature::darktower::runtime
