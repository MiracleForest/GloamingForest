#pragma once

// GloamingForest - U22 (lichdecor): pure parts of /gloamingforest sinister_spawner (Java's
// ParticleArgument, ResourceOrIdArgument and ResourceLocation rules over the 1.21.1 particle
// registry) and of the wrought iron fence (WroughtIronFenceBlock#shouldBePost / #makeFenceState).
// No game includes: gtested in tests/lichdecor.

#include <optional>
#include <string>
#include <vector>

namespace gf::core::lichdecor {

/// A parse failure: the Java translation key and its English text (arguments filled in).
struct ArgError {
    std::string key;
    std::string text;
};

/// ResourceLocation#isValidNamespace / #isValidPath of "ns:path" (or "path").
[[nodiscard]] bool validResourceLocation(std::string const& id);
/// "path" -> "minecraft:path"; "ns:path" unchanged.
[[nodiscard]] std::string withDefaultNamespace(std::string const& id);

/// Every particle type of the 1.21.1 registry plus upstream's (sorted ids).
[[nodiscard]] std::vector<std::string> const& particleRegistry();
[[nodiscard]] bool                            isRegisteredParticle(std::string const& id);

/// A parsed `<particle>` argument.
struct ParticleArg {
    std::string id;         ///< namespaced
    std::string options;    ///< the "{...}" SNBT text, "" when none
    bool        valueEqual; ///< HashSet membership compares by value (SimpleParticleType, records)
};
/// ParticleArgument#readParticle: an id, then optional SNBT options checked against the type's
/// required fields. The error comes in Java's order (id, then options).
[[nodiscard]] std::optional<ParticleArg> parseParticle(std::string const& text, ArgError* error);

/// A parsed `<loot>` argument: an id (namespaced) or an inline table (no key, `inlineTable`).
struct LootArg {
    std::string id;
    bool        inlineTable = false;
};
/// ResourceOrIdArgument#parseInlineOrId (+ the existence check, `exists(id)`).
[[nodiscard]] std::optional<LootArg> parseLoot(std::string const& text, bool (*exists)(std::string const&), ArgError* error);

/// Minimal SNBT compound check: the top-level keys of "{k: v, ...}" (nested values are skipped).
/// nullopt with a Java `argument.nbt.*` style error when it is not one compound.
[[nodiscard]] std::optional<std::vector<std::string>> snbtKeys(std::string const& text, ArgError* error);

// ---- wrought iron fence -----------------------------------------------------------------------------

enum class FenceSide { None, Middle, Bottom, Top, Full };
enum class PostState { None, Post, Capped };
[[nodiscard]] char const*              fenceSideName(FenceSide s);
[[nodiscard]] std::optional<FenceSide> fenceSideOf(std::string const& name);
[[nodiscard]] char const*              postStateName(PostState s);
[[nodiscard]] std::optional<PostState> postStateOf(std::string const& name);

/// WroughtIronFenceBlock#shouldBePost (sides in N, E, S, W order).
[[nodiscard]] bool shouldBePost(FenceSide n, FenceSide e, FenceSide s, FenceSide w);
/// WroughtIronFenceBlock#makeFenceState: whether the fence above / below continues that side.
[[nodiscard]] FenceSide fenceSide(bool above, bool below);

} // namespace gf::core::lichdecor
