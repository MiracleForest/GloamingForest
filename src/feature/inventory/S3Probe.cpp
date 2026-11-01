// Spike S3 (F10): probes of the data-driven block entity / container components that decide the
// backend of gf::inventory (docs/spikes/S3.md). The probe blocks gf_s3:* exist only in the spike
// scenario (tools/e2e/server/scenarios/combat.s3.prelaunch.ps1 adds them); without them the probes
// report that and pass, so they are harmless in "combat.*" and "full" runs.

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_map>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/world/Container.h"
#include "mc/world/DataDrivenBlockContainer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/component/ContainerBlockActorComponent.h"
#include "mc/deps/core/sem_ver/SemVersion.h"
#include "mc/resources/PackLoadRequirement.h"
#include "mc/world/level/block/components/BlockBlockEntityDescription.h"
#include "mc/world/level/block/components/BlockComponentFactory.h"
#include "mc/world/level/block/components/BlockContainerDescription.h"
#include "mc/world/level/storage/AllExperiments.h"
#include "mc/world/level/storage/Experiments.h"
#include "mc/world/level/storage/LevelData.h"

#include "feature/inventory/InventoryInternal.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"
#include "selftest/SelfTest.h"

namespace gf::inventory::s3 {

namespace {

using selftest::Context;

/// "5,6" -> experiment indices forced on by the spike (GF_S3_EXPERIMENTS).
std::vector<int> forcedExperiments() {
    std::vector<int> out;
    if (char const* env = std::getenv("GF_S3_EXPERIMENTS")) {
        std::stringstream ss(env);
        std::string       item;
        while (std::getline(ss, item, ','))
            if (!item.empty()) out.push_back(std::atoi(item.c_str()));
    }
    return out;
}

void forceInto(std::vector<bool>& flags) {
    for (int e : forcedExperiments()) {
        if (e < 0 || e >= static_cast<int>(AllExperiments::Count)) continue;
        if (flags.size() <= static_cast<std::size_t>(e)) flags.resize(static_cast<std::size_t>(e) + 1, false);
        flags[static_cast<std::size_t>(e)] = true;
    }
}

} // namespace

} // namespace gf::inventory::s3

// The level data's experiments are read from level.dat (an existing world): force the spike's
// flags in right after (the "level-data flag" route).
LL_TYPE_INSTANCE_HOOK(
    S3ExperimentsTagHook,
    ll::memory::HookPriority::Normal,
    Experiments,
    &Experiments::getTagData,
    void,
    CompoundTag const& tag
) {
    origin(tag);
    gf::inventory::s3::forceInto(this->mExperimentData.get());
    gf::log().info("S3: experiments forced into the level data from level.dat");
}

namespace gf::inventory::s3 {

namespace {

/// The load requirements and release format of a block component ("minecraft:container").
std::string describeComponent(std::unordered_map<std::string, BlockComponentFactory::ComponentMetadata>& map, std::string const& name, bool unlock) {
    auto it = map.find(name);
    if (it == map.end()) return name + ": not registered";
    auto&       meta = it->second;
    std::string out  = name + ": requirements [";
    for (auto const& req : meta.requirements.get()) {
        auto const* raw = reinterpret_cast<std::uint32_t const*>(&req);
        out += "(" + std::to_string(raw[0]) + "," + std::to_string(raw[1] & 0xff) + ")";
    }
    out += "] released ";
    auto const& rel = meta.releasedMinFormatVersion.get();
    out += rel ? std::to_string(rel->mMajor) + "." + std::to_string(rel->mMinor) + "." + std::to_string(rel->mPatch) : std::string("never");
    if (unlock) {
        meta.requirements.get().clear();
        out += " -> requirements cleared";
    }
    return out;
}

} // namespace
} // namespace gf::inventory::s3

