// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// Spike S7 follow-up (unit F1 "import", docs/spikes/S7.md): how a block JSON of each format_version declares block
// tags in BDS 1.26.51. The probe blocks gf_s7:<form>_<format> come from the throw-away pack of
// tools/e2e/server/scenarios/import.s7.prelaunch.ps1 (forms: "tag:<id>" components, "minecraft:tags" as an object
// or an array, each tag form with minecraft:block_entity), which lists every probe it writes in the pack's
// probes.json ({"probes": [{id, form, format}]}): the single list of the probes, read here. import.s7_probe reports,
// per probe, whether the game registered it, the tags of its block type and default permutation, and its block actor
// type, and checks them against what 1.26.51 measured (the minecraft:tags array and minecraft:block_entity from
// 1.26.20, "tag:<id>" components before, the item form never). Without the probe pack (every other run) it reports
// the component name and that no probe is installed (two lines), and passes.
//
// Server build only: the probes run in the server harness, and the schema's name is read from the server binary.
//
// F1 owns no source directory, so this lives in src/feature/ext/import/ (new files only, rule R18).

#ifndef GF_CLIENT

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/v1_26_20/block/components/Tags.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include "nlohmann/json.hpp"

#include "game/Blocks.h"
#include "mod/Main.h"
#include "selftest/SelfTest.h"

namespace {

/// The probe forms the expectations below know (the prelaunch's $forms).
constexpr std::array<std::string_view, 5> kS7Forms = {"legacy", "object", "array", "array_entity", "legacy_entity"};

std::string tagList(std::vector<HashedString> const& tags) {
    std::string s;
    for (auto const& t : tags) s += (s.empty() ? "" : ", ") + t.getString();
    return "[" + s + "]";
}

/// A format_version ("1.26.20") is 1.26.20 or later: the SharedTypes::v1_26_20 block schema reads it. nullopt: not
/// a three-part version.
std::optional<bool> modernFormat(std::string_view format) {
    std::array<int, 3> v{};
    size_t             part = 0;
    bool               digit = false;
    for (char c : format) {
        if (c == '.' || c == '_') {
            if (!digit || ++part == v.size()) return std::nullopt;
            digit = false;
        } else if (c >= '0' && c <= '9') {
            v[part] = v[part] * 10 + (c - '0');
            digit   = true;
        } else return std::nullopt;
    }
    if (part != v.size() - 1 || !digit) return std::nullopt;
    return v >= std::array<int, 3>{1, 26, 20};
}

} // namespace

GF_REGISTER_SELFTEST("import.s7_probe", [](gf::selftest::Context& ctx) {
    // the component the 1.26.20+ block schema keeps tags in (SharedTypes::v1_26_20::BlockDefinition::Tags)
    ctx.info("block schema v1_26_20 tags component: '" + std::string(SharedTypes::v1_26_20::BlockDefinition::Tags::NameId()) + "'");
    // the probe list the prelaunch wrote with the pack
    auto          list = gf::Mod::getInstance().getSelf().getModDir() / "behavior_packs" / "GloamingForest_S7_BP" / "probes.json";
    std::ifstream in(list);
    if (!in) {
        // without the probe pack (every run but import.s7) nothing more to report
        ctx.info("0 probe blocks listed (the probe pack of import.s7.prelaunch.ps1 is not installed in this run)");
        return;
    }
    auto doc = nlohmann::json::parse(in, nullptr, false);
    ctx.require(doc.is_object() && doc.contains("probes") && doc["probes"].is_array() && !doc["probes"].empty(), "the probe pack's probes.json lists the probes");
    struct Probe {
        std::string  form;
        std::string  id;
        std::string  format;
        Block const* block;
        bool         modern; ///< format 1.26.20 or later (the SharedTypes::v1_26_20 block schema)
    };
    std::vector<Probe> probes;
    for (auto const& p : doc["probes"]) {
        auto id     = p.is_object() ? p.value("id", std::string()) : std::string();
        auto form   = p.is_object() ? p.value("form", std::string()) : std::string();
        auto format = p.is_object() ? p.value("format", std::string()) : std::string();
        auto modern = modernFormat(format);
        bool known  = std::find(kS7Forms.begin(), kS7Forms.end(), form) != kS7Forms.end();
        if (id.empty() || !known || !modern) {
            ctx.check(false, "probes.json entry " + p.dump() + ": an id, a known form and a three-part format_version");
            continue;
        }
        probes.push_back({form, id, format, gf::game::blockWith(id), *modern});
    }
    int registered = 0;
    for (auto const& p : probes)
        if (p.block) ++registered;
    ctx.info(std::to_string(registered) + " of " + std::to_string(probes.size()) + " probe blocks registered");
    for (auto const& p : probes) {
        if (!p.block) {
            ctx.info(p.id + ": not registered");
            continue;
        }
        auto const& type = p.block->getBlockType();
        ctx.info(
            p.id + ": type tags " + tagList(*type.mTags) + ", permutation tags " + tagList(*p.block->mTags) + ", pickaxe tag "
            + (type.hasTag(HashedString("minecraft:is_pickaxe_item_destructible")) ? "yes" : "no") + ", block actor type "
            + std::to_string(static_cast<int>(type.getBlockEntityType()))
        );
    }
    // what 1.26.51 measured (docs/spikes/S7.md): the minecraft:tags array from 1.26.20, "tag:<id>" components before,
    // the item form never, minecraft:block_entity from 1.26.20 (then a data-driven block actor, type 62)
    for (auto const& p : probes) {
        bool want   = p.form == "array" || p.form == "array_entity" ? p.modern : p.form == "legacy" ? !p.modern : false;
        bool entity = p.form.ends_with("_entity");
        ctx.check(
            (p.block != nullptr) == want,
            p.id + " (format " + p.format + ")" + (want ? " registers" : " is refused") + " ("
                + (p.modern ? "the 1.26.20 block schema" : "a pre-1.26.20 format") + ")"
        );
        if (!p.block || !want) continue;
        auto const& type = p.block->getBlockType();
        ctx.check(
            type.hasTag(HashedString("minecraft:is_pickaxe_item_destructible")) && type.hasTag(HashedString("gf_s7:probe")),
            p.id + " carries both probe tags"
        );
        if (entity) ctx.check(static_cast<int>(type.getBlockEntityType()) != 0, p.id + " has a block actor");
    }
});

#endif // GF_CLIENT
