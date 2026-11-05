// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/command/MapLocatorCommand.java
//   src/main/java/twilightforest/command/MapBiomesCommand.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// /twilightforest map_locator <structure> [<player> [<skip_known_structures>]] and
// /twilightforest biomepng [<width> [<height> [<showBiomePercents>]]] (U13, docs/spec/uncraftmaps.md
// 3.6). Both need Java's permission level 2 (commands::isAdmin; below it Bedrock's
// commands.generic.unknown, as F6's commands answer).
//
// map_locator: TF's structures through F6's gf::landmark::ext::locate (the random-spread rings,
// then the landmark grid); Minecraft's own (minecraft: ids) through Bedrock's world generator
// (WorldGenerator::findNearestStructureFeature with the Bedrock feature of the Java id). A
// structure tag ("#ns:path") is typed as the rest of the line, since Bedrock's string parameter
// refuses a leading '#': "#tag [<player name | @s> [true|false]]".
//
// biomepng: the server build is TF's dedicated server (returns at once, nothing happens); the
// client build in a local world samples the biomes, reports and writes the PNG under the world's
// folder. Biomes of the Twilight Forest come from its biome source (any quart); other dimensions
// are read from their loaded chunks (Bedrock's biome sources are not callable from here).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <unordered_map>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/file/PathBuffer.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandOutputParameter.h"
#include "mc/server/commands/CommandRawText.h"
#include "mc/server/commands/CommandSelector.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/levelgen/WorldGenerator.h"
#include "mc/world/level/storage/LevelStorage.h"

#include "core/uncraftmaps/Maps.h"
#include "core/uncraftmaps/Png.h"
#include "feature/dimension/World.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/tags/Tags.h"
#include "feature/datamaps/DataMaps.h"
#include "feature/uncraftmaps/Maps.h"
#include "feature/worldgen/Worldgen.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Commands.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::uncraftmaps {

// Command parameter types need external linkage (reflection).
struct MapLocatorParams {
    std::string             structure;
    CommandSelector<Player> player;
    bool                    skip_known_structures = false;
};
struct MapLocatorOneParam {
    std::string structure;
};
struct MapLocatorTagParam {
    CommandRawText structure;
};
struct BiomePngWidth {
    int width = 4096;
};
struct BiomePngParams {
    int  width             = 4096;
    int  height            = 4096;
    bool showBiomePercents = true;
};

