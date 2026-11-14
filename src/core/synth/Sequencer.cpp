// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Contract "synth" (F3): the tracker-style music sequencer.
#include "core/synth/Sequencer.h"

#include <algorithm>
#include <map>
#include <sstream>

#include "core/synth/Random.h"
#include "core/synth/SynthMath.h"

namespace gf::core::synth {

namespace {

struct Cell {
    enum class Kind { Empty, Off, Notes } kind = Kind::Empty;
    std::vector<int> notes;
    double           vel = 1.0;
    std::string      inst;
};

Cell parseCell(std::string const& tok) {
    Cell c;
    if (tok.empty() || tok == "." || tok == "_" || tok == "..") return c;
    if (tok == "-" || tok == "off" || tok == "r" || tok == "==") {
        c.kind = Cell::Kind::Off;
        return c;
    }
    std::string body = tok;
    auto        at   = body.find('@');
    if (at != std::string::npos) {
        c.inst = body.substr(at + 1);
        body   = body.substr(0, at);
    }
    auto colon = body.find(':');
    if (colon != std::string::npos) {
        c.vel = Json::parse(body.substr(colon + 1)).number();
        body  = body.substr(0, colon);
    }
    std::stringstream ss(body);
    std::string       part;
    while (std::getline(ss, part, '+')) {
        int n = parseNote(part);
        if (n < 0) throw PatchError("bad note cell \"" + tok + "\"");
        c.notes.push_back(n);
    }
    c.kind = Cell::Kind::Notes;
    return c;
}

std::vector<std::string> tokens(Json const& notes) {
    std::vector<std::string> out;
    auto                     add = [&out](std::string const& s) {
        std::stringstream ss(s);
        std::string       t;
        while (ss >> t) {
            if (t == "|") continue;
            out.push_back(t);
        }
    };
    if (notes.isString()) add(notes.string());
    else if (notes.isArray()) {
        for (auto const& x : notes.items()) {
            if (x.isString()) add(x.string());
        }
    }
    return out;
}

} // namespace

int parseNote(std::string const& t) {
    if (t.empty()) return -1;
    static constexpr int base[7] = {9, 11, 0, 2, 4, 5, 7}; // A..G
    char                 l       = char(t[0] >= 'a' && t[0] <= 'g' ? t[0] - 32 : t[0]);
    if (l < 'A' || l > 'G') return -1;
    int         n = base[l - 'A'];
    std::size_t i = 1;
    while (i < t.size() && (t[i] == '#' || t[i] == 'b')) {
        n += t[i] == '#' ? 1 : -1;
        ++i;
    }
    if (i >= t.size()) return -1;
    int sign = 1;
    if (t[i] == '-') {
        sign = -1;
        ++i;
    }
    if (i >= t.size() || t[i] < '0' || t[i] > '9') return -1;
    int oct = 0;
    while (i < t.size() && t[i] >= '0' && t[i] <= '9') oct = oct * 10 + (t[i++] - '0');
    if (i != t.size()) return -1;
    return (sign * oct + 1) * 12 + n;
}

std::vector<float> renderTrack(Json const& track, int sampleRate, PatchLibrary const* library) {
    int const rate = sampleRate > 0 ? sampleRate : int(track.num("rate", 32000));
    double    bpm  = track.num("bpm", 90);
    double    rpb  = track.num("rpb", 4);
    if (bpm <= 0 || rpb <= 0) throw PatchError("track needs bpm > 0 and rpb > 0");
    double const        rowSec      = 60.0 / (bpm * rpb);
    double const        swing       = track.num("swing", 0);
    double const        length      = math::clamp(track.num("length", 60), 1.0, 900.0);
    std::uint64_t const seed        = std::uint64_t(track.num("seed", 1));
    Json const&         instruments = track["instruments"];
    Json const&         patterns    = track["patterns"];
    Json const&         order       = track["order"];
    if (!instruments.isObject() || !patterns.isObject() || !order.isArray() || order.size() == 0) {
        throw PatchError("a track needs \"instruments\", \"patterns\" and \"order\"");
    }

    std::vector<double>                        mix(std::size_t(length * rate) + 1, 0.0);
    std::map<std::string, std::vector<double>> cache;
    auto renderNote = [&](std::string const& instName, int note, double vel, double dur) -> std::vector<double> const& {
        Json const* inst = instruments.find(instName);
        if (!inst) throw PatchError("unknown instrument \"" + instName + "\"");
        auto        durSamples = std::int64_t(math::round(dur * rate));
        std::string key =
            instName + "|" + std::to_string(note) + "|" + Json(vel).dump() + "|" + std::to_string(durSamples);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
        Json params = Json::object();
        if (auto const* p = inst->find("params"); p && p->isObject()) {
            for (auto const& [k, v] : p->members()) params.set(k, v);
        }
        params.set("freq", math::midiToHz(note));
        params.set("note", double(note));
        params.set("vel", vel);
        params.set("dur", double(durSamples) / rate);
        std::uint64_t       s   = hashString(key, seed);
        auto                pcm = renderPatch((*inst)["patch"], params, s, rate, library);
        double              g   = math::dbToGain(inst->num("gain", 0)) * (inst->flag("velAmp", true) ? vel : 1.0);
        std::vector<double> buf(pcm.size());
        for (std::size_t i = 0; i < pcm.size(); ++i) buf[i] = double(pcm[i]) * g;
        return cache.emplace(key, std::move(buf)).first->second;
    };

    double songTime = 0;
    bool   loop     = track.flag("loop", true);
    for (std::size_t oi = 0; songTime < length; ++oi) {
        if (oi >= order.size()) {
            if (!loop) break;
            oi = 0;
        }
        Json const& entry     = order[oi];
        std::string name      = entry.isString() ? entry.string() : entry.str("p", "");
        int         transpose = entry.isObject() ? int(entry.num("t", 0)) : 0;
        Json const* pat       = patterns.find(name);
        if (!pat) throw PatchError("unknown pattern \"" + name + "\"");
        int rows = int(pat->num("rows", 16));
        if (rows <= 0) throw PatchError("pattern \"" + name + "\" has no rows");
        for (auto const& ch : (*pat)["channels"].items()) {
            std::string       instName = ch.str("inst", "");
            int               octave   = int(ch.num("octave", 0));
            double            chGain   = math::dbToGain(ch.num("gain", 0));
            auto              toks     = tokens(ch["notes"]);
            std::vector<Cell> cells;
            for (auto const& t : toks) cells.push_back(parseCell(t));
            for (std::size_t r = 0; r < cells.size() && int(r) < rows; ++r) {
                Cell const& c = cells[r];
                if (c.kind != Cell::Kind::Notes) continue;
                std::size_t end = r + 1;
                while (end < cells.size() && int(end) < rows && cells[end].kind == Cell::Kind::Empty) ++end;
                double startSec = songTime + double(r) * rowSec + ((r % 2 == 1) ? swing * rowSec : 0.0);
                double dur      = double(std::min<std::size_t>(end, std::size_t(rows)) - r) * rowSec;
                if (startSec >= length) continue;
                for (int n : c.notes) {
                    int         note = n + transpose + 12 * octave;
                    auto const& buf  = renderNote(c.inst.empty() ? instName : c.inst, note, c.vel, dur);
                    auto        at   = std::size_t(math::round(startSec * rate));
                    for (std::size_t i = 0; i < buf.size() && at + i < mix.size(); ++i) mix[at + i] += buf[i] * chGain;
                }
            }
        }
        songTime += double(rows) * rowSec;
    }

    std::map<std::string, double> params;
    detail::finish(mix, track, params, seed, rate, false);
    mix.resize(std::size_t(length * rate));
    double      fadeIn  = track.num("fadeIn", 0);
    double      fadeOut = track.num("fadeOut", 2.0);
    std::size_t fi      = std::min(mix.size(), std::size_t(fadeIn * rate));
    for (std::size_t i = 0; i < fi; ++i) mix[i] *= double(i) / double(fi);
    std::size_t fo = std::min(mix.size(), std::size_t(fadeOut * rate));
    for (std::size_t i = 0; i < fo; ++i) mix[mix.size() - 1 - i] *= double(i) / double(fo);
    return std::vector<float>(mix.begin(), mix.end());
}

} // namespace gf::core::synth