// The block component registry: report (and with GF_S3_UNLOCK unlock) the container component.
LL_STATIC_HOOK(
    S3ComponentRegistryHook,
    ll::memory::HookPriority::Normal,
    &BlockComponentFactory::registerAllCerealDescriptions,
    void,
    cereal::ReflectionCtx& ctx
) {
    origin(ctx);
    bool unlock = std::getenv("GF_S3_UNLOCK") != nullptr;
    auto& map   = BlockComponentFactory::setupContextInstanceIfRequired(ctx);
    gf::log().info("S3: {}", gf::inventory::s3::describeComponent(map, "minecraft:container", unlock));
    gf::log().info("S3: {}", gf::inventory::s3::describeComponent(map, "minecraft:block_entity", false));
    gf::log().info("S3: {}", gf::inventory::s3::describeComponent(map, "minecraft:geometry", false));
}

namespace gf::inventory::s3 {

namespace {

std::string experimentsText() {
    auto level = ll::service::getLevel();
    if (!level) return "(no level)";
    auto&       data  = level->getLevelData();
    auto const& flags = data.mExperiments->mExperimentData.get();
    std::string out;
    for (std::size_t i = 0; i < flags.size(); ++i)
        if (flags[i]) out += (out.empty() ? "" : ",") + std::to_string(i);
    return out.empty() ? "none" : out;
}

/// The spike's probe pack is in this run: GF_S3_PROBE (server harness) or the flag file the client
/// harness leaves next to the mod (<mod>/s3-probe.flag).
bool probeEnabled() {
    if (std::getenv("GF_S3_PROBE")) return true;
    try {
        std::error_code ec;
        return std::filesystem::exists(Mod::getInstance().getSelf().getModDir() / "s3-probe.flag", ec);
    } catch (...) {
        return false;
    }
}

/// The container of a data-driven block actor (nullptr: none).
Container* dataDrivenContainer(BlockActor* actor) {
    if (!actor) return nullptr;
    auto* comp = actor->mContainer.get();
    if (!comp) return nullptr;
    return comp->mContainer.get();
}

class S3ProbeModule {
public:
    // Everything here is spike-only: without the probe pack (or the GF_S3_* variables) nothing is
    // hooked or logged.
    void load() {
        if (!probeEnabled() && forcedExperiments().empty() && !std::getenv("GF_S3_UNLOCK")) return;
        S3ComponentRegistryHook::hook();
        mRegistryHooked = true;
        // the spike's probe block with a data-driven block actor is a 27-slot container
        if (probeEnabled()) declare({.block = "gf_s3:entity", .rows = 3, .slots = 27, .title = "container.chest", .skin = ""});
        if (!forcedExperiments().empty()) {
            S3ExperimentsTagHook::hook();
            mHooked = true;
        }
    }
    void install() {}
    void uninstall() {
        if (mRegistryHooked) S3ComponentRegistryHook::unhook();
        if (mHooked) S3ExperimentsTagHook::unhook();
        mHooked = mRegistryHooked = false;
    }

private:
    bool mHooked         = false;
    bool mRegistryHooked = false;
};

constexpr char kProbeKey[] = "gf:inventory:s3_probe";

core::GlobalPos probePos(Context& ctx, BlockSource const& region) {
    auto pos = ctx.testPos(6091);
    pos.y    = std::max(pos.y - 3, region.getMinHeight() + 1);
    return pos;
}

} // namespace

} // namespace gf::inventory::s3

GF_REGISTER_MODULE_ORDERED(gf::inventory::s3::S3ProbeModule, gf::ModuleOrder::Contracts);

