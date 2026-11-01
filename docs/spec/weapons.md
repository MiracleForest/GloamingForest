# Weapons and throwables (U8 `weapons`)

Behaviour spec of unit **U8** (wave W1), translated from The Twilight Forest 4.8 (`9e40a757`, MC 1.21.1) by F16. It is
the checklist the U8 worker implements and the A2 audit compares against. Sections 1-5 are the behaviour; Appendix A
lists every id U8 owns with TF's registration and data; Appendix B the strings.

## 1. Scope

Items `block_and_chain`, `crumble_horn`, `cube_of_annihilation`, `cube_talisman`, `ender_bow`, `glass_sword`,
`ice_bomb`, `ice_bow`, `ice_sword`, `knightmetal_shield`, `lamp_of_cinders`, `moonworm_queen`, `ore_magnet`,
`peacock_feather_fan`, `seeker_bow`, `transformation_powder`, `triple_bow`; projectile entities `chain_block`,
`cube_of_annihilation`, `ice_arrow`, `moonworm_shot`, `seeker_arrow`, `thrown_ice`; data maps `crumble_horn`,
`transformation_powder` (R7); attachment `feather_fan_falling`; data components `infinite_glass_sword`,
`thrown_projectile`; damage type `moonworm`; particle `annihilate`; 12 sound events; stat `blocks_crumbled`; trigger
`broke_glass_sword`; recipe type `moonworm_queen_repair_recipe`; shield parrying (config section `weapons`:
`common.Shield Parrying.parryNonTwilightAttacks`, `common.Shield Parrying.shieldParryTicksArrow`).

Not U8: the `destruction` enchantment and `smash_blocks` attachment (U10; the chain block calls them), the moonworm
block (U6), thorns blocks (U30), `frozen` / `spiked` damage types (other owners, used by id), the ICE and GLASS tool
tiers' repair tags (U7 rows).

Contracts: item, projectile, combat, effects, data, client.world, fx.

## 2. TF sources

`init/TFItems.java`, `init/TFEntities.java`, `init/TFDataMaps.java`, `init/TFDataComponents.java`,
`init/TFDataAttachments.java`, `item/ChainBlockItem.java`, `entity/projectile/ChainBlock.java`,
`item/CrumbleHornItem.java`, `item/CubeOfAnnihilationItem.java`, `entity/projectile/CubeOfAnnihilation.java`,
`item/EnderBowItem.java`, `item/GlassSwordItem.java`, `item/IceBombItem.java`, `entity/projectile/IceBomb.java`,
`item/IceBowItem.java`, `entity/projectile/IceArrow.java`, `item/IceSwordItem.java`, `item/KnightmetalShieldItem.java`,
`item/LampOfCindersItem.java`, `item/MoonwormQueenItem.java`, `entity/projectile/MoonwormShot.java`,
`item/OreMagnetItem.java`, `item/PeacockFanItem.java`, `item/SeekerBowItem.java`, `entity/projectile/SeekerArrow.java`,
`entity/projectile/TFArrow.java`, `entity/projectile/TFThrowable.java`, `item/TransformPowderItem.java`,
`item/TripleBowItem.java`, `item/recipe/MoonwormQueenRepairRecipe.java`, `events/ToolEvents.java` (ender bow swap,
ore magnet cache), `events/EntityEvents.java` (triple bow, parrying), `events/CapabilityEvents.java` (feather fan
fall), `dispenser/{TFDispenserBehaviors,FeatherFanDispenseBehavior,CrumbleDispenseBehavior,TransformationDispenseBehavior,DamageableStackDispenseBehavior}.java`,
`client/renderer/entity/{BlockChainRenderer,CubeOfAnnihilationRenderer,MoonwormShotRenderer,DefaultArrowRenderer}.java`,
`client/model/entity/{Chain,CubeOfAnnihilation,KnightmetalShield}Model.java`, `client/particle/AnnihilateParticle.java`,
`init/TFCreativeTabs.java` (infinite glass sword).

## 3. Behaviour

### 3.1 Block and chain (`ChainBlockItem`, entity `chain_block`)

- Item: durability 99, repair `#c:ingots/knightmetal`; use duration 72000, BLOCK animation. Use: refused while the
  stack has `thrown_projectile` or outside the world border; plays `item.twilightforest.block_and_chain.fire` (0.5,
  pitch 1 / (rand*0.4 + 1.2)); server spawns a `ChainBlock` and stores its UUID in `twilightforest:thrown_projectile`;
  the player keeps using the item. Inventory tick clears the component when the entity is gone.
- Entity (ThrowableProjectile, 0.6x0.6, tracking range 80, update 1, synced `hand`, `is_foil`, `is_returning`):
  shot from the thrower's rotation at speed **1.5**, inaccuracy 1.0; gravity **0.05**. Not able to use portals.
  - Hit entity (not the owner, inside the border): base damage **10.0** of type `twilightforest:spiked` (indirect,
    the chain as direct entity, owner as attacker), modified by the stack's enchantments; a player blocking with a
    shield gets the shield damaged by 5 and disabled; on success plays `...block_and_chain.hit` (1, rand),
    starts returning, `tickCount += 60`, the item loses 1 durability.
  - Hit block: if it cannot break the block (outside border, not correct tool for drops — only with Destruction,
    tier wood/stone/iron for levels 1/2/3 on `#twilightforest:mineable_with_block_and_chain` —, in
    `#block_and_chain_never_breaks`, adventure restrictions) or 12 blocks were already smashed, it bounces; then the
    stack's HIT_BLOCK enchantment effects run (Destruction smashes, U10).
  - Bounce: plays `...block_and_chain.collide` (0.125, rand) the first time, starts returning, velocity x0.6 and the
    component into the face reversed x0.6.
  - Tick (server): no owner -> removed; stuck (moving < 0.003 per tick horizontally) for **100** ticks or farther
    than **16** blocks -> returning. Returning: within 2 blocks of the owner it is removed (the item loses
    min(smashed, 3) durability if the owner smashed blocks); otherwise velocity = old * (1 - a) + dirToOwnerEye * 2a
    (a = min(tickCount * 0.03, 1)) minus gravity. Removal stops the owner's use.
