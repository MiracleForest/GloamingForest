// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Selftests of the synthesized sounds (unit F3 "synth", prefix "synth.", testPos dx 5300..5399).
//
// synth.definitions  the assembled resource pack defines every TF sound event (328, the list
//                    tools/sounds/synth.ps1 generates into SoundEvents.inc) with the expected
//                    number of variants; every synthesized file exists and is a mono Ogg Vorbis
//                    stream; vanilla references exist in the game's vanilla pack (client only).
// synth.play_all     plays every event once, one per tick, to the players of the world: through
//                    PlaySoundPacket on the server (the path of gf::fx::sound), through the game's
//                    own sound engine on the client, where an event the engine does not know fails.
//
// F3 owns no feature directory (docs/spec/owners.tsv): these tests live in the unit's extension
// directory (rule R18) and use only W0 headers plus src/core/synth.

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json.hpp"

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/network/packet/StopSoundPacket.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"

#ifdef GF_CLIENT
#include <windows.h>

#include "ll/api/thread/ClientThreadExecutor.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/sound/SoundEngine.h"
#include "mc/deps/audio/ServerSoundHandle.h"

#include "client/Client.h"
#endif

#include "core/synth/Vorbis.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;

struct SoundEvent {
    char const* id;
    char const* owner;
    int         variants;
};

constexpr SoundEvent kEvents[] = {
#define GF_SOUND_EVENT(id, owner, variants) {id, owner, variants},
#include "feature/ext/synth/SoundEvents.inc"
#undef GF_SOUND_EVENT
};

constexpr char const* kCategories[] = {"ambient", "block", "bottle", "bucket", "hostile", "music", "neutral", "player", "record", "ui", "weather"};

std::filesystem::path resourcePack() {
    return gf::Mod::getInstance().getSelf().getModDir() / "resource_packs" / "GloamingForest_RP";
}

std::vector<std::uint8_t> readBytes(std::filesystem::path const& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

#ifdef GF_CLIENT
/// The game's vanilla resource pack (next to the game executable), empty when not found.
std::filesystem::path vanillaPack() {
    wchar_t buf[MAX_PATH * 4];
    DWORD   n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    if (n == 0) return {};
    auto dir = std::filesystem::path(std::wstring(buf, n)).parent_path() / "data" / "resource_packs" / "vanilla";
    std::error_code ec;
    return std::filesystem::is_directory(dir, ec) ? dir : std::filesystem::path{};
}
#endif

} // namespace

GF_REGISTER_SELFTEST("synth.definitions", [](Context& ctx) {
    auto const pack = resourcePack();
    auto const file = pack / "sounds" / "sound_definitions.json";
    ctx.require(std::filesystem::exists(file), "the resource pack has sounds/sound_definitions.json");
    nlohmann::json doc;
    try {
        std::ifstream in(file);
        doc = nlohmann::json::parse(in);
    } catch (std::exception const& e) {
        ctx.fail(std::string("sound_definitions.json does not parse: ") + e.what());
    }
    ctx.require(doc.contains("sound_definitions") && doc["sound_definitions"].is_object(), "sound_definitions is an object");
    auto const& defs = doc["sound_definitions"];
#ifdef GF_CLIENT
    auto const vanilla = vanillaPack();
    if (vanilla.empty()) ctx.info("vanilla pack not found next to the game: vanilla references not checked");
#endif
    std::set<std::string> checkedFiles; // synthesized files (and, on the client, vanilla sounds) already checked
    int                   synthesized = 0, vanillaRefs = 0, problems = 0;
    auto                  problem = [&](std::string const& what) {
        if (++problems <= 20) ctx.check(false, what);
    };
    for (auto const& ev : kEvents) {
        auto it = defs.find(ev.id);
        if (it == defs.end()) {
            problem(std::string("no sound definition for ") + ev.id + " (owner " + ev.owner + ")");
            continue;
        }
        auto const& def = *it;
        auto        cat = def.value("category", std::string{});
        if (std::find(std::begin(kCategories), std::end(kCategories), cat) == std::end(kCategories)) {
            problem(std::string(ev.id) + ": bad category '" + cat + "'");
        }
        auto const& sounds = def.contains("sounds") ? def["sounds"] : nlohmann::json::array();
        if (!sounds.is_array() || sounds.empty()) {
            problem(std::string(ev.id) + " has no sounds");
            continue;
        }
        if (int(sounds.size()) != ev.variants) {
            problem(std::string(ev.id) + ": " + std::to_string(sounds.size()) + " variants, expected " + std::to_string(ev.variants));
        }
        for (auto const& s : sounds) {
            auto name = s.is_string() ? s.get<std::string>() : s.value("name", std::string{});
            if (name.rfind("sounds/twilightforest/", 0) == 0) {
                ++synthesized;
                if (!checkedFiles.insert(name).second) continue;
                auto path = pack / (name + ".ogg");
                if (!std::filesystem::exists(path)) {
                    problem(std::string(ev.id) + ": missing file " + name + ".ogg");
                    continue;
                }
                auto bytes = readBytes(path);
                auto info  = gf::core::synth::probeOggVorbis(bytes);
                if (!info.valid) problem(name + ".ogg is not a valid Ogg Vorbis file: " + info.error);
                else if (info.channels != 1) problem(name + ".ogg is not mono (3D sounds must be mono)");
                else if (info.samples <= 0) problem(name + ".ogg has no samples");
            } else {
                ++vanillaRefs;
#ifdef GF_CLIENT
                if (!vanilla.empty() && checkedFiles.insert(name).second) {
                    bool found = false;
                    for (char const* ext : {".fsb", ".ogg", ".wav"}) found = found || std::filesystem::exists(vanilla / (name + ext));
                    if (!found) problem(std::string(ev.id) + ": vanilla sound " + name + " is not in the game's vanilla pack");
                }
#endif
            }
        }
    }
    ctx.check(problems == 0, std::to_string(problems) + " sound definition problem(s)");
    ctx.info(
        std::to_string(std::size(kEvents)) + " events, " + std::to_string(checkedFiles.size()) + " distinct sounds checked ("
        + std::to_string(synthesized) + " synthesized and " + std::to_string(vanillaRefs) + " vanilla references)"
    );
});

