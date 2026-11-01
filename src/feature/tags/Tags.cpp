// Neutral stub of the tag part of "data" (F0). F9 replaces this file (rule R18).
#include "feature/tags/Tags.h"

#include "core/Stub.h"

namespace gf::tags {

bool has(Registry, std::string const&, std::string const&) {
    GF_STUB("tags.has");
    return false;
}

std::vector<std::string> members(Registry, std::string const&) {
    GF_STUB("tags.members");
    return {};
}

char const* registryName(Registry registry) {
    switch (registry) {
    case Registry::Block:
        return "block";
    case Registry::Item:
        return "item";
    case Registry::EntityType:
        return "entity_type";
    case Registry::Biome:
        return "worldgen/biome";
    case Registry::Fluid:
        return "fluid";
    case Registry::DamageType:
        return "damage_type";
    case Registry::Enchantment:
        return "enchantment";
    case Registry::Structure:
        return "worldgen/structure";
    case Registry::BannerPattern:
        return "banner_pattern";
    case Registry::PaintingVariant:
        return "painting_variant";
    case Registry::GameEvent:
        return "game_event";
    case Registry::Instrument:
        return "instrument";
    }
    return "";
}

} // namespace gf::tags
