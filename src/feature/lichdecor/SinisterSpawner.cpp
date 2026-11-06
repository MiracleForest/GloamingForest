// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/SinisterSpawnerBlock.java
//   src/main/java/twilightforest/command/SinisterSpawnerCommand.java
//   src/main/java/twilightforest/block/entity/spawner/SinisterSpawnerBlockEntity.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U22 (lichdecor): the sinister spawner block's own behaviour. Its block entity (BaseSpawner +
// SinisterSpawnerLogic: ticking, particles, the loot table field) is F8's (feature/ext/entity
// EntityExt.h, in the block's BlockData); this file adds
//   - /twilightforest sinister_spawner add_particle|remove_particle|set_loot|clear_loot (permission 2),
//   - the creative use with a boss trophy / boss spawner item (SinisterSpawnerBlock#particlesFromItem),
//   - the drops: the stored loot table rolled into the drops and 15 + nextInt(15) + nextInt(15) xp.
//
// Particle options: F8 keeps a list of particle ids plus one parameter set per id. Java keeps a
// HashSet<ParticleOptions>; the value-equal options (simple types, sculk_charge, fallen_leaf) are a
// set by (id, options), the others never compare equal (each add_particle adds an entry, no
// remove_particle matches them). U22 keeps the (id, options) entries in its own BlockData field
// "sinister_options" next to F8's list, and rebuilds F8's list from it.

#include <map>
#include <string>
#include <vector>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/server/commands/Command.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandFilePath.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPosition.h"
#include "mc/world/actor/item/ExperienceOrb.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include "core/lichdecor/Arguments.h"
#include "feature/common/BlockData.h"
#include "feature/common/Interactions.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/lichdecor/LootTables.h"
#include "feature/lichdecor/Lichdecor.h"
#include "feature/loot/Loot.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Commands.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::lichdecor {

// command parameters (external linkage: LeviLamina's command registry keys on them)
struct SinisterParticleParams {
    std::string     particle;
    CommandPosition pos;
};
struct SinisterLootParams {
    CommandFilePath loot; ///< a file path token: Bedrock strings stop at "/" (loot ids have them)
    CommandPosition pos;
};
struct SinisterPosParams {
    CommandPosition pos;
};

