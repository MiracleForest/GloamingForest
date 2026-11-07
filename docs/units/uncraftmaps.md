# U13 `uncraftmaps`: uncrafting table and maps

Behaviour spec: [docs/spec/uncraftmaps.md](../spec/uncraftmaps.md). Code: `src/feature/uncraftmaps/`
(server: table, menu, form flow, maps, commands, config), `src/client/uncraftmaps/` (the native
screen, the map icons), `src/core/uncraftmaps/` (pure maths, gtested); selftests `uncraftmaps.*`
(dx 1300..1399). U13 was not audited in round 1; the W1 PR (#56) lists the original deviations.

## Audit round 2

Checked against TF 4.8 (`9e40a757`): `UncraftingMenu` (slotsChanged, the combined grid,
isValidMatchForInput, the costs), `UncraftingResultSlot#onTake` (the combined take, the marked
remainders), `UncraftingTableBlock`, `UncraftingTableCondition`, `EmptyMagicMapItem`,
`EmptyMazeMapItem`, `MazeMapItem#update` (view radius, the stone search, the ore recolouring),
`MagicMapItem` / `MazeMapItem#appendHoverText`, the commands' spec (3.6) against `Commands.cpp`.
The menu, the map updates and the commands are faithful translations; the items below are what
changed or stays open.

Fixed:

- **The uncrafting table's recipe ignored `disableUncraftingTable`.** TF loads the recipe only under
  `twilightforest:uncrafting_table_enabled` (`UncraftingTableCondition`); Bedrock loads the imported BP
  recipe whatever the config, so a disabled table could still be crafted. A special crafting gate
  (`twilightforest:uncrafting_table_enabled_gate`, F9's `registerSpecialCrafting` over the BP layout
  `twilightforest:uncrafting_table`) now refuses the craft while the table is disabled and gives the
  recipe's own result otherwise. Selftest `uncraftmaps.table_recipe_gate`. Limit: F9's crafting hook
  refuses only a craft whose request names the BP recipe (every crafting screen does); a request
  whose recipe net id is missing or names another result falls back to the result-only search, where
  a matcher that says no is skipped and the vanilla craft goes ahead (routed to F9).
- **Vanilla players could not repair.** The form flow only uncrafted. A recipe page now offers
  **Repair** when the input has broken (marked) parts and the inventory holds those parts: they go into
  the assembly grid over the ghost recipe, exactly as a TF player fills the broken cells on the
  screen (only plain stacks of the ghost's item and data, only for a damaged input, and not when the
  filled cells alone form the recipe: TF then crafts normally and keeps the input), and the result slot is taken with the menu's own rules (TF's combined grid, the same-kind
  check, the enchantment transfer, the recrafting cost and the levels; the input is used up). The page
  shows the result and its level cost first. Selftest `uncraftmaps.form_repair` (no parts: refused;
  too few levels: refused, the parts come back; then the new enchanted pickaxe, 2 ingots used, the
  levels charged).

Verified, nothing to change:

- The maze / ore map update keeps TF's colour of the block at `yCenter` when the stone search moves the
  sampled state (Java adds the map colour before the search and only the ore recolouring reads the
  found state); radius 16, 8 under a ceiling; ore colours win with weight 1000.
- The combined take puts the marked uncrafting stacks of the cells the player left empty back into the
  assembly grid (TF's `tempRemainderMap`), so closing the screen hands them out; ported as is (TF's
  behaviour). GloamingForest's stacks carry no `TwilightForestMarker`, so they stack with plain items
  (Java's marked copies do not): a cosmetic difference.

Still open (honest):

- **Recipe book**: no recipe book button, `UncraftingRecipeBookComponent` or `UncrafterPlaceRecipe`
  (Java's recipe book has no Bedrock counterpart). Not-ported candidate.
- **Native screen input**: left / right pickup, shift quick-move and middle-click clone only; no hotbar
  number keys, Q (throw), double-click collect or drag splitting.
- **Vanilla form**: repairing with the shown recipe only; free recrafting into another recipe of the
  same kind (TF: any recipe the assembly grid forms) needs the client mod's screen.
- **`ResultSlot#checkTakeAchievements`**: the crafted stat and the recipe unlocks are not awarded on a
  recraft (Bedrock's crafted statistics are not reachable from a mod).
- **Filled TF maps** are `minecraft:filled_map` stacks (Bedrock draws only its own map item): vanilla
  inventory icon, no "Id #" / Y-level tooltip (Bedrock's map ids are 64-bit unique ids, not Java's
  small counters, and advanced tooltips do not exist server side).
- **Landmark icons for vanilla clients**: the stand-in vanilla decorations did not show in the W1
  remote-vanilla screenshot; not re-checked this round (no client run).
- **map_locator**: Bedrock's white X stands in for Java's `red_x`.
- **biomepng**: outside the Twilight Forest it reads only loaded chunks; the PNG is uncompressed.
