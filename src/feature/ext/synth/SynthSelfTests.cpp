// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Selftests of the synthesized sounds (unit F3 "synth", prefix "synth.", testPos dx 5300..5399).
//
// synth.definitions  the assembled resource pack defines every TF sound event (328) as TF's inventory
//                    says (SoundEvents.inc, generated from TF's sounds.json by
//                    tools/sounds/baseline/generate.ps1, never from the recipes): the category of
//                    TF's SoundSource, TF's subtitle key and TF's variants (count, volume, pitch,
//                    stream); every synthesized file exists and is a mono Ogg Vorbis stream; vanilla
//                    references exist in the game's vanilla packs (client only).
// synth.play_all     plays every event once, one per tick. On the client through the game's own sound
//                    engine: an event it does not know fails, its subtitle must be TF's, a long
//                    synthesized sound must still be playing a quarter second later (a file the
//                    engine cannot decode does not play) and the engine's volume and pitch must be
//                    TF's. On the server through PlaySoundPacket (the path of gf::fx::sound) to every
//                    real player; with none (a BDS without clients) it is skipped and says so.
//
// F3 owns no feature directory (docs/spec/owners.tsv): these tests live in the unit's extension
// directory (rule R18) and use only W0 headers plus src/core/synth.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
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
#include "mc/deps/audio/PlayingSoundAttributes.h"
#include "mc/deps/audio/ServerSoundHandle.h"
#include "mc/locale/I18n.h"
#include "mc/locale/Localization.h"

#include "client/Client.h"
#endif

#include "core/synth/Vorbis.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;

// ---- TF's inventory (SoundEvents.inc) ---------------------------------------------------------------------

struct SoundEvent {
    char const* id;
    char const* owner;
    char const* category;
    char const* subtitle; // "" when TF gives none
    int         count;    // variants
};

struct SoundVariant {
    double volume;
    double pitch;
    bool   stream;
};

constexpr SoundEvent kEvents[] = {
#define GF_SOUND_EVENT(id, owner, category, subtitle, count) {id, owner, category, subtitle, count},
#define GF_SOUND_VARIANT(volume, pitch, stream)
#include "feature/ext/synth/SoundEvents.inc"
#undef GF_SOUND_EVENT
#undef GF_SOUND_VARIANT
};

constexpr SoundVariant kVariants[] = {
#define GF_SOUND_EVENT(id, owner, category, subtitle, count)
#define GF_SOUND_VARIANT(volume, pitch, stream) {volume, pitch, stream},
#include "feature/ext/synth/SoundEvents.inc"
#undef GF_SOUND_EVENT
#undef GF_SOUND_VARIANT
};

constexpr std::size_t variantTotal() {
    std::size_t n = 0;
    for (auto const& e : kEvents) n += std::size_t(e.count);
    return n;
}
static_assert(variantTotal() == std::size(kVariants), "SoundEvents.inc: variant counts and variant lines disagree");

/// The variants of event i in kVariants.
std::vector<SoundVariant> variantsOf(std::size_t index) {
    std::size_t first = 0;
    for (std::size_t i = 0; i < index; ++i) first += std::size_t(kEvents[i].count);
    return {std::begin(kVariants) + std::ptrdiff_t(first), std::begin(kVariants) + std::ptrdiff_t(first + std::size_t(kEvents[index].count))};
}

