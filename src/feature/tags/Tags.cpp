// The tag part of contract "data" (F9): upstream's tags and the vanilla / c: tags they add to, loaded from
// the mod's data tree (data/<ns>/tags/<registry>/<path>.json, imported by F1 and unioned with the
// units' fragments by assemble, rule R6), nested tags resolved, the vanilla members of vanilla tags
// answered by the bridge (VanillaTags.h). The data's vanilla ids are Java's: they become the game's
// Bedrock ids at load (JavaIds.h); queries take Bedrock ids.
//
// A tag's members are Java's TagLoader result: its own entries, plus every member of every tag it
// includes, whether the include is a "#tag" of the data or one of the bridge's (Java's own nesting:
// #minecraft:overworld_carver_replaceables holds #minecraft:dirt, so upstream's uberous_soil, which upstream's
// data adds to minecraft:dirt, is an overworld carver replaceable), minus NeoForge's "remove"
// entries. Tags that include each other (Java rejects the cycle) share their members; a removed
// #tag is never merged into the remover (a removal whose tag includes the remover back is dropped).
//
// Every tag is resolved once per load into an immutable snapshot; queries read the snapshot
// without a lock (a thread keeps the snapshot it last read until a reload publishes a new one), so
// world generation can ask per block while /gloamingforest reload swaps the data. Bedrock's native
// block / item tags are read into the snapshot on the server thread (at the data load and at every
// world load): no query, from any thread, touches the game's registries.
#include "feature/tags/Tags.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/service/Bedrock.h"

#include "feature/ext/item/Tags.h"
#include "feature/item/DataFiles.h"
#include "feature/tags/JavaIds.h"
#include "feature/tags/TagsInternal.h"
#include "feature/tags/VanillaTags.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::tags {

namespace {

namespace data = feature::item::data;

constexpr Registry kRegistries[] = {Registry::Block,         Registry::Item,            Registry::EntityType, Registry::Biome,
                                    Registry::Fluid,         Registry::DamageType,      Registry::Enchantment, Registry::Structure,
                                    Registry::BannerPattern, Registry::PaintingVariant, Registry::GameEvent,  Registry::Instrument};
constexpr std::size_t kRegistryCount = std::size(kRegistries);

std::size_t indexOf(Registry registry) { return static_cast<std::size_t>(registry); }

/// Heterogeneous lookup by string_view (no copy of the query).
struct StringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};
using StringSet = std::unordered_set<std::string, StringHash, std::equal_to<>>;
template <class V>
using StringMap = std::unordered_map<std::string, V, StringHash, std::equal_to<>>;

struct RawTag {
    std::vector<std::string> values;   ///< ids and "#tags", in file order
    std::vector<std::string> removes;  ///< NeoForge's "remove": ids and "#tags" taken out of the tag
    std::vector<std::string> optional; ///< entries with "required": false
};
using RawDir = StringMap<RawTag>;

/// One tag resolved (for one registry: with the bridge, or data only).
struct Resolved {
    StringSet                                    data;       ///< data members of the tag and its includes (Bedrock ids), removals applied
    std::vector<std::string>                     dataSorted; ///< the same, sorted
    StringSet                                    listed;     ///< ids the bridge's rules of the tag and its includes list
    std::vector<vanilla::Rule const*>            patterns;   ///< those rules that also match by pattern
    std::vector<std::string>                     bridged;    ///< the bridged tags reached
    std::vector<std::shared_ptr<Resolved const>> nested;     ///< included tags with removals of their own: they answer for themselves
    StringSet                                    removed;    ///< NeoForge "remove" ids
    std::vector<std::shared_ptr<Resolved const>> removedTags; ///< NeoForge "remove" #tags
    bool                                         usesRegistry = false; ///< members need the registry's ids (bridge rules here or nested)

    [[nodiscard]] bool opaque() const { return !removed.empty() || !removedTags.empty(); }

    /// Membership of a namespaced id (`native`: the snapshot's index of Bedrock's native tags).
    [[nodiscard]] bool has(std::optional<Registry> registry, std::string const& id, vanilla::NativeIndex const* native) const {
        if (!removed.empty() && removed.contains(id)) return false;
        for (auto const& t : removedTags)
            if (t->has(registry, id, native)) return false;
        if ((!data.empty() && data.contains(id)) || (!listed.empty() && listed.contains(id))) return true;
        if (registry)
            for (auto const* rule : patterns)
                if (vanilla::matchesPattern(*rule, id, native)) return true;
        for (auto const& n : nested)
            if (n->has(registry, id, native)) return true;
        return false;
    }

