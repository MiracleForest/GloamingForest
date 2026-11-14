# U9 `artifacts`: Bedrock notes

What U9 does differently from upstream 4.8 on Bedrock, and why (docs/spec/artifacts.md is the behaviour).

## Death keeping

- The charm of life is a `death::onLethal` handler at `combat::priority::Highest` (upstream's LivingDeathEvent
  at HIGHEST). Charms are found in armour, main inventory, then off-hand (GloamingItemStackUtils).
- The charms of keeping run as the first `death::registerKeeper`: the first stack the keepers see
  plans the whole inventory with `core::artifacts::applyKeeping` (upstream's tiers, scopes, the charm I
  `NonNullList.of` quirk, the one-casket-left rule), takes the kept stacks out of their slots and stores
  them in `gf:artifacts:kept:<uuid>` with the consumed charm. F10's lowest keeper then keeps
  `#gloamingforest:kept_on_death`, as upstream does after the charm. `death::onRespawn` returns the stacks
  (their slot when empty, else anywhere, else at the feet) and plays the charm animation.
- The keepsake casket is placed from `death::onDropsRemaining`. Bedrock has no logical height apart
  from the build height: the casket's y is capped by the dimension's top.
- When upstream would place a skull chest (damage 2 and the 15 % roll) and U22's skull chest is not a
  container (yet), the casket stays at damage 2.

## Charm animation

