# U14 progression: advancements, criteria engine, biome restrictions

Implementation notes of U14 (`src/feature/progression_content/`, `src/client/progression/`,
`src/core/progression/`). The behaviour spec is [docs/spec/progression.md](../spec/progression.md).

## Trigger payloads

Units fire TF's triggers by string through `gf::progression::trigger(player, id, data)` (R21).
The criteria engine reads these keys (an id may be a string or an object with `id` / `name` /
`item`; Java block-state suffixes such as `[facing=up]` are ignored):

| Trigger | Fired by | Data the engine reads |
|---|---|---|
| `twilightforest:make_tf_portal` | F4 | none |
| `twilightforest:hurt_boss` | F8 (boss fight participants, also the group advancement on death) | `entity` (`hurt_entity`, `boss`, `type`): the boss's entity id |
| `twilightforest:kill_bug` | U6 | `bug` or `block`: the bug's block id |
| `twilightforest:broke_glass_sword` | U8 | none |
| `twilightforest:drink_from_flask` | U9 | `doses` (int), `seconds` (int, since the first dose), `potion` (id, e.g. `minecraft:strong_harming`) |
| `twilightforest:consume_hydra_chop_on_low_hunger` | U10 | none |
| `twilightforest:add_modifier` | U11 | none |
| `twilightforest:uncraft_item` | U13 | `item` (or `stack`): the uncrafted item id (or `{id, count}`) |
| `twilightforest:complete_quest_ram` | U19 | none |
| `twilightforest:placed_on_trophy_pedestal`, `twilightforest:kill_all_phantoms` | U25 | none |
| `twilightforest:activate_ghast_trap` | U26 | none |
| `minecraft:item_used_on_block` | the item's owner, after a successful use-on (U29 for magic beans, U10 for redstone on experiment 115) | `pos` (`[x, y, z]` or `{x, y, z}`; the block there is read after the use), optionally `block` + `states` instead, and `item` (or `tool`, `stack`): the item used |
| `minecraft:recipe_crafted` | U14's extension `src/feature/ext/progression/RecipeCrafted.cpp` (crafting screen) | `recipe_id`: the BP recipe id; numbered layouts (`..._3`) count as the Java recipe |
| `minecraft:consume_item` | F10 | `item` |

`minecraft:inventory_changed` (the whole inventory, armor and offhand), `minecraft:player_killed_entity`
(the victim actor from `events::onKilled`) and the `location` / `tick` criteria (every second and
after every grant) come from F10's events.

## Item colour

`twilightforest:color` (arctic_armor_dyed) reads the stack's dye from Bedrock's `customColor` tag
or from the item data `dyed_color` / `minecraft:dyed_color` (an int RGB, or `{rgb}`), whichever the
armor's owner (U7) writes.

## Flask doses

TF resets the flask dose tracking when an advancement with a `drink_from_flask` criterion is
earned (`EntityEvents#resetFlaskLogic`). The tracking is U9's: it listens to
`progression::onGranted` for `twilightforest:full_mettle_alchemist` (the only such advancement).

## Screens and toasts

- `/twilightforest advancements` and the client mod's `L` key open `progression.advancements`:
  the native tree for companions, a form list (with a detail form per advancement) for vanilla
  clients.
- Earning an advancement: the experience reward, the chat announcement to every player, the toast
  (`progression.toast` to companions, the game's text toast for vanilla clients) and TF's
  `progression_end` message (Bedrock chat has no links: the URL follows the blue text).
- Locked biome toast: the client mod runs TF's static countdown; vanilla clients get the same
  countdown per player on the server and the game's toast.