/// "volume|pitch|stream" keys, sorted (a play picks one of a definition's sounds at random: the order
/// does not matter).
std::vector<std::string> variantKeys(std::vector<SoundVariant> const& vs) {
    std::vector<std::string> out;
    for (auto const& v : vs) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%.4f|%.4f|%d", v.volume, v.pitch, v.stream ? 1 : 0);
        out.emplace_back(buf);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string joined(std::vector<std::string> const& v) {
    std::string s;
    for (auto const& x : v) s += (s.empty() ? "" : ", ") + x;
    return s;
}

#ifdef GF_CLIENT
/// The (volume, pitch) every variant of an event shares, if they all do.
std::optional<std::pair<double, double>> uniformLevel(std::vector<SoundVariant> const& vs) {
    if (vs.empty()) return std::nullopt;
    for (auto const& v : vs) {
        if (std::fabs(v.volume - vs[0].volume) > 1e-9 || std::fabs(v.pitch - vs[0].pitch) > 1e-9) return std::nullopt;
    }
    return std::make_pair(vs[0].volume, vs[0].pitch);
}

// synth.play_all on the client
constexpr float  kPlayVolume = 0.25f; // requested volume and pitch of every play
constexpr float  kPlayPitch  = 1.0f;
constexpr double kLongSec    = 1.0;  // a sound this long (at its pitch) must still be playing at the check
constexpr double kCheckSec   = 0.25; // seconds after the play
constexpr int    kLagTicks   = 6;    // the check is queued this many ticks after the play
#endif

constexpr char const* kCategories[] = {"ambient", "block", "bottle", "bucket", "hostile", "music", "neutral", "player", "record", "ui", "weather"};

// ---- the assembled resource pack ----------------------------------------------------------------------

std::filesystem::path resourcePack() {
    return gf::Mod::getInstance().getSelf().getModDir() / "resource_packs" / "GloamingForest_RP";
}

std::vector<std::uint8_t> readBytes(std::filesystem::path const& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

/// sound_definitions of the assembled pack; an empty object (and the reason) when it cannot be read.
nlohmann::json loadDefinitions(std::string& error) {
    auto const file = resourcePack() / "sounds" / "sound_definitions.json";
    if (!std::filesystem::exists(file)) {
        error = "the resource pack has no sounds/sound_definitions.json";
        return nlohmann::json::object();
    }
    try {
        std::ifstream in(file);
        auto          doc = nlohmann::json::parse(in);
        if (doc.contains("sound_definitions") && doc["sound_definitions"].is_object()) return doc["sound_definitions"];
        error = "sound_definitions.json has no sound_definitions object";
    } catch (std::exception const& e) {
        error = std::string("sound_definitions.json does not parse: ") + e.what();
    }
    return nlohmann::json::object();
}

std::string soundName(nlohmann::json const& s) { return s.is_string() ? s.get<std::string>() : s.value("name", std::string{}); }

SoundVariant soundLevel(nlohmann::json const& s) {
    if (!s.is_object()) return {1.0, 1.0, false};
    return {s.value("volume", 1.0), s.value("pitch", 1.0), s.value("stream", false)};
}

#ifdef GF_CLIENT
/// The game's vanilla resource packs whose sounds a definition may name (vanilla, vanilla_music),
/// next to the game executable; empty when not found.
std::vector<std::filesystem::path> vanillaPacks() {
    wchar_t buf[MAX_PATH * 4];
    DWORD   n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    if (n == 0) return {};
    auto const                         dir = std::filesystem::path(std::wstring(buf, n)).parent_path() / "data" / "resource_packs";
    std::vector<std::filesystem::path> out;
    std::error_code                    ec;
    for (char const* name : {"vanilla", "vanilla_music"}) {
        if (std::filesystem::is_directory(dir / name, ec)) out.push_back(dir / name);
    }
    return out;
}
#endif

} // namespace

GF_REGISTER_SELFTEST("synth.definitions", [](Context& ctx) {
    std::string error;
    auto const  defs = loadDefinitions(error);
    ctx.require(error.empty(), error);
    auto const pack = resourcePack();
#ifdef GF_CLIENT
    auto const vanilla = vanillaPacks();
    if (vanilla.empty()) ctx.info("vanilla packs not found next to the game: vanilla references not checked");
#endif
    std::set<std::string> checkedFiles; // synthesized files (and, on the client, vanilla sounds) already checked
    int                   synthesized = 0, vanillaRefs = 0, problems = 0, subtitles = 0;
    auto                  problem = [&](std::string const& what) {
        if (++problems <= 20) ctx.check(false, what);
    };
    for (std::size_t i = 0; i < std::size(kEvents); ++i) {
        auto const& ev = kEvents[i];
        auto        it = defs.find(ev.id);
        if (it == defs.end()) {
            problem(std::string("no sound definition for ") + ev.id + " (owner " + ev.owner + ")");
            continue;
        }
        auto const& def = *it;
        auto        cat = def.value("category", std::string{});
        if (std::find(std::begin(kCategories), std::end(kCategories), cat) == std::end(kCategories)) {
            problem(std::string(ev.id) + ": bad category '" + cat + "'");
        } else if (cat != ev.category) {
            problem(std::string(ev.id) + ": category '" + cat + "', TF plays it as '" + ev.category + "'");
        }
        auto sub = def.value("subtitle", std::string{});
        if (sub != ev.subtitle) problem(std::string(ev.id) + ": subtitle '" + sub + "', TF's is '" + ev.subtitle + "'");
        if (!sub.empty()) ++subtitles;
        auto const& sounds = def.contains("sounds") ? def["sounds"] : nlohmann::json::array();
        if (!sounds.is_array() || sounds.empty()) {
            problem(std::string(ev.id) + " has no sounds");
            continue;
        }
        std::vector<SoundVariant> got;
        for (auto const& s : sounds) got.push_back(soundLevel(s));
        auto const gotKeys  = variantKeys(got);
        auto const wantKeys = variantKeys(variantsOf(i));
        if (gotKeys.size() != wantKeys.size()) {
            problem(std::string(ev.id) + ": " + std::to_string(gotKeys.size()) + " variants, TF has " + std::to_string(wantKeys.size()));
        } else if (gotKeys != wantKeys) {
            problem(std::string(ev.id) + ": variants (volume|pitch|stream) " + joined(gotKeys) + ", TF's are " + joined(wantKeys));
        }
        for (auto const& s : sounds) {
            auto name = soundName(s);
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
                    for (auto const& dir : vanilla) {
                        for (char const* ext : {".fsb", ".ogg", ".wav"}) found = found || std::filesystem::exists(dir / (name + ext));
                    }
                    if (!found) problem(std::string(ev.id) + ": vanilla sound " + name + " is in none of the game's vanilla packs");
                }
#endif
            }
        }
    }
    ctx.check(problems == 0, std::to_string(problems) + " sound definition problem(s)");
    ctx.info(
        std::to_string(std::size(kEvents)) + " events (" + std::to_string(std::size(kVariants)) + " variants, " + std::to_string(subtitles)
        + " subtitles) as in TF's inventory, " + std::to_string(checkedFiles.size()) + " distinct sounds checked ("
        + std::to_string(synthesized) + " synthesized and " + std::to_string(vanillaRefs) + " vanilla references)"
    );
});

