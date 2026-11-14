# Forest and hollow-hill hostiles (U17) on the client: the unit's selftests pass in the local world
# (integrated server), then each mob and projectile is put in front of the camera for the screenshot
# review against upstream's textures and models (%LOCALAPPDATA%\gf-upstream\view):
#   eggs                the 13 spawn eggs in the hotbar (upstream's colours)
#   mob_<name>          each mob alone at close range, in daylight
#   mist_wolf_dark      the mist wolf in a dark stone room (MistWolfRenderer: translucent and dim)
#   hostile_wolf_angry  a hostile wolf with a target (angry face, raised tail) against a survival player
#   slime_beetle_tail   the slime beetle from the side (the translucent slime tail)
#   pinch_beetle_open   a pinch beetle holding the survival player (open pincers)
#   winter_wolf_breath, fire_beetle_breath   the breath particles (snow, flames) at a survival player
#   druid_bolt, slime_blob   a druid's nature bolt and a slime beetle's blob in flight
#   king_spider_rider   the king spider with its skeleton druid
#   wraith              the wraith (alpha 0.6) in front of a stone wall
#   redcap_swing_<n>    a redcap swinging at a survival player, 3 frames (FixedHumanoidModel's attack:
#                       the body turn and the arms moving with it, animation.gloamingforest.redcap.attack_arms)
#   anim_<name>         A5 animation review: every living upstream mob (bosses aside) moved across the camera
#                       a block a step with /tp, then standing: the walk and idle poses against upstream's
#                       setupAnim (bosses: their units' scenarios)
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
    if ($r.Pass -lt 16) { throw "expected 16 hostiles selftests to pass, got $($r.Pass)" }
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
foreach ($e in $eggs) { Invoke-Chat "/give @s gloamingforest:$($e)_spawn_egg" }
Invoke-Teleport '/tp @s 1700 -60 700 0 5'
Start-Sleep -Seconds 2
Save-Screenshot 'eggs_1' | Out-Null
Invoke-Chat '/clear @s'
foreach ($e in @('wraith', 'fire_beetle', 'slime_beetle', 'pinch_beetle')) { Invoke-Chat "/give @s gloamingforest:$($e)_spawn_egg" }
Start-Sleep -Seconds 1
Save-Screenshot 'eggs_2' | Out-Null
Invoke-Chat '/clear @s'

# ---- each mob alone (creative: nothing targets the player), summoned south of the camera ----
$mobs = @(@('hostile_wolf', '~ ~ ~3'), @('mist_wolf', '~ ~ ~5'), @('winter_wolf', '~ ~ ~5'), @('hedge_spider', '~ ~ ~4'),
    @('swarm_spider', '~ ~ ~2'), @('skeleton_druid', '~ ~ ~4'), @('redcap', '~ ~ ~3'), @('redcap_sapper', '~ ~ ~3'),
    @('fire_beetle', '~ ~ ~3'), @('slime_beetle', '~ ~ ~3'), @('pinch_beetle', '~ ~ ~3'))
foreach ($m in $mobs) {
    Invoke-Teleport '/tp @s 1710 -60 740 0 10'
    Invoke-Chat "/summon gloamingforest:$($m[0]) $($m[1])"
    Start-Sleep -Milliseconds 1500
    Save-Screenshot "mob_$($m[0])" | Out-Null
    Invoke-Chat "/kill @e[type=gloamingforest:$($m[0])]"
}

# the king spider and its druid rider
Invoke-Teleport '/tp @s 1710 -60 740 0 0'
Invoke-Chat '/summon gloamingforest:king_spider ~ ~ ~7'
Start-Sleep -Seconds 2
Save-Screenshot 'king_spider_rider' | Out-Null
Invoke-Chat '/kill @e[type=gloamingforest:king_spider]'
Invoke-Chat '/kill @e[type=gloamingforest:skeleton_druid]'

# the slime beetle from the side: the translucent tail
Invoke-Teleport '/tp @s 1710 -60 740 0 20'
Invoke-Chat '/summon gloamingforest:slime_beetle ~2 ~ ~2 90'
Start-Sleep -Seconds 2
Save-Screenshot 'slime_beetle_tail' | Out-Null
Invoke-Chat '/kill @e[type=gloamingforest:slime_beetle]'

