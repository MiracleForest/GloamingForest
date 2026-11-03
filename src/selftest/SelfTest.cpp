// Adapted from LiteLDev/Levitate v0.1.0 (src/selftest/SelfTest.cpp), LGPL-3.0-or-later.
#include "selftest/SelfTest.h"

#include <algorithm>
#include <mutex>

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/LoopbackPacketSender.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/Packet.h"
#include "mc/network/packet/BossEventPacket.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "core/Glob.h"
#include "core/VolumeHash.h"
#include "feature/dimension/World.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Commands.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::selftest {

namespace {
bool gRunning       = false;
int  gPlayerCounter = 0;

BlockSource* overworld() { return game::regionOf(0); }

/// The world spawn with a usable y: Bedrock keeps y = 32767 ("find the surface") until the spawn
/// is resolved, so the y is taken from the surface of the spawn column when out of range.
BlockPos spawnSurface() {
    auto level = ll::service::getLevel();
    if (!level) return {};
    BlockPos spawn  = level->getSharedSpawnPos();
    auto*    region = overworld();
    if (region && (spawn.y < region->getMinHeight() || spawn.y >= region->getMaxHeight())) {
        spawn.y = region->hasChunksAt(spawn, 0, false) ? region->getAboveTopSolidBlock(spawn.x, spawn.z, false, false) : -60;
    }
    return spawn;
}

/// The 5-block z row of a dx hundred: units U1..U30 (rows 1..30), W0b units F0..F16 (dx 5000..6699:
/// rows 31..47), W2 (dx 9000..9999: rows 48..57), anything else wraps into 58..63.
int rowOf(int hundreds) {
    if (hundreds >= 0 && hundreds <= 30) return hundreds;
    if (hundreds >= 50 && hundreds <= 66) return 31 + (hundreds - 50);
    if (hundreds >= 90 && hundreds <= 99) return 48 + (hundreds - 90);
    return 58 + (((hundreds % 6) + 6) % 6);
}

std::string bossEventType(BossEventUpdateType t) {
    switch (t) {
    case BossEventUpdateType::Add:
        return "add";
    case BossEventUpdateType::PlayerAdded:
        return "player_added";
    case BossEventUpdateType::Remove:
        return "remove";
    case BossEventUpdateType::PlayerRemoved:
        return "player_removed";
    case BossEventUpdateType::UpdatePercent:
        return "update_percent";
    case BossEventUpdateType::UpdateName:
        return "update_name";
    case BossEventUpdateType::UpdateProperties:
        return "update_properties";
    case BossEventUpdateType::UpdateStyle:
        return "update_style";
    case BossEventUpdateType::Query:
        return "query";
    default:
        return "unknown";
    }
}

// ---- boss bar capture ------------------------------------------------------------------------

std::mutex&                     captureMutex() {
    static std::mutex m;
    return m;
}
std::vector<BossEventCapture*>& captures() {
    static std::vector<BossEventCapture*> v;
    return v;
}

void inspectPacket(Packet const& packet) {
    if (packet.getId() != MinecraftPacketIds::BossEvent) return;
    {
        std::lock_guard lock(captureMutex());
        if (captures().empty()) return;
    }
    auto const& p = static_cast<BossEventPacket const&>(packet);
    BossEvent   e;
    e.bossId  = p.mBossID->rawID;
    e.type    = bossEventType(p.mEventType);
    e.name    = p.getUnredactedName();
    e.percent = p.mHealthPercent;
    e.color   = static_cast<int>(static_cast<uchar>(p.mColor));
    e.overlay = static_cast<int>(static_cast<uchar>(p.mOverlay));
    BossEventCapture::record(e);
}
} // namespace

CommandResult runServerCommand(std::string const& command, int dim, CommandPermissionLevel permission) {
    auto level = ll::service::getLevel();
    if (!level) return {false, 0, "no level"};
    ServerCommandOrigin origin("gloamingforest-selftest", level->asServer(), permission, DimensionType(dim));
    // the caller gets the reply in the result: no "Command error:" log line (mod/Commands.h); the
    // framework's own clean-up removes ticking areas that may not exist on every run
    struct Quiet {
        Quiet() { ++commands::detail::quietCommandErrors; }
        ~Quiet() { --commands::detail::quietCommandErrors; }
    } quiet;
    auto out = ll::command::CommandRegistrar::getServerInstance().executeCommand(command, origin);
    CommandResult r{out.mSuccessCount > 0, static_cast<int>(out.mSuccessCount), {}};
    for (auto const& m : out.mMessages) {
        if (!r.messages.empty()) r.messages += "; ";
        r.messages += m.mMessageId;
        for (auto const& p : m.mParams) r.messages += " " + p;
    }
    return r;
}

