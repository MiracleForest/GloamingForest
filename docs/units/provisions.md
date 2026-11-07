# U10 `provisions`: provisions, enchantments, effects, music discs

Behaviour spec: [docs/spec/provisions.md](../spec/provisions.md). Code: `src/feature/provisions/`,
`src/feature/ext/provisions/` (the frosted / smash / recharge helpers other units call); selftests
`provisions.*` (dx 1000..1099).

## Audit round 1

Checked against TF 4.8: `RechargeScepterEffect` (renewal), the stale bread (`CustomDamageSwordItem`,
`Tiers.WOOD`), `Experiment115Block` (use, sneak use, eating).

Fixed:

- **Renewal on the zombie scepter.** The scepter repair ingredients were matched by `item` / `tag` only,
  so the zombie scepter's NeoForge components ingredient (a potion of Strength, long or strong) never
  matched and renewal never refilled it. Ingredients now also read `neoforge:components` (`items` plus the
  `minecraft:potion_contents` potion, matched by Bedrock's potion aux value) and Java item ids are mapped to
  Bedrock's. Any other potion is still no ingredient. Selftest `provisions.enchantments` (zombie scepter part).
- **Stale bread.** Tiers.WOOD's enchantability 15 (sword enchantments) and repair: the BP item now has
  `minecraft:enchantable` and `minecraft:repairable` with TF's `#minecraft:planks` (vanilla planks of
  1.21.1, TF's eight planks and `#twilightforest:towerwood`), and two stale breads combine. Selftest
  `provisions.stale_bread`.

Verified, nothing to change:

- Renewal's crafting remainders: Java gives a remainder only for a stack still non-empty after the shrink
  (`hasCraftingRemainingItem` of an emptied stack is false). TF's ingredients (golden apple, fermented
  spider eye, ender pearls, rotten flesh, potions of Strength that stack to 1) have no remainder, so none
  is ever given.
- Experiment 115's sneak use (`core::provisions::e115Action`, gtested): with empty hands a non-regenerating
  block gives a slice, a regenerating one is eaten (`PASS_TO_DEFAULT_BLOCK_INTERACTION` -> `useWithoutItem`).

Open:

- Frosted ice cube render (the client overlay of FrostedEffect), Experiment 115's alternate icons
  (`think` / `full` item predicates): client-mod drawing, not done this round.