namespace {

namespace cu = core::uncraftmaps;

std::string say(CommandOrigin const& origin, std::string const& key, std::string_view fallback) {
    auto* p = commands::playerOf(origin);
    return p ? trFor(*p, key, fallback) : tr(key, fallback);
}
template <class... A>
std::string say(CommandOrigin const& origin, std::string const& key, std::string_view fallback, A const&... args) {
    auto* p = commands::playerOf(origin);
    return p ? trFor(*p, key, fallback, args...) : tr(key, fallback, args...);
}

void denied(CommandOutput& output, std::string const& literal) { output.error("commands.generic.unknown", {CommandOutputParameter(literal)}); }

int originDim(CommandOrigin const& origin) {
    if (auto* d = origin.getDimension()) return static_cast<int>(d->getDimensionId());
    return 0;
}

/// Bedrock's structure feature for a Java vanilla structure id ("" when Bedrock has none).
std::string bedrockFeature(std::string const& javaId) {
    static std::unordered_map<std::string, std::string> const map{
        {"minecraft:village_plains", "village"},       {"minecraft:village_desert", "village"},     {"minecraft:village_savanna", "village"},
        {"minecraft:village_snowy", "village"},        {"minecraft:village_taiga", "village"},      {"minecraft:desert_pyramid", "temple"},
        {"minecraft:jungle_pyramid", "temple"},        {"minecraft:swamp_hut", "temple"},           {"minecraft:igloo", "temple"},
        {"minecraft:ocean_ruin_cold", "ruins"},        {"minecraft:ocean_ruin_warm", "ruins"},      {"minecraft:shipwreck", "shipwreck"},
        {"minecraft:shipwreck_beached", "shipwreck"},  {"minecraft:buried_treasure", "buried_treasure"}, {"minecraft:pillager_outpost", "pillager_outpost"},
        {"minecraft:mineshaft", "mineshaft"},          {"minecraft:mineshaft_mesa", "mineshaft"},   {"minecraft:fortress", "fortress"},
        {"minecraft:bastion_remnant", "bastion_remnant"}, {"minecraft:end_city", "end_city"},       {"minecraft:stronghold", "stronghold"},
        {"minecraft:monument", "monument"},            {"minecraft:mansion", "mansion"},            {"minecraft:ruined_portal", "ruined_portal"},
        {"minecraft:ruined_portal_desert", "ruined_portal"}, {"minecraft:ruined_portal_jungle", "ruined_portal"},
        {"minecraft:ruined_portal_swamp", "ruined_portal"},  {"minecraft:ruined_portal_mountain", "ruined_portal"},
        {"minecraft:ruined_portal_ocean", "ruined_portal"},  {"minecraft:ruined_portal_nether", "ruined_portal"},
        {"minecraft:ancient_city", "ancient_city"},    {"minecraft:trail_ruins", "trail_ruins"},    {"minecraft:trial_chambers", "trial_chambers"},
    };
    auto it = map.find(javaId);
    return it == map.end() ? std::string() : "minecraft:" + it->second;
}

/// ResourceLocation#read (namespace minecraft when absent).
std::string withNamespace(std::string id) { return id.find(':') == std::string::npos ? "minecraft:" + id : id; }

struct Found {
    std::string key;
    int         x = 0, z = 0;
};

/// ChunkGenerator#findNearestMapStructure (with TF's hook) for a key or "#tag".
std::optional<Found> search(int dim, std::string const& printable, std::vector<std::string> const& holders, int x, int z, bool skip) {
    bool tf = std::any_of(holders.begin(), holders.end(), [](std::string const& k) { return k.starts_with("twilightforest:"); });
    if (tf) {
        if (auto hit = gf::landmark::ext::locate(dim, printable, x, z, 100, skip)) return Found{hit->key, hit->pos.x, hit->pos.z};
    }
    // Minecraft's structures: Bedrock's own search
    auto level = ll::service::getLevel();
    if (!level) return std::nullopt;
    auto dimension = level->getDimension(dim).lock();
    if (!dimension || !dimension->mWorldGenerator) return std::nullopt;
    std::optional<Found> best;
    double               bestDist = 0;
    for (auto const& k : holders) {
        auto feature = bedrockFeature(k);
        if (feature.empty()) continue;
        BlockPos out{};
        if (!dimension->mWorldGenerator->findNearestStructureFeature(HashedString(feature), BlockPos{x, 0, z}, out, skip, std::nullopt)) continue;
        double d = std::pow(out.x - x, 2) + std::pow(out.z - z, 2);
        if (!best || d < bestDist) best = Found{k, out.x, out.z}, bestDist = d;
    }
    return best;
}

void mapLocator(CommandOrigin const& origin, CommandOutput& output, std::string structure, std::vector<Player*> players, bool skip) {
    if (!commands::isAdmin(origin)) return denied(output, "map_locator");
    auto trim = [](std::string s) {
        auto a = s.find_first_not_of(" \t");
        if (a == std::string::npos) return std::string();
        return s.substr(a, s.find_last_not_of(" \t") - a + 1);
    };
    structure     = trim(structure);
    bool tag      = structure.starts_with('#');
    auto id       = withNamespace(tag ? structure.substr(1) : structure);
    auto printable = tag ? "#" + id : id;
    // LocateCommand#getHolders: the key, or the tag's members; else ERROR_STRUCTURE_INVALID
    std::vector<std::string> holders;
    if (tag) holders = tags::members(tags::Registry::Structure, id);
    else if (id.starts_with("twilightforest:") || !bedrockFeature(id).empty() || id.starts_with("minecraft:")) holders.push_back(id);
    bool known = !holders.empty() && (tag || id.starts_with("minecraft:") || gf::landmark::descriptor(id) != nullptr || tags::has(tags::Registry::Structure, "twilightforest:landmark", id) || id.starts_with("twilightforest:"));
    if (!known) return output.error(say(origin, "commands.locate.structure.invalid", "There is no structure with type \"%s\"", printable));
    if (players.empty()) {
        // the default target: the executing player (the console must name one)
        auto* self = commands::playerOf(origin);
        if (!self) return output.error(say(origin, "permissions.requires.player", "A player is required to run this command here"));
        players.push_back(self);
    }
    int  dim = originDim(origin);
    auto pos = game::blockPosOf(origin.getWorldPosition());
    auto hit = search(dim, printable, holders, pos.x, pos.z, skip);
    if (!hit) {
        log().info("uncraftmaps: map_locator {} from {} {}: none", printable, pos.x, pos.z);
        return output.error(say(origin, "commands.locate.structure.not_found", "Could not find a structure of type \"%s\" nearby", printable));
    }
    auto map = locatorMap(dim, hit->x, hit->z);
    for (auto* p : players) {
        if (!p || game::items::isEmpty(map)) continue;
        ItemStack copy = map;
        p->add(copy); // Inventory#add: lost when full
        p->sendInventory(false);
    }
    // LocateCommand#showLocateResult: the found position is passed as the source, so the distance is 0
    auto element = tag ? printable + " (" + hit->key + ")" : printable;
    output.success(say(origin, "commands.locate.structure.success", "The nearest %s is at %s (%s blocks away)", element,
                       "§a[" + say(origin, "chat.coordinates", "%s, %s, %s", std::to_string(hit->x), "~", std::to_string(hit->z)) + "]§r", "0"));
    log().info("uncraftmaps: map_locator {} -> {} at {} {} for {} player(s)", printable, hit->key, hit->x, hit->z, players.size());
}

// ---- biomepng ----------------------------------------------------------------------------------------

/// The biome id at quart (qx, 0, qz) of `dim` (getNoiseBiome).
std::string noiseBiome(int dim, int qx, int qz) {
    auto tf = gf::world::twilight();
    if (tf && static_cast<int>(*tf) == dim) return worldgen::biomeAt(BlockPos{qx * 4, 0, qz * 4});
    auto region = game::regionOf(dim);
    if (!region) return {};
    BlockPos at{qx * 4, 0, qz * 4};
    if (!region->hasChunksAt(at, 0, false)) return "minecraft:plains";
    auto name = region->getBiome(at).mHash->getString();
    return name.find(':') == std::string::npos ? "minecraft:" + name : name;
}

std::uint32_t biomeColour(std::string const& biome) {
    static std::unordered_map<std::string, std::uint32_t> cache; // BIOME2COLOR, kept for the session
    if (auto it = cache.find(biome); it != cache.end()) return it->second;
    std::uint32_t c;
    if (auto fixed = cu::biomePngColor(biome)) c = *fixed;
    else if (auto const* v = datamaps::get("twilightforest:magic_map_color", biome); v && v->is_object())
        c = cu::biomePngDataMapColor(static_cast<std::uint32_t>(v->value("map_color", nlohmann::json::object()).value("color", 0)));
    else c = 0xFF000000u; // getBiomeColor's default: opaque black (non-zero: no grass colour fallback)
    return cache[biome] = c;
}

void biomePng(CommandOrigin const& origin, CommandOutput& output, int width, int height, bool showBiomePercents) {
    if (!commands::isAdmin(origin)) return denied(output, "biomepng");
#ifndef GF_CLIENT
    // FMLEnvironment.dist.isDedicatedServer(): return -1, nothing else
    (void)output, (void)width, (void)height, (void)showBiomePercents;
    log().info("uncraftmaps: biomepng on a dedicated server: nothing to do");
    return;
#else
    auto fail = [&] { output.error(say(origin, "command.failed", "An unexpected error occurred trying to execute that command")); };
    if (width <= 0 || height <= 0) return fail(); // NativeImage: "Invalid texture size"
    int dim = originDim(origin);
    std::vector<std::uint32_t>        pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    std::vector<std::string>          order; // biomes in first-seen order
    std::map<std::string, int>        counts;
    int                               progressUpdate = height / 8;
    for (int x = 0; x < height; ++x) {
        for (int z = 0; z < width; ++z) {
            auto b = noiseBiome(dim, x - width / 2, z - height / 2);
            auto c = biomeColour(b);
            if (auto it = counts.find(b); it == counts.end()) {
                counts[b] = 0;
                order.push_back(b);
            } else {
                ++it->second;
            }
            // setPixelRGBA(x, z): outside the image when x >= width or z >= height
            if (x >= width || z >= height) {
                log().info("uncraftmaps: biomepng {}x{}: ({}, {}) outside of image bounds", width, height, x, z);
                return fail();
            }
            pixels[static_cast<std::size_t>(z) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = c;
        }
        if (progressUpdate == 0) return fail(); // x % 0: ArithmeticException
        if (x % progressUpdate == 0) {
            double percent = static_cast<double>(x) / height * 100;
            output.success(say(origin, "commands.tffeature.biomepng.progress", "%s%% Done mapping", cu::decimalFormat00(percent)));
        }
    }
    if (showBiomePercents) {
        output.success(say(origin, "commands.tffeature.biomepng.counts_header", "Approximate biome-block counts within a %sx%s region", std::to_string(width), std::to_string(height)));
        long long total = 0;
        for (auto const& [b, n] : counts) total += n;
        for (auto const& b : order) {
            int n = counts[b];
            output.success(b + ": " + std::to_string(n) + "§7 (" + cu::decimalFormat00(static_cast<double>(n) / static_cast<double>(total) * 100) + "%)");
        }
    }
    auto pos    = origin.getWorldPosition();
    int  startX = static_cast<int>(std::floor(pos.x)) - width / 2;
    int  startZ = static_cast<int>(std::floor(pos.z)) - height / 2;
    auto level  = ll::service::getLevel();
    if (!level) return fail();
    auto seed  = worldgen::seed();
    auto world = std::filesystem::path(level->getLevelStorage().getFullPath().value);
    auto path  = (world / cu::biomePngPath(seed, startX, startZ, width, height)).lexically_normal();
    try {
        if (!std::filesystem::exists(path)) {
            std::filesystem::create_directories(path.parent_path());
            auto          png = cu::encodePng(width, height, pixels);
            std::ofstream f(path, std::ios::binary);
            f.write(reinterpret_cast<char const*>(png.data()), static_cast<std::streamsize>(png.size()));
            if (!f) throw std::runtime_error("write failed");
        }
    } catch (std::exception const& e) {
        log().warn("uncraftmaps: biomepng could not save {}: {}", path.string(), e.what());
        return output.error(say(origin, "commands.tffeature.biomepng.save_failed", "Could not save image! Please report this!"));
    }
    output.success(say(origin, "commands.tffeature.biomepng.save_success", "Image saved!"));
    log().info("uncraftmaps: biomepng wrote {}", path.string());
#endif
}

} // namespace

} // namespace gf::feature::uncraftmaps