GF_REGISTER_SELFTEST("synth.play_all", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "a level is loaded");
#ifdef GF_CLIENT
    // ---- the client's own sound engine ----
    std::string error;
    auto const  defs = loadDefinitions(error);
    ctx.require(error.empty(), error);
    // the shortest synthesized variant of each event at its pitch (0 when a variant is vanilla: unknown)
    std::vector<double> minSeconds(std::size(kEvents), 0.0);
    for (std::size_t i = 0; i < std::size(kEvents); ++i) {
        auto it = defs.find(kEvents[i].id);
        if (it == defs.end() || !it->contains("sounds")) continue;
        double shortest = 1e9;
        for (auto const& s : (*it)["sounds"]) {
            auto name = soundName(s);
            if (name.rfind("sounds/twilightforest/", 0) != 0) {
                shortest = 0;
                break;
            }
            auto info = gf::core::synth::probeOggVorbis(readBytes(resourcePack() / (name + ".ogg")));
            if (!info.valid || info.sampleRate <= 0) {
                shortest = 0;
                break;
            }
            shortest = std::min(shortest, double(info.samples) / info.sampleRate / std::max(0.01, soundLevel(s).pitch));
        }
        minSeconds[i] = shortest < 1e8 ? shortest : 0;
    }

    using Clock = std::chrono::steady_clock;
    struct Play {
        std::uint64_t              handle = 0;
        Clock::time_point          at{};
        bool                       played = false;
        std::optional<std::string> subtitle, subtitleText; // the engine's, TF's key in the current language
        std::optional<float>       volume, pitch;
        int                        later = -1; // -1 not measured, 0 stopped, 1 still playing
    };
    struct Result {
        std::mutex        lock;
        std::vector<Play> plays;
        Play              control, bogus;
        std::atomic<int>  done{0};
    };
    auto res = std::make_shared<Result>();
    res->plays.resize(std::size(kEvents));
    auto slot = [res](int index) -> Play& { return index >= 0 ? res->plays[std::size_t(index)] : (index == -1 ? res->control : res->bogus); };
    auto play = [res, slot](std::string name, int index) {
        ll::thread::ClientThreadExecutor::getDefault().execute([res, slot, name = std::move(name), index] {
            Play p;
            auto* client = gf::client::currentClient();
            auto* player = gf::client::currentPlayer();
            if (client && player) {
                auto engine  = client->getSoundEngine();
                p.handle     = engine->play(name, player->getPosition(), kPlayVolume, kPlayPitch, std::nullopt);
                p.at         = Clock::now();
                p.played     = true;
                p.subtitle   = engine->getSubtitle(name);
                if (index >= 0 && *kEvents[index].subtitle) p.subtitleText = getI18n().get(kEvents[index].subtitle, getI18n().getCurrentLanguage());
                if (auto attr = engine->tryGetPlayingSoundAttributes(p.handle)) {
                    p.volume = attr->mVolume;
                    p.pitch  = attr->mPitch;
                }
            }
            std::lock_guard lock(res->lock);
            slot(index) = p;
            ++res->done;
        });
    };
    // still playing kCheckSec after the play (measured on the client thread: its tasks may run late)
    // (within [kCheckSec, the sound's length - 0.15 s]; a task that runs outside it measures nothing and
    // a later pass asks again)
    auto checkLater = [res, slot](int index, double seconds) {
        ll::thread::ClientThreadExecutor::getDefault().execute([res, slot, index, seconds] {
            auto* client = gf::client::currentClient();
            std::lock_guard lock(res->lock);
            auto&           p = slot(index);
            if (!client || !p.played || p.later >= 0) return;
            double const elapsed = std::chrono::duration<double>(Clock::now() - p.at).count();
            if (elapsed < kCheckSec || elapsed > seconds - 0.15) return;
            p.later = client->getSoundEngine()->isPlayingSound(p.handle) ? 1 : 0;
        });
    };
    // the engine's level of a play, once the sound has started (a sound that is still loading at the
    // play has no attributes yet)
    auto levelLater = [res, slot](int index) {
        ll::thread::ClientThreadExecutor::getDefault().execute([res, slot, index] {
            auto* client = gf::client::currentClient();
            std::lock_guard lock(res->lock);
            auto&           p = slot(index);
            if (!client || !p.played || p.volume) return;
            if (auto attr = client->getSoundEngine()->tryGetPlayingSoundAttributes(p.handle)) {
                p.volume = attr->mVolume;
                p.pitch  = attr->mPitch;
            }
        });
    };
    constexpr double kControlSec = 5.0; // mob.enderdragon.death plays for longer
    play("mob.enderdragon.death", -1);           // a long vanilla event: the engine knows it and plays it for seconds
    play("gf.synth.selftest.no_such_sound", -2); // an unknown event
    // streamed events (music, discs) one at a time: the engine keeps only a few streams playing and
    // stops the oldest, so overlapping them would look like a file that does not play
    auto streamed = [](std::size_t i) {
        for (auto const& v : variantsOf(i)) {
            if (v.stream) return true;
        }
        return false;
    };
    auto stop = [res, slot](int index) {
        ll::thread::ClientThreadExecutor::getDefault().execute([res, slot, index] {
            auto*           client = gf::client::currentClient();
            std::lock_guard lock(res->lock);
            auto const      h = slot(index).handle;
            if (client && h != 0 && h != ~std::uint64_t(0)) client->getSoundEngine()->stop(h);
        });
    };
    std::vector<int> order, streams;
    for (int i = 0; i < int(std::size(kEvents)); ++i) (streamed(std::size_t(i)) ? streams : order).push_back(i);
    int const n = int(order.size());
    for (int t = 0; t < n + kLagTicks; ++t) {
        if (t < n) play(kEvents[order[std::size_t(t)]].id, order[std::size_t(t)]);
        for (int back : {1, 2, 4}) {
            if (t - back >= 0 && t - back < n) levelLater(order[std::size_t(t - back)]);
        }
        if (t >= kLagTicks && t <= kLagTicks * 4) checkLater(-1, kControlSec);
        // the check, and a second chance for the checks whose task ran too early
        for (int lag : {kLagTicks, 2 * kLagTicks}) {
            int const j = t - lag;
            if (j >= 0 && j < n && minSeconds[std::size_t(order[std::size_t(j)])] >= kLongSec) {
                checkLater(order[std::size_t(j)], minSeconds[std::size_t(order[std::size_t(j)])]);
            }
        }
        co_await ctx.ticks(1);
    }
    for (int j = std::max(0, n - kLagTicks); j < n; ++j) {
        if (minSeconds[std::size_t(order[std::size_t(j)])] >= kLongSec) checkLater(order[std::size_t(j)], minSeconds[std::size_t(order[std::size_t(j)])]);
    }
    for (int i : streams) {
        play(kEvents[i].id, i);
        co_await ctx.ticks(1);
        levelLater(i);
        co_await ctx.ticks(kLagTicks);
        if (minSeconds[std::size_t(i)] >= kLongSec) checkLater(i, minSeconds[std::size_t(i)]);
        co_await ctx.ticks(kLagTicks);
        if (minSeconds[std::size_t(i)] >= kLongSec) checkLater(i, minSeconds[std::size_t(i)]);
        stop(i);
    }    for (int wait = 0; wait < 100 && res->done.load() < int(std::size(kEvents)) + 2; ++wait) co_await ctx.ticks(1);
    co_await ctx.ticks(2); // the last checks
    ctx.require(res->done.load() == int(std::size(kEvents)) + 2, "the client thread played every event");

    // the results, then stop what still plays (the music tracks run for minutes)
    std::vector<Play> plays;
    Play              control, bogus;
    {
        std::lock_guard lock(res->lock);
        plays   = res->plays;
        control = res->control;
        bogus   = res->bogus;
    }
    std::vector<std::uint64_t> handles{control.handle};
    for (auto const& p : plays) handles.push_back(p.handle);
    ll::thread::ClientThreadExecutor::getDefault().execute([handles = std::move(handles)] {
        auto* client = gf::client::currentClient();
        if (!client) return;
        for (auto h : handles) {
            if (h != 0 && h != ~std::uint64_t(0)) client->getSoundEngine()->stop(h);
        }
    });
    auto known = [](std::uint64_t h) { return h != 0 && h != ~std::uint64_t(0); }; // the engine returns ~0 (or 0) for an unknown event
    auto index = [](std::string_view id) {
        for (std::size_t i = 0; i < std::size(kEvents); ++i) {
            if (id == kEvents[i].id) return i;
        }
        return std::size(kEvents);
    };
    auto describe = [&](std::size_t i) {
        auto const& p = plays[i];
        return std::string(kEvents[i].id) + " handle " + std::to_string(p.handle) + " volume " + (p.volume ? std::to_string(*p.volume) : "-") + " pitch "
             + (p.pitch ? std::to_string(*p.pitch) : "-") + " later " + std::to_string(p.later) + " subtitle '" + p.subtitle.value_or("<none>") + "'";
    };
    // what the engine reports, for the log (a level change of the engine shows up here first)
    for (auto id : {"entity.twilightforest.parrot.imitate.alpha_yeti", "entity.twilightforest.naga.hiss", "music.twilightforest.twilight_forest", "item.twilightforest.ore_meter.crackle"}) {
        if (auto i = index(id); i < std::size(kEvents)) ctx.info("engine: " + describe(i));
    }
    ctx.info("engine: control mob.enderdragon.death handle " + std::to_string(control.handle) + " later " + std::to_string(control.later) + ", unknown event handle " + std::to_string(bogus.handle));

    // 1. every TF event is known to the engine
    if (!known(control.handle) || known(bogus.handle)) {
        ctx.fail("the sound engine does not tell known from unknown events (control " + std::to_string(control.handle) + ", unknown " + std::to_string(bogus.handle) + ")");
    }
    int missing = 0;
    for (std::size_t i = 0; i < std::size(kEvents); ++i) {
        if (!known(plays[i].handle) && ++missing <= 20) ctx.check(false, std::string("the client does not know ") + kEvents[i].id);
    }
    ctx.check(missing == 0, std::to_string(missing) + " event(s) unknown to the client");

    // 2. TF's subtitles (the engine may give the key or its text)
    int badSubtitles = 0;
    for (std::size_t i = 0; i < std::size(kEvents); ++i) {
        std::string const want = kEvents[i].subtitle;
        auto const&       got  = plays[i].subtitle;
        bool ok = want.empty() ? (!got || got->empty()) : (got && (*got == want || (plays[i].subtitleText && *got == *plays[i].subtitleText)));
        if (!ok && ++badSubtitles <= 20) ctx.check(false, std::string(kEvents[i].id) + ": the engine's subtitle is '" + got.value_or("<none>") + "', TF's is '" + want + "'");
    }
    ctx.check(badSubtitles == 0, std::to_string(badSubtitles) + " event(s) without TF's subtitle in the engine");

    // 3. long synthesized sounds still play a quarter second later (a file the engine cannot load or
    // decode does not play); measured when the engine reports the long vanilla control still playing
    int measured = 0, stopped = 0;
    if (control.later == 1) {
        for (std::size_t i = 0; i < std::size(kEvents); ++i) {
            if (plays[i].later < 0) continue;
            ++measured;
            if (plays[i].later == 0 && ++stopped <= 20) ctx.check(false, std::string(kEvents[i].id) + " is not playing " + std::to_string(kCheckSec) + "s after the play (its file did not load?)");
        }
        ctx.check(stopped == 0, std::to_string(stopped) + " synthesized sound(s) not playing");
        ctx.check(measured > 0, "long synthesized sounds were measured");
    } else {
        ctx.info("the engine does not report the long vanilla control as playing (" + std::to_string(control.later) + "): playing check not possible here");
    }

    // 4. the engine's volume and pitch of a play: calibrated on the events TF plays at 1.0 / 1.0 (the
    // requested level when the engine applies the definition's), then checked on every event whose
    // variants share another level (the parrot imitations' 0.6 / 1.8, the music's 0.5, ...)
    int  calVol = 0, calPitch = 0, calVolOk = 0, calPitchOk = 0;
    auto approx = [](float a, double b) { return std::fabs(double(a) - b) < 1e-3; };
    for (std::size_t i = 0; i < std::size(kEvents); ++i) {
        auto lvl = uniformLevel(variantsOf(i));
        if (!lvl || lvl->first != 1.0 || lvl->second != 1.0) continue;
        if (plays[i].volume) {
            ++calVol;
            if (approx(*plays[i].volume, kPlayVolume)) ++calVolOk;
        }
        if (plays[i].pitch) {
            ++calPitch;
            if (approx(*plays[i].pitch, kPlayPitch)) ++calPitchOk;
        }
    }
    bool const checkVol = calVol > 0 && calVolOk == calVol, checkPitch = calPitch > 0 && calPitchOk == calPitch;
    int        levels = 0, badLevels = 0;
    for (std::size_t i = 0; i < std::size(kEvents) && (checkVol || checkPitch); ++i) {
        auto lvl = uniformLevel(variantsOf(i));
        if (!lvl || (lvl->first == 1.0 && lvl->second == 1.0)) continue;
        auto const& p   = plays[i];
        bool        bad = false;
        if (!p.volume) continue; // not observed (the sound ended before the engine reported it)
        if (checkVol && !approx(*p.volume, kPlayVolume * lvl->first)) bad = true;
        if (checkPitch && p.pitch && !approx(*p.pitch, kPlayPitch * lvl->second)) bad = true;
        ++levels;
        if (bad && ++badLevels <= 20) ctx.check(false, describe(i) + ": TF's level is volume " + std::to_string(lvl->first) + " pitch " + std::to_string(lvl->second));
    }
    ctx.check(badLevels == 0, std::to_string(badLevels) + " event(s) not at TF's volume and pitch");
    ctx.info(
        "engine levels: volume " + std::string(checkVol ? "checked" : "not checked") + " (" + std::to_string(calVolOk) + "/" + std::to_string(calVol)
        + " calibration plays at the requested volume), pitch " + (checkPitch ? "checked" : "not checked") + " (" + std::to_string(calPitchOk) + "/"
        + std::to_string(calPitch) + "), " + std::to_string(levels) + " events at another level observed; " + std::to_string(measured) + " long sounds measured playing"
    );
    gf::log().info(
        "synth: played {} sound events on the client ({} long sounds still playing after {}s; levels checked: volume {}, pitch {})",
        std::size(kEvents),
        measured - stopped,
        kCheckSec,
        checkVol,
        checkPitch
    );
#else
    // ---- the server: PlaySoundPacket to every real player (simulated players receive nothing) ----
    int targets = 0;
    level->forEachPlayer([&targets](Player& p) {
        if (!p.isSimulated()) ++targets;
        return true;
    });
    if (targets == 0) {
        // nothing would be played to anybody: say so instead of a silent pass (the client scenario's
        // remote modes run this test with a real player on the BDS)
        ctx.info("SKIPPED: no real player on this server (simulated players receive no packets); tools/e2e/client/run.ps1 -Scenario synth -Mode remote-vanilla runs it with one");
        gf::log().info("synth: play_all skipped: no real player");
        co_return;
    }
    for (auto const& ev : kEvents) {
        auto lvl = ll::service::getLevel();
        if (!lvl) break;
        lvl->forEachPlayer([&ev](Player& p) { // looked up again every tick (R16)
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