namespace {

namespace cl = gf::core::lichdecor;
namespace bx = gf::boss::ext;
namespace bd = gf::blockdata;

constexpr char kSinister[] = "twilightforest:sinister_spawner";

// ---- the (id, options) entries ---------------------------------------------------------------------

struct Entry {
    std::string id;
    std::string options;
};

/// U22's entries of the spawner at `pos`, reconciled with F8's particle list (another writer —
/// template data, the trophy use — changes only F8's list: its ids without options then).
std::vector<Entry> entriesOf(BlockSource& region, BlockPos const& pos, bx::SinisterSpawner const& s) {
    std::vector<Entry> out;
    if (auto const* doc = bd::get(region, pos, kDataKey); doc && doc->is_object() && doc->contains("sinister_options")) {
        for (auto const& e : (*doc)["sinister_options"])
            if (e.is_object()) out.push_back({e.value("id", std::string()), e.value("options", std::string())});
    }
    std::vector<std::string> ids;
    for (auto const& e : out) ids.push_back(e.id);
    if (ids != s.particles) {
        out.clear();
        for (auto const& p : s.particles) out.push_back({p, {}});
    }
    return out;
}

/// Writes the entries to U22's field and F8's list (+ parameters for the option types).
void storeEntries(BlockSource& region, BlockPos const& pos, bx::SinisterSpawner s, std::vector<Entry> const& entries) {
    s.particles.clear();
    Json params = s.particleParams.is_object() ? s.particleParams : Json::object();
    Json list   = Json::array();
    for (auto const& e : entries) {
        s.particles.push_back(e.id);
        if (!e.options.empty()) params[e.id] = Json{{"snbt", e.options}};
        list.push_back(Json{{"id", e.id}, {"options", e.options}});
    }
    s.particleParams = params;
    bx::setSinisterSpawner(region, pos, s);
    Json doc = Json::object();
    if (auto const* d = bd::get(region, pos, kDataKey); d && d->is_object()) doc = *d;
    doc["sinister_options"] = list;
    bd::set(region, pos, kDataKey, doc);
}

/// SinisterSpawnerLogic#addParticle (true: changed).
bool addParticle(BlockSource& region, BlockPos const& pos, cl::ParticleArg const& p) {
    auto s = bx::sinisterSpawner(region, pos);
    if (!s) return false;
    auto entries = entriesOf(region, pos, *s);
    if (p.valueEqual) {
        for (auto const& e : entries)
            if (e.id == p.id && e.options == p.options) return false;
    }
    entries.push_back({p.id, p.options});
    storeEntries(region, pos, *s, entries);
    return true;
}

/// SinisterSpawnerLogic#removeParticle (true: changed). Never-equal options never match.
bool removeParticle(BlockSource& region, BlockPos const& pos, cl::ParticleArg const& p) {
    if (!p.valueEqual) return false;
    auto s = bx::sinisterSpawner(region, pos);
    if (!s) return false;
    auto entries = entriesOf(region, pos, *s);
    auto before  = entries.size();
    std::erase_if(entries, [&](Entry const& e) { return e.id == p.id && e.options == p.options; });
    if (entries.size() == before) return false;
    storeEntries(region, pos, *s, entries);
    return true;
}

bool tableExists(std::string const& id) { return gf::loot::ext::tableExists(id); }

/// BlockPosArgument#getLoadedBlockPos: the region and position, or the Java error.
BlockSource* loadedPos(CommandOrigin const& origin, CommandPosition const& at, int version, BlockPos& pos, std::string& error) {
    auto* dim = origin.getDimension();
    if (!dim) {
        error = "That position is not loaded";
        return nullptr;
    }
    auto* region = game::regionOf(static_cast<int>(dim->getDimensionId()));
    pos          = game::blockPosOf(at.getPosition(version, origin, Vec3{0, 0, 0}));
    if (!region || game::blockIdAt(*region, pos).empty()) {
        error = tr("argument.pos.unloaded", "That position is not loaded");
        return nullptr;
    }
    if (pos.y < region->getMinHeight() || pos.y >= region->getMaxHeight()) {
        error = tr("argument.pos.outofworld", "That position is out of this world!");
        return nullptr;
    }
    return region;
}

bool isSinister(BlockSource& region, BlockPos const& pos) { return game::blockIdAt(region, pos) == kSinister; }

void succeed(CommandOutput& output, bool changed) {
    if (changed) ++output.mSuccessCount; // Java returns 1 / 0 and sends no feedback
}

// ---- SinisterSpawnerBlock#particlesFromItem ------------------------------------------------------------

struct ItemParticles {
    std::vector<std::string> ids;
    Json                     params = Json::object();
};

std::optional<ItemParticles> particlesFromItem(std::string const& item) {
    auto is = [&](char const* boss, bool trophy = true, bool spawner = true) {
        return (trophy && item == std::string("twilightforest:") + boss + "_trophy") || (spawner && item == std::string("twilightforest:") + boss + "_boss_spawner");
    };
    if (is("naga")) return ItemParticles{{"minecraft:smoke", "minecraft:crit"}};
    if (is("lich")) return ItemParticles{{"minecraft:smoke", "twilightforest:ominous_flame"}};
    if (is("minoshroom")) return ItemParticles{{"minecraft:smoke", "minecraft:crimson_spore"}};
    if (is("hydra")) return ItemParticles{{"minecraft:smoke", "minecraft:flame"}};
    if (is("knight_phantom")) return ItemParticles{{"minecraft:smoke", "twilightforest:ominous_flame"}};
    if (is("ur_ghast"))
        return ItemParticles{{"minecraft:smoke", "minecraft:dust"}, Json{{"minecraft:dust", {{"color", {1.0, 0.0, 0.0}}, {"scale", 1.0}}}}};
    if (is("alpha_yeti")) return ItemParticles{{"twilightforest:snow", "minecraft:falling_water"}};
    if (is("snow_queen")) return ItemParticles{{"twilightforest:snow", "twilightforest:snow_warning"}};
    if (is("final_boss", false, true)) return ItemParticles{{"twilightforest:annihilate"}};
    if (is("quest_ram", true, false)) return ItemParticles{{"twilightforest:transformation_particle"}};
    return std::nullopt;
}

// ---- drops ---------------------------------------------------------------------------------------------

struct PendingBreak {
    std::string table;
    bool        creative = false;
    bool        player   = false;
};
std::map<GlobalPos, PendingBreak>& pendingBreaks() {
    static std::map<GlobalPos, PendingBreak> m;
    return m;
}

void dropLoot(BlockSource& region, BlockPos const& pos, std::string const& table, Player* player, ItemStack const* tool) {
    if (table.empty()) return;
    gf::loot::LootContext ctx;
    ctx.region = &region;
    ctx.origin = Vec3{pos.x + 0.5F, pos.y + 0.5F, pos.z + 0.5F};
    ctx.player = player;
    ctx.thisEntity = player;
    ctx.tool       = tool;
    for (auto const& stack : gf::loot::roll(table, ctx)) popResource(region, pos, stack);
}

class SinisterSpawnerModule {
public:
    void install() {
        static bool once = false;
        if (once) return;
        once = true;
        namespace ia = gf::interactions;
        // creative use with a trophy / boss spawner item: its particles
        ia::onUseOn({.block = kSinister}, [](ia::UseOnContext const& c) {
            if (!c.player.isCreative() || c.itemId.empty()) return ia::Result::Pass;
            auto particles = particlesFromItem(c.itemId);
            if (!particles) return ia::Result::Pass;
            if (!c.firstEvent) return ia::Result::Consume;
            auto* region = game::regionOf(c.pos.dim);
            if (!region) return ia::Result::Pass;
            BlockPos pos = game::blockPos(c.pos);
            if (auto s = bx::sinisterSpawner(*region, pos)) {
                // SinisterSpawnerLogic#setParticles(list, true): the set is replaced
                std::vector<Entry> entries;
                for (auto const& id : particles->ids) entries.push_back({id, {}});
                s->particleParams = particles->params;
                storeEntries(*region, pos, *s, entries);
            }
            return ia::Result::Consume;
        });
        // the break: remember the loot table and who broke it (BlockData is gone afterwards)
        ia::onDestroy({.block = kSinister}, [](ia::DestroyContext const& c) {
            auto* region = game::regionOf(c.pos.dim);
            if (!region) return ia::Result::Pass;
            pendingBreaks()[c.pos] = {bx::sinisterLootTable(*region, game::blockPos(c.pos)), c.player.isCreative(), true};
            return ia::Result::Pass;
        }, ia::priority::Low);
        ia::onDestroyed({.block = kSinister}, [](ia::DestroyContext const& c, bool destroyed) {
            auto it = pendingBreaks().find(c.pos);
            if (it == pendingBreaks().end()) return;
            auto pending = it->second;
            pendingBreaks().erase(it);
            if (!destroyed || pending.creative) return;
            auto* region = game::regionOf(c.pos.dim);
            if (!region) return;
            BlockPos pos = game::blockPos(c.pos);
            dropLoot(*region, pos, pending.table, &c.player, &c.item);
            // getExpDrop: 15 + nextInt(15) + nextInt(15) (no xp with silk touch in Java's
            // Block#getExpDrop path; TF overrides it without the check, so silk touch drops xp too)
            int xp = 15 + randomInt(15) + randomInt(15);
            ExperienceOrb::spawnOrbs(*region, Vec3{pos.x + 0.5F, pos.y + 0.5F, pos.z + 0.5F}, xp, ExperienceOrb::DropType::FromBlock, nullptr);
        });
        // other removals (explosions, ...): getDrops still adds the loot table
        bd::onErase([](GlobalPos const& gp, Json const& doc, std::string const& previous) {
            if (previous != kSinister) return;
            if (pendingBreaks().contains(gp)) return; // a player's break: handled above
            auto const* s = doc.contains("sinister_spawner") ? &doc["sinister_spawner"] : nullptr;
            if (!s || !s->is_object()) return;
            std::string table = s->value("loot", std::string()); // F8's SinisterSpawner JSON (Boss.cpp toJson)
            if (table.empty()) return;
            gf::interactions::nextTick([gp, table] {
                if (auto* region = game::regionOf(gp.dim)) dropLoot(*region, game::blockPos(gp), table, nullptr, nullptr);
            });
        });
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::feature::lichdecor

GF_REGISTER_MODULE(gf::feature::lichdecor::SinisterSpawnerModule);

GF_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    namespace ld = gf::feature::lichdecor;
    namespace cl = gf::core::lichdecor;
    // suggestions for <loot>: every table id is too many for a soft enum; TF's ominous drops first
    for (bool add : {true, false}) {
        cmd.overload<ld::SinisterParticleParams>()
            .text("sinister_spawner")
            .text(add ? "add_particle" : "remove_particle")
            .required("particle")
            .required("pos")
            .execute([add](CommandOrigin const& origin, CommandOutput& output, ld::SinisterParticleParams const& p, Command const& command) {
                if (!gf::commands::isAdmin(origin)) return output.error("You need operator permission.");
                cl::ArgError err;
                auto         particle = cl::parseParticle(p.particle, &err);
                if (!particle) return output.error(err.text); // Java key err.key; the text is already filled (it may hold SNBT braces)
                BlockPos    pos;
                std::string posError;
                int         version = command.mVersion;
                auto*       region  = ld::loadedPos(origin, p.pos, version, pos, posError);
                if (!region) return output.error(posError);
                if (!ld::isSinister(*region, pos)) return;
                ld::succeed(output, add ? ld::addParticle(*region, pos, *particle) : ld::removeParticle(*region, pos, *particle));
            });
    }
    cmd.overload<ld::SinisterLootParams>()
        .text("sinister_spawner")
        .text("set_loot")
        .required("loot")
        .required("pos")
        .execute([](CommandOrigin const& origin, CommandOutput& output, ld::SinisterLootParams const& p, Command const& command) {
            if (!gf::commands::isAdmin(origin)) return output.error("You need operator permission.");
            cl::ArgError err;
            auto         loot = cl::parseLoot(p.loot.mText, &ld::tableExists, &err);
            if (!loot) return output.error(err.text); // Java key err.key; the text is already filled (it may hold SNBT braces)
            BlockPos    pos;
            std::string posError;
            int         version = command.mVersion;
            auto*       region  = ld::loadedPos(origin, p.pos, version, pos, posError);
            if (!region) return output.error(posError);
            if (!ld::isSinister(*region, pos)) return;
            // an inline table has no key: like clear_loot
            ld::succeed(output, gf::boss::ext::setSinisterLootTable(*region, pos, loot->inlineTable ? std::string() : loot->id));
        });
    cmd.overload<ld::SinisterPosParams>()
        .text("sinister_spawner")
        .text("clear_loot")
        .required("pos")
        .execute([](CommandOrigin const& origin, CommandOutput& output, ld::SinisterPosParams const& p, Command const& command) {
            if (!gf::commands::isAdmin(origin)) return output.error("You need operator permission.");
            BlockPos    pos;
            std::string posError;
            int         version = command.mVersion;
            auto*       region  = ld::loadedPos(origin, p.pos, version, pos, posError);
            if (!region) return output.error(posError);
            if (!ld::isSinister(*region, pos)) return;
            ld::succeed(output, gf::boss::ext::setSinisterLootTable(*region, pos, std::string()));
        });
});
