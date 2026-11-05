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
| `twilightforest:add_modifier` | U11 | `modifier`: the added modifier id (compared when the criterion names one; TF's own criterion takes any) |
| `twilightforest:uncraft_item` | U13 | `item` (or `stack`): the uncrafted item id (or `{id, count}`) |
| `twilightforest:complete_quest_ram` | U19 | none |
| `twilightforest:placed_on_trophy_pedestal`, `twilightforest:kill_all_phantoms` | U25 | none |
| `twilightforest:activate_ghast_trap` | U26 | none |
| `minecraft:item_used_on_block` | U14 itself for redstone on experiment 115 and magic beans on uberous soil (it watches both uses and fires the trigger the tick after a use that took effect); the item's owner may fire it too (harmless) | `pos` (`[x, y, z]` or `{x, y, z}`; the block there is read after the use), optionally `block` + `states` instead, and `item` (or `tool`, `stack`): the item used |
| `minecraft:recipe_crafted` | U14's extension `src/feature/ext/progression/RecipeCrafted.cpp` (crafting screen) | `recipe_id`: the BP recipe id; F1's numbered layouts of a split Java recipe count as it (`<id>_<n>` when the Java recipe `<id>` exists in F9's data, has no BP recipe of its own id, has a layout `<id>_0`, and `<id>_<n>` is no Java recipe itself) |
| `minecraft:consume_item` | F10 | `item` |
| `twilightforest:structure_cleared` | F6 (`landmark::setConquered`) | `structure`: the landmark key, compared with the criterion's required `structure` |

`minecraft:inventory_changed` (the whole inventory, armor and offhand), `minecraft:player_killed_entity`
(the victim actor from `events::onKilled`) and the `location` / `tick` criteria (every second and
after every grant) come from F10's events. For `player_killed_entity` the victim's vehicle and the
killing blow's direct entity are captured in F10's lethal pipeline (`death::onLethal`, lowest
priority) before `die()` dismounts the rider, and read back in `onKilled`: chicken_jerky needs the
baby zombie's chicken and a direct entity (the player, not a projectile) holding the jerky. A kill
F10 credits to a player whose lethal damage had no player in it (fire, a fall after the player's
hit) has no snapshot: as Java's damage source of that death, it has neither a direct nor a source
entity (those predicates fail) and `is_direct` is true (`null == null`).

## Tree layout and texts

The advancement positions are Minecraft's `TreeNodePosition` (Walker / Buchheim tidy tree) over
the whole tree, as `ServerAdvancementManager#apply` computes them on the Java server and sends
them to clients in `DisplayInfo` x / y (`core::progression::layoutTree`; gtested exactly against
Java's own output in `tests/progression/TreeLayoutGolden.inc`, produced by
`tests/progression/oracle/TreeNodePositionOracle.java` against the oracle's 1.21.1 server).
Hidden nodes keep their cells, as in Java. Java keeps an advancement's children in an identity
hash set, so its sibling order differs between runs; GloamingForest uses the file order.

Titles and descriptions are text components (`translate` + `with`); the server renders them in a
player's language for the form, chat and vanilla toasts, the client mod renders them in the game's
language. Icons carry their components (`twilightforest:e115_variant`, `flask_contents`,
`water_walk`) as item data under the component id.

## Item colour

`twilightforest:color` (arctic_armor_dyed) reads the stack's dye from Bedrock's `customColor` tag
or from the item data `dyed_color` / `minecraft:dyed_color` (an int RGB, or `{rgb}`), whichever the
armor's owner (U7) writes.

## Flask doses

TF resets the flask dose tracking when an advancement with a `drink_from_flask` criterion is
earned (`EntityEvents#resetFlaskLogic`, `PotionFlaskTrackingAttachment#resetDoses`). U14 does it on
every such grant (TF's only one is `full_mettle_alchemist`):

- the player's entity data under TF's attachment id `twilightforest:flask_doses`
  (`gf::entity::data(player, "twilightforest:flask_doses")`), when it is an object: `doses_drank`
  set to 0 and `last_used_potion` removed (the attachment codec's field names;
  `last_game_time_started` is kept, as `resetDoses` keeps `lastTimeStarted`);
- the trigger `twilightforest:flask_doses_reset` (`{"advancement": id}`) for an owner that keeps
  the tracking elsewhere.

The tracking itself (consecutive doses of one potion, `drink_from_flask`) is U9's: U9 should keep
it in that entity data document (selftest `progression.flask_reset`).

## Screens and toasts

- `/twilightforest advancements` and the client mod's `L` key open `progression.advancements`:
  the native tree for companions, a form list (with a detail form per advancement) for vanilla
  clients.
- Earning an advancement: the experience reward, the chat announcement to every player (the title
  in `AdvancementType#getChatColor`: green, dark purple for challenges), the toast
  (`progression.toast` to companions, the game's text toast for vanilla clients, its first line
  `§e` yellow, `§d` pink for challenges, as AdvancementToast's -256 / -30465) and TF's
  `progression_end` message (Bedrock chat has no links: the URL follows the blue text).
- The client mod draws its toasts itself (`src/client/progression/Toasts.cpp`), as Java's
  ToastComponent: AdvancementToast at Java's offsets (icon with its variant at 8,8, text at 30,7
  and 30,18; a title wider than 125 pixels: the toast name at 30,11 for 1.5 s, then the title's
  lines centred, fading in) and TF's LockedBiomeToast (icon at 6,8, text at 25,7 and 25,18), 5
  slots, 600 ms slides, over the HUD and over the advancement screen. F12's toast queue only has
  TF's layout; the slots it holds (other units' toasts) are left free.
- Locked biome toast: the client mod runs TF's static countdown; vanilla clients get the same
  countdown per player on the server and the game's toast.
- A challenge's sound: Java's `ui.toast.challenge_complete` is Mojang's (All Rights Reserved) and has
  no Bedrock counterpart, and F3's synth renders only sound events with an owners.tsv row. U14 plays
  an original fanfare of the game's note block sounds instead
  (`core::progression::challengeFanfare`: a C major arpeggio and chord, 1.2 s): the client mod when
  its challenge toast first shows (as AdvancementToast), the server to vanilla clients with their
  toast.
- The window header is the selected tab's title at (8, 6) in 0x404040
  (`AdvancementsScreen#renderWindow`; "Advancements" without a tab).
- The native tree pans by dragging with the left button and with the wheel (Shift: sideways); the
  hover dims the tree, splits the title bar by progress, and puts the description above the node
  near the bottom and the box to the left near the screen's right edge, as AdvancementWidget.

## e2e demos

`progression.demo_*` and the client toast tests act only on real players carrying the scoreboard
tag `gf_e2e_progression` (the client scenario adds and removes it). The first demo saves the
player's exact progress and `progression.demo_reset` restores it quietly (no toast, chat line,
experience or message for the re-granted advancements: `setQuietGrants`), so a `full` or glob run
never changes a real player's progression. Other units' `progression::onGranted` listeners still
see the re-grants.