GF_REGISTER_SELFTEST("combat.s3_probe", [](gf::selftest::Context& ctx) -> ll::coro::CoroTask<> {
    using namespace gf::inventory::s3;
    ctx.info(
        std::string("component names: container='") + BlockContainerDescription::NameID() + "' block_entity='"
        + BlockBlockEntityDescription::NameID() + "', experiments " + experimentsText()
    );
    int dx = 6090;
    for (char const* id : {"gf_s3:container", "gf_s3:entity_container", "gf_s3:entity"}) {
        auto pos = ctx.testPos(dx++);
        ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
        if (!gf::game::blockWith(id)) {
            ctx.info(std::string(id) + ": not registered (no S3 probe pack in this run)");
            continue;
        }
        ctx.require(ctx.setBlock(pos, id), std::string("place ") + id);
        co_await ctx.ticks(2);
        auto* region = ctx.region();
        ctx.require(region != nullptr, "region");
        auto* actor = region->getBlockEntity(gf::game::blockPos(pos));
        std::string what = std::string(id) + ": block actor ";
        if (!actor) {
            ctx.info(what + "none");
            continue;
        }
        what += "type " + std::to_string(static_cast<int>(actor->getType()));
        auto* c = dataDrivenContainer(actor);
        if (!c) {
            ctx.info(what + ", no container component");
            continue;
        }
        what += ", container size " + std::to_string(c->getContainerSize()) + " max stack " + std::to_string(c->getMaxStackSize());
        if (auto stick = gf::game::items::make("minecraft:stick", 5)) {
            c->setItem(4, *stick);
            what += ", slot 4 after set: " + gf::game::items::idOf(c->getItem(4)) + " x" + std::to_string(gf::game::items::countOf(c->getItem(4)));
        }
        ctx.info(what);
    }
});

GF_REGISTER_SELFTEST("combat.s3_persist_write", [](gf::selftest::Context& ctx) -> ll::coro::CoroTask<> {
    using namespace gf::inventory::s3;
    if (!gf::game::blockWith("gf_s3:entity") || !gf::inventory::declared("gf_s3:entity")) {
        ctx.info("no S3 probe pack in this run: nothing to write");
        co_return;
    }
    char const* id = "gf_s3:entity";
    auto*       region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto pos = probePos(ctx, *region);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    region = ctx.region();
    auto bp = gf::game::blockPos(pos);
    ctx.require(gf::game::setBlock(*region, bp, *gf::game::blockWith(id)), "place the probe container");
    co_await ctx.ticks(2);
    region   = ctx.region();
    auto* c  = dataDrivenContainer(region->getBlockEntity(bp));
    ctx.require(c != nullptr, "the probe block has a container");
    auto diamonds = gf::game::items::make("minecraft:diamond", 7);
    ctx.require(diamonds.has_value(), "diamond stack");
    c->setItem(3, *diamonds);
    gf::game::db::put(kProbeKey, {{"pos", {pos.dim, pos.x, pos.y, pos.z}}, {"id", id}});
    ctx.info(std::string("probe container ") + id + " written: slot 3 = 7 diamonds");
});

GF_REGISTER_SELFTEST("combat.s3_persist_read", [](gf::selftest::Context& ctx) -> ll::coro::CoroTask<> {
    using namespace gf::inventory::s3;
    auto marker = gf::game::db::get(kProbeKey);
    if (!marker) {
        ctx.info("experiments " + experimentsText() + "; no S3 probe container written before: nothing to check");
        co_return;
    }
    ctx.info("experiments " + experimentsText());
    gf::game::db::erase(kProbeKey);
    auto at = (*marker)["pos"];
    gf::core::GlobalPos pos{at[0].get<int>(), at[1].get<int>(), at[2].get<int>(), at[3].get<int>()};
    ctx.require(co_await ctx.ensureLoaded(pos), "probe area loaded");
    auto* region = ctx.region();
    auto  bp     = gf::game::blockPos(pos);
    ctx.info("after reload the block is '" + ctx.blockAt(pos) + "'");
    auto* c = dataDrivenContainer(region->getBlockEntity(bp));
    if (!c) ctx.info("after reload: no container");
    else {
        auto const& s = c->getItem(3);
        ctx.info("after reload slot 3 = " + gf::game::items::idOf(s) + " x" + std::to_string(gf::game::items::countOf(s)));
        ctx.check(gf::game::items::idOf(s) == "minecraft:diamond" && gf::game::items::countOf(s) == 7, "the probe container kept its items");
    }
    gf::game::clearBlock(*region, bp);
});
