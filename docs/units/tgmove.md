# U12 `tgmove`: Travellers' Gear movement

The twelve movement modifiers of upstream 4.8's Travellers' Gear (behaviour: [docs/spec/tgmove.md](../spec/tgmove.md)).

| Part | Where |
|---|---|
| Rules, numbers, state machines (pure, gtested) | `src/core/tgmove/TgMove.{h,cpp}`, `tests/tgmove/TgMoveTest.cpp` |
| Server: per-tick logic of players and mobs, companion packets, vanilla fallbacks, fall damage | `src/feature/tgmove/TgMove.{h,cpp}` |
| Selftests `tgmove.*` (dx 1200..1299) | `src/feature/tgmove/TgMoveTests.cpp` |
| Client mod: the local player's movement, the wings' animation | `src/client/tgmove/TgMoveClient.cpp` |
| Wings animation (TravellersWingsModel) | `assets/pack/rp/animations/tgmove/travellers_wings.animation.json` (played by the wings' attachable, see below) |
| Particle `gloamingforest:double_jump` (upstream's DoubleJumpParticle = ExplodeParticle, half size) | `assets/pack/rp/particles/tgmove/double_jump.json` (the generated skeleton is dropped by `tools/import/overrides/tgmove.json`) |
| Mining speed extension (submerged mining speed) | `src/feature/ext/tgmove/Mining.{h,cpp}` (contract extension, R18) |
| Pack patch extension (the wings' attachable plays the animation with the client mod only) | `src/feature/ext/tgmove/PackPatch.{h,cpp}` (contract extension, R18) |
| e2e | `tools/e2e/server/scenarios/tgmove.ps1`, `tools/e2e/client/scenarios/tgmove.ps1` |

## Who does what

Java's movement is client authoritative; Bedrock's is server authoritative: the server simulates every player from its
input and corrects a client that ends up elsewhere (measured: a double jump the client made on its own was put back 2
ticks later). So each modifier runs in both simulations of a player. With the client mod, the local player applies
the double jump, sidestep, glide, step up, water walk, unrestrained, swift swim, agile ranger and straight ahead's
strafe before its tick (`gf::client::movement::onLocalTick`) and sends upstream's packets on `tgmove.double_jump`,
`tgmove.sidestep` and `tgmove.gradual_glide`. The server pushes the double jump and the sidestep itself
(SetActorMotionPacket; sound, particles, safe fall, wing state, cooldown sound), applies the glide, the water walk, the
step height, the slowdown immunity, Depth Strider, agile ranger's item use slowdown and straight ahead's x1.4 speed
(movement speed attribute, forward input only) to its copy, runs upstream's validator on refused packets (dedicated servers
only, as upstream: 2 free, then a snap back through F12's movement validator `tgmove.travellers_movement`, a disconnect at 5)
and owns: Jump Boost II (high jump), the fall distance of a glide (v^2 / 2g), the safe fall +2 after a double jump,
slimy soles' cancelled landing and bounce (`tgmove.slimy` gives the companion the next double jump's boost), aquatic
agility's oxygen bonus and mining speed, efficient eater's movement and jump exhaustion and the wing state
(`tgmove.wings_state` to companions within 64 blocks). Every value the unit changes on the engine (step height,
immunity entries, Depth Strider, item use slowdown, attribute modifier, effects) is put back when the modifier ends,
when the state is dropped (disconnect, respawn, tests) and at uninstall; a weaker effect of the player's own that Jump
Boost II replaced comes back with the time it had left (Java's hidden effect).

Mobs wearing the gear (upstream's `updateOtherModifiers` runs for every LivingEntity) glide whenever they fall, bounce on
slimy soles, get Jump Boost II, run x1.4 with straight ahead, ignore block slowdowns with unrestrained and have a
synced wing state.

`last_damage_armor_time` is kept in F8's entity data under `gloamingforest:last_damage_armor_time` (game time; damage
types in `#minecraft:bypasses_armor` do not count), which U11's auto repair reads by that string.

`leashed_pathfinder_override` is the entity tag `gloamingforest:leashed_pathfinder_override` (U21 adds it by string to
the lich tower's fenced mobs); it is dropped from a mob that cannot be leashed or is not leashed when it joins the
world, as upstream's `handleLeashPathingOverrides`. Its effect, no stay-close pathing within 6 blocks of the holder: see
"Bedrock adaptations" (the leash pulls a marked mob only beyond 6 blocks, and its paths to the holder are dropped).

## Wings animation

`TravellersWingsModel#setupModelAnimations` (state, phase, interpolation to the target with tau 4, crouch drop) is
`core::tgmove::animateWings`. The client mod runs it every frame (`client::render::onWorld`) for every player (and mob)
wearing the wings, from the synced `tgmove.wings_state`, and hands the wing bases' pose to the actor's Molang variables
`variable.tgmove_wing_{on,rx,ry,rz,ly,drop}`. `animation.gloamingforest.travellers_wings` (U12's file) turns those into
deltas from the geometry's rest pose (with `v.tgmove_wing_on` 0 it falls back to upstream's IDLE flap: phase / 20, sin / 15
around 30°, -0.6, -0.3 rad, the left wing mirrored, 2 px lower while sneaking).

An attachable's `v.` variables are its own, not the wearer's (measured: `unknown variable 'variable.tgmove_wing_on'`),
and Molang's `??` does not take an `->` expression. So the client sets the pose as *public* variables of the wearer
(`MolangVariableSetting_AccessSpecifier::Public` plus the public value) and the animation reads them as
`c.owning_entity->v.tgmove_wing_*`. The client mod sets them every frame on every entity in its level that wears the
wings (`Level::getRuntimeActorList`, scanned each client tick; a new wearer gets its variables in that tick, before
a frame draws it), whether or not a wing state came for it yet (IDLE until one does), so the animation never reads a
variable that is not there.

The wings' attachable (`assets/gen/geo/tgear/rp/attachables/travellers_wings.json`, item `gloamingforest:travellers_wings`
of U11, generated by F2) is not U12's file, and a vanilla client must never reference the animation (the arrow cannot
be guarded: it would log a Molang error per frame for every wearer in view). So the client mod adds the animation to
the attachable as the game reads it: the contract extension `gf::ext::packpatch` (`src/feature/ext/tgmove/PackPatch`,
hooks on `ResourcePack::getResource` and `ResourcePackManager::load`, client build only, installed at mod load before
the packs load) patches the JSON document of `.../travellers_wings.json` whose identifier is
`gloamingforest:travellers_wings`:

```json
"animations": {"tgmove_wings": "animation.gloamingforest.travellers_wings"},
"scripts": {"parent_setup": "variable.leg_layer_visible = 0.0;", "animate": ["tgmove_wings"]},
"render_controllers": [{"controller.render.armor": "!c.is_first_person"}]
```

(the last line: no wings in first person, as upstream draws no armour there). The log says `pack patch: patched <path>`
once per file; the client e2e waits for it, and checks that a vanilla client (remote-vanilla) has no such line and a
clean content log with a wearer in view. Vanilla clients keep the attachable as shipped: the wings in the model's rest
pose (spec section 4: the wings' poses need the client mod). The packs on disk and the server's packs are unchanged,
so F2's `models.attachables` selftest still sees the generated file.
## Bedrock adaptations

- Safe fall distance and fall damage: Bedrock has no safe fall attribute; the double jump's +2 is 2 less fall damage
  (F10's combat hook), which is what Java's attribute does; slimy soles decides on that reduced damage.
- Unrestrained, stuck speed (ResetStuckUnrestrained): Bedrock's `BlockMovementSlowdownImmunityComponent` for cobweb,
  sweet berry bush and powder snow (client and server copy), the blocks whose slowdown that component covers
  (`EntityInsideSystemImpl::tickWebMovementSlowdown` / `tickPowderSnowMovementSlowdown`).
- Unrestrained on the ground (BlockHooks#resetBlockFrictionWithUnrestrained, #resetSlimeMomentumWithUnrestrained,
  #stopBouncing; EntityHooks#resetFactorWithUnrestrained): Bedrock has no per-entity block friction, so
  `applyUnrestrained` corrects the velocity before each engine step, in the client mod and in the server copy alike:
  the drag of the last step (friction x 0.91 and the engine's own slowdown) is turned into upstream's 0.6 x 0.91, and the
  input acceleration of the coming step gets the difference between 0.6's factor (0.21600002 / 0.6^3) and the
  block's (`core::tgmove::unrestrainedStep`, gtested against a simulated engine: same positions and velocities as
  Java's 0.6 step). What Bedrock does on each block was measured (`tgmove.unrestrained_blocks`, a 0.4 push and a 20
  tick walk of a simulated player, without and with the boots):

  | Block | Bedrock | Unrestrained here (upstream) |
  |---|---|---|
  | ice / packed ice / blue ice | friction 0.98 / 0.98 / 0.989: a 0.4 push slides 3.66 / 3.66 / 3.94 blocks | 0.881, as on grass (0.881) |
  | slime | friction 0.8 and Java's step-on factor 0.4 + 0.2 x \|vy\| on the velocity: 0.57; walk 0.058 | 0.881; walk 0.19 (grass 0.20); no walking bounce |
  | honey block | the same as slime (friction 0.8, factor 0.4 + 0.2 x \|vy\|): 0.57; walk 0.058; jump 0.514 high (power 0.42 x 0.6) | 0.881; walk 0.19; jump 1.252, as on grass |
  | soul sand | a slide is not slowed; the walk's acceleration x 0.544 (walk 0.110) | walk 0.193 |

  A slime landing slower than 0.08 (a walk) does not bounce (`stopBouncing`: the engine's bounce of it is taken back);
  a fall still bounces. The honey jump: the engine's weaker jump is made up in the next step (the height it lost is
  added to that step's velocity and taken off the one after). Exactly one simulation detects it: the server's copy
  when the server runs the mod (it sends the velocity it added on `tgmove.honey_jump` and the client mod adds the same
  on its next airborne step, plus the steps it jumped weakly since its jump key's tick until the word came: `core::tgmove::honeyCatchUp`, gtested
  for 0 to 4 steps late against the full jump), the client mod alone without one. Both making it up stacked through the server's
  corrections (measured in round 3: 2.52 and 4.06 blocks in two local runs); the client e2e now jumps 3 times and
  wants 2.52 +- 0.15 each, made up once each. Without the input (mobs; simulated players) the input
  acceleration is the one the last two engine steps agree on (a push or knockback is not taken for input). A vanilla
  client's server copy keeps the engine's friction (its client simulates the engine's; correcting it every tick would
  pull it back and forth): for vanilla clients unrestrained is only the slowdown immunity of cobweb, sweet berry bush
  and powder snow, with no friction, slime, honey or soul sand change.
  Input changes on the ground with unrestrained (agile ranger, straight ahead, the glide's sneak) accelerate with 0.6's
  factor, as the rest of the step (`inputDelta(..., unrestrained)`), not with the block's.
  WaterSprint: upstream only treats a player standing on water as "not in water" (`unrestrainedSprintingInWater` returns
  `isInWater` unless `canStandOnFluid`), so sprinting is kept while water walking and nowhere else, as here.
- Swift swim: water movement efficiency +1 is Depth Strider III (`WaterWalkSpeedEnchantComponent`); e2e measures the
  swimming speed with and without the vest.
- Agile ranger: Java multiplies the input by 5, which cancels its 0.2 use slowdown. Bedrock's slowdown is in the
  input (measured: a held key's input is 0.122 long while drawing a bow, client and server alike), so both
  simulations add the velocity of the input without it (`agileRangerInput`: / the slowdown, at most length 1;
  `inputDelta`). The slowdown is the current use's, read from the input (`itemUseSlowdownOf`: the engine's input over
  the raw input `MoveInputState::mAnalogMoveVector`; each item has its own), 0.122 where the raw input does not tell
  and multiply `ItemUseSlowdownModifierComponent` by 5 (at most 1); e2e measures walking while drawing a bow with and
  without the modifier (x5 of the input alone gave 0.129 blocks per tick against 0.207 walking).
- Water walk (client mod and the server's copy of a companion): a water source with no water above carries the player
  at its half-block stable shape; the player is on the ground there (`OnGroundFlagComponent`) with ground speed (Depth
  Strider III), sprinting kept with unrestrained. Splash particles every 3rd tick come from the server.
- Aquatic agility: the oxygen bonus keeps 3 of 4 breaths (`BreathableComponent`); submerged mining speed x5 through
  the contract extension `gf::ext::mining` (a factor on `Player::getDestroyProgress` while the eyes are under water,
  both builds), the same x5 Java's +4 multiply-total gives over the attribute's base.
- Efficient eater (ReduceMovementFoodExhaustion): the server halves Java's movement exhaustion (sprint 0.1 / m, swim
  and under water 0.01 / m in 3D, wading 0.01 / m horizontally) and the exhaustion of ground jumps (0.05 / 0.2) and
  double jumps. It takes back at most half of what the exhaustion value actually grew by in the tick (the exhaustion
  of mining or attacking stays), and a move of more than a block in a tick (a teleport, a push) is not movement.
  When the exhaustion passes 4 in a tick (the game takes 4 off and a saturation or food point), the refund counts the
  wrap and what the exhaustion cannot give back yet is owed and taken from the next ticks' exhaustion (at most 4).
- Companion packets: Java's move packets come before PerformDoubleJumpPacket / PerformSidestepPacket; Bedrock's auth
  input of the same tick can come after the custom packet. A packet the server copy refuses when it comes is tried
  again on the next 3 ticks (`processPending`) and only then counts for upstream's validator (`tgmove.packet_before_move`).
- Straight ahead's FOV (PlayerHooks#straightAheadNullify): the client compensates only while the synced movement
  speed attribute carries the `gloamingforest:travellers_gear.boots_straight_ahead` modifier, by its amount, with the
  walk speed ability; the e2e compares the FOV factor walking with straight ahead (compensated) and without it.
- Input changes (handleStraightAhead's strafe / 1.4, handleAgileRanger's x5, speedUpControlledWhileSneaking's / 0.2):
  Bedrock reads `MoveInputComponent::mMove` after any hook of ours (measured: an mMove written before the local tick
  changed nothing), so both simulations add the velocity the changed input makes in one step instead
  (`inputDelta`: the difference of Entity#moveRelative's accelerations, on the ground speed x the block's friction
  factor, in the air the flying speed 0.02 / 0.026). Measured: Bedrock's input while sneaking is already 0.3 long
  (its sneak factor is in the input, as Java's); gliding with SHIFT + W moves 0.198 blocks per tick (upstream: air speed,
  about 0.2; 0.047 without the change).
- Water walk splash: only while the server copy stands on the water (a companion water walking), as upstream's
  processWaterWalking.
- Per tick, each player's (and wearer mob's) active modifiers are one mask (one gear lookup per armour slot); mobs
  are looked for every 20 ticks and their wearers ticked until the gear comes off. The client drops the wing state
  of entities it has not drawn for 200 frames; each wing wearer's flap starts at a random phase
  (TravellersWingsAnimAttachment).
- leashed_pathfinder_override: measured (`tgmove.leash`): Bedrock's leashed pigs 3.5 blocks from the holder come to
  about 1 block of it within 5 s (the leash's default preset is soft 4 / hard 7 / max 12). A marked mob's leash presets
  get a soft distance of at least 6 (Java pulls a marked mob only beyond 6 blocks), and while it is within 6 blocks of
  its holder a navigation path of it that ends within 2.5 blocks of the holder is dropped
  (`NavigationComponent::mPath`; Java's `closeRangeLeashBehaviour` paths it to 2 blocks of the holder unless marked);
  its other paths (wandering) stay. `tgmove.leash` checks every tick for 100 ticks that no marked pig within 6 blocks
  has a path to its holder, and reports the unmarked pigs' paths to it and every pig's distance (pigs also stroll,
  past the holder too, so distances alone do not tell).
  The marker is dropped (and the mob's own presets put back) when the mob cannot be leashed, or is not leashed when it
  joins the world (upstream's `handleLeashPathingOverrides` on EntityJoinLevel: `!mayBeLeashed`); a mob counts as joined once
  it is 40 ticks old in this session (whoever marks it leashes it right after spawning it), checked once per session.
- Wing state for late observers: the state goes to the companions within 64 blocks when it changes, to companions
  that came into range (or joined) since within 20 ticks, and to every tracker every 200 ticks.

## Vanilla clients (best effort)

- Double jump: jump pressed in the air (F12's input edges) -> the server's double jump with a motion packet; a
  may-fly player's second press within 6 ticks starts creative flight instead (upstream's `avoidCreativeFly`).
- Gradual glide: sneaking while falling -> hidden Slow Falling. Slow Falling's terminal sink is about 0.06 blocks per
  tick, much slower than upstream's glide (vy x 5/6 per tick, about 0.36 blocks per tick); Bedrock offers no other way to
  slow a vanilla client's fall.
- High jump, slimy soles' bounce, straight ahead's speed (from the auth input's forward key), the double jump's safe
  fall, swift swim, agile ranger and aquatic agility work from the server's simulation (the vanilla client follows the
  server's attributes / effects / corrections).
- Unrestrained: only the slowdown immunity of cobweb, sweet berry bush and powder snow (an entity component the
  vanilla client simulates too). Friction 0.6, the slime and honey step slowdowns, the honey jump and soul sand are
  not changed for a vanilla client: its own simulation keeps the engine's, and correcting its server copy every tick
  would pull it back and forth.
- The wings: the model's rest pose, no state-driven poses (the pose comes from the client mod, which adds the
  animation to the attachable only on its side; see Wings animation).
- Not available: sidestep (the spec lists it as unavailable; the auth input's per-tick move vector would show a strafe
  double tap, but the push is client-side prediction a vanilla client does not make, and a server push on a guessed
  tap would fire on ordinary strafing), step up and water walking on the client side (the server's copy does not
  water-walk for a client that does not).

## Audit round 1

- Checked: the two `tgmove.*` failures reported from a full run (`tgmove.unrestrained_blocks`: "the full jump on honey:
  0.514"; `tgmove.validator`: the snap back, the retried packet and the sidestep packet). Both come from one run
  (worktree of U17, 2026-11-06) whose server log has `Server stop requested.` at 19:32:39.9, exactly 900 s after the
  scenario started: `tools/e2e/server/run.ps1`'s default `-TimeoutSec 900` stopped the server in the middle of
  `tgmove.unrestrained_blocks` ("Disabling GloamingForest" follows), so this unit's tick hooks, the companion channels
  and the `tgmove.travellers_movement` validator were gone for the honey jump and for every check of
  `tgmove.validator`; the selftests ran on in the stopping server (as did `trees.core_disabled_config` after them).
  Not a defect of this unit: run `full` with `-TimeoutSec 14400` (docs/spec/e2e.md). The same code passes in main's
  full run (927 / 928, 2026-11-07) and in the `tgmove` scenario.
- Client run (key-driven movement, `tools/e2e/client/run.ps1 -Scenario tgmove -Mode local`, 2026-11-07): PASSED.
  Measured against upstream: double jump 2.52 blocks (upstream about 2.5); sidestep 4.04 blocks while the strafe key is held (upstream's
  push alone 1.6 / 0.454 = 3.5; the held key adds its walk); glide sink 0.352 blocks per tick (upstream terminal 0.357) and
  0.196 forward with sneak (upstream about 0.2); water walk stands at the source + 0.5; step up 1.0 block; unrestrained honey
  jumps 2.52 three times (upstream 2.52, made up once each); agile ranger bow-draw walk 0.212 (walk 0.207); soul sand 0.209,
  cobweb 0.207, ice walk 0.205 / slide 0.26 as on grass, slime walk 0.205; swift swim 0.207 (0.085 without); straight
  ahead x1.393 forward, strafe 0.581 (upstream 0.581), FOV compensated. Screenshots (wings idle, double jump with the
  DOUBLE_JUMP pose and particles, glide, water walk, step up, bow) checked by eye: the wings' rest and flap poses read
  as upstream's TravellersWingsModel.
- Open: the hidden Jump Boost II of high jump (and the hidden Slow Falling of the vanilla glide) shows its icon in
  Bedrock's HUD effect list (`mEffectVisible` false hides the particles only; Java's attribute shows nothing). Hiding
  it needs a client hook of the HUD effect list (HudMobEffectsRenderer / MobEffectsLayout, whose entries carry no
  effect id): an F12 contract addition for round 2.