    /// The data members here and in the nested tags (candidates of members()).
    void collectData(std::set<std::string>& out) const {
        out.insert(dataSorted.begin(), dataSorted.end());
        for (auto const& n : nested) n->collectData(out);
    }
};

using ResolvedMap = StringMap<std::shared_ptr<Resolved const>>;

/// The loaded data and every tag resolved: immutable once published (but for the members cache).
struct Snapshot {
    StringMap<RawDir>                             raw;     ///< registry dir -> tag -> entries
    std::size_t                                   files = 0;
    std::array<ResolvedMap, kRegistryCount>       bridged; ///< by registry: data and bridge
    StringMap<ResolvedMap>                        dataOnly; ///< by dir: data members only
    /// Bedrock's native tags of the game's blocks and items, read on the server thread (null before
    /// a level exists): queries never touch the game's registries, from any thread.
    std::shared_ptr<vanilla::NativeIndex const>   native;
    mutable std::mutex                            membersMutex;
    mutable StringMap<std::shared_ptr<std::vector<std::string> const>> members; ///< "<registry>|<tag>", with the bridge

    [[nodiscard]] Resolved const* find(Registry registry, std::string_view tag) const {
        auto const& m  = bridged[indexOf(registry)];
        auto        it = m.find(tag);
        return it == m.end() ? nullptr : it->second.get();
    }
    [[nodiscard]] Resolved const* findData(std::string_view dir, std::string_view tag) const {
        auto d = dataOnly.find(dir);
        if (d == dataOnly.end()) return nullptr;
        auto it = d->second.find(tag);
        return it == d->second.end() ? nullptr : it->second.get();
    }
};

/// Java 1.20 and older spelled some tag directories in the plural.
std::string normalizeDir(std::string dir) {
    static StringMap<std::string> const plural{
        {"blocks", "block"}, {"items", "item"}, {"entity_types", "entity_type"}, {"fluids", "fluid"}, {"game_events", "game_event"},
    };
    if (auto it = plural.find(dir); it != plural.end()) return it->second;
    return dir;
}

/// "worldgen/biome/x/y" -> ("worldgen/biome", "x/y"); "block/x" -> ("block", "x").
std::pair<std::string, std::string> splitRegistry(std::string const& rel) {
    auto first = rel.find('/');
    if (first == std::string::npos) return {};
    auto head = rel.substr(0, first);
    if (head == "worldgen") {
        auto second = rel.find('/', first + 1);
        if (second == std::string::npos) return {};
        return {rel.substr(0, second), rel.substr(second + 1)};
    }
    return {normalizeDir(head), rel.substr(first + 1)};
}

/// The `tags::Registry` of a (normalized) registry directory.
std::optional<Registry> registryOf(std::string_view dir) {
    for (auto r : kRegistries)
        if (dir == registryName(r)) return r;
    return std::nullopt;
}

std::string stripHash(std::string_view tag) {
    if (tag.starts_with('#')) tag.remove_prefix(1);
    return data::withNamespace(tag);
}

/// The tag id of a query without '#', namespaced: a view of `tag` itself when it has a namespace.
std::string_view tagKey(std::string const& tag, std::string& buffer) {
    std::string_view t = tag;
    if (t.starts_with('#')) t.remove_prefix(1);
    if (t.find(':') != std::string_view::npos) return t;
    buffer = data::withNamespace(t);
    return buffer;
}

/// The id of a query, namespaced (`id` itself when it has a namespace).
std::string const& idKey(std::string const& id, std::string& buffer) {
    if (id.find(':') != std::string::npos) return id;
    buffer = data::withNamespace(id);
    return buffer;
}

/// Reads a tag file's entries ("values" or "remove") into `out`.
void readEntries(nlohmann::json const& doc, char const* key, std::vector<std::string>& out, std::vector<std::string>* optional) {
    auto it = doc.find(key);
    if (it == doc.end() || !it->is_array()) return;
    for (auto const& v : *it) {
        if (v.is_string()) out.push_back(v.get<std::string>());
        else if (v.is_object() && v.contains("id") && v["id"].is_string()) {
            auto id = v["id"].get<std::string>();
            out.push_back(id);
            if (optional && !v.value("required", true)) optional->push_back(id);
        }
    }
}

