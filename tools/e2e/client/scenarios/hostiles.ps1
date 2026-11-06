# Forest and hollow-hill hostiles (U17) on the client: the unit's selftests pass in the local world
# (integrated server), then each mob and projectile is put in front of the camera for the screenshot
# review against TF's textures and models (%LOCALAPPDATA%\gf-tf):
#   eggs                the 13 spawn eggs in the hotbar (TF's colours)
#   mob_<name>          each mob alone at close range, in daylight
#   mist_wolf_dark      the mist wolf in a dark stone room (MistWolfRenderer: translucent and dim)
#   hostile_wolf_angry  a hostile wolf with a target (angry face, raised tail) against a survival player
#   slime_beetle_tail   the slime beetle from the side (the translucent slime tail)
#   pinch_beetle_open   a pinch beetle holding the survival player (open pincers)
#   winter_wolf_breath, fire_beetle_breath   the breath particles (snow, flames) at a survival player
#   druid_bolt, slime_blob   a druid's nature bolt and a slime beetle's blob in flight
#   king_spider_rider   the king spider with its skeleton druid
#   wraith              the wraith (alpha 0.6) in front of a stone wall
# Modes: local runs the selftests on the integrated server first; remote-mod and remote-vanilla join
# the BDS host and take the same shots (everything here is resource pack and server side: a vanilla
# client sees the same).
# Screenshots: out/hostiles/*.png
Save-Screenshot 'hud' | Out-Null
$remote = $script:Mode -ne 'local'
if ($remote) {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
    if ($script:Mode -eq 'remote-mod') { Wait-LogLine '\[host\].*companion joined' 60 | Out-Null }
} else {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'hostiles.*' -TimeoutSeconds 900
    if ($r.Fail -gt 0) { throw "$($r.Fail) hostiles selftest(s) failed" }
    if ($r.Pass -lt 12) { throw "expected 12 hostiles selftests to pass, got $($r.Pass)" }
}

Invoke-Chat '/gamemode creative'
Invoke-Chat '/difficulty easy'
Invoke-Chat '/gamerule dofiretick false'
Invoke-Chat '/gamerule domobspawning false'
Invoke-Chat '/gamerule mobgriefing false'
Invoke-Chat '/gamerule doDaylightCycle false'
Invoke-Chat '/time set noon'
Invoke-Teleport '/tp @s 1700 -60 700'
Start-Sleep -Seconds 2

# ---- the spawn eggs in the hotbar ----
Invoke-Chat '/clear @s'
$eggs = @('hostile_wolf', 'mist_wolf', 'winter_wolf', 'king_spider', 'hedge_spider', 'swarm_spider', 'skeleton_druid', 'redcap', 'redcap_sapper')
foreach ($e in $eggs) { Invoke-Chat "/give @s twilightforest:$($e)_spawn_egg" }
Invoke-Teleport '/tp @s 1700 -60 700 0 5'
Start-Sleep -Seconds 2
Save-Screenshot 'eggs_1' | Out-Null
Invoke-Chat '/clear @s'
foreach ($e in @('wraith', 'fire_beetle', 'slime_beetle', 'pinch_beetle')) { Invoke-Chat "/give @s twilightforest:$($e)_spawn_egg" }
Start-Sleep -Seconds 1
Save-Screenshot 'eggs_2' | Out-Null
Invoke-Chat '/clear @s'

# ---- each mob alone (creative: nothing targets the player), summoned south of the camera ----
$mobs = @(@('hostile_wolf', '~ ~ ~3'), @('mist_wolf', '~ ~ ~5'), @('winter_wolf', '~ ~ ~5'), @('hedge_spider', '~ ~ ~4'),
    @('swarm_spider', '~ ~ ~2'), @('skeleton_druid', '~ ~ ~4'), @('redcap', '~ ~ ~3'), @('redcap_sapper', '~ ~ ~3'),
    @('fire_beetle', '~ ~ ~3'), @('slime_beetle', '~ ~ ~3'), @('pinch_beetle', '~ ~ ~3'))