# the wraith in front of a stone wall
Invoke-Chat '/fill 1706 -60 748 1714 -55 748 stone'
Invoke-Teleport '/tp @s 1710 -60 740 0 0'
Invoke-Chat '/summon gloamingforest:wraith ~ ~ ~4'
Start-Sleep -Seconds 2
Save-Screenshot 'wraith' | Out-Null
Invoke-Chat '/kill @e[type=gloamingforest:wraith]'
Invoke-Chat '/fill 1706 -60 748 1714 -55 748 air'

# ---- the mist wolf in the dark (a closed stone room, the camera inside) ----
Invoke-Chat '/fill 1720 -61 760 1730 -55 770 stone hollow'
Invoke-Chat '/summon gloamingforest:mist_wolf 1725 -60 767'
Invoke-Teleport '/tp @s 1725 -60 762 0 5'
Start-Sleep -Seconds 3
Save-Screenshot 'mist_wolf_dark' | Out-Null
Invoke-Chat '/kill @e[type=gloamingforest:mist_wolf]'
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
    Invoke-Chat "/summon gloamingforest:$($f[0]) $($f[1]) 180"
    # the breathers breathe at whoever hurt them: a hit from the player starts it
    if ($f[0] -eq 'winter_wolf' -or $f[0] -eq 'fire_beetle') { Invoke-Chat "/damage @e[type=gloamingforest:$($f[0])] 1 entity_attack entity @s" }
    Start-Sleep -Seconds $f[3]
    Save-Screenshot $f[2] | Out-Null
    Invoke-Chat '/gamemode creative'
    Invoke-Chat "/kill @e[type=gloamingforest:$($f[0])]"
    Start-Sleep -Seconds 1
}

# ---- the redcap's attack swing (three frames of one swing) ----
Invoke-Teleport '/tp @s 1740 -60 780 0 10'
Invoke-Chat '/gamemode survival'
Invoke-Chat '/summon gloamingforest:redcap ~ ~ ~2 180'
Start-Sleep -Seconds 2
foreach ($n in 1..3) {
    Save-Screenshot "redcap_swing_$n" | Out-Null
    Start-Sleep -Milliseconds 100
}
Invoke-Chat '/gamemode creative'
Invoke-Chat '/kill @e[type=gloamingforest:redcap]'
Invoke-Chat '/difficulty easy'
Invoke-Chat '/effect @s clear'

# ---- A5 animation review: every living upstream mob walking past the camera (creative: no fights) ----
# the summoned mob is moved one block a step across the view so its walk animation plays
$living = @('adherent', 'alpha_yeti', 'armored_giant', 'bighorn_sheep', 'blockchain_goblin', 'boar', 'carminite_broodling',
    'carminite_ghastguard', 'carminite_ghastling', 'carminite_golem', 'death_tome', 'deer', 'dwarf_rabbit', 'fire_beetle',
    'giant_miner', 'harbinger_cube', 'hedge_spider', 'helmet_crab', 'hostile_wolf', 'ice_crystal', 'king_spider', 'kobold',
    'lich_minion', 'lower_goblin_knight', 'loyal_zombie', 'maze_slime', 'minotaur', 'mist_wolf', 'mosquito_swarm', 'penguin',
    'pinch_beetle', 'quest_ram', 'raven', 'redcap', 'redcap_sapper', 'rising_zombie', 'roving_cube', 'skeleton_druid',
    'slime_beetle', 'snow_guardian', 'squirrel', 'stable_ice_core', 'swarm_spider', 'tiny_bird', 'towerwood_borer', 'troll',
    'unstable_ice_core', 'upper_goblin_knight', 'winter_wolf', 'wraith', 'yeti')
Invoke-Chat '/difficulty easy'
foreach ($m in $living) {
    Invoke-Teleport '/tp @s 1760 -60 800 0 10'
    Invoke-Chat "/summon gloamingforest:$m 1757 -60 806"
    Start-Sleep -Milliseconds 500
    foreach ($step in 0..5) {
        Invoke-Chat "/tp @e[type=gloamingforest:$m] $(1757 + $step) -60 806"
        Start-Sleep -Milliseconds 150
    }
    Save-Screenshot "anim_$m" | Out-Null
    Start-Sleep -Milliseconds 400
    Save-Screenshot "anim_$($m)_idle" | Out-Null
    Invoke-Chat "/kill @e[type=gloamingforest:$m]"
}
