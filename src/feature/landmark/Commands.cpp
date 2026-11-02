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
// /twilightforest center | info | conquer [reactivate] | genbook [<structure>] (TF's subcommands,
// literals, argument shapes and texts) and the GloamingForest additions (docs/spec/README.md,
// Subcommands): generatebook (an alias of genbook), locate <structure> | place <structure> [x y z]
// (Java's vanilla /locate structure and /place structure, which know TF's structures there;
// Bedrock's do not). Locating landmark grid
// structures follows TF's ResolveNearestNonRandomSpreadMapStructure hook (WorldUtil
// #findNearestMapLandmark); random spread structures use vanilla's ring search.
//
// Permissions: TF's levels mapped one down onto Bedrock's (Java 2 "gamemasters" -> GameDirectors,
// commands::isAdmin; Java 3 "admins", genbook -> Admin, or any operator: Java's default
// op-permission-level 4 gives operators level 3, Bedrock operators run at GameDirectors). Java's
// requires(n) hides the literal from a source below level n, so brigadier's parse stops there.
// Bedrock cannot hide one overload: a command's permission is the Signature's
// (CommandRegistry::Signature::permissionLevel; CommandRegistry::Overload has no such field, and
// ll::command::CommandRegistrar::getOrCreateCommand takes the requirement per command), and
// /twilightforest must stay open to everyone (TF's root and structure_util clear_display require
// nothing). A source below the level gets Bedrock's own answer for a command it may not use,
// commands.generic.unknown, which the client translates.
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/service/Bedrock.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandOutputParameter.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/dimension/Dimension.h"

#include "core/jrand/Mth.h"
#include "core/landmark/Grid.h"
#include "core/landmark/Landmark.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/worldgen/Generation.h"
#include "feature/landmark/Internal.h"
#include "feature/tags/Tags.h"
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

/// A source below the literal's permission level (Java: requires(n) hides `literal`; see above):
/// Bedrock's "Unknown command: %s. Please check that the command exists and that you have
/// permission to use it.", translated by the reader's client.
void denied(CommandOutput& output, std::string const& literal) { output.error("commands.generic.unknown", {CommandOutputParameter(literal)}); }

/// CenterCommand.
void center(CommandOrigin const& origin, CommandOutput& output) {
    if (!commands::isAdmin(origin)) return denied(output, "center");
    auto  pos = origin.getWorldPosition();
    int   dx  = static_cast<int>(std::floor(pos.x));
    int   dz  = static_cast<int>(std::floor(pos.z));
    int   dim = originDim(origin);
    auto  cc  = cl::nearestCenter(dx >> 4, dz >> 4);
    // LegacyLandmarkPlacements#pickLandmarkAtBlock(cc.x, cc.z, level): the biome at the region's center
    auto  closest = cl::pickForChunk(levelSeed(), cc.x >> 4, cc.z >> 4, [dim](int x, int z) { return biomeAt(dim, x, 0, z); });
    bool  fc      = cl::blockIsInCenter(dx, dz);
    auto* player  = commands::playerOf(origin);
    // Component#withStyle(DARK_GREEN)#getString(): the plain text, the style is dropped
    auto  name    = structureName(closest, player);
    output.success(say(origin, "commands.tffeature.nearest", "The nearest feature is: %s", name));
    output.success(say(origin, "commands.tffeature.center", "Center of feature: %s", shortString(cc.x, 0, cc.z)));
    output.success(say(origin, "commands.tffeature.chunk", "Located in feature chunk: %s", fc ? "true" : "false"));
    log().info("landmark: center {} {} -> {} at {} {} (in center chunk: {})", dx, dz, closest, cc.x, cc.z, fc);
}

