# Travellers' Gear: movement (U12 `tgmove`)

Behaviour spec of unit **U12** (wave W1), translated from The Twilight Forest 4.8 (`9e40a757`, MC 1.21.1) by F16. It is
the checklist the U12 worker implements and the A6 audit compares against. Sections 1-5 are the behaviour; Appendix A
lists every id U12 owns with TF's registration and data; Appendix B the strings. The gear items, recipes and the
modifier framework are U11's (`tgear.md`); U12 implements the twelve movement modifiers and the wings animation state.

## 1. Scope

- Modifiers (registry entries `travellers_modifiers/twilightforest:<id>`) and their data components:
  `agile_ranger` (`agile_ranger_modifier`), `aquatic_agility` (`aquatic_agility`), `double_jump` (`double_jump`),
  `gradual_glide` (`gradually_gliding_multiplier`), `high_jump` (`high_jump_amplifier`), `side_step`
  (`sidestep_cooldown`), `slimy_soles` (`slimy_soles_coefficient`), `step_up` (`high_step`), `straight_ahead`
  (`straight_ahead_multiplier`), `swift_swim` (`swift_swim`), `unrestrained` (`unrestrained`), `water_walk`
  (`water_walk`).
- 17 attachments (`double_jump_validator`, `double_jump_validator_last_check`, `has_double_jump`,
  `is_gradually_gliding`, `last_damage_armor_time`, `last_horizontal_impulse`, `last_horizontal_walking_time`,
  `last_jump_key_press_time`, `last_non_horizontal_impulse`, `last_tick_water_walking`, `leashed_pathfinder_override`,
  `sidestep_validator`, `sidestep_validator_last_check`, `slimy_soles_bounce_info`, `temporary_saved_straight_ahead`,
  `travellers_wings`, `travellers_wings_anim`), particle `double_jump`, client config
  `client.travellersWingsGradualGlide` (section `tgmove`), net channels `tgmove.double_jump`, `tgmove.gradual_glide`,
  `tgmove.sidestep`, `tgmove.wings_state`.
- Sound events are U11's ids (`item.twilightforest.travellers_gear.double_jump`, `side_step`, `side_step_ready`):
  U12 plays them by id.

Contracts: gear (data), client.input+movement, rules+fx, combat.

## 2. TF sources

`init/custom/TravellersModifiersManager.java`, `item/travellers_gear/TravellersGearLogic.java`,
`events/TravellersGearEvents.java`, `client/event/TravellersClientEvents.java`, `asmhooks/EntityHooks.java`
(water walk, unrestrained), `asmhooks/PlayerHooks.java` (straight ahead FOV), `components/entity/{TravellersWingsAttachment,TravellersWingsAnimAttachment,SlimySolesAttachment}.java`,
`init/TFDataAttachments.java`, `init/TFAttributeModifiers.java`, `network/{PerformDoubleJump,PerformSidestep,GradualGlide,TravellersWingsState}Packet.java`,
`client/model/armor/TravellersWingsModel.java`, `client/particle/DoubleJumpParticle.java`, `config/TFClientConfig.java`,
`events/EntityEvents.java` (leash override cleanup), `world/components/structures/lichtowerrevamp/*` (sets the leash
override on fenced mobs).

## 3. Behaviour

Active = the modifier is on the stack in its slot, the wearer is not a spectator and the piece is not broken (U11).