/// True the first time `key` is warned about in this process: the tag warnings repeat neither per
/// registry variant nor per /gloamingforest reload.
bool warnOnce(std::string key) {
    static std::mutex            m;
    static std::set<std::string> warned;
    std::lock_guard              lock(m);
    return warned.insert(std::move(key)).second;
}

/// Resolves the tags of one directory for one registry variant (the bridge's includes and rules
/// only with a registry). Tarjan's strongly connected components over the include edges: tags that
/// include each other (a cycle) are one component and share its members. Components are resolved
/// after every component they include and every tag they remove (NeoForge's "remove" #tag is a
/// dependency, never a shared membership): a removal whose tag reaches the remover back through
/// includes (a cycle Java rejects) is dropped with a warning.
class Builder {
public:
    /// `quiet`: no warnings (the selftests' private snapshots of synthetic data).
    Builder(std::optional<Registry> registry, std::string dir, RawDir const* raw, bool quiet, vanilla::NativeIndex const* native)
    : mRegistry(registry), mDir(std::move(dir)), mRaw(raw), mQuiet(quiet), mNative(native) {}

    std::shared_ptr<Resolved const> get(std::string const& tag) {
        if (!mIndex.contains(tag)) connect(tag);
        finishComponent(mComp.at(tag));
        return mDone.at(tag);
    }
    ResolvedMap take() { return std::move(mDone); }

private:
    struct Edges {
        std::vector<std::string> include; ///< data "#tags" and the bridge rule's includes
        std::vector<std::string> remove;  ///< "#tags" of NeoForge's "remove"
    };
    enum class State : char { Open, Pending, Done };

    RawTag const* rawOf(std::string const& tag) const {
        if (!mRaw) return nullptr;
        auto it = mRaw->find(tag);
        return it == mRaw->end() ? nullptr : &it->second;
    }
    vanilla::Rule const* ruleOf(std::string const& tag) const { return mRegistry ? vanilla::find(*mRegistry, tag) : nullptr; }
    bool                 firstWarning(std::string key) const { return !mQuiet && warnOnce(std::move(key)); }

    Edges edgesOf(std::string const& tag) const {
        Edges e;
        auto  add = [](std::vector<std::string>& to, std::string t) {
            if (std::find(to.begin(), to.end(), t) == to.end()) to.push_back(std::move(t));
        };
        if (auto const* rule = ruleOf(tag))
            for (auto& t : vanilla::includes(*rule)) add(e.include, std::move(t));
        if (auto const* raw = rawOf(tag)) {
            for (auto const& v : raw->values)
                if (v.starts_with('#')) add(e.include, stripHash(v));
            for (auto const& v : raw->removes)
                if (v.starts_with('#')) add(e.remove, stripHash(v));
        }
        return e;
    }

    /// Tarjan over the include edges only: records the components (resolved by finishComponent).
    void connect(std::string const& v) {
        int index = mNext++;
        mIndex[v] = index;
        mLow[v]   = index;
        mStack.push_back(v);
        mOnStack.insert(v);
        auto e = edgesOf(v);
        for (auto const& w : e.include) {
            if (!mIndex.contains(w)) {
                connect(w);
                mLow[v] = std::min(mLow[v], mLow[w]);
            } else if (mOnStack.contains(w)) {
                mLow[v] = std::min(mLow[v], mIndex[w]);
            }
        }
        mEdges.emplace(v, std::move(e)); // finish() reads them (map nodes keep their address)
        if (mLow[v] != index) return;
        auto                     id = static_cast<int>(mComponents.size());
        std::vector<std::string> scc;
        while (true) {
            auto w = std::move(mStack.back());
            mStack.pop_back();
            mOnStack.erase(w);
            mComp[w] = id;
            bool root = w == v;
            scc.push_back(std::move(w));
            if (root) break;
        }
        mComponents.push_back(std::move(scc));
        mState.push_back(State::Open);
    }