LL_TYPE_INSTANCE_HOOK(
    SelftestBossToClientHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    static_cast<void (LoopbackPacketSender::*)(UserEntityIdentifierComponent const*, Packet const&)>(&LoopbackPacketSender::$sendToClient),
    void,
    UserEntityIdentifierComponent const* user,
    Packet const&                        packet
) {
    inspectPacket(packet);
    origin(user, packet);
}

LL_TYPE_INSTANCE_HOOK(
    SelftestBossToNetIdHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    static_cast<void (LoopbackPacketSender::*)(NetworkIdentifier const&, Packet const&, SubClientId)>(&LoopbackPacketSender::$sendToClient),
    void,
    NetworkIdentifier const& id,
    Packet const&            packet,
    SubClientId              sub
) {
    inspectPacket(packet);
    origin(id, packet, sub);
}

LL_TYPE_INSTANCE_HOOK(
    SelftestBossBroadcastHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    static_cast<void (LoopbackPacketSender::*)(Packet const&)>(&LoopbackPacketSender::$sendBroadcast),
    void,
    Packet const& packet
) {
    inspectPacket(packet);
    origin(packet);
}

BossEventCapture::BossEventCapture() {
    std::lock_guard lock(captureMutex());
    captures().push_back(this);
}

BossEventCapture::~BossEventCapture() {
    std::lock_guard lock(captureMutex());
    std::erase(captures(), this);
}

std::vector<BossEvent> BossEventCapture::events() const {
    std::lock_guard lock(captureMutex());
    return mEvents;
}

void BossEventCapture::clear() {
    std::lock_guard lock(captureMutex());
    mEvents.clear();
}

void BossEventCapture::record(BossEvent const& e) {
    std::lock_guard lock(captureMutex());
    for (auto* c : captures()) c->mEvents.push_back(e);
}

// ---- context -------------------------------------------------------------------------------------

struct Context::Restore {
    int                       dim = 0;
    core::BlockBox            box;
    std::vector<Block const*> blocks; ///< y, z, x order (as `forEach`)
};

namespace {
template <class Fn>
void forEach(core::BlockBox const& b, Fn&& fn) {
    for (int y = b.minY; y <= b.maxY; ++y)
        for (int z = b.minZ; z <= b.maxZ; ++z)
            for (int x = b.minX; x <= b.maxX; ++x) fn(x, y, z);
}
} // namespace

Context::Context(std::string name) : mName(std::move(name)) {}

namespace {
/// A simulated player of a test, looked up again by its unique id (nullptr when it left the level).
SimulatedPlayer* findTestPlayer(std::int64_t id) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto* p = level->getPlayer(ActorUniqueID{id});
    // only simulated players are stored in mPlayers, and unique ids are never reused in a run
    return p && p->isSimulated() ? static_cast<SimulatedPlayer*>(p) : nullptr;
}
} // namespace

Context::~Context() {
    for (auto id : mPlayers) {
        if (auto* p = findTestPlayer(id)) p->simulateDisconnect();
    }
    for (auto const& pos : mBlocks) {
        if (auto* r = game::regionOf(pos.dim)) game::clearBlock(*r, game::blockPos(pos));
    }
    // flat areas back to what they were, newest first
    for (auto it = mRestores.rbegin(); it != mRestores.rend(); ++it) {
        auto& r      = **it;
        auto* region = game::regionOf(r.dim);
        if (!region) continue;
        std::size_t i = 0;
        forEach(r.box, [&](int x, int y, int z) {
            Block const* want = r.blocks[i++];
            BlockPos     p{x, y, z};
            if (want && region->hasChunksAt(p, 0, false) && &region->getBlock(p) != want) game::setBlock(*region, p, *want);
        });
    }
    for (auto const& [name, dim] : mTickingAreas) runServerCommand("tickingarea remove " + name, dim);
}

SimulatedPlayer& Context::spawnPlayer(std::string const& name) {
    auto level = ll::service::getLevel();
    if (!level) fail("no level loaded");
    core::GlobalPos at = mHasOrigin ? mOrigin : core::GlobalPos{0, spawnSurface().x, spawnSurface().y, spawnSurface().z};
    auto            pos = Vec3{static_cast<float>(at.x) + 0.5f, static_cast<float>(at.y) + 1.0f, static_cast<float>(at.z) + 0.5f};
    auto            n   = name.empty() ? "gf_test_" + std::to_string(++gPlayerCounter) : name;
    auto            sp  = SimulatedPlayer::create(n, pos, DimensionType(at.dim));
    if (!sp) fail("could not spawn a simulated player");
    mPlayers.push_back(sp->getOrCreateUniqueID().rawID);
    return *sp;
}

