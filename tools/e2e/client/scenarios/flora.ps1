# Flora (U6) on the client: post-merge acceptance (BeDD). Builds a garden of every flora block on a
# grass platform with commands and photographs it by day and by night, the critters (firefly glow and
# particles, cicada notes, the moonworm's light) over a few seconds, the jars (firefly jar sparkles,
# cicada jar notes, the firefly particle spawner's wandering fireflies), the bushes in all four ages
# and with snow, the fallen leaves piles and the huge lily pad on water, then runs the server's
# flora selftests on the integrated server.
# Compare with TF's textures (textures/block/*berry*, *oreberry*, fallen_leaves, moss/clover patch,
# mayapple, fiddlehead, mushgloom, root(s), liveroot, torchberry_plant, huge_lily_pad, jar_*,
# entity/firefly-tiny, cicada-model, moonworm) and TF's critter models.
# remote-vanilla: the same garden from a vanilla client (static critter models, no client particles).
# Screenshots: out/flora*/{garden-day,garden-night,bushes,critters-1,critters-2,jars-1,jars-2,spawner,leaves,lily,selftest}.png
Save-Screenshot 'hud' | Out-Null
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
$x = 1000; $y = 100; $z = 1000
Invoke-Chat '/gamemode creative'
Invoke-Chat '/weather clear'
Invoke-Chat '/time set 6000'
Invoke-Teleport "/tp @s $($x + 8) $($y + 6) $($z - 8) 0 30"
Start-Sleep -Seconds 3
Invoke-Chat "/fill $($x - 2) $($y - 1) $($z - 2) $($x + 22) $($y - 1) $($z + 14) grass_block"
Invoke-Chat "/fill $($x - 2) $y $($z - 2) $($x + 22) $($y + 6) $($z + 14) air"
# row 0: the berry bushes, ages 0..3
$bushes = 'raspberry_bush', 'blueberry_bush', 'blackberry_bush', 'maloberry_bush'
for ($i = 0; $i -lt $bushes.Count; $i++) {
    for ($a = 0; $a -le 3; $a++) { Invoke-Chat "/setblock $($x + $i * 5 + $a) $y $z twilightforest:$($bushes[$i]) [`"twilightforest:age`"=$a]" }
}
# row 2: dark tower bushes on netherrack, oreberries on stone (ripe), one snowy bush
$dark = 'blightberry_bush', 'duskberry_bush', 'skyberry_bush', 'stingberry_bush'
for ($i = 0; $i -lt $dark.Count; $i++) {
    Invoke-Chat "/setblock $($x + $i * 2) $($y - 1) $($z + 2) netherrack"
    Invoke-Chat "/setblock $($x + $i * 2) $y $($z + 2) twilightforest:$($dark[$i]) [`"twilightforest:age`"=3]"
}
$ore = 'iron_oreberry', 'gold_oreberry', 'copper_oreberry', 'essence_oreberry'
for ($i = 0; $i -lt $ore.Count; $i++) {
    Invoke-Chat "/setblock $($x + 10 + $i * 2) $($y - 1) $($z + 2) stone"
    Invoke-Chat "/setblock $($x + 10 + $i * 2) $y $($z + 2) twilightforest:$($ore[$i]) [`"twilightforest:age`"=3]"
}
Invoke-Chat "/setblock $($x + 20) $y $($z + 2) twilightforest:raspberry_bush [`"twilightforest:age`"=1,`"twilightforest:layers`"=3]"
# row 4: ground plants
$plants = 'moss_patch', 'clover_patch', 'mayapple', 'fiddlehead', 'mushgloom', 'potted_mayapple', 'potted_fiddlehead', 'potted_mushgloom', 'liveroot_block', 'root', 'huge_water_lily'
for ($i = 0; $i -lt $plants.Count; $i++) { Invoke-Chat "/setblock $($x + $i * 2) $y $($z + 4) twilightforest:$($plants[$i])" }
for ($l = 1; $l -le 8; $l++) { Invoke-Chat "/setblock $($x + $l - 1) $y $($z + 6) twilightforest:fallen_leaves [`"twilightforest:layers`"=$l]" }
# hanging plants under a dirt beam
Invoke-Chat "/fill $($x + 10) $($y + 3) $($z + 6) $($x + 14) $($y + 3) $($z + 6) dirt"
Invoke-Chat "/setblock $($x + 10) $($y + 2) $($z + 6) twilightforest:root_strand"
Invoke-Chat "/setblock $($x + 12) $($y + 2) $($z + 6) twilightforest:torchberry_plant [`"twilightforest:has_torchberries`"=true]"
Invoke-Chat "/setblock $($x + 14) $($y + 2) $($z + 6) twilightforest:torchberry_plant [`"twilightforest:has_torchberries`"=false]"
# row 8: critters (floor, wall, ceiling) and jars
$critters = 'firefly', 'cicada', 'moonworm'
for ($i = 0; $i -lt $critters.Count; $i++) {
    Invoke-Chat "/setblock $($x + $i * 3) $y $($z + 8) twilightforest:$($critters[$i]) [`"twilightforest:facing`"=`"up`"]"
    Invoke-Chat "/setblock $($x + $i * 3 + 1) $y $($z + 9) stone"
    Invoke-Chat "/setblock $($x + $i * 3 + 1) $y $($z + 8) twilightforest:$($critters[$i]) [`"twilightforest:facing`"=`"north`"]"
}
$jars = 'firefly_jar', 'cicada_jar', 'mason_jar', 'firefly_particle_spawner'
for ($i = 0; $i -lt $jars.Count; $i++) { Invoke-Chat "/setblock $($x + 12 + $i * 2) $y $($z + 8) twilightforest:$($jars[$i])" }
# the huge lily pad on a pool
Invoke-Chat "/fill $($x + 18) $($y - 1) $($z + 10) $($x + 21) $($y - 1) $($z + 13) water"
foreach ($p in @(@(0, 0, 'nw'), @(1, 0, 'ne'), @(0, 1, 'sw'), @(1, 1, 'se'))) {
    Invoke-Chat "/setblock $($x + 19 + $p[0]) $y $($z + 11 + $p[1]) twilightforest:huge_lily_pad [`"twilightforest:facing`"=`"north`",`"twilightforest:piece`"=`"$($p[2])`"]"
}
Start-Sleep -Seconds 3
Invoke-Teleport "/tp @s $($x + 10) $($y + 9) $($z - 7) 0 45"
Start-Sleep -Seconds 3
Save-Screenshot 'garden-day' | Out-Null
Invoke-Teleport "/tp @s $($x + 8) $($y + 3) $($z - 2) 0 30"
Start-Sleep -Seconds 2
Save-Screenshot 'bushes' | Out-Null
Invoke-Chat '/time set 18000'
Invoke-Teleport "/tp @s $($x + 10) $($y + 9) $($z - 7) 0 45"
Start-Sleep -Seconds 3
Save-Screenshot 'garden-night' | Out-Null
Invoke-Teleport "/tp @s $($x + 4) $($y + 2) $($z + 5) 0 30"
Start-Sleep -Seconds 2
Save-Screenshot 'critters-1' | Out-Null
Start-Sleep -Seconds 3
Save-Screenshot 'critters-2' | Out-Null
Invoke-Teleport "/tp @s $($x + 15) $($y + 2) $($z + 5) 0 30"
Start-Sleep -Seconds 2
Save-Screenshot 'jars-1' | Out-Null
Start-Sleep -Seconds 2
Save-Screenshot 'jars-2' | Out-Null
Invoke-Teleport "/tp @s $($x + 18) $($y + 3) $($z + 6) 0 30"
Start-Sleep -Seconds 2
Save-Screenshot 'spawner' | Out-Null
Invoke-Chat '/time set 6000'
Invoke-Teleport "/tp @s $($x + 4) $($y + 3) $($z + 3) 0 40"
Start-Sleep -Seconds 2
Save-Screenshot 'leaves' | Out-Null
Invoke-Teleport "/tp @s $($x + 20) $($y + 4) $($z + 8) 0 50"
Start-Sleep -Seconds 2
Save-Screenshot 'lily' | Out-Null
if ($script:Mode -eq 'local') {
    $r = Invoke-SelfTest 'flora.*'
    Save-Screenshot 'selftest' | Out-Null
    if ($r.Fail -gt 0) { throw "$($r.Fail) flora selftest(s) failed on the integrated server" }
}
Invoke-Chat "/fill $($x - 2) $($y - 1) $($z - 2) $($x + 22) $($y + 6) $($z + 14) air"
Write-Step 'flora garden photographed'