GF_REGISTER_SELFTEST("synth.play_all", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "a level is loaded");
#ifdef GF_CLIENT
    // the client's own sound engine: an event it cannot resolve returns no sound handle
    struct Result {
        std::mutex                 lock;
        std::vector<std::uint64_t> handles;
        std::uint64_t              control = 0, bogus = 0;
        std::atomic<int>           done{0};
    };
    auto res = std::make_shared<Result>();
    res->handles.assign(std::size(kEvents), 0);
    auto play = [res](std::string name, int index) {
        ll::thread::ClientThreadExecutor::getDefault().execute([res, name = std::move(name), index] {
            std::uint64_t h      = 0;
            auto*         client = gf::client::currentClient();
            auto*         player = gf::client::currentPlayer();
            if (client && player) {
                h = client->getSoundEngine()->play(name, player->getPosition(), 0.25f, 1.0f, std::nullopt);
            }
            std::lock_guard lock(res->lock);
            if (index >= 0) res->handles[std::size_t(index)] = h;
            else if (index == -1) res->control = h;
            else res->bogus = h;
            ++res->done;
        });
    };
    play("random.click", -1);              // a vanilla event: the engine knows it
    play("gf.synth.selftest.no_such_sound", -2); // an unknown event
    for (int i = 0; i < int(std::size(kEvents)); ++i) {
        play(kEvents[i].id, i);
        co_await ctx.ticks(1);
    }
    for (int wait = 0; wait < 100 && res->done.load() < int(std::size(kEvents)) + 2; ++wait) co_await ctx.ticks(1);
    ctx.require(res->done.load() == int(std::size(kEvents)) + 2, "the client thread played every event");
    std::lock_guard lock(res->lock);
    // the engine returns ~0 (or 0) for an event it cannot resolve
    auto known = [](std::uint64_t h) { return h != 0 && h != ~std::uint64_t(0); };
    if (known(res->control) && !known(res->bogus)) {
        // the engine tells known from unknown events: every TF event must be known
        int missing = 0;
        for (std::size_t i = 0; i < std::size(kEvents); ++i) {
            if (!known(res->handles[i]) && ++missing <= 20) ctx.check(false, std::string("the client does not know ") + kEvents[i].id);
        }
        ctx.check(missing == 0, std::to_string(missing) + " event(s) unknown to the client");
    } else {
        ctx.info("the sound engine does not report unknown events (control " + std::to_string(res->control) + ", bogus " + std::to_string(res->bogus) + ")");
    }
    // stop what still plays (the music tracks run for minutes)
    std::vector<std::uint64_t> handles = res->handles;
    ll::thread::ClientThreadExecutor::getDefault().execute([handles = std::move(handles)] {
        auto* client = gf::client::currentClient();
        if (!client) return;
        for (auto h : handles) {
            if (h != 0 && h != ~std::uint64_t(0)) client->getSoundEngine()->stop(h);
        }
    });
    gf::log().info("synth: played {} sound events on the client", std::size(kEvents));
#else
    // the server: PlaySoundPacket to every real player (simulated players receive nothing)
    std::vector<Player*> players;
    level->forEachPlayer([&players](Player& p) {
        if (!p.isSimulated()) players.push_back(&p);
        return true;
    });
    int const targets = int(players.size());
    players.clear(); // never kept across ticks (R16): looked up again every tick
    for (auto const& ev : kEvents) {
        auto lvl = ll::service::getLevel();
        if (!lvl) break;
        lvl->forEachPlayer([&ev](Player& p) {
            if (p.isSimulated()) return true;
            auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
            if (!packet) return true;
            auto& pkt   = static_cast<PlaySoundPacket&>(*packet);
            auto  pos   = p.getPosition();
            pkt.mName   = ev.id;
            pkt.mPos    = BlockPos(int(pos.x * 8), int(pos.y * 8), int(pos.z * 8)); // 1/8 block units
            pkt.mVolume = 0.25f;
            pkt.mPitch  = 1.0f;
            pkt.sendTo(p);
            return true;
        });
        co_await ctx.ticks(1);
    }
    // stop what still plays (the music tracks run for minutes)
    if (auto lvl = ll::service::getLevel()) {
        lvl->forEachPlayer([](Player& p) {
            if (p.isSimulated()) return true;
            for (auto const& ev : kEvents) {
                if (std::string_view(ev.id).rfind("music", 0) != 0) continue;
                auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::StopSound);
                if (!packet) continue;
                auto& pkt            = static_cast<StopSoundPacket&>(*packet);
                pkt.mName            = ev.id;
                pkt.mStopAll         = false;
                pkt.mStopMusicLegacy = false;
                pkt.sendTo(p);
            }
            return true;
        });
    }
    ctx.info("played " + std::to_string(std::size(kEvents)) + " events to " + std::to_string(targets) + " player(s)");
    gf::log().info("synth: played {} sound events to {} player(s)", std::size(kEvents), targets);
#endif
});
