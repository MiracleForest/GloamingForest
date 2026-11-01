# F9 (item) on the client, spike S6's evidence and the real-path checks.
#
# 1. item.spike_client gives the player the S6 carriers: hotbar 1 the fixture item with a custom
#    enchantment and a tooltip (lore + glint marker), 2 an iron sword whose only enchantment is
#    custom, 3 a banner with a pattern id no client knows (local mode: appended to the client's
#    registry first), 4 / 5 fixture items whose bars come from their item data (1 and 3 doses of 4),
#    6 armour trimmed with a material appended to the server's trim registry, 7 the same armour with
#    a vanilla trim; and the fixture carrier effect (hidden Slowness II). Screenshots:
#    out/item/{hud-carriers,slot1..7-*,inventory,tooltip-*}.png. item.spike_client_clear takes it back.
# 2. Special crafting through a real crafting table (gf_test:fixture_item: two sticks in a column,
#    special recipe of item.real_craft_arm), checked by item.real_craft_check.
# 3. An anvil merge of a sword and a book with the fixture enchantment at I (item.real_anvil_*).
# Local mode also runs every item selftest in the integrated server; remote modes run them on the
# BDS host and check that a vanilla client stays connected with the carriers in its inventory.
$script:IsLocal = $script:Mode -eq 'local'
if ($script:IsLocal) {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'item.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) item selftest(s) failed in the local world: $($r.Failed -join ', ')" }
    if ($r.Pass -lt 30) { throw "expected at least 30 item selftests, got $($r.Pass)" }
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
}
Wait-HudReady

# hover a slot before clicking it: the screen only takes a click on the slot under the mouse
function Click-Slot([double]$x, [double]$y, [switch]$Right) {
    Move-Mouse $x $y -Relative
    Start-Sleep -Milliseconds 300
    if ($Right) { Click $x $y -Relative -Right } else { Click $x $y -Relative }
    Start-Sleep -Milliseconds 400
}
# the hotbar row of a container screen (1600 x 900 client area)
$hotbarY = 0.729
function Hotbar-X([int]$slot) { 0.5055 + ($slot - 1) * 0.0343 }

# ---- 1. the S6 carriers -------------------------------------------------------------------------------
if ($script:IsLocal) {
    $r = Invoke-SelfTest 'item.spike_banner_append'
    if ($r.Fail -gt 0) { throw 'item.spike_banner_append failed' }
}
$mark = Get-LogMark
$r = Invoke-SelfTest 'item.spike_client'
if ($r.Fail -gt 0) { throw 'item.spike_client failed' }
Wait-LogLine 'S6 carriers given to' 30 -Since $mark | Out-Null
Start-Sleep -Seconds 12  # the inventory and the lore refresh reach the client; the chat lines fade
Wait-HudReady

Save-Screenshot 'hud-carriers' | Out-Null
$names = @{ 1 = 'slot1-fixture'; 2 = 'slot2-sword'; 3 = 'slot3-banner'; 4 = 'slot4-bar-one-dose'; 5 = 'slot5-bar-three-doses'; 6 = 'slot6-trim-probe'; 7 = 'slot7-trim-emerald' }
foreach ($slot in 1..7) {
    Press-Key "$slot"
    Start-Sleep -Milliseconds 700
    Save-Screenshot $names[$slot] | Out-Null
}
Press-Key '1'

Open-Inventory
Save-Screenshot 'inventory' | Out-Null
foreach ($t in @(@(1, 'tooltip-fixture'), @(2, 'tooltip-sword'), @(3, 'tooltip-banner'), @(6, 'tooltip-trim-probe'), @(7, 'tooltip-trim-emerald'))) {
    Move-Mouse (Hotbar-X $t[0]) $hotbarY -Relative
    Start-Sleep -Milliseconds 800
    Save-Screenshot $t[1] | Out-Null
}
Close-Screen

if (-not $script:IsLocal) {
    Start-Sleep -Seconds 5
    if (-not (Get-GameProcess)) { throw 'the game closed with the S6 carriers in its inventory' }
    if (Get-LeviLog | Select-String -Pattern 'disconnect|Disconnected' -Quiet) { throw 'the client was disconnected with the S6 carriers' }
}
$r = Invoke-SelfTest 'item.spike_client_clear'
if ($r.Fail -gt 0) { throw 'item.spike_client_clear failed' }

