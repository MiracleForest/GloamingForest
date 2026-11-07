# U20 `naga`: Naga courtyard and Naga

Behaviour reference: [docs/spec/naga.md](../spec/naga.md) and TF 4.8 (`9e40a757`).

## Audit round 1

Checked (A3, landmarks and bosses I):

- The fight end to end (`naga.death`): the spawner's long range, the segments popping one by one, the 124-tick trail
  to the twilight oak / canopy death chest with the scales and the trophy, `player_killed_entity` / `hurt_boss` /
  `inventory_changed` and `progress_naga`'s criteria. Added: the conquest of the home landmark (`BaseTFBoss#postmortem`
  marks `twilightforest:naga_courtyard` conquered at the home point).
- The courtyard against TF's `NagaCourtyardStructure` record (`naga.landmark_walk`): no progression lock, no hint book,
  conquerable, no protected piece (TF's `ConquerableStructure` has no `AdvancementLockConfig`).

Fixed: nothing in the unit's code needed a change.

Open: none found in this round.
