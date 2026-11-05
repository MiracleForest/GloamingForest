# U12 `tgmove`: Travellers' Gear movement

The twelve movement modifiers of TF 4.8's Travellers' Gear (behaviour: [docs/spec/tgmove.md](../spec/tgmove.md)).

| Part | Where |
|---|---|
| Rules, numbers, state machines (pure, gtested) | `src/core/tgmove/TgMove.{h,cpp}`, `tests/tgmove/TgMoveTest.cpp` |
| Server: per-tick logic, companion packets, vanilla fallbacks, fall damage | `src/feature/tgmove/TgMove.{h,cpp}` |
| Selftests `tgmove.*` (dx 1200..1299) | `src/feature/tgmove/TgMoveTests.cpp` |
| Client mod: the local player's movement | `src/client/tgmove/TgMoveClient.cpp` |
| Particle `twilightforest:double_jump` (TF's DoubleJumpParticle = ExplodeParticle, half size) | `assets/pack/rp/particles/tgmove/double_jump.json` (the generated skeleton is dropped by `tools/import/overrides/tgmove.json`) |
| e2e | `tools/e2e/server/scenarios/tgmove.ps1`, `tools/e2e/client/scenarios/tgmove.ps1` |

## Who does what

Java's movement is client authoritative and so is Bedrock's. With the client mod, the local player applies the
double jump, sidestep, glide, step up, water walk, unrestrained, swift swim, agile ranger and straight ahead's strafe
before its tick (`gf::client::movement::onLocalTick`) and sends TF's packets on `tgmove.double_jump`,
`tgmove.sidestep` and `tgmove.gradual_glide`. The server repeats the double jump and sidestep (sound, particles,
safe fall, wing state, cooldown sound), runs TF's validator on refused packets (dedicated servers only, as TF: 2 free,
then a snap back through F12's movement validator `tgmove.travellers_movement`, a disconnect at 5) and owns: Jump
Boost II (high jump), the fall distance of a glide (v^2 / 2g), the safe fall +2 after a double jump, slimy soles'
cancelled landing and bounce (SetActorMotionPacket, `tgmove.slimy` gives the companion the next double jump's
boost), straight ahead's x1.4 speed (movement speed attribute, forward only), the step height and slowdown
immunity of the server's copy, aquatic agility's oxygen bonus, efficient eater's movement exhaustion and the wing
state (`tgmove.wings_state` to companions within 64 blocks).

## Bedrock adaptations

- Safe fall distance and fall damage: Bedrock has no safe fall attribute; the double jump's +2 is 2 less fall damage
  (F10's combat hook), which is what Java's attribute does.
- Unrestrained: Bedrock's `BlockMovementSlowdownImmunityComponent` for cobweb, sweet berry bush, powder snow, honey
  block and soul sand (client and server copy) instead of clearing `stuckSpeedMultiplier` / the block factors.
- Swift swim: water movement efficiency +1 is Depth Strider III (`WaterWalkSpeedEnchantComponent`).
- Water walk (client mod): a water source with no water above carries the player at its half-block stable shape;
  the player is on the ground there (`OnGroundFlagComponent`) with ground speed (Depth Strider III), sprinting kept
  with unrestrained. Splash particles every 3rd tick come from the server.
- Aquatic agility: the oxygen bonus keeps 3 of 4 breaths (`BreathableComponent`).
- Efficient eater (ReduceMovementFoodExhaustion): the server halves Java's movement exhaustion (sprint 0.1 / m,
  swim 0.01 / m, the double jump's 0.05 / 0.2).

## Vanilla clients (best effort)

- Double jump: jump pressed in the air (F12's input edges) -> the server's double jump with a motion packet.
- Gradual glide: sneaking while falling -> hidden Slow Falling.
- High jump, slimy soles' bounce, straight ahead's speed, the double jump's safe fall, unrestrained, swift swim and
  aquatic agility work from the server (as far as the vanilla client follows the server's attributes / effects).
- Not available: sidestep (a double tap is not visible in the auth input), step up, water walking, agile ranger, the
  wings' animation.

## Not ported / open

- The wings' animation (`TravellersWingsModel`): the state machine and the animation math are translated
  (`core::tgmove::determineWingState`, `animateWings`) and the state is synced, but the wings' attachable is U11's
  file and has no animation hooked to it yet.
- Aquatic agility's submerged mining speed x5: no destroy-speed hook for armour in the contracts.
- `leashed_pathfinder_override`: Bedrock leashed mobs do not path to their holder, so there is nothing to override;
  U21 needs no marker.