# ---- 2. special crafting through a crafting table ------------------------------------------------------
$mark = Get-LogMark
$r = Invoke-SelfTest 'item.real_craft_arm'
if ($r.Fail -gt 0) { throw 'item.real_craft_arm failed' }
Wait-LogLine 'real craft armed for' 30 -Since $mark | Out-Null
Invoke-Chat '/tp @s ~ ~ ~ -90 20'
Invoke-Chat '/setblock ~3 ~ ~ crafting_table'
Start-Sleep -Milliseconds 800
Click 0.5 0.5 -Relative -Right
Start-Sleep -Milliseconds 1500
Save-Screenshot 'craft-table' | Out-Null
# hotbar 1 -> the grid's top middle, hotbar 2 -> its centre (the BP recipe: two sticks in a column)
Click-Slot (Hotbar-X 1) $hotbarY; Click-Slot 0.5805 0.305
Click-Slot (Hotbar-X 2) $hotbarY; Click-Slot 0.5805 0.365
Start-Sleep -Milliseconds 400
Save-Screenshot 'craft-grid' | Out-Null
# the result goes onto the cursor and into hotbar 3: the craft request is sent when it is placed
$mark = Get-LogMark
Click-Slot 0.730 0.365
Click-Slot (Hotbar-X 3) $hotbarY
Wait-LogLine 'recipes: special crafting gf_test:fixture_item for' 15 -Since $mark | Out-Null
Wait-LogLine 'recipes: special crafting remainder placed' 15 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'craft-taken' | Out-Null
Move-Mouse (Hotbar-X 3) $hotbarY -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'craft-result-tooltip' | Out-Null
Close-Screen
$mark = Get-LogMark
$r = Invoke-SelfTest 'item.real_craft_check'
if ($r.Fail -gt 0) { throw "the real special craft failed: $($r.Failed -join ', ')" }
Wait-LogLine 'real craft checked for .*: 2 special items, 1 remainder' 15 -Since $mark | Out-Null
Invoke-Chat '/setblock ~3 ~ ~ air'

# ---- 3. an anvil merge ---------------------------------------------------------------------------------
$mark = Get-LogMark
$r = Invoke-SelfTest 'item.real_anvil_arm'
if ($r.Fail -gt 0) { throw 'item.real_anvil_arm failed' }
Wait-LogLine 'real anvil armed for' 30 -Since $mark | Out-Null
Invoke-Chat '/setblock ~3 ~ ~ anvil'
Start-Sleep -Milliseconds 800
Click 0.5 0.5 -Relative -Right
Start-Sleep -Milliseconds 1500
Save-Screenshot 'anvil-screen' | Out-Null
# the sword (hotbar 1) into the first input, the book (hotbar 2) into the second: Java's AnvilScreen
# slots (27, 47), (76, 47), result (134, 47) in the panel the crafting table also uses
Click-Slot (Hotbar-X 1) $hotbarY; Click-Slot 0.5398 0.4022
Click-Slot (Hotbar-X 2) $hotbarY; Click-Slot 0.6333 0.4022
Start-Sleep -Milliseconds 600
Save-Screenshot 'anvil-inputs' | Out-Null
Click-Slot 0.7438 0.4022
Click-Slot (Hotbar-X 3) $hotbarY
Start-Sleep -Milliseconds 800
Save-Screenshot 'anvil-taken' | Out-Null
Close-Screen
$mark = Get-LogMark
$r = Invoke-SelfTest 'item.real_anvil_check'
if ($r.Fail -gt 0) { throw "the real anvil merge failed: $($r.Failed -join ', ')" }
$line = Wait-LogLine 'real anvil checked for .*: fixture_react \d' 15 -Since $mark
Write-Step "anvil: $line"
if ($line -notmatch 'fixture_react 2') { throw "the real anvil merge did not give fixture_react II: $line" }
Invoke-Chat '/setblock ~3 ~ ~ air'

if (-not $script:IsLocal) {
    $r = Invoke-SelfTest 'item.tags_fixture'
    if ($r.Fail -gt 0) { throw 'the host stopped answering after the carriers' }
}
