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

## Audit round 2

Fixed:

- **Frosted ice cubes** (IceLayer, client mod). Java reads the frosted_slowdown modifier from the attributes it
  syncs to every client; Bedrock clients see only their own effects, so the server sends each companion the frosted
  living entities within 64 blocks in its dimension (`provisions.frosted` {entities: [[unique id, amplifier]]}, checked
  every 5 ticks, sent when the list changes, again after a companion joins) and `src/client/provisions/Frosted.cpp`
  draws IceLayer's cubes: Java's seeded `java.util.Random` (id * id * 3121 + id * 45238971, the runtime id as
  Java's network id), int(height / 0.4) + amplifier + 2 half-block cubes in LivingEntityRenderer's model space
  (body yaw, the flip, -1.501), the death tome's offset, not on the local player in first person. The cube is
  `geometry.twilightforest.provisions.ice_cube` with the game's ice texture through `drawGeo` (alpha-tested, not
  translucent as Java's translucentMovingBlock; the random tumble uses drawGeo's y-x-z order). Selftest
  `provisions.frosted_sync`; the client scenario frosts a showcase pig (not run this round).
- **Experiment 115 icons** (client mod): the `think` variant (`twilightforest:e115_variant`, the advancement icon of
  `experiment_115_115`) shows `item/think115` through U11's icon layers (`tf_provisions_think115`).

Open:

- The `full` variant (TF: the block model `experiment115_8_8_regenerating` as the item) keeps the item icon: slot
  icons are item atlas sprites and F12's GUI item renderer does not run for slots on 1.26.51.
- Vanilla clients get neither (no companion data, no icon layers).
