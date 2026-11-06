# U9 `artifacts`: Bedrock notes

What U9 does differently from TF 4.8 on Bedrock, and why (docs/spec/artifacts.md is the behaviour).

## Death keeping

- The charm of life is a `death::onLethal` handler at `combat::priority::Highest` (TF's LivingDeathEvent
  at HIGHEST). Charms are found in armour, main inventory, then off-hand (TFItemStackUtils).
- The charms of keeping run as the first `death::registerKeeper`: the first stack the keepers see
  plans the whole inventory with `core::artifacts::applyKeeping` (TF's tiers, scopes, the charm I
  `NonNullList.of` quirk, the one-casket-left rule), takes the kept stacks out of their slots and stores
  them in `gf:artifacts:kept:<uuid>` with the consumed charm. F10's lowest keeper then keeps
  `#twilightforest:kept_on_death`, as TF does after the charm. `death::onRespawn` returns the stacks
  (their slot when empty, else anywhere, else at the feet) and plays the charm animation.
- The keepsake casket is placed from `death::onDropsRemaining`. Bedrock has no logical height apart
  from the build height: the casket's y is capped by the dimension's top.
- When TF would place a skull chest (damage 2 and the 15 % roll) and U22's skull chest is not a
  container (yet), the casket stays at damage 2.

## Charm animation

`SpawnCharmPacket` spawns a client-side entity in Java. Here the server spawns
`twilightforest:charm_effect` (not saved) that circles the player's eyes for 200 ticks, so other
players see it too. With `totemCharmAnimation` (read from the server's `artifacts` section) the
totem activation event goes to the player instead. The sound plays to the player only.

## Scepters

- The lifedrain beam is drawn by the server for everyone (`twilightforest:magic_effect` along the
  beam from TF's third-person hand position), not by a client packet: the particle effect takes its
  colour from the sign of the velocity it gets (down: TF's red, up: the twilight scepter's teal),
  because F10's fx gives TF particles no colour variable on vanilla clients. `artifacts.lifedrain_particle`
  is not sent.
- A dispensed twilight scepter bolt needs a shooter for F8's simulation: the nearest player within
  64 blocks shoots it (it has no owner). Without a player near, the dispenser fails.
- The killing blow of the lifedrain scepter plays no entity death sound (Bedrock's death sounds are
  per entity in the resource pack).
- Renewal (U10's enchantment) is read by id (`twilightforest:renewal`) through `gf::enchant::level`.

## Fortification shields

Players keep their shields in `gf:artifacts:shields:<uuid>`, other entities in their entity data.
The invulnerability frames of the shield breaks are U9's own 20-tick counter (damage is cancelled, so
the game's frames never start). Companions draw TF's HUD from `artifacts.shields`; vanilla players see
the count on the action bar while it changes. `/twilightforest shield <target> set|add <amount> [<temp>]`
takes an entity selector (exactly one entity). `twilightforest:give_3_shields` is the Bedrock function
`twilightforest/give_3_shields` (`execute as @p run scriptevent twilightforest:give_3_shields`, answered
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
lore (the potion name and a dose meter) instead of TF's tooltip image. Flask icons do not show the
potion colour (Bedrock item icons are static).

## Ore meter

The wipe is a swing at nothing (LeviLamina's PlayerSwingEvent with no block or entity in reach) or
`artifacts.wipe_ore_meter` from a client. Block names in the results are the server's display names.
Vanilla players get "Loading..." on the action bar and the results as chat lines.

## Not ported

- Emperor's cloth hiding the armour on the wearer (no armour render hook in F12); the cloth recipe,
  tooltip and washing work. The cloth recipe's armour layout uses Bedrock's `minecraft:is_armor` item tag.
- The mystic crown's inventory icon is the crown splinter's (TF draws a 3D model).
- The pocket watch's Mining Fatigue immunity removes an instance on the next tick (no
  "effect applicable" event).
