# Knight stronghold (U25) on the client: the unit's selftests pass in the local world (integrated
# server), then every block, item, mob and the stronghold itself are put in front of the camera for
# the screenshot review against upstream's textures and models (%LOCALAPPDATA%\gf-upstream\view):
#   blocks              the trophy pedestal latent and active, the stronghold shield in two facings
#   pedestal_activate   a naga trophy placed on a pedestal between shields: active pedestal, shields gone
#   items               the four spawn eggs, the pedestal and the shield in the hotbar
#   mob_<name>          each mob alone at close range: knight phantom, goblin knight pair, block-and-
#                       chain goblin (swinging its spike block), helmet crab
#   goblin_spear        the goblin knight pair against a survival player (heavy spear: body pitch, arm swing)
#   phantom_fight       the six knights from the boss spawner circling their home, with the boss bar
#   phantom_charge      a knight phantom charging (scale 1.8, smoke)
#   phantom_throw       axe / pickaxe knights throwing their weapons (thrown_wep)
#   stronghold_outside  a knight stronghold placed with /gloamingforest place: the access chamber and the ruins
#   stronghold_inside   the entrance hall
# Modes: local runs the selftests on the integrated server first; remote-mod and remote-vanilla join
# the BDS host and take the same shots (everything here is vanilla-renderable: properties, render
# controllers, animations).
# Screenshots: out/stronghold/*.png
Save-Screenshot 'hud' | Out-Null
$remote = $script:Mode -ne 'local'
if ($remote) {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
    if ($script:Mode -eq 'remote-mod') { Wait-LogLine '\[host\].*companion joined' 60 | Out-Null }
} else {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'stronghold.*' -TimeoutSeconds 1200
    if ($r.Fail -gt 0) { throw "$($r.Fail) stronghold selftest(s) failed" }
    if ($r.Pass -lt 10) { throw "expected 10 stronghold selftests to pass, got $($r.Pass)" }
}
Invoke-Chat '/gamemode creative'
Invoke-Chat '/difficulty easy'
Invoke-Chat '/gamerule domobspawning false'
Invoke-Chat '/gamerule doDaylightCycle false'
Invoke-Chat '/time set noon'
Invoke-Chat '/effect @s night_vision 99999 1 true'
Invoke-Teleport '/tp @s 600 -60 600'
Start-Sleep -Seconds 2

# ---- blocks ----
Invoke-Chat '/fill 596 -60 600 612 -50 612 air'
Invoke-Chat '/fill 596 -61 600 612 -61 612 minecraft:smooth_stone'
Invoke-Chat '/setblock 600 -60 606 gloamingforest:trophy_pedestal'
Invoke-Chat '/setblock 602 -60 606 gloamingforest:trophy_pedestal ["gloamingforest:active"=true]'
Invoke-Chat '/setblock 604 -60 606 gloamingforest:stronghold_shield'
Invoke-Chat '/setblock 606 -60 606 gloamingforest:stronghold_shield ["gloamingforest:facing"="south"]'
Invoke-Teleport '/tp @s 603.5 -60 610.5 180 12'
Start-Sleep -Seconds 3
Save-Screenshot 'blocks' | Out-Null

# ---- the pedestal's activation: a trophy on top, the shields around it break ----
Invoke-Chat '/fill 596 -60 600 612 -50 612 air'
Invoke-Chat '/setblock 604 -60 606 gloamingforest:trophy_pedestal'
Invoke-Chat '/fill 602 -61 604 606 -61 608 gloamingforest:stronghold_shield'
Invoke-Chat '/setblock 604 -61 606 minecraft:smooth_stone'
Invoke-Teleport '/tp @s 604.5 -58 611 180 25'
Invoke-Chat '/setblock 604 -59 606 gloamingforest:naga_trophy'
Start-Sleep -Seconds 3
Save-Screenshot 'pedestal_activate' | Out-Null
Invoke-Chat '/fill 596 -61 600 612 -50 612 air'
Invoke-Chat '/fill 596 -61 600 612 -61 612 minecraft:smooth_stone'

# ---- items ----
Invoke-Chat '/clear @s'
foreach ($i in @('gloamingforest:knight_phantom_spawn_egg', 'gloamingforest:lower_goblin_knight_spawn_egg', 'gloamingforest:blockchain_goblin_spawn_egg',
        'gloamingforest:helmet_crab_spawn_egg', 'gloamingforest:trophy_pedestal', 'gloamingforest:stronghold_shield')) { Invoke-Chat "/give @s $i" }
Invoke-Teleport '/tp @s 606 -60 615 0 5'
Start-Sleep -Seconds 2
Save-Screenshot 'items' | Out-Null
Invoke-Chat '/clear @s'

# ---- each mob alone ----
$mobs = @(@('knight_phantom', '~ ~ ~5'), @('lower_goblin_knight', '~ ~ ~4'), @('blockchain_goblin', '~ ~ ~4'), @('helmet_crab', '~ ~ ~3'))
foreach ($m in $mobs) {
    Invoke-Teleport '/tp @s 610 -60 640 0 8'
    Invoke-Chat "/summon gloamingforest:$($m[0]) $($m[1])"
    Start-Sleep -Milliseconds 1500
    Save-Screenshot "mob_$($m[0])" | Out-Null
    Invoke-Chat "/kill @e[type=gloamingforest:$($m[0])]"
}
Invoke-Chat '/kill @e[type=gloamingforest:upper_goblin_knight]'

# ---- fights against a survival player ----
Invoke-Chat '/effect @s resistance 300 255 true'
Invoke-Teleport '/tp @s 610 -60 640 0 0'
Invoke-Chat '/summon gloamingforest:lower_goblin_knight ~ ~ ~3'
Invoke-Chat '/gamemode survival'
Start-Sleep -Seconds 4
Save-Screenshot 'goblin_spear' | Out-Null
Invoke-Chat '/gamemode creative'
Invoke-Chat '/kill @e[type=gloamingforest:lower_goblin_knight]'
Invoke-Chat '/kill @e[type=gloamingforest:upper_goblin_knight]'
Invoke-Chat '/setblock 610 -58 650 gloamingforest:knight_phantom_boss_spawner'
Invoke-Teleport '/tp @s 610 -58 646 0 0'
Invoke-Chat '/gamemode survival'
Start-Sleep -Seconds 5
Save-Screenshot 'phantom_fight' | Out-Null
Start-Sleep -Seconds 6
Save-Screenshot 'phantom_charge' | Out-Null
Start-Sleep -Seconds 6
Save-Screenshot 'phantom_throw' | Out-Null
Invoke-Chat '/gamemode creative'
Invoke-Chat '/kill @e[type=gloamingforest:knight_phantom]'
Invoke-Chat '/kill @e[type=gloamingforest:thrown_wep]'

# ---- a knight stronghold ----
Invoke-Teleport '/tp @s 900 -40 900'
Start-Sleep -Seconds 4
Invoke-Chat '/gloamingforest place gloamingforest:knight_stronghold 900 -50 900'
Start-Sleep -Seconds 12
Invoke-Teleport '/tp @s 908 -42 905 180 30'
Start-Sleep -Seconds 4
Save-Screenshot 'stronghold_outside' | Out-Null
Invoke-Teleport '/tp @s 907 -57 908 180 10'
Start-Sleep -Seconds 4
Save-Screenshot 'stronghold_inside' | Out-Null
Save-Screenshot 'selftest' | Out-Null
