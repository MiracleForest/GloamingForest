# Final castle (U30) on the client: the unit's selftests pass in the local world (integrated server),
# then every block, mob and the castle itself are put in front of the camera for the screenshot review
# against upstream's textures, models and GUIs (%LOCALAPPDATA%\gf-upstream\view):
#   door_<colour>           each castle door alone, a 3 x 3 wall of it, and the wall after a use
#                           (vanished: the rune flow through the frame)
#   field_<colour>          each force field as a vertical post (up / down), a flat wall and an L corner
#   thorns_<kind>           brown, green and burnt thorns on the three axes, plus a 3 x 3 x 3 cluster
#   thorn_plants            the thorn rose (up, north), thorn leaves and the three potted thorns
#   cinder                  the cinder furnace unlit / lit, the cinder log on its three axes, cinder wood
#   thornlands              (local) the thornlands around the final plateau of the castle site the selftests
#                           located (their "final castle site chunk" line), thorns grown by the feature
#   mob_<name>              the adherent (hovering), the harbinger cube (walking), the roving cube
#                           (spinning, particles) and the final boss spawner
#   gazebo_wip              the gazebo template at the player's feet with its four "Final Castle WIP."
#                           lines, then gazebo_punched after punching the box (the lines are gone)
#   castle_outside          a final castle placed with /gloamingforest place: keep, maze towers, rune doors and the
#                           violet force-field cage on the roof; castle_door_open: the keep's yellow door
#                           a moment after a use (rune flow)
# Modes: local runs the selftests on the integrated server first; remote-mod and remote-vanilla join the
# BDS host and take the same shots (vanilla clients see the WIP lines as name tags).
# Screenshots: out/finalcastle/*.png
Save-Screenshot 'hud' | Out-Null
$remote = $script:Mode -ne 'local'
if ($remote) {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
    if ($script:Mode -eq 'remote-mod') { Wait-LogLine '\[host\].*companion joined' 60 | Out-Null }
} else {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $mark = Get-LogMark
    $r = Invoke-SelfTest 'finalcastle.*' -TimeoutSeconds 1500
    if ($r.Fail -gt 0) { throw "$($r.Fail) finalcastle selftest(s) failed" }
    if ($r.Pass -lt 21) { throw "expected 21 finalcastle selftests to pass, got $($r.Pass)" }
}

Invoke-Chat '/gamemode creative'
Invoke-Chat '/difficulty peaceful'
Invoke-Chat '/gamerule dofiretick false'
Invoke-Chat '/gamerule domobspawning false'
Invoke-Chat '/effect @s night_vision 99999 1 true'
Invoke-Chat '/gamerule doDaylightCycle false'
Invoke-Chat '/time set noon'
Invoke-Teleport '/tp @s 600 -60 600'
Start-Sleep -Seconds 2

function Clear-Stage {
    Invoke-Chat '/fill 594 -60 598 614 -48 614 air'
    Invoke-Chat '/fill 594 -61 598 614 -61 614 minecraft:smooth_stone'
}
# looks north at the stage from 7 blocks south, a little above it
function Save-Stage([string]$name) {
    Invoke-Teleport '/tp @s 604.5 -58 613.5 180 18'
    Start-Sleep -Seconds 3
    Save-Screenshot $name | Out-Null
}

# ---- castle doors: alone, a 3 x 3 wall, the wall vanished after a use ----
foreach ($c in 'yellow', 'violet', 'pink', 'blue') {
    Clear-Stage
    $door = "gloamingforest:${c}_castle_door"
    Invoke-Chat "/setblock 600 -60 606 $door"
    Invoke-Chat "/fill 604 -60 606 606 -58 606 $door"
    Invoke-Chat "/setblock 609 -60 606 gloamingforest:${c}_castle_rune_brick"
    Save-Stage "door_$c"
    # use the middle of the wall: it vanishes after 2-6 ticks, the whole wall with it (the flow)
    Invoke-Teleport '/tp @s 605.5 -60 609.5 180 8'
    Start-Sleep -Milliseconds 800
    Click 0.5 0.5 -Relative -Right
    Start-Sleep -Milliseconds 600
    Save-Screenshot "door_${c}_vanished" | Out-Null
}