    /// Whether component `c` reaches a component being resolved through include edges (then a
    /// removal of a tag of `c` would depend on itself).
    bool reachesPending(int c) const {
        std::vector<int>        todo{c};
        std::unordered_set<int> seen{c};
        while (!todo.empty()) {
            int x = todo.back();
            todo.pop_back();
            if (mState[x] == State::Pending) return true;
            if (mState[x] == State::Done) continue; // its include closure is resolved
            for (auto const& v : mComponents[x])
                for (auto const& w : mEdges.at(v).include)
                    if (int y = mComp.at(w); seen.insert(y).second) todo.push_back(y);
        }
        return false;
    }

    /// Resolves component `c` after its dependencies (the components it includes and the tags it
    /// removes).
    void finishComponent(int c) {
        if (mState[c] != State::Open) return;
        mState[c] = State::Pending;
        auto const scc = mComponents[c]; // a copy: connect() below may grow mComponents
        for (auto const& v : scc)
            for (auto const& w : mEdges.at(v).include)
                if (int d = mComp.at(w); d != c) finishComponent(d);
        StringSet removable; ///< the removed #tags resolved before this component
        for (auto const& v : scc) {
            for (auto const& t : mEdges.at(v).remove) {
                if (!mIndex.contains(t)) connect(t);
                int d = mComp.at(t);
                if (reachesPending(d)) { // d == c included: c is pending
                    if (firstWarning(mDir + "|remove|" + v + "|" + t))
                        // the removed tag includes this one back: removing it would empty the tag
                        log().warn("tags: {} tag {} removes #{}, which includes it back (a cycle Java rejects): the removal is ignored", mDir, v, t);
                    continue;
                }
                finishComponent(d);
                removable.insert(t);
            }
        }
        finish(scc, removable);
        mState[c] = State::Done;
    }

    /// One warning per include of `tag` that names neither a tag of the data nor one of the bridge
    /// (Java fails such a tag for a required entry; here it resolves to nothing), so a misspelt or
    /// missing bridge rule shows. Optional ("required": false) data entries stay quiet.
    void warnDangling(std::string const& tag, std::vector<std::string> const& includes) {
        auto const* raw = rawOf(tag);
        for (auto const& w : includes) {
            if (rawOf(w) || ruleOf(w)) continue;
            bool optional = raw && std::any_of(raw->optional.begin(), raw->optional.end(), [&](std::string const& o) { return o.starts_with('#') && stripHash(o) == w; });
            if (optional || !firstWarning(mDir + "|dangling|" + tag + "|" + w)) continue;
            log().warn("tags: {} tag {} includes #{}, which has no data and no bridge rule: it adds nothing", mDir, tag, w);
        }
    }

    static void addUnique(std::vector<std::shared_ptr<Resolved const>>& to, std::shared_ptr<Resolved const> const& r) {
        if (std::find(to.begin(), to.end(), r) == to.end()) to.push_back(r);
    }

