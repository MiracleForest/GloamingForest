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
earned (`EntityEvents#resetFlaskLogic`, `PotionFlaskTrackingAttachment#resetDoses`: `dosesDrank`
0, `lastUsedPotion` cleared, `lastTimeStarted` kept). TF's only such advancement is
`full_mettle_alchemist`.

The tracking (the attachment `twilightforest:flask_doses`) and `drink_from_flask` are U9's in
owners.tsv, so U14 does not write the tracking (R2). On every such grant it fires, on F10's
trigger bus:

    twilightforest:flask_doses_reset   {"advancement": "<the advancement id>"}

**Hand-off for U9 (checklist item):** register
`gf::progression::onTrigger("twilightforest:flask_doses_reset", fn)` and in `fn` do
`resetDoses` on the player's tracking (doses to 0, the last potion cleared, the start time kept).
F0 should add the row `trigger twilightforest:flask_doses_reset U14 manual` (fired by U14,
consumed by U9); A8 checks the pair when it folds W1. Selftest `progression.flask_reset` checks
that the trigger fires for `full_mettle_alchemist` only and that U14 leaves the tracking alone.
## Screens and toasts

- `/twilightforest advancements` and the client mod's `L` key open `progression.advancements`:
  the native tree for companions, a form list (with a detail form per advancement) for vanilla
  clients.
- Java's `announceAdvancements` gamerule is GloamingForest's bool gamerule `announceAdvancements`
  (default true, `/twilightforest gamerule announceAdvancements false`; Bedrock's `/gamerule` has no
  such rule): while false nobody gets the chat line, the toast and rewards stay (selftest
  `progression.announce_gamerule`). Its owners.tsv row (`gamerule announceAdvancements U14`) is F0's
  to add.
- Earning an advancement: the experience reward, the chat announcement to every player (the title
  in `AdvancementType#getChatColor`: green, dark purple for challenges), the toast
  (`progression.toast` to companions, the game's text toast for vanilla clients, its first line
  `§e` yellow, `§d` pink for challenges, as AdvancementToast's -256 / -30465) and TF's
  `progression_end` message (Bedrock chat has no links: the URL follows the blue text).
- The client mod draws its toasts itself (`src/client/progression/Toasts.cpp`), as Java's
  ToastComponent: AdvancementToast at Java's offsets (icon with its variant at 8,8, text at 30,7
  and 30,18; a title wider than 125 pixels: the toast name at 30,11 for 1.5 s, then the title's
  lines centred, fading in) and TF's LockedBiomeToast (icon at 6,8, text at 25,7 and 25,18), 5
  slots, 600 ms slides, over the HUD and over the advancement screen (exactly one of the two draws
  them in a frame, decided by draw order: the screen tells the toasts when it opens and closes, and
  while it is open and drawing the HUD layer leaves them to it; leaving a level, ClientExitLevelEvent,
  drops them, as F12's queue). F12's toast queue only has TF's layout; the
  slots it holds (other units' toasts) when a U14 toast takes a slot are left free, but an F12 toast
  that arrives later takes slot 0 even under a U14 toast: F12's slot table is internal to
  `src/client/ui/Ui.cpp`, so one shared table needs an F12 API (slot reservation, or an icon-stack
  field and the advancement layout in `toast::show`), routed to A8/F12.
- Locked biome toast: the client mod runs TF's static countdown; vanilla clients get the same
  countdown per player on the server and the game's toast.
- A challenge's sound (Challenge sound): Java's `ui.toast.challenge_complete` is Mojang's (All Rights
  Reserved) and has no Bedrock counterpart. U14 plays its own original synthesized sound event
  `gf.progression.challenge_complete` (`core::progression::kChallengeSound`, category `ui`): the
  client mod with `playUI` when its challenge toast first shows (as AdvancementToast), the server
  with `fx::soundTo` to vanilla clients with their toast. It is defined in
  `assets/pack/rp/sounds.d/progression.json` on `assets/pack/rp/sounds/progression/challenge_complete.ogg`,
  rendered by the synth toolkit's renderer (`src/core/synth`) from the track in
  `tools/sounds/recipes/progression.json` (a glockenspiel arpeggio resolving on a bell chord, 2.2 s).
  F3's `synth.ps1` renders only events with an owners.tsv `sound_event` row, which U14 has none of,
  so the file is rendered with the same renderer directly (the gtest
  `ProgressionChallengeSound.PackDefinesTheOriginalSound` re-renders the recipe and checks the
  committed file's format and length; selftest `progression.challenge_sound` checks the assembled
  pack):

      pwsh tools/sounds/synth.ps1 -Check   # compiles the renderer into %LOCALAPPDATA%\gf-synth\<hash>\gf-synth.exe
      # job file: {"threads": 1, "library": {<tools/sounds/patches/**.json by name>}, "jobs": [{"name":
      #   "progression/ui/challenge_complete", "kind": "track", "track": <the recipe's track>,
      #   "rate": 44100, "quality": 0.4, "out": "assets/pack/rp/sounds/progression/challenge_complete.ogg"}]}
      & $env:LOCALAPPDATA\gf-synth\<hash>\gf-synth.exe render <job file>

  F0/A8 may add the row `sound_event gf.progression.challenge_complete U14` so that `synth.ps1 -Unit
  progression` renders it like every other owner's.
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