# ---- force fields: a post (up / down), a flat wall and an L corner of each colour ----
foreach ($c in 'violet', 'pink', 'orange', 'green', 'blue') {
    Clear-Stage
    $f = "gloamingforest:${c}_force_field"
    Invoke-Chat "/fill 599 -60 606 599 -57 606 $f [`"gloamingforest:up`"=true,`"gloamingforest:down`"=true]"
    Invoke-Chat "/fill 602 -60 606 605 -57 606 $f [`"gloamingforest:east`"=true,`"gloamingforest:west`"=true,`"gloamingforest:up`"=true,`"gloamingforest:down`"=true]"
    Invoke-Chat "/fill 608 -60 606 610 -57 606 $f [`"gloamingforest:east`"=true,`"gloamingforest:west`"=true,`"gloamingforest:up`"=true,`"gloamingforest:down`"=true]"
    Invoke-Chat "/fill 608 -60 604 608 -57 605 $f [`"gloamingforest:north`"=true,`"gloamingforest:south`"=true,`"gloamingforest:up`"=true,`"gloamingforest:down`"=true]"
    Invoke-Chat "/fill 608 -60 606 608 -57 606 $f [`"gloamingforest:north`"=true,`"gloamingforest:east`"=true,`"gloamingforest:up`"=true,`"gloamingforest:down`"=true]"
    Save-Stage "field_$c"
}

# ---- thorns: the three axes and a cluster of each kind ----
foreach ($k in 'brown', 'green', 'burnt') {
    Clear-Stage
    $t = "gloamingforest:${k}_thorns"
    Invoke-Chat "/setblock 599 -60 606 $t [`"gloamingforest:axis`"=`"y`"]"
    Invoke-Chat "/setblock 601 -60 606 $t [`"gloamingforest:axis`"=`"x`"]"
    Invoke-Chat "/setblock 603 -60 606 $t [`"gloamingforest:axis`"=`"z`"]"
    Invoke-Chat "/fill 606 -60 605 608 -58 607 $t"
    Save-Stage "thorns_$k"
}

# ---- thorn plants: rose up / north, thorn leaves, the potted thorns ----
Clear-Stage
Invoke-Chat '/setblock 599 -60 606 gloamingforest:thorn_rose ["gloamingforest:facing"="up"]'
Invoke-Chat '/setblock 601 -59 607 gloamingforest:brown_thorns'
Invoke-Chat '/setblock 601 -59 606 gloamingforest:thorn_rose ["gloamingforest:facing"="north"]'
Invoke-Chat '/setblock 603 -60 606 gloamingforest:thorn_leaves ["gloamingforest:persistent"=true]'
Invoke-Chat '/setblock 605 -60 606 gloamingforest:potted_thorn'
Invoke-Chat '/setblock 607 -60 606 gloamingforest:potted_green_thorn'
Invoke-Chat '/setblock 609 -60 606 gloamingforest:potted_dead_thorn'
Save-Stage 'thorn_plants'

# ---- cinder furnace unlit / lit, cinder log on three axes, cinder wood ----
Clear-Stage
Invoke-Chat '/setblock 599 -60 606 gloamingforest:cinder_furnace ["gloamingforest:facing"="south"]'
Invoke-Chat '/setblock 601 -60 606 gloamingforest:cinder_furnace ["gloamingforest:facing"="south","gloamingforest:lit"=true]'
Invoke-Chat '/setblock 603 -60 606 gloamingforest:cinder_log ["gloamingforest:axis"="y"]'
Invoke-Chat '/setblock 605 -60 606 gloamingforest:cinder_log ["gloamingforest:axis"="x"]'
Invoke-Chat '/setblock 607 -60 606 gloamingforest:cinder_log ["gloamingforest:axis"="z"]'
Invoke-Chat '/setblock 609 -60 606 gloamingforest:cinder_wood'
Save-Stage 'cinder'
Clear-Stage

