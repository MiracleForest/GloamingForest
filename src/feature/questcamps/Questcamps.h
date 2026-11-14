// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
#pragma once

// U19 (questcamps): the Questing Ram and its quest (QuestRam.cpp, Quest.cpp), the quest grove
// (Grove.cpp), the camps (Camp.cpp) and upstream's template features (TemplateFeatures.cpp). Internal to
// the unit (rule R2: no other unit includes it).

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/world/level/BlockPos.h"

#include "core/questcamps/Questcamps.h"
#include "feature/structure/Structure.h"

class Actor;
class ItemStack;
class Mob;

namespace gf::feature::questcamps {

using Json = nlohmann::json;

inline constexpr char kRam[]        = "gloamingforest:quest_ram";
inline constexpr char kGrove[]      = "gloamingforest:quest_grove";
inline constexpr char kCamp[]       = "gloamingforest:camp";
inline constexpr char kGrovePiece[] = "gloamingforest:TFQuest1";
/// gf::entity::data key of a ram ({"ColorFlags", "Rewarded"}), synced to companions.
inline constexpr char kRamData[] = "questcamps";
/// The ram's entity property (the 16 wool segments of the model, every client).
inline constexpr char kColorProperty[] = "gloamingforest:color_flags";
/// The trigger the completed quest fires (QuestRamCompletionTrigger).
inline constexpr char kTrigger[] = "gloamingforest:complete_quest_ram";
/// The net channel of the quest context (SyncQuestsPacket).
inline constexpr char kSyncChannel[] = "questcamps.sync_quests";

/// Config section `questcamps` (docs/spec/config.md).
struct QuestcampsConfig {
    int  version = 1;
    /// client.questRamWoolIndicator (ram_indicator): the crosshair indicator (client mod).
    bool questRamWoolIndicator = true;
};

// ---- Quest.cpp: gloaming/quests/questing_ram (QuestReloadListener, QuestingRamContext) --------------------
struct Quest {
    std::array<Json, 16> items;  ///< Ingredient by dye id (null: none)
    std::string          reward = "gloamingforest:entities/questing_ram_rewards";
    bool                 fromData = false; ///< false: QuestingRamContext.FALLBACK
};
/// The current quest (a copy: reloads replace it).
[[nodiscard]] Quest quest();
void                reloadQuest();
/// Whether `itemId` matches an Ingredient JSON ({"item"}, {"tag"} or a list of them).
[[nodiscard]] bool ingredientMatches(Json const& ingredient, std::string const& itemId);
/// The dye ids whose quest ingredient `itemId` matches, in the quest map's order.
[[nodiscard]] std::vector<int> questColorsOf(std::string const& itemId);
/// The quest as the companion protocol sends it ({"items": {colour: ingredient}, "reward"}).
[[nodiscard]] Json questJson();

// ---- QuestRam.cpp ------------------------------------------------------------------------------------------
void installRam();
void uninstallRam();
[[nodiscard]] int  colorFlags(Actor& ram);
[[nodiscard]] bool rewarded(Actor& ram);
void               setColorFlags(Actor& ram, int flags);
void               setRewarded(Actor& ram, bool value);
/// QuestRam#isItemTempting.
[[nodiscard]] bool isItemTempting(Actor& ram, ItemStack const& stack);
/// QuestRam#tryAccept: sets the colour of a quest item the ram still needs (particles, sound).
bool tryAccept(Actor& ram, ItemStack const& stack);
/// QuestRam#rewardQuest: the rewards dropped, the trigger fired, the grove conquered.
void rewardQuest(Actor& ram);
/// FeaturePlacers.placeEntity's restriction point for a ram placed during generation: the ram
/// that appears at `pos` (its block) of `dim` takes it as its home when it first ticks.
void queueHome(int dim, BlockPos const& pos);
/// The number of homes waiting for their ram (selftests).
[[nodiscard]] std::size_t pendingHomes();

// ---- Grove.cpp ---------------------------------------------------------------------------------------------
void installGrove();
/// QuestGroveStructure#getFirstPiece at generation point (x, y, z).
[[nodiscard]] std::unique_ptr<structure::Piece> makeGrove(int x, int y, int z);

// ---- Camp.cpp ----------------------------------------------------------------------------------------------
void installCamp();

// ---- TemplateFeatures.cpp ----------------------------------------------------------------------------------
void installFeatures();
/// The feature types' bodies, for the selftests (region, origin, random, the configured "config").
bool placeDruidHut(structure::Region& region, BlockPos const& origin, structure::RandomSource& random, Json const& config);
bool placeSimpleWell(structure::Region& region, BlockPos const& origin, structure::RandomSource& random, Json const& config);
bool placeFancyWell(structure::Region& region, BlockPos const& origin, structure::RandomSource& random, Json const& config);
bool placeGroveRuins(structure::Region& region, BlockPos const& origin, structure::RandomSource& random);
bool placeStoneCircle(structure::Region& region, BlockPos const& origin, structure::RandomSource& random);
bool placeGraveyard(structure::Region& region, BlockPos const& origin, structure::RandomSource& random);
/// What the last placement of a template feature did on this thread (selftests).
struct FeatureReport {
    std::string              templateId;
    BlockPos                 placementPos{0, 0, 0};
    structure::Rotation      rotation = structure::Rotation::None;
    structure::Mirror        mirror   = structure::Mirror::None;
    std::vector<std::string> markers;   ///< handled data marker labels
    std::string              basement;  ///< the druid hut's basement template ("" none)
    int                      graves = 0, wraiths = 0, chests = 0;
    std::string              failure; ///< why the feature placed nothing ("" when it did)
};
[[nodiscard]] FeatureReport& lastFeatureReport();

} // namespace gf::feature::questcamps