- Renderer: chain links from the owner's hand to the ball (`BlockChainRenderer`), enchantment glint when `is_foil`.

### 3.2 Crumble horn (`CrumbleHornItem`)

Durability 1024, RARE, TOOT_HORN animation, use duration 72000; use plays `entity.twilightforest.quest_ram.ambient`
(1, 0.8). While held, on every use tick whose remaining-tick count is a multiple of 5 and above 10 (`onUseTick`
count = remaining ticks, 72000 at the start, so from the first tick on, every 5 ticks): plays the sound again and
crumbles a 4x4x4 box
(center = eye + look*3, from center-2 to center+1). For each block with a `twilightforest:crumble_horn` data-map entry
(`{result, chance_to_crumble}`, Appendix A): break event must pass; if result is air: with the chance, players break
it as if harvested (only if they can harvest it, drops via playerDestroy, destroy particles), non-players destroy it
if mob griefing; else with the chance replace it by `result` keeping properties (+ particles). Each crumble: stat
`twilightforest:blocks_crumbled` +1, stat `minecraft:used`, 1 durability (stops when broken). Dispenser: crumbles the
block in front (data map, ignores chance), 1 durability, fail sound when nothing happened, never uses the last point.

### 3.3 Cube of annihilation (`CubeOfAnnihilationItem`, entity `cube_of_annihilation`)

- Item: stack 1, fire resistant, UNCOMMON; disables shields when attacking; use (like the chain) spawns the cube,
  stores `thrown_projectile`, keeps using (BLOCK animation, 72000).