| Modifier | Slot | Value | Behaviour |
|---|---|---|---|
| `double_jump` | legs | unit | `has_double_jump` is reset to true on ground / in liquid / on a ladder (false when inactive; reset also clears the validator and the safe-fall modifier). Client: pressing jump (no screen open; ignored if a may-fly player pressed jump within the last 6 ticks, so creative double-tap flight still works) calls `performDoubleJump` and sends `PerformDoubleJumpPacket`; the server repeats it and, if it fails, runs the abuse validator. `performDoubleJump`: requires `has_double_jump` and not elytra-flying / climbing / on ground / swimming / creative-flying / in liquid / riding; does a normal jump (`jumpFromGround`), adds the slimy-soles stored boost (vertical speed = sqrt(vy^2 + boost^2)), resets fall distance, plays `...travellers_gear.double_jump` (1.5, (voicePitch - 1)*1.1 + 0.98), clears `has_double_jump`, adds `+2` safe fall distance (`travellers_gear.boots_double_jump_safe_fall_distance`), and with wings sets wing state DOUBLE_JUMP; server sends 10 `twilightforest:double_jump` particles from the wings (body yaw offset /3, y+1.2) and the wing state packet. |
| `side_step` | legs | `sidestep_cooldown` 40 ticks | Client (`handleSidestep`, each movement-input update): on ground, a horizontal (strafe) key pressed again in the same direction (sign of the last non-zero strafe impulse) on a tick whose previous impulse was zero, with `tickCount - lastHorizontalWalkingTime` **less than 4 ticks (<= 3)**, where `lastHorizontalWalkingTime` is the last tick the strafe impulse was non-zero -> `tryPerformSidestep` + `PerformSidestepPacket`. Requires more than the cooldown since `lastSidestepTime` (`gameTime - last > cooldown`), not elytra, on ground, not crouching. Pushes the player 1.6 sideways (~5 blocks), plays `...travellers_gear.side_step` (1, voice pitch), wing state SIDESTEP (left/right). When the cooldown has passed the wearer hears `...side_step_ready` once. Tooltip interpolates the left/right key names. |
| `gradual_glide` | legs | `gradually_gliding_multiplier` 1 - 1/6 | Client decides gliding = (`travellersWingsGradualGlide` default true) == sneak held, falling (y velocity < 0) and not on ground; sent with `GradualGlidePacket` (server relays to trackers). Each tick while falling and gliding (not elytra): vy *= 5/6 and fall distance = vy^2 / 2 / gravity. While gliding and sneaking the input impulses are divided by 0.2 (sneak does not slow). Server turns gliding off when the modifier is inactive. |
| `high_jump` | legs | builtin `high_jump_amplifier` 1 (wings) | Jump Boost II (amplifier 1, 2 ticks, hidden) every tick |
| `agile_ranger` | legs | `agile_ranger_modifier` 5.0 | Client movement input: while using a projectile weapon (or `#twilightforest:travellers_agile_ranger_whitelisted`, not `#travellers_agile_ranger_blacklisted`) and not riding, strafe/forward impulses x5 (cancels the use slowdown) |
| `step_up` | feet | builtin `high_step` | +0.5 step height (`travellers_gear.boots_high_step`), removed while crouching |
| `straight_ahead` | feet | `straight_ahead_multiplier` 1.4 | movement speed x1.4 (`travellers_gear.boots_straight_ahead`, multiply-total) only while moving forward (client input), strafe impulse divided by 1.4; the FOV calculation ignores this modifier (no FOV zoom-out) |
| `slimy_soles` | feet | `slimy_soles_coefficient` 0.5 | landing (not sneaking) with fall damage > 0 (or a forced bounce) cancels the fall; bounce velocity = -vy x sqrt(0.5), also stored as the next double jump boost; next tick: `entity.slime.jump` (0.5, 1), slime block particles (count 40 x min(0.2 + v, 2.5), offsets 0, speed 0.15), vy = sqrt(vy^2 + v^2); forced bounce continues while |vy| > 0.25; a normal jump clears it |
| `unrestrained` | feet | unit | stuck-speed multipliers (cobwebs, berry bushes, powder snow) cleared every tick; block speed / jump factors (soul sand, honey) forced to 1.0; can sprint in water |
| `water_walk` | feet | unit | water counts as standable when the submerged height < **0.4** and not sneaking; every 3rd tick while walking on water (moving > 0.01) splash particles (width count, velocity -motion, +0.5 up) |
| `swift_swim` | chest | builtin (vest) | +1 water movement efficiency (`travellers_gear.vest_fast_swimming`) |
| `aquatic_agility` | head | attribute entries | +3 oxygen bonus, submerged mining speed x5 (+4 multiply-total) while worn |

