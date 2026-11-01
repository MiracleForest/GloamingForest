# F9 (item) on the client, spike S6's evidence. item.spike_client gives the player the S6 carriers:
# slot 1 the fixture item with a custom enchantment and a tooltip (lore + glint marker), slot 2 an
# iron sword whose only enchantment is custom (glint without a vanilla line), slot 3 a banner with
# a pattern id no client knows, and the fixture carrier effect (hidden Slowness II). Screenshots:
# out/item/{hud-carriers,slot1-fixture,slot2-sword,slot3-banner,inventory,tooltip-fixture}.png.
# Local mode also runs every item selftest in the integrated server; remote modes run them on the
# BDS host and check that a vanilla client stays connected with the carriers in its inventory.
if ($script:Mode -eq 'local') {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'item.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) item selftest(s) failed in the local world" }
    if ($r.Pass -lt 20) { throw "expected at least 20 item selftests, got $($r.Pass)" }
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
}

# the carriers for the real player
$mark = Get-LogMark
$r = Invoke-SelfTest 'item.spike_client'
if ($r.Fail -gt 0) { throw 'item.spike_client failed' }
Wait-LogLine 'S6 carriers given to' 30 -Since $mark | Out-Null
Start-Sleep -Seconds 12  # the inventory and the lore refresh reach the client; the chat lines fade

Save-Screenshot 'hud-carriers' | Out-Null
foreach ($slot in 1, 2, 3) {
    Press-Key "$slot"
    Start-Sleep -Milliseconds 700
    $name = @{ 1 = 'slot1-fixture'; 2 = 'slot2-sword'; 3 = 'slot3-banner' }[$slot]
    Save-Screenshot $name | Out-Null
}
Press-Key '1'

Open-Inventory
Save-Screenshot 'inventory' | Out-Null
# the hotbar row of the inventory screen (slots 34 px apart at 1280 x 720): hover for the hover text
Move-Mouse 0.506 0.729 -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'tooltip-fixture' | Out-Null
Move-Mouse 0.540 0.729 -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'tooltip-sword' | Out-Null
Move-Mouse 0.574 0.729 -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'tooltip-banner' | Out-Null
Close-Screen

if ($script:Mode -ne 'local') {
    Start-Sleep -Seconds 5
    if (-not (Get-GameProcess)) { throw 'the game closed with the S6 carriers in its inventory' }
    if (Get-LeviLog | Select-String -Pattern 'disconnect|Disconnected' -Quiet) { throw 'the client was disconnected with the S6 carriers' }
    $r = Invoke-SelfTest 'item.tags_fixture'
    if ($r.Fail -gt 0) { throw 'the host stopped answering after the carriers' }
}