    /// Resolves one component (its includes outside it, and the #tags in `removable`, are resolved
    /// already).
    void finish(std::vector<std::string> const& scc, StringSet const& removable) {
        StringSet inScc(scc.begin(), scc.end());
        std::vector<Edges const*> edges;
        for (auto const& v : scc) edges.push_back(&mEdges.at(v));
        bool cyclic = scc.size() > 1 || std::find(edges[0]->include.begin(), edges[0]->include.end(), scc[0]) != edges[0]->include.end();
        if (cyclic) {
            std::vector<std::string> sorted(scc.begin(), scc.end());
            std::sort(sorted.begin(), sorted.end());
            std::string names;
            for (auto const& t : sorted) names += (names.empty() ? "" : ", ") + t;
            if (firstWarning(mDir + "|" + names))
                log().warn("tags: {} tags include each other ({}): Java rejects the cycle, here they share their members (and the removals of each)", mDir, names);
        }
        auto base = std::make_shared<Resolved>();
        for (std::size_t i = 0; i < scc.size(); ++i) {
            auto const& v = scc[i];
            warnDangling(v, edges[i]->include);
            if (auto const* raw = rawOf(v))
                for (auto const& value : raw->values)
                    if (!value.starts_with('#'))
                        for (auto& id : javaids::bedrock(mDir, data::withNamespace(value))) base->data.insert(std::move(id));
            if (auto const* rule = ruleOf(v)) {
                base->bridged.push_back(v);
                for (auto& id : vanilla::listedIds(*rule)) base->listed.insert(std::move(id));
                if (vanilla::hasPatterns(*rule)) base->patterns.push_back(rule);
            }
            for (auto const& w : edges[i]->include) {
                if (inScc.contains(w)) continue;
                auto const& sub = mDone.at(w);
                if (sub->opaque()) {
                    addUnique(base->nested, sub);
                    for (auto const& b : sub->bridged)
                        if (std::find(base->bridged.begin(), base->bridged.end(), b) == base->bridged.end()) base->bridged.push_back(b);
                    continue;
                }
                base->data.insert(sub->data.begin(), sub->data.end());
                base->listed.insert(sub->listed.begin(), sub->listed.end());
                for (auto const* p : sub->patterns)
                    if (std::find(base->patterns.begin(), base->patterns.end(), p) == base->patterns.end()) base->patterns.push_back(p);
                for (auto const& b : sub->bridged)
                    if (std::find(base->bridged.begin(), base->bridged.end(), b) == base->bridged.end()) base->bridged.push_back(b);
                for (auto const& n : sub->nested) addUnique(base->nested, n);
            }
        }
        base->usesRegistry = !base->listed.empty() || !base->patterns.empty()
                          || std::any_of(base->nested.begin(), base->nested.end(), [](auto const& n) { return n->usesRegistry; });
        auto sortData = [](Resolved& r) {
            r.dataSorted.assign(r.data.begin(), r.data.end());
            std::sort(r.dataSorted.begin(), r.dataSorted.end());
        };
        sortData(*base);
        // NeoForge's "remove": taken out of the tag (and of the tags that include it). The tags of a
        // cycle share their members, so the removals of each apply to all of them.
        std::shared_ptr<Resolved> own;
        for (auto const& v : scc) {
            auto const* raw = rawOf(v);
            if (!raw || raw->removes.empty()) continue;
            for (auto const& entry : raw->removes) {
                if (entry.starts_with('#')) {
                    auto t = stripHash(entry);
                    if (!removable.contains(t)) continue; // dropped (finishComponent warned)
                    if (!own) own = std::make_shared<Resolved>(*base);
                    addUnique(own->removedTags, mDone.at(t));
                } else {
                    if (!own) own = std::make_shared<Resolved>(*base);
                    for (auto& id : javaids::bedrock(mDir, data::withNamespace(entry))) own->removed.insert(std::move(id));
                }
            }
        }
        std::shared_ptr<Resolved const> shared = base;
        if (own) {
            std::erase_if(own->data, [&](std::string const& id) {
                return own->removed.contains(id)
                    || std::any_of(own->removedTags.begin(), own->removedTags.end(), [&](auto const& t) { return t->has(mRegistry, id, mNative); });
            });
            sortData(*own);
            shared = own;
        }
        for (auto const& v : scc) mDone[v] = shared;
    }

    std::optional<Registry>          mRegistry;
    std::string                      mDir;
    RawDir const*                    mRaw;
    bool                             mQuiet;
    vanilla::NativeIndex const*      mNative;
    ResolvedMap                      mDone;
    StringMap<Edges>                 mEdges;
    StringMap<int>                   mIndex;
    StringMap<int>                   mLow;
    StringMap<int>                   mComp;       ///< tag -> component
    std::vector<std::vector<std::string>> mComponents; ///< by component: its tags
    std::vector<State>               mState;      ///< by component
    std::vector<std::string>         mStack;
    StringSet                        mOnStack;
    int                              mNext = 0;
};

