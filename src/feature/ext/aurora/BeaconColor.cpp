// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/AuroralizedGlassBlock.java (getBeaconColorMultiplier, the
//   NeoForge hook BeaconBlockEntity#tick asks of every block above a beacon)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U28's contract extension (R18): see BeaconColor.h. The engine's beam sections stay as they are
// unless a registered block is in the beam; then the sections over the same blocks (the engine's
// total height, so its opacity rule is kept) are rebuilt with Java's colour rule
// (core::aurora::beaconSections): the vanilla stained glass by DyeColor#getTextureDiffuseColor, the
// registered blocks by their multiplier.
#include "feature/ext/aurora/BeaconColor.h"

#include <unordered_map>

namespace gf::feature::ext::aurora::beacon {
namespace {
std::unordered_map<std::string, ColorFn>& multipliers() {
    static std::unordered_map<std::string, ColorFn> m;
    return m;
}
} // namespace

void registerColorMultiplier(std::string const& blockId, ColorFn fn) { multipliers()[blockId] = std::move(fn); }

} // namespace gf::feature::ext::aurora::beacon

// The beam sections exist only in the client: BeaconBlockActor::generateBeamSections is declared for
// LL_PLAT_C only (BDS never computes them; each client draws the beam). BDS and vanilla clients
// draw the engine's beam. Built into the client build (GF_CLIENT) only.
#ifdef GF_CLIENT

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Color.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BeaconBeamSection.h"
#include "mc/world/level/block/actor/BeaconBlockActor.h"

#include "core/aurora/BeaconSections.h"
#include "game/Blocks.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::ext::aurora::beacon {