Player& Context::player() {
    for (auto id : mPlayers) {
        if (auto* p = findTestPlayer(id)) return *p;
    }
    return spawnPlayer();
}

core::GlobalPos Context::testPos(int dx, int dz) const {
    core::GlobalPos origin = mHasOrigin ? mOrigin : core::GlobalPos{0, spawnSurface().x, spawnSurface().y, spawnSurface().z};
    // x = 5 + dx % 100 east of the origin (a larger dx is farther from a player at the origin),
    // one 5-block row in z per hundred of dx (see rowOf), kept loaded by the run's ticking areas.
    int col = ((dx % 100) + 100) % 100;
    int hundreds = (dx - col) / 100;
    return {origin.dim, origin.x + 5 + col, origin.y, origin.z + dz - 64 + rowOf(hundreds) * 5};
}

BlockSource* Context::region() const { return game::regionOf(mDim); }

ll::coro::CoroTask<core::BlockBox> Context::flatArea(int dx, int size, int height) {
    if (size > kMaxFlatAreaSize) {
        info("flatArea size " + std::to_string(size) + " clamped to " + std::to_string(kMaxFlatAreaSize)
             + " (a Bedrock ticking area holds at most 100 chunks)");
    }
    size   = std::clamp(size, 1, kMaxFlatAreaSize);
    height = std::clamp(height, 1, 128);
    auto           at = testPos(dx);
    core::BlockBox box{at.x, at.y, at.z, at.x + size - 1, at.y + height - 1, at.z + size - 1};
    // Keep it loaded while the test runs (it may reach outside the run's ticking areas). Bedrock
    // allows 10 ticking areas per world (the run holds 3) and refuses the add beyond that: the
    // area would then silently unload and the restore below would skip its blocks.
    // names gf_selftest_area_1..7 per test: bounded, so a run can remove what a crashed one left
    std::string area = "gf_selftest_area_" + std::to_string(mTickingAreas.size() + 1);
    auto        added = runServerCommand(
        "tickingarea add " + std::to_string(box.minX) + " 0 " + std::to_string(box.minZ) + " " + std::to_string(box.maxX) + " 0 "
            + std::to_string(box.maxZ) + " " + area + " true",
        mDim
    );
    if (!added.ok) {
        fail(
            "flatArea(" + std::to_string(dx) + ", " + std::to_string(size) + "): the ticking area was refused ("
            + added.messages + "); Bedrock allows 10 per world (the run holds 3), so a test may hold at most "
            + std::to_string(kMaxFlatAreasPerTest) + " flat areas"
        );
    }
    mTickingAreas.emplace_back(area, mDim);
    require(co_await ensureLoaded(at), "flat area loaded");
    auto* region = this->region();
    require(region != nullptr, "test dimension available");
    bool loaded = false;
    for (int i = 0; i < 200 && !loaded; ++i) {
        loaded = true;
        for (int x = box.minX; loaded && x <= box.maxX + 15; x += 16)
            for (int z = box.minZ; loaded && z <= box.maxZ + 15; z += 16)
                loaded = region->hasChunksAt(BlockPos{std::min(x, box.maxX), box.minY, std::min(z, box.maxZ)}, 0, false);
        if (!loaded) co_await ll::chrono::ticks(1);
    }
    require(loaded, "flatArea(" + std::to_string(dx) + ", " + std::to_string(size) + "): every chunk of the area loaded within 10 s");
    core::BlockBox full = box;
    full.minY           = box.minY - 1; // the ground layer
    auto restore        = std::make_unique<Restore>();
    restore->dim        = mDim;
    restore->box        = full;
    restore->blocks.reserve(static_cast<std::size_t>(full.volume()));
    forEach(full, [&](int x, int y, int z) {
        BlockPos p{x, y, z};
        restore->blocks.push_back(region->hasChunksAt(p, 0, false) ? &region->getBlock(p) : nullptr);
    });
    mRestores.push_back(std::move(restore));
    auto const* grass = game::blockWith("minecraft:grass_block");
    auto const* air   = game::blockWith("minecraft:air");
    require(grass && air, "vanilla blocks available");
    forEach(full, [&](int x, int y, int z) {
        BlockPos     p{x, y, z};
        Block const* want = y == full.minY ? grass : air;
        if (region->hasChunksAt(p, 0, false) && &region->getBlock(p) != want) game::setBlock(*region, p, *want);
    });
    co_return box;
}