/// Reads the data tree (`withData`: the mod's, or the one below `base`) and resolves every tag (the
/// data's and the bridge's) into a new snapshot. Without the data it holds the bridge's rules only:
/// the answers before the first load.
std::shared_ptr<Snapshot const> build(bool withData, std::shared_ptr<vanilla::NativeIndex const> native, std::filesystem::path const* base = nullptr) {
    auto       s     = std::make_shared<Snapshot>();
    s->native        = std::move(native);
    auto const files = !withData ? std::vector<data::File>{} : base ? data::list("tags", *base) : data::list("tags");
    for (auto const& f : files) {
        auto [dir, path] = splitRegistry(f.rel);
        if (dir.empty() || path.empty()) continue;
        auto doc = data::read(f.path);
        if (!doc.is_object()) continue;
        auto& tag = s->raw[dir][f.ns + ":" + path];
        if (doc.value("replace", false)) tag = {};
        readEntries(doc, "values", tag.values, &tag.optional);
        readEntries(doc, "remove", tag.removes, nullptr);
        ++s->files;
    }
    bool const quiet = base != nullptr;
    for (auto registry : kRegistries) {
        std::string dir = registryName(registry);
        auto        d   = s->raw.find(dir);
        Builder     b(registry, dir, d == s->raw.end() ? nullptr : &d->second, quiet, s->native.get());
        if (d != s->raw.end())
            for (auto const& [tag, raw] : d->second) (void)b.get(tag);
        for (auto const& tag : vanilla::knownTags(registry)) (void)b.get(tag);
        s->bridged[indexOf(registry)] = b.take();
    }
    // upstream's own registries ("gloaming", ...): data members only
    for (auto const& [dir, tags] : s->raw) {
        if (registryOf(dir)) continue;
        Builder b(std::nullopt, dir, &tags, quiet, s->native.get());
        for (auto const& [tag, raw] : tags) (void)b.get(tag);
        s->dataOnly[dir] = b.take();
    }
    return s;
}

/// The published snapshot. Readers take it without a lock through their thread's copy, which they
/// refresh (under the mutex) only when the generation changed: a reload publishes a whole new
/// snapshot, and a thread still reading the old one keeps it alive.
struct Published {
    std::mutex                      mutex; ///< guards `snapshot`; serialises loads
    std::shared_ptr<Snapshot const> snapshot = build(false, nullptr); ///< the bridge alone until the data loads
    std::atomic<std::uint64_t>      generation{1};
};

Published& published() {
    static Published p;
    return p;
}

/// A private snapshot this thread reads instead of the published one (internal::ScopedSnapshot:
/// selftests' synthetic data, never seen by other threads), and the ones it shadows.
struct Private {
    std::shared_ptr<Snapshot const>    snapshot;
    std::shared_ptr<std::atomic<bool>> alive; ///< false once its ScopedSnapshot is gone (on any thread)
    std::shared_ptr<Private>           outer;
};
thread_local std::shared_ptr<Private> tPrivate;

/// Drops this thread's private snapshots whose scope ended (also one destroyed on another thread:
/// the owner thread lets go of it at its next query).
void dropEndedPrivate() {
    while (tPrivate && !tPrivate->alive->load(std::memory_order_acquire)) tPrivate = tPrivate->outer;
}

std::shared_ptr<Snapshot const> const& current() {
    if (tPrivate) [[unlikely]] {
        dropEndedPrivate();
        if (tPrivate) return tPrivate->snapshot;
    }
    struct Local {
        std::shared_ptr<Snapshot const> snapshot;
        std::shared_ptr<Snapshot const> previous; ///< one refresh back: a caller up the stack may still read it
        std::uint64_t                   generation = 0;
    };
    thread_local Local local;
    auto&              p = published();
    if (local.generation != p.generation.load(std::memory_order_acquire)) {
        std::lock_guard lock(p.mutex);
        local.previous   = std::move(local.snapshot);
        local.snapshot   = p.snapshot;
        local.generation = p.generation.load(std::memory_order_relaxed);
    }
    return local.snapshot;
}

/// Serialises loads. Timed: a load that never returns (a hung thread) must not block the server
/// thread's next /gloamingforest reload forever.
std::timed_mutex& loadMutex() {
    static std::timed_mutex m;
    return m;
}

/// The index of Bedrock's native tags the next builds use (read on the server thread; guarded by
/// loadMutex).
std::shared_ptr<vanilla::NativeIndex const>& nativeIndex() {
    static std::shared_ptr<vanilla::NativeIndex const> index;
    return index;
}