# ---- mobs, each alone in front of the camera ----
Invoke-Teleport '/tp @s 610 -60 640 0 8'
Start-Sleep -Seconds 2
foreach ($m in @(@('adherent', '~ ~1 ~4'), @('harbinger_cube', '~ ~ ~5'), @('roving_cube', '~ ~ ~4'))) {
    Invoke-Teleport '/tp @s 610 -60 640 0 8'
    Invoke-Chat "/summon gloamingforest:$($m[0]) $($m[1])"
    Start-Sleep -Milliseconds 1500
    Save-Screenshot "mob_$($m[0])" | Out-Null
    Invoke-Chat "/kill @e[type=gloamingforest:$($m[0])]"
}
Invoke-Chat '/setblock 610 -60 644 gloamingforest:final_boss_boss_spawner'
Invoke-Teleport '/tp @s 610 -59 640 0 10'
Start-Sleep -Seconds 2
Save-Screenshot 'mob_final_boss_spawner' | Out-Null
Invoke-Chat '/setblock 610 -60 644 air'

# ---- the gazebo with its WIP lines, then punched ----
Invoke-Teleport '/tp @s 700 -60 600'
Start-Sleep -Seconds 3
Invoke-Chat '/gloamingforest structure_util place gloamingforest:final_castle/temp/gazebo'
Start-Sleep -Seconds 4
# the gazebo's centre is 10 blocks east and south of the player's feet: look at the lines from 4 blocks north
Invoke-Teleport '/tp @s 710.5 -59 606.5 0 -5'
Start-Sleep -Seconds 3
Save-Screenshot 'gazebo_wip' | Out-Null
Invoke-Teleport '/tp @s 710.5 -60 608.5 0 0'
Start-Sleep -Seconds 1
Click 0.5 0.5 -Relative
Start-Sleep -Seconds 2
Invoke-Teleport '/tp @s 710.5 -59 606.5 0 -5'
Start-Sleep -Seconds 2
Save-Screenshot 'gazebo_punched' | Out-Null

# ---- the castle: placed in loaded chunks, seen from outside; the keep's yellow door opened ----
Invoke-Teleport '/tp @s 900 40 900'
Start-Sleep -Seconds 6
Invoke-Chat '/gloamingforest place gloamingforest:final_castle 900 -60 900'
Start-Sleep -Seconds 20
Invoke-Teleport '/tp @s 990 100 990 135 15'
Start-Sleep -Seconds 8
Save-Screenshot 'castle_outside' | Out-Null
Invoke-Teleport '/tp @s 940 75 900 225 10'
Start-Sleep -Seconds 4
Save-Screenshot 'castle_keep_roof' | Out-Null
# the keep (SOUTH, local x 48 = world x + 24): the yellow door faces east at the keep's middle
Invoke-Teleport '/tp @s 927.5 62 916.5 90 0'
Start-Sleep -Seconds 3
Save-Screenshot 'castle_door' | Out-Null
Click 0.5 0.5 -Relative -Right
Start-Sleep -Milliseconds 700
Save-Screenshot 'castle_door_open' | Out-Null

# ---- (local) the thornlands around the final plateau the selftests located ----
if (-not $remote) {
    $site = Get-LeviLog -Since $mark | Select-String -Pattern 'final castle site chunk (-?\d+) (-?\d+)' | Select-Object -First 1
    if ($site) {
        $sx = [int]$site.Matches[0].Groups[1].Value * 16 + 7
        $sz = [int]$site.Matches[0].Groups[2].Value * 16 + 7
        $gloamingMark = Get-LogMark
        Invoke-Chat '/gloamingforest tp'
        Wait-LogLine 'dimension: .* travelled from minecraft:overworld to gloamingforest:gloaming_forest' 60 -Since $gloamingMark | Out-Null
        Start-Sleep -Seconds 4
        Invoke-Chat '/gamemode spectator'
        # the plateau is about 100 blocks around the site; the thornlands ring lies beyond it
        Invoke-Teleport "/tp @s $($sx + 150) 140 $sz 90 35"
        Start-Sleep -Seconds 10
        Save-Screenshot 'thornlands' | Out-Null
        Invoke-Teleport "/tp @s $($sx + 120) 200 $($sz + 120) 135 25"
        Start-Sleep -Seconds 10
        Save-Screenshot 'castle_natural' | Out-Null
        Invoke-Chat '/gamemode creative'
    }
}