ll::coro::CoroTask<bool> Context::inTwilight() {
    auto dim = world::twilight();
    if (!dim) co_return false;
    int id = static_cast<int>(*dim);
    auto& p  = player();
    p.teleport(Vec3{0.5f, 128.f, 0.5f}, DimensionType(id));
    for (int i = 0; i < 200; ++i) {
        co_await ll::chrono::ticks(1);
        auto* region = game::regionOf(id);
        if (!region || !region->hasChunksAt(BlockPos{0, 64, 0}, 1, false)) continue;
        int y      = region->getAboveTopSolidBlock(0, 0, false, false);
        mDim       = id;
        mOrigin    = {id, 0, y, 0};
        mHasOrigin = true;
        info("in the Twilight Forest (dimension " + std::to_string(id) + "), origin y " + std::to_string(y));
        co_return true;
    }
    co_return false;
}

bool Context::setBlock(core::GlobalPos const& pos, std::string const& id, nlohmann::json const& states) {
    auto* region = game::regionOf(pos.dim);
    if (!region) return false;
    auto const* block = game::blockWithStates(id, states);
    if (!block) return false;
    mBlocks.push_back(pos);
    return game::setBlock(*region, game::blockPos(pos), *block);
}

std::string Context::blockAt(core::GlobalPos const& pos) const {
    auto* region = game::regionOf(pos.dim);
    return region ? game::blockIdAt(*region, game::blockPos(pos)) : std::string{};
}

ll::coro::CoroTask<bool> Context::ensureLoaded(core::GlobalPos pos) {
    auto& p = player();
    p.teleport(
        Vec3{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 2.f, static_cast<float>(pos.z) + 0.5f},
        DimensionType(pos.dim)
    );
    for (int i = 0; i < 100; ++i) {
        if (auto* region = game::regionOf(pos.dim)) {
            if (region->hasChunksAt(game::blockPos(pos), 2, false)) co_return true;
        }
        co_await ll::chrono::ticks(1);
    }
    co_return false;
}

void Context::cleanupBlock(core::GlobalPos const& pos) { mBlocks.push_back(pos); }

std::string Context::snapshotVolume(core::BlockBox const& box) const {
    core::VolumeHasher h;
    auto*              region = this->region();
    if (!region) return {};
    forEach(box, [&](int x, int y, int z) {
        BlockPos p{x, y, z};
        if (!region->hasChunksAt(p, 0, false)) {
            h.add(x - box.minX, y - box.minY, z - box.minZ, "<unloaded>");
            return;
        }
        auto const& b = region->getBlock(p);
        h.add(x - box.minX, y - box.minY, z - box.minZ, b.getTypeName(), game::statesOf(b));
    });
    return h.hex();
}