/// Builds and publishes a new snapshot. `refreshNative` (server thread only): re-reads Bedrock's
/// native tags from the game's registries first (the data reload and every world load); otherwise
/// the last index is kept (a reload from another thread never touches the game's registries).
void load(bool refreshNative) {
    std::unique_lock serial(loadMutex(), std::defer_lock);
    if (!serial.try_lock_for(std::chrono::seconds(30))) {
        log().error("tags: another tag load has held the lock for 30 s (hung?): this load is skipped");
        return;
    }
    if (refreshNative) nativeIndex() = vanilla::buildNativeIndex();
    if (data::rootOverridden()) {
        // a selftest's synthetic data tree: it reads its tags through a private ScopedSnapshot, and
        // every other thread (world generation) keeps the published tags of the mod's data
        log().info("tags: a selftest's data root is set: the published tags stay");
        return;
    }
    auto next  = build(true, nativeIndex());
    auto files = next->files;
    {
        auto&           p = published();
        std::lock_guard lock(p.mutex);
        p.snapshot = std::move(next);
        p.generation.fetch_add(1, std::memory_order_release);
    }
    log().info("tags: {} tag files loaded ({} native tag members)", files, vanilla::nativeIndexSize(nativeIndex().get()));
}

/// A world load (the client fills its registries per world): re-reads Bedrock's native tags, and
/// republishes only when they changed (BDS: the data load at mod enable already read them).
void onWorldReady() {
    auto index = vanilla::buildNativeIndex();
    {
        std::unique_lock serial(loadMutex(), std::defer_lock);
        if (!serial.try_lock_for(std::chrono::seconds(30))) {
            log().error("tags: another tag load has held the lock for 30 s (hung?): the native tags are not refreshed");
            return;
        }
        if (vanilla::sameNativeIndex(nativeIndex().get(), index.get())) return;
        nativeIndex() = std::move(index);
    }
    load(false);
}

// the data reload (server thread) and every world load
[[maybe_unused]] bool const kRegistered = (data::onReload([] { load(true); }), game::db::onWorldReady(&onWorldReady), true);

std::string membersKey(Registry registry, std::string_view tag) { return std::to_string(indexOf(registry)) + "|" + std::string(tag); }

} // namespace

bool has(Registry registry, std::string const& tag, std::string const& id) {
    if (tag.empty() || id.empty()) return false;
    std::string tagBuffer, idBuffer;
    auto const& snapshot = current();
    auto const* r        = snapshot->find(registry, tagKey(tag, tagBuffer));
    return r && r->has(registry, idKey(id, idBuffer), snapshot->native.get());
}

std::vector<std::string> members(Registry registry, std::string const& tag) {
    if (tag.empty()) return {};
    std::string tagBuffer;
    auto        snapshot = current(); // a copy: members may take a while
    auto        key      = tagKey(tag, tagBuffer);
    auto const* r        = snapshot->find(registry, key);
    if (!r) return {};
    if (!r->usesRegistry) {
        std::set<std::string> all;
        r->collectData(all);
        std::vector<std::string> out;
        for (auto const& id : all)
            if (r->has(registry, id, snapshot->native.get())) out.push_back(id);
        return out;
    }
    auto cacheKey = membersKey(registry, key);
    {
        std::lock_guard lock(snapshot->membersMutex);
        if (auto it = snapshot->members.find(cacheKey); it != snapshot->members.end()) return *it->second;
    }
    // the candidates: the data members and the registry's ids (the bridge's rules decide)
    std::set<std::string> candidates;
    r->collectData(candidates);
    auto ids = vanilla::registryIds(registry);
    candidates.insert(ids.begin(), ids.end());
    auto list = std::make_shared<std::vector<std::string>>();
    for (auto const& id : candidates)
        if (r->has(registry, id, snapshot->native.get())) list->push_back(id);
    // the bridge enumerates the game's registries: cache only once a level exists
    if (!ll::service::getLevel()) return *list;
    std::lock_guard lock(snapshot->membersMutex);
    snapshot->members[cacheKey] = list;
    return *list;
}

char const* registryName(Registry registry) {
    switch (registry) {
    case Registry::Block:
        return "block";
    case Registry::Item:
        return "item";
    case Registry::EntityType:
        return "entity_type";
    case Registry::Biome:
        return "worldgen/biome";
    case Registry::Fluid:
        return "fluid";
    case Registry::DamageType:
        return "damage_type";
    case Registry::Enchantment:
        return "enchantment";
    case Registry::Structure:
        return "worldgen/structure";
    case Registry::BannerPattern:
        return "banner_pattern";
    case Registry::PaintingVariant:
        return "painting_variant";
    case Registry::GameEvent:
        return "game_event";
    case Registry::Instrument:
        return "instrument";
    }
    return "";
}

