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
// total height, so its opacity rule is kept) are rebuilt with Java's colour rule: the vanilla
// stained glass by DyeColor#getTextureDiffuseColor, the registered blocks by their multiplier.
#include "feature/ext/aurora/BeaconColor.h"

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Color.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BeaconBeamSection.h"
#include "mc/world/level/block/actor/BeaconBlockActor.h"

#include "game/Blocks.h"
#include "mod/Module.h"

namespace gf::feature::ext::aurora::beacon {

namespace {

std::unordered_map<std::string, ColorFn>& multipliers() {
    static std::unordered_map<std::string, ColorFn> m;
    return m;
}

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

std::array<float, 3> rgbOf(unsigned c) {
    return {static_cast<float>((c >> 16) & 255) / 255.0F, static_cast<float>((c >> 8) & 255) / 255.0F, static_cast<float>(c & 255) / 255.0F};
}

void recolour(BeaconBlockActor& beacon, BlockSource& region) {
    auto& sections = beacon.mBeamSections.get();
    int   total    = 0;
    for (auto const& s : sections) total += s.mHeight;
    if (total <= 0 || multipliers().empty()) return;
    BlockPos const base = beacon.getPosition();
    // the colour multiplier of each block of the beam (nullopt: none)
    std::vector<std::optional<std::array<float, 3>>> colours(static_cast<std::size_t>(total));
    bool                                            custom = false;
    for (int i = 0; i < total; i++) {
        BlockPos    pos{base.x, base.y + 1 + i, base.z};
        if (pos.y >= region.getMaxHeight()) break;
        std::string id = gf::game::blockId(region.getBlock(pos));
        if (auto it = multipliers().find(id); it != multipliers().end()) {
            colours[static_cast<std::size_t>(i)] = rgbOf(it->second(pos));
            custom                               = true;
        } else if (auto g = glassColors().find(id); g != glassColors().end()) {
            colours[static_cast<std::size_t>(i)] = rgbOf(g->second);
        }
    }
    if (!custom) return;
    // BeaconBlockEntity#tick: the beacon's own white section, then the blocks above
    struct Section {
        std::array<float, 3> rgb;
        int                  height;
    };
    std::vector<Section> rebuilt{{{1.0F, 1.0F, 1.0F}, 0}};
    for (auto const& c : colours) {
        if (!c) {
            rebuilt.back().height++;
            continue;
        }
        if (rebuilt.size() <= 1) rebuilt.push_back({*c, 1});
        else if (*c == rebuilt.back().rgb) rebuilt.back().height++;
        else {
            auto const& prev = rebuilt.back().rgb;
            rebuilt.push_back({{(prev[0] + (*c)[0]) / 2.0F, (prev[1] + (*c)[1]) / 2.0F, (prev[2] + (*c)[2]) / 2.0F}, 1});
        }
    }
    sections.clear();
    for (auto const& s : rebuilt) {
        if (s.height <= 0) continue;
        BeaconBeamSection out{};
        out.mHeight = s.height;
        out.mColor  = mce::Color(s.rgb[0], s.rgb[1], s.rgb[2], 1.0F);
        sections.push_back(out);
    }
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

void registerColorMultiplier(std::string const& blockId, ColorFn fn) { multipliers()[blockId] = std::move(fn); }

} // namespace gf::feature::ext::aurora::beacon

GF_REGISTER_MODULE(gf::feature::ext::aurora::beacon::BeaconColorModule);
