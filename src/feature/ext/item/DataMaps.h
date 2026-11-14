#pragma once

// Extension of contract "data" (data maps) by F9 (rule R18; to be folded into
// feature/datamaps/DataMaps.h by A8).
//
// A data map's values can hold ids of a registry: upstream's crumble_horn turns a block into "crumble_to"
// (a block), transformation_powder and ominous_fire turn an entity into "transform_to" (an entity
// type). upstream's data spells vanilla ids the Java way; at load the runtime renames the keys (by the
// map's registry) and the value fields it knows to hold ids to the game's Bedrock ids
// (tags/JavaIds.h), and leaves every other field as it is (a sound id, a colour, a chance). Built in,
// from the codecs of upstream 4.8 and NeoForge 21.1:
//   gloamingforest:crumble_horn          crumble_to            block
//   gloamingforest:transformation_powder transform_to          entity_type
//   gloamingforest:ominous_fire          transform_to          entity_type
//   neoforge:oxidizables                 next_oxidation_stage  block
//   neoforge:waxables                    waxed                 block
// The consumer of another map registers its id fields (rule R7); the data maps load again.
//
//     gf::datamaps::ext::registerIdField("gloamingforest:my_map", "result", "block");

#include <string>

namespace gf::datamaps::ext {

/// The top-level field `field` of the values of `mapId` holds an id of registry directory
/// `registryDir` ("block", "item", "entity_type"): its Java vanilla ids become the game's.
void registerIdField(std::string const& mapId, std::string const& field, std::string const& registryDir);

} // namespace gf::datamaps::ext