foreach ($m in $mobs) {
    Invoke-Teleport '/tp @s 1710 -60 740 0 10'
    Invoke-Chat "/summon twilightforest:$($m[0]) $($m[1])"
    Start-Sleep -Milliseconds 1500
    Save-Screenshot "mob_$($m[0])" | Out-Null
    Invoke-Chat "/kill @e[type=twilightforest:$($m[0])]"
}

# the king spider and its druid rider
Invoke-Teleport '/tp @s 1710 -60 740 0 0'
Invoke-Chat '/summon twilightforest:king_spider ~ ~ ~7'
Start-Sleep -Seconds 2
Save-Screenshot 'king_spider_rider' | Out-Null
Invoke-Chat '/kill @e[type=twilightforest:king_spider]'
Invoke-Chat '/kill @e[type=twilightforest:skeleton_druid]'

# the slime beetle from the side: the translucent tail
Invoke-Teleport '/tp @s 1710 -60 740 0 20'
Invoke-Chat '/summon twilightforest:slime_beetle ~2 ~ ~2 90'
Start-Sleep -Seconds 2
Save-Screenshot 'slime_beetle_tail' | Out-Null
Invoke-Chat '/kill @e[type=twilightforest:slime_beetle]'

# the wraith in front of a stone wall
Invoke-Chat '/fill 1706 -60 748 1714 -55 748 stone'
Invoke-Teleport '/tp @s 1710 -60 740 0 0'
Invoke-Chat '/summon twilightforest:wraith ~ ~ ~4'
Start-Sleep -Seconds 2
Save-Screenshot 'wraith' | Out-Null
Invoke-Chat '/kill @e[type=twilightforest:wraith]'
Invoke-Chat '/fill 1706 -60 748 1714 -55 748 air'

# ---- the mist wolf in the dark (a closed stone room, the camera inside) ----
Invoke-Chat '/fill 1720 -61 760 1730 -55 770 stone hollow'
Invoke-Chat '/summon twilightforest:mist_wolf 1725 -60 767'
Invoke-Teleport '/tp @s 1725 -60 762 0 5'
Start-Sleep -Seconds 3
Save-Screenshot 'mist_wolf_dark' | Out-Null
Invoke-Chat '/kill @e[type=twilightforest:mist_wolf]'
Invoke-Chat '/fill 1720 -61 760 1730 -55 770 air'

# ---- against a survival player (protected): angry wolf, breaths, grab, projectiles ----
Invoke-Chat '/effect @s resistance 300 255 true'
Invoke-Chat '/effect @s fire_resistance 300 1 true'
Invoke-Chat '/effect @s regeneration 300 5 true'
Invoke-Chat '/difficulty normal'
$fights = @(@('hostile_wolf', '~ ~ ~4', 'hostile_wolf_angry', 2), @('winter_wolf', '~ ~ ~4', 'winter_wolf_breath', 6),
    @('fire_beetle', '~ ~ ~3', 'fire_beetle_breath', 6), @('skeleton_druid', '~ ~ ~5', 'druid_bolt', 4),
    @('slime_beetle', '~ ~ ~5', 'slime_blob', 4), @('pinch_beetle', '~ ~ ~4', 'pinch_beetle_open', 6))
foreach ($f in $fights) {
    Invoke-Teleport '/tp @s 1740 -60 780 0 10'
    Invoke-Chat '/gamemode survival'
    Invoke-Chat "/summon twilightforest:$($f[0]) $($f[1]) 180"
    # the breathers breathe at whoever hurt them: a hit from the player starts it
    if ($f[0] -eq 'winter_wolf' -or $f[0] -eq 'fire_beetle') { Invoke-Chat "/damage @e[type=twilightforest:$($f[0])] 1 entity_attack entity @s" }
    Start-Sleep -Seconds $f[3]
    Save-Screenshot $f[2] | Out-Null
    Invoke-Chat '/gamemode creative'
    Invoke-Chat "/kill @e[type=twilightforest:$($f[0])]"
    Start-Sleep -Seconds 1
}
Invoke-Chat '/difficulty easy'
Invoke-Chat '/effect @s clear'
