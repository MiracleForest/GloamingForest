# U24 `hydra`: Hydra lair, Hydra and fire swamp

Behaviour reference: [docs/spec/hydra.md](../spec/hydra.md) and TF 4.8 (`9e40a757`).

## Audit round 1

Checked (A3, landmarks and bosses I):

- The fight and the lair (`hydra.conquest`): the lair forced in a flat area, its hydra boss spawner, the Hydra woken
  with a home at the spawner, its death conquering the lair, the mangrove death chest at the home point. Added: the
  chest's contents (the hydra trophy, hydra chops and fiery blood of `entities/hydra`).
- The lair against TF's `HydraLairStructure` record (`hydra.landmark_walk`): locked by `progress_labyrinth`, the
  `hydralair` hint book, conquerable; its protected pieces refuse a survival player without the advancement, let him in
  with it, and never refuse a creative player.

Fixed: nothing in the unit's code needed a change.

Open: none found in this round.
