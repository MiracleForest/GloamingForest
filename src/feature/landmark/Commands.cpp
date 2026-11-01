// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/command/CenterCommand.java
//   src/main/java/twilightforest/command/InfoCommand.java
//   src/main/java/twilightforest/command/ConquerCommand.java
//   src/main/java/twilightforest/command/GenerateBookCommand.java
//   src/main/java/twilightforest/util/WorldUtil.java
//   src/main/java/twilightforest/asmhooks/MapHooks.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// /twilightforest center | info | conquer [reactivate] | genbook [structure] (TF's subcommands)
// and locate <structure> | place <structure> [x y z] (Java's vanilla /locate structure and
// /place structure, which know TF's structures there; Bedrock's do not). Locating landmark grid
// structures follows TF's ResolveNearestNonRandomSpreadMapStructure hook (WorldUtil
// #findNearestMapLandmark); random spread structures use vanilla's ring search.
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/service/Bedrock.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/dimension/Dimension.h"

#include "core/landmark/Grid.h"
#include "core/landmark/Landmark.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/landmark/Internal.h"
#include "game/Blocks.h"
#include "mod/Commands.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::landmark {

// Command parameter types need external linkage (reflection).
struct StructureParam {
    std::string structure;
};
struct PlaceParams {
    std::string structure;
    int         x = 0, y = 0, z = 0;
};

namespace {

namespace cl  = core::landmark;
namespace plc = core::landmark::placement;

std::string say(CommandOrigin const& origin, std::string const& key, std::string_view fallback) {
    auto* p = commands::playerOf(origin);
    return p ? trFor(*p, key, fallback) : tr(key, fallback);
}
template <class... A>
std::string say(CommandOrigin const& origin, std::string const& key, std::string_view fallback, A const&... args) {
    auto* p = commands::playerOf(origin);
    return p ? trFor(*p, key, fallback, args...) : tr(key, fallback, args...);
}

int originDim(CommandOrigin const& origin) {
    if (auto* d = origin.getDimension()) return static_cast<int>(d->getDimensionId());
    return 0;
}

BlockPos originBlock(CommandOrigin const& origin) { return game::blockPosOf(origin.getWorldPosition()); }

std::string shortString(int x, int y, int z) { return std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z); }

/// CenterCommand.
void center(CommandOrigin const& origin, CommandOutput& output) {
    if (!commands::isAdmin(origin)) return output.error("You need operator permission.");
    auto  pos = origin.getWorldPosition();
    int   dx  = static_cast<int>(std::floor(pos.x));
    int   dz  = static_cast<int>(std::floor(pos.z));
    int   dim = originDim(origin);
    auto  cc  = cl::nearestCenter(dx >> 4, dz >> 4);
    // LegacyLandmarkPlacements#pickLandmarkAtBlock(cc.x, cc.z, level): the biome at the region's center
    auto  closest = cl::pickForChunk(levelSeed(), cc.x >> 4, cc.z >> 4, [dim](int x, int z) { return biomeAt(dim, x, 0, z); });
    bool  fc      = cl::blockIsInCenter(dx, dz);
    auto* player  = commands::playerOf(origin);
    auto  name    = "§2" + structureName(closest, player) + "§r"; // ChatFormatting.DARK_GREEN
    output.success(say(origin, "commands.tffeature.nearest", "The nearest feature is: %s", name));
    output.success(say(origin, "commands.tffeature.center", "Center of feature: %s", shortString(cc.x, 0, cc.z)));
    output.success(say(origin, "commands.tffeature.chunk", "Located in feature chunk: %s", fc ? "true" : "false"));
    log().info("landmark: center {} {} -> {} at {} {} (in center chunk: {})", dx, dz, closest, cc.x, cc.z, fc);
}

