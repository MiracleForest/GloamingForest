# U12 `tgmove`: Travellers' Gear movement

The twelve movement modifiers of TF 4.8's Travellers' Gear (behaviour: [docs/spec/tgmove.md](../spec/tgmove.md)).

| Part | Where |
|---|---|
| Rules, numbers, state machines (pure, gtested) | `src/core/tgmove/TgMove.{h,cpp}`, `tests/tgmove/TgMoveTest.cpp` |
| Server: per-tick logic of players and mobs, companion packets, vanilla fallbacks, fall damage | `src/feature/tgmove/TgMove.{h,cpp}` |
| Selftests `tgmove.*` (dx 1200..1299) | `src/feature/tgmove/TgMoveTests.cpp` |
| Client mod: the local player's movement, the wings' animation | `src/client/tgmove/TgMoveClient.cpp` |
| Wings animation (TravellersWingsModel) | `assets/pack/rp/animations/tgmove/travellers_wings.animation.json` (played by the wings' attachable, see below) |
| Particle `twilightforest:double_jump` (TF's DoubleJumpParticle = ExplodeParticle, half size) | `assets/pack/rp/particles/tgmove/double_jump.json` (the generated skeleton is dropped by `tools/import/overrides/tgmove.json`) |
| Mining speed extension (submerged mining speed) | `src/feature/ext/tgmove/Mining.{h,cpp}` (contract extension, R18) |
| e2e | `tools/e2e/server/scenarios/tgmove.ps1`, `tools/e2e/client/scenarios/tgmove.ps1` |

## Who does what

Java's movement is client authoritative; Bedrock's is server authoritative: the server simulates every player from its
input and corrects a client that ends up elsewhere (measured: a double jump the client made on its own was put back 2
ticks later). So each modifier runs in both simulations of a player. With the client mod, the local player applies
the double jump, sidestep, glide, step up, water walk, unrestrained, swift swim, agile ranger and straight ahead's
strafe before its tick (`gf::client::movement::onLocalTick`) and sends TF's packets on `tgmove.double_jump`,
`tgmove.sidestep` and `tgmove.gradual_glide`. The server pushes the double jump and the sidestep itself
(SetActorMotionPacket; sound, particles, safe fall, wing state, cooldown sound), applies the glide, the water walk, the
step height, the slowdown immunity, Depth Strider, agile ranger's item use slowdown and straight ahead's x1.4 speed
(movement speed attribute, forward input only) to its copy, runs TF's validator on refused packets (dedicated servers
only, as TF: 2 free, then a snap back through F12's movement validator `tgmove.travellers_movement`, a disconnect at 5)
and owns: Jump Boost II (high jump), the fall distance of a glide (v^2 / 2g), the safe fall +2 after a double jump,
slimy soles' cancelled landing and bounce (`tgmove.slimy` gives the companion the next double jump's boost), aquatic
agility's oxygen bonus and mining speed, efficient eater's movement and jump exhaustion and the wing state
(`tgmove.wings_state` to companions within 64 blocks). Every value the unit changes on the engine (step height,
immunity entries, Depth Strider, item use slowdown, attribute modifier, effects) is put back when the modifier ends,
when the state is dropped (disconnect, respawn, tests) and at uninstall; a weaker effect of the player's own that Jump
Boost II replaced comes back with the time it had left (Java's hidden effect).

Mobs wearing the gear (TF's `updateOtherModifiers` runs for every LivingEntity) glide whenever they fall, bounce on
slimy soles, get Jump Boost II, run x1.4 with straight ahead, ignore block slowdowns with unrestrained and have a
synced wing state.

`last_damage_armor_time` is kept in F8's entity data under `twilightforest:last_damage_armor_time` (game time; damage
types in `#minecraft:bypasses_armor` do not count), which U11's auto repair reads by that string.

`leashed_pathfinder_override` is the entity tag `twilightforest:leashed_pathfinder_override` (U21 adds it by string to
the lich tower's fenced mobs); it is dropped from a mob that can no longer be leashed (no `LeashableComponent`), as
TF's `handleLeashPathingOverrides`. Its effect, no stay-close pathing within 6 blocks of the holder: see the last
item of "Bedrock adaptations" (the leash pulls a marked mob only beyond 6 blocks; Bedrock's own approach to the holder
is not reachable).

## Wings animation

`TravellersWingsModel#setupModelAnimations` (state, phase, interpolation to the target with tau 4, crouch drop) is
`core::tgmove::animateWings`. The client mod runs it every frame (`client::render::onWorld`) for every player (and mob)
wearing the wings, from the synced `tgmove.wings_state`, and hands the wing bases' pose to the actor's Molang variables
`variable.tgmove_wing_{on,rx,ry,rz,ly,drop}`. `animation.twilightforest.travellers_wings` (U12's file) turns those into
deltas from the geometry's rest pose; without the client mod (vanilla clients) it plays TF's IDLE flap (phase / 20,
sin / 15 around 30°, -0.6, -0.3 rad, the left wing mirrored, 2 px lower while sneaking).

An attachable's `v.` variables are its own, not the wearer's (measured: `unknown variable 'variable.tgmove_wing_on'`),
and Molang's `??` does not take an `->` expression. So the client sets the pose as *public* variables of the wearer
(`MolangVariableSetting_AccessSpecifier::Public` plus the public value) and the animation reads them as
`c.owning_entity->v.tgmove_wing_*`.

Open, for U11 / F2 (not U12's files): the wings' attachable
`assets/gen/geo/tgear/rp/attachables/travellers_wings.json` (item `twilightforest:travellers_wings`, U11; generated by
F2, whose `models.attachables` selftest pins the file and requires its animations in F2's index) has to play the
animation and hide the wings in first person, as TF does (tried locally, not committed: the first person shows no
wing boxes with it):

```json
"animations": {"wings": "animation.twilightforest.travellers_wings"},
"scripts": {"parent_setup": "variable.leg_layer_visible = 0.0;", "animate": ["wings"]},
"render_controllers": [{"controller.render.armor": "!c.is_first_person"}]
```

U12 tried a manual override of that attachable (same id, in its own pack folder): it works for the game but takes
the attachable away from its owner and fails F2's `models.attachables` in the full run, so it is not shipped. Until
the attachable plays the animation, the wings stay in the geometry's rest pose.

## Bedrock adaptations

- Safe fall distance and fall damage: Bedrock has no safe fall attribute; the double jump's +2 is 2 less fall damage
  (F10's combat hook), which is what Java's attribute does; slimy soles decides on that reduced damage.
- Unrestrained, stuck speed (ResetStuckUnrestrained): Bedrock's `BlockMovementSlowdownImmunityComponent` for cobweb,
  sweet berry bush and powder snow (client and server copy), the blocks whose slowdown that component covers
  (`EntityInsideSystemImpl::tickWebMovementSlowdown` / `tickPowderSnowMovementSlowdown`).
- Unrestrained on the ground (BlockHooks#resetBlockFrictionWithUnrestrained, #resetSlimeMomentumWithUnrestrained,
  #stopBouncing; EntityHooks#resetFactorWithUnrestrained): Bedrock has no per-entity block friction, so
  `applyUnrestrained` corrects the velocity before each engine step, in the client mod and in the server copy alike:
  the drag of the last step (friction x 0.91 and the engine's own slowdown) is turned into TF's 0.6 x 0.91, and the
  input acceleration of the coming step gets the difference between 0.6's factor (0.21600002 / 0.6^3) and the
  block's (`core::tgmove::unrestrainedStep`, gtested against a simulated engine: same positions and velocities as
  Java's 0.6 step). What Bedrock does on each block was measured (`tgmove.unrestrained_blocks`, a 0.4 push and a 20
  tick walk of a simulated player, without and with the boots):

  | Block | Bedrock | Unrestrained here (TF) |
  |---|---|---|
  | ice / packed ice / blue ice | friction 0.98 / 0.98 / 0.989: a 0.4 push slides 3.66 / 3.66 / 3.94 blocks | 0.881, as on grass (0.881) |
  | slime | friction 0.8 and Java's step-on factor 0.4 + 0.2 x \|vy\| on the velocity: 0.57; walk 0.058 | 0.881; walk 0.19 (grass 0.20); no walking bounce |
  | honey block | the same as slime (friction 0.8, factor 0.4 + 0.2 x \|vy\|): 0.57; walk 0.058; jump 0.514 high (power 0.42 x 0.6) | 0.881; walk 0.19; jump 1.252, as on grass |
  | soul sand | a slide is not slowed; the walk's acceleration x 0.544 (walk 0.110) | walk 0.193 |

  A slime landing slower than 0.08 (a walk) does not bounce (`stopBouncing`: the engine's bounce of it is taken back);
  a fall still bounces. The honey jump: the engine's weaker jump is made up in the next step (the height it lost is
  added to that step's velocity and taken off the one after). Without the input (mobs; simulated players) the input
  acceleration is the one the last two engine steps agree on (a push or knockback is not taken for input). A vanilla
  client's server copy keeps the engine's friction (its client simulates the engine's; correcting it every tick would
  pull it back and forth).
  WaterSprint: TF only treats a player standing on water as "not in water" (`unrestrainedSprintingInWater` returns
  `isInWater` unless `canStandOnFluid`), so sprinting is kept while water walking and nowhere else, as here.
- Swift swim: water movement efficiency +1 is Depth Strider III (`WaterWalkSpeedEnchantComponent`); e2e measures the
  swimming speed with and without the vest.
- Agile ranger: Java multiplies the input by 5 (cancelling the 0.2 use slowdown); both simulations multiply Bedrock's
  `ItemUseSlowdownModifierComponent` by 5 (at most 1) and add the velocity of the x5 input (`inputDelta`, for an
  input that carries the slowdown); e2e measures walking while drawing a bow with and without the modifier.
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
- Companion packets: Java's move packets come before PerformDoubleJumpPacket / PerformSidestepPacket; Bedrock's auth
  input of the same tick can come after the custom packet. A packet the server copy refuses when it comes is tried
  again on the next 3 ticks (`processPending`) and only then counts for TF's validator (`tgmove.packet_before_move`).
- Straight ahead's FOV (PlayerHooks#straightAheadNullify): the client compensates only while the synced movement
  speed attribute carries the `twilightforest:travellers_gear.boots_straight_ahead` modifier, by its amount, with the
  walk speed ability; the e2e compares the FOV factor walking with straight ahead (compensated) and without it.
- Input changes (handleStraightAhead's strafe / 1.4, handleAgileRanger's x5, speedUpControlledWhileSneaking's / 0.2):
  Bedrock reads `MoveInputComponent::mMove` after any hook of ours (measured: an mMove written before the local tick
  changed nothing), so both simulations add the velocity the changed input makes in one step instead
  (`inputDelta`: the difference of Entity#moveRelative's accelerations, on the ground speed x the block's friction
  factor, in the air the flying speed 0.02 / 0.026). Measured: Bedrock's input while sneaking is already 0.3 long
  (its sneak factor is in the input, as Java's); gliding with SHIFT + W moves 0.198 blocks per tick (TF: air speed,
  about 0.2; 0.047 without the change).
- Water walk splash: only while the server copy stands on the water (a companion water walking), as TF's
  processWaterWalking.
- Per tick, each player's (and wearer mob's) active modifiers are one mask (one gear lookup per armour slot); mobs
  are looked for every 20 ticks and their wearers ticked until the gear comes off. The client drops the wing state
  of entities it has not drawn for 200 frames; each wing wearer's flap starts at a random phase
  (TravellersWingsAnimAttachment).
- leashed_pathfinder_override: measured (`tgmove.leash`): Bedrock's leashed pigs 3.5 blocks from the holder come to
  about 1 block of it within 5 s (the leash's default preset is soft 4 / hard 7 / max 12; the approach is not the
  leash spring). A marked mob's leash presets get a soft distance of at least 6 (Java pulls a marked mob only beyond
  6 blocks); Bedrock's approach within the soft distance is an AI path of the engine that LeviLamina does not expose
  (MoveTowardsRestrictionGoal and the leash systems have no callable members), so marked mobs still come closer.

## Vanilla clients (best effort)

- Double jump: jump pressed in the air (F12's input edges) -> the server's double jump with a motion packet; a
  may-fly player's second press within 6 ticks starts creative flight instead (TF's `avoidCreativeFly`).
- Gradual glide: sneaking while falling -> hidden Slow Falling. Slow Falling's terminal sink is about 0.06 blocks per
  tick, much slower than TF's glide (vy x 5/6 per tick, about 0.36 blocks per tick); Bedrock offers no other way to
  slow a vanilla client's fall.
- High jump, slimy soles' bounce, straight ahead's speed (from the auth input's forward key), the double jump's safe
  fall, unrestrained, swift swim, agile ranger and aquatic agility work from the server's simulation (the vanilla
  client follows the server's attributes / effects / corrections).
- The wings show TF's idle flap (no state-driven poses).
- Not available: sidestep (a double tap is not visible in the auth input), step up and water walking on the client
  side (the server's copy does not water-walk for a client that does not).