- **Anti-abuse** (`validateMovement`, dedicated servers only): each rejected double jump / sidestep packet
  increments a counter; a gap of >= 45 ticks resets it (unless elytra flying); count > 1 snaps the player back
  (position packet); count >= 5 disconnects with `multiplayer.disconnect.flying`.
- **Wing state** (`TravellersWingsAttachment`, server computed, synced with `TravellersWingsStatePacket`): DOUBLE_JUMP
  locked for 15 ticks, SIDESTEP locked for 11 ticks; otherwise RIDE (passenger), SWIM, FALL_SLOW (airborne, fall
  distance < 2.3, not flying), FALL_FAST (falling, fall distance > 2.3), SPRINT, WALK (walk speed > 0.1), IDLE.
  Client animation (`TravellersWingsModel`): right wing rotation = sin(phase)/swing + offset, phase advancing
  dt/divisor; per state (divisor, x offset, y, z, swing): ride (10, 30 deg, -0.6, -0.3, big 15), swim (17, 40 deg,
  -1.0, -0.5, big), fall slow (17, 50 deg, -1.1, -0.1, big), fall fast (2, 40 deg, -1.1, -0.3, small 8), sprint (2,
  30 deg, -0.3, 0, big), sidestep (2, 30 deg, -0.6 left / 0.4 right, 0, big; left wing -0.4 / 0.6), double jump fixed
  (-0.4, -0.8, -0.1) with faster interpolation, idle/walk (4 when walking else 20, 30 deg, -0.6, -0.3, big);
  rotations ease toward the target with time constant 4 ticks; wings drop 2 px while crouching; left wing mirrors.