namespace {

/// DyeColor#getTextureDiffuseColor of the vanilla stained glass and panes.
std::unordered_map<std::string, unsigned> const& glassColors() {
    static std::unordered_map<std::string, unsigned> const m = [] {
        std::unordered_map<std::string, unsigned> out;
        struct Dye {
            char const* name;
            unsigned    rgb;
        };
        for (auto d : {Dye{"white", 0xF9FFFE}, Dye{"orange", 0xF9801D}, Dye{"magenta", 0xC74EBD}, Dye{"light_blue", 0x3AB3DA}, Dye{"yellow", 0xFED83D},
                       Dye{"lime", 0x80C71F}, Dye{"pink", 0xF38BAA}, Dye{"gray", 0x474F52}, Dye{"light_gray", 0x9D9D97}, Dye{"cyan", 0x169C9C},
                       Dye{"purple", 0x8932B8}, Dye{"blue", 0x3C44AA}, Dye{"brown", 0x835432}, Dye{"green", 0x5E7C16}, Dye{"red", 0xB02E26},
                       Dye{"black", 0x1D1D21}}) {
            out[std::string("minecraft:") + d.name + "_stained_glass"]      = d.rgb;
            out[std::string("minecraft:") + d.name + "_stained_glass_pane"] = d.rgb;
        }
        return out;
    }();
    return m;
}

std::unordered_set<std::int64_t>& logged() {
    static std::unordered_set<std::int64_t> s;
    return s;
}

unsigned rgbOf(mce::Color const& c) {
    auto ch = [](float f) { return static_cast<unsigned>(std::clamp(f, 0.0F, 1.0F) * 255.0F + 0.5F); };
    return ch(c.r) << 16 | ch(c.g) << 8 | ch(c.b);
}

void recolour(BeaconBlockActor& beacon, BlockSource& region) {
    auto& sections = beacon.mBeamSections.get();
    int   total    = 0;
    for (auto const& s : sections) total += s.mHeight;
    if (total <= 0 || multipliers().empty()) return;
    BlockPos const base = beacon.getPosition();
    // Bedrock's sections start at the beacon block itself: the engine's own sections of a vanilla
    // beacon under red stained glass two blocks up are 2 white (the beacon and the air block), then
    // red, then a last section past the world's top. That lead block belongs to the first (beacon's
    // own) section, so the beam is white up to the first coloured block, as Java's.
    int const above = region.getMaxHeight() - 1 - base.y;
    int const lead  = std::clamp(total - above, 0, 1);
    // the colour multiplier of each block of the beam above the beacon (nullopt: none)
    std::vector<std::optional<unsigned>> colours(static_cast<std::size_t>(total - lead));
    bool                                 custom = false;
    for (int i = 0; i < total - lead; i++) {
        BlockPos pos{base.x, base.y + 1 + i, base.z};
        if (pos.y >= region.getMaxHeight()) break;
        std::string id = gf::game::blockId(region.getBlock(pos));
        if (auto it = multipliers().find(id); it != multipliers().end()) {
            colours[static_cast<std::size_t>(i)] = it->second(pos);
            custom                               = true;
        } else if (auto g = glassColors().find(id); g != glassColors().end()) {
            colours[static_cast<std::size_t>(i)] = g->second;
        }
    }
    std::int64_t const key = (static_cast<std::int64_t>(base.x) & 0x3FFFFFF) << 38 | (static_cast<std::int64_t>(base.z) & 0x3FFFFFF) << 12 |
                             (static_cast<std::int64_t>(base.y) & 0xFFF);
    if (!custom) {
        // the engine's own sections of a vanilla stained glass beam (once per beacon): the reference
        // the client scenario compares the rebuilt sections with
        bool glass = std::any_of(colours.begin(), colours.end(), [](auto const& c) { return c.has_value(); });
        if (glass && logged().insert(key).second) {
            std::string engine;
            for (auto const& s : sections) engine += std::to_string(s.mHeight) + "x" + std::to_string(rgbOf(s.mColor.get())) + " ";
            gf::log().info("aurora: vanilla beacon beam at {} {} {}: engine sections {}, total {}, above {}", base.x, base.y, base.z, engine, total, above);
        }
        return;
    }
    unsigned beaconRgb = sections.empty() ? 0xFFFFFFU : rgbOf(sections.front().mColor.get());
    auto     rebuilt   = gf::core::aurora::beaconSections(colours, beaconRgb);
    if (!rebuilt.empty()) rebuilt.front().height += lead;
    std::string engine;
    for (auto const& s : sections) engine += std::to_string(s.mHeight) + "x" + std::to_string(rgbOf(s.mColor.get())) + " ";
    sections.clear();
    for (auto const& s : rebuilt) {
        if (s.height <= 0) continue;
        BeaconBeamSection out{};
        out.mHeight = s.height;
        out.mColor  = mce::Color(static_cast<float>((s.rgb >> 16) & 255) / 255.0F, static_cast<float>((s.rgb >> 8) & 255) / 255.0F,
                                 static_cast<float>(s.rgb & 255) / 255.0F, 1.0F);
        sections.push_back(out);
    }
    // one line per beacon position (the client scenario waits for it next to its beam screenshot)

    if (rebuilt.size() >= 2 && logged().insert(key).second)
        gf::log().info("aurora: beacon beam at {} {} {} coloured {:06X} (white {} block(s) from the beacon; engine sections {}, total {}, above {})", base.x,
                       base.y, base.z, rebuilt[1].rgb, rebuilt[0].height, engine, total, above);
}

} // namespace

LL_TYPE_INSTANCE_HOOK(AuroraBeaconSectionsHook, ll::memory::HookPriority::Normal, BeaconBlockActor, &BeaconBlockActor::generateBeamSections, void, ::BlockSource& region) {
    origin(region);
    recolour(*this, region);
}

namespace {
struct BeaconColorModule {
    void install() { AuroraBeaconSectionsHook::hook(); }
    void uninstall() { AuroraBeaconSectionsHook::unhook(); }
};
} // namespace

} // namespace gf::feature::ext::aurora::beacon

GF_REGISTER_MODULE(gf::feature::ext::aurora::beacon::BeaconColorModule);

#endif // GF_CLIENT