GF_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    namespace u = gf::feature::uncraftmaps;
    cmd.overload<u::MapLocatorOneParam>().text("map_locator").required("structure").execute(
        [](CommandOrigin const& o, CommandOutput& out, u::MapLocatorOneParam const& p) { u::mapLocator(o, out, p.structure, {}, false); }
    );
    cmd.overload<u::MapLocatorParams>().text("map_locator").required("structure").required("player").optional("skip_known_structures").execute(
        [](CommandOrigin const& o, CommandOutput& out, u::MapLocatorParams const& p) {
            std::vector<Player*> players;
            for (auto* pl : p.player.results(o)) players.push_back(pl);
            if (players.empty()) return out.error(gf::tr("commands.generic.noTargetMatch", "No targets matched selector"));
            u::mapLocator(o, out, p.structure, players, p.skip_known_structures);
        }
    );
    // "#tag [<player name | @s> [true | false]]"
    cmd.overload<u::MapLocatorTagParam>().text("map_locator").required("structure").execute(
        [](CommandOrigin const& o, CommandOutput& out, u::MapLocatorTagParam const& p) {
            std::istringstream       in(p.structure.mText);
            std::string              structure, who, flag;
            in >> structure >> who >> flag;
            std::vector<Player*> players;
            if (!who.empty() && who != "@s") {
                auto level = ll::service::getLevel();
                Player* found = nullptr;
                if (level) level->forEachPlayer([&](Player& pl) {
                        if (pl.getRealName() == who) found = &pl;
                        return found == nullptr;
                    });
                if (!found) return out.error(gf::tr("commands.generic.noTargetMatch", "No targets matched selector"));
                players.push_back(found);
            }
            u::mapLocator(o, out, structure, players, flag == "true");
        }
    );
    // IntegerArgumentType.integer(0)
    auto low = [](CommandOutput& out, int n) {
        out.error(gf::tr("argument.integer.low", "Integer must not be less than %s, found %s", "0", std::to_string(n)));
    };
    cmd.overload().text("biomepng").execute([](CommandOrigin const& o, CommandOutput& out) { u::biomePng(o, out, 4096, 4096, true); });
    cmd.overload<u::BiomePngWidth>().text("biomepng").required("width").execute([low](CommandOrigin const& o, CommandOutput& out, u::BiomePngWidth const& p) {
        if (p.width < 0) return low(out, p.width);
        u::biomePng(o, out, p.width, p.width, true);
    });
    cmd.overload<u::BiomePngParams>().text("biomepng").required("width").required("height").optional("showBiomePercents").execute(
        [low](CommandOrigin const& o, CommandOutput& out, u::BiomePngParams const& p) {
            if (p.width < 0) return low(out, p.width);
            if (p.height < 0) return low(out, p.height);
            u::biomePng(o, out, p.width, p.height, p.showBiomePercents);
        }
    );
});