- `leashed_pathfinder_override`: marks mobs (set by U21's lich tower fences) that should not stay close to their leash
  holder; removed on join if the mob can no longer be leashed.
- `last_damage_armor_time` is set whenever armor takes damage (used by U11's auto repair).

## 4. Bedrock adaptation notes

- These are client-authoritative movements on Java (README deviation): with the client mod, U12 implements them in
  `client::movement::onLocalTick(prio, fn(MoveState&))` (double jump, sidestep, glide, agile ranger, straight ahead,
  water walk, step height) and sends the TF packets as `tgmove.*` channels; the server validates with
  `movement::validate("double_jump", ...)` / `("sidestep", ...)` reproducing the 45-tick / 5-strike rules.
- Vanilla clients (best effort): `movement::onInputEdge` (jump pressed in air -> server `applyMotion` for the double
  jump; sneak-in-air for glide as Slow Falling), no step-up, no water walking, no agile ranger; sidestep via double-tap
  is not detectable -> unavailable. Document in not-ported.md.
- Attribute-based modifiers (swift swim, aquatic agility, high jump, step height) map to Bedrock components / effects
  where they exist (Jump Boost effect works on vanilla).
- Unrestrained: clear Bedrock's cobweb/powder snow slowdown server side where possible; client mod for exact parity.
- Wings animation is client-mod rendering (F2 attachable for the legs model + `client::render`); the state is synced
  over `tgmove.wings_state`.

## 5. Acceptance checklist (selftests `tgmove.*`, dx 1200-1299)

- Double jump once per airtime, safe-fall +2, slimy-soles boost added, abuse validator thresholds.
- Sidestep double-tap window: `tickCount - lastHorizontalWalkingTime` (the last tick the strafe key was held) < 4, so a
  difference of 3 (key released for 2 ticks) triggers and 4 does not; cooldown 40 ticks, 1.6 push, ready sound; gradual
  glide vy x5/6 with fall distance recomputed.
- High jump Jump Boost II; step height +0.5 off while crouching; straight ahead x1.4 forward only.
- Slimy soles bounce velocity sqrt(0.5) factor and fall cancel; unrestrained in cobweb; water walk threshold 0.4.
- Wing state transitions and lock durations (15 / 11 ticks).

## Appendix A: Owned ids (generated inventory)

Every id this unit owns in `docs/spec/owners.tsv`, with the TF 4.8 data the W1 worker must reproduce. Registration statements are TF's own Java (`init/TF*.java`), condensed (namespaces such as `MapColor.` dropped); loot, recipe, tag, advancement and data rows are summarised from TF's generated JSON (`src/generated/resources`). Loot notation: `self` = drops the block itself when it survives the explosion; `alternatives(a → b)` = first matching child; `[if ...]` = conditions; `{...}` = functions. In summaries `mc:` is `minecraft:` and ids without a namespace are `twilightforest:`.

| Kind | Count |
|---|---|
| `attachment` | 17 |
| `config_key` | 1 |
| `config_section` | 1 |
| `data_component` | 12 |
| `e2e` | 1 |
| `lang` | 2 |
| `net` | 5 |
| `particle` | 1 |
| `path` | 20 |
| `registry_entry` | 12 |
| `selftest` | 1 |
| `selftest_dx` | 1 |
| `worlddb` | 1 |

### Unit facts

| Kind | Ids |
|---|---|
| Selftest glob (`selftest`) | `tgmove.*` |
| Selftest dx range (`selftest_dx`) | `1200-1299` |
| World DB keys (`worlddb`) | `gf:tgmove:*` |
| E2E scenario (`e2e`) | `tgmove` |
| Net channels (`net`) | `tgmove.*`, `tgmove.double_jump`, `tgmove.gradual_glide`, `tgmove.sidestep`, `tgmove.wings_state` |
| Config section (`config_section`) | `tgmove` |
| Owned paths (`path`) | `**/*.d/tgmove.json`, `assets/data/*/tgmove.json`, `assets/gen/audio/tgmove/**`, `assets/gen/geo/tgmove/**`, `assets/gen/import/tgmove/**`, `assets/lang/*/tgmove.json`, `assets/pack/{bp,rp}/**/tgmove/**`, `assets/pack/rp/texts.d/*/tgmove.lang`, `assets/structures/tgmove/**`, `assets/tags/**/*.d/tgmove.json`, `docs/units/tgmove.md`, `src/client/tgmove/**`, `src/core/tgmove/**`, `src/feature/ext/tgmove/**`, `src/feature/tgmove/**`, `tests/tgmove/**`, `tools/e2e/*/scenarios/tgmove{,.*}.ps1`, `tools/import/overrides/tgmove.json`, `tools/sounds/recipes/tgmove.json`, `tools/structures/tgmove/**` |

### Particle types (1)

| Id | TF registration (`TFParticleType.java`) |
|---|---|
| `twilightforest:double_jump` | `PARTICLE_TYPES.register("double_jump", new SimpleParticleType(true));` |

### Data components (12)

| Id | TF registration (`TFDataComponents.java`) |
|---|---|
| `twilightforest:agile_ranger_modifier` | `COMPONENTS.register("agile_ranger_modifier", () -> DataComponentType.<Float>builder().persistent(ExtraCodecs.POSITIVE_FLOAT).networkSynchronized(ByteBufCodecs.FLOAT).cacheEncoding().build());` |
| `twilightforest:aquatic_agility` | `COMPONENTS.register("aquatic_agility", () -> DataComponentType.<Unit>builder().persistent(Codec.unit(Unit.INSTANCE)).networkSynchronized(StreamCodec.unit(Unit.INSTANCE)).cacheEncoding().build());` |
| `twilightforest:double_jump` | `COMPONENTS.register("double_jump", () -> DataComponentType.<Unit>builder().persistent(Codec.unit(Unit.INSTANCE)).networkSynchronized(StreamCodec.unit(Unit.INSTANCE)).cacheEncoding().build());` |
| `twilightforest:gradually_gliding_multiplier` | `COMPONENTS.register("gradually_gliding_multiplier", () -> DataComponentType.<Float>builder().persistent(ExtraCodecs.POSITIVE_FLOAT).networkSynchronized(ByteBufCodecs.FLOAT).cacheEncoding().build());` |
| `twilightforest:high_jump_amplifier` | `COMPONENTS.register("high_jump_amplifier", () -> DataComponentType.<Integer>builder().persistent(ExtraCodecs.UNSIGNED_BYTE).networkSynchronized(ByteBufCodecs.INT).cacheEncoding().build());` |
| `twilightforest:high_step` | `COMPONENTS.register("high_step", () -> DataComponentType.<Unit>builder().persistent(Codec.unit(Unit.INSTANCE)).networkSynchronized(StreamCodec.unit(Unit.INSTANCE)).cacheEncoding().build());` |
| `twilightforest:sidestep_cooldown` | `COMPONENTS.register("sidestep_cooldown", () -> DataComponentType.<Long>builder().persistent(Codec.LONG).networkSynchronized(ByteBufCodecs.VAR_LONG).cacheEncoding().build());` |
| `twilightforest:slimy_soles_coefficient` | `COMPONENTS.register("slimy_soles_coefficient", () -> DataComponentType.<Float>builder().persistent(ExtraCodecs.POSITIVE_FLOAT).networkSynchronized(ByteBufCodecs.FLOAT).cacheEncoding().build());` |
| `twilightforest:straight_ahead_multiplier` | `COMPONENTS.register("straight_ahead_multiplier", () -> DataComponentType.<Double>builder().persistent(Codec.DOUBLE).networkSynchronized(ByteBufCodecs.DOUBLE).cacheEncoding().build());` |
| `twilightforest:swift_swim` | `COMPONENTS.register("swift_swim", () -> DataComponentType.<Unit>builder().persistent(Codec.unit(Unit.INSTANCE)).networkSynchronized(StreamCodec.unit(Unit.INSTANCE)).cacheEncoding().build());` |
| `twilightforest:unrestrained` | `COMPONENTS.register("unrestrained", () -> DataComponentType.<Unit>builder().persistent(Codec.unit(Unit.INSTANCE)).networkSynchronized(StreamCodec.unit(Unit.INSTANCE)).cacheEncoding().build());` |
| `twilightforest:water_walk` | `COMPONENTS.register("water_walk", () -> DataComponentType.<Unit>builder().persistent(Codec.unit(Unit.INSTANCE)).networkSynchronized(StreamCodec.unit(Unit.INSTANCE)).cacheEncoding().build());` |

### Data attachments (17)

| Id | TF registration (`TFDataAttachments.java`) |
|---|---|
| `twilightforest:double_jump_validator` | `ATTACHMENT_TYPES.register("double_jump_validator", () -> AttachmentType.builder(() -> 0).serialize(Codec.INT).build());` |
| `twilightforest:double_jump_validator_last_check` | `ATTACHMENT_TYPES.register("double_jump_validator_last_check", () -> AttachmentType.builder(() -> 0).serialize(Codec.INT).build());` |
| `twilightforest:has_double_jump` | `ATTACHMENT_TYPES.register("has_double_jump", () -> AttachmentType.builder(() -> false).serialize(Codec.BOOL).build());` |
| `twilightforest:is_gradually_gliding` | `ATTACHMENT_TYPES.register("is_gradually_gliding", () -> AttachmentType.builder(() -> false).serialize(Codec.BOOL).sync(ByteBufCodecs.BOOL).build());` |
| `twilightforest:last_damage_armor_time` | `ATTACHMENT_TYPES.register("last_damage_armor_time", () -> AttachmentType.builder(() -> 0L).serialize(Codec.LONG).build());` |
| `twilightforest:last_horizontal_impulse` | `ATTACHMENT_TYPES.register("last_horizontal_impulse", () -> AttachmentType.builder(() -> 0F).serialize(Codec.FLOAT).build());` |
| `twilightforest:last_horizontal_walking_time` | `ATTACHMENT_TYPES.register("last_horizontal_walking_time", () -> AttachmentType.builder(() -> 0).serialize(Codec.INT).build());` |
| `twilightforest:last_jump_key_press_time` | `ATTACHMENT_TYPES.register("last_jump_key_press_time", () -> AttachmentType.builder(() -> 0).serialize(Codec.INT).build());` |
| `twilightforest:last_non_horizontal_impulse` | `ATTACHMENT_TYPES.register("last_non_horizontal_impulse", () -> AttachmentType.builder(() -> 0F).serialize(Codec.FLOAT).build());` |
| `twilightforest:last_tick_water_walking` | `ATTACHMENT_TYPES.register("last_tick_water_walking", () -> AttachmentType.builder(() -> 0L).serialize(Codec.LONG).build());` |
| `twilightforest:leashed_pathfinder_override` | `ATTACHMENT_TYPES.register("leashed_pathfinder_override", () -> AttachmentType.builder(() -> Unit.INSTANCE).serialize(Codec.unit(Unit.INSTANCE)).build());` |
| `twilightforest:sidestep_validator` | `ATTACHMENT_TYPES.register("sidestep_validator", () -> AttachmentType.builder(() -> 0).serialize(Codec.INT).build());` |
| `twilightforest:sidestep_validator_last_check` | `ATTACHMENT_TYPES.register("sidestep_validator_last_check", () -> AttachmentType.builder(() -> 0).serialize(Codec.INT).build());` |
| `twilightforest:slimy_soles_bounce_info` | `ATTACHMENT_TYPES.register("slimy_soles_bounce_info", () -> AttachmentType.builder(SlimySolesAttachment::new).serialize(SlimySolesAttachment.CODEC).build());` |
| `twilightforest:temporary_saved_straight_ahead` | `ATTACHMENT_TYPES.register("temporary_saved_straight_ahead", () -> AttachmentType.builder(() -> 1D).serialize(Codec.DOUBLE).build());` |
| `twilightforest:travellers_wings` | `ATTACHMENT_TYPES.register("travellers_wings", () -> AttachmentType.builder(TravellersWingsAttachment::new).build());` |
| `twilightforest:travellers_wings_anim` | `ATTACHMENT_TYPES.register("travellers_wings_anim", () -> AttachmentType.builder(TravellersWingsAnimAttachment::new).build());` |

### Registry entries (12)

- `travellers_modifiers/twilightforest:agile_ranger`: `{"type":"twilightforest:component","component":{"twilightforest:agile_ranger_modifier":5.0},"description":[{"translate":"travellers_gear.modifier.twilightforest.agile_ranger.description"}],"equipment_slots":"legs"}`
- `travellers_modifiers/twilightforest:aquatic_agility`: `{"type":"twilightforest:attribute","attribute_modifiers":[{"type":"minecraft:generic.oxygen_bonus","amount":3.0,"id":"twilightforest:travellers_gear.goggles_aquatic_agility_oxygen","operation":"add_value","slot":"head"},{"type":"minecraft:player.submerged_mining_speed","amount":4.0,"id":"twilightforest:travellers_gear.goggles_aquatic_agility_mining","operation":"add_multiplied_total","slot":"head"}],"builtin_modifier":false,"component":"twilightforest:aquatic_agility","description":[{"translate":"travellers_gear.modifier.twilightforest.aquatic_agility.description"}],"equipment_slots":"head"}`
- `travellers_modifiers/twilightforest:double_jump`: `{"type":"twilightforest:component","component":{"twilightforest:double_jump":{}},"description":[{"translate":"travellers_gear.modifier.twilightforest.double_jump.description"}],"equipment_slots":"legs"}`
- `travellers_modifiers/twilightforest:gradual_glide`: `{"type":"twilightforest:component","component":{"twilightforest:gradually_gliding_multiplier":0.8333333},"description":[{"translate":"travellers_gear.modifier.twilightforest.gradual_glide.description"}],"equipment_slots":"legs"}`
- `travellers_modifiers/twilightforest:high_jump`: `{"type":"twilightforest:builtin","component":"twilightforest:high_jump_amplifier","equipment_slots":"legs"}`
- `travellers_modifiers/twilightforest:side_step`: `{"type":"twilightforest:component","component":{"twilightforest:sidestep_cooldown":40},"description":[{"translate":"travellers_gear.modifier.twilightforest.side_step.description","with":[{"keybind":"key.left"},{"keybind":"key.right"}]}],"equipment_slots":"legs"}`
- `travellers_modifiers/twilightforest:slimy_soles`: `{"type":"twilightforest:component","component":{"twilightforest:slimy_soles_coefficient":0.5},"description":[{"translate":"travellers_gear.modifier.twilightforest.slimy_soles.description"}],"equipment_slots":"feet"}`
- `travellers_modifiers/twilightforest:step_up`: `{"type":"twilightforest:attribute","attribute_modifiers":[{"type":"minecraft:generic.step_height","amount":0.5,"id":"twilightforest:travellers_gear.boots_high_step","operation":"add_value","slot":"feet"}],"builtin_modifier":true,"component":"twilightforest:high_step","equipment_slots":"feet"}`
- `travellers_modifiers/twilightforest:straight_ahead`: `{"type":"twilightforest:component","component":{"twilightforest:straight_ahead_multiplier":1.4},"description":[{"translate":"travellers_gear.modifier.twilightforest.straight_ahead.description"}],"equipment_slots":"feet"}`
- `travellers_modifiers/twilightforest:swift_swim`: `{"type":"twilightforest:attribute","attribute_modifiers":[{"type":"minecraft:generic.water_movement_efficiency","amount":1.0,"id":"twilightforest:travellers_gear.vest_fast_swimming","operation":"add_value","slot":"chest"}],"builtin_modifier":true,"component":"twilightforest:swift_swim","equipment_slots":"chest"}`
- `travellers_modifiers/twilightforest:unrestrained`: `{"type":"twilightforest:component","component":{"twilightforest:unrestrained":{}},"description":[{"translate":"travellers_gear.modifier.twilightforest.unrestrained.description"}],"equipment_slots":"feet"}`
- `travellers_modifiers/twilightforest:water_walk`: `{"type":"twilightforest:component","component":{"twilightforest:water_walk":{}},"description":[{"translate":"travellers_gear.modifier.twilightforest.water_walk.description"}],"equipment_slots":"feet"}`

### Config keys (1)

See [config.md](config.md) for the section mapping; defaults and translation ids below are TF's.

| TF key | Default | Translation id | English (config screen) |
|---|---|---|---|
| `client.travellersWingsGradualGlide` | `true` | `manual_travellers_wings_gradual_glide` | Manual Gradually Gliding — When this option is off, slow falling is the default. Holding the sneak key makes you fall at normal speed. When this option is on, normal falling is the default. Holding the sneak key activates slow falling. |

## Appendix B: Strings (2 en_us keys owned)

Keys already shown above (block/item/entity names, subtitles) are not repeated. The 29 Bedrock locales are generated from TF's lang files (R9); owners use these keys with the English text as fallback.

| Key | English |
|---|---|
| `config.twilightforest.manual_travellers_wings_gradual_glide` | Manual Gradually Gliding |
| `config.twilightforest.manual_travellers_wings_gradual_glide.tooltip` | When this option is off, slow falling is the default. Holding the sneak key makes you fall at normal speed. When this option is on, normal falling is the default. Holding the sneak key activates slow falling. |

