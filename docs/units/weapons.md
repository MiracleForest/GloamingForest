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

## Audit round 2

Sounds (W2 round 2, audio pass). Checked with `tools/sounds/lint.ps1` on the assembled pack (`tools/assemble.ps1 -Check`): before this round 2 of this unit's sound events still played F3's name-derived baseline placeholder. Peacock fan gust and knightmetal shield shatter (stand-ins for Java vanilla sounds Bedrock lacks; the shatter's zombie-on-iron-door stand-in is the same design as U9 artifacts' fortification shield block, one Java sound). `tools/sounds/recipes/weapons.json` now designs every one of them (original synthesized sounds, unit-local patches layered from the shared library; TF's audio never read; ids, subtitles, categories, volumes, pitches and variant counts unchanged), rendered with `pwsh tools/sounds/synth.ps1 -Unit weapons` into `assets/gen/audio/weapons/`. `synth.ps1 -Check` and `lint.ps1` pass; with the other round-2 audio PRs (one per owning unit) the lint reports 0 baseline-only events for the whole pack.

Open: nothing for audio.