/// InfoCommand.
void infoCommand(CommandOrigin const& origin, CommandOutput& output) {
    if (!commands::isAdmin(origin)) return output.error("You need operator permission.");
    int  dim = originDim(origin);
    auto pos = originBlock(origin);
    auto rec = nearestLandmark(dim, pos.x >> 4, pos.z >> 4, GlobalPos{dim, pos.x, pos.y, pos.z});
    auto const* inf = rec ? info(rec->start.key) : nullptr;
    if (!rec || !inf || !inf->traits.landmark) {
        log().info("landmark: info at {} {} {}: no landmark", pos.x, pos.y, pos.z);
        return; // TF returns 0 without a message
    }
    auto* player = commands::playerOf(origin);
    // FMLLoader.isProduction(): released builds warn that the command is WIP
    output.success("§c§l" + say(origin, "commands.tffeature.info.wip", "This command is still WIP, some things may still be broken."));
    output.success(say(origin, "commands.tffeature.nearest", "The nearest feature is: %s", structureName(inf->d.key, player)));
    auto const& b   = rec->start.box;
    auto        box = "[" + std::to_string(b.minX) + ", " + std::to_string(b.minY) + ", " + std::to_string(b.minZ) + ", " + std::to_string(b.maxX) + ", "
             + std::to_string(b.maxY) + ", " + std::to_string(b.maxZ) + "]";
    output.success(say(origin, "commands.tffeature.structure.boundaries", "Structure boundaries: %s", box));
    if (b.contains(pos.x, pos.y, pos.z)) {
        output.success("§l§a" + say(origin, "commands.tffeature.structure.inside", "You are in a Landmark structure"));
        if (inf->traits.conquerable) {
            bool c = rec->start.conquered;
            output.success(std::string(c ? "§l§a" : "§l§c") + say(origin, "commands.tffeature.structure.conquer.status", "Structure conquer flag: %s", c ? "true" : "false"));
        }
        // what is the spawn list
        output.success("§n" + say(origin, "commands.tffeature.structure.spawn_list", "Spawn list for this area is:"));
        if (auto spawns = gf::landmark::ext::potentialSpawns(dim, "monster", pos.x, pos.y, pos.z)) {
            for (auto const& e : *spawns) {
                auto type   = e.value("type", std::string{});
                auto colon  = type.find(':');
                auto entity = colon == std::string::npos ? type : type.substr(colon + 1);
                auto ns     = colon == std::string::npos ? std::string("minecraft") : type.substr(0, colon);
                // EntityType#getDescription: Java's "entity.<namespace>.<path>" key
                auto key    = "entity." + ns + "." + entity;
                auto name   = player ? trFor(*player, key, entity) : tr(key, entity);
                output.success(say(origin, "commands.tffeature.structure.spawn_info", "%s, Weight %s", name, std::to_string(e.value("weight", 0))));
            }
        }
    } else {
        output.success("§l§c" + say(origin, "commands.tffeature.structure.outside", "You are not in a Landmark structure"));
    }
    log().info("landmark: info at {} {} {}: {} conquered={}", pos.x, pos.y, pos.z, inf->d.key, rec->start.conquered);
}

/// ConquerCommand#changeStructureActivity.
void conquer(CommandOrigin const& origin, CommandOutput& output, bool flag) {
    if (!commands::isAdmin(origin)) return output.error("You need operator permission.");
    int  dim = originDim(origin);
    auto pos = originBlock(origin);
    auto rec = nearestLandmark(dim, pos.x >> 4, pos.z >> 4, GlobalPos{dim, pos.x, pos.y, pos.z});
    auto const* inf = rec ? info(rec->start.key) : nullptr;
    if (!rec || !rec->start.box.contains(pos.x, pos.y, pos.z) || !inf || !inf->traits.conquerable)
        return output.error(say(origin, "commands.tffeature.structure.required", "You are not in a structure"));
    output.success(say(origin, "commands.tffeature.structure.conquer.update", "Structure conquer flag was %s, changing to %s",
                       rec->start.conquered ? "true" : "false", flag ? "true" : "false"));
    if (rec->start.conquered != flag) {
        rec->start.conquered = flag;
        markDirty(*rec);
    }
    log().info("landmark: {} at {} {} {} conquered={} (command)", rec->start.key, rec->start.center.x, rec->start.center.y, rec->start.center.z, flag);
}

/// Gives a book to a player (Player#addItem, else drop it).
void giveBook(Player& player, ItemStack book) {
    if (!player.add(book)) player.drop(book, true);
    player.sendInventory(false);
}

/// GenerateBookCommand#generateBook.
void genbook(CommandOrigin const& origin, CommandOutput& output, std::optional<std::string> structure) {
    if (!commands::isAdmin(origin)) return output.error("You need operator permission.");
    auto* player = commands::playerOf(origin);
    if (!player || player->isSimulatedPlayer())
        return output.error(say(origin, "commands.tffeature.not_player", "This command must be run by a valid player!"));
    int given = 0;
    if (!structure) {
        for (auto const& key : descriptorKeys()) {
            auto const* inf = info(key);
            if (!inf || !inf->traits.progression || key.starts_with("gf_test:")) continue; // StructureHints
            if (auto book = createHintBook(*inf, player)) {
                giveBook(*player, *book);
                ++given;
            }
        }
    } else {
        auto const* inf = info(normalizeKey(*structure));
        std::optional<ItemStack> book;
        if (inf && inf->traits.progression) book = createHintBook(*inf, player);
        else book = makeBook("unknown", 2, player); // HintConfig.defaultBook()
        if (book) {
            giveBook(*player, *book);
            ++given;
        }
    }
    output.success("Gave " + std::to_string(given) + " book(s)");
    log().info("landmark: genbook gave {} book(s) to {}", given, player->getRealName());
}

