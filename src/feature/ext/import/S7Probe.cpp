// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// Spike S7 follow-up (unit F1 "import", docs/spikes/S7.md): how a block JSON of each format_version declares block
// tags in BDS 1.26.51. The probe blocks gf_s7:<form>_<format> come from the throw-away pack of
// tools/e2e/server/scenarios/import.s7.prelaunch.ps1 (forms: "tag:<id>" components, "minecraft:tags" as an object
// or an array, each tag form with minecraft:block_entity); import.s7_probe reports, per probe, whether the game
// registered it, the tags of its block type and default permutation, and its block actor type. Without the probe
// pack (every other run) it reports the component name only and passes.
//
// Server build only: the probes run in the server harness, and the schema's name is read from the server binary.
//
// F1 owns no source directory, so this lives in src/feature/ext/import/ (new files only, rule R18).

#ifndef GF_CLIENT

#include <string>
#include <vector>

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/v1_26_20/block/components/Tags.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include "game/Blocks.h"
#include "selftest/SelfTest.h"

namespace {

// keep in step with $formats / $forms in tools/e2e/server/scenarios/import.s7.prelaunch.ps1
constexpr char const* kS7Formats[] = {"1_21_80", "1_26_0", "1_26_10", "1_26_20", "1_26_30", "1_26_40", "1_26_50"};
constexpr char const* kS7Forms[]   = {"legacy", "object", "array", "array_entity", "legacy_entity"};

std::string tagList(std::vector<HashedString> const& tags) {
    std::string s;
    for (auto const& t : tags) s += (s.empty() ? "" : ", ") + t.getString();
    return "[" + s + "]";
}

} // namespace

GF_REGISTER_SELFTEST("import.s7_probe", [](gf::selftest::Context& ctx) {
    // the component the 1.26.20+ block schema keeps tags in (SharedTypes::v1_26_20::BlockDefinition::Tags)
    ctx.info("block schema v1_26_20 tags component: '" + std::string(SharedTypes::v1_26_20::BlockDefinition::Tags::NameId()) + "'");
    int registered = 0, probes = 0;
    for (auto const* form : kS7Forms) {
        for (auto const* format : kS7Formats) {
            ++probes;
            std::string id = std::string("gf_s7:") + form + "_" + format;
            auto const* block = gf::game::blockWith(id);
            if (!block) {
                ctx.info(id + ": not registered");
                continue;
            }
            ++registered;
            auto const& type = block->getBlockType();
            ctx.info(
                id + ": type tags " + tagList(*type.mTags) + ", permutation tags " + tagList(*block->mTags) + ", pickaxe tag "
                + (type.hasTag(HashedString("minecraft:is_pickaxe_item_destructible")) ? "yes" : "no") + ", block actor type "
                + std::to_string(static_cast<int>(type.getBlockEntityType()))
            );
        }
    }
    ctx.info(std::to_string(registered) + " of " + std::to_string(probes) + " probe blocks registered"
             + (registered == 0 ? " (the probe pack of import.s7.prelaunch.ps1 is not installed in this run)" : ""));
});

#endif // GF_CLIENT
