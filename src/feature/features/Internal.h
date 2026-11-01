#pragma once

// Internals of the feature interpreter (F5): registries, the imported data, the tree state and the
// one-warning-per-id log. Not a contract: units use feature/features/Features.h and
// feature/ext/worldgen/FeatureKit.h.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "nlohmann/json.hpp"

#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/features/Features.h"

namespace gf::features::detail {

using Json = nlohmann::json;

/// Logs `message` once per `key` (unknown ids, unsupported properties).
void warnOnce(std::string const& key, std::string const& message);

/// "id" -> "minecraft:id".
[[nodiscard]] std::string normalizeId(std::string id);

/// A member of a JSON object by reference (no copy, unlike json::value): null / an empty object /
/// an empty array when `j` is not an object or has no such member. Keep pointers into config JSON
/// only through these, never through json::value(), which returns a temporary copy.
[[nodiscard]] inline Json const& field(Json const& j, char const* key) {
    static Json const null;
    if (!j.is_object()) return null;
    auto it = j.find(key);
    return it == j.end() ? null : *it;
}
[[nodiscard]] inline Json const& fieldObj(Json const& j, char const* key) {
    static Json const empty = Json::object();
    if (!j.is_object()) return empty;
    auto it = j.find(key);
    return it == j.end() ? empty : *it;
}
[[nodiscard]] inline Json const& fieldArr(Json const& j, char const* key) {
    static Json const empty = Json::array();
    if (!j.is_object()) return empty;
    auto it = j.find(key);
    return it == j.end() ? empty : *it;
}

// ---- registries (written at install, read on worker threads) --------------------------------------
struct Registries {
    std::map<std::string, FeatureFn>       types;
    std::map<std::string, PlacementFn>     placements;
    std::map<std::string, TrunkPlacerFn>   trunkPlacers;
    std::map<std::string, FoliagePlacerFn> foliagePlacers;
    std::map<std::string, TreeDecoratorFn> decorators;
    std::map<std::string, RootPlacerFn>    rootPlacers;
    std::map<std::string, StateProviderFn> stateProviders;
};
/// Read access (shared lock held while `fn` runs).
template <class F>
auto withRegistries(F&& fn);
std::shared_mutex& registriesMutex();
Registries&        registries();

template <class F>
auto withRegistries(F&& fn) {
    std::shared_lock lock(registriesMutex());
    return fn(static_cast<Registries const&>(registries()));
}

/// Built-in vanilla types (VanillaFeatures.cpp, Trees.cpp, Providers.cpp); idempotent.
void registerVanillaFeatures();
void registerVanillaPlacements();
void registerVanillaTrees();
void registerVanillaProviders();

// ---- data -----------------------------------------------------------------------------------------
struct Data {
    std::map<std::string, Json> configured; ///< configured_feature id -> JSON
    std::map<std::string, Json> placed;     ///< placed_feature id -> JSON
    std::map<std::string, Json> biomes;     ///< biome id -> JSON
    std::map<std::string, Json> carvers;    ///< configured_carver id -> JSON
    std::map<std::string, std::unordered_set<std::string>> biomeFeatureSets; ///< biome id -> its placed feature ids
    std::map<std::string, std::vector<std::vector<std::string>>> biomeSteps;   ///< biome id -> placed feature ids per step
    std::uint64_t generation = 0; ///< increases with every (re)load (cache keys)
};
/// The imported worldgen data of every namespace under <mod>/data (loaded once), plus the vanilla
/// features TF's biomes reference.
[[nodiscard]] Data const& data();
/// Reloads the data (tests, /twilightforest reload).
void reloadData();
/// The vanilla placed / configured features TF's biomes use ({"placed": {...}, "configured": {...}}).
[[nodiscard]] Json const& vanillaFeatures();

// ---- placement --------------------------------------------------------------------------------------
/// Places a configured feature JSON ({type, config}) at `pos` (Feature#place: origin must be writable).
bool placeConfiguredObject(Json const& configured, std::string const& id, Region& region, BlockPos const& pos, RandomSource& random);
/// Runs a placed feature JSON ({feature, placement}); `topFeature` is the placed feature id the
/// biome filter checks ("" when placed directly).
bool placePlacedObject(Json const& placed, std::string const& topFeature, Region& region, BlockPos const& pos, RandomSource& random);
/// Whether the biome at `pos` lists placed feature `placedId` (BiomeFilter).
[[nodiscard]] bool biomeHasFeature(std::string const& biomeId, std::string const& placedId);
/// Applies the LargeWriter blocks queued for the center chunk of `region`; returns their count.
int drainLargeWriter(int dimension, Region& region);

// ---- trees ------------------------------------------------------------------------------------------
/// The TreeContext TreeFeature hands to placers and decorators, with Java's position sets.
struct TreeState : TreeContext {
    std::unordered_set<std::int64_t> logSet, leafSet, rootSet, decorationSet;
    std::vector<BlockPos>            decorations;
};
[[nodiscard]] std::int64_t packPos(BlockPos const& pos);

// ---- regions -----------------------------------------------------------------------------------------
/// A Region backed by a game BlockSource (the generator's and the selftests'); the others are
/// in-memory regions.
struct SourceRegion : worldgen::Region {
    [[nodiscard]] virtual BlockSource* source() = 0;
};

// ---- blocks ------------------------------------------------------------------------------------------
/// The Bedrock air block.
[[nodiscard]] Block const& air();
/// Forgets the Java state -> permutation cache (pack blocks are registered per world).
void clearBlockCache();
/// A block as {"name", "states"} (LargeWriter persistence) and back.
[[nodiscard]] Json        blockToJson(Block const& block);
[[nodiscard]] Block const* blockFromJson(Json const& json);
/// Java property value of `block` ("axis" -> "y"), nullopt when absent.
[[nodiscard]] std::optional<std::string> javaProperty(Block const& block, std::string_view name);
/// The members of block tag `tag` in the imported data (<mod>/data/<ns>/tags/block), cached per
/// loaded data; empty when the tag has no file.
[[nodiscard]] std::shared_ptr<std::unordered_set<std::string> const> dataBlockTag(std::string const& tag);

} // namespace gf::features::detail