/// `/twilightforest locate <structure>`.
void locate(CommandOrigin const& origin, CommandOutput& output, std::string const& structure) {
    if (!commands::isAdmin(origin)) return output.error("You need operator permission.");
    int  dim = originDim(origin);
    auto pos = originBlock(origin);
    auto key = structure.starts_with("#") ? structure : normalizeKey(structure);
    auto hit = gf::landmark::ext::locate(dim, key, pos.x, pos.z, 100);
    if (!hit) {
        log().info("landmark: locate {} from {} {}: none", key, pos.x, pos.z);
        return output.error(say(origin, "commands.locate.structure.not_found", "Could not find a structure of type \"%s\" nearby", key));
    }
    double dx   = hit->pos.x - pos.x;
    double dz   = hit->pos.z - pos.z;
    int    dist = static_cast<int>(std::floor(std::sqrt(dx * dx + dz * dz)));
    output.success(say(origin, "commands.locate.structure.success", "The nearest %s is at %s (%s blocks away)", hit->key,
                       "[" + std::to_string(hit->pos.x) + ", ~, " + std::to_string(hit->pos.z) + "]", std::to_string(dist)));
    log().info("landmark: locate {} from {} {}: {} at {} {} ({} blocks)", key, pos.x, pos.z, hit->key, hit->pos.x, hit->pos.z, dist);
}

/// `/twilightforest place <structure> [x y z]`.
void place(CommandOrigin const& origin, CommandOutput& output, std::string const& structure, std::optional<BlockPos> at) {
    if (!commands::isAdmin(origin)) return output.error("You need operator permission.");
    int  dim    = originDim(origin);
    auto pos    = at.value_or(originBlock(origin));
    auto key    = normalizeKey(structure);
    auto region = game::regionOf(dim);
    if (!region || !info(key)) return output.error(say(origin, "commands.place.structure.invalid", "There is no structure with type \"%s\"", key));
    if (!generatorFor(key) || !gf::landmark::generateAt(key, *region, pos)) {
        log().info("landmark: place {} at {} {} {} failed", key, pos.x, pos.y, pos.z);
        return output.error(say(origin, "commands.place.structure.failed", "Failed to place structure"));
    }
    output.success(say(origin, "commands.place.structure.success", "Generated structure \"%s\" at %s, %s, %s", key, std::to_string(pos.x), std::to_string(pos.y), std::to_string(pos.z)));
    log().info("landmark: placed {} at {} {} {}", key, pos.x, pos.y, pos.z);
}

} // namespace

} // namespace gf::feature::landmark

GF_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    namespace fl = gf::feature::landmark;
    cmd.overload().text("center").execute([](CommandOrigin const& o, CommandOutput& out) { fl::center(o, out); });
    cmd.overload().text("info").execute([](CommandOrigin const& o, CommandOutput& out) { fl::infoCommand(o, out); });
    cmd.overload().text("conquer").execute([](CommandOrigin const& o, CommandOutput& out) { fl::conquer(o, out, true); });
    cmd.overload().text("conquer").text("reactivate").execute([](CommandOrigin const& o, CommandOutput& out) { fl::conquer(o, out, false); });
    for (char const* literal : {"genbook", "generatebook"}) {
        cmd.overload().text(literal).execute([](CommandOrigin const& o, CommandOutput& out) { fl::genbook(o, out, std::nullopt); });
        cmd.overload<fl::StructureParam>().text(literal).required("structure").execute(
            [](CommandOrigin const& o, CommandOutput& out, fl::StructureParam const& p) { fl::genbook(o, out, p.structure); }
        );
    }
    cmd.overload<fl::StructureParam>().text("locate").required("structure").execute(
        [](CommandOrigin const& o, CommandOutput& out, fl::StructureParam const& p) { fl::locate(o, out, p.structure); }
    );
    cmd.overload<fl::StructureParam>().text("place").required("structure").execute(
        [](CommandOrigin const& o, CommandOutput& out, fl::StructureParam const& p) { fl::place(o, out, p.structure, std::nullopt); }
    );
    cmd.overload<fl::PlaceParams>().text("place").required("structure").required("x").required("y").required("z").execute(
        [](CommandOrigin const& o, CommandOutput& out, fl::PlaceParams const& p) { fl::place(o, out, p.structure, BlockPos{p.x, p.y, p.z}); }
    );
});