/// InfoCommand.
void infoCommand(CommandOrigin const& origin, CommandOutput& output) {
    if (!commands::isAdmin(origin)) return denied(output, "info");
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
        // what is the spawn list (TF underlines it: Bedrock has no underline, and its §n is the
        // copper colour, so the line stays plain)
        output.success(say(origin, "commands.tffeature.structure.spawn_list", "Spawn list for this area is:"));
        if (auto spawns = gf::landmark::ext::potentialSpawns(dim, "monster", pos.x, pos.y, pos.z)) {
            for (auto const& e : *spawns) {
                // EntityType#getDescription().getString(): the entity's name in the reader's language
                auto name = entityName(e.value("type", std::string{}), player);
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
    if (!commands::isAdmin(origin)) return denied(output, "conquer");
    int  dim = originDim(origin);
    auto pos = originBlock(origin);
    auto rec = nearestLandmark(dim, pos.x >> 4, pos.z >> 4, GlobalPos{dim, pos.x, pos.y, pos.z});
    auto const* inf = rec ? info(rec->start.key) : nullptr;
    if (!rec || !rec->start.box.contains(pos.x, pos.y, pos.z) || !inf || !inf->traits.conquerable) {
        log().info("landmark: {} at {} {} {}: not in a structure", flag ? "conquer" : "conquer reactivate", pos.x, pos.y, pos.z);
        return output.error(say(origin, "commands.tffeature.structure.required", "You are not in a structure"));
    }
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

} // namespace

std::optional<std::string> parseResourceLocation(std::string const& text) {
    // ResourceLocation#bySeparator(text, ':'): the namespace before the first ':' (none, or an empty
    // one: "minecraft"), the path after it; then isValidNamespace / isValidPath
    auto        colon = text.find(':');
    std::string ns    = colon == std::string::npos || colon == 0 ? std::string("minecraft") : text.substr(0, colon);
    std::string path  = colon == std::string::npos ? text : text.substr(colon + 1);
    auto        plain = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'; };
    if (!std::all_of(ns.begin(), ns.end(), plain)) return std::nullopt;
    if (!std::all_of(path.begin(), path.end(), [&](char c) { return plain(c) || c == '/'; })) return std::nullopt;
    return ns + ":" + path;
}

bool structureRegistered(std::string const& key) {
    // the Structure registry: Minecraft's, TF's and the data packs' structures (the worldgen's), plus
    // the descriptors (the gf_test fixtures of development builds)
    return info(key) != nullptr || worldgen::ext::structureSlot(key).has_value();
}

namespace {

/// A resource location argument as brigadier reads it: ResourceLocation#read takes the characters a
/// resource location may hold (readGreedy) and parses them (argument.id.invalid), then the
/// dispatcher refuses anything left of the word (CommandSyntaxException
/// #dispatcherExpectedArgumentSeparator). nullopt once the error is sent.
std::optional<std::string> readResourceArgument(CommandOrigin const& origin, CommandOutput& output, std::string const& text) {
    auto end = text.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyz_-.:/");
    auto key = parseResourceLocation(text.substr(0, end));
    if (!key) {
        output.error(say(origin, "argument.id.invalid", "Invalid ID"));
        return std::nullopt;
    }
    if (end != std::string::npos) {
        output.error(say(origin, "command.expected.separator", "Expected whitespace to end one argument, but found trailing data"));
        return std::nullopt;
    }
    return key;
}

/// GenerateBookCommand#generateBook (requires permission level 3: Bedrock's Admin, or an operator,
/// as Java's default op-permission-level 4 gives every operator level 3). The structure argument
/// is a ResourceKeyArgument: resolved (ResourceKeyArgument#getStructure) before generateBook runs.
void genbook(CommandOrigin const& origin, CommandOutput& output, std::optional<std::string> structure) {
    auto* player = commands::playerOf(origin);
    if (origin.getPermissionsLevel() < CommandPermissionLevel::Admin && !(player && player->isOperator())) return denied(output, "genbook");
    auto const  arg   = structure.value_or("(all)");
    Info const* named = nullptr;
    if (structure) {
        // ResourceKeyArgument#resolveKey: a key the registry does not hold is ERROR_INVALID_STRUCTURE
        // with the location
        auto key = readResourceArgument(origin, output, *structure);
        if (!key) {
            log().info("landmark: genbook {}: invalid id", arg);
            return;
        }
        if (!structureRegistered(*key)) {
            log().info("landmark: genbook {}: no structure {}", arg, *key);
            return output.error(say(origin, "commands.place.structure.invalid", "There is no structure with type \"%s\"", *key));
        }
        named = info(*key);
    }
    if (!player || player->isSimulatedPlayer()) {
        log().info("landmark: genbook {}: not run by a player", arg);
        return output.error(say(origin, "commands.tffeature.not_player", "This command must be run by a valid player!"));
    }
    int given = 0;
    if (!structure) {
        // every StructureHints of the registry, in registry order (TF's keys sorted by path)
        for (auto const& key : descriptorKeys()) {
            auto const* inf = info(key);
            if (!inf || !inf->traits.progression || isFixtureKey(key)) continue;
            if (auto book = createHintBook(*inf, player)) {
                giveBook(*player, *book);
                ++given;
            }
        }
    } else {
        // a StructureHints structure gives its hint book, any other structure HintConfig.defaultBook()
        std::optional<ItemStack> book;
        if (named && named->traits.progression) book = createHintBook(*named, player);
        else book = makeBook("unknown", 2, player);
        if (book) {
            giveBook(*player, *book);
            ++given;
        }
    }
    // Command.SINGLE_SUCCESS without a message, as TF
    ++output.mSuccessCount;
    log().info("landmark: genbook gave {} book(s) to {}", given, player->getRealName());
}

/// Mth.floor(LocateCommand#dist(x1, z1, x2, z2)): Mth.sqrt((float) (dx * dx + dz * dz)) over Java's
/// wrapping int arithmetic (an overflowed square is negative: its NaN root casts to 0 in Java).
int horizontalDistance(int x1, int z1, int x2, int z2) {
    namespace mth = core::mth;
    int  dx   = mth::iadd(x2, mth::imul(x1, -1));
    int  dz   = mth::iadd(z2, mth::imul(z1, -1));
    auto root = std::sqrt(static_cast<float>(mth::iadd(mth::imul(dx, dx), mth::imul(dz, dz))));
    return std::isnan(root) ? 0 : mth::floorf(root);
}

/// A registered structure without a landmark descriptor (Minecraft's own, or a data pack's): Java's
/// /locate and /place handle it, here Bedrock's world generator owns Minecraft's structures and
/// GloamingForest knows the Twilight Forest's only.
void notTwilightStructure(CommandOrigin const& origin, CommandOutput& output, std::string const& printable) {
    output.error(say(origin, "gf.landmark.not_twilight_structure",
                     "/twilightforest locate and place know the Twilight Forest's structures only; %s is not one of them (Bedrock's own /locate finds Minecraft's)",
                     printable));
}

/// `/twilightforest locate <structure>` (LocateCommand#locateStructure).
void locate(CommandOrigin const& origin, CommandOutput& output, std::string const& structure) {
    if (!commands::isAdmin(origin)) return denied(output, "locate");
    int  dim = originDim(origin);
    auto pos = originBlock(origin);
    // ResourceOrTagKeyArgument: "#" and a tag, or a key; resource locations as Java reads them
    bool tag    = structure.starts_with("#");
    auto parsed = readResourceArgument(origin, output, tag ? structure.substr(1) : structure);
    if (!parsed) return;
    // ResourceOrTagKeyArgument.Result#asPrintable
    auto printable = tag ? "#" + *parsed : *parsed;
    // LocateCommand#getHolders: Registry#getHolder for a key, Registry#getTag for a tag (the
    // structure tags of the tags contract: TF's #twilightforest:landmark and any data pack's);
    // neither is ERROR_STRUCTURE_INVALID with the printable argument
    std::vector<std::string> holders;
    if (tag) holders = tags::members(tags::Registry::Structure, *parsed);
    else if (structureRegistered(*parsed)) holders.push_back(*parsed);
    if (holders.empty()) {
        log().info("landmark: locate {}: invalid", printable);
        return output.error(say(origin, "commands.locate.structure.invalid", "There is no structure with type \"%s\"", printable));
    }
    // Minecraft's structures: in the Twilight Forest none generates (no structure set of theirs has a
    // biome of TF's biome source), so Java finds none there either and the search below answers
    // not_found; anywhere else Java finds them, and only Bedrock's own /locate can
    auto tf = twilightDim();
    if ((!tf || *tf != dim) && std::any_of(holders.begin(), holders.end(), [](std::string const& k) { return info(k) == nullptr; })) {
        log().info("landmark: locate {}: not a Twilight Forest structure", printable);
        return notTwilightStructure(origin, output, printable);
    }
    // LocateCommand: ChunkGenerator#findNearestMapStructure(level, holders, pos, 100, false)
    auto hit = gf::landmark::ext::locate(dim, printable, pos.x, pos.z, 100);
    if (!hit) {
        log().info("landmark: locate {} from {} {}: none", printable, pos.x, pos.z);
        return output.error(say(origin, "commands.locate.structure.not_found", "Could not find a structure of type \"%s\" nearby", printable));
    }
    // LocateCommand#showLocateResult: a tag names the structure found after it
    auto element = tag ? printable + " (" + hit->key + ")" : printable;
    int  dist    = horizontalDistance(pos.x, pos.z, hit->pos.x, hit->pos.z);
    output.success(say(origin, "commands.locate.structure.success", "The nearest %s is at %s (%s blocks away)", element,
                       "[" + std::to_string(hit->pos.x) + ", ~, " + std::to_string(hit->pos.z) + "]", std::to_string(dist)));
    log().info("landmark: locate {} from {} {}: {} at {} {} ({} blocks)", printable, pos.x, pos.z, hit->key, hit->pos.x, hit->pos.z, dist);
}

/// `/twilightforest place <structure> [x y z]`.
void place(CommandOrigin const& origin, CommandOutput& output, std::string const& structure, std::optional<BlockPos> at) {
    if (!commands::isAdmin(origin)) return denied(output, "place");
    int  dim    = originDim(origin);
    auto pos    = at.value_or(originBlock(origin));
    auto parsed = readResourceArgument(origin, output, structure); // ResourceKeyArgument
    if (!parsed) return;
    auto key = *parsed;
    // ResourceKeyArgument#getStructure: ERROR_INVALID_STRUCTURE for a key the registry does not hold
    if (!structureRegistered(key)) return output.error(say(origin, "commands.place.structure.invalid", "There is no structure with type \"%s\"", key));
    // PlaceCommand#placeStructure places Minecraft's structures anywhere (its biome check passes
    // everything); Bedrock's world generator owns those
    if (!info(key)) {
        log().info("landmark: place {}: not a Twilight Forest structure", key);
        return notTwilightStructure(origin, output, key);
    }
    auto region = game::regionOf(dim);
    if (!region) return output.error(say(origin, "commands.place.structure.failed", "Failed to place structure"));
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
    // TF's genbook [<structure>] (GenerateBookCommand) and GloamingForest's alias generatebook
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

namespace {

/// A structure set and the targets it places (in the order of the targets).
struct SetTargets {
    fl::StructureSet const*  set = nullptr;
    std::vector<std::string> keys;
};

/// ChunkGenerator#getStructureGeneratingAt for a random spread placement. StructureCheck#checkStart:
/// a chunk whose starts are known answers START_PRESENT / START_NOT_PRESENT; else the frequency
/// reduction may say START_NOT_PRESENT, and otherwise (CHUNK_LOAD_NEEDED) the chunk's real start
/// decides: the structure set's placement, weighted pick, findGenerationPoint and the biome check
/// at the generation point, as the worldgen Starts stage makes it. Java brings the chunk to
/// STRUCTURE_STARTS for that; here only this set is evaluated (fl::probeStart) and nothing is
/// stored, so a locate never writes starts for chunks nobody generated (the Starts stage makes the
/// same start when the chunk generates).
bool structureAt(int dimension, std::string const& key, fl::StructureSet const& set, std::int64_t seed, int chunkX, int chunkZ, bool skipKnownStructures) {
    if (fl::startsEvaluated(dimension, chunkX, chunkZ)) {
        auto rec = fl::findRecord(dimension, chunkX, chunkZ, key);
        return rec && !rec->placed && !skipKnownStructures;
    }
    if (!plc::applyAdditionalChunkRestrictions(set.placement, seed, chunkX, chunkZ)) return false;
    // a new start has no reference yet: StructureStart#canBeReferenced, so skipKnownStructures takes it
    return fl::probeStart(dimension, set, chunkX, chunkZ) == key;
}

} // namespace

std::optional<Located> locate(int dimension, std::string const& keyOrTag, int x, int z, int searchRadius, bool skipKnownStructures) {
    // targets: one key, or the members of a structure tag (the tags contract's), the ones with a
    // descriptor (Minecraft's own structures never generate in the Twilight Forest). The gf_test
    // fixtures say they are in #twilightforest:landmark (no tag file lists them)
    std::vector<std::string> targets;
    if (keyOrTag.starts_with('#')) {
        auto const tag = keyOrTag.substr(1);
        for (auto const& k : gf::tags::members(gf::tags::Registry::Structure, tag))
            if (fl::info(k)) targets.push_back(k);
        if (tag == "twilightforest:landmark")
            for (auto const& k : fl::descriptorKeys())
                if (auto const* i = fl::info(k); i && fl::isFixtureKey(k) && i->traits.landmarkTag) targets.push_back(k);
    } else if (fl::info(keyOrTag)) {
        targets.push_back(keyOrTag);
    }
    if (targets.empty()) return std::nullopt;
    auto distSqr = [&](int px, int pz) {
        double ddx = px - x, ddz = pz - z;
        return ddx * ddx + ddz * ddz;
    };
    std::optional<Located> best;
    double                 bestDist = std::numeric_limits<double>::max();
    auto                   consider = [&](std::string const& key, int px, int pz) {
        if (double d = distSqr(px, pz); d < bestDist) {
            bestDist = d;
            best     = Located{key, GlobalPos{dimension, px, 0, pz}};
        }
    };
    // the gf_test fixtures have no structure set: their forceStart starts are what locate finds
    for (auto const& rec : fl::allRecords(dimension))
        if (rec->forced && !rec->placed && fl::isFixtureKey(rec->start.key) && std::find(targets.begin(), targets.end(), rec->start.key) != targets.end())
            consider(rec->start.key, rec->start.center.x, rec->start.center.z);
    auto tf = fl::twilightDim();
    if (!tf || *tf != dimension) return best;

    auto seed    = fl::levelSeed();
    auto variety = [seed](int cx, int cz) { return cl::pickVariety(seed, cx, cz); };
    auto lookup  = [](std::string const& id) -> plc::Placement const* {
        auto const* s = fl::structureSet(id);
        return s ? &s->placement : nullptr;
    };
    std::vector<SetTargets> spread, grid;
    for (auto const* set : fl::structureSets()) {
        SetTargets st{set, {}};
        for (auto const& t : targets)
            if (std::any_of(set->structures.begin(), set->structures.end(), [&](auto const& e) { return e.first == t; })) st.keys.push_back(t);
        if (st.keys.empty()) continue;
        if (set->placement.kind == plc::Kind::LandmarkGrid) grid.push_back(std::move(st));
        else if (set->placement.spacing > 0) spread.push_back(std::move(st)); // random spread, avoid_landmark_grid
    }

    // ChunkGenerator#findNearestMapStructure: the random spread placements ring by ring (ring k of
    // getNearestGeneratedStructure, `searchRadius` rings); the first ring with a hit decides
    if (!spread.empty()) {
        int cx = x >> 4, cz = z >> 4;
        for (int k = 0; k <= searchRadius; ++k) {
            bool found = false;
            for (auto const& st : spread) {
                auto const& p       = st.set->placement;
                bool        hitRing = false;
                for (int i = -k; i <= k && !hitRing; ++i) {
                    bool edgeI = i == -k || i == k;
                    for (int j = -k; j <= k && !hitRing; ++j) {
                        if (!edgeI && j != -k && j != k) continue;
                        auto pot = plc::potentialStructureChunk(p, seed, cx + p.spacing * i, cz + p.spacing * j);
                        for (auto const& key : st.keys) {
                            if (!structureAt(dimension, key, *st.set, seed, pot.x, pot.z, skipKnownStructures)) continue;
                            auto [lx, lz] = plc::locatePos(p, pot.x, pot.z);
                            consider(key, lx, lz); // getStructureGeneratingAt: the first one of the ring
                            hitRing = true;
                            break;
                        }
                    }
                }
                found = found || hitRing;
            }
            if (found) return best;
        }
    }
    if (grid.empty()) return best;

    // MapHooks#resolveNearestNonRandomSpreadMapStructure: WorldUtil#findNearestMapLandmark over
    // LegacyLandmarkPlacements#landmarkCenterScanner (TF passes the radius as 16-chunk cells), and
    // its result replaces vanilla's
    std::optional<Located> landmark;
    double                 landmarkDist = std::numeric_limits<double>::max();
    for (auto const& c : cl::centerScanner(x, z, searchRadius)) {
        for (auto const& st : grid) {
            if (!plc::isStructureChunk(st.set->placement, seed, c.x >> 4, c.z >> 4, variety, lookup)) continue;
            auto biome = fl::biomeAt(dimension, c.x, 0, c.z);
            for (auto const& k : targets) {
                if (std::find(st.keys.begin(), st.keys.end(), k) == st.keys.end()) continue;
                auto const* inf = fl::info(k);
                if (!inf || !fl::biomeMatches(inf->biomes, biome)) continue;
                if (skipKnownStructures && fl::findRecord(dimension, c.x >> 4, c.z >> 4, k)) break;
                // distToLowCornerSqr(pos.x, 0, pos.z)
                if (double d = distSqr(c.x, c.z); d < landmarkDist) {
                    landmarkDist = d;
                    landmark     = Located{k, GlobalPos{dimension, c.x, 0, c.z}};
                }
            }
        }
    }
    return landmark ? landmark : best;
}

} // namespace gf::landmark::ext
