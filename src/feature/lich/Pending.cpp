// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// U21 (lich): the queue of world edits the lich tower's pieces make after generation (Pending.h).

#include "feature/lich/Pending.h"

#include <map>
#include <mutex>
#include <optional>
#include <vector>

#include "mc/world/level/BlockSource.h"

#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::lich::pending {

namespace {

constexpr char const* kKey      = "gf:lich:pending";
constexpr int         kMaxTries = 60; ///< loaded but not applicable this many times: dropped

struct Entry {
    int         dim = 0;
    BlockPos    pos{0, 0, 0};
    std::string kind;
    Json        data;
    int         tries = 0;
};

struct Queue {
    std::mutex                     mutex;
    std::vector<Entry>             entries;
    std::map<std::string, Apply>   kinds;
    bool                           dirty = false;
};

Queue& queue() {
    static Queue q;
    return q;
}

Json toJson(Entry const& e) { return Json{{"dim", e.dim}, {"pos", {e.pos.x, e.pos.y, e.pos.z}}, {"kind", e.kind}, {"data", e.data}, {"tries", e.tries}}; }

std::optional<Entry> fromJson(Json const& j) {
    if (!j.is_object() || !j.contains("pos") || !j["pos"].is_array() || j["pos"].size() != 3) return std::nullopt;
    Entry e;
    e.dim   = j.value("dim", 0);
    e.pos   = BlockPos{j["pos"][0].get<int>(), j["pos"][1].get<int>(), j["pos"][2].get<int>()};
    e.kind  = j.value("kind", std::string());
    e.data  = j.value("data", Json::object());
    e.tries = j.value("tries", 0);
    return e;
}

} // namespace

void registerKind(std::string const& kind, Apply fn) {
    std::lock_guard lock(queue().mutex);
    queue().kinds[kind] = std::move(fn);
}

thread_local int gSuppressed = 0;

Suppress::Suppress() { ++gSuppressed; }
Suppress::~Suppress() { --gSuppressed; }

void add(int dim, BlockPos const& pos, std::string const& kind, Json data) {
    if (gSuppressed > 0) return;
    std::lock_guard lock(queue().mutex);
    queue().entries.push_back(Entry{dim, pos, kind, std::move(data), 0});
    queue().dirty = true;
}

void tick() {
    std::vector<Entry>           ready;
    std::map<std::string, Apply> kinds;
    {
        std::lock_guard lock(queue().mutex);
        if (queue().entries.empty()) return;
        kinds = queue().kinds;
        auto& all = queue().entries;
        for (auto it = all.begin(); it != all.end();) {
            auto* region = game::regionOf(it->dim);
            if (region && region->hasChunksAt(it->pos, 1, false)) {
                ready.push_back(std::move(*it));
                it = all.erase(it);
            } else ++it;
        }
    }
    if (ready.empty()) return;
    std::vector<Entry> again;
    for (auto& e : ready) {
        auto* region = game::regionOf(e.dim);
        auto  kit    = kinds.find(e.kind);
        bool  done   = false;
        if (region && kit != kinds.end()) {
            try {
                done = kit->second(*region, e.pos, e.data);
            } catch (std::exception const& ex) {
                log().error("lich: pending {} at {} {} {} failed: {}", e.kind, e.pos.x, e.pos.y, e.pos.z, ex.what());
                done = true;
            }
        } else if (kit == kinds.end()) {
            log().warn("lich: unknown pending edit '{}' dropped", e.kind);
            done = true;
        }
        if (!done && ++e.tries < kMaxTries) again.push_back(std::move(e));
    }
    std::lock_guard lock(queue().mutex);
    for (auto& e : again) queue().entries.push_back(std::move(e));
    queue().dirty = true;
}

void load() {
    std::vector<Entry> loaded;
    if (auto j = game::db::get(kKey); j && j->is_array())
        for (auto const& e : *j)
            if (auto entry = fromJson(e)) loaded.push_back(std::move(*entry));
    // edits queued by chunks generated before the world was ready (the client generates its spawn
    // before onWorldReady) stay: the saved ones are added in front of them
    std::lock_guard lock(queue().mutex);
    bool queuedBefore = !queue().entries.empty();
    for (auto& e : queue().entries) loaded.push_back(std::move(e));
    queue().entries = std::move(loaded);
    queue().dirty   = queuedBefore;
}

void clear() {
    std::lock_guard lock(queue().mutex);
    queue().entries.clear();
    queue().dirty = false;
}

void save() {
    Json out = Json::array();
    {
        std::lock_guard lock(queue().mutex);
        if (!queue().dirty) return;
        for (auto const& e : queue().entries) out.push_back(toJson(e));
        queue().dirty = false;
    }
    if (out.empty()) game::db::erase(kKey);
    else game::db::put(kKey, out);
}

std::size_t size() {
    std::lock_guard lock(queue().mutex);
    return queue().entries.size();
}

} // namespace gf::feature::lich::pending