namespace internal {

std::vector<std::string> registryDirs() {
    std::vector<std::string> out;
    for (auto const& [dir, tags] : current()->raw) out.push_back(dir);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> tagsOf(std::string const& dir) {
    std::vector<std::string> out;
    auto const&              snapshot = current();
    if (auto it = snapshot->raw.find(dir); it != snapshot->raw.end())
        for (auto const& [tag, raw] : it->second) out.push_back(tag);
    std::sort(out.begin(), out.end());
    return out;
}

std::optional<Registry> registryOfDir(std::string const& dir) { return registryOf(normalizeDir(dir)); }

// A directory of a `tags::Registry` goes through has / members (the vanilla bridge included, the
// same snapshot entries); upstream's own registries ("gloaming", ...) have data members only.
bool hasIn(std::string const& dir, std::string const& tag, std::string const& id) {
    if (auto r = registryOfDir(dir)) return has(*r, tag, id);
    if (tag.empty() || id.empty()) return false;
    std::string tagBuffer, idBuffer;
    auto const& snapshot = current();
    auto const* r        = snapshot->findData(normalizeDir(dir), tagKey(tag, tagBuffer));
    return r && r->has(std::nullopt, idKey(id, idBuffer), snapshot->native.get());
}

std::vector<std::string> membersIn(std::string const& dir, std::string const& tag) {
    if (auto r = registryOfDir(dir)) return members(*r, tag);
    return dataMembers(dir, tag);
}

// The data members every include reaches (a vanilla registry's bridged tags bring their data too),
// none of the bridge's own.
std::vector<std::string> dataMembers(std::string const& dir, std::string const& tag) {
    if (tag.empty()) return {};
    std::string             tagBuffer;
    auto                    snapshot = current();
    auto                    key      = tagKey(tag, tagBuffer);
    auto                    registry = registryOfDir(dir);
    Resolved const*         r        = registry ? snapshot->find(*registry, key) : snapshot->findData(normalizeDir(dir), key);
    if (!r) return {};
    std::set<std::string> all;
    r->collectData(all);
    std::vector<std::string> out;
    for (auto const& id : all)
        if (r->has(registry, id, snapshot->native.get())) out.push_back(id);
    return out;
}

std::vector<std::string> bridgedOf(Registry registry, std::string const& tag) {
    std::string tagBuffer;
    auto        snapshot = current();
    auto const* r        = snapshot->find(registry, tagKey(tag, tagBuffer));
    if (!r) return {};
    auto out = r->bridged;
    std::sort(out.begin(), out.end());
    return out;
}

std::size_t fileCount() { return current()->files; }

std::uint64_t generation() { return published().generation.load(std::memory_order_acquire); }

void reload() { load(false); }

ScopedSnapshot::ScopedSnapshot(std::filesystem::path const& root)
: mAlive(std::make_shared<std::atomic<bool>>(true)),
  mThread(std::this_thread::get_id()) {
    tPrivate = std::make_shared<Private>(Private{build(true, current()->native, &root), mAlive, std::move(tPrivate)});
}

ScopedSnapshot::~ScopedSnapshot() {
    mAlive->store(false, std::memory_order_release);
    if (std::this_thread::get_id() == mThread) {
        dropEndedPrivate();
        return;
    }
    // a scope that spanned a thread switch (a co_await): the owner thread drops the snapshot at its
    // next query, so it never keeps answering from the synthetic data
    log().error("tags: a ScopedSnapshot was destroyed on another thread than the one that made it (it must not span a co_await)");
}

} // namespace internal

namespace ext {
bool                     hasIn(std::string const& dir, std::string const& tag, std::string const& id) { return internal::hasIn(dir, tag, id); }
std::vector<std::string> membersIn(std::string const& dir, std::string const& tag) { return internal::membersIn(dir, tag); }
std::vector<std::string> bedrockIds(std::string const& dir, std::string const& javaId) {
    return javaids::bedrock(normalizeDir(dir), data::withNamespace(javaId));
}
std::string bedrockId(std::string const& dir, std::string const& javaId) { return javaids::primary(normalizeDir(dir), data::withNamespace(javaId)); }
} // namespace ext

} // namespace gf::tags