- Entity: no gravity; shot at speed 1.5. Each server tick it flies toward a point **16** blocks along the owner's look
  (or back to the owner's eye when returning) with max speed **0.5**; it annihilates every block in its box inflated
  by 0.2: the block must pass the break event and be in `#twilightforest:annihilation_inclusions` or have explosion
  resistance < **8** and be breakable (adventure rules apply); it is removed without drops, plays
  `block.twilightforest.generic.annihilation` (0.125, 0.75 + rand*0.25), sends 27 `twilightforest:annihilate`
  particles (3x3x3 grid, gaussian 0.2 velocity) to players within 32; an unbreakable block makes it return. Entity hit
  (living): 10 damage (player/mob attack source), `tickCount += 60`. Returns when it hit an obstacle, has no player
  owner or the owner stops using; removed within 2 blocks of the owner.
- `cube_talisman`: plain item (fire resistant, UNCOMMON), crafting / loot only.

### 3.4 Bows

All four: durability 384, UNCOMMON, vanilla bow draw.

- `triple_bow` (`TripleBowItem`): each fired arrow spawns 3 projectiles: the middle normal, the outer two with
  `minecraft:intangible_projectile` (not pickable) and +-0.15 vertical velocity (0.0075 * 20 * j); speed = power*2.5,
  accuracy 1.0, crit at full power; sound `entity.arrow.shoot`. Arrows from it strip the victim's invulnerability
  frames (`EntityEvents.entityHurts`: damage type `arrow` from a player holding the triple bow -> invulnerableTime 0).
- `seeker_bow` -> `seeker_arrow` (base damage **1.0**): while flying fast (speed^2 > 1) it picks a target in a box
  spanned by its motion x5 rotated +-30 degrees, inflated +-2.5 vertically: first a monster targeting the owner, then a
  non-neutral monster with line of sight, then the living entity (not owner, not owner's pets, with line of sight)
  best aligned with its motion (cosine > 0.5). Steering: new motion = (course + 0.8*toTarget) scaled to keep length,
  +0.045 y; drops the target when cosine <= 0.5. Client trail: 4 `witch` particles per tick. Never critical on hit.
- `ice_bow` -> `ice_arrow`: client trail 4 snow `falling_dust` particles; on hitting a living entity applies
  `twilightforest:frosted` 200 ticks amplifier 2 (chill aura rules). Repairs with `#repairs_ice_tools`.
- `ender_bow`: marks arrows with persistent tag `twilightforest:ender`; on hitting a living non-boss
  (`#c:bosses` excluded) entity that is not the shooter, the shooter and the target swap positions and rotations
  (vehicles swapped too), the shooter gets 40 invulnerability ticks, entity event 46 (teleport particles) and
  `item.chorus_fruit.teleport` at both.
- TF arrows (`TFArrow`) pick up as a normal `minecraft:arrow` and apply the parent arrow's post-hurt effects (tipped
  arrows keep their effects).

### 3.5 Swords

- `ice_sword` (ICE tier: 32 uses, attack +3.5 tier bonus, sword +3 / -2.4): on a successful hit applies frosted 200 t,
  amplifier 2 and sends 20 `twilightforest:snow` particles around the target to trackers.
- `glass_sword` (GLASS tier: 1 use, +36 attack, ench. 30; UNCOMMON; no repair): every hit sends 20 white stained
  glass block particles to trackers and breaks the sword (unless creative or the item has
  `twilightforest:infinite_glass_sword`): plays `item.twilightforest.glass_sword.break` (1, 0.5), stat
  `minecraft:broken`, triggers `twilightforest:broke_glass_sword` and durability-changed. The creative tab also offers
  an unbreakable copy with `infinite_glass_sword` and lore `item.twilightforest.glass_sword.desc`.

### 3.6 Knightmetal shield (`KnightmetalShieldItem`)

Durability 1024, vanilla shield behaviour and `blocking` model predicate, no tooltip; repairs with
`#c:ingots/knightmetal` or the vanilla shield repair items except planks; custom model (`KnightmetalShieldModel`,
item rendered by ISTER). `item.twilightforest.knightmetal_shield.shatter` is the "metal shield shatters" sound played
by U24's hydra when it breaks a non-wood shield (`TFSounds.METAL_SHIELD_SHATTERS`).

### 3.7 Lamp of cinders, moonworm queen, ore magnet, peacock fan, transformation powder, ice bomb

- `lamp_of_cinders` (durability 1024, fire resistant, UNCOMMON, not enchantable): use on a block: brown or green
  thorns (`twilightforest:brown_thorns` / `green_thorns`) become `burnt_thorns` (properties kept), placed-block
  trigger, sound `item.twilightforest.lamp.burn` (0.5, 1.5), 10 smoke + flame particles. Hold-use (BOW animation):
  on release after > **12** ticks (and not on the last durability point) burns all thorns within +-4 of eye + look*2,
  sound (1.5, 0.8), 6 mob-spawn particle bursts, ignites every non-player living entity within 4 of that point (box
  around pos-2) for **5 s**. (TF 4.8 does not consume durability on use.)
- `moonworm_queen` (durability 256, no repair, RARE, not enchantable): use on a block places a `twilightforest:moonworm`
  like a block item (1 durability, never breaking: `hurtButDontBreak`); hold-use: release after > 12 ticks (not the
  last point) shoots a `moonworm_shot` (speed 1.5), 2 durability, sound `item.twilightforest.moonworm.squish`.
  Refuses use at 0 durability left. Repair recipe type `twilightforest:moonworm_queen_repair_recipe` (special crafting,
  any grid of >= 2 slots: exactly one **damaged** moonworm queen + one or more `twilightforest:torchberries` and
  nothing else; result = a fresh queen with damage = old damage - 64 per berry; JSON has only `category: misc`).
  Dispenser: shoots a moonworm shot, 2 durability, squish sound.
  - `moonworm_shot` (TFThrowable, gravity 0.03, pickable radius 1.0): on block hit places a moonworm facing the hit
    face where it can survive (not fire/lava), `projectile_land` + squish sound; else rolls
    `twilightforest:blocks/moonworm_failed_to_place` drops (U6's table) and `bug.squish`. On entity hit: a player without a helmet
    gets a moonworm block on the head; others take 1 (1/3) or 0 damage of type `twilightforest:moonworm` + failed
    drops. Always removed with 8 slime particles.
- `ore_magnet` (durability 64, enchantable only with Unbreaking books): release after > **10** ticks: casts a voxel
  line 32 blocks from the eye along the look, then retries with yaw/pitch offsets of 10 degrees in the order
  (+10,0), (+10,+10), (0,+10), (-10,+10), (-10,0), (-10,-10), (0,-10), (+10,-10) until something moved. Along a line:
  the first block in `#twilightforest:ore_magnet_safe_replace_block` is the base; the first ore after it (a block in a
  `c:ores_in_ground/*` tag with a matching `c:ore_bearing_ground/*` tag, not in `#ore_magnet_ignore`; ancient debris
  -> netherrack) without a block entity is pulled: up to **24** connected identical ore blocks are moved by the
  offset base - ore where the destination is safe/replaceable/air; the ore's old position becomes its ground block
  (stone by default). Costs 1 durability per moved block; sound `item.twilightforest.magnet.grab`.
- `peacock_feather_fan` (durability 1024, RARE, use duration 20, BLOCK animation): server: fans a 4x4x4 box centred 3
  blocks along the look: flowers break with 1/3 chance (break event), lit candles and TF lightable blocks
  (normal/dim) are extinguished; pushable entities, items and projectiles get velocity look*2; other players (not
  sneaking) get a `MovePlayerPacket` with look*2, 2 cost and a 40-tick cooldown for the fan; durability cost =
  fanned + 1. In the air (not swimming, not already flagged) the player is flagged `feather_fan_falling` instead of
  the cloud particles; on the ground 30 `cloud` particles along the look are sent to players within 64. Sound
  `item.twilightforest.fan.whoosh` (1 + rand, rand*0.7 + 0.3). Client: while elytra-gliding adds a boost like a
  rocket plus 1.25 up (look*0.1 + (look*2 - motion)*0.5); when flagged it launches the player (x/z * 1.05, y 1.5).
  `feather_fan_falling` makes the next landing ignore fall damage (player tick) and clears on ground / swimming /
  water. Dispenser: pushes entities within 3 of the front block along the facing (1 durability each) if fewer than
  the remaining durability, whoosh sound, 30 cloud particles.
- `transformation_powder`: use on a living, non-owned entity with a `twilightforest:transformation_powder` data-map
  entry converts it to the mapped entity type (`EntityUtil.convertEntity`: mobs use `convertTo` keeping equipment,
  others copy position and NBT; reason CONVERSION; plays `item.twilightforest.transformation_powder.use` = vanilla
  zombie villager cure, subtitle "Mob transforms") and consumes one (not in creative);
  air use only shows 30 `crit` particles in a 2x2x2 box 2 blocks ahead (client). Dispenser: converts living entities in
  the front block.
- `ice_bomb` (stack 16): use plays `item.twilightforest.ice_bomb.fired` (0.5, 0.4 / (rand*0.4 + 0.8)), consumes one,
  throws `thrown_ice` (pitch offset -5, speed 1.25, inaccuracy 1.0). Dispenser shoots it as a projectile.
  - `thrown_ice` (`IceBomb`, gravity 0.025 until it lands): hitting a living entity deals the freeze hit x2; hitting a
    block stops it and applies terrain effects in radius 2 (corners skipped): water -> ice, lava source -> obsidian,
    empty spots where snow survives -> snow layer, `#twilightforest:ice_bomb_replaceables` -> snow or air, snow
    layers +1. It then stays for **100** ticks: terrain effects radius 3 once at tick 99, every 20 ticks it hits living
    entities in box +-3/+-2/+-3 (except the owner): yetis are replaced by two ice blocks, others take the freeze hit
    (x1): `twilightforest:frozen` damage 1.0 (5.0 for `#minecraft:freeze_hurts_extra_types`) x multiplier and frosted
    for 100 x multiplier ticks, amplifier 0 (freeze-immune types skipped). Client: 16 `snow_guardian` particles per
    tick in a 3.5 radius; in flight a trail of 5 (2 for alpha yeti throws) `snow_guardian` particles. Ends with ice
    break particles. Renders as packed ice.

### 3.8 Shield parrying (`EntityEvents.onParryProjectile`)

When a projectile hits a living entity that is blocking with a shield and has been blocking for at most
`shieldParryTicksArrow` ticks (default **40**), and the projectile is a TF projectile (`ITFProjectile`) or
`parryNonTwilightAttacks` (default **false**) is on, the projectile is deflected (AIM_DEFLECT, owned by the blocker)
and the impact cancelled. Disabled when the Shield Parry mod is present.

## 4. Bedrock adaptation notes

- Projectiles through `projectile::registerType(id, Spec{gravity, drag, onHitEntity, onHitBlock, tick})` with TF's
  gravities (chain 0.05, ice bomb 0.025, moonworm shot 0.03, cube 0); the chain and cube are steered every tick
  (`tick`), arrows via `projectile::addSteering` for seeker arrows.
- `thrown_projectile` UUID lives in the item user data (`item::setData`); hold-to-use items (horn, lamp, magnet,
  queen, chain, cube) need Bedrock use duration (`minecraft:use_modifiers`) with `useTick` / `release` from F9.
- Chain rendering and cube model: client mod renderers; vanilla clients see the projectile entity only.
- Ender bow swap and triple-bow i-frame strip: `combat::onDealt`; parrying: `combat::onIncoming` + projectile
  deflection; Bedrock has no parry: implement the deflect by re-shooting the projectile from the blocker.
- Feather fan launch and elytra boost are client movement on Java: the server applies `movement::applyMotion` and
  sends the knockback packet to vanilla clients (Bedrock motion packets); fall-damage immunity through
  `combat::onIncoming` for fall damage while flagged.
- Data maps (`crumble_horn`, `transformation_powder`) load from the imported JSON via `datamaps::get`.
- Infinite glass sword: creative catalog entry with the user-data flag (F9 catalog).

## 5. Acceptance checklist (selftests `weapons.*`, dx 800-899)

- Each item's durability, rarity, stack size, enchantability and repair rules.
- Chain: 10 damage, shield disable, 16-block leash, 100-tick stuck return, durability costs; cube: annihilates
  resistance < 8 blocks and inclusions, returns on obstacles.
- Horn: crumble map results and chances (seeded), stat, durability.
- Bows: triple bow 3 projectiles (2 intangible), seeker steering, ice arrow frosted, ender swap (not bosses).
- Ice bomb terrain conversion and 100-tick zone damage; yeti -> ice.
- Lamp burns thorns in +-4 and ignites mobs 5 s after > 12 ticks; moonworm queen place / shoot / repair recipe.
- Ore magnet vein pull (<= 24) with the 9-direction retry order; fan pushes and fall immunity; transformation powder
  conversions; parry window 40 ticks.

## Appendix A: Owned ids (generated inventory)

Every id this unit owns in `docs/spec/owners.tsv`, with the TF 4.8 data the W1 worker must reproduce. Registration statements are TF's own Java (`init/TF*.java`), condensed (namespaces such as `MapColor.` dropped); loot, recipe, tag, advancement and data rows are summarised from TF's generated JSON (`src/generated/resources`). Loot notation: `self` = drops the block itself when it survives the explosion; `alternatives(a → b)` = first matching child; `[if ...]` = conditions; `{...}` = functions. In summaries `mc:` is `minecraft:` and ids without a namespace are `twilightforest:`.

| Kind | Count |
|---|---|
| `advancement` | 2 |
| `attachment` | 1 |
| `config_key` | 2 |
| `config_section` | 1 |
| `damage_type` | 1 |
| `data_component` | 2 |
| `data_map` | 2 |
| `e2e` | 1 |
| `entity` | 6 |
| `item` | 17 |
| `lang` | 44 |
| `net` | 1 |
| `particle` | 1 |
| `path` | 20 |
| `recipe` | 3 |
| `recipe_serializer` | 1 |
| `selftest` | 1 |
| `selftest_dx` | 1 |
| `sound_event` | 12 |
| `stat` | 1 |
| `tag` | 1 |
| `trigger` | 1 |
| `worlddb` | 1 |

### Unit facts

| Kind | Ids |
|---|---|
| Selftest glob (`selftest`) | `weapons.*` |
| Selftest dx range (`selftest_dx`) | `800-899` |
| World DB keys (`worlddb`) | `gf:weapons:*` |
| E2E scenario (`e2e`) | `weapons` |
| Net channels (`net`) | `weapons.*` |
| Config section (`config_section`) | `weapons` |
| Owned paths (`path`) | `**/*.d/weapons.json`, `assets/data/*/weapons.json`, `assets/gen/audio/weapons/**`, `assets/gen/geo/weapons/**`, `assets/gen/import/weapons/**`, `assets/lang/*/weapons.json`, `assets/pack/{bp,rp}/**/weapons/**`, `assets/pack/rp/texts.d/*/weapons.lang`, `assets/structures/weapons/**`, `assets/tags/**/*.d/weapons.json`, `docs/units/weapons.md`, `src/client/weapons/**`, `src/core/weapons/**`, `src/feature/ext/weapons/**`, `src/feature/weapons/**`, `tests/weapons/**`, `tools/e2e/*/scenarios/weapons{,.*}.ps1`, `tools/import/overrides/weapons.json`, `tools/sounds/recipes/weapons.json`, `tools/structures/weapons/**` |

### Items (17)

| Item | English name | TF registration (`TFItems`) | Notes |
|---|---|---|---|
| `twilightforest:block_and_chain` | Block and Chain | `ITEMS.register("block_and_chain", new ChainBlockItem(new Item.Properties().durability(99)));` | item tags: #alexscaves:ferromagnetic_items, #mc:breaks_decorated_pots, #mc:enchantable/durability, #mc:enchantable/mining, #mc:enchantable/mining_loot, #enchantable/block_and_chain |
| `twilightforest:crumble_horn` | Crumble Horn | `ITEMS.register("crumble_horn", new CrumbleHornItem(new Item.Properties().durability(1024).rarity(RARE)));` | dispenser: CrumbleDispenseBehavior (TFDispenserBehaviors); item tags: #mc:enchantable/durability |
| `twilightforest:cube_of_annihilation` | Cube of Annihilation | `ITEMS.register("cube_of_annihilation", new CubeOfAnnihilationItem(new Item.Properties().stacksTo(1).fireResistant().rarity(UNCOMMON)));` | item tags: #randomium:blacklist, #wip |
| `twilightforest:cube_talisman` | Talisman of the Cube | `ITEMS.register("cube_talisman", new Item(new Item.Properties().fireResistant().rarity(UNCOMMON)));` | item tags: #randomium:blacklist, #wip |
| `twilightforest:ender_bow` | Ender Bow | `ITEMS.register("ender_bow", new EnderBowItem(new Item.Properties().rarity(UNCOMMON).durability(384)));` | item tags: #c:tools/bow, #mc:enchantable/bow, #mc:enchantable/durability |
| `twilightforest:glass_sword` | Glass Sword | `ITEMS.register("glass_sword", new GlassSwordItem(TFToolMaterials.GLASS, new Item.Properties().setNoRepair().rarity(UNCOMMON).attributes(SwordItem.createAttributes(TFToolMaterials.GLASS, 3, -2.4F))));` | item tags: #forbidden_arcanus:modifier/eternal_incompatible, #mc:swords, #randomium:blacklist |
| `twilightforest:ice_bomb` | Ice Bomb | `ITEMS.register("ice_bomb", new IceBombItem(new Item.Properties().stacksTo(16)));` | dispenser shoots it as a projectile (TFDispenserBehaviors) |
| `twilightforest:ice_bow` | Ice Bow | `ITEMS.register("ice_bow", new IceBowItem(new Item.Properties().rarity(UNCOMMON).durability(384)));` | item tags: #c:tools/bow, #mc:enchantable/bow, #mc:enchantable/durability |
| `twilightforest:ice_sword` | Ice Sword | `ITEMS.register("ice_sword", new IceSwordItem(TFToolMaterials.ICE, new Item.Properties().attributes(SwordItem.createAttributes(TFToolMaterials.ICE, 3, -2.4F))));` | item tags: #mc:swords |
| `twilightforest:knightmetal_shield` | Knightmetal Shield | `ITEMS.register("knightmetal_shield", new KnightmetalShieldItem(new Item.Properties().durability(1024)));` | item tags: #alexscaves:ferromagnetic_items, #c:tools/shield, #mc:enchantable/durability |
| `twilightforest:lamp_of_cinders` | Lamp of Cinders | `ITEMS.register("lamp_of_cinders", new LampOfCindersItem(new Item.Properties().fireResistant().durability(1024).rarity(UNCOMMON)));` | item tags: #mc:piglin_loved |
| `twilightforest:moonworm_queen` | Moonworm Queen | `ITEMS.register("moonworm_queen", new MoonwormQueenItem(new Item.Properties().setNoRepair().durability(256).rarity(RARE)));` | dispenser: fires its projectile and damages the stack (TFDispenserBehaviors); item tags: #travellers_agile_ranger_whitelisted |
| `twilightforest:ore_magnet` | Ore Magnet | `ITEMS.register("ore_magnet", new OreMagnetItem(new Item.Properties().durability(64)));` | item tags: #alexscaves:ferromagnetic_items, #mc:enchantable/durability |
| `twilightforest:peacock_feather_fan` | Peacock Feather Fan | `ITEMS.register("peacock_feather_fan", new PeacockFanItem(new Item.Properties().durability(1024).rarity(RARE)));` | dispenser: FeatherFanDispenseBehavior (TFDispenserBehaviors); item tags: #mc:enchantable/durability |
| `twilightforest:seeker_bow` | Seeker Bow | `ITEMS.register("seeker_bow", new SeekerBowItem(new Item.Properties().rarity(UNCOMMON).durability(384)));` | item tags: #c:tools/bow, #mc:enchantable/bow, #mc:enchantable/durability |
| `twilightforest:transformation_powder` | Transformation Powder | `ITEMS.register("transformation_powder", new TransformPowderItem(new Item.Properties()));` | dispenser: TransformationDispenseBehavior (TFDispenserBehaviors); item tags: #banned_uncrafting_ingredients |
| `twilightforest:triple_bow` | Tri-Bow | `ITEMS.register("triple_bow", new TripleBowItem(new Item.Properties().rarity(UNCOMMON).durability(384)));` | item tags: #c:tools/bow, #mc:enchantable/bow, #mc:enchantable/durability |

### Entity types (6)

| Id | TF registration (`TFEntities.java`) |
|---|---|
| `twilightforest:chain_block` (Block and Chain) | `buildNoEgg(TFEntityNames.CHAIN_BLOCK, makeCastedBuilder(ChainBlock.class, ChainBlock::new, 0.6F, 0.6F, 80, 1), true);` |
| `twilightforest:cube_of_annihilation` (Cube of Annihilation) | `buildNoEgg(TFEntityNames.CUBE_OF_ANNIHILATION, makeCastedBuilder(CubeOfAnnihilation.class, CubeOfAnnihilation::new, 1.0F, 1.0F, 80, 1), true);` |
| `twilightforest:ice_arrow` (Ice Arrow) | `buildNoEgg(TFEntityNames.ICE_ARROW, makeCastedBuilder(IceArrow.class, IceArrow::new, 0.5F, 0.5F, 150, 1), false);` |
| `twilightforest:moonworm_shot` (Moonworm) | `buildNoEgg(TFEntityNames.MOONWORM_SHOT, makeCastedBuilder(MoonwormShot.class, MoonwormShot::new, 0.25F, 0.25F, 150, 3), false);` |
| `twilightforest:seeker_arrow` (Seeker Arrow) | `buildNoEgg(TFEntityNames.SEEKER_ARROW, makeCastedBuilder(SeekerArrow.class, SeekerArrow::new, 0.5F, 0.5F, 150, 1), false);` |
| `twilightforest:thrown_ice` (Ice Bomb) | `buildNoEgg(TFEntityNames.THROWN_ICE, makeCastedBuilder(IceBomb.class, IceBomb::new, 1.0F, 1.0F, 80, 2), true);` |

### Particle types (1)

| Id | TF registration (`TFParticleType.java`) |
|---|---|
| `twilightforest:annihilate` | `PARTICLE_TYPES.register("annihilate", new SimpleParticleType(false));` |

### Data components (2)

| Id | TF registration (`TFDataComponents.java`) |
|---|---|
| `twilightforest:infinite_glass_sword` | `COMPONENTS.register("infinite_glass_sword", () -> DataComponentType.<Unit>builder().persistent(Codec.unit(Unit.INSTANCE)).networkSynchronized(StreamCodec.unit(Unit.INSTANCE)).build());` |
| `twilightforest:thrown_projectile` | `COMPONENTS.register("thrown_projectile", () -> DataComponentType.<UUID>builder().persistent(UUIDUtil.CODEC).networkSynchronized(UUIDUtil.STREAM_CODEC).build());` |

### Data attachments (1)

| Id | TF registration (`TFDataAttachments.java`) |
|---|---|
| `twilightforest:feather_fan_falling` | `ATTACHMENT_TYPES.register("feather_fan_falling", () -> AttachmentType.builder(() -> false).serialize(Codec.BOOL).sync(ByteBufCodecs.BOOL).build());` |

### Recipe serializers / types (1)

| Id | TF registration (`TFRecipes.java`) |
|---|---|
| `twilightforest:moonworm_queen_repair_recipe` | `RECIPE_SERIALIZERS.register("moonworm_queen_repair_recipe", new SimpleCraftingRecipeSerializer<>(MoonwormQueenRepairRecipe::new));` |

### Stats (1)

| Id | TF registration (`TFStats.java`) |
|---|---|
| `twilightforest:blocks_crumbled` (Blocks Crumbled) | `makeTFStat("blocks_crumbled");` |

### Advancement triggers (1)

| Id | TF registration (`TFAdvancements.java`) |
|---|---|
| `twilightforest:broke_glass_sword` | `TRIGGERS.register("broke_glass_sword", SimpleAdvancementTrigger::new);` |

### Damage types (1)

- `twilightforest:moonworm`: `{"exhaustion":0.0,"message_id":"twilightforest.moonworm","scaling":"when_caused_by_living_non_player"}` death.attack.twilightforest.moonworm = "%1$s was shot by Moonworm";

### Data maps (2)

- `twilightforest:crumble_horn` (registry `block`): `{"values":{"minecraft:andesite":{"crumble_chance":0.05,"crumble_to":"minecraft:air"},"minecraft:clay":{"crumble_chance":0.05,"crumble_to":"minecraft:air"},"minecraft:coarse_dirt":{"crumble_chance":0.2,"crumble_to":"minecraft:dirt"},"minecraft:cobblestone":{"crumble_chance":0.2,"crumble_to":"minecraft:gravel"},"minecraft:cracked_polished_blackstone_bricks":{"crumble_chance":0.2,"crumble_to":"minecraft:blackstone"},"minecraft:deepslate_bricks":{"crumble_chance":0.2,"crumble_to":"minecraft:cracked_deepslate_bricks"},"minecraft:deepslate_tiles":{"crumble_chance":0.2,"crumble_to":"minecraft:cracked_deepslate_tiles"},"minecraft:diorite":{"crumble_chance":0.05,"crumble_to":"minecraft:air"},"minecraft:dirt":{"crumble_chance":0.05,"crumble_to":"minecraft:air"},"minecraft:exposed_chiseled_copper":{"crumble_chance":0.2,"crumble_to":"minecraft:chiseled_copper"},"minecraft:exposed_copper":{"crumble_chance":0.2,"crumble_to":"minecraft:copper_block"},"minecraft:exposed_copper_bulb":{"crumble_chance":0.2,"crumble_to":"minecraft:copper_bulb"},"minecraft:exposed_copper_door":{"crumble_chance":0.2,"crumble_to":"minecraft:copper_door"},"minecraft:exposed_copper_grate":{"crumble_chance":0.2,"crumble_to":"minecraft:copper_grate"},"minecraft:exposed_copper_trapdoor":{"crumble_chance":0.2,"crumble_to":"minecraft:copper_trapdoor"},"minecraft:exposed_cut_copper":{"crumble_chance":0.2,"crumble_to":"minecraft:cut_copper"},"minecraft:exposed_cut_copper_slab":{"crumble_chance":0.2,"crumble_to":"minecraft:cut_copper_slab"},"minecraft:exposed_cut_copper_stairs":{"crumble_chance":0.2,"crumble_to":"minecraft:cut_copper_stairs"},"minecraft:granite":{"crumble_chance":0.05,"crumble_to":"minecraft:air"},"minecraft:grass_block":{"crumble_chance":0.2,"crumble_to":"minecraft:dirt"},"minecraft:gravel":{"crumble_chance":0.05,"crumble_to":"minecraft:air"},"minecraft:infested_stone_bricks":{"crumble_chance":0.2,"crumble_to":"minecraft:infested_cracked_stone_bricks"},"minecraft:mycelium":{"crumble_chance":0.2,"crumble_to":"minecraft:dirt"},"minecraft:nether_bricks":{"crumble_chance":0.2,"crumble_to":"minecraft:cracked_nether_bricks"},"minecraft:oxidized_chiseled_copper":{"crumble_chance":0.2,"crumble_to":"minecraft:weathered_chiseled_copper"},"minecraft:oxidized_copper":{"crumble_chance":0.2,"crumble_to":"minecraft:weathered_copper"},"minecraft:oxidized_copper_bulb":{"crumble_chance":0.2,"crumble_to":"minecraft:weathered_copper_bulb"},"minecraft:oxidized_copper_door":{"crumble_chance":0.2,"crumble_to":"minecraft:weathered_copper_door"},"minecraft:oxidized_copper_grate":{"crumble_chance":0.2,"crumble_to":"minecraft:weathered_copper_grate"},"minecraft:oxidized_copper_trapdoor":{"crumble_chance":0.2,"crumble_to":"minecraft:weathered_copper_trapdoor"},"minecraft:oxidized_cut_copper":{"crumble_chance":0.2,"crumble_to":"minecraft:weathered_cut_copper"},"minecraft:oxidized_cut_copper_slab":{"crumble_chance":0.2,"crumble_to":"minecraft:weathered_cut_copper_slab"},"minecraft:oxidized_cut_copper_stairs":{"crumble_chance":0.2,"crumble_to":"minecraft:weathered_cut_copper_stairs"},"minecraft:podzol":{"crumble_chance":0.2,"crumble_to":"minecraft:dirt"},"minecraft:polished_blackstone_bricks":{"crumble_chance":0.2,"crumble_to":"minecraft:cracked_polished_blackstone_bricks"},"minecraft:red_sand":{"crumble_chance":0.05,"crumble_to":"minecraft:air"},"minecraft:red_sandstone":{"crumble_chance":0.2,"crumble_to":"minecraft:red_sand"},"minecraft:rooted_dirt":{"crumble_chance":0.2,"crumble_to":"minecraft:dirt"},"minecraft:sand":{"crumble_chance":0.05,"crumble_to":"minecraft:air"},"minecraft:sandstone":{"crumble_chance":0.2,"crumble_to":"minecraft:sand"},"minecraft:stone":{"crumble_chance":0.2,"crumble_to":"minecraft:cobblestone"},"minecraft:stone_bricks":{"crumble_chance":0.2,"crumble_to":"minecraft:cracked_stone_bricks"},"minecraft:weathered_chiseled_copper":{"crumble_chance":0.2,"crumble_to":"minecraft:exposed_chiseled_copper"},"minecraft:weathered_copper":{"crumble_chance":0.2,"crumble_to":"minecraft:exposed_copper"},"minecraft:weathered_copper_bulb":{"crumble_chance":0.2,"crumble_to":"minecraft:exposed_copper_bulb"},"minecraft:weathered_copper_door":{"crumble_chance":0.2,"crumble_to":"minecraft:exposed_copper_door"},"minecraft:weathered_copper_grate":{"crumble_chance":0.2,"crumble_to":"minecraft:exposed_copper_grate"},"minecraft:weathered_copper_trapdoor":{"crumble_chance":0.2,"crumble_to":"minecraft:exposed_copper_trapdoor"},"minecraft:weathered_cut_copper":{"crumble_chance":0.2,"crumble_to":"minecraft:exposed_cut_copper"},"minecraft:weathered_cut_copper_slab":{"crumble_chance":0.2,"crumble_to":"minecraft:exposed_cut_copper_slab"},"minecraft:weathered_cut_copper_stairs":{"crumble_chance":0.2,"crumble_to":"minecraft:exposed_cut_copper_stairs"},"twilightforest:castle_brick":{"crumble_chance":0.2,"crumble_to":"twilightforest:cracked_castle_brick"},"twilightforest:castle_brick_stairs":{"crumble_chance":0.2,"crumble_to":"twilightforest:cracked_castle_brick_stairs"},"twilightforest:cracked_castle_brick":{"crumble_chance":0.2,"crumble_to":"twilightforest:worn_castle_brick"},"twilightforest:cracked_deadrock":{"crumble_chance":0.2,"crumble_to":"twilightforest:weathered_deadrock"},"twilightforest:deadrock":{"crumble_chance":0.2,"crumble_to":"twilightforest:cracked_deadrock"},"twilightforest:etched_nagastone":{"crumble_chance":0.2,"crumble_to":"twilightforest:cracked_etched_nagastone"},"twilightforest:mazestone_brick":{"crumble_chance":0.2,"crumble_to":"twilightforest:cracked_mazestone"},"twilightforest:nagastone_pillar":{"crumble_chance":0.2,"crumble_to":"twilightforest:cracked_nagastone_pillar"},"twilightforest:nagastone_stairs_left":{"crumble_chance":0.2,"crumble_to":"twilightforest:cracked_nagastone_stairs_left"},"twilightforest:nagastone_stairs_right":{"crumble_chance":0.2,"crumble_to":"twilightforest:cracked_nagastone_stairs_right"},"twilightforest:towerwood":{"crumble_chance":0.2,"crumble_to":"twilightforest:cracked_towerwood"},"twilightforest:underbrick":{"crumble_chance":0.2,"crumble_to":"twilightforest:cracked_underbrick"}}}`
  TF registration: `TwilightForestMod.prefix("crumble_horn"), Registries.BLOCK, CrumbledBlock.CODEC).synced(CrumbledBlock.CODEC, false).build();`
- `twilightforest:transformation_powder` (registry `entity_type`): `{"values":{"minecraft:bat":{"transform_to":"twilightforest:raven"},"minecraft:cave_spider":{"transform_to":"twilightforest:swarm_spider"},"minecraft:chicken":{"transform_to":"twilightforest:penguin"},"minecraft:cow":{"transform_to":"twilightforest:deer"},"minecraft:ghast":{"transform_to":"twilightforest:carminite_ghastguard"},"minecraft:parrot":{"transform_to":"twilightforest:tiny_bird"},"minecraft:pig":{"transform_to":"twilightforest:boar"},"minecraft:rabbit":{"transform_to":"twilightforest:dwarf_rabbit"},"minecraft:sheep":{"transform_to":"twilightforest:bighorn_sheep"},"minecraft:silverfish":{"transform_to":"twilightforest:towerwood_borer"},"minecraft:slime":{"transform_to":"twilightforest:maze_slime"},"minecraft:spider":{"transform_to":"twilightforest:hedge_spider"},"minecraft:vex":{"transform_to":"twilightforest:wraith"},"minecraft:witch":{"transform_to":"twilightforest:skeleton_druid"},"minecraft:wolf":{"transform_to":"twilightforest:hostile_wolf"},"minecraft:zombified_piglin":{"transform_to":"twilightforest:minotaur"},"twilightforest:bighorn_sheep":{"transform_to":"minecraft:sheep"},"twilightforest:boar":{"transform_to":"minecraft:pig"},"twilightforest:carminite_ghastguard":{"transform_to":"minecraft:ghast"},"twilightforest:deer":{"transform_to":"minecraft:cow"},"twilightforest:dwarf_rabbit":{"transform_to":"minecraft:rabbit"},"twilightforest:hedge_spider":{"transform_to":"minecraft:spider"},"twilightforest:hostile_wolf":{"transform_to":"minecraft:wolf"},"twilightforest:maze_slime":{"transform_to":"minecraft:slime"},"twilightforest:minotaur":{"transform_to":"minecraft:zombified_piglin"},"twilightforest:penguin":{"transform_to":"minecraft:chicken"},"twilightforest:raven":{"transform_to":"minecraft:bat"},"twilightforest:skeleton_druid":{"transform_to":"minecraft:witch"},"twilightforest:swarm_spider":{"transform_to":"minecraft:cave_spider"},"twilightforest:tiny_bird":{"transform_to":"minecraft:parrot"},"twilightforest:towerwood_borer":{"transform_to":"minecraft:silverfish"},"twilightforest:wraith":{"transform_to":"minecraft:vex"}}}`
  TF registration: `TwilightForestMod.prefix("transformation_powder"), Registries.ENTITY_TYPE, EntityTransformation.CODEC).synced(EntityTransformation.CODEC, false).build();`

### Sound events (12)

`sounds.json` content (vanilla `event` references can be played with the matching Bedrock vanilla sound; TF file names are listed only as names: the audio itself is All Rights Reserved and is re-synthesised by F3/the owner).

| Event | Subtitle | sounds.json entries |
|---|---|---|
| `block.twilightforest.generic.annihilation` | subtitles.twilightforest.block.generic.annihilation = Block dissolves | event block.fire.extinguish |
| `item.twilightforest.block_and_chain.collide` | subtitles.twilightforest.item.block_and_chain.collide = Block and Chain hits block | event block.anvil.land |
| `item.twilightforest.block_and_chain.fire` | subtitles.twilightforest.item.block_and_chain.fire = Block and Chain thrown | event entity.arrow.shoot |
| `item.twilightforest.block_and_chain.hit` | subtitles.twilightforest.item.block_and_chain.hit = Block and Chain hits entity | event entity.iron_golem.attack |
| `item.twilightforest.fan.whoosh` | subtitles.twilightforest.item.fan.whoosh = Peacock Feather Fan blows | random/breath |
| `item.twilightforest.glass_sword.break` | subtitles.twilightforest.item.glass_sword.break = Glass Sword shatters | event block.glass.break |
| `item.twilightforest.ice_bomb.fired` | subtitles.twilightforest.item.ice_bomb.fired = Ice Bomb thrown | event entity.arrow.shoot |
| `item.twilightforest.knightmetal_shield.shatter` | subtitles.twilightforest.item.knightmetal_shield.shatter = Metal Shield shatters | event entity.zombie.attack_iron_door |
| `item.twilightforest.lamp.burn` | subtitles.twilightforest.item.lamp.burn = Lamp of Cinders ignites area | event entity.ghast.shoot |
| `item.twilightforest.magnet.grab` | subtitles.twilightforest.item.magnet.grab = Ore Magnet pulls up ore | event item.chorus_fruit.teleport |
| `item.twilightforest.moonworm.squish` | subtitles.twilightforest.item.moonworm.squish = Moonworm fires | event entity.slime.squish_small |
| `item.twilightforest.transformation_powder.use` | subtitles.twilightforest.item.transformation_powder.use = Mob transforms | event entity.zombie_villager.cure |

### Tags (1)

| Tag | Members (TF generated JSON) |
|---|---|
| `block/#twilightforest:annihilation_inclusions` | mc:nether_portal, #deadrock, castle_brick, thick_castle_brick, mossy_castle_brick, castle_roof_tile, worn_castle_brick, blue_castle_rune_brick, violet_castle_rune_brick, yellow_castle_rune_brick, pink_castle_rune_brick, pink_force_field, orange_force_field, green_force_field, blue_force_field, violet_force_field, brown_thorns, green_thorns |

### Recipes (3)

Recipe-unlock advancements (generated by F1 into the recipe JSON, R21) are listed with their recipe; criteria are the `has_*` triggers besides `has_the_recipe`.

| Recipe | Type | Ingredients | Result | Unlock advancement (criteria) |
|---|---|---|---|---|
| `twilightforest:equipment/block_and_chain` | mc:crafting_shapeless | #c:storage_blocks/knightmetal + #c:ingots/knightmetal + #c:ingots/knightmetal + #c:ingots/knightmetal + knightmetal_ring | block_and_chain | `twilightforest:recipes/combat/equipment/block_and_chain` (has_block: has #c:storage_blocks/knightmetal; has_ingot: has #c:ingots/knightmetal; has_ring: has knightmetal_ring) |
| `twilightforest:equipment/knightmetal_shield` | mc:crafting_shaped | '-#' / '-o' / '-#' ; #=#towerwood, -=#c:ingots/knightmetal, o=knightmetal_ring | knightmetal_shield | `twilightforest:recipes/combat/equipment/knightmetal_shield` (has_ingot: has #c:ingots/knightmetal; has_ring: has knightmetal_ring) |
| `twilightforest:moonworm_queen_repair_recipe` | moonworm_queen_repair_recipe | {} | - | - |

### Config keys (2)

See [config.md](config.md) for the section mapping; defaults and translation ids below are TF's.

| TF key | Default | Translation id | English (config screen) |
|---|---|---|---|
| `common.Shield Parrying.parryNonTwilightAttacks` | `false` | `parry_non_twilight` | Parry Non-TF Projectiles — Set to true to parry non-Twilight projectiles. |
| `common.Shield Parrying.shieldParryTicksArrow` | `40` | `parry_window` | Parry Window — The amount of ticks after raising a shield that makes it OK to parry a projectile. (1 tick = 1/20 second) |

## Appendix B: Strings (44 en_us keys owned)

Keys already shown above (block/item/entity names, subtitles) are not repeated. The 29 Bedrock locales are generated from TF's lang files (R9); owners use these keys with the English text as fallback.

| Key | English |
|---|---|
| `config.twilightforest.parry_non_twilight` | Parry Non-TF Projectiles |
| `config.twilightforest.parry_non_twilight.tooltip` | Set to true to parry non-Twilight projectiles. |
| `config.twilightforest.parry_window` | Parry Window |
| `config.twilightforest.parry_window.tooltip` | The amount of ticks after raising a shield that makes it OK to parry a projectile. (1 tick = 1/20 second) |
| `config.twilightforest.shield` | Shield Interactions |
| `config.twilightforest.shield.tooltip` | We recommend downloading the Shield Parry mod for parrying, but these controls remain for without. |
| `item.twilightforest.glass_sword.desc` | Creative Mode-only |
| `item.twilightforest.moonworm_queen.jei_info_message` | Torchberries restore 64 durability each |

