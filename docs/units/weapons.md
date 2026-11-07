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

Checked: the round-1 F8 / F10 entry points (S3 / S4 "Open"), TF's `ItemModelGenerator#bowTex` / `iceBowTex`,
`TFCreativeTabs#createGlassSwordAndLoreVer` against F9's catalog extension.

Fixed:

- **Parrying** (`Melee.cpp` blockAndParry, `EntityEvents#onParryProjectile`): a parried TF projectile now
  belongs to the blocker (`gf::projectile::ext::setOwner`, Java's `Projectile#setOwner`) and its impact is
  cancelled (`ext::cancelImpact`, `ProjectileImpactEvent#setCanceled`) with the AIM_DEFLECT velocity set in
  the hit; the next-tick velocity re-set is gone. A shooterless projectile (its own causing entity now) is
  parried too, as TF's ProjectileImpactEvent handler does. `weapons.shield_and_fan` checks the new owner.
- **Block and chain**: the "flight" velocity saved in onHitBlock and given back on the next tick is gone; the
  hit sets the velocity it flies on with (the kit's ThrowableProjectile behaviour, F8 round 1).
- **Damage pair**: `causingAndDirect` (World.cpp, Common.cpp) is removed; F10 reports attacker = the owner,
  direct = the projectile, which the bows, the cube and the shield read as is.
- **Bow pull frames**: RP attachables for the four bows (`assets/pack/rp/attachables/weapons/*.json`, render
  controller `controller.render.twilightforest.bow`): held like the vanilla bow (its geometries and wield
  animations) with `pulling_0` while drawn, `pulling_1` from 0.65 s and `pulling_2` from 0.9 s
  (`q.item_in_use_duration`, the pull predicate of `bowTex`). Every client, vanilla included. The client
  scenario shoots `weapons-bow-{standby,pulling0,pulling2}`.

Open:

- The pull frames are not verified on a client yet (client scenario written, not run): the attachable's
  `q.item_in_use_duration` and the vanilla bow geometries' texture keys are what the vanilla 1.16 bow
  attachable used. The inventory / hotbar icon stays the standby icon (Bedrock draws item icons statically;
  vanilla's own bow icon does not change either).
- The ice bow's attachable draws its solid layer only (TF's model has a translucent `ice_bow_clear` second
  layer; the inventory icon also shows the solid layer only).
- The creative-only infinite glass sword entry: F9's `gf::catalog::ext::setDefaultData` gives one data to
  every catalog entry of an id, and the item catalog cannot list the glass sword twice with different data
  (union merge of `item_catalog.d`, no per-entry hook). Needs an F9 API for a second, data-carrying entry.
