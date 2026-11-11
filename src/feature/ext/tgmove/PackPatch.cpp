// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// The pack patch extension (PackPatch.h): two hooks on the reads of resource pack files,
// ResourcePack::getResource and ResourcePackManager::load (outside the W0 contracts; disclosed for
// A8). Client build only: the server build has no patches and installs no hook.
#include "feature/ext/tgmove/PackPatch.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <exception>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

#include "mod/Log.h"
#include "mod/Module.h"

#ifdef GF_CLIENT
#include "ll/api/memory/Hook.h"

#include "mc/deps/core/file/Path.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/resources/ResourcePack.h"
#include "mc/resources/ResourcePackManager.h"
#endif

namespace gf::ext::packpatch {

namespace {

struct Patch {
    std::string suffix; ///< lower case, '/' separators
    PatchFn     fn;
};

struct Patches {
    std::mutex            mutex;
    std::vector<Patch>    list;
    std::set<std::string> logged; ///< one log line per patched path
    std::atomic<int>      count{0};
    std::atomic<int>      size{0}; ///< patches registered
};
Patches& patches() {
    static auto* p = new Patches; // read by loader threads up to process exit
    return *p;
}

std::string normalized(std::string_view path) {
    std::string s(path);
    for (char& c : s) c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// Whether `path` ends with `suffix` (lower case, '/'), ignoring case and separators, without copying.
bool endsWith(std::string_view path, std::string const& suffix) {
    if (path.size() < suffix.size()) return false;
    std::size_t off = path.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        char c = path[off + i];
        c      = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c != suffix[i]) return false;
    }
    return true;
}

/// Applies the patches whose suffix matches `path` to the document `content`.
void apply(std::string_view path, std::string& content) {
    auto& p = patches();
    if (p.size.load(std::memory_order_acquire) == 0) return; // nothing registered: no lock per read
    std::vector<PatchFn> fns;
    {
        std::lock_guard lock(p.mutex);
        for (auto const& patch : p.list)
            if (endsWith(path, patch.suffix)) fns.push_back(patch.fn);
    }
    if (fns.empty()) return;
    try {
        auto doc = nlohmann::json::parse(content, nullptr, false, true);
        if (doc.is_discarded()) return;
        bool changed = false;
        for (auto const& fn : fns) changed = fn(doc) || changed;
        if (!changed) return;
        content = doc.dump(2);
        p.count.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard lock(p.mutex);
        if (p.logged.insert(std::string(path)).second) log().info("pack patch: patched {}", path);
    } catch (std::exception const& e) {
        log().error("pack patch: {} failed: {}", path, e.what());
    }
}

#ifdef GF_CLIENT
LL_TYPE_INSTANCE_HOOK(
    PackPatchResourceHook,
    ll::memory::HookPriority::Normal,
    ResourcePack,
    &ResourcePack::getResource,
    bool,
#if GF_LL_VERSION >= 2660
    ::Core::PathView const& resourceName,
#else
    ::Core::Path const& resourceName,
#endif
    std::string&        resourceStream,
    int                 subpackIndex
) {
    bool ok = origin(resourceName, resourceStream, subpackIndex);
    if (ok) apply(resourceName.getUtf8StringView(), resourceStream);
    return ok;
}

#if GF_LL_VERSION < 2660 // 26.60's client does not export ResourcePackManager::load: packs only
LL_TYPE_INSTANCE_HOOK(
    PackPatchManagerHook,
    ll::memory::HookPriority::Normal,
    ResourcePackManager,
    static_cast<bool (ResourcePackManager::*)(::ResourceLocation const&, std::string&) const>(&ResourcePackManager::$load),
    bool,
    ::ResourceLocation const& location,
    std::string&              resourceStream
) {
    bool ok = origin(location, resourceStream);
    if (ok) {
        Core::PathBuffer<std::string> const& path = location.mPath;
        apply(path.value, resourceStream);
    }
    return ok;
}
#endif
#endif

class PackPatchModule {
public:
    // the resource packs load at game start, before the mods are enabled
    void load() { hook(); }
    void install() { hook(); }
    void uninstall() {
#ifdef GF_CLIENT
        if (mHooked) {
            PackPatchResourceHook::unhook();
#if GF_LL_VERSION < 2660
            PackPatchManagerHook::unhook();
#endif
            mHooked = false;
        }
#endif
    }

private:
    void hook() {
#ifdef GF_CLIENT
        if (mHooked) return;
        PackPatchResourceHook::hook();
#if GF_LL_VERSION < 2660
        PackPatchManagerHook::hook();
#endif
        mHooked = true;
#endif
    }
    bool mHooked = false;
};

} // namespace

void addJsonPatch(std::string pathSuffix, PatchFn fn) {
    auto& p = patches();
    std::lock_guard lock(p.mutex);
    p.list.push_back(Patch{normalized(pathSuffix), std::move(fn)});
    p.size.store(static_cast<int>(p.list.size()), std::memory_order_release);
}

int patchedCount() { return patches().count.load(std::memory_order_relaxed); }

} // namespace gf::ext::packpatch

GF_REGISTER_MODULE(gf::ext::packpatch::PackPatchModule);