`SpawnCharmPacket` spawns a client-side entity in Java. Here the server spawns
`gloamingforest:charm_effect` (not saved) that circles the player's eyes for 200 ticks, so other
players see it too. With `totemCharmAnimation` (read from the server's `artifacts` section) the
totem activation event goes to the player instead. The sound plays to the player only.

## Scepters

- The lifedrain beam is drawn by the server for everyone (`gloamingforest:magic_effect` along the
  beam from upstream's third-person hand position), not by a client packet: the particle effect takes its
  colour from the sign of the velocity it gets (down: upstream's red, up: the gloaming scepter's teal),
  because F10's fx gives upstream particles no colour variable on vanilla clients. `artifacts.lifedrain_particle`
  is not sent.
- A dispensed gloaming scepter bolt needs a shooter for F8's simulation: the nearest player within
  64 blocks shoots it (it has no owner). Without a player near, the dispenser fails.
- The killing blow of the lifedrain scepter plays no entity death sound (Bedrock's death sounds are
  per entity in the resource pack).
- Renewal (U10's enchantment) is read by id (`gloamingforest:renewal`) through `gf::enchant::level`.

## Fortification shields

Players keep their shields in `gf:artifacts:shields:<uuid>`, other entities in their entity data.
The invulnerability frames of the shield breaks are U9's own 20-tick counter (damage is cancelled, so
the game's frames never start). Companions draw upstream's HUD from `artifacts.shields`; vanilla players see
the count on the action bar while it changes. A mob's shields are read again when it is hit or commanded after a world reload; until then its temporary shields do not count down. A player's shields are lost on death (upstream's attachment is not copied on death). `/gloamingforest shield <target> set|add <amount> [<temp>]`
takes an entity selector (exactly one entity). `gloamingforest:give_3_shields` is the Bedrock function
`gloamingforest/give_3_shields` (`execute as @p run scriptevent gloamingforest:give_3_shields`, answered
by `src/feature/ext/artifacts/Give3Shields.cpp`).

## Keepsake casket

A BlockData container (F10's inventory contract, `assets/data/containers/artifacts.json`, 45 slots).
Owner and name are in the block data; the damage is the block state. The lid does not animate (no
block entity renderer); the block shows the closed casket model rotated by `facing`. The damaged item
names (chipped / damaged) are the first lore line.

## Potion flasks

Bedrock has no right-click-onto-a-stack in inventories: a flask is filled by using it while a potion
is in the other hand, or by using a potion while a flask is in the other hand. A filled flask carries a
stack nonce so it never stacks (Java's stack size 1). The dose bar is F9's item bar; the tooltip is text
lore (the potion name and a dose meter) instead of upstream's tooltip image. With the client mod the icon
follows upstream's item models (doses 1..3, splintered / damaged glass) with the liquid in the potion's colour
(ColorHandler): 264 prebuilt composites (22 potion colours x 12 dose / breakage looks, upstream-derived, under
`assets/pack/rp/textures/items/artifacts/upstream-derived/`) picked through U11's icon layers; vanilla clients
see the empty flask.

## Ore meter

The wipe is a swing at nothing (LeviLamina's PlayerSwingEvent with no block or entity in reach) or
`artifacts.wipe_ore_meter` from a client. Block names in the results are the server's display names.
Vanilla players get "Loading..." on the action bar and the results as chat lines.

## Not ported

- Emperor's cloth hiding the armour on the wearer: registered with F12's `hideArmor` (client mod), but
  F12 notes that 1.26.51's v2 actor renderer draws players' worn armour without the check it hooks, so
  it shows no effect on players yet (feature/ext/ui/Client.h); vanilla clients always see the armour. The
  cloth recipe, tooltip and washing work. The cloth recipe's armour layout uses Bedrock's
  `minecraft:is_armor` item tag.
- The mystic crown's inventory icon is the crown splinter's (upstream draws the lich's hat model; Bedrock's
  slot icons are atlas sprites and F12's GUI item renderer does not run for inventory slots on 1.26.51).
- The pocket watch's Mining Fatigue immunity removes an instance on the next tick (no
  "effect applicable" event).

## Audit round 2

Fixed:

- **Emperor's cloth data key.** U9 wrote the cloth as item data `emperors_cloth`, U7's no-template smithing
  (and its selftest) as upstream's component id `gloamingforest:emperors_cloth`, so a piece clothed in the
  smithing fallback had no tooltip and could not be washed. U9 now writes upstream's id (value `{}`, Unit) and
  still reads and washes off the old key. Selftest `artifacts.recipes`.
- **Duplicate cloth recipe.** U7's crafting-table copy of `gloamingforest:emperors_cloth_smithing`
  (armour + cloth, elytra + cloth) duplicated U9's `gloamingforest:emperors_cloth_recipe_{armor,elytra}`
  (upstream's own crafting recipe, same match and result): Bedrock logged a duplicate-recipe warning. U7 drops
  its copy (U7 PR); U9's recipe is the one crafting table recipe.
- **Loyal zombie despawning.** LoyalZombie#removeWhenFarAway (only untamed ones) was already the
  `gf:untamed` group; it no longer despawns at the simulation edge (Java has no such rule), and
  `artifacts.loyal_zombie` checks the tamed zombie has no `minecraft:despawn` and an untamed one has.
- **Flask icons** (client mod): see Potion flasks.
- **Emperor's cloth armour hiding** (client mod): `hideArmor` registered (see Not ported for F12's gap).

Open:

- Keepsake casket lid animation (ChestLidController via SkullChestRenderer#renderCasket): needs the lid
  as its own bone hidden by a block state while a block renderer draws it opening; not done.
- Mystic crown GUI model (see Not ported).
- Client visuals of this round (flask icons, armour hiding) were not run in the client e2e (round-2
  workers run server scenarios only).



## Audit round 2

Sounds (W2 round 2, audio pass). Checked with `tools/sounds/lint.ps1` on the assembled pack (`tools/assemble.ps1 -Check`): before this round 11 of this unit's sound events still played F3's name-derived baseline placeholder. Keepsake casket, fortification shield (add, block, break, expire), life, gloaming and zombie scepters. Files upstream shares with other owners keep one design: the scepter drain / gloaming_use / zombie files use U21 lich's existing designs, the casket files are mirrored by U22 lichdecor, and the zombie-on-iron-door stand-in (shield block) is the same as U8 weapons' knightmetal shatter. `tools/sounds/recipes/artifacts.json` now designs every one of them (original synthesized sounds, unit-local patches layered from the shared library; upstream's audio never read; ids, subtitles, categories, volumes, pitches and variant counts unchanged), rendered with `pwsh tools/sounds/synth.ps1 -Unit artifacts` into `assets/gen/audio/artifacts/`. `synth.ps1 -Check` and `lint.ps1` pass; with the other round-2 audio PRs (one per owning unit) the lint reports 0 baseline-only events for the whole pack.

Open: nothing for audio.
