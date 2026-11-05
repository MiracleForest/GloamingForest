# U12 `tgmove`: Travellers' Gear movement

The twelve movement modifiers of TF 4.8's Travellers' Gear (behaviour: [docs/spec/tgmove.md](../spec/tgmove.md)).

| Part | Where |
|---|---|
| Rules, numbers, state machines (pure, gtested) | `src/core/tgmove/TgMove.{h,cpp}`, `tests/tgmove/TgMoveTest.cpp` |
| Server: per-tick logic of players and mobs, companion packets, vanilla fallbacks, fall damage | `src/feature/tgmove/TgMove.{h,cpp}` |
| Selftests `tgmove.*` (dx 1200..1299) | `src/feature/tgmove/TgMoveTests.cpp` |
| Client mod: the local player's movement, the wings' animation | `src/client/tgmove/TgMoveClient.cpp` |
| Wings animation (TravellersWingsModel) | `assets/pack/rp/animations/tgmove/travellers_wings.animation.json`, `assets/pack/rp/attachables/tgmove/travellers_wings.json` |
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
TF's `handleLeashPathingOverrides`. Its effect, no stay-close pathing within 6 blocks of the holder, is how Bedrock's
leash always works: `LeashableSystem` only pulls a leashed mob with a spring beyond the preset's soft distance
(`LeashablePreset` soft / hard / max distance) and never paths it to the holder, so a marked mob needs nothing more.

## Wings animation

`TravellersWingsModel#setupModelAnimations` (state, phase, interpolation to the target with tau 4, crouch drop) is
`core::tgmove::animateWings`. The client mod runs it every frame (`client::render::onWorld`) for every player (and mob)
wearing the wings, from the synced `tgmove.wings_state`, and hands the wing bases' pose to the actor's Molang variables
`variable.tgmove_wing_{on,rx,ry,rz,ly,drop}`. The wings' attachable (U12's manual override of the generated
`twilightforest:travellers_wings` attachable of U11 / F2; same geometry, texture and materials) plays
`animation.twilightforest.travellers_wings`, which turns those into deltas from the geometry's rest pose. Without the
client mod (vanilla clients) the animation plays TF's IDLE flap (phase / 20, sin / 15 around 30°, -0.6, -0.3 rad, the
left wing mirrored, 2 px lower while sneaking). The wings are not drawn in first person (`!c.is_first_person`), as in
TF.

## Bedrock adaptations

- Safe fall distance and fall damage: Bedrock has no safe fall attribute; the double jump's +2 is 2 less fall damage
  (F10's combat hook), which is what Java's attribute does; slimy soles decides on that reduced damage.
- Unrestrained: Bedrock's `BlockMovementSlowdownImmunityComponent` for cobweb, sweet berry bush, powder snow, honey
  block and soul sand (client and server copy) instead of clearing `stuckSpeedMultiplier` / the block factors; the
  e2e measures soul sand and cobweb speed and the honey block jump with and without it. WaterSprint: TF only treats a
  player standing on water as "not in water" (`unrestrainedSprintingInWater` returns `isInWater` unless
  `canStandOnFluid`), so sprinting is kept while water walking and nowhere else, as here.
- Swift swim: water movement efficiency +1 is Depth Strider III (`WaterWalkSpeedEnchantComponent`); e2e measures the
  swimming speed with and without the vest.
- Agile ranger: Java multiplies the input by 5 (cancelling the 0.2 use slowdown); Bedrock applies the bow's slowdown
  from `ItemUseSlowdownModifierComponent`, which both simulations multiply by 5 (at most 1); e2e measures walking while
  drawing a bow with and without the modifier.
- Water walk (client mod and the server's copy of a companion): a water source with no water above carries the player
  at its half-block stable shape; the player is on the ground there (`OnGroundFlagComponent`) with ground speed (Depth
  Strider III), sprinting kept with unrestrained. Splash particles every 3rd tick come from the server.
- Aquatic agility: the oxygen bonus keeps 3 of 4 breaths (`BreathableComponent`); submerged mining speed x5 through
  the contract extension `gf::ext::mining` (a factor on `Player::getDestroyProgress` while the eyes are under water,
  both builds), the same x5 Java's +4 multiply-total gives over the attribute's base.
- Efficient eater (ReduceMovementFoodExhaustion): the server halves Java's movement exhaustion (sprint 0.1 / m, swim
  and under water 0.01 / m in 3D, wading 0.01 / m horizontally) and the exhaustion of ground jumps (0.05 / 0.2) and
  double jumps.

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
