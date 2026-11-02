#pragma once

// Extension of contract "enchant+effects" (enchantments) by F9 (rule R18; to be folded into
// feature/enchant/Enchant.h by A8).
//
// - Flags: Java 1.21 has no treasure / tradeable / discoverable fields on an enchantment; they are
//   the vanilla enchantment tags #minecraft:treasure, #minecraft:tradeable and
//   #minecraft:in_enchanting_table. So Spec's `treasure`, `tradeable` and `discoverable` are read
//   from those tags for every spec, the data's and an owner's registerType alike (TF 4.8 puts none
//   of its four enchantments in any of them: the enchanting table never offers them). An owner that
//   wants other flags than its tags give sets them here; they win over the tags and the Spec.
// - Vanilla clients and the anvil: a vanilla client previews the anvil itself and offers no result
//   for a merge whose second item brings custom enchantments only (an enchanted book of TF's from
//   loot): the server's anvil merge never runs. Their fallback is the anvil form: a vanilla player
//   who sneaks and uses an anvil holding an enchanted book whose enchantments are all custom gets a
//   form of the items in their inventory the book can go on (a plain use opens the client's own
//   anvil screen, with a chat hint to sneak), with Java's AnvilMenu result and level cost; a
//   choice applies it as the anvil would (the levels, the prior work penalty, the 12 % chance that
//   the anvil is damaged, its sound). Players with the client mod use the anvil screen itself, and
//   books with vanilla enchantments too go through the anvil screen (the client offers their
//   vanilla result, the server adds the custom ones).

#include <string>

namespace gf::enchant::ext {

/// Java's tag-given flags of an enchantment.
struct Flags {
    bool treasure     = false; ///< #minecraft:treasure
    bool tradeable    = false; ///< #minecraft:tradeable
    bool discoverable = false; ///< #minecraft:in_enchanting_table
};
/// Sets an enchantment's flags explicitly (instead of its tags), before or after its registration.
void setFlags(std::string const& enchantmentId, Flags flags);
/// Goes back to the tags.
void clearFlags(std::string const& enchantmentId);

} // namespace gf::enchant::ext