int Context::give(Player& player, ItemStack const& item, int slot) {
    auto& inv = player.getInventory();
    if (slot < 0) {
        for (int i = 0; i < inv.getContainerSize(); ++i) {
            if (inv.getItem(i).isNull()) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0 || slot >= inv.getContainerSize()) return -1;
    inv.setItem(slot, item);
    player.sendInventory(false);
    return slot;
}

int Context::give(std::string const& id, int count) {
    auto stack = game::items::make(id, count);
    if (!stack) return -1;
    return give(player(), *stack);
}

BossEventCapture& Context::captureBossEvent() {
    if (!mBossCapture) mBossCapture = std::make_unique<BossEventCapture>();
    return *mBossCapture;
}

ll::coro::CoroTask<> Context::ticks(int n) { co_await ll::chrono::ticks(std::max(n, 1)); }

void Context::check(bool condition, std::string const& message) {
    if (!condition) mFailures.push_back(message);
}

void Context::require(bool condition, std::string const& message) {
    if (!condition) fail(message);
}

void Context::fail(std::string const& message) {
    mFailures.push_back(message);
    throw Abort(message);
}

void Context::info(std::string const& message) const { log().info("SELFTEST INFO {}: {}", mName, message); }

bool running() { return gRunning; }

namespace {
/// The ticking areas of a run, relative to the spawn (`Context::testPos`: x spawn+5..+104, z rows
/// spawn-64..+255). A Bedrock ticking area holds at most 100 chunks, so the rows are split in three.
struct TestArea {
    char const* name;
    int         x0, z0, x1, z1;
};
constexpr TestArea kTestAreas[] = {
    {"gf_selftest_0", -16, -64, 120, 32 },
    {"gf_selftest_1", -16, 33,  120, 129},
    {"gf_selftest_2", -16, 130, 120, 255},
};

/// Keeps the test area loaded for the whole run, so tests never race the chunk loading of freshly
/// spawned players and entities a test spawns are not unloaded halfway through it.
ll::coro::CoroTask<> loadTestArea() {
    auto level = ll::service::getLevel();
    if (!level) co_return;
    BlockPos spawn = level->getSharedSpawnPos();
    spawn.y        = 0; // y is irrelevant for the ticking area (it spans the whole height)
    auto at = [&](int dx, int dz) { return std::to_string(spawn.x + dx) + " 0 " + std::to_string(spawn.z + dz); };
    // ticking areas persist with the world: drop the ones a crashed run left, or they would eat into
    // Bedrock's 10 per world (failures of these removes are expected and ignored)
    std::vector<int> dims{0};
    if (auto tw = world::twilight()) dims.push_back(static_cast<int>(*tw));
    for (int dim : dims) {
        for (auto const& a : kTestAreas) runServerCommand(std::string("tickingarea remove ") + a.name, dim);
        for (int i = 1; i <= Context::kMaxFlatAreasPerTest; ++i) runServerCommand("tickingarea remove gf_selftest_area_" + std::to_string(i), dim);
    }
    for (auto const& a : kTestAreas) {
        auto r = runServerCommand("tickingarea add " + at(a.x0, a.z0) + " " + at(a.x1, a.z1) + " " + a.name + " true");
        if (!r.ok) log().warn("selftest: ticking area {} was refused ({}); tests in it may not stay loaded", a.name, r.messages);
    }
    for (int i = 0; i < 300; ++i) {
        auto* region = overworld();
        bool  loaded = region != nullptr;
        for (auto const& a : kTestAreas) {
            for (int dx = a.x0; loaded && dx <= a.x1; dx += 16) {
                for (int dz = a.z0; loaded && dz <= a.z1; dz += 16) {
                    loaded = region->hasChunksAt(BlockPos{spawn.x + dx, spawn.y, spawn.z + dz}, 0, false);
                }
            }
        }
        if (loaded) co_return;
        co_await ll::chrono::ticks(1);
    }
    log().warn("selftest: the test area around the spawn did not load in time");
}
} // namespace

void runAll(std::string const& glob, std::function<void(std::string const&)> out) {
    if (gRunning) {
        log().info("SELFTEST BUSY");
        if (out) out("SELFTEST BUSY");
        return;
    }
    std::vector<TestEntry const*> tests;
    for (auto const& t : TestRegistry::entries()) {
        if (glob.empty() || core::globMatch(glob, t.name)) tests.push_back(&t);
    }
    std::sort(tests.begin(), tests.end(), [](auto* a, auto* b) { return a->name < b->name; });
    gRunning  = true;
    auto emit = [out](std::string const& line) {
        log().info("{}", line);
        if (out) out(line);
    };
    emit("SELFTEST START count=" + std::to_string(tests.size()));
    ll::coro::keepThis([tests = std::move(tests), emit]() -> ll::coro::CoroTask<> {
        int pass = 0, fail = 0;
        co_await loadTestArea();
        for (auto const* t : tests) {
            std::string reason;
            {
                Context ctx(t->name);
                auto    result = co_await t->run(ctx).tryGet();
                if (!ctx.failures().empty()) {
                    for (auto const& f : ctx.failures()) reason += (reason.empty() ? "" : "; ") + f;
                } else if (!result) {
                    reason = "exception: " + result.error().message();
                }
            } // cleanup (players, blocks, areas) before the next test
            if (reason.empty()) {
                ++pass;
                emit("SELFTEST PASS " + t->name);
            } else {
                ++fail;
                emit("SELFTEST FAIL " + t->name + ": " + reason);
            }
            co_await ll::chrono::ticks(1);
        }
        for (auto const& a : kTestAreas) runServerCommand(std::string("tickingarea remove ") + a.name);
        emit("SELFTEST DONE pass=" + std::to_string(pass) + " fail=" + std::to_string(fail));
        gRunning = false;
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

class SelfTestModule {
public:
    void install() {
        SelftestBossToClientHook::hook();
        SelftestBossToNetIdHook::hook();
        SelftestBossBroadcastHook::hook();
    }
    void uninstall() {
        SelftestBossBroadcastHook::unhook();
        SelftestBossToNetIdHook::unhook();
        SelftestBossToClientHook::unhook();
    }
};

} // namespace gf::selftest

GF_REGISTER_MODULE_ORDERED(gf::selftest::SelfTestModule, gf::ModuleOrder::Services);