// ---- locating (extension) ----------------------------------------------------------------------------

namespace gf::landmark::ext {

namespace fl  = gf::feature::landmark;
namespace cl  = gf::core::landmark;
namespace plc = gf::core::landmark::placement;

std::optional<Located> locate(int dimension, std::string const& keyOrTag, int x, int z, int chunkRadius, bool skipKnownStructures) {
    auto tf = fl::twilightDim();
    // targets: one key or the #twilightforest:landmark tag
    std::vector<std::string> targets;
    if (keyOrTag == "#twilightforest:landmark") {
        for (auto const& k : fl::descriptorKeys())
            if (auto const* i = fl::info(k); i && i->traits.landmarkTag) targets.push_back(k);
    } else if (fl::info(keyOrTag)) {
        targets.push_back(keyOrTag);
    }
    if (targets.empty()) return std::nullopt;
    // off-grid starts already made (forceStart / place) count too
    std::optional<Located> best;
    double                 bestDist = std::numeric_limits<double>::max();
    auto                   consider = [&](std::string const& key, int px, int py, int pz) {
        double ddx = px - x, ddz = pz - z, d = ddx * ddx + ddz * ddz;
        if (d < bestDist) {
            bestDist = d;
            best     = Located{key, GlobalPos{dimension, px, py, pz}};
        }
    };
    for (auto const& rec : fl::allRecords(dimension))
        if (rec->forced && std::find(targets.begin(), targets.end(), rec->start.key) != targets.end())
            consider(rec->start.key, rec->start.center.x, rec->start.center.y, rec->start.center.z);
    if (!tf || *tf != dimension) return best;

    auto seed    = fl::levelSeed();
    auto variety = [seed](int cx, int cz) { return cl::pickVariety(seed, cx, cz); };
    auto lookup  = [](std::string const& id) -> plc::Placement const* {
        auto const* s = fl::structureSet(id);
        return s ? &s->placement : nullptr;
    };
    for (auto const* set : fl::structureSets()) {
        std::vector<std::string> keys;
        for (auto const& [k, w] : set->structures)
            if (std::find(targets.begin(), targets.end(), k) != targets.end()) keys.push_back(k);
        if (keys.empty()) continue;
        if (set->placement.kind == plc::Kind::LandmarkGrid) {
            // WorldUtil#findNearestMapLandmark over LegacyLandmarkPlacements#landmarkCenterScanner
            // (TF passes the chunk radius as the number of 16-chunk cells: capped at 16 cells here)
            for (auto const& c : cl::centerScanner(x, z, std::min(chunkRadius, 16))) {
                if (!plc::isStructureChunk(set->placement, seed, c.x >> 4, c.z >> 4, variety, lookup)) continue;
                for (auto const& k : keys) {
                    auto const* inf = fl::info(k);
                    if (!inf || !fl::biomeMatches(inf->biomes, fl::biomeAt(dimension, c.x, 0, c.z))) continue;
                    if (skipKnownStructures && fl::findRecord(dimension, c.x >> 4, c.z >> 4, k)) break;
                    // distToLowCornerSqr(pos.x, 0, pos.z)
                    consider(k, c.x, 0, c.z);
                }
            }
        } else if (set->placement.spacing > 0) {
            // ChunkGenerator#getNearestGeneratedStructure (rings of placement regions)
            int    spacing = set->placement.spacing;
            int    cx = x >> 4, cz = z >> 4;
            int    regions = chunkRadius / spacing + 1;
            for (int ring = 0; ring <= regions; ++ring) {
                bool found = false;
                for (int i = -ring; i <= ring; ++i) {
                    for (int j = -ring; j <= ring; ++j) {
                        if (std::abs(i) != ring && std::abs(j) != ring) continue;
                        int  rx  = cx + spacing * i, rz = cz + spacing * j;
                        auto pot = plc::potentialStructureChunk(set->placement, seed, rx, rz);
                        if (!plc::isStructureChunk(set->placement, seed, pot.x, pot.z, variety, lookup)) continue;
                        for (auto const& k : keys) {
                            auto const* inf = fl::info(k);
                            if (!inf || !fl::biomeMatches(inf->biomes, fl::biomeAt(dimension, pot.x * 16 + 8, fl::seaLevelOf(dimension), pot.z * 16 + 8))) continue;
                            auto [lx, lz] = plc::locatePos(set->placement, pot.x, pot.z);
                            consider(k, lx, 0, lz);
                            found = true;
                        }
                    }
                }
                if (found) break;
            }
        }
    }
    return best;
}

} // namespace gf::landmark::ext
