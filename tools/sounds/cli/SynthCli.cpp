// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Contract "synth" (F3): the renderer behind tools/sounds/synth.ps1.
//
// Built by synth.ps1 with clang-cl straight from this file and src/core/synth/*.cpp (standard
// library only) into a cache keyed by the sources' hash. Usage:
//
//     gf-synth render <jobs.json>     render every job, print "OK <name> <samples> <bytes>"
//     gf-synth probe <file.ogg>...    print "<file> <channels> <rate> <samples>" or "BAD <file>: why"
//
// jobs.json: {"library": {"<patch name>": {patch}}, "threads": 4, "jobs": [
//     {"name": "fauna/mob/raven/caw1", "out": "<path>.ogg", "kind": "patch", "patch": {..} | "name",
//      "params": {..}, "seed": 1, "rate": 44100, "quality": 0.5, "wav": "<optional preview .wav>"},
//     {"name": "provisions/music/radiance", "out": "...", "kind": "track", "track": {..}, ...}]}
// The output depends only on the job (never on paths, time or thread scheduling).

#include <atomic>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/synth/Json.h"
#include "core/synth/Patch.h"
#include "core/synth/Random.h"
#include "core/synth/Sequencer.h"
#include "core/synth/Vorbis.h"
#include "core/synth/Wav.h"

using namespace gf::core::synth;

namespace {

std::string readFile(std::string const& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read " + path);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void writeFile(std::string const& path, std::vector<std::uint8_t> const& data) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("cannot write " + tmp);
        f.write(reinterpret_cast<char const*>(data.data()), std::streamsize(data.size()));
        if (!f) throw std::runtime_error("write failed: " + tmp);
    }
    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0) throw std::runtime_error("cannot rename " + tmp);
}

int render(std::string const& jobsPath) {
    Json const   doc = Json::parse(readFile(jobsPath));
    PatchLibrary lib;
    if (auto const* l = doc.find("library"); l && l->isObject()) {
        for (auto const& [k, v] : l->members()) lib.add(k, v);
    }
    Json const& jobs    = doc["jobs"];
    unsigned    threads = unsigned(doc.num("threads", 4));
    if (threads < 1) threads = 1;
    if (threads > 16) threads = 16;
    std::atomic<std::size_t> next{0};
    std::atomic<int>         failures{0};
    std::mutex               out;
    auto                     worker = [&] {
        while (true) {
            std::size_t i = next++;
            if (i >= jobs.size()) return;
            Json const& job  = jobs[i];
            std::string name = job.str("name", "job" + std::to_string(i));
            try {
                int                rate    = int(job.num("rate", 44100));
                double             quality = job.num("quality", 0.5);
                std::vector<float> pcm;
                std::string        kind = job.str("kind", "patch");
                if (kind == "track") {
                    pcm = renderTrack(job["track"], rate, &lib);
                } else if (kind == "patch") {
                    pcm = renderPatch(job["patch"], job["params"], std::uint64_t(job.num("seed", 0)), rate, &lib);
                } else {
                    throw std::runtime_error("unknown kind " + kind);
                }
                VorbisOptions o;
                o.sampleRate = rate;
                o.quality    = quality;
                o.serial     = std::uint32_t(hashString(name));
                auto ogg     = encodeVorbis(pcm, o);
                if (job.has("out")) writeFile(job.str("out", ""), ogg);
                if (job.has("wav")) writeFile(job.str("wav", ""), encodeWav(pcm, rate));
                std::lock_guard lock(out);
                std::printf("OK %s %zu %zu\n", name.c_str(), pcm.size(), ogg.size());
                std::fflush(stdout);
            } catch (std::exception const& e) {
                ++failures;
                std::lock_guard lock(out);
                std::printf("ERR %s: %s\n", name.c_str(), e.what());
                std::fflush(stdout);
            }
        }
    };
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t) pool.emplace_back(worker);
    for (auto& t : pool) t.join();
    return failures.load() == 0 ? 0 : 1;
}

int probe(int argc, char** argv) {
    int bad = 0;
    for (int i = 2; i < argc; ++i) {
        try {
            std::string s    = readFile(argv[i]);
            auto        info = probeOggVorbis(
                std::span<std::uint8_t const>(reinterpret_cast<std::uint8_t const*>(s.data()), s.size())
            );
            if (info.valid) {
                std::printf(
                    "%s %d %d %lld\n",
                    argv[i],
                    info.channels,
                    info.sampleRate,
                    static_cast<long long>(info.samples)
                );
            } else {
                ++bad;
                std::printf("BAD %s: %s\n", argv[i], info.error.c_str());
            }
        } catch (std::exception const& e) {
            ++bad;
            std::printf("BAD %s: %s\n", argv[i], e.what());
        }
    }
    return bad == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::string cmd = argc > 1 ? argv[1] : "";
        if (cmd == "render" && argc == 3) return render(argv[2]);
        if (cmd == "probe") return probe(argc, argv);
        std::fprintf(stderr, "usage: gf-synth render <jobs.json> | gf-synth probe <file.ogg>...\n");
        return 2;
    } catch (std::exception const& e) {
        std::fprintf(stderr, "gf-synth: %s\n", e.what());
        return 1;
    }
}
