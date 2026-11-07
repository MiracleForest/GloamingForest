# U8 `weapons`: weapons and throwables

Behaviour spec: [docs/spec/weapons.md](../spec/weapons.md). Code: `src/feature/weapons/`; selftests
`weapons.*` (dx 800..899) in `src/feature/weapons/Tests.cpp`.

## Audit round 1

Checked against TF 4.8: the four bows (`TripleBowItem`, `EnderBowItem`, `SeekerBowItem`, `IceBowItem`),
`OreMagnetItem`'s enchanting rules, the glass sword's creative entry.

Fixed:

- **Punch on the triple and ender bows.** Their arrows are the game's `minecraft:arrow`, spawned by U8
  (Bedrock's own `BowItem::releaseUsing`, which sets Punch, never runs for TF's bows), so Punch was lost.
  The arrow now carries the bow's Punch level (entity data `weapons:punch`) and its first hit on a living
  entity applies `AbstractArrow#doKnockback` (arrow motion, x 0.6 per level, knockback resistance).
  Selftest `weapons.bow_punch`.
- **Ore magnet enchantments.** TF: `isEnchantable` false (no enchanting table) and the item is in
  `#minecraft:enchantable/durability`, so books of Unbreaking and Mending (and Curse of Vanishing) apply
  at the anvil (`isBookEnchantable` reads `getEnchantments()` of the book, which is empty for a stored
  enchantment, so its Unbreaking filter never rejects). The BP item had no `minecraft:enchantable`; it
  now has the flint-and-steel slot with enchantability 0 (the same Bedrock set: Unbreaking, Mending,
  Curse of Vanishing; no table offers). Selftest `weapons.ore_magnet_enchants`.

Open:

- Bow pull icons (`pulling_0..2` item predicates): Bedrock item icons are static for data-driven items;
  needs the client mod's item renderer (F12 `registerItemRenderer`).
- The creative-only infinite glass sword entry (`TFCreativeTabs#createGlassSwordAndLoreVer`: lore,
  Unbreakable, `infinite_glass_sword`): Bedrock's creative list holds item ids, not stacks with data;
  needs a creative item hook with a user-data stack (F9's catalog). The tooltip and the no-break rule for a
  stack carrying `infinite_glass_sword` already work.
